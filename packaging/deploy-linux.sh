#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"

BUILD_ROOT="${OPENTM_BUILD_ROOT:-$ROOT/build}"
DIST="${OPENTM_DIST_DIR:-$ROOT/dist}"

WANT_APPIMAGE=1
WANT_TARBALL=1
VERSION=""
JOBS="$(command -v nproc >/dev/null 2>&1 && nproc || echo 4)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-appimage) WANT_APPIMAGE=0; shift;;
        --no-tarball) WANT_TARBALL=0; shift;;
        --version) VERSION="$2"; shift 2;;
        --jobs) JOBS="$2"; shift 2;;
        --build-root) BUILD_ROOT="$2"; shift 2;;
        --dist) DIST="$2"; shift 2;;
        *) echo "deploy-linux.sh: unknown argument: $1" >&2; exit 2;;
    esac
done

ARCH="$(uname -m)"

mkdir -p "$BUILD_ROOT"
if ! ln -sfn . "$BUILD_ROOT/.symlink-probe" 2>/dev/null; then
    FALLBACK="${TMPDIR:-/tmp}/opentm-build"
    echo "[deploy] ${BUILD_ROOT} cannot hold symlinks (exfat/NTFS?)."
    echo "[deploy] building in ${FALLBACK} instead; artifacts still land in ${DIST}."
    BUILD_ROOT="$FALLBACK"
    mkdir -p "$BUILD_ROOT"
    if ! ln -sfn . "$BUILD_ROOT/.symlink-probe" 2>/dev/null; then
        echo "[deploy] ERROR: ${BUILD_ROOT} cannot hold symlinks either." >&2
        echo "[deploy] Pass --build-root with a path on a normal filesystem." >&2
        exit 1
    fi
fi
rm -f "$BUILD_ROOT/.symlink-probe"

BUILD_DIR="$BUILD_ROOT/release-bundle"
APPDIR="$BUILD_ROOT/AppDir"
CACHE="$BUILD_ROOT/.deploy-tools"

if [[ -z "$VERSION" ]]; then
    if git -C "$ROOT" describe --tags --exact-match >/dev/null 2>&1; then
        VERSION="$(git -C "$ROOT" describe --tags --exact-match | sed 's/^v//')"
    else
        VERSION="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\+\([0-9][^[:space:]]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
        if git -C "$ROOT" rev-parse --short HEAD >/dev/null 2>&1; then
            VERSION="${VERSION}+$(git -C "$ROOT" rev-parse --short HEAD)"
        fi
    fi
fi

echo "[deploy] version ${VERSION}, arch ${ARCH}, jobs ${JOBS}"

echo "[deploy] configuring"
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DOPENTM_BUILD_TESTS=OFF

echo "[deploy] building"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

rm -rf "$APPDIR"
echo "[deploy] installing to AppDir"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr --strip

mkdir -p "$CACHE"
fetch_tool() {
    local name="$1" url="$2"
    if [[ ! -x "$CACHE/$name" ]]; then
        echo "[deploy] downloading $name"
        curl -fsSL -o "$CACHE/$name" "$url"
        chmod +x "$CACHE/$name"
    fi
}
LD_BASE="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous"
LDQ_BASE="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous"
fetch_tool "linuxdeploy-${ARCH}.AppImage"           "${LD_BASE}/linuxdeploy-${ARCH}.AppImage"
fetch_tool "linuxdeploy-plugin-qt-${ARCH}.AppImage" "${LDQ_BASE}/linuxdeploy-plugin-qt-${ARCH}.AppImage"

export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$CACHE:$PATH"

if [[ -z "${QMAKE:-}" ]]; then
    QMAKE="$(command -v qmake6 || command -v qmake || true)"
    [[ -n "$QMAKE" ]] && export QMAKE
fi
echo "[deploy] qmake: ${QMAKE:-<not found, plugin will guess>}"

export EXTRA_QT_PLUGINS="platforms;imageformats;iconengines;tls;networkinformation;xcbglintegrations;wayland-decoration-client;wayland-graphics-integration-client;wayland-shell-integration"
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so;libqoffscreen.so;libqminimal.so"

echo "[deploy] bundling Qt and dependencies"
"$CACHE/linuxdeploy-${ARCH}.AppImage" \
    --appdir "$APPDIR" \
    -e "$APPDIR/usr/bin/opentm_app" \
    -e "$APPDIR/usr/bin/opentm_cli" \
    -e "$APPDIR/usr/bin/opentm_server" \
    -e "$APPDIR/usr/bin/opentm_tray" \
    -d "$APPDIR/usr/share/applications/opentm.desktop" \
    -i "$APPDIR/usr/share/icons/hicolor/256x256/apps/opentm.png" \
    --plugin qt

mkdir -p "$DIST"

# ----- tarball -----
if [[ "$WANT_TARBALL" == "1" ]]; then
    STAGE="$BUILD_ROOT/tarball/opentm-${VERSION}-linux-${ARCH}"
    rm -rf "$BUILD_ROOT/tarball"
    mkdir -p "$STAGE"

    cp -a "$APPDIR/usr/." "$STAGE/"

    rm -f "$STAGE/bin/AppRun"

    cat > "$STAGE/README.txt" <<EOF
OpenTM ${VERSION} (linux-${ARCH})

Run bin/opentm_app. Qt is bundled in lib/ and plugins/, so nothing needs
installing and nothing needs to be on LD_LIBRARY_PATH.

Keep bin/ intact: opentm_app starts opentm_tray and opentm_server as
siblings, and all four binaries have to stay in the same directory.

To get a menu entry, copy share/applications/opentm.desktop into
~/.local/share/applications and edit Exec= to the full path of
bin/opentm_app.
EOF

    echo "[deploy] writing tarball"
    tar -C "$BUILD_ROOT/tarball" -czf \
        "$DIST/opentm-${VERSION}-linux-${ARCH}.tar.gz" \
        "opentm-${VERSION}-linux-${ARCH}"
fi

if [[ "$WANT_APPIMAGE" == "1" ]]; then
    echo "[deploy] writing AppImage"
    OUTPUT="OpenTM-${VERSION}-${ARCH}.AppImage" \
    "$CACHE/linuxdeploy-${ARCH}.AppImage" \
        --appdir "$APPDIR" \
        -d "$APPDIR/usr/share/applications/opentm.desktop" \
        -i "$APPDIR/usr/share/icons/hicolor/256x256/apps/opentm.png" \
        --output appimage
    mv "OpenTM-${VERSION}-${ARCH}.AppImage" "$DIST/"
fi

echo "[deploy] writing checksums"
( cd "$DIST" && sha256sum ./*.tar.gz./*.AppImage 2>/dev/null > SHA256SUMS ) || true

echo "[deploy] done:"
ls -lh "$DIST"

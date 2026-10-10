#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
else
    JOBS=8
fi
echo "[build.sh] parallel jobs: ${JOBS}"

CONFIG="Debug"
PRESET="linux-debug"
RUN_AFTER=0
DO_CLEAN=0

for arg in "$@"; do
    case "${arg,,}" in
        release) CONFIG="Release"; PRESET="linux-release";;
        debug)   CONFIG="Debug"; PRESET="linux-debug";;
        vcpkg)   PRESET="linux-debug-vcpkg" ;;
        run)     RUN_AFTER=1 ;;
        clean)   DO_CLEAN=1 ;;
        *)       echo "[build.sh] unknown arg: ${arg}"; exit 2;;
    esac
done

if [[ "${PRESET}" == "linux-debug-vcpkg" && -z "${VCPKG_ROOT:-}" ]]; then
    echo "[build.sh] ERROR: vcpkg preset selected but VCPKG_ROOT is not set."
    exit 1
fi
 
if [[ "${DO_CLEAN}" == "1" && -d build ]]; then
    echo "[build.sh] removing build/"
    rm -rf build
fi

echo "[build.sh] cmake --preset ${PRESET}"
cmake --preset "${PRESET}"

echo "[build.sh] cmake --build --preset ${PRESET} --parallel ${JOBS}"
cmake --build --preset "${PRESET}" --parallel "${JOBS}"

echo "[build.sh] ctest --preset ${PRESET} --parallel ${JOBS}"
ctest --preset "${PRESET}" --parallel "${JOBS}" || true

if [[ "${RUN_AFTER}" == "1" ]]; then
    EXE="build/${PRESET}/bin/opentm_app"
    if [[ -x "${EXE}" ]]; then
        echo "[build.sh] launching ${EXE}"
        "${EXE}" &
    else
        echo "[build.sh] WARNING: ${EXE} not found"
        exit 3
    fi
fi

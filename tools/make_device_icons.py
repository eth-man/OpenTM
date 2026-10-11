import colorsys
import pathlib
import struct

ICONS = pathlib.Path(__file__).resolve().parent.parent / "resources" / "icons"
SOURCE = ICONS / "drive.bmp"

TINTS = {
    "drive_usb":   (205, 0.85),   # blue
    "drive_ms":    (280, 0.70),   # violet - Memory Stick / Duo
    "drive_sd":    (115, 0.70),   # green - SD / SDHC
    "drive_cf":    (28,  0.85),   # orange - CompactFlash
    "drive_flash": (0,   0.75),   # red - internal /dev_flash*
}


def colorize(data: bytes, hue_deg: float, sat: float) -> bytearray:
    """Recolour opaque pixels to `hue`, preserving per-pixel lightness."""
    out = bytearray(data)
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    for i in range(w * abs(h)):
        p = off + i * 4
        b, g, r, a = data[p], data[p + 1], data[p + 2], data[p + 3]
        if a == 0:
            continue
        lightness = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0
        nr, ng, nb = colorsys.hls_to_rgb(hue_deg / 360.0, lightness, sat)
        out[p]     = round(nb * 255)
        out[p + 1] = round(ng * 255)
        out[p + 2] = round(nr * 255)
    return out


def main() -> None:
    src = SOURCE.read_bytes()
    for name, (hue, sat) in TINTS.items():
        dest = ICONS / f"{name}.bmp"
        dest.write_bytes(colorize(src, hue, sat))
        print(f"wrote {dest.relative_to(ICONS.parent.parent)}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Erzeugt src/media/images_cyd_z.h: die Hintergrundbilder des CYD-Treibers (ESP32_2432S028R/_2USB)
als zlib/Deflate-komprimierte RGB565-Daten.

Unkomprimiert belegen die Bilder ~805 KB Flash, komprimiert ~90 KB. Erst dadurch passt die Firmware
zweimal in den Flash (zwei OTA-Slots -> Update per WLAN). Dekomprimiert wird zeilenweise mit dem
tinfl-Decoder aus dem ESP32-ROM (siehe src/drivers/displays/zImage.cpp).

Aufruf (aus dem Projektverzeichnis): python tools/compress_images.py
"""
import re
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = [ROOT / "src/media/images_320_170.h", ROOT / "src/media/images_bottom_320_70.h"]
OUTPUT = ROOT / "src/media/images_cyd_z.h"

# Name im Quell-Header -> (Breite, Höhe) der tatsächlich genutzten Pixel
IMAGES = {
    "initScreen": (320, 170),
    "setupModeScreen": (320, 170),
    "MinerScreen": (320, 170),
    "minerClockScreen": (320, 170),
    "globalHashScreen": (320, 170),
    "priceScreen": (320, 170),
    "bottonPoolScreen": (320, 70),
    "bottonPoolScreen_g": (320, 70),
}

# 4-KB-Fenster: der Decoder braucht nur 4 KB RAM als Wörterbuch (statt 32 KB), kostet ~3 KB mehr Flash
WINDOW_BITS = 12


def read_arrays(path):
    src = re.sub(r"//[^\n]*", "", path.read_text(encoding="latin-1"))  # Kommentare enthalten auch Hex-Zahlen
    pattern = r"const unsigned short (\w+)\s*\[[^\]]*\]\s*PROGMEM\s*=\s*\{(.*?)\};"
    for m in re.finditer(pattern, src, re.S):
        yield m.group(1), [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", m.group(2))]


def main():
    arrays = {}
    for path in SOURCES:
        arrays.update(read_arrays(path))

    out = [
        "// Automatisch erzeugt von tools/compress_images.py - nicht von Hand bearbeiten.",
        "// zlib-komprimierte RGB565-Bilder für den CYD-Treiber (Decoder: drivers/displays/zImage.h).",
        "#pragma once",
        "",
        '#include "drivers/displays/zImage.h"',
        "",
    ]
    total_raw = total_z = 0
    for name, (w, h) in IMAGES.items():
        pixels = arrays[name][: w * h]
        if len(pixels) != w * h:
            raise SystemExit(f"{name}: {len(pixels)} Pixel statt {w * h}")
        raw = struct.pack("<%dH" % len(pixels), *pixels)
        comp = zlib.compressobj(9, zlib.DEFLATED, WINDOW_BITS, 9)
        data = comp.compress(raw) + comp.flush()
        # Rundlauf-Kontrolle
        if zlib.decompress(data) != raw:
            raise SystemExit(f"{name}: Rundlauf fehlgeschlagen")
        crc = zlib.crc32(raw) & 0xFFFFFFFF
        total_raw += len(raw)
        total_z += len(data)

        out.append(f"static const uint8_t {name}_zdata[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            out.append("  " + ", ".join(f"0x{b:02X}" for b in data[i : i + 24]) + ",")
        out.append("};")
        out.append(f"static const ZImage {name}_z = {{ {w}, {h}, sizeof({name}_zdata), {name}_zdata, 0x{crc:08X}u }};")
        out.append("")

    out.append("// Alle Bilder, z. B. für den Selbsttest beim Start")
    out.append("static const ZImage* const cydZImages[] = { " + ", ".join(f"&{n}_z" for n in IMAGES) + " };")
    out.append("static const char* const cydZImageNames[] = { " + ", ".join(f'"{n}"' for n in IMAGES) + " };")
    out.append("")
    OUTPUT.write_text("\n".join(out), encoding="utf-8", newline="\n")
    print(f"{OUTPUT.relative_to(ROOT)}: {len(IMAGES)} Bilder, {total_raw / 1024:.0f} KB -> {total_z / 1024:.0f} KB")


if __name__ == "__main__":
    main()

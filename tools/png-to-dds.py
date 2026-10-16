#!/usr/bin/env python3
# Afterglow - Converts a 3840x2160 PNG into the home-screen DDS (BC7, DX10).
# Copyright (C) 2026 DevvNirvana
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The kit prepares pic0.dds and pic1.dds with texconv on Windows
# (tools/prepare-assets.sh). This does the same on Linux with the etcpak
# BC7 encoder (pip install etcpak pillow): one level, no mipmaps, the layout
# tools/validate-assets.sh checks.
#
#   python3 tools/png-to-dds.py sce_sys/background-source.png sce_sys/pic0.dds

import struct
import sys

import etcpak
from PIL import Image

WIDTH, HEIGHT = 3840, 2160
DXGI_FORMAT_BC7_UNORM = 98
DDS_DIMENSION_TEXTURE2D = 3


def header(width, height, size):
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000  # caps, height, width, pixel format, linear size
    pixel_format = struct.pack("<II4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
    dds = struct.pack("<4sIIIIIII", b"DDS ", 124, flags, height, width, size, 0, 1)
    dds += b"\0" * 44 + pixel_format + struct.pack("<IIIII", 0x1000, 0, 0, 0, 0)
    dx10 = struct.pack("<IIIII", DXGI_FORMAT_BC7_UNORM, DDS_DIMENSION_TEXTURE2D, 0, 1, 0)
    return dds + dx10


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: png-to-dds.py <input.png> <output.dds>")
    image = Image.open(sys.argv[1]).convert("RGBA")
    if image.size != (WIDTH, HEIGHT):
        image = image.resize((WIDTH, HEIGHT), Image.LANCZOS)
    blocks = etcpak.compress_bc7(image.tobytes(), WIDTH, HEIGHT)
    expected = WIDTH // 4 * (HEIGHT // 4) * 16
    if len(blocks) != expected:
        raise SystemExit(f"encoder returned {len(blocks)} bytes, expected {expected}")
    with open(sys.argv[2], "wb") as out:
        out.write(header(WIDTH, HEIGHT, expected))
        out.write(blocks)
    print(f"wrote {sys.argv[2]}: {WIDTH}x{HEIGHT} BC7")


if __name__ == "__main__":
    main()

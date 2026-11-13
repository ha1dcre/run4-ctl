#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""pngpack.py SRC.png DST.png: recompress a PNG's pixel data (the simulator writes it uncompressed). Standard library only."""
import struct, sys, zlib

def chunks(b):
    i = 8
    while i < len(b):
        n, t = struct.unpack(">I4s", b[i:i + 8])
        yield t, b[i + 8:i + 8 + n]
        i += 12 + n

def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))

src = open(sys.argv[1], "rb").read()
assert src[:8] == b"\x89PNG\r\n\x1a\n"
hdr, idat = b"", b""
for t, d in chunks(src):
    if t == b"IHDR":
        hdr = d
    elif t == b"IDAT":
        idat += d
out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", hdr) + chunk(b"IDAT", zlib.compress(zlib.decompress(idat), 9)) + chunk(b"IEND", b"")
open(sys.argv[2], "wb").write(out)

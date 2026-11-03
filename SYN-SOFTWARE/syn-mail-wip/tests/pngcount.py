#!/usr/bin/env python3
# ------------------------------------------------------------------------
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-MAIL
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------
# Counts the pixels of one colour in a PNG (8-bit RGB or RGBA, not interlaced), for the HTML reader's tests (G265).
# Usage: pngcount.py FILE RRGGBB [X0 Y0 X1 Y1]; prints "WxH COUNT" (COUNT inside the box when one is given).
import struct, sys, zlib

def read(path):
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    i, idat = 8, b""
    while i < len(d):
        n, t = struct.unpack(">I4s", d[i:i + 8])
        c = d[i + 8:i + 8 + n]
        if t == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", c)
            assert depth == 8 and ctype in (2, 6) and inter == 0, "only 8-bit RGB or RGBA, not interlaced"
        elif t == b"IDAT":
            idat += c
        i += 12 + n
    bpp = 3 if ctype == 2 else 4
    raw, stride = zlib.decompress(idat), w * bpp
    rows, prev, p = [], bytearray(stride), 0
    for _ in range(h):
        f, line = raw[p], bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows

w, h, bpp, rows = read(sys.argv[1])
want = bytes.fromhex(sys.argv[2])
x0, y0, x1, y1 = (int(v) for v in sys.argv[3:7]) if len(sys.argv) >= 7 else (0, 0, w, h)
n = 0
for y in range(max(0, y0), min(h, y1)):
    r = rows[y]
    for x in range(max(0, x0), min(w, x1)):
        if r[x * bpp:x * bpp + 3] == want:
            n += 1
print("%dx%d %d" % (w, h, n))

#!/usr/bin/env python3
"""Compare a MEGA65 demo frame with the host reference's (make check-frame).

usage: check_frame.py MEMDUMP HOSTFRAME.raw WOLFPAL.inc [OUT.png]

MEMDUMP is Xemu's -dumpmem of the game stopped at demo frame N (m65_test.c):
its draw buffer at $50000 is in the VIC-IV full-colour character layout,
addr(x, y) = (x >> 3) * 1600 + y * 8 + (x & 7). HOSTFRAME is the host's frame
N, 320x200 row-major. Both are palette indices. OUT.png shows the MEGA65
frame, the host frame and the differing pixels (white) side by side.
"""
import re
import sys

FB2 = 0x50000
W, H = 320, 200

mem = open(sys.argv[1], "rb").read()
host = open(sys.argv[2], "rb").read()
m65 = bytes(mem[FB2 + (x >> 3) * 1600 + y * 8 + (x & 7)] for y in range(H) for x in range(W))

diff = [i for i in range(W * H) if m65[i] != host[i]]
# The 3D view: rows above the status bar (the rest is drawn once per level).
view = [i for i in diff if i // W < 160]
print(f"{len(diff)} of {W * H} pixels differ ({len(view)} in the 3D view area)")
if diff:
    xs = [i % W for i in diff]
    ys = [i // W for i in diff]
    print(f"  differing area: x {min(xs)}-{max(xs)}, y {min(ys)}-{max(ys)}")
    cols = sorted(set(xs))
    print(f"  {len(cols)} columns differ, first: {cols[:16]}")

if len(sys.argv) > 4:
    from PIL import Image
    pal = [tuple(int(v) * 255 // 63 for v in m.groups())
           for m in re.finditer(r"RGB\(\s*(\d+),\s*(\d+),\s*(\d+)\)",
                                open(sys.argv[3]).read())]
    img = Image.new("RGB", (W * 3 + 16, H))
    bad = set(diff)
    for y in range(H):
        for x in range(W):
            i = y * W + x
            img.putpixel((x, y), pal[m65[i]])
            img.putpixel((W + 8 + x, y), pal[host[i]])
            img.putpixel((2 * W + 16 + x, y), (255, 255, 255) if i in bad else (0, 0, 0))
    img = img.resize((img.width * 2, img.height * 2), Image.NEAREST)
    img.save(sys.argv[4])
    print(f"  wrote {sys.argv[4]} (MEGA65 | host | differences)")

sys.exit(1 if diff else 0)

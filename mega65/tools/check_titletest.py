#!/usr/bin/env python3
"""Check the memory dump of titletest.prg (see titletest.cpp)."""
import re
import struct
import sys

mem = open(sys.argv[1], "rb").read()
ref = open(sys.argv[2]).read()
want_a = int(re.search(r"REF_TITLE_SA (\d+)", ref).group(1))
want_b = int(re.search(r"REF_TITLE_SB (\d+)", ref).group(1))
fmt = "<BBHHIIHHBB"
magic, title_ok, sa, sb, ms50, fade, ssa, ssb, fartext_ok, palette_ok = struct.unpack(fmt, mem[0x5FC00:0x5FC00 + struct.calcsize(fmt)])

sig = open(sys.argv[3], "rb").read()
wa = wb = 0
for v in sig:
    wa = (wa + v) & 0xFFFF
    wb = (wb + wa) & 0xFFFF

checks = [
    (f"displayed sign-on screen matches SIGNON.BIN ({ssa},{ssb} vs {wa},{wb})", (ssa, ssb) == (wa, wb)),
    ("FarText walks the help article like a char pointer", fartext_ok == 1),
    ("fade-in ends on gamepal (curpal and VIC-IV palette registers)", palette_ok == 1),
    ("test ran to the end (no Quit)", magic == 0xEE),
    (f"displayed title matches the original decoder ({sa},{sb} vs {want_a},{want_b})",
     title_ok == 1 and (sa, sb) == (want_a, want_b)),
    (f"SDL_GetTicks over 50 PAL frames: {ms50} ms (want 1000 +/- 1%)", 990 <= ms50 <= 1010),
]
# End to end: the screenshot must show the title in the game's colours, at 2x
# from (80, 104). Rows 30..144 are not covered by the text/picture overlays.
# This catches framebuffer layout and palette errors that read-back cannot.
if len(sys.argv) > 5:
    from PIL import Image
    title = open(sys.argv[4], "rb").read()
    pal = [tuple(int(v) * 255 // 63 for v in m.groups())
           for m in re.finditer(r"RGB\(\s*(\d+),\s*(\d+),\s*(\d+)\)",
                                open(sys.argv[5]).read())]
    shot = Image.open(sys.argv[6]).convert("RGB")
    bad = 0
    first = None
    for y in range(30, 145):
        for x in range(320):
            want = pal[title[y * 320 + x]]
            got = shot.getpixel((80 + 2 * x, 104 + 2 * y))
            if any(abs(a - b) > 2 for a, b in zip(want, got)):
                bad += 1
                if first is None:
                    first = (x, y, title[y * 320 + x], want, got)
    checks.append((f"screenshot shows the title in the game palette ({bad} of "
                   f"{115 * 320} pixels differ; first {first})", bad == 0))

fail = 0
for name, ok in checks:
    print(("PASS  " if ok else "FAIL  ") + name)
    fail += not ok
print(f"      30-step fade-in took {fade} ms")
sys.exit(1 if fail else 0)

#!/usr/bin/env python3
"""Check the memory dump of titletest.prg (see titletest.cpp)."""
import sys

from m65common import read, ref_int, report, sums16, unpack, wolfpal

mem = read(sys.argv[1])
ref = open(sys.argv[2]).read()
want_a = ref_int(ref, "REF_TITLE_SA")
want_b = ref_int(ref, "REF_TITLE_SB")
magic, title_ok, sa, sb, ms50, fade, ssa, ssb, fartext_ok, palette_ok = unpack("<BBHHIIHHBB", mem, 0x5FC00)

wa, wb = sums16(read(sys.argv[3]))

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
    title = read(sys.argv[4])
    pal = wolfpal(sys.argv[5])
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

report(checks, [f"30-step fade-in took {fade} ms"])

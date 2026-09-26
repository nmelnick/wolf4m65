#!/usr/bin/env python3
"""Check the memory dump of titletest.prg (see titletest.cpp)."""
import re
import struct
import sys

mem = open(sys.argv[1], "rb").read()
ref = open(sys.argv[2]).read()
want_a = int(re.search(r"REF_TITLE_SA (\d+)", ref).group(1))
want_b = int(re.search(r"REF_TITLE_SB (\d+)", ref).group(1))
fmt = "<BBHHIIHHB"
magic, title_ok, sa, sb, ms50, fade, ssa, ssb, fartext_ok = struct.unpack(fmt, mem[0x5FC00:0x5FC00 + struct.calcsize(fmt)])

sig = open(sys.argv[3], "rb").read()
wa = wb = 0
for v in sig:
    wa = (wa + v) & 0xFFFF
    wb = (wb + wa) & 0xFFFF

checks = [
    (f"displayed sign-on screen matches SIGNON.BIN ({ssa},{ssb} vs {wa},{wb})", (ssa, ssb) == (wa, wb)),
    ("FarText walks the help article like a char pointer", fartext_ok == 1),
    ("test ran to the end (no Quit)", magic == 0xEE),
    (f"displayed title matches the original decoder ({sa},{sb} vs {want_a},{want_b})",
     title_ok == 1 and (sa, sb) == (want_a, want_b)),
    (f"SDL_GetTicks over 50 PAL frames: {ms50} ms (want 1000 +/- 1%)", 990 <= ms50 <= 1010),
]
fail = 0
for name, ok in checks:
    print(("PASS  " if ok else "FAIL  ") + name)
    fail += not ok
print(f"      30-step fade-in took {fade} ms")
sys.exit(1 if fail else 0)

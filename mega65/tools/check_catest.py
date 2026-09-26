#!/usr/bin/env python3
"""Check the memory dump of catest.prg (see catest.cpp)."""
import re
import struct
import sys

mem = open(sys.argv[1], "rb").read()
huff = open(sys.argv[2]).read()
maps = open(sys.argv[3]).read()
nchunks = len(re.findall(r"^\s*\{(\d+),", huff, re.M))
nmaps = int(re.search(r"REF_NUMMAPS (\d+)", maps).group(1))
fmt = "<BBHHHHHHBIHIHH"
(magic, screen_calls, grchunks, grbad, grfirstbad, nm, mapbad, mapfirstbad,
 pictable_ok, heap, pm_pages, pm_bytes, pm_sa, pm_sb) = struct.unpack(fmt, mem[0x50000:0x50000 + struct.calcsize(fmt)])

# VSWAP pages as the original PM_Startup defines them.
vs = open(sys.argv[4], "rb").read()
chunks, = struct.unpack("<H", vs[0:2])
offs = list(struct.unpack(f"<{chunks}I", vs[6:6 + 4 * chunks])) + [len(vs)]
lens = struct.unpack(f"<{chunks}H", vs[6 + 4 * chunks:6 + 6 * chunks])
va = vb = vbytes = 0
for i in range(chunks):
    if not offs[i]:
        continue
    size = lens[i] if not offs[i + 1] else offs[i + 1] - offs[i]
    for v in vs[offs[i]:offs[i] + size]:
        va = (va + v) & 0xFFFF
        vb = (vb + va) & 0xFFFF
    vbytes += size

checks = [
    (f"VSWAP: {pm_pages} pages, {pm_bytes} bytes (want {chunks}, {vbytes})",
     (pm_pages, pm_bytes) == (chunks, vbytes)),
    ("VSWAP pages match the original PM_Startup's", (pm_sa, pm_sb) == (va, vb)),
    ("test ran to the end (no Quit)", magic == 0xEE),
    ("pictable matches chunk STRUCTPIC", pictable_ok == 1),
    (f"cached {grchunks} of {nchunks} graphics chunks", grchunks == nchunks),
    (f"graphics match the original code ({grbad} bad, first {grfirstbad})", grbad == 0),
    ("CA_CacheScreen reached the video layer", screen_calls == 1),
    (f"loaded {nm} of {nmaps} maps", nm == nmaps),
    (f"map planes match the original code ({mapbad} bad, first {mapfirstbad})", mapbad == 0),
]
fail = 0
for name, ok in checks:
    print(("PASS  " if ok else "FAIL  ") + name)
    fail += not ok
print(f"      far heap used: {heap} bytes ({heap / 1024:.0f}KB)")
sys.exit(1 if fail else 0)

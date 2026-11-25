#!/usr/bin/env python3
"""Check the memory dump of catest.prg (see catest.cpp)."""
import struct
import sys

from m65common import read, ref_chunks, ref_int, report, sums16, unpack

mem = read(sys.argv[1])
huff = open(sys.argv[2]).read()
maps = open(sys.argv[3]).read()
nchunks = ref_chunks(huff)
nmaps = ref_int(maps, "REF_NUMMAPS")
(magic, screen_calls, grchunks, grbad, grfirstbad, nm, mapbad, mapfirstbad,
 pictable_ok, heap, pm_pages, pm_bytes, pm_sa, pm_sb) = unpack("<BBHHHHHHBIHIHH", mem, 0x50000)

# VSWAP pages as the original PM_Startup defines them.
vs = read(sys.argv[4])
chunks, = struct.unpack("<H", vs[0:2])
offs = list(struct.unpack(f"<{chunks}I", vs[6:6 + 4 * chunks])) + [len(vs)]
lens = struct.unpack(f"<{chunks}H", vs[6 + 4 * chunks:6 + 6 * chunks])
va = vb = vbytes = 0
for i in range(chunks):
    if not offs[i]:
        continue
    size = lens[i] if not offs[i + 1] else offs[i + 1] - offs[i]
    va, vb = sums16(vs[offs[i]:offs[i] + size], va, vb)
    vbytes += size

report([
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
], [f"far heap used: {heap} bytes ({heap / 1024:.0f}KB)"])

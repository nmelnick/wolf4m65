#!/usr/bin/env python3
"""Check the memory dump of hufftest.prg."""
import sys

from m65common import read, ref_chunks, ref_int, report, unpack

mem = read(sys.argv[1])
ref = open(sys.argv[2]).read()
nchunks = ref_chunks(ref)
total = ref_int(ref, "REF_TOTAL")
magic, load_rc, chunks, bad, first_bad, got_total, heap = unpack("<BbHHHII", mem, 0x50000)

report([
    ("test ran to the end", magic == 0xEE),
    (f"files loaded (rc={load_rc})", load_rc == 0),
    (f"expanded {chunks} of {nchunks} chunks", chunks == nchunks),
    (f"all chunks match the original decoder ({bad} bad, first {first_bad})", bad == 0),
    (f"expanded {got_total} of {total} bytes", got_total == total),
], [f"far heap used: {heap} bytes ({heap / 1024:.0f}KB)"])

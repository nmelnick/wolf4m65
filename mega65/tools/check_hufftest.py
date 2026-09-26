#!/usr/bin/env python3
"""Check the memory dump of hufftest.prg."""
import re
import struct
import sys

mem = open(sys.argv[1], "rb").read()
ref = open(sys.argv[2]).read()
nchunks = sum(1 for m in re.finditer(r"^\s*\{(\d+),", ref, re.M))
total = int(re.search(r"REF_TOTAL (\d+)", ref).group(1))
magic, load_rc, chunks, bad, first_bad, got_total, heap = \
    struct.unpack("<BbHHHII", mem[0x50000:0x50000 + 16])

checks = [
    ("test ran to the end", magic == 0xEE),
    (f"files loaded (rc={load_rc})", load_rc == 0),
    (f"expanded {chunks} of {nchunks} chunks", chunks == nchunks),
    (f"all chunks match the original decoder ({bad} bad, first {first_bad})", bad == 0),
    (f"expanded {got_total} of {total} bytes", got_total == total),
]
fail = 0
for name, ok in checks:
    print(("PASS  " if ok else "FAIL  ") + name)
    fail += not ok
print(f"      far heap used: {heap} bytes ({heap / 1024:.0f}KB)")
sys.exit(1 if fail else 0)

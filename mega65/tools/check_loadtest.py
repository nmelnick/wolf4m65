#!/usr/bin/env python3
"""Check the memory dump of loadtest.prg against the host copy of the file."""
import struct
import sys

mem = open(sys.argv[1], "rb").read()
host = open(sys.argv[2], "rb").read()
magic, init_rc, fd, close_rc, got, mid = struct.unpack("<BbbbII", mem[0x50000:0x5000C])
S = 64
s0, s1, s2 = (mem[0x50040 + i * S:0x50040 + (i + 1) * S] for i in range(3))

checks = [
    ("test ran to the end", magic == 0xEE),
    ("init/open/close ok", init_rc == 0 and fd >= 0 and close_rc == 0),
    (f"read {len(host)} bytes (got {got})", got == len(host)),
    ("start matches", s0 == host[:S]),
    ("middle matches", s1 == host[mid:mid + S]),
    ("end matches", s2 == host[-S:]),
]
bad = 0
for name, ok in checks:
    print(("PASS  " if ok else "FAIL  ") + name)
    bad += not ok
sys.exit(1 if bad else 0)

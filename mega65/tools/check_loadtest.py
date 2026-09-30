#!/usr/bin/env python3
"""Check the memory dump of loadtest.prg against the host copy of the file."""
import sys

from m65common import read, report, unpack

mem = read(sys.argv[1])
host = read(sys.argv[2])
magic, init_rc, fd, close_rc, got, mid = unpack("<BbbbII", mem, 0x50000)
S = 64
s0, s1, s2 = (mem[0x50040 + i * S:0x50040 + (i + 1) * S] for i in range(3))

report([
    ("test ran to the end", magic == 0xEE),
    ("init/open/close ok", init_rc == 0 and fd >= 0 and close_rc == 0),
    (f"read {len(host)} bytes (got {got})", got == len(host)),
    ("start matches", s0 == host[:S]),
    ("middle matches", s1 == host[mid:mid + S]),
    ("end matches", s2 == host[-S:]),
])

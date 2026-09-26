#!/usr/bin/env python3
"""Check the memory dump of dostest.prg (see dostest.c)."""
import struct
import sys

mem = open(sys.argv[1], "rb").read()
magic, init_rc, fd, close_rc, got = struct.unpack("<BbbbI", mem[0x50000:0x50008])
expected = bytes(((i * 7) ^ 0x5A) & 0xFF for i in range(1500))

checks = [
    ("test ran to the end", magic == 0xEE),
    ("m65_dos_init == 0", init_rc == 0),
    ("m65_dos_open >= 0", fd >= 0),
    ("read 1500 bytes", got == 1500),
    ("data matches", mem[0x40000:0x40000 + 1500] == expected),
    ("m65_dos_close == 0", close_rc == 0),
]
bad = 0
for name, ok in checks:
    print(("PASS  " if ok else "FAIL  ") + name)
    bad += not ok
sys.exit(1 if bad else 0)

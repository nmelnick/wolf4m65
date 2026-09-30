#!/usr/bin/env python3
"""Check the memory dump of dostest.prg (see dostest.c)."""
import sys

from m65common import read, report, unpack

mem = read(sys.argv[1])
magic, init_rc, fd, close_rc, got = unpack("<BbbbI", mem, 0x50000)
expected = bytes(((i * 7) ^ 0x5A) & 0xFF for i in range(1500))

report([
    ("test ran to the end", magic == 0xEE),
    ("m65_dos_init == 0", init_rc == 0),
    ("m65_dos_open >= 0", fd >= 0),
    ("read 1500 bytes", got == 1500),
    ("data matches", mem[0x40000:0x40000 + 1500] == expected),
    ("m65_dos_close == 0", close_rc == 0),
])

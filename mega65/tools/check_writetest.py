#!/usr/bin/env python3
"""Check the memory dump of writetest.prg (see writetest.c)."""
import sys

from m65common import read, report, unpack

mem = read(sys.argv[1])
magic, init_rc, wfd, w1, w2, w3, rfd, sub_fd, got = unpack("<BbbbbbbbI", mem, 0x50000)
row = bytes((i * 13 + 5) & 0xFF for i in range(256))
pattern = row * 4
expected = pattern[:512] + pattern[512:812] + bytes(212)

report([
    ("test ran to the end", magic == 0xEE),
    ("m65_dos_init == 0", init_rc == 0),
    ("TEST.BIN found in WOLF4M65/", sub_fd >= 0),
    ("TESTW.BIN opened for writing", wfd >= 0),
    ("first sector written", w1 == 0),
    ("second sector written (300 bytes, zero padded)", w2 == 0),
    ("writing past the end fails", w3 == -1),
    ("TESTW.BIN opened again", rfd >= 0),
    ("read back 1024 bytes", got == 1024),
    ("data matches", mem[0x41000:0x41000 + 1024] == expected),
])

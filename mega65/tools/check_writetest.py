#!/usr/bin/env python3
"""Check the memory dump of writetest.prg (see writetest.c)."""
import struct
import sys

mem = open(sys.argv[1], "rb").read()
magic, init_rc, wfd, w1, w2, w3, rfd, sub_fd, got = struct.unpack("<BbbbbbbbI", mem[0x50000:0x5000C])
row = bytes((i * 13 + 5) & 0xFF for i in range(256))
pattern = row * 4
expected = pattern[:512] + pattern[512:812] + bytes(212)

checks = [
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
]
bad = 0
for name, ok in checks:
    print(("PASS  " if ok else "FAIL  ") + name)
    bad += not ok
sys.exit(1 if bad else 0)

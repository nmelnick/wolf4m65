#!/usr/bin/env python3
"""Check the memory dump of proxytest.prg (see proxytest.cpp)."""
import struct
import sys

mem = open(sys.argv[1], "rb").read()
magic, firstfail, fails, checks = struct.unpack("<BBHH", mem[0x5FC00:0x5FC06])
ok = magic == 0xEE and fails == 0 and checks > 0
print(("PASS  " if ok else "FAIL  ") +
      f"far proxies match near arrays ({checks} checks, {fails} failed, first id {firstfail})")
sys.exit(0 if ok else 1)

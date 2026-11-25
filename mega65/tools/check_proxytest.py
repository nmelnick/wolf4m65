#!/usr/bin/env python3
"""Check the memory dump of proxytest.prg (see proxytest.cpp)."""
import sys

from m65common import read, report, unpack

magic, firstfail, fails, checks = unpack("<BBHH", read(sys.argv[1]), 0x5FC00)
report([(f"far proxies match near arrays ({checks} checks, {fails} failed, first id {firstfail})",
         magic == 0xEE and fails == 0 and checks > 0)])

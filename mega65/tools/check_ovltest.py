#!/usr/bin/env python3
"""Check the memory dump of the overlay test (ovltest/main.c)."""
import sys

from m65common import read, report, unpack

mem = read(sys.argv[1])
magic, load_rc, sq7, add3, fib10, call_other5, tail4, deep6, big, after, cur, layout_ok, rodata_at, objlist_at, inner20 = \
    unpack("<BbHHHHHHiHBBHHH", mem, 0x50000)

report([
    ("test ran to the end", magic == 0xEE),
    (f"layout: table at ${rodata_at:04X} (mid, from the data file), objlist at ${objlist_at:04X} (high, zeroed)", layout_ok == 1),
    ("overlays loaded", load_rc == 0),
    ("sq_ptr(7) == 49          (pointer to static overlay fn)", sq7 == 49),
    ("add3(1,2,3) == 6         (args in rc registers)", add3 == 6),
    ("big(100000,3) == 300001  (32-bit args and result)", big == 300001),
    ("fib(10) == 55            (recursion)", fib10 == 55),
    ("call_other(5) == 40      (overlay -> overlay -> resident -> overlay)", call_other5 == 40),
    ("tail(4) == 12            (tail call)", tail4 == 12),
    ("deep(6) == 6             (ping-pong between overlays)", deep6 == 6),
    ("add3(10,20,30) == 60     (still fine afterwards)", after == 60),
    ("via_inner(20) == 42      (direct call to a thunk-less function)", inner20 == 42),
], [f"__ovl_cur at end = {cur}"])

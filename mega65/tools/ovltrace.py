#!/usr/bin/env python3
"""Name the calls in an overlay trace (make OVLTRACE=1 run-wolf).

usage: ovltrace.py ELF SERIALFILE [LAST]
Each trace line is "<overlay id> <target>" (hex); this prints the function
name for each (the last LAST lines, default all), plus other serial output.
"""
import re
import sys

from m65common import function_symbols

elf, ser = sys.argv[1], sys.argv[2]
last = int(sys.argv[3]) if len(sys.argv) > 3 else 0

# Overlay k's section is .ovlK; body symbols are in it at window addresses.
names = {}
for section, table in function_symbols(elf).items():
    ovl = re.fullmatch(r"\.ovl(\d+)", section)
    for addr, name in table if ovl else ():
        names.setdefault((int(ovl.group(1)), addr), name)

lines = open(ser, errors="replace").read().replace("\r", "").split("\n")
if last:
    lines = lines[-last:]
for l in lines:
    m = re.fullmatch(r"\.?([0-9A-F]{2}) ([0-9A-F]{4})", l)
    if m:
        k, a = int(m.group(1), 16), int(m.group(2), 16)
        n = names.get((k, a), "?")
        print(f"{k:3d} {a:04X} {n.removesuffix('.body')}")
    elif l:
        print("    | " + l)

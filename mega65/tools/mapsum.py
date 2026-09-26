#!/usr/bin/env python3
"""Summarise an LLD map: per output section, the biggest contributors.
usage: mapsum.py MAP [SECTION...]"""
import collections
import re
import sys

lines = open(sys.argv[1]).read().split("\n")
wanted = sys.argv[2:] or [".text", ".rodata", ".data", ".bss", ".noinit", ".highbss"]
OUT = re.compile(r"^\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)\s+\d+\s+(\.\S+)$")
IN = re.compile(r"^\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)\s+\d+\s+(\S+):\((\S+)\)$")
cur = None
items = collections.defaultdict(list)
for l in lines:
    m = OUT.match(l)
    if m:
        cur = m.group(2)
        continue
    m = IN.match(l)
    if m and cur in wanted:
        src = re.sub(r".*/|-[0-9a-f]{6}\.o$|\.o$", "", m.group(2))
        items[cur].append((int(m.group(1), 16), src, m.group(3)))
for sect in wanted:
    by = collections.Counter()
    for size, src, sec in items[sect]:
        by[src] += size
    print(f"== {sect}: {sum(by.values())} bytes")
    for k, v in by.most_common(10):
        print(f"   {v:6d}  {k}")
    for size, src, sec in sorted(items[sect], reverse=True)[:8]:
        print(f"         {size:6d}  {src}  {sec}")

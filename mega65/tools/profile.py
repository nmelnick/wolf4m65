#!/usr/bin/env python3
"""Where the time goes: read the sampling profiler's counts (make profile).

usage: profile.py PROGRAM.elf MEMDUMP [TOP]

The counts (see m65_prof.s) are in the memory dump at $40000: 4096 16-bit
counters for the 64KB address space in 16-byte buckets, then 512 per overlay
for the window at $4000-$5FFF. Each bucket goes to the function that
contains it (functions are found from the ELF's symbols: those of section
.ovlK for overlay K).
"""
import bisect
import os
import re
import struct
import subprocess
import sys
from collections import Counter

BIN = os.path.expanduser("~/opt/llvm-mos/bin")
COPY = 0x40000
WBASE, WEND = 0x4000, 0x6000

elf, dump = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 30
mem = open(dump, "rb").read()

# Functions: (start, name) per region: None = outside the window, K = overlay K.
funcs = {}
out = subprocess.check_output([f"{BIN}/llvm-objdump", "-t", elf], text=True)
for line in out.splitlines():
    m = re.match(r"([0-9a-f]+)\s+(\S*)\s+(\S*)\s+(\S+)\s+[0-9a-f]+\s+(\S+)$", line)
    if not m or m.group(3) not in ("F", ""):
        continue
    addr, section, name = int(m.group(1), 16), m.group(4), m.group(5)
    ovl = re.fullmatch(r"\.ovl(\d+)", section)
    if ovl:
        funcs.setdefault(int(ovl.group(1)), []).append((addr, name))
    elif section.startswith(".text") and not WBASE <= addr < WEND:
        funcs.setdefault(None, []).append((addr, name))
for v in funcs.values():
    v.sort()


def owner(region, addr):
    table = funcs.get(region, [])
    i = bisect.bisect_right(table, (addr, "\xff")) - 1
    return table[i][1] if i >= 0 else f"?{addr:04x}"


def demangle(names):
    try:
        res = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True,
                             text=True).stdout.splitlines()
        return dict(zip(names, res))
    except OSError:
        return {n: n for n in names}


count = Counter()
regions = Counter()
for i in range(4096):
    n = struct.unpack_from("<H", mem, COPY + 2 * i)[0]
    if n:
        addr = i * 16 + 8
        key = ("res", owner(None, addr))
        count[key] += n
        regions["resident" if not WBASE <= addr < WEND else "window?"] += n
for k in range(1, 44):
    for j in range(512):
        n = struct.unpack_from("<H", mem, COPY + 8192 + (k * 512 + j) * 2)[0]
        if n:
            count[(f"ovl {k}", owner(k, WBASE + j * 16 + 8))] += n
            regions[f"overlay {k}"] += n

total = sum(count.values())
print(f"{total} samples (about 1 per ms)")
names = demangle([n.removesuffix(".body") for _, n in count])
print(f"\n{'samples':>8} {'%':>6}  where")
for (region, name), n in count.most_common(top):
    print(f"{n:8d} {100 * n / total:6.1f}  {region:7s} {names.get(name.removesuffix('.body'), name)}")
print("\nby region:")
for r, n in regions.most_common(12):
    print(f"{n:8d} {100 * n / total:6.1f}  {r}")

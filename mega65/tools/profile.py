#!/usr/bin/env python3
"""Where the time goes: read the sampling profiler's counts (make profile).

usage: profile.py PROGRAM.elf MEMDUMP [TOP] [--hot FILE]

The counts (see m65_prof.s) are in the memory dump at $40000: 4096 16-bit
counters for the 64KB address space in 16-byte buckets, then 512 per overlay
for the window at $4000-$5FFF. Each bucket goes to the function that
contains it (functions are found from the ELF's symbols: those of section
.ovlK for overlay K).
"""
import re
import struct
import subprocess
import sys
from collections import Counter

from m65common import function_symbols, owner as owner_in

COPY = 0x40000
WBASE, WEND = 0x4000, 0x6000

args = sys.argv[1:]
hotfile = None
if "--hot" in args:
    i = args.index("--hot")
    hotfile = args[i + 1]
    del args[i:i + 2]
elf, dump = args[0], args[1]
top = int(args[2]) if len(args) > 2 else 30
mem = open(dump, "rb").read()

# Functions: (start, name) per region: None = outside the window, K = overlay K.
funcs = {}
for section, table in function_symbols(elf).items():
    ovl = re.fullmatch(r"\.ovl(\d+)", section)
    if ovl:
        funcs[int(ovl.group(1))] = table
    elif section.startswith(".text"):
        funcs.setdefault(None, []).extend(s for s in table if not WBASE <= s[0] < WEND)
funcs.setdefault(None, []).sort()


def owner(region, addr):
    return owner_in(funcs.get(region, []), addr) or f"?{addr:04x}"


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
if hotfile:
    # The overlay functions by time spent, for ovlgen --hot (chip RAM slots).
    hot = [n.removesuffix(".body") for (region, n), _ in count.most_common()
           if region.startswith("ovl")]
    with open(hotfile, "w") as f:
        f.write("# Overlay functions by time spent (make hotlist): ovlgen puts them\n"
                "# first into the overlays that run from chip RAM.\n")
        f.write("\n".join(hot) + "\n")
    print(f"wrote {hotfile}: {len(hot)} functions")
print("\nby region:")
for r, n in regions.most_common(12):
    print(f"{n:8d} {100 * n / total:6.1f}  {r}")

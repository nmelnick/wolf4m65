#!/usr/bin/env python3
"""ovlcalls: the game's call graph between overlay functions, counted, from
an overlay trace (make calltrace: a build with every call through its thunk
and OVLTRACE, which logs "<cur> <caller> <id> <target>" per call; see
ovl/ovl_rt.s).

usage: ovlcalls.py TRACE.ser PROGRAM.elf FIRST LAST OUT.txt

Counts the calls made in demo frames FIRST to LAST (by the calls to
FrameDumpHook, one a demo frame) and writes OUT.txt: "count caller callee"
per line, most first, for ovlgen --calls (functions by their final names;
a caller may be resident code).
"""
import collections
import re
import sys

from m65common import function_symbols, owner as owner_in


def owner(table, addr):
    name = owner_in(table, addr)
    return name.removesuffix(".body") if name else None


def main():
    trace, elf, first, last, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
    secs = function_symbols(elf, untyped=False)
    resident = sorted(secs.get(".text", []) + secs.get(".midtext", []))
    line = re.compile(r"^([0-9A-F]{2}) ([0-9A-F]{4}) ([0-9A-F]{2}) ([0-9A-F]{4})$")
    counts = collections.Counter()
    frame = 0
    for raw in open(trace, "rb").read().decode("latin-1").replace("\r", "").split("\n"):
        m = line.match(raw.strip())
        if not m:
            continue
        cur, caller, oid, target = (int(g, 16) for g in m.groups())
        callee = owner(secs.get(f".ovl{oid}", []), target)
        if callee == "FrameDumpHook":
            frame += 1
            if frame > last:
                break
        if frame < first:
            continue
        if cur and 0x4000 <= caller < 0x6000:
            who = owner(secs.get(f".ovl{cur}", []), caller)
        else:
            who = owner(resident, caller)
        counts[(who or "?", callee or "?")] += 1
    with open(out, "w") as f:
        f.write(f"# Calls between functions in demo frames {first}-{last} (make calltrace):\n"
                "# count caller callee. ovlgen --calls packs frequent pairs together.\n")
        for (a, b), n in counts.most_common():
            f.write(f"{n} {a} {b}\n")
    print(f"{out}: {sum(counts.values())} calls, {len(counts)} caller-callee pairs, frames {first}-{last}")


if __name__ == "__main__":
    main()

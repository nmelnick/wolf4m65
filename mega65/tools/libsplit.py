#!/usr/bin/env python3
"""libsplit: move library code (libc, compiler-rt) into code overlays.

The llvm-mos libraries are LLVM bitcode. This compiles the members a link
actually uses to assembly, so ovlgen can place them in overlays like the
game's own code. Members stay resident (assembled as normal objects) when:
  - resident code needs them (transitively): it runs before the overlays are
    loaded (the start-up path, the overlay runtime and loader);
  - they contain start-up code (.init sections);
  - they are small integer helpers used everywhere (a thunk would cost more
    than the call), listed in HOT.
Non-bitcode members (assembly) are left to the normal library link.

usage: libsplit.py --why WHY.txt --out DIR RESIDENT.o...
Writes DIR/*.s and DIR/overlay.txt, DIR/resident.txt (lists of .s files).
"""
import argparse
import os
import re
import subprocess

from m65common import llvm

CC = "mos-mega65-clang"
HOT = {"mul.cc.obj", "shift.cc.obj", "rotate.cc.obj", "mem.c.obj"}
# All bitcode libraries: none may be left for the final link, or LLD's LTO
# handling extracts every "libcall" member again (duplicating ours).
LIBS = ("libcrt.a", "libc.a", "libcrt0.a", "libcopy-data.a")
# Members the MEGA65 build replaces (m65_stdio.c): console I/O through the
# KERNAL, which is gone once the game runs.
REPLACED = {"putchar.c.obj", "getchar.c.obj", "cbm_k_chrin.c.obj"}
# Functions the MEGA65 build replaces with versions on the math unit's
# hardware multiplier and divider (m65_math.c, m65_hwdiv.c): taken out of the
# library members that define them.
REPLACED_FUNCS = {"__mulhi3", "__mulsi3",
                  "__udivhi3", "__divhi3", "__umodhi3", "__modhi3",
                  "__udivsi3", "__divsi3", "__umodsi3", "__modsi3"}


def drop_functions(asm, names):
    """Remove the named functions (one section each: -ffunction-sections) from
    an assembly file."""
    lines = open(asm).read().split("\n")
    out, skip = [], None
    for l in lines:
        m = re.match(r"^\s*\.section\s+\.text\.([\w.$]+),", l)
        if m:
            skip = m.group(1) if m.group(1) in names else None
        elif skip and re.match(r"^\s*\.section\s", l):
            skip = None
        if not skip:
            out.append(l)
    open(asm, "w").write("\n".join(out))


def nm(obj):
    out = subprocess.check_output([llvm("llvm-nm"), obj], text=True)
    defined, undefined = set(), set()
    for line in out.splitlines():
        p = line.split()
        if len(p) == 2 and p[0] == "U":
            undefined.add(p[1])
        elif len(p) == 3 and p[1] in "TDBRWVtdbrv":   # (W: weak, e.g. strlen)
            defined.add(p[2])
    return defined, undefined


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--why", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("resident", nargs="+")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    members = {}                                    # member -> archive
    for line in open(args.why).read().splitlines()[1:]:
        m = re.match(r"(.*/(lib[^/]*\.a))\((.*)\)$", line.split("\t")[1])
        if m and m.group(2) in LIBS and m.group(3) not in REPLACED:
            members[m.group(3)] = m.group(1)

    info = {}                                       # member -> (asm, defined, undefined, init)
    for member, archive in sorted(members.items()):
        bc = os.path.join(args.out, member)
        subprocess.check_call([llvm("llvm-ar"), "x", archive, member], cwd=args.out)
        if open(bc, "rb").read(4) != b"BC\xc0\xde":
            continue                                # assembly member: normal link
        base = re.sub(r"\W", "_", member)
        asm = os.path.join(args.out, base + ".s")
        obj = os.path.join(args.out, base + ".o")
        subprocess.check_call([CC, "-Os", "-fno-lto", "-ffunction-sections", "-x", "ir",
                               "-Wno-override-module", "-S", bc, "-o", asm])
        drop_functions(asm, REPLACED_FUNCS)
        subprocess.check_call([CC, "-c", "-fno-lto", asm, "-o", obj])
        defined, undefined = nm(obj)
        init = ".init" in open(asm).read()
        info[member] = (asm, defined, undefined, init)

    owner = {}
    for member, (_, defined, _, _) in info.items():
        for sym in defined:
            owner[sym] = member

    resident = {m for m, (_, _, _, init) in info.items() if init or m in HOT}
    todo = set()
    for obj in args.resident:
        todo |= nm(obj)[1]
    for m in resident:
        todo |= info[m][2]
    while todo:
        sym = todo.pop()
        m = owner.get(sym)
        if m and m not in resident:
            resident.add(m)
            todo |= info[m][2]

    with open(os.path.join(args.out, "resident.txt"), "w") as f:
        for m in sorted(resident):
            f.write(info[m][0] + "\n")
    with open(os.path.join(args.out, "overlay.txt"), "w") as f:
        for m in sorted(set(info) - resident):
            f.write(info[m][0] + "\n")
    print(f"libsplit: {len(info)} bitcode members; {len(resident)} resident "
          f"({', '.join(sorted(resident))}), {len(info) - len(resident)} to overlays")


if __name__ == "__main__":
    main()

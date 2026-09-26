#!/usr/bin/env python3
"""ovlgen: split a program's functions between a resident core and overlays.

Input is the compiler's *assembly* output (clang -S -ffunction-sections), one
file per translation unit. For every function that is placed in an overlay:

  * its definition is renamed F -> F.body and moved to section .ovlN.F,
  * a resident thunk named F is generated in its place, so that every
    reference (calls, tail calls, function pointers in data, same-file calls)
    reaches the overlay through the runtime in ovl/ovl_rt.s.

Static functions are made global first, with a per-file prefix, so that a
thunk can reference them.

Outputs (in --out):
  <stem>.ovl.s      rewritten assembly for each input
  ovl_thunks.s      thunks, map tables, __ovl_count
  ovl.ld            linker script fragment: memory layout and OVERLAY block
  plan.txt          human readable plan and size report
  plan.json         same, machine readable

Policy (v1, deliberately simple): functions stay in source-file order.
Overlays are filled first-fit in that order, keeping a file's functions
together where they fit. Files listed in --resident-files and functions
listed in --resident-funcs stay resident.
"""

import argparse
import json
import os
import re
import subprocess
import sys

LLVM_SIZE = os.path.expanduser("~/opt/llvm-mos/bin/llvm-size")

SECTION_RE = re.compile(r'^\s*\.section\s+(\.text\.[^\s,"]+)\s*,\s*"([^"]*)"')
IDENT = r"[A-Za-z0-9_.$]"


def strip_comment(line):
    """Assembly comments start with ';' (not inside a string)."""
    out, in_str = [], False
    for ch in line:
        if ch == '"':
            in_str = not in_str
        if ch == ";" and not in_str:
            break
        out.append(ch)
    return "".join(out)


class Func:
    def __init__(self, module, name, section, lo, hi, is_global):
        self.module = module          # source file stem
        self.name = name              # symbol in the assembly
        self.section = section        # .text.NAME
        self.lo, self.hi = lo, hi     # line range [lo, hi) of the block
        self.is_global = is_global
        self.size = 0
        self.overlay = 0              # 0 = resident
        self.reason = ""              # why resident, if it is

    @property
    def final_name(self):
        return self.name if self.is_global else f"{self.module}__{self.name}"


class Module:
    def __init__(self, path):
        self.path = path
        self.stem = re.sub(r"\W", "_", os.path.splitext(os.path.basename(path))[0])
        with open(path) as f:
            self.lines = f.read().split("\n")
        self.funcs = []
        self.parse()

    def parse(self):
        L = self.lines
        starts = [i for i, l in enumerate(L) if re.match(r"^\s*\.section\s", l)
                  or re.match(r"^\s*\.(text|data|bss)\b", l)]
        starts.append(len(L))
        for a, b in zip(starts, starts[1:]):
            m = SECTION_RE.match(L[a])
            if not m:
                continue
            section, flags = m.group(1), m.group(2)
            name = section[len(".text."):]
            block = [strip_comment(l) for l in L[a:b]]
            has_label = any(re.match(rf"^{re.escape(name)}:", l) for l in block)
            is_func = any(re.match(rf"^\s*\.type\s+{re.escape(name)}\s*,\s*@function", l)
                          for l in block)
            if not (has_label and is_func):
                continue   # startup code, multi-function sections, ...
            is_global = any(re.match(rf"^\s*\.globl\s+{re.escape(name)}\s*$", l)
                            for l in block)
            weak = any(re.match(rf"^\s*\.weak\s+{re.escape(name)}\s*$", l) for l in block)
            f = Func(self.stem, name, section, a, b, is_global)
            if "G" in flags or weak:
                f.reason = "comdat/weak"
            self.funcs.append(f)


def assemble_sizes(mods, cc, extra):
    """Assemble each module unmodified and read the size of every .text.NAME."""
    for m in mods:
        obj = m.path + ".size.o"
        subprocess.check_call([cc, "-c", "-fno-lto", *extra, "-o", obj, m.path])
        out = subprocess.check_output([LLVM_SIZE, "-A", obj], text=True)
        sizes = {}
        for line in out.splitlines():
            p = line.split()
            if len(p) >= 2 and p[0].startswith(".text."):
                sizes[p[0]] = int(p[1])
        for f in m.funcs:
            f.size = sizes.get(f.section, 0)
        os.remove(obj)


def plan(mods, window, resident_files, resident_funcs, per_file=False):
    overlays = []            # list of lists of Func
    cur, fill = None, 0

    def new_overlay():
        nonlocal cur, fill
        cur, fill = [], 0
        overlays.append(cur)

    for m in mods:
        cand = []
        for f in m.funcs:
            if f.reason:
                continue
            if m.stem in resident_files:
                f.reason = "resident file"
            elif f.name in resident_funcs or f.final_name in resident_funcs:
                f.reason = "resident function"
            else:
                cand.append(f)
        total = sum(f.size for f in cand)
        if cur is None or per_file or (total <= window and fill + total > window):
            new_overlay()
        for f in cand:
            if f.size > window:
                sys.exit(f"ovlgen: {m.stem}:{f.name} is {f.size} bytes, "
                         f"larger than the {window}-byte window")
            if fill + f.size > window:
                new_overlay()
            cur.append(f)
            f.overlay = len(overlays)     # ids start at 1
            fill += f.size
    return overlays


def rename_identifier(lines, old, new):
    pat = re.compile(rf"(?<!{IDENT}){re.escape(old)}(?!{IDENT})")
    out = []
    for l in lines:
        if re.match(r"^\s*\.(ascii|asciz|string)\b", l):
            out.append(l)
            continue
        code, sep, comment = l.partition(";")
        out.append(pat.sub(new, code) + sep + comment)
    return out


def rewrite(m):
    """Return the module's assembly with overlay functions moved and renamed."""
    lines = list(m.lines)
    ovl_funcs = [f for f in m.funcs if f.overlay]

    # 1. Statics become globals with a file prefix (whole file, all references).
    for f in ovl_funcs:
        if not f.is_global:
            lines = rename_identifier(lines, f.name, f.final_name)

    # 2. Rename the definition inside each function's block.
    for f in ovl_funcs:
        n, body = f.final_name, f.final_name + ".body"
        for i in range(f.lo, f.hi):
            l = lines[i]
            code, sep, comment = l.partition(";")
            # (Section names are never renamed: '.' counts as an identifier
            # character, so '.text.sq' keeps its original name.)
            if re.match(r"^\s*\.section\s+" + re.escape(f.section) + r"\s*,", code):
                code = code.replace(f.section, f".ovl{f.overlay}.{n}", 1)
            elif re.match(rf"^\s*\.type\s+{re.escape(n)}\s*,", code):
                code = code.replace(n, body, 1)
            elif re.match(rf"^\s*\.globl\s+{re.escape(n)}\s*$", code):
                code = code.replace(n, body, 1)
            elif re.match(rf"^{re.escape(n)}:", code):
                code = body + code[len(n):]
            elif re.match(rf"^\s*\.size\s+{re.escape(n)}\s*,", code):
                code = re.sub(rf"(?<!{IDENT}){re.escape(n)}(?!{IDENT})", body, code)
            lines[i] = code + sep + comment
        # Static functions had no .globl; the body must now be global.
        if not f.is_global:
            for i in range(f.lo, f.hi):
                if re.match(rf"^{re.escape(body)}:", lines[i]):
                    lines.insert(i, f"\t.globl\t{body}")
                    # keep later line indexes valid for following functions
                    for g in ovl_funcs:
                        if g.lo > i:
                            g.lo += 1
                        if g.hi > i:
                            g.hi += 1
                    break
    return "\n".join(lines)


def make_tables(nover, wbase, wsize):
    """Map register values for overlay ids 0..nover (lower MAP region)."""
    slot = 0x4000
    first_block = wbase >> 13
    blocks = wsize >> 13
    mask = sum(1 << (first_block + i) for i in range(blocks))
    a, x = [0], [0]
    for k in range(1, nover + 1):
        phys = (k - 1) * slot                    # offset inside MB $81
        off = ((phys - wbase) >> 8) & 0xFFF
        a.append(off & 0xFF)
        x.append((mask << 4) | (off >> 8))
    return a, x


def emit_thunks(path, funcs, nover, wbase, wsize):
    a, x = make_tables(nover, wbase, wsize)
    with open(path, "w") as f:
        f.write("; Generated by ovlgen.py -- do not edit.\n")
        f.write('\t.section\t.text.ovl_thunks,"ax",@progbits\n')
        for fn in funcs:
            n = fn.final_name
            f.write(f"\t.globl\t{n}\n\t.type\t{n},@function\n{n}:\n")
            f.write(f"\tjsr\t__ovl_call\n\t.byte\t{fn.overlay}\n\t.short\t{n}.body\n")
            f.write(f"\t.size\t{n}, . - {n}\n")
        f.write('\n\t.section\t.rodata.__ovl_tab,"a",@progbits\n')
        for name, vals in (("__ovl_tab_a", a), ("__ovl_tab_x", x)):
            f.write(f"\t.globl\t{name}\n{name}:\n\t.byte\t" +
                    ",".join(str(v) for v in vals) + "\n")
        f.write(f"\t.globl\t__ovl_count\n__ovl_count:\n\t.byte\t{nover}\n")


LD_TEMPLATE = """/* Generated by ovlgen.py -- do not edit.
 *
 *   $2001..window   resident: BASIC stub, crt, libc, thunks, code, rodata
 *   window          overlay window (VMA of every overlay)
 *   after window    data and bss, up to the soft stack
 */

__basic_zp_start = 0x0002;
__basic_zp_end = 0x0090;

__rc0 = __basic_zp_start;
INCLUDE imag-regs.ld
__basic_zp_size = __basic_zp_end - __basic_zp_start;

MEMORY {{
    zp : ORIGIN = __rc31 + 1, LENGTH = __basic_zp_end - (__rc31 + 1)
    ramlo (rw) : ORIGIN = 0x2001, LENGTH = {lo_len:#x}
    ramhi (rw) : ORIGIN = {hi_org:#x}, LENGTH = {hi_len:#x}
    prg (rw) : ORIGIN = 0x2001, LENGTH = {hi_end:#x} - 0x2001
    window (rx) : ORIGIN = {wbase:#x}, LENGTH = {wsize:#x}
}}

INPUT(basic-header.o)
/* .data is stored low in the PRG and runs high, so it must be copied. */
INPUT(libcopy-data.a)

REGION_ALIAS("c_readonly", ramlo)
REGION_ALIAS("c_writeable", ramhi)

SECTIONS {{
    .basic_header : {{ *(.basic_header) }}

    /* Buffers the hypervisor reads must be below $8000. This rule must come
     * before c.ld's, which would otherwise claim them for .bss (high). */
    .lowbss (NOLOAD) : ALIGN(256) {{ *(.bss.lowbss.*) }} > ramlo

    INCLUDE c.ld
}}

INPUT(unmap-basic.o)

/* Overlays: every one is linked at the window address, and stored far
 * outside the PRG (load addresses 0x100000 + 16K * (k-1)), to be extracted
 * by ovlpack.
 * (LLD's OVERLAY cannot take a region, so these are plain sections; the
 * intended VMA overlap needs --no-check-sections.) */
SECTIONS {{
{overlays}
}}
{asserts}
__stack = {hi_end:#x};

OUTPUT_FORMAT {{
    SHORT(0x2001)
    TRIM(prg)
}}
"""


def emit_ld(path, nover, wbase, wsize, hi_end):
    ov = "\n".join(f"    .ovl{k} {wbase:#x} : AT({0x100000 + (k - 1) * wsize:#x}) "
                   f"{{ *(.ovl{k}.*) }} > window"
                   for k in range(1, nover + 1))
    asserts = "".join(
        f'ASSERT(SIZEOF(.ovl{k}) <= {wsize:#x}, "overlay {k} does not fit the window")\n'
        for k in range(1, nover + 1))
    with open(path, "w") as f:
        f.write(LD_TEMPLATE.format(
            lo_len=wbase - 0x2001, hi_org=wbase + wsize, hi_len=hi_end - (wbase + wsize),
            hi_end=hi_end, wbase=wbase, wsize=wsize, overlays=ov, asserts=asserts))


def report(path, mods, overlays, window):
    lines = []
    res = [f for m in mods for f in m.funcs if not f.overlay]
    lines.append(f"window {window} bytes, {len(overlays)} overlays, "
                 f"{sum(len(o) for o in overlays)} overlay functions\n")
    for k, o in enumerate(overlays, 1):
        used = sum(f.size for f in o)
        files = sorted({f.module for f in o})
        lines.append(f"overlay {k:2d}: {used:6d}/{window} bytes, {len(o):3d} functions: "
                     f"{', '.join(files)}")
    lines.append("")
    lines.append(f"resident functions (from the sources): {len(res)}, "
                 f"{sum(f.size for f in res)} bytes")
    for f in res:
        lines.append(f"  {f.module}:{f.name}  {f.size}  ({f.reason or 'not a plain function'})")
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    return "\n".join(lines[:len(overlays) + 2])


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("asm", nargs="+", help="compiler assembly files, in link order")
    ap.add_argument("--out", required=True)
    ap.add_argument("--cc", default="mos-mega65-clang")
    ap.add_argument("--window-base", type=lambda s: int(s, 0), default=0x4000)
    ap.add_argument("--window-size", type=lambda s: int(s, 0), default=0x4000)
    ap.add_argument("--data-end", type=lambda s: int(s, 0), default=0xD000)
    ap.add_argument("--resident-files", default="")
    ap.add_argument("--resident-funcs", default="")
    ap.add_argument("--per-file", action="store_true",
                    help="start a new overlay for every input file (for tests)")
    args = ap.parse_args()

    assert args.window_base % 0x2000 == 0 and args.window_size % 0x2000 == 0
    assert args.window_base + args.window_size <= 0x8000, "window must be in the lower region"
    assert args.window_size <= 0x4000, "attic slots are 16KB"
    os.makedirs(args.out, exist_ok=True)

    mods = [Module(p) for p in args.asm]
    assemble_sizes(mods, args.cc, [])
    overlays = plan(mods, args.window_size,
                    set(filter(None, args.resident_files.split(","))),
                    set(filter(None, args.resident_funcs.split(","))), args.per_file)

    for m in mods:
        with open(os.path.join(args.out, m.stem + ".ovl.s"), "w") as f:
            f.write(rewrite(m))
    ovl_funcs = [f for m in mods for f in m.funcs if f.overlay]
    emit_thunks(os.path.join(args.out, "ovl_thunks.s"), ovl_funcs, len(overlays),
                args.window_base, args.window_size)
    emit_ld(os.path.join(args.out, "ovl.ld"), len(overlays), args.window_base,
            args.window_size, args.data_end)
    summary = report(os.path.join(args.out, "plan.txt"), mods, overlays, args.window_size)
    with open(os.path.join(args.out, "plan.json"), "w") as f:
        json.dump([[{"module": x.module, "name": x.final_name, "size": x.size}
                    for x in o] for o in overlays], f, indent=1)
    print(summary)


if __name__ == "__main__":
    main()

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
  ovl_thunks.s      thunks, map tables, __ovl_count, __ovl_slot
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
        self.aliases = []             # (alias, target) from `alias = target`
        self.parse()

    def parse(self):
        L = self.lines
        for l in L:
            m = re.match(r"^([A-Za-z_.$][\w.$]*)\s*=\s*([A-Za-z_.$][\w.$]*)\s*$", strip_comment(l))
            if m:
                self.aliases.append((m.group(1), m.group(2)))
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
            f = Func(self.stem, name, section, a, b, is_global or weak)
            f.weak = weak               # (overlay-able; its thunk is weak too)
            if "G" in flags:
                f.reason = "comdat"     # may exist in several objects: resident
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


CHIP_BASE = 0x20000          # chip RAM for overlays (the C65 ROM's, unused)


def plan(mods, window, resident_files, resident_funcs, per_file=False,
         chip_slots=0, hot_funcs=(), hot_files=()):
    """-> overlays (lists of Func; overlay k is overlays[k - 1]), and how many
    of them run from chip RAM: the first ones,
    up to chip_slots, run from chip RAM: they get the hot functions (in the
    order given), then those of the hot files (in the order given), packed
    first-fit. The rest
    are planned file by file, and run from attic RAM."""
    overlays = []            # list of lists of Func
    cur, fill = None, 0
    nchip = 0

    if chip_slots:
        byname = {}
        for m in mods:
            for f in m.funcs:
                if not f.reason and m.stem not in resident_files \
                        and f.name not in resident_funcs and f.final_name not in resident_funcs:
                    byname.setdefault(f.final_name, []).append(f)
        order = [f for n in hot_funcs for f in byname.get(n, [])]
        stems = {m.stem: m for m in mods}
        order += [f for h in hot_files if h in stems for fs in
                  (byname.get(f.final_name, []) for f in stems[h].funcs) for f in fs]
        bins, fills = [[] for _ in range(chip_slots)], [0] * chip_slots
        room = window - 64          # (the sizes are estimates: a little slack)
        for f in order:
            if f.overlay:
                continue
            for i in range(chip_slots):
                if fills[i] + f.size <= room:
                    bins[i].append(f)
                    fills[i] += f.size
                    f.overlay = i + 1
                    break
        overlays = [b for b in bins if b]
        assert all(bins[i] for i in range(len(overlays)))     # (filled in order)
        nchip = len(overlays)

    def new_overlay():
        nonlocal cur, fill
        cur, fill = [], 0
        overlays.append(cur)

    for m in mods:
        cand = []
        for f in m.funcs:
            if f.reason or f.overlay:
                continue
            if m.stem in resident_files:
                f.reason = "resident file"
            elif f.name in resident_funcs or f.final_name in resident_funcs:
                f.reason = "resident function"
            else:
                cand.append(f)
        if not cand:
            continue                      # (all in chip overlays already)
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
    return overlays, nchip


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

    # `alias = F` cannot refer to a symbol defined in another object (the
    # thunk), so such aliases get thunks of their own (see alias_thunks) and
    # the assignment goes.
    moved = {f.name for f in ovl_funcs}
    for i, l in enumerate(lines):
        mm = re.match(r"^([A-Za-z_.$][\w.$]*)\s*=\s*([A-Za-z_.$][\w.$]*)\s*$", strip_comment(l))
        if mm and mm.group(2) in moved:
            lines[i] = "; (alias " + mm.group(1) + " is a thunk: see ovl_thunks.s)"

    # Address-significance markers name the definition, which becomes F.body
    # (left alone, they would be undefined references to F).
    for f in ovl_funcs:
        for i, l in enumerate(lines):
            if re.match(rf"^\s*\.addrsig_sym\s+{re.escape(f.name)}\s*$", l):
                lines[i] = l.replace(f.name, getattr(f, "bodyname", f.final_name + ".body"))

    # 1. Statics become globals with a file prefix (whole file, all references).
    for f in ovl_funcs:
        if not f.is_global:
            lines = rename_identifier(lines, f.name, f.final_name)

    # 2. Rename the definition inside each function's block.
    for f in ovl_funcs:
        n, body = f.final_name, getattr(f, "bodyname", f.final_name + ".body")
        for i in range(f.lo, f.hi):
            l = lines[i]
            code, sep, comment = l.partition(";")
            # (Section names are never renamed: '.' counts as an identifier
            # character, so '.text.sq' keeps its original name.)
            if re.match(r"^\s*\.section\s+" + re.escape(f.section) + r"\s*,", code):
                code = code.replace(f.section, f".ovl{f.overlay}.{n}", 1)
            elif re.match(rf"^\s*\.type\s+{re.escape(n)}\s*,", code):
                code = code.replace(n, body, 1)
            elif re.match(rf"^\s*\.(globl|weak)\s+{re.escape(n)}\s*$", code):
                code = code.replace(n, body, 1)
            elif re.match(rf"^{re.escape(n)}:", code):
                code = body + code[len(n):]
            elif re.match(rf"^\s*\.size\s+{re.escape(n)}\s*,", code):
                code = re.sub(rf"(?<!{IDENT}){re.escape(n)}(?!{IDENT})", body, code)
            elif re.match(rf"^\s*\.(hidden|protected)\s+{re.escape(n)}\s*$", code):
                code = code.replace(n, body, 1)     # (else: an undefined reference)
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
    return lines


DIRECTIVES = re.compile(r"^\s*\.(globl|type|size|hidden|protected|weak|section)\b")


def link_calls(mods, extern_refs):
    """Pass 2, over all modules: a direct call or tail jump (jsr/jmp F) from a
    function in the same overlay as F goes straight to F.body; every other
    reference to F (another overlay, resident code, address taken, data, or
    external resident objects) needs F's thunk. Returns the thunked names."""
    ovl_of = {f.final_name: f.overlay for m in mods for f in m.funcs
              if f.overlay and not getattr(f, "dropped", False)}
    needed = set(n for n in extern_refs if n in ovl_of)
    ident = re.compile(rf"(?<!{IDENT})[A-Za-z_.$][\w.$]*(?!{IDENT})")
    for m in mods:
        lines = m.out
        blockovl = [0] * len(lines)
        for f in m.funcs:
            if f.overlay:
                for i in range(f.lo, f.hi):
                    blockovl[i] = f.overlay
        # (a direct call inside a dropped duplicate still names the winner)
        for i, l in enumerate(lines):
            code, sep, comment = l.partition(";")
            if DIRECTIVES.match(code) or re.match(r"^\s*[A-Za-z_.$][\w.$]*:", code):
                continue
            call = re.match(r"^(\s*(?:jsr|jmp)\s+)([A-Za-z_.$][\w.$]*)\s*$", code)
            if call and call.group(2) in ovl_of and blockovl[i] == ovl_of[call.group(2)]:
                lines[i] = call.group(1) + call.group(2) + ".body" + sep + comment
                continue
            for tok in ident.findall(code):
                if tok in ovl_of:
                    needed.add(tok)
    return needed


def extern_refs(objects):
    """Undefined symbols of objects linked as they are (resident code)."""
    refs = set()
    for obj in objects:
        out = subprocess.check_output([os.path.join(os.path.dirname(LLVM_SIZE), "llvm-nm"),
                                       "-u", obj], text=True)
        refs |= {l.split()[-1] for l in out.splitlines() if l.strip()}
    return refs


def make_tables(nover, wbase, wsize, nchip=0):
    """Map register values for overlay ids 0..nover (lower MAP region): the
    megabyte, and the offset/mask. Overlay k is in slot k - 1 (window-sized)
    of MB $81 (attic RAM); the first nchip run from chip RAM instead, from
    CHIP_BASE + (k - 1) * wsize in MB 0 (ovl_load copies them there)."""
    slot = wsize
    first_block = wbase >> 13
    blocks = wsize >> 13
    mask = sum(1 << (first_block + i) for i in range(blocks))
    a, x, mb = [0], [0], [0]
    for k in range(1, nover + 1):
        if k <= nchip:
            phys, m = CHIP_BASE + (k - 1) * slot, 0x00
        else:
            phys, m = (k - 1) * slot, 0x81       # offset inside MB $81
        off = ((phys - wbase) >> 8) & 0xFFF
        a.append(off & 0xFF)
        x.append((mask << 4) | (off >> 8))
        mb.append(m)
    return a, x, mb


def emit_thunks(path, funcs, nover, wbase, wsize, aliases=(), needed=None, nchip=0):
    a, x, mb = make_tables(nover, wbase, wsize, nchip)
    with open(path, "w") as f:
        f.write("; Generated by ovlgen.py -- do not edit.\n")
        for fn in funcs:
            n = fn.final_name
            if getattr(fn, "dropped", False):
                continue                            # a losing weak duplicate
            if needed is not None and n not in needed:
                continue                            # only called directly
            # One section per thunk, so the linker can drop unused ones (and
            # with them the overlay bodies nothing else refers to).
            f.write(f'\t.section\t.text.ovlthunk.{n},"ax",@progbits\n')
            bind = ".weak" if getattr(fn, "weak", False) else ".globl"
            f.write(f"\t{bind}\t{n}\n\t.type\t{n},@function\n{n}:\n")
            f.write(f"\tjsr\t__ovl_call\n\t.byte\t{fn.overlay}\n\t.short\t{n}.body\n")
            f.write(f"\t.size\t{n}, . - {n}\n")
        for name, overlay, body in aliases:          # `alias = F` for moved F
            f.write(f'\t.section\t.text.ovlthunk.{name},"ax",@progbits\n')
            f.write(f"\t.globl\t{name}\n\t.type\t{name},@function\n{name}:\n")
            f.write(f"\tjsr\t__ovl_call\n\t.byte\t{overlay}\n\t.short\t{body}\n")
            f.write(f"\t.size\t{name}, . - {name}\n")
        f.write('\n\t.section\t.rodata.__ovl_tab,"a",@progbits\n')
        for name, vals in (("__ovl_tab_a", a), ("__ovl_tab_x", x), ("__ovl_tab_mb", mb)):
            f.write(f"\t.globl\t{name}\n{name}:\n\t.byte\t" +
                    ",".join(str(v) for v in vals) + "\n")
        f.write(f"\t.globl\t__ovl_count\n__ovl_count:\n\t.byte\t{nover}\n")
        f.write(f"\t.globl\t__ovl_slot\n__ovl_slot:\n\t.short\t{wsize}\n")
        f.write(f"\t.globl\t__ovl_nchip\n__ovl_nchip:\n\t.byte\t{nchip}\n")


LD_TEMPLATE = """/* Generated by ovlgen.py -- do not edit.
 *
 * The overlay build's memory layout (see also m65_platform.c, which sets up
 * the parts that need the KERNAL gone):
 *
 *   $0300-$1FFF  lowmem   .rodata (copied from its load address by
 *                         m65_takeover)
 *   $2001-$3FFF  prg      BASIC stub, start-up, resident code
 *   {wbase:#06x}-...    window   overlay window; at load time also the load
 *                         addresses (LMA) of .rodata, .data and .zp.data
 *   {hi_org:#06x}-{hi_end:#06x}  mid      .data, .bss, .noinit, heap, soft stack
 *   $E000-$FFF9  high     .highbss (large arrays; zeroed by m65_takeover)
 *
 * The PRG file holds $2001-{loadend:#06x}.
 */

__basic_zp_start = 0x0002;
__basic_zp_end = 0x0090;

__rc0 = __basic_zp_start;
INCLUDE imag-regs.ld
__basic_zp_size = __basic_zp_end - __basic_zp_start;

MEMORY {{
    zp : ORIGIN = __rc31 + 1, LENGTH = __basic_zp_end - (__rc31 + 1)
    lowmem (rw) : ORIGIN = 0x0300, LENGTH = 0x1D00
    prg (rx) : ORIGIN = 0x2001, LENGTH = {wbase:#x} - 0x2001
    loadarea (r) : ORIGIN = {wbase:#x}, LENGTH = {wsize:#x}
    mid (rw) : ORIGIN = {hi_org:#x}, LENGTH = {hi_len:#x}
    high (rw) : ORIGIN = 0xE000, LENGTH = 0x1FFA
    image (rw) : ORIGIN = 0x2001, LENGTH = {loadend:#x} - 0x2001
    window (rx) : ORIGIN = {wbase:#x}, LENGTH = {wsize:#x}
    /* Not in the PRG: .data is loaded from its own file by m65_startup. */
    dataimg (r) : ORIGIN = 0x200000, LENGTH = 0x10000
}}

INPUT(basic-header.o)
INPUT(libcopy-data.a)
INPUT(unmap-basic.o)

/* The SDK's zero-page scripts put load addresses in c_readonly. */
REGION_ALIAS("c_readonly", loadarea)
REGION_ALIAS("c_writeable", mid)

SECTIONS {{
    .basic_header : {{ *(.basic_header) }} > prg

    .text : {{ INCLUDE text-sections.ld }} > prg

    /* Uninitialised arrays placed in the regions' spare room, so that the
     * heap (the rest of mid) is large enough. Zeroed by m65_takeover. */
    .prgbss (NOLOAD) : {{ {prgbss} }} > prg
    __prgbss_start = ADDR(.prgbss);
    __prgbss_size = SIZEOF(.prgbss);

    /* Read-only tables (all .rodata.* except the string pools, which start
     * with 's') run in mid and load with .data from the data file: lowmem
     * keeps the strings. Nothing reads these before m65_startup loads that
     * file. This rule must come before .rodata's, which would claim them. */
    .rodata_far : {{ *(.rodata.[!s]*) }} > mid AT> dataimg

    /* Resident code that does not fit prg (e.g. the music interrupt):
     * loaded with .data from the data file, so usable after m65_startup. */
    .midtext : {{ *(.midtext*) }} > mid AT> dataimg

    .rodata : {{ INCLUDE rodata-sections.ld }} > lowmem AT> loadarea
    __rodata_start = ADDR(.rodata);
    __rodata_load_start = LOADADDR(.rodata);
    __rodata_size = SIZEOF(.rodata);

    .lowbss (NOLOAD) : {{ {lowbss} }} > lowmem
    __lowbss_start = ADDR(.lowbss);
    __lowbss_size = SIZEOF(.lowbss);

    /* .data is not in the PRG (the load area is too small for it and
     * .rodata): the build extracts it to its own file, and m65_startup loads
     * it straight into place. The C runtime's copy is given nothing to do. */
    .data : {{ INCLUDE data-sections.ld }} > mid AT> dataimg
    /* .rodata_far and .data: one block, loaded from the data file. */
    __m65_data_start = ADDR(.rodata_far);
    __m65_data_size = ADDR(.data) + SIZEOF(.data) - ADDR(.rodata_far);
    ASSERT(LOADADDR(.data) - LOADADDR(.rodata_far) == ADDR(.data) - ADDR(.rodata_far),
           "the data file must be one contiguous block")
    __data_load_start = 0;
    __data_size = 0;

    INCLUDE zp.ld

    /* Large arrays that fit nowhere else. */
    .highbss (NOLOAD) : {{ {highbss} }} > high
    __highbss_start = ADDR(.highbss);
    __highbss_size = SIZEOF(.highbss);

    .bss (NOLOAD) : {{ INCLUDE bss-sections.ld }} > mid
    INCLUDE bss-symbols.ld

    .noinit (NOLOAD) : {{ INCLUDE noinit-sections.ld }} > mid
}}

/* Overlays: every one is linked at the window address, and stored far
 * outside the PRG (load addresses 0x100000 + window size * (k-1)), to be extracted
 * by ovlpack.
 * (LLD's OVERLAY cannot take a region, so these are plain sections; the
 * intended VMA overlap needs --no-check-sections.) */
SECTIONS {{
{overlays}
}}
{asserts}
/* The hardware stack: 512 bytes at the top of mid, used in 16-bit mode (see
 * m65_takeover), then the C (soft) stack below it. */
__hwstack_top = {hi_end:#x} - 1;
__stack = {hi_end:#x} - 0x200;

OUTPUT_FORMAT {{
    SHORT(0x2001)
    TRIM(image)
}}
"""


# Which .bss sections go into the regions' spare room (see the template).
HIGHBSS = "*(.bss.objlist) *(.bss.doorobjlist)"
LOWBSS = "*(.bss.palette1) *(.bss.curpal) *(.bss.vislist)"
PRGBSS = ""                     # (none fit now: resident code grew)


def emit_ld(path, nover, wbase, wsize, hi_end, highbss=HIGHBSS, lowbss=LOWBSS, prgbss=PRGBSS):
    ov = "\n".join(f"    .ovl{k} {wbase:#x} : AT({0x100000 + (k - 1) * wsize:#x}) "
                   f"{{ *(.ovl{k}.*) }} > window"
                   for k in range(1, nover + 1))
    asserts = "".join(
        f'ASSERT(SIZEOF(.ovl{k}) <= {wsize:#x}, "overlay {k} does not fit the window")\n'
        for k in range(1, nover + 1))
    with open(path, "w") as f:
        f.write(LD_TEMPLATE.format(
            hi_org=wbase + wsize, hi_len=hi_end - (wbase + wsize), hi_end=hi_end,
            loadend=wbase + wsize, wbase=wbase, wsize=wsize, overlays=ov,
            asserts=asserts, highbss=highbss, lowbss=lowbss, prgbss=prgbss))


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
    ap.add_argument("--window-size", type=lambda s: int(s, 0), default=0x2000)
    ap.add_argument("--data-end", type=lambda s: int(s, 0), default=0xD000)
    ap.add_argument("--resident-files", default="")
    ap.add_argument("--resident-funcs", default="")
    ap.add_argument("--extern-objs", default="",
                    help="comma-separated resident objects whose references need thunks")
    ap.add_argument("--per-file", action="store_true",
                    help="start a new overlay for every input file (for tests)")
    ap.add_argument("--chip-slots", type=int, default=0,
                    help="overlays that run from chip RAM (at most 16: $20000-$3FFFF)")
    ap.add_argument("--hot", help="files (comma-separated) of hot function names, hottest first (for --chip-slots)")
    ap.add_argument("--hot-files", default="",
                    help="comma-separated files whose functions come next (for --chip-slots)")
    args = ap.parse_args()

    assert args.window_base % 0x2000 == 0 and args.window_size % 0x2000 == 0
    assert args.window_base + args.window_size <= 0x8000, "window must be in the lower region"
    assert args.window_size <= 0x4000, "attic slots are 16KB"
    os.makedirs(args.out, exist_ok=True)

    mods = [Module(p) for p in args.asm]
    for m in mods:
        if any(re.match(r"^\s*\.section\s+\.init_array", l) for l in m.lines):
            sys.exit(f"ovlgen: {m.path} has global constructors (.init_array): they run "
                     "before main, i.e. before the code overlays are loaded. Make the "
                     "constructors constexpr (or avoid them).")
    assemble_sizes(mods, args.cc, [])
    assert args.chip_slots * args.window_size <= 0x20000, "chip slots: $20000-$3FFFF"
    hot = []
    if args.hot:
        for path in args.hot.split(","):
            hot += [l.strip() for l in open(path) if l.strip() and not l.startswith("#")]
    hot_files = [h for h in args.hot_files.split(",") if h]
    overlays, nchip = plan(mods, args.window_size,
                    set(filter(None, args.resident_files.split(","))),
                    set(filter(None, args.resident_funcs.split(","))), args.per_file,
                    args.chip_slots, hot, hot_files)

    # A name defined more than once (weak library definitions, possibly next
    # to our strong replacements): as the linker would, keep the strong one
    # (else the first); the others get unique body names and no thunk, so
    # nothing refers to them and the linker drops them.
    byname = {}
    for m in mods:
        for f in m.funcs:
            if f.overlay:
                byname.setdefault(f.final_name, []).append(f)
    for name, fs in byname.items():
        if len(fs) > 1:
            winner = next((f for f in fs if not f.weak), fs[0])
            for k, f in enumerate(x for x in fs if x is not winner):
                f.bodyname = f"{name}.weakdup{k}.body"
                f.dropped = True
    for m in mods:
        m.out = rewrite(m)
    needed = link_calls(mods, extern_refs(filter(None, args.extern_objs.split(","))))
    for m in mods:
        with open(os.path.join(args.out, m.stem + ".ovl.s"), "w") as f:
            f.write("\n".join(m.out))
    ovl_funcs = [f for m in mods for f in m.funcs if f.overlay]
    aliases = []
    for m in mods:
        byname = {f.name: f for f in m.funcs if f.overlay}
        for alias, target in m.aliases:
            if target in byname:
                f = byname[target]
                aliases.append((alias, f.overlay, f.final_name + ".body"))
    emit_thunks(os.path.join(args.out, "ovl_thunks.s"), ovl_funcs, len(overlays),
                args.window_base, args.window_size, aliases, needed, nchip)
    print(f"thunks: {len(needed)} of {len(ovl_funcs)} overlay functions need one")
    emit_ld(os.path.join(args.out, "ovl.ld"), len(overlays), args.window_base,
            args.window_size, args.data_end)
    summary = report(os.path.join(args.out, "plan.txt"), mods, overlays, args.window_size)
    summary += f"\n{nchip} overlays run from chip RAM (the first {nchip})\n"
    with open(os.path.join(args.out, "plan.json"), "w") as f:
        json.dump([[{"module": x.module, "name": x.final_name, "size": x.size}
                    for x in o] for o in overlays], f, indent=1)
    print(summary)


if __name__ == "__main__":
    main()

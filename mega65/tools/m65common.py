"""What the tools share: where llvm-mos's binaries are, an ELF's function
symbols, and the test checkers' reading and reporting of a memory dump.
"""
import bisect
import collections
import os
import re
import shutil
import struct
import subprocess
import sys


# --- llvm-mos -----------------------------------------------------------------

def llvm_bin():
    """llvm-mos's bin directory: $LLVM_MOS_BIN, else the one holding the
    mos-mega65-clang on the PATH (the one make uses), else ~/opt/llvm-mos/bin.
    (Not just the PATH for llvm-nm and friends: a system LLVM's would come
    first there and does not know the 6502.)"""
    if os.environ.get("LLVM_MOS_BIN"):
        return os.environ["LLVM_MOS_BIN"]
    cc = shutil.which("mos-mega65-clang")
    if cc:
        return os.path.dirname(os.path.realpath(cc))
    return os.path.expanduser("~/opt/llvm-mos/bin")


def llvm(tool):
    """Path of an llvm-mos binary, e.g. llvm("llvm-objdump")."""
    return os.path.join(llvm_bin(), tool)


# --- ELF symbols ----------------------------------------------------------------

def function_symbols(elf, untyped=True):
    """-> {section: sorted [(address, name)]}: the functions of each section,
    and with `untyped` the symbols without a type too (labels in hand-written
    assembly, but also linker markers such as __init_array_start). Overlay
    K's functions are in section .ovlK, at window addresses."""
    out = subprocess.check_output([llvm("llvm-objdump"), "-t", elf], text=True)
    secs = collections.defaultdict(list)
    for line in out.splitlines():
        m = re.match(r"([0-9a-f]+)\s+(\S*)\s+(\S*)\s+(\S+)\s+[0-9a-f]+\s+(\S+)$", line)
        if m and (m.group(3) == "F" or untyped and m.group(3) == ""):
            secs[m.group(4)].append((int(m.group(1), 16), m.group(5)))
    for table in secs.values():
        table.sort()
    return secs


def owner(table, addr):
    """The name of the function in `table` (sorted [(address, name)]) that
    contains addr: the last one starting at or before it. None if none does."""
    i = bisect.bisect_right(table, (addr, "\xff")) - 1
    return table[i][1] if i >= 0 else None


# --- Test checkers ----------------------------------------------------------------

def read(path):
    return open(path, "rb").read()


def unpack(fmt, mem, at):
    """struct.unpack of fmt at offset `at` of mem (a test's report block)."""
    return struct.unpack(fmt, mem[at:at + struct.calcsize(fmt)])


def ref_int(text, name):
    """The number #defined as `name` in a generated reference header."""
    return int(re.search(rf"{name} (\d+)", text).group(1))


def ref_chunks(text):
    """How many { n, ... } entries a generated reference header has."""
    return len(re.findall(r"^\s*\{(\d+),", text, re.M))


def sums16(data, a=0, b=0):
    """The tests' running sums over bytes (a: of the bytes, b: of the a's,
    both mod 65536); pass a, b back in to continue over more data."""
    for v in data:
        a = (a + v) & 0xFFFF
        b = (b + a) & 0xFFFF
    return a, b


def wolfpal(path):
    """The game palette (wolfpal.inc) as 8-bit (r, g, b) tuples."""
    return [tuple(int(v) * 255 // 63 for v in m.groups())
            for m in re.finditer(r"RGB\(\s*(\d+),\s*(\d+),\s*(\d+)\)", open(path).read())]


def report(checks, notes=()):
    """Print PASS/FAIL for each (description, ok), then the notes, and exit
    with 1 if any failed."""
    bad = 0
    for name, ok in checks:
        print(("PASS  " if ok else "FAIL  ") + name)
        bad += not ok
    for note in notes:
        print("      " + note)
    sys.exit(1 if bad else 0)

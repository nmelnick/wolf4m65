#!/usr/bin/env python3
"""ovlpack: extract the overlay images from a linked ELF into one .OVL file.

usage: ovlpack.py PROGRAM.elf COUNT OUT.OVL [SLOT_SIZE]

Image k (1..COUNT) is padded to SLOT_SIZE (default 8KB, the window size) and stored at file
offset (k-1)*SLOT_SIZE, which is where the loader puts it in attic RAM.
"""
import os
import subprocess
import sys
import tempfile

from m65common import llvm

elf, count, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
slot = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x2000
objcopy = llvm("llvm-objcopy")

with tempfile.TemporaryDirectory() as tmp, open(out, "wb") as f:
    for k in range(1, count + 1):
        part = os.path.join(tmp, f"ovl{k}.bin")
        subprocess.check_call([objcopy, "-O", "binary", f"--only-section=.ovl{k}", elf, part])
        data = open(part, "rb").read()
        if len(data) > slot:
            sys.exit(f"ovlpack: overlay {k} is {len(data)} bytes, slot is {slot}")
        f.write(data + bytes(slot - len(data)))
print(f"ovlpack: {count} overlays -> {out} ({count * slot} bytes)")

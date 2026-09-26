#!/usr/bin/env python3
"""Extract the Wolf3D sign-on picture (signon[] in signon.cpp, the non-SPEAR
one) into a raw 320x200 file. The MEGA65 build loads it from the SD card
instead of compiling a 64000-byte array into the program.

usage: extract_signon.py ../signon.cpp OUT.BIN
"""
import re
import sys

src = open(sys.argv[1], encoding="latin-1").read()
start = src.index("byte signon[] = {")
body = src[start:src.index("};", start)]
data = bytes(int(h, 16) for h in re.findall(r"0x([0-9A-Fa-f]{2})", body))
if len(data) != 64000:
    sys.exit(f"extract_signon: expected 64000 bytes, got {len(data)}")
open(sys.argv[2], "wb").write(data)

#!/usr/bin/env python3
"""Precompute the renderer's trig tables on the host with the original
BuildTables (from git revision BASE of wl_main.cpp), so the MEGA65 needs
neither floating point nor tan/sin for them. Writes TABLES.BIN:

    finetangent[FINEANGLES/4]   int32 little endian
    sintable[ANGLES+ANGLES/4]   int32 little endian (costable = +ANGLES/4)

usage: gen_tables.py OUT.BIN [BASE]      (run from mega65/)
"""
import os
import subprocess
import sys
import tempfile

out = sys.argv[1]
BASE = sys.argv[2] if len(sys.argv) > 2 else "3d41ccc"
src = subprocess.check_output(["git", "show", f"{BASE}:wl_main.cpp"]).decode("latin-1")
i = src.index("void BuildTables (void)")
build = src[i:src.index("\n}\n", i) + 3]
radline = [l for l in src.split("\n") if "radtoint" in l and "=" in l and "const" in l][0]

prog = r'''
#include <math.h>
#include <stdio.h>
#include <stdint.h>
typedef int32_t fixed;
#define PI              3.141592657
#define GLOBAL1         (1l<<16)
#define ANGLES          360
#define ANGLEQUAD       (ANGLES/4)
#define FINEANGLES      3600
''' + radline + r'''
int32_t finetangent[FINEANGLES/4];
fixed sintable[ANGLES+ANGLES/4];
''' + build + r'''
int main(int argc, char **argv)
{
    BuildTables();
    FILE *f = fopen(argv[1], "wb");
    for (int i = 0; i < FINEANGLES/4; i++)
        for (int b = 0; b < 4; b++) fputc((uint32_t)finetangent[i] >> (8 * b), f);
    for (int i = 0; i < ANGLES+ANGLES/4; i++)
        for (int b = 0; b < 4; b++) fputc((uint32_t)sintable[i] >> (8 * b), f);
    return fclose(f) != 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c, exe = os.path.join(tmp, "t.cpp"), os.path.join(tmp, "t")
    open(c, "w").write(prog)
    subprocess.check_call(["g++", "-O0", "-w", "-o", exe, c])
    subprocess.check_call([exe, os.path.abspath(out)])

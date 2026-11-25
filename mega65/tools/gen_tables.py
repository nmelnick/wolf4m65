#!/usr/bin/env python3
"""Precompute the renderer's trig tables on the host with the original
BuildTables (from git revision BASE of wl_main.cpp), so the MEGA65 needs
neither floating point nor tan/sin for them. Writes TABLES.BIN:

    finetangent[FINEANGLES/4]   int32 little endian
    sintable[ANGLES+ANGLES/4]   int32 little endian (costable = +ANGLES/4)

then CalcProjection's results (focal length FOCALLENGTH) for each view
width the game can have, 64 to 320 in steps of 16:

    scale                       int32 little endian
    intang[viewwidth/2]         int16 little endian (pixelangle[halfview-1-i]
                                = intang[i], pixelangle[halfview+i] = -intang[i])

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
#define FOCALLENGTH     (0x5700l)
#define MINDIST         (0x5800l)
#define VIEWGLOBAL      0x10000
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
    for (int viewwidth = 64; viewwidth <= 320; viewwidth += 16) {
        // (CalcProjection, wl_main.cpp)
        double facedist = FOCALLENGTH+MINDIST;
        int halfview = viewwidth/2;
        fixed scale = (fixed) (halfview*facedist/(VIEWGLOBAL/2));
        for (int b = 0; b < 4; b++) fputc((uint32_t)scale >> (8 * b), f);
        for (int i = 0; i < halfview; i++) {
            double tang = (int32_t)i*VIEWGLOBAL/viewwidth/facedist;
            float angle = (float) atan(tang);
            int intang = (int) (angle*radtoint);
            fputc(intang & 0xFF, f);
            fputc((intang >> 8) & 0xFF, f);
        }
    }
    return fclose(f) != 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c, exe = os.path.join(tmp, "t.cpp"), os.path.join(tmp, "t")
    open(c, "w").write(prog)
    subprocess.check_call(["g++", "-O0", "-w", "-o", exe, c])
    subprocess.check_call([exe, os.path.abspath(out)])

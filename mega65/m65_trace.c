// The ray caster's inner loop (AsmRefresh in wl_draw.cpp), in assembly.
//
// m65_trace steps one ray through the map, exactly as the original's two
// loops do (the vertical-crossing loop and the horizontal-crossing loop,
// which hand over to each other), marking the tiles it crosses in spotvis,
// until something needs the C code:
//   1  a tile at xspot (vertical crossing): tilehit set
//   2  a tile at yspot (horizontal crossing): tilehit set
//   3  the map's edge on a vertical crossing   (the original: HitHorizBorder)
//   4  the map's edge on a horizontal crossing (the original: HitVertBorder)
//   (the original's "spot beyond the map" cannot happen: a spot is only used
//   inside the map)
// entry: 0 starts a ray (the vertical loop's top), 1 goes on after a tile the
// ray passes (the original's passvert), 2 the same for passhoriz.
//
// It works on the C globals (xtile, ytile, xtilestep, ytilestep,
// xintercept, yintercept, tilehit; xspot and yspot are not kept, nothing
// else uses them), with the ray's steps in m65_rxstep/m65_rystep and
// tilemap's and spotvis's addresses (chip RAM, 256-byte aligned, the same
// top byte) in m65_tm_base/m65_sv_base; the map is 64 x 64.
// The original read the map and wrote spotvis through a function call each.

#include <stdint.h>

int32_t m65_rxstep, m65_rystep;
uint32_t m65_tm_base, m65_sv_base;
uint8_t m65_trace(uint8_t entry);

__asm__(
    "  .zeropage m65_trp\n"
    "  .section .zp.bss,\"aw\",@nobits\n"
    "m65_trp: .zero 4\n"                        // the 32-bit pointer into the map
    "  .section .bss.m65_trace_tmp,\"aw\",@nobits\n"
    "m65_trs: .zero 2\n"                        // the spot (xtile * 64 + y, or x * 64 + ytile)

    "  .section .text.m65_trace,\"ax\",@progbits\n"
    "  .globl m65_trace\n"
    "  .type m65_trace,@function\n"
    "m65_trace:\n"
    "  ldx m65_tm_base+3\n stx m65_trp+3\n"    // (tilemap and spotvis: same top byte)
    "  cmp #1\n beq .Lpassvert\n"
    "  cmp #2\n bne .Lvtop\n jmp .Lpasshoriz\n"

    // ---- the vertical-crossing loop ----
    ".Lvtop:\n"
    // if (ytilestep == -1 && (yintercept >> 16) <= ytile) goto horizentry;
    // if (ytilestep ==  1 && (yintercept >> 16) >= ytile) goto horizentry;
    "  lda ytilestep+1\n bmi 1f\n"
    "  sec\n lda yintercept+2\n sbc ytile\n lda yintercept+3\n sbc ytile+1\n"
    "  bvc 2f\n eor #$80\n"
    "2: bpl .Lhentry\n bra .Lventry\n"
    "1: sec\n lda ytile\n sbc yintercept+2\n lda ytile+1\n sbc yintercept+3\n"
    "  bvc 3f\n eor #$80\n"
    "3: bpl .Lhentry\n"
    ".Lventry:\n"
    // the edge: (uint32)yintercept > 64 * 65536 - 1 || (word)xtile >= 64
    "  lda yintercept+3\n bne .Lvedge\n"
    "  lda yintercept+2\n cmp #$40\n bcs .Lvedge\n"
    "  lda xtile+1\n bne .Lvedge\n"
    "  lda xtile\n cmp #64\n bcs .Lvedge\n"
    // inside the map: the spot is xtile * 64 + (yintercept >> 16), < 4096
    "  lsr\n lsr\n sta m65_trs+1\n"
    "  lda xtile\n asl\n asl\n asl\n asl\n asl\n asl\n ora yintercept+2\n sta m65_trs\n"
    // tilehit = tilemap[spot] (the base's low byte is 0)
    "  sta m65_trp\n"
    "  clc\n lda m65_tm_base+1\n adc m65_trs+1\n sta m65_trp+1\n"
    "  lda m65_tm_base+2\n adc #0\n sta m65_trp+2\n"
    "  ldz #0\n lda [m65_trp],z\n beq .Lpassvert\n"
    "  sta tilehit\n ldx #0\n stx tilehit+1\n"
    "  lda #1\n rts\n"
    ".Lpassvert:\n"
    // spotvis[spot] = 1
    "  lda m65_trs\n sta m65_trp\n"
    "  clc\n lda m65_sv_base+1\n adc m65_trs+1\n sta m65_trp+1\n"
    "  lda m65_sv_base+2\n adc #0\n sta m65_trp+2\n"
    "  ldz #0\n lda #1\n sta [m65_trp],z\n"
    // xtile += xtilestep; yintercept += ystep
    "  clc\n lda xtile\n adc xtilestep\n sta xtile\n"
    "  lda xtile+1\n adc xtilestep+1\n sta xtile+1\n"
    "  clc\n lda yintercept\n adc m65_rystep\n sta yintercept\n"
    "  lda yintercept+1\n adc m65_rystep+1\n sta yintercept+1\n"
    "  lda yintercept+2\n adc m65_rystep+2\n sta yintercept+2\n"
    "  lda yintercept+3\n adc m65_rystep+3\n sta yintercept+3\n"
    "  jmp .Lvtop\n"

    ".Lvedge:\n lda #3\n rts\n"
    ".Lhedge:\n lda #4\n rts\n"

    // ---- the horizontal-crossing loop ----
    ".Lhtop:\n"
    // if (xtilestep == -1 && (xintercept >> 16) <= xtile) goto vertentry;
    // if (xtilestep ==  1 && (xintercept >> 16) >= xtile) goto vertentry;
    "  lda xtilestep+1\n bmi 1f\n"
    "  sec\n lda xintercept+2\n sbc xtile\n lda xintercept+3\n sbc xtile+1\n"
    "  bvc 2f\n eor #$80\n"
    "2: bmi .Lhentry\n jmp .Lventry\n"
    "1: sec\n lda xtile\n sbc xintercept+2\n lda xtile+1\n sbc xintercept+3\n"
    "  bvc 3f\n eor #$80\n"
    "3: bmi .Lhentry\n jmp .Lventry\n"
    ".Lhentry:\n"
    // the edge: (uint32)xintercept > 64 * 65536 - 1 || (word)ytile >= 64
    "  lda xintercept+3\n bne .Lhedge\n"
    "  lda xintercept+2\n cmp #$40\n bcs .Lhedge\n"
    "  lda ytile+1\n bne .Lhedge\n"
    "  lda ytile\n cmp #64\n bcs .Lhedge\n"
    // inside the map: the spot is (xintercept >> 16) * 64 + ytile, < 4096
    "  lda xintercept+2\n lsr\n lsr\n sta m65_trs+1\n"
    "  lda xintercept+2\n asl\n asl\n asl\n asl\n asl\n asl\n ora ytile\n sta m65_trs\n"
    // tilehit = tilemap[spot]
    "  sta m65_trp\n"
    "  clc\n lda m65_tm_base+1\n adc m65_trs+1\n sta m65_trp+1\n"
    "  lda m65_tm_base+2\n adc #0\n sta m65_trp+2\n"
    "  ldz #0\n lda [m65_trp],z\n beq .Lpasshoriz\n"
    "  sta tilehit\n ldx #0\n stx tilehit+1\n"
    "  lda #2\n rts\n"
    ".Lpasshoriz:\n"
    // spotvis[spot] = 1
    "  lda m65_trs\n sta m65_trp\n"
    "  clc\n lda m65_sv_base+1\n adc m65_trs+1\n sta m65_trp+1\n"
    "  lda m65_sv_base+2\n adc #0\n sta m65_trp+2\n"
    "  ldz #0\n lda #1\n sta [m65_trp],z\n"
    // ytile += ytilestep; xintercept += xstep
    "  clc\n lda ytile\n adc ytilestep\n sta ytile\n"
    "  lda ytile+1\n adc ytilestep+1\n sta ytile+1\n"
    "  clc\n lda xintercept\n adc m65_rxstep\n sta xintercept\n"
    "  lda xintercept+1\n adc m65_rxstep+1\n sta xintercept+1\n"
    "  lda xintercept+2\n adc m65_rxstep+2\n sta xintercept+2\n"
    "  lda xintercept+3\n adc m65_rxstep+3\n sta xintercept+3\n"
    "  jmp .Lhtop\n"
    "  .size m65_trace, . - m65_trace\n");

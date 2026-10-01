#include <mega65.h>
#include <stdint.h>
#include <string.h>

#include "m65_video.h"

// ---------------------------------------------------------------------------
// DMA
// ---------------------------------------------------------------------------

// Enhanced-DMA job: option bytes followed by an F018B list.
struct dma_job {
    uint8_t opt_f018b;        // 0x0b
    uint8_t opt_src_mb;       // 0x80
    uint8_t src_mb;
    uint8_t opt_dst_mb;       // 0x81
    uint8_t dst_mb;
    uint8_t opt_dst_skip;     // 0x85: destination step, whole bytes
    uint8_t dst_skip;
    uint8_t opt_src_frac;     // 0x82: source step, fraction (8.8 fixed point)
    uint8_t src_frac;
    uint8_t opt_src_int;      // 0x83: source step, whole bytes
    uint8_t src_int;
    uint8_t opt_end;          // 0x00
    struct DMAList_F018B list;
};

static struct dma_job job;

// A count of 0 does nothing (to the DMA controller it means 64KB). Callers
// rely on this: e.g. m65_takeover passes linker-symbol sizes, which the
// compiler assumes to be non-zero, so a guard there is optimised away.
static void dma_run(uint8_t cmd, uint32_t dst, uint32_t src, uint16_t count,
                    uint8_t dstskip, uint16_t srcstep)
{
    if (!count)
        return;
    job.opt_f018b  = ENABLE_F018B_OPT;
    job.opt_src_mb = SRC_ADDR_BITS_OPT;
    job.src_mb     = (uint8_t)(src >> 20);
    job.opt_dst_mb = DST_ADDR_BITS_OPT;
    job.dst_mb     = (uint8_t)(dst >> 20);
    job.opt_dst_skip = DST_SKIP_RATE_OPT;
    job.dst_skip   = dstskip;
    job.opt_src_frac = 0x82;
    job.src_frac   = (uint8_t)srcstep;
    job.opt_src_int = 0x83;
    job.src_int    = (uint8_t)(srcstep >> 8);
    job.opt_end    = 0;

    job.list.command     = cmd;
    job.list.count       = count;
    job.list.source_addr = (uint16_t)src;
    job.list.source_bank = (uint8_t)(src >> 16) & 0x0f;
    job.list.dest_addr   = (uint16_t)dst;
    job.list.dest_bank   = (uint8_t)(dst >> 16) & 0x0f;
    job.list.command_msb = 0;
    job.list.modulo      = 0;

    // The DMA engine reads `job` and writes memory behind the compiler's back:
    // barriers stop it from dropping or reordering the list stores (which it
    // did once LTO inlined two back-to-back copies), and from assuming the
    // destination still holds what it held before.
    __asm__ volatile("" ::: "memory");
    DMA.enable_f018b = 1;
    DMA.addr_mb   = 0;
    DMA.addr_bank = 0;
    DMA.addr_msb  = (uint8_t)((uint16_t)&job >> 8);
    DMA.trigger_enhanced = (uint8_t)((uint16_t)&job & 0xff);
    __asm__ volatile("" ::: "memory");
}

void m65_dma_fill(uint32_t dst, uint8_t value, uint16_t count)
{
    dma_run(DMA_FILL_CMD, dst, value, count, 1, 0x100);
}

void m65_dma_copy(uint32_t dst, uint32_t src, uint16_t count)
{
    dma_run(DMA_COPY_CMD, dst, src, count, 1, 0x100);
}

void m65_dma_fill_skip(uint32_t dst, uint8_t value, uint16_t count, uint8_t dstskip)
{
    dma_run(DMA_FILL_CMD, dst, value, count, dstskip, 0x100);
}


// A scaled copy: the source address steps by srcstep (8.8 fixed point: 0x100
// is one byte) for each destination byte, the destination by dstskip.
// The renderer's workhorse (a job per wall column and per sprite column),
// so in assembly, with a job list of its own whose constant bytes are set
// already: dma_run in C took ~630 cycles a job before the DMA even started.
// (Arguments: dst in A, X, __rc2, __rc3; src in __rc4-__rc7; count in
// __rc8/9; srcstep in __rc10/11; dstskip in __rc12. A count of 0 does
// nothing, as for dma_run.)
__asm__(
    // m65_bankmb b2, b3, bank, mb: from a 28-bit address's bytes 2 and 3,
    // the DMA list's bank (bits 16-19) to `bank` and the option's megabyte
    // (bits 20-27) to `mb`. (Uses __rc13.)
    "  .macro m65_bankmb b2, b3, bank, mb\n"
    "  lda \\b2\n and #$0f\n sta \\bank\n"
    "  lda \\b3\n asl\n asl\n asl\n asl\n sta __rc13\n"
    "  lda \\b2\n lsr\n lsr\n lsr\n lsr\n ora __rc13\n sta \\mb\n"
    "  .endm\n"
    // m65_dmago: start the job list m65_sjob (enhanced, F018B, in bank 0).
    "  .macro m65_dmago\n"
    "  lda #1\n sta $d703\n"                        // (F018B)
    "  lda #0\n sta $d702\n sta $d704\n"          // the list: bank 0,
    "  lda #>m65_sjob\n sta $d701\n"
    "  lda #<m65_sjob\n sta $d705\n"               // and go
    "  .endm\n"

    "  .section .data.m65_sjob,\"aw\",@progbits\n"
    "m65_sjob:\n"
    "  .byte $0b\n"                    // 0: F018B lists
    "  .byte $80, 0\n"                 // 1: source MB (2)
    "  .byte $81, 0\n"                 // 3: destination MB (4)
    "  .byte $85, 1\n"                 // 5: destination step (6)
    "  .byte $82, 0\n"                 // 7: source step, fraction (8)
    "  .byte $83, 1\n"                 // 9: source step, whole bytes (10)
    "  .byte $00\n"                    // 11: end of options
    "  .byte $00\n"                    // 12: copy
    "  .word 0\n"                      // 13: count
    "  .word 0\n"                      // 15: source
    "  .byte 0\n"                      // 17: source bank
    "  .word 0\n"                      // 18: destination
    "  .byte 0\n"                      // 20: destination bank
    "  .byte 0\n"                      // 21: command, high byte
    "  .word 0\n"                      // 22: modulo

    "  .section .text.m65_dma_scale,\"ax\",@progbits\n"
    "  .globl m65_dma_scale\n"
    "  .type m65_dma_scale,@function\n"
    "m65_dma_scale:\n"
    "  sta m65_sjob+18\n stx m65_sjob+19\n"        // destination
    "  m65_bankmb __rc2, __rc3, m65_sjob+20, m65_sjob+4\n"
    "  lda __rc4\n sta m65_sjob+15\n lda __rc5\n sta m65_sjob+16\n"   // source
    "  m65_bankmb __rc6, __rc7, m65_sjob+17, m65_sjob+2\n"
    "  lda __rc10\n sta m65_sjob+8\n lda __rc11\n sta m65_sjob+10\n"  // steps
    "  lda __rc12\n sta m65_sjob+6\n"
    "  lda __rc8\n sta m65_sjob+13\n ora __rc9\n beq 1f\n"            // count
    "  lda __rc9\n sta m65_sjob+14\n"
    "  m65_dmago\n"
    "1: rts\n"
    "  .size m65_dma_scale, . - m65_dma_scale\n"

    // m65_dma_col_setup(base, srcstep): base (A, X, __rc2, __rc3) and the
    // source step (__rc4/5) for the m65_dma_segs calls that follow.
    "  .section .bss.m65_colvars,\"aw\",@nobits\n"
    "m65_colbase: .zero 2\n"
    "m65_colbank: .zero 1\n"
    "m65_colmb:   .zero 1\n"
    "m65_colstep: .zero 2\n"
    "  .section .text.m65_dma_col_setup,\"ax\",@progbits\n"
    "  .globl m65_dma_col_setup\n"
    "  .type m65_dma_col_setup,@function\n"
    "m65_dma_col_setup:\n"
    "  sta m65_colbase\n stx m65_colbase+1\n"
    "  m65_bankmb __rc2, __rc3, m65_colbank, m65_colmb\n"
    "  lda __rc4\n sta m65_colstep\n lda __rc5\n sta m65_colstep+1\n"
    "  rts\n"
    "  .size m65_dma_col_setup, . - m65_dma_col_setup\n"

    // m65_dma_segs(xoff, n): for k < n (__rc2), a scaled copy of
    // m65_segcount[k] rows from m65_segsrc[k] to base + xoff (A/X) +
    // m65_segtop[k] * 8, every 8th byte (one screen column's posts).
    "  .section .text.m65_dma_segs,\"ax\",@progbits\n"
    "  .globl m65_dma_segs\n"
    "  .type m65_dma_segs,@function\n"
    "m65_dma_segs:\n"
    "  sta __rc6\n stx __rc7\n"
    "  lda m65_colmb\n sta m65_sjob+4\n"          // (as m65_dma_scale may have
    "  lda #8\n sta m65_sjob+6\n"                 //  changed them since)
    "  lda m65_colstep\n sta m65_sjob+8\n"
    "  lda m65_colstep+1\n sta m65_sjob+10\n"
    "  lda #0\n sta m65_sjob+14\n"
    "  ldy #0\n"
    "2: cpy __rc2\n bcs 3f\n"
    "  lda m65_segtop,y\n sta __rc8\n lda #0\n sta __rc9\n"
    "  asl __rc8\n rol __rc9\n asl __rc8\n rol __rc9\n asl __rc8\n rol __rc9\n"
    "  clc\n lda __rc8\n adc __rc6\n sta __rc8\n lda __rc9\n adc __rc7\n sta __rc9\n"
    "  clc\n lda __rc8\n adc m65_colbase\n sta m65_sjob+18\n"
    "  lda __rc9\n adc m65_colbase+1\n sta m65_sjob+19\n"
    "  lda m65_colbank\n adc #0\n sta m65_sjob+20\n"
    "  lda m65_segcount,y\n sta m65_sjob+13\n"
    "  tya\n asl\n asl\n tax\n"
    "  lda m65_segsrc,x\n sta m65_sjob+15\n"
    "  lda m65_segsrc+1,x\n sta m65_sjob+16\n"
    "  lda m65_segsrc+2,x\n and #$0f\n sta m65_sjob+17\n"
    "  lda m65_segsrc+3,x\n asl\n asl\n asl\n asl\n sta __rc10\n"
    "  lda m65_segsrc+2,x\n lsr\n lsr\n lsr\n lsr\n ora __rc10\n sta m65_sjob+2\n"
    "  m65_dmago\n"
    "  iny\n bra 2b\n"
    "3: rts\n"
    "  .size m65_dma_segs, . - m65_dma_segs\n"
);

// A screen column's posts, for m65_dma_segs.
uint8_t m65_segtop[M65_MAXSEGS], m65_segcount[M65_MAXSEGS];
uint32_t m65_segsrc[M65_MAXSEGS];

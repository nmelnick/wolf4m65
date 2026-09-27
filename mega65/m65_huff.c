#include <stdint.h>

#include "m65_huff.h"

#define BUF 256

// The decoder proper, in assembly (m65_huff_run below): it walks the tree a
// bit at a time from m65_huff_inbuf into m65_huff_outbuf, and returns 1 when
// it has written m65_huff_want bytes (0: 256) or 0 when it needs the next
// BUF bytes of input (m65_huff_empty set). The tree walk, its input byte
// and the buffer positions carry over from one call to the next.
uint8_t m65_huff_inbuf[BUF], m65_huff_outbuf[BUF];
uint8_t m65_huff_in, m65_huff_out, m65_huff_empty, m65_huff_want;
const m65_huffnode *m65_huff_table, *m65_huff_head;
uint8_t m65_huff_run(void);

__asm__(
    "  .zeropage m65_hfn\n"
    "  .section .zp.bss,\"aw\",@nobits\n"
    "m65_hfn: .zero 2\n"                        // the current node
    "  .section .bss.m65_huff_tmp,\"aw\",@nobits\n"
    "m65_hfv: .zero 1\n"                        // the input byte, shifted right a bit at a time
    "m65_hfb: .zero 1\n"                        // its bits left
    "m65_hft: .zero 2\n"                        // (the node's value)

    "  .section .text.m65_huff_run,\"ax\",@progbits\n"
    "  .globl m65_huff_run\n"
    "  .type m65_huff_run,@function\n"
    "m65_huff_run:\n"
    "  ldy m65_huff_in\n"
    "  ldx m65_huff_out\n"
    ".Lhbit:\n"
    "  dec m65_hfb\n bpl .Lhhave\n"
    "  lda m65_huff_empty\n bne .Lhstarve\n"    // (the input buffer is used up)
    "  lda m65_huff_inbuf,y\n sta m65_hfv\n"
    "  iny\n bne 1f\n inc m65_huff_empty\n"
    "1: lda #7\n sta m65_hfb\n"
    ".Lhhave:\n"
    // the next bit (lowest first) picks bit0 or bit1 of the node
    "  ldz #0\n lsr m65_hfv\n bcc 2f\n ldz #2\n"
    "2: lda (m65_hfn),z\n sta m65_hft\n"
    "  inz\n lda (m65_hfn),z\n bne .Lhnode\n"
    // < 256: a byte out, and back to the head node
    "  lda m65_hft\n sta m65_huff_outbuf,x\n inx\n"
    "  lda m65_huff_head\n sta m65_hfn\n"
    "  lda m65_huff_head+1\n sta m65_hfn+1\n"
    "  dec m65_huff_want\n bne .Lhbit\n"
    "  lda #1\n bra .Lhret\n"
    // else the node at table + (value - 256) * 4 (value - 256 < 256)
    ".Lhnode:\n"
    "  lda #0\n sta m65_hft+1\n"
    "  lda m65_hft\n asl\n rol m65_hft+1\n asl\n rol m65_hft+1\n"
    "  clc\n adc m65_huff_table\n sta m65_hfn\n"
    "  lda m65_hft+1\n adc m65_huff_table+1\n sta m65_hfn+1\n"
    "  bra .Lhbit\n"
    ".Lhstarve:\n"
    "  lda #0\n sta m65_hfb\n"                  // (the dec made it $FF)
    ".Lhret:\n"
    "  sty m65_huff_in\n"
    "  stx m65_huff_out\n"
    "  ldz #0\n"
    "  rts\n"
    "  .size m65_huff_run, . - m65_huff_run\n"
);

void far_huff_expand(farptr src, farptr dst, uint32_t length,
                     const m65_huffnode *table)
{
    extern uint8_t m65_hfb;
    extern const m65_huffnode *m65_hfn;
    uint16_t n;

    m65_huff_table = table;
    m65_huff_head = table + 254;        // head node is always 254
    m65_hfn = m65_huff_head;
    m65_hfb = 0;
    m65_huff_empty = 1;                 // (read on the first bit)

    while (length)
    {
        n = length > BUF ? BUF : (uint16_t)length;
        m65_huff_want = (uint8_t)n;     // (256: 0)
        m65_huff_out = 0;
        while (!m65_huff_run())
        {
            far_read(m65_huff_inbuf, src, BUF);
            src = FAR_ADD(src, BUF);
            m65_huff_in = 0;
            m65_huff_empty = 0;
        }
        far_write(dst, m65_huff_outbuf, n);
        dst = FAR_ADD(dst, n);
        length -= n;
    }
}

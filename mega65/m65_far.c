#include <stdint.h>

#include "m65_dos.h"
#include "m65_far.h"
#include "m65_video.h"      // m65_dma_copy

void far_read(void *dst, farptr src, uint16_t count)
{
    m65_dma_copy((uint32_t)(uintptr_t)dst, src.a, count);
}

void far_write(farptr dst, const void *src, uint16_t count)
{
    m65_dma_copy(dst.a, (uint32_t)(uintptr_t)src, count);
}

void far_copy(farptr dst, farptr src, uint16_t count)
{
    m65_dma_copy(dst.a, src.a, count);
}

// Bytes, words and longs use the 45GS02's flat 32-bit indirect addressing,
// through a pointer in zero page: the pointer is set once, and Z picks the
// byte (a few cycles each, instead of a DMA job). Calling convention: the
// address (a farptr) in A, X, __rc2, __rc3; a value to store in __rc4
// (__rc5); a value read back in A (X, __rc2, __rc3). Z is left 0.
#define FAR_SETPTR \
    "  sta m65_flatptr\n stx m65_flatptr+1\n" \
    "  lda __rc2\n sta m65_flatptr+2\n lda __rc3\n sta m65_flatptr+3\n"
#define FAR_FUNC(name) \
    ".section .text." #name ",\"ax\",@progbits\n" \
    ".globl " #name "\n.type " #name ",@function\n" #name ":\n"

__asm__(
    ".zeropage m65_flatptr\n"
    ".section .zp.bss,\"aw\",@nobits\n"
    "m65_flatptr: .zero 4\n"

    FAR_FUNC(far_peek)
    FAR_SETPTR
    "  ldz #0\n lda [m65_flatptr],z\n rts\n"

    FAR_FUNC(far_poke)
    FAR_SETPTR
    "  lda __rc4\n ldz #0\n sta [m65_flatptr],z\n rts\n"

    FAR_FUNC(far_peekw)
    FAR_SETPTR
    "  ldz #1\n lda [m65_flatptr],z\n tax\n"
    "  ldz #0\n lda [m65_flatptr],z\n rts\n"

    FAR_FUNC(far_pokew)
    FAR_SETPTR
    "  ldz #0\n lda __rc4\n sta [m65_flatptr],z\n"
    "  inz\n lda __rc5\n sta [m65_flatptr],z\n ldz #0\n rts\n"

    FAR_FUNC(far_peekl)
    FAR_SETPTR
    "  ldz #3\n lda [m65_flatptr],z\n sta __rc3\n"
    "  dez\n lda [m65_flatptr],z\n sta __rc2\n"
    "  dez\n lda [m65_flatptr],z\n tax\n"
    "  dez\n lda [m65_flatptr],z\n rts\n");


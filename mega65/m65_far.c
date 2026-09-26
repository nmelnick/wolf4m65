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

// Single bytes use the 45GS02's flat 32-bit indirect addressing, through a
// pointer in zero page: a few cycles instead of a DMA job.
static volatile uint32_t flatptr __attribute__((section(".zp.bss")));

uint8_t far_peek(farptr p)
{
    uint8_t v;
    flatptr = p.a;
    __asm__ volatile("ldz #0\n lda [%1],z" : "=a"(v) : "i"(&flatptr) : "memory");
    return v;
}

void far_poke(farptr p, uint8_t v)
{
    flatptr = p.a;
    __asm__ volatile("ldz #0\n sta [%0],z" :: "i"(&flatptr), "a"(v) : "memory");
}

void far_pokew(farptr p, uint16_t v)
{
    far_poke(p, (uint8_t)v);
    far_poke(FAR_ADD(p, 1), (uint8_t)(v >> 8));
}

uint16_t far_peekw(farptr p)
{
    return far_peek(p) | (uint16_t)far_peek(FAR_ADD(p, 1)) << 8;
}

uint32_t far_peekl(farptr p)
{
    return far_peekw(p) | (uint32_t)far_peekw(FAR_ADD(p, 2)) << 16;
}


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

uint16_t far_peekw(farptr p)
{
    return far_peek(p) | (uint16_t)far_peek(FAR_ADD(p, 1)) << 8;
}

uint32_t far_peekl(farptr p)
{
    return far_peekw(p) | (uint32_t)far_peekw(FAR_ADD(p, 2)) << 16;
}

static uint32_t file_next = ATTIC_FILES;

farptr far_load_file(const char *name, uint32_t *size)
{
    farptr at = FAR(file_next);
    uint32_t got;
    int fd;

    fd = m65_dos_open(name);
    if (fd < 0)
        return FARNULL;
    got = m65_dos_read(fd, at.a, ATTIC_FILES_END - file_next);
    m65_dos_close(fd);

    file_next = (file_next + got + 255) & ~255UL;
    if (size)
        *size = got;
    return at;
}

static uint32_t heap_next = ATTIC_HEAP;

farptr far_alloc(uint32_t size)
{
    farptr p = FAR(heap_next);
    if (size > ATTIC_HEAP_END - heap_next)
        return FARNULL;
    heap_next += (size + 1) & ~1UL;
    return p;
}

uint32_t far_heap_used(void)
{
    return heap_next - ATTIC_HEAP;
}

static uint32_t chip_next = CHIP_HEAP;

farptr far_alloc_chip(uint16_t size)
{
    farptr p = FAR(chip_next);
    if (size > CHIP_HEAP_END - chip_next)
        return FARNULL;
    chip_next += (size + 1) & ~1U;
    return p;
}

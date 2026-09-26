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

uint8_t far_peek(farptr p)
{
    uint8_t v;
    far_read(&v, p, 1);
    return v;
}

uint16_t far_peekw(farptr p)
{
    uint16_t v;
    far_read(&v, p, 2);
    return v;
}

uint32_t far_peekl(farptr p)
{
    uint32_t v;
    far_read(&v, p, 4);
    return v;
}

void far_poke(farptr p, uint8_t v)
{
    far_write(p, &v, 1);
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

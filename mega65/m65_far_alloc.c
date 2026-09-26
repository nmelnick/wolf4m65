// Far memory: whole-file loading and the far heaps (overlay-able; the far
// access primitives in m65_far.c stay resident).
#include <stdint.h>

#include "m65_dos.h"
#include "m65_far.h"

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

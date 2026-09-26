// Large-file test: load VSWAP.WL1 into attic RAM and sample it.
#include <stdint.h>
#include "m65_debug.h"
#include "m65_dos.h"
#include "m65_video.h"

#define ATTIC   0x8200000UL         // MB $82: clear of the overlay area
#define REPORT  0x50000UL
#define SAMPLE  64

struct report {
    uint8_t  magic;
    int8_t   init_rc, fd, close_rc;
    uint32_t got;
    uint32_t mid_offset;
};

static struct report rep;
static uint8_t samples[3][SAMPLE];

int main(void)
{
    uint32_t mid;

    __asm__ volatile("sei");

    rep.init_rc = (int8_t)m65_dos_init();
    rep.fd      = (int8_t)m65_dos_open("VSWAP.WL1");
    rep.got     = m65_dos_read(rep.fd, ATTIC, 0x800000UL);
    rep.close_rc = (int8_t)m65_dos_close(rep.fd);

    mid = (rep.got / 2) & ~0x3FUL;
    rep.mid_offset = mid;

    m65_dma_copy((uint32_t)(uintptr_t)samples[0], ATTIC, SAMPLE);
    m65_dma_copy((uint32_t)(uintptr_t)samples[1], ATTIC + mid, SAMPLE);
    m65_dma_copy((uint32_t)(uintptr_t)samples[2], ATTIC + rep.got - SAMPLE, SAMPLE);

    rep.magic = 0xEE;
    m65_dma_copy(REPORT, (uint32_t)(uintptr_t)&rep, sizeof rep);
    m65_dma_copy(REPORT + 0x40, (uint32_t)(uintptr_t)samples, sizeof samples);
    m65_debug_puts("TEST-DONE");
    for (;;) {}
}

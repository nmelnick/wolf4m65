// File layer test: reads TEST.BIN from the SD card root into far memory, then
// reports what happened in one block at $50000 for inspection with -dumpmem.
#include <stdint.h>
#include "m65_debug.h"
#include "m65_dos.h"
#include "m65_video.h"

#define BUF 0x40000UL           // far destination for the file data
#define REPORT 0x50000UL

struct report {
    uint8_t  magic;             // 0xEE once the test ran to the end
    int8_t   init_rc;           // 0 = ok
    int8_t   fd;                // >= 0 = ok
    int8_t   close_rc;          // 0 = ok
    uint32_t got;               // bytes read
};

static struct report rep;

int main(void)
{
    __asm__ volatile("sei");

    rep.init_rc  = (int8_t)m65_dos_init();
    rep.fd       = (int8_t)m65_dos_open("TEST.BIN");
    rep.got      = m65_dos_read(rep.fd, BUF, 2000);
    rep.close_rc = (int8_t)m65_dos_close(rep.fd);
    rep.magic    = 0xEE;

    m65_dma_copy(REPORT, (uint32_t)(uintptr_t)&rep, sizeof rep);
    m65_debug_puts("TEST-DONE");
    for (;;) {}
}

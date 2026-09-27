// Write test: in the SD card's WOLF4M65 directory, overwrite the 1024-byte
// file TESTW.BIN with a pattern (two sectors), then open it again and read it
// back; also read TEST.BIN there. Reports in one block at $50000 for
// tools/check_writetest.py (-dumpmem).
#include <stdint.h>
#include "m65_debug.h"
#include "m65_dos.h"
#include "m65_video.h"

const char m65_dos_subdir[] = "WOLF4M65";

#define PATTERN 0x40000UL       // what is written
#define BACK    0x41000UL       // what is read back
#define REPORT  0x50000UL

struct report {
    uint8_t  magic;             // 0xEE once the test ran to the end
    int8_t   init_rc;           // 0 = ok
    int8_t   wfd;               // >= 0 = ok
    int8_t   w1, w2, w3;        // the two writes (0) and one past the end (-1)
    int8_t   rfd;
    int8_t   sub_fd;            // TEST.BIN, only in WOLF4M65
    uint32_t got;               // bytes read back
};

static struct report rep;
static uint8_t row[256];

int main(void)
{
    uint16_t i;
    __asm__ volatile("sei");

    for (i = 0; i < 256; i++)
        row[i] = (uint8_t)(i * 13 + 5);
    for (i = 0; i < 4; i++)
        m65_dma_copy(PATTERN + i * 256, (uint32_t)(uintptr_t)row, 256);
    m65_dma_fill(BACK, 0x55, 1024);

    rep.init_rc = (int8_t)m65_dos_init();
    rep.sub_fd  = (int8_t)m65_dos_open("test.bin");
    if (rep.sub_fd >= 0)
        m65_dos_close(rep.sub_fd);
    rep.wfd = (int8_t)m65_dos_open("TESTW.BIN");
    rep.w1 = (int8_t)m65_dos_write512(rep.wfd, PATTERN, 512);
    rep.w2 = (int8_t)m65_dos_write512(rep.wfd, PATTERN + 512, 300);
    rep.w3 = (int8_t)m65_dos_write512(rep.wfd, PATTERN, 512);
    m65_dos_close(rep.wfd);

    rep.rfd = (int8_t)m65_dos_open("TESTW.BIN");
    rep.got = m65_dos_read(rep.rfd, BACK, 1024);
    m65_dos_close(rep.rfd);
    rep.magic = 0xEE;

    m65_dma_copy(REPORT, (uint32_t)(uintptr_t)&rep, sizeof rep);
    m65_debug_puts("TEST-DONE");
    for (;;) {}
}

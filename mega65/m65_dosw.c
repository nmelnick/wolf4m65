// Writing files through the hypervisor (Hyppo DOS): apart from m65_dos.c,
// which is resident, as the game only writes from its menus (an overlay).
//
// Hyppo's writefile writes the SD sector buffer ($FFD6E00) over the open
// file's current sector and moves on to the next; it cannot allocate, so
// only an existing file's sectors can be overwritten.

#include <stdint.h>

#include "m65_dos.h"
#include "m65_video.h"      // m65_dma_copy, m65_dma_fill

#define HYPPO_WRITEFILE  0x1C

#define SD_SECTOR_BUFFER 0xFFD6E00UL
#define SD_CTL_MAPSDBUF  (*(volatile uint8_t *)0xD689)   // bit 7: the SD buffer, not the FDC's

int m65_dos_write512(int fd, uint32_t src, uint16_t count)
{
    uint8_t a = HYPPO_WRITEFILE, x = (uint8_t)fd, ok = 0;

    if (count > 512)
        return -1;
    SD_CTL_MAPSDBUF |= 0x80;
    m65_dma_copy(SD_SECTOR_BUFFER, src, count);
    if (count < 512)
        m65_dma_fill(SD_SECTOR_BUFFER + count, 0, 512 - count);
    __asm__ volatile(
        "sta $d640\n"
        "clv\n"
        "bcc 1f\n"
        "inc %[ok]\n"
        "1:\n"
        : "+a"(a), "+x"(x), [ok] "+r"(ok)
        :
        : "y", "c", "v", "memory");
    return ok ? 0 : -1;
}

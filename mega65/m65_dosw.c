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

// Create a file of `size` bytes (< 16MB) in the current directory; it must
// not exist yet. Hyppo's mkfile (marked unfinished in its source): the name
// is 8.3; it allocates a contiguous run of clusters, in whole FAT sectors'
// worth (128 clusters), all chained to the file although the directory
// entry has `size`; the contents are whatever was on the card. Returns 0
// on success.
#define HYPPO_SETNAME    0x2E
#define namebuf ((char *)0x0200)        // (as m65_dos.c: setname's buffer)

int m65_dos_mkfile(const char *name, uint32_t size)
{
    uint8_t a = HYPPO_SETNAME, x = 0x00, y = 0x02, ok = 0, i;

    for (i = 0; name[i] && i < 63; i++)
        namebuf[i] = name[i];
    namebuf[i] = 0;
    __asm__ volatile(
        "sta $d640\n"
        "clv\n"
        "bcc 1f\n"
        "inc %[ok]\n"
        "1:\n"
        : "+a"(a), "+x"(x), "+y"(y), [ok] "+r"(ok)
        :
        : "c", "v", "memory");
    if (!ok)
        return -1;
    ok = 0;
    x = (uint8_t)size;
    y = (uint8_t)(size >> 8);
    i = (uint8_t)(size >> 16);
    __asm__ volatile(
        "taz\n"                         // (Z: the size's top byte)
        "lda #$1e\n"                    // (HYPPO_MKFILE)
        "sta $d640\n"
        "clv\n"
        "ldz #0\n"                      // (the compiler's code expects Z = 0)
        "bcc 1f\n"
        "inc %[ok]\n"
        "1:\n"
        : "+a"(i), "+x"(x), "+y"(y), [ok] "+r"(ok)
        :
        : "c", "v", "memory");
    return ok ? 0 : -1;
}

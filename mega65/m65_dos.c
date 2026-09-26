#include <stdint.h>
#include <string.h>

#include "m65_dos.h"
#include "m65_video.h"      // m65_dma_copy

// Hyppo function numbers.
#define HYPPO_OPENFILE   0x18
#define HYPPO_READFILE   0x1A
#define HYPPO_CLOSEFILE  0x20
#define HYPPO_CLOSEALL   0x22
#define HYPPO_SETNAME    0x2E
#define HYPPO_FINDFILE   0x34
#define HYPPO_CDROOTDIR  0x3C

#define SD_SECTOR_BUFFER 0xFFD6E00UL
#define SD_CTL_MAPSDBUF  (*(volatile uint8_t *)0xD689)   // bit 7: show the SD
                                                         // buffer, not the FDC's

// Filename buffer. Hyppo's setname only honours the pointer's high byte, and
// the buffer must be below $8000 (setname fails with error $10 otherwise).
// Neither can be relied on for a linker-placed buffer in a large program, so
// it lives at a fixed address: page $02, BASIC's input buffer, which is free
// once the program owns the machine (no BASIC, no KERNAL calls).
#define namebuf ((char *)0x0200)
#define NAMEBUF_SIZE 64

struct hres { uint8_t a, x, y, ok; };

// Call a Hyppo function. `ok` is 1 if it returned with carry set.
static struct hres hcall(uint8_t fn, uint8_t x, uint8_t y)
{
    struct hres r;
    uint8_t a = fn, ok = 0;

    __asm__ volatile(
        "sta $d640\n"
        "clv\n"
        "bcc 1f\n"
        "inc %[ok]\n"
        "1:\n"
        : "+a"(a), "+x"(x), "+y"(y), [ok] "+r"(ok)
        :
        : "c", "v", "memory");   // Hyppo reads/writes memory (e.g. namebuf)

    r.a = a; r.x = x; r.y = y; r.ok = ok;
    return r;
}

int m65_dos_init(void)
{
    struct hres r;
    r = hcall(HYPPO_CLOSEALL, 0, 0);
    r = hcall(HYPPO_CDROOTDIR, 0, 0);
    return r.ok ? 0 : -1;
}

int m65_dos_open(const char *name)
{
    struct hres r;
    size_t n = strlen(name);

    if (n >= NAMEBUF_SIZE)
        return -1;
    memcpy(namebuf, name, n + 1);

    r = hcall(HYPPO_SETNAME, (uint8_t)(uintptr_t)namebuf,
              (uint8_t)((uintptr_t)namebuf >> 8));
    if (!r.ok)
        return -1;

    // openfile opens whatever findfile located, so findfile is mandatory.
    r = hcall(HYPPO_FINDFILE, 0, 0);
    if (!r.ok)
        return -1;

    r = hcall(HYPPO_OPENFILE, 0, 0);
    return r.ok ? (int)r.a : -1;
}

int m65_dos_close(int fd)
{
    struct hres r = hcall(HYPPO_CLOSEFILE, (uint8_t)fd, 0);
    return r.ok ? 0 : -1;
}

uint16_t m65_dos_read512(int fd, uint32_t dst)
{
    struct hres r = hcall(HYPPO_READFILE, (uint8_t)fd, 0);
    uint16_t got;

    if (!r.ok)
        return M65_DOS_EOF;
    got = ((uint16_t)r.y << 8) | r.x;      // bytes read, in the sector buffer
    SD_CTL_MAPSDBUF |= 0x80;
    if (got)
        m65_dma_copy(dst, SD_SECTOR_BUFFER, got);
    return got;
}

uint32_t m65_dos_read(int fd, uint32_t dst, uint32_t count)
{
    uint32_t total = 0;

    while (total < count) {
        uint16_t got = m65_dos_read512(fd, dst + total);
        if (got == 0 || got == M65_DOS_EOF)
            break;
        total += got;
    }
    return total > count ? count : total;
}

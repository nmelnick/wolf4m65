#include <stdint.h>
#include <string.h>

#include "m65_dos.h"
#include "m65_hyppo.h"
#include "m65_video.h"      // m65_dma_copy

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

// Locate a file or directory in the current directory (for openfile/chdir).
static uint8_t find(const char *name)
{
    size_t n = strlen(name);

    if (n >= NAMEBUF_SIZE)
        return 0;
    memcpy(namebuf, name, n + 1);
    if (!hcall(HYPPO_SETNAME, (uint8_t)(uintptr_t)namebuf,
               (uint8_t)((uintptr_t)namebuf >> 8)).ok)
        return 0;
    return hcall(HYPPO_FINDFILE, 0, 0).ok;
}

// The program's directory, if it defines one (m65_platform.c: the game's
// WOLF4M65); where it is missing, the root directory is used.
extern const char m65_dos_subdir[] __attribute__((weak));

int m65_dos_init(void)
{
    struct hres r;
    r = hcall(HYPPO_CLOSEALL, 0, 0);
    r = hcall(HYPPO_CDROOTDIR, 0, 0);
    if (r.ok && m65_dos_subdir && find(m65_dos_subdir))
        hcall(HYPPO_CHDIR, 0, 0);
    return r.ok ? 0 : -1;
}

int m65_dos_open(const char *name)
{
    struct hres r;

    // openfile opens whatever findfile located, so findfile is mandatory.
    if (!find(name))
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

// In the game, in .midtext: resident, but loaded with WOLF.DAT, so not
// usable before (m65_startup reads WOLF.DAT itself); the PRG is full.
#ifdef M65_RESIDENT
__attribute__((section(".midtext.m65_dos_read")))
#endif
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

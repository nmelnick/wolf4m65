// POSIX file access for Wolf4SDL, on top of attic RAM. The files are in the
// SD card's WOLF4M65 directory (m65_dos_subdir, m65_platform.c; the root if
// it is missing).
//
// Data files: open() loads the whole file from the SD card into attic RAM
// the first time (see far_load_file) and remembers it; read() and lseek()
// then work on that copy. m65_file_far() gives the file's far address for
// code that wants to read it in place, such as the graphics and audio caches.
//
// Save games and the config file (SAVEGAM?.*, CONFIG.*) are written too,
// all in one file, SAVES.DAT: Hyppo can only overwrite an existing file's
// sectors (and make one, not quite finished), not make one longer, so it is
// made at its full size, SAVE_NSLOTS slots of SAVE_SLOT bytes (0: the
// config, 1-10: SAVEGAM0-9), and each is overwritten in place: a header
// ("WM65", the length of what follows), then the contents; a slot without
// the header counts as a missing file. The game makes SAVES.DAT itself when
// there is none (saves_create). The whole of SAVES.DAT is read into
// attic RAM the first time, and reads come from that copy. Writes go to the
// copy, then on close() to the SD card: Hyppo cannot seek, so the file is
// read up to the slot, and the slot's sectors written. One at a time is open.

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "m65_debug.h"
#include "m65_dos.h"
#include "m65_far.h"
#include "m65_posix.h"

#define MAXLOADED 12
#define MAXOPEN   6

#define SAVE_FD     MAXOPEN     // the save/config file's descriptor
#define SAVE_HDR    16          // "WM65", the length (32 bits), zeros
#define SAVE_SLOT   32768UL     // (a whole number of sectors)
#define SAVE_NSLOTS 11
#define SAVE_MAX    (SAVE_SLOT - SAVE_HDR)
#define SAVE_FILE   "SAVES" M65_VSUFFIX ".DAT"    // (M65_VSUFFIX: the Makefile's, see m65_compat.h)

static struct {
    uint8_t  open, writing;
    uint8_t  slot;
    uint32_t size;          // the contents' length
    uint32_t pos;
} sv;
static farptr saves;        // SAVES.DAT's copy in attic RAM
static uint8_t saves_state; // 0 not read yet, 1 read, 2 missing (or short)

static struct {
    char     name[13];
    farptr   base;
    uint32_t size;
} loaded[MAXLOADED];
static uint8_t nloaded;
static uint8_t dos_ready;

static struct {
    uint8_t  used;
    uint8_t  file;          // index into loaded[]
    uint32_t pos;
} fds[MAXOPEN];

static void upcase(char *dst, const char *src)
{
    uint8_t i;
    for (i = 0; i < 12 && src[i]; i++)
        dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? src[i] - 32 : src[i];
    dst[i] = 0;
}

static int dos_start(void)
{
    if (!dos_ready) {
        if (m65_dos_init() != 0)
            return -1;
        dos_ready = 1;
    }
    return 0;
}

// (Not strncmp/memcmp: the library's string functions are resident, and
// resident memory is full.)
static int same(const char *a, const char *b, uint8_t n)
{
    while (n--)
        if (*a++ != *b++)
            return 0;
    return 1;
}

static int is_savefile(const char *up)
{
    return same(up, "SAVEGAM", 7) || same(up, "CONFIG.", 7);
}

static farptr slot_base(void)
{
    return FAR_ADD(saves, sv.slot * SAVE_SLOT);
}

// Make SAVES.DAT, all zeros (nothing saved), where there is none yet: the
// game makes it itself rather than ship it, so that copying a new version
// onto the SD card leaves the saves alone. (Once, taking a second or two.)
static void saves_create(void)
{
    uint32_t at;
    int fd;

    if (m65_dos_mkfile(SAVE_FILE, SAVE_SLOT * SAVE_NSLOTS) != 0
        || (fd = m65_dos_open(SAVE_FILE)) < 0) {
        m65_debug_puts("SAVE FAILED");  // (the serial log; strings are in short low memory)
        return;
    }
    for (at = 0; at < SAVE_SLOT * SAVE_NSLOTS; at += 512)
        if (m65_dos_write512(fd, 0, 0) != 0)            // (a sector of zeros)
            break;
    m65_dos_close(fd);
    m65_debug_puts("CREATED");
}

// Open a save/config file (the name upper case) for reading or writing.
static int save_open(const char *up, int writing)
{
    uint8_t hdr[8];
    int fd;

    if (sv.open || dos_start() != 0)
        return -1;
    if (!saves_state) {                 // the first time: all of SAVES.DAT
        saves_state = 2;
        saves = far_alloc(SAVE_SLOT * SAVE_NSLOTS);
        if (!FAR_ISNULL(saves) && m65_dos_open(SAVE_FILE) < 0)
            saves_create();
        if (!FAR_ISNULL(saves) && (fd = m65_dos_open(SAVE_FILE)) >= 0) {
            if (m65_dos_read(fd, saves.a, SAVE_SLOT * SAVE_NSLOTS) == SAVE_SLOT * SAVE_NSLOTS)
                saves_state = 1;
            m65_dos_close(fd);
        }
    }
    if (saves_state != 1)
        return -1;
    sv.slot = up[0] == 'C' ? 0 : (uint8_t)(up[7] - '0' + 1);
    if (sv.slot >= SAVE_NSLOTS)
        return -1;
    sv.pos = sv.size = 0;
    if (!writing) {                     // anything in the slot?
        far_read(hdr, slot_base(), sizeof hdr);
        memcpy(&sv.size, hdr + 4, 4);
        if (!same((const char *)hdr, "WM65", 4) || sv.size > SAVE_MAX)
            return -1;
        m65_debug_puts(up);             // (the serial log: which save was read)
    }
    sv.writing = (uint8_t)writing;
    sv.open = 1;
    return SAVE_FD;
}

static int save_read(void *buf, unsigned count)
{
    if (count > sv.size - sv.pos)
        count = (unsigned)(sv.size - sv.pos);
    if (count) {
        far_read(buf, FAR_ADD(slot_base(), SAVE_HDR + sv.pos), count);
        sv.pos += count;
    }
    return (int)count;
}

static int save_write(const void *buf, unsigned count)
{
    if (sv.pos + count > SAVE_MAX)
        return -1;
    far_write(FAR_ADD(slot_base(), SAVE_HDR + sv.pos), buf, count);
    sv.pos += count;
    if (sv.pos > sv.size)
        sv.size = sv.pos;
    return (int)count;
}

// Close; a written slot goes to the SD card now, a sector at a time.
static int save_close(void)
{
    uint8_t hdr[SAVE_HDR];
    uint32_t total, at, skip;
    uint16_t n;
    int rc = 0, fd;

    sv.open = 0;
    if (!sv.writing)
        return 0;
    memset(hdr, 0, sizeof hdr);
    memcpy(hdr, "WM65", 4);
    memcpy(hdr + 4, &sv.size, 4);
    far_write(slot_base(), hdr, sizeof hdr);
    fd = m65_dos_open(SAVE_FILE);
    if (fd < 0) {
        m65_debug_puts("SAVE FAILED");
        return -1;
    }
    // No seek: read up to the slot (into the copy, which holds the same).
    skip = sv.slot * SAVE_SLOT;
    for (at = 0; at < skip && !rc; at += 512)
        rc = m65_dos_read512(fd, saves.a + at) != 512;
    total = SAVE_HDR + sv.size;
    for (at = 0; at < total && !rc; at += 512) {
        n = total - at < 512 ? (uint16_t)(total - at) : 512;
        rc = m65_dos_write512(fd, slot_base().a + at, n);
    }
    m65_dos_close(fd);
    m65_debug_puts(rc ? "SAVE FAILED" : "SAVED");  // (the serial log)
    return rc;
}

// Index of the file in loaded[], loading it if needed; -1 if it can't be.
// `up`: the name, upper case (upcase).
static int find_or_load_up(const char *up)
{
    uint8_t i;

    for (i = 0; i < nloaded; i++)
        if (!strcmp(loaded[i].name, up))
            return i;
    if (nloaded == MAXLOADED || dos_start() != 0)
        return -1;
    loaded[nloaded].base = far_load_file(up, &loaded[nloaded].size);
    if (FAR_ISNULL(loaded[nloaded].base))
        return -1;
    strcpy(loaded[nloaded].name, up);
    return nloaded++;
}

// The same for any name (8.3, any case).
static int find_or_load(const char *name)
{
    char up[13];

    if (strlen(name) > 12)
        return -1;
    upcase(up, name);
    return find_or_load_up(up);
}

farptr m65_file_far(const char *name, uint32_t *size)
{
    int f = find_or_load(name);
    if (f < 0)
        return FARNULL;
    if (size)
        *size = loaded[f].size;
    return loaded[f].base;
}

int open(const char *name, int flags, ...)
{
    int f, fd;

    char up[13];

    if (strlen(name) > 12)
        return -1;
    upcase(up, name);
    // O_RDWR (0x03) includes the O_WRONLY bit in this libc.
    if (is_savefile(up))
        return save_open(up, (flags & (O_WRONLY | O_CREAT | O_TRUNC | O_APPEND)) != 0);
    if (flags & (O_WRONLY | O_CREAT | O_TRUNC | O_APPEND))
        return -1;
    f = find_or_load_up(up);
    if (f < 0)
        return -1;
    for (fd = 0; fd < MAXOPEN; fd++) {
        if (!fds[fd].used) {
            fds[fd].used = 1;
            fds[fd].file = (uint8_t)f;
            fds[fd].pos = 0;
            return fd;
        }
    }
    return -1;
}

int close(int fd)
{
    if (fd == SAVE_FD && sv.open)
        return save_close();
    if (fd < 0 || fd >= MAXOPEN || !fds[fd].used)
        return -1;
    fds[fd].used = 0;
    return 0;
}

int read(int fd, void *buf, unsigned count)
{
    uint32_t left;

    if (fd == SAVE_FD && sv.open && !sv.writing)
        return save_read(buf, count);
    if (fd < 0 || fd >= MAXOPEN || !fds[fd].used)
        return -1;
    left = loaded[fds[fd].file].size - fds[fd].pos;
    if (count > left)
        count = (unsigned)left;
    if (count) {
        far_read(buf, FAR_ADD(loaded[fds[fd].file].base, fds[fd].pos), count);
        fds[fd].pos += count;
    }
    return (int)count;
}

int write(int fd, const void *buf, unsigned count)
{
    if (fd == SAVE_FD && sv.open && sv.writing)
        return save_write(buf, count);
    return -1;
}

off_t lseek(int fd, off_t offset, int whence)
{
    int32_t pos, cur, end;

    if (fd == SAVE_FD && sv.open) {
        cur = (int32_t)sv.pos;
        end = (int32_t)sv.size;
    } else if (fd >= 0 && fd < MAXOPEN && fds[fd].used) {
        cur = (int32_t)fds[fd].pos;
        end = (int32_t)loaded[fds[fd].file].size;
    } else
        return -1;
    if (whence == SEEK_SET)
        pos = offset;
    else if (whence == SEEK_CUR)
        pos = cur + offset;
    else if (whence == SEEK_END)
        pos = end + offset;
    else
        return -1;
    if (pos < 0)
        return -1;
    if (fd == SAVE_FD)
        sv.pos = (uint32_t)pos;
    else
        fds[fd].pos = (uint32_t)pos;
    return pos;
}

int stat(const char *path, struct stat *buf)
{
    int f = find_or_load(path);
    if (f < 0)
        return -1;
    if (buf)
        buf->st_size = (long)loaded[f].size;
    return 0;
}

// Nothing is ever deleted (a save game is overwritten in place instead).
int unlink(const char *name)
{
    (void)name;
    return -1;
}

int mkdir(const char *name, ...)
{
    (void)name;
    return -1;
}

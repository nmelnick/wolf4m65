// POSIX file access for Wolf4SDL, on top of attic RAM. The files are in the
// SD card's WOLF4M65 directory (m65_dos_subdir, m65_platform.c; the root if
// it is missing).
//
// Data files: open() loads the whole file from the SD card into attic RAM
// the first time (see far_load_file) and remembers it; read() and lseek()
// then work on that copy. m65_file_far() gives the file's far address for
// code that wants to read it in place, such as the graphics and audio caches.
//
// Save games and the config file (SAVEGAM?.*, CONFIG.*) are written too.
// Hyppo can only overwrite an existing file's sectors, not make one or make
// it longer, so they are fixed-size files (make sdcard creates them, of
// SAVE_FILESIZE bytes) holding a header (SAVE_MAGIC and the length of what
// follows) and the contents; one without the magic counts as missing. One at
// a time is open, through a staging buffer in attic RAM: read in sector by
// sector as far as it is read, and written back in sectors on close().

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

#define SAVE_FD       MAXOPEN   // the save/config file's descriptor
#define SAVE_HDR      16        // "WM65", the length (32 bits), zeros
#define SAVE_FILESIZE 32768UL   // the files on the SD card
#define SAVE_MAX      (SAVE_FILESIZE - SAVE_HDR)

static struct {
    uint8_t  open, writing;
    int8_t   hfd;           // Hyppo's file, while reading
    uint32_t size;          // the contents' length
    uint32_t have;          // bytes of the file (header included) in svbuf
    uint32_t pos;
    char     name[13];
} sv;
static farptr svbuf;

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

// Open a save/config file (the name upper case) for reading or writing.
static int save_open(const char *up, int writing)
{
    uint8_t hdr[8];

    if (sv.open || dos_start() != 0)
        return -1;
    if (FAR_ISNULL(svbuf)) {
        svbuf = far_alloc(SAVE_FILESIZE);
        if (FAR_ISNULL(svbuf))
            return -1;
    }
    sv.pos = sv.size = sv.have = 0;
    sv.hfd = -1;
    strcpy(sv.name, up);
    if (!writing) {
        // the first sector: is there anything in it?
        sv.hfd = (int8_t)m65_dos_open(up);
        if (sv.hfd < 0)
            return -1;
        sv.have = m65_dos_read512(sv.hfd, svbuf.a);
        far_read(hdr, svbuf, sizeof hdr);
        memcpy(&sv.size, hdr + 4, 4);
        if (sv.have == M65_DOS_EOF || sv.have < SAVE_HDR || !same((const char *)hdr, "WM65", 4)
            || sv.size > SAVE_MAX) {
            m65_dos_close(sv.hfd);
            return -1;
        }
    }
    sv.writing = (uint8_t)writing;
    sv.open = 1;
    if (!writing)
        m65_debug_puts(up);             // (the serial log: which save was read)
    return SAVE_FD;
}

static int save_read(void *buf, unsigned count)
{
    uint16_t got;

    if (count > sv.size - sv.pos)
        count = (unsigned)(sv.size - sv.pos);
    while (sv.have < SAVE_HDR + sv.pos + count) {       // (read in as far as needed)
        got = m65_dos_read512(sv.hfd, svbuf.a + sv.have);
        if (got == 0 || got == M65_DOS_EOF)
            return -1;
        sv.have += got;
    }
    if (count) {
        far_read(buf, FAR_ADD(svbuf, SAVE_HDR + sv.pos), count);
        sv.pos += count;
    }
    return (int)count;
}

static int save_write(const void *buf, unsigned count)
{
    if (sv.pos + count > SAVE_MAX)
        return -1;
    far_write(FAR_ADD(svbuf, SAVE_HDR + sv.pos), buf, count);
    sv.pos += count;
    if (sv.pos > sv.size)
        sv.size = sv.pos;
    return (int)count;
}

// Close; written contents go to the SD card now, a sector at a time.
static int save_close(void)
{
    uint8_t hdr[SAVE_HDR];
    uint32_t total, at;
    uint16_t n;
    int rc = 0, fd;

    sv.open = 0;
    if (!sv.writing)
        return m65_dos_close(sv.hfd);
    memset(hdr, 0, sizeof hdr);
    memcpy(hdr, "WM65", 4);
    memcpy(hdr + 4, &sv.size, 4);
    far_write(svbuf, hdr, sizeof hdr);
    fd = m65_dos_open(sv.name);
    if (fd < 0) {
        m65_debug_puts("SAVE FAILED: no such file");
        return -1;
    }
    total = SAVE_HDR + sv.size;
    for (at = 0; at < total && !rc; at += 512) {
        n = total - at < 512 ? (uint16_t)(total - at) : 512;
        rc = m65_dos_write512(fd, svbuf.a + at, n);
    }
    m65_dos_close(fd);
    m65_debug_puts(rc ? "SAVE FAILED" : "SAVED");  // (the serial log)
    return rc;
}

// Index of the file in loaded[], loading it if needed; -1 if it can't be.
static int find_or_load(const char *name)
{
    char up[13];
    uint8_t i;

    if (strlen(name) > 12)
        return -1;
    upcase(up, name);
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
    f = find_or_load(name);
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

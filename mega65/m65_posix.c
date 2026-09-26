// Read-only POSIX file access for Wolf4SDL, on top of attic RAM.
//
// open() loads the whole file from the SD card into attic RAM the first time
// (see far_load_file) and remembers it; read() and lseek() then work on that
// copy. m65_file_far() gives the file's far address for code that wants to
// read it in place, such as the graphics and audio caches.
//
// Writing (savegames, config) is not supported yet: open() for writing fails.

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "m65_dos.h"
#include "m65_far.h"
#include "m65_posix.h"

#define MAXLOADED 12
#define MAXOPEN   6

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
    if (nloaded == MAXLOADED)
        return -1;
    if (!dos_ready) {
        if (m65_dos_init() != 0)
            return -1;
        dos_ready = 1;
    }
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

    // O_RDWR (0x03) includes the O_WRONLY bit in this libc.
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
    if (fd < 0 || fd >= MAXOPEN || !fds[fd].used)
        return -1;
    fds[fd].used = 0;
    return 0;
}

int read(int fd, void *buf, unsigned count)
{
    uint32_t left;

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
    (void)fd; (void)buf; (void)count;
    return -1;
}

off_t lseek(int fd, off_t offset, int whence)
{
    int32_t pos;

    if (fd < 0 || fd >= MAXOPEN || !fds[fd].used)
        return -1;
    if (whence == SEEK_SET)
        pos = offset;
    else if (whence == SEEK_CUR)
        pos = (int32_t)fds[fd].pos + offset;
    else if (whence == SEEK_END)
        pos = (int32_t)loaded[fds[fd].file].size + offset;
    else
        return -1;
    if (pos < 0)
        return -1;
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

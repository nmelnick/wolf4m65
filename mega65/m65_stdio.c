// The stdio the game uses, without the KERNAL (gone after m65_takeover):
//   - fopen/fread/fwrite/fseek/ftell/fclose over the file layer
//     (m65_posix.c: only save games and the config file can be written)
//   - printf and friends print to the debug serial port: llvm-mos's printf
//     calls __putchar for every character. There is no console input.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "m65_debug.h"

struct _FILE { int fd; int eof; };

void __putchar(char c)
{
    if (c == '\n')
        m65_debug_putc('\r');
    m65_debug_putc(c);
}

// No console input (the KERNAL is gone; the game reads the keyboard itself).
int __getchar(void)
{
    return EOF;
}

FILE *fopen(const char *name, const char *mode)
{
    FILE *f;
    int fd;

    if (mode[1] == '+' || (mode[1] && mode[2] == '+'))
        return NULL;
    if (mode[0] == 'r')
        fd = open(name, O_RDONLY);
    else if (mode[0] == 'w')
        fd = open(name, O_WRONLY | O_CREAT | O_TRUNC);
    else
        return NULL;
    if (fd < 0)
        return NULL;
    f = (FILE *)malloc(sizeof *f);
    if (!f) {
        close(fd);
        return NULL;
    }
    f->fd = fd;
    f->eof = 0;
    return f;
}

int fclose(FILE *f)
{
    int r;
    if (!f)
        return EOF;
    r = close(f->fd);
    free(f);
    return r;
}

size_t fread(void *buf, size_t size, size_t n, FILE *f)
{
    size_t total = size * n;
    int got;
    if (!f || !size)
        return 0;
    got = read(f->fd, buf, total);
    if (got < 0)
        got = 0;
    if ((size_t)got < total)
        f->eof = 1;
    return (size_t)got / size;
}

size_t fwrite(const void *buf, size_t size, size_t n, FILE *f)
{
    int put;
    if (!f || !size)
        return 0;
    put = write(f->fd, buf, size * n);
    return put < 0 ? 0 : (size_t)put / size;
}

int fseek(FILE *f, long offset, int whence)
{
    if (!f)
        return -1;
    f->eof = 0;
    return lseek(f->fd, offset, whence) < 0 ? -1 : 0;
}

long ftell(FILE *f)
{
    return lseek(f->fd, 0, SEEK_CUR);
}

int feof(FILE *f)
{
    return f->eof;
}

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

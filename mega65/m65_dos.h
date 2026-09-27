// MEGA65 file access through the hypervisor (Hyppo DOS), without the KERNAL.
//
// The hypervisor is entered by writing a function number to $D640. The
// function numbers are Hyppo's; results come back in A/X/Y with carry set on
// success. Files are read through the SD sector buffer at $FFD6E00, so this
// works whatever the CPU memory map looks like.
//
// Files are in the directory m65_dos_subdir names, if the program defines it
// and it exists (the game: WOLF4M65), else in the root. Names are 8.3, upper
// case.

#ifndef M65_DOS_H
#define M65_DOS_H

#include <stdint.h>

#define M65_DOS_EOF ((uint16_t)0xFFFF)

// Returns 0 on success. Must be called before the first open.
int m65_dos_init(void);

// Returns a file descriptor (0..) or -1 on failure.
int m65_dos_open(const char *name);
int m65_dos_close(int fd);

// Read up to 512 bytes into the far address `dst` (28-bit). Returns the
// number of bytes read, 0 at end of file, or M65_DOS_EOF on error.
uint16_t m65_dos_read512(int fd, uint32_t dst);

// Read `count` bytes into far memory in 512-byte pieces. Returns bytes read.
uint32_t m65_dos_read(int fd, uint32_t dst, uint32_t count);

// Overwrite the file's next 512-byte sector with `count` (<= 512) bytes from
// the far address `src`, the rest of the sector zero (m65_dosw.c). Hyppo
// cannot make a file longer: past its end this fails. Returns 0 on success.
int m65_dos_write512(int fd, uint32_t src, uint16_t count);

#endif

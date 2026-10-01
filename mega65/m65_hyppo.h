// The hypervisor's DOS (Hyppo), for m65_dos.c and m65_dosw.c: function
// numbers (written to $D640), the SD card's sector buffer, and setname's
// file name buffer.

#ifndef M65_HYPPO_H
#define M65_HYPPO_H

#include <stdint.h>

#define HYPPO_CHDIR      0x0C
#define HYPPO_OPENFILE   0x18
#define HYPPO_READFILE   0x1A
#define HYPPO_WRITEFILE  0x1C
#define HYPPO_MKFILE     0x1E
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

#endif

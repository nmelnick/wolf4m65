// Far memory: anything outside the CPU's 64KB view, addressed with 28 bits.
//
// A farptr is wrapped in a struct on purpose: code that still treats a far
// address as a normal pointer fails to compile instead of silently reading
// the wrong 64KB.

#ifndef M65_FAR_H
#define M65_FAR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint32_t a; } farptr;

#define FARNULL             ((farptr){0})
#define FAR(addr)           ((farptr){(uint32_t)(addr)})
#define FAR_OF(ptr)         ((farptr){(uint32_t)(uintptr_t)(ptr)})   // near -> far

// Functions rather than macros, so that C++ proxies that convert to a farptr
// (e.g. grsegs[i], see m65_fararray.hpp) can be passed directly.
static inline int FAR_ISNULL(farptr p) { return p.a == 0; }
static inline farptr FAR_ADD(farptr p, uint32_t n) { farptr r; r.a = p.a + n; return r; }

// Attic RAM layout (HyperRAM, $8000000-$87FFFFF).
//   MB $80        not free (in use by the system; see ovl_rt.s)
//   MB $81        code overlays (ovl_rt.s / ovl_load.c)
//   $8200000...   game data files, loaded whole at start-up
//   $8400000...   far heap: decompressed graphics and audio chunks
// Chip RAM (faster than attic RAM) used as far memory:
//   $13000-$1F7FF chip heap: hot game data (map planes, actorat). Above our
//                 screen RAM ($12000), below the C64-style colour RAM view.
#define CHIP_HEAP        0x13000UL
#define CHIP_HEAP_END    0x1F800UL
#define ATTIC_FILES      0x8200000UL
#define ATTIC_FILES_END  0x8400000UL
#define ATTIC_HEAP       0x8400000UL
#define ATTIC_HEAP_END   0x8800000UL

// Copy between far and near memory, or far to far (DMA).
void far_read(void *dst, farptr src, uint16_t count);
void far_write(farptr dst, const void *src, uint16_t count);
void far_copy(farptr dst, farptr src, uint16_t count);

uint8_t  far_peek(farptr p);
uint16_t far_peekw(farptr p);
uint32_t far_peekl(farptr p);
void     far_poke(farptr p, uint8_t v);
void     far_pokew(farptr p, uint16_t v);

// Load a whole file from the SD card root into the file area of attic RAM.
// Files are packed one after the other, each starting on a 256-byte boundary.
// Returns the file's far address and stores its size; FARNULL on failure.
farptr far_load_file(const char *name, uint32_t *size);

// Far heap: bump allocation, nothing is ever freed. Returns FARNULL when full.
farptr far_alloc(uint32_t size);
uint32_t far_heap_used(void);

// The same, in chip RAM (CHIP_HEAP): for data accessed often.
farptr far_alloc_chip(uint16_t size);

#ifdef __cplusplus
}
#endif

#endif

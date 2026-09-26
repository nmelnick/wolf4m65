// MEGA65-specific additions to the read-only POSIX layer (m65_posix.c).

#ifndef M65_POSIX_H
#define M65_POSIX_H

#include <stdint.h>
#include "m65_far.h"

#ifdef __cplusplus
extern "C" {
#endif

// Far address and size of a data file, loading it into attic RAM if needed.
// FARNULL if the file cannot be loaded.
farptr m65_file_far(const char *name, uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif

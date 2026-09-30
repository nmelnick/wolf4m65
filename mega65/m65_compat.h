// Force-included into every Wolf4SDL translation unit (-include). Fills the
// gaps in the llvm-mos libc that Wolf4SDL relies on.

#ifndef M65_COMPAT_H
#define M65_COMPAT_H

// The version's suffix on the port's own files (WOLF1.OVL, SFX6.DAT,
// SAVES1.DAT...): the Makefile's VERSION sets it.
#ifndef M65_VSUFFIX
#define M65_VSUFFIX "1"
#endif

#include <stddef.h>
#include <string.h>

#include "m65_far.h"      // farptr, used in the game's headers under MEGA65

// llvm-mos's <unistd.h> and <fcntl.h> have no extern "C" guards: include
// them here, once, with C linkage (m65_posix.c implements them in C).
#ifdef __cplusplus
extern "C" {
#endif
#include <fcntl.h>
#include <unistd.h>
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

// llvm-mos libm only provides fmin/fmax. These are implemented in m65_libm.c.
double atan(double x);
double atan2(double y, double x);

#ifdef __cplusplus
}
#endif

// Keeps a function out of its caller, e.g. so that the caller fits a code
// overlay (the game sources define it empty elsewhere).
#define M65_NOINLINE __attribute__((noinline))

#endif

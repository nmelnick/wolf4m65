// Force-included into every Wolf4SDL translation unit (-include). Fills the
// gaps in the llvm-mos libc that Wolf4SDL relies on.

#ifndef M65_COMPAT_H
#define M65_COMPAT_H

#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// llvm-mos libm only provides fmin/fmax. These are implemented in m65_libc.c.
double sin(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double sqrt(double x);

char *strdup(const char *s);

#ifdef __cplusplus
}
#endif

#endif

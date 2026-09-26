// Taking over the machine at the start of main (see m65_platform.c).

#ifndef M65_PLATFORM_H
#define M65_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

// Must be the first thing main does, before any read-only data (.rodata) or
// data placed in high memory is used. Afterwards the KERNAL is gone.
void m65_takeover(void);

#ifdef __cplusplus
}
#endif

#endif

// Taking over the machine at the start of main (see m65_platform.c).

#ifndef M65_PLATFORM_H
#define M65_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

// Must be the first thing main does, before any read-only data (.rodata) or
// data placed in high memory is used. Afterwards the KERNAL is gone.
void m65_takeover(void);

// Everything the game needs before its own code runs: takeover, loading the
// initialised data (.data, from `datafile`) and the code overlays (from
// `ovlfile`). Resident, and calls nothing in an overlay; on failure it
// reports on the debug serial port and stops with a red border.
void m65_startup(const char *ovlfile, const char *datafile);

#ifdef __cplusplus
}
#endif

#endif

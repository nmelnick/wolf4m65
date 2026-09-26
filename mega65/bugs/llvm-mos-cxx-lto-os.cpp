// llvm-mos miscompile, found 2026-09-25 (clang 24.0.0git 9e5efd81, target
// mos-mega65). Hangs in puthex's first loop when built as C++ with -Os and
// LTO (the default for mos-mega65-clang++ -Os). The same source works:
//   - as C (mos-mega65-clang) at -Os, -O2, with or without LTO
//   - as C++ with -Os -fno-lto, or with -O2
// It only fails when puthex is not inlined (two call sites).
//
//   mos-mega65-clang++ -Os -o p.prg llvm-mos-cxx-lto-os.cpp
//   xemu-xmega65 -prg p.prg -hyperserialfile out.txt   -> no output (hang)
//
// The Wolf4SDL build uses -fno-lto (the overlay generator needs per-file
// assembly), so it is not affected; test programs must use -fno-lto too.
#include <stdint.h>
#include "../m65_debug.h"
static void puthex(const char *tag, uint32_t v)
{
    static const char hx[] = "0123456789ABCDEF";
    char b[40];
    int n = 0, k;
    while (*tag) b[n++] = *tag++;
    for (k = 28; k >= 0; k -= 4) b[n++] = hx[(v >> k) & 15];
    b[n] = 0;
    m65_debug_puts(b);
}
int main(void)
{
    puthex("hello ", 0x1234ABCDUL);
    puthex("again ", 0x0055AA00UL);
    m65_debug_puts("TEST-DONE");
    for (;;) {}
}

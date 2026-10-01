// assert() failures: report on the debug serial port (m65_debug.h), show a
// red border, and halt. Replaces llvm-mos's __assert, which prints through
// the KERNAL - unusable once the program owns the machine.
#include <assert.h>

#include "m65_debug.h"
#include "m65_hw.h"

void __assert(const char *file, const char *line, const char *function,
              const char *expr)
{
    m65_debug_puts("ASSERT:");
    m65_debug_puts(file);
    m65_debug_puts(line);
    m65_debug_puts(function);
    m65_debug_puts(expr);
    m65_debug_puts("TEST-DONE");        // (lets test runs stop right away)
    for (;;)
        VIC_BORDER = 2;
}

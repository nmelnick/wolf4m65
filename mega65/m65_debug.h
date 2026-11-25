// Debug output through the hypervisor serial port ($D643).
//
// Xemu writes it to the file given by -hyperserialfile; on real hardware it
// goes out of the serial monitor. tools/xrun.py watches for a marker line.

#ifndef M65_DEBUG_H
#define M65_DEBUG_H

static inline void m65_debug_putc(char c)
{
    __asm__ volatile("sta $d643\n clv\n" :: "a"(c) : "v");
}

static inline void m65_debug_puts(const char *s)
{
    while (*s)
        m65_debug_putc(*s++);
    m65_debug_putc('\r');
    m65_debug_putc('\n');
}

#endif

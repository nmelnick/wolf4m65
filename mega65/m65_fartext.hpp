// A read-only cursor into far memory that behaves like a `const char *`, so
// that pointer-walking parsers (wl_text.cpp's article layout) run unchanged
// on text that stays in far memory. Every dereference is a far_peek.

#ifndef M65_FARTEXT_HPP
#define M65_FARTEXT_HPP

#include <stdint.h>
#include "m65_far.h"

class FarText
{
public:
    FarText () : a(0) {}
    FarText (farptr p) : a(p.a) {}

    char operator* () const { return (char) far_peek(FAR(a)); }

    FarText &operator++ () { a++; return *this; }
    FarText operator++ (int) { FarText t = *this; a++; return t; }
    FarText &operator-- () { a--; return *this; }
    FarText operator-- (int) { FarText t = *this; a--; return t; }

    FarText operator+ (int32_t n) const { FarText t; t.a = a + n; return t; }
    FarText operator- (int32_t n) const { FarText t; t.a = a - n; return t; }

    bool operator< (const FarText &o) const { return a < o.a; }
    bool operator== (const FarText &o) const { return a == o.a; }
    bool operator!= (const FarText &o) const { return a != o.a; }

private:
    uint32_t a;
};

#endif

// A pointer to 16-bit words in far memory that behaves like `word *`, so code
// walking the map planes (mapsegs, MAPSPOT) runs unchanged with the planes in
// far memory. Dereferencing yields a FarWord: reading it converts to a word
// (far_peekw), assigning to it writes (far memory, two far_pokes).

#ifndef M65_FARWORD_HPP
#define M65_FARWORD_HPP

#include <stdint.h>
#include "m65_far.h"

class FarWord
{
public:
    explicit FarWord (uint32_t addr) : a(addr) {}

    operator uint16_t () const { return far_peekw(FAR(a)); }

    FarWord &operator= (uint16_t v) { far_pokew(FAR(a), v); return *this; }

    // Copies the value, not the address: *map = *(map-1).
    FarWord &operator= (const FarWord &o) { return *this = (uint16_t) o; }

private:
    uint32_t a;
};

class FarWordPtr
{
public:
    FarWordPtr () : a(0) {}
    FarWordPtr (farptr p) : a(p.a) {}

    FarWord operator* () const { return FarWord(a); }
    FarWord operator[] (int32_t i) const { return FarWord(a + 2 * i); }

    FarWordPtr &operator++ () { a += 2; return *this; }
    FarWordPtr operator++ (int) { FarWordPtr t = *this; a += 2; return t; }
    FarWordPtr &operator-- () { a -= 2; return *this; }
    FarWordPtr operator-- (int) { FarWordPtr t = *this; a -= 2; return t; }

    FarWordPtr operator+ (int32_t n) const { FarWordPtr t; t.a = a + 2 * n; return t; }
    FarWordPtr operator- (int32_t n) const { FarWordPtr t; t.a = a - 2 * n; return t; }

    bool operator== (const FarWordPtr &o) const { return a == o.a; }
    bool operator!= (const FarWordPtr &o) const { return a != o.a; }

    farptr far () const { return FAR(a); }

private:
    uint32_t a;
};

#endif

// A T in far memory, and a pointer to one, that behave like `T &` and `T *`,
// so that code written for near data (map planes walked by pointer, the
// help text, tables) runs unchanged with the data in far memory.
//
// FarRef<T>: reading converts to a T, assigning writes; assigning one
// FarRef to another copies the value, not the address. T must be trivially
// copyable; 1-, 2- and 4-byte ones use the flat far access (fast), others
// a DMA job.
// FarPtr<T>: *p and p[i] are FarRefs; ++, --, + and - step whole T's.
//
// (Constructors are constexpr: globals of these types must be initialised at
// compile time. A dynamic initialiser would run before main, i.e. before the
// code overlays are loaded.)

#ifndef M65_FARREF_HPP
#define M65_FARREF_HPP

#include <stdint.h>
#include <string.h>
#include "m65_far.h"

template <class T>
class FarRef
{
public:
    constexpr explicit FarRef (uint32_t addr) : a(addr) {}

    operator T () const
    {
        T t;
        if constexpr (sizeof(T) == 1) {
            uint8_t v = far_peek(FAR(a)); memcpy(&t, &v, 1);
        } else if constexpr (sizeof(T) == 2) {
            uint16_t v = far_peekw(FAR(a)); memcpy(&t, &v, 2);
        } else if constexpr (sizeof(T) == 4) {
            uint32_t v = far_peekl(FAR(a)); memcpy(&t, &v, 4);
        } else
            far_read(&t, FAR(a), sizeof(T));
        return t;
    }

    FarRef &operator= (T v)          // (by value: smaller code than by reference)
    {
        if constexpr (sizeof(T) == 1) {
            uint8_t w; memcpy(&w, &v, 1); far_poke(FAR(a), w);
        } else if constexpr (sizeof(T) == 2) {
            uint16_t w; memcpy(&w, &v, 2); far_pokew(FAR(a), w);
        } else if constexpr (sizeof(T) == 4) {
            uint32_t w; memcpy(&w, &v, 4);
            far_pokew(FAR(a), (uint16_t) w);
            far_pokew(FAR(a + 2), (uint16_t) (w >> 16));
        } else
            far_write(FAR(a), &v, sizeof(T));
        return *this;
    }
    FarRef &operator= (const FarRef &o) { return *this = (T) o; }   // value copy

protected:
    uint32_t a;
};

template <class T>
class FarPtr
{
public:
    constexpr FarPtr () : a(0) {}
    constexpr FarPtr (farptr p) : a(p.a) {}

    FarRef<T> operator* () const { return FarRef<T>(a); }
    FarRef<T> operator[] (int32_t i) const { return FarRef<T>(a + (int32_t) sizeof(T) * i); }

    FarPtr &operator++ () { a += sizeof(T); return *this; }
    FarPtr operator++ (int) { FarPtr t = *this; a += sizeof(T); return t; }
    FarPtr &operator-- () { a -= sizeof(T); return *this; }
    FarPtr operator-- (int) { FarPtr t = *this; a -= sizeof(T); return t; }

    FarPtr operator+ (int32_t n) const { FarPtr t; t.a = a + (int32_t) sizeof(T) * n; return t; }
    FarPtr operator- (int32_t n) const { FarPtr t; t.a = a - (int32_t) sizeof(T) * n; return t; }

    bool operator== (const FarPtr &o) const { return a == o.a; }
    bool operator!= (const FarPtr &o) const { return a != o.a; }
    bool operator< (const FarPtr &o) const { return a < o.a; }

    farptr far () const { return FAR(a); }

private:
    uint32_t a;
};

#endif

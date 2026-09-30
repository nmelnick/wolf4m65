// A fixed-size array of T in far memory that behaves like `T a[N]`: a[i]
// yields a reference that reads as a T and writes on assignment (T must be
// trivially copyable: farptr, integers). Storage is set with init().

#ifndef M65_FARARRAY_HPP
#define M65_FARARRAY_HPP

#include <stdint.h>
#include <string.h>
#include "m65_far.h"
#include "m65_video.h"      // m65_dma_fill

template <class T, int N>
class FarArray
{
public:
    class Ref
    {
    public:
        constexpr explicit Ref (uint32_t addr) : a(addr) {}

        // 2- and 4-byte elements use flat access (fast); others use DMA.
        operator T () const
        {
            if constexpr (sizeof(T) == 2) {
                uint16_t v = far_peekw(FAR(a)); T t; memcpy(&t, &v, 2); return t;
            } else if constexpr (sizeof(T) == 4) {
                uint32_t v = far_peekl(FAR(a)); T t; memcpy(&t, &v, 4); return t;
            } else {
                T v; far_read(&v, FAR(a), sizeof(T)); return v;
            }
        }
        Ref &operator= (const T &v)
        {
            if constexpr (sizeof(T) == 2) {
                uint16_t w; memcpy(&w, &v, 2); far_pokew(FAR(a), w);
            } else if constexpr (sizeof(T) == 4) {
                uint32_t w; memcpy(&w, &v, 4);
                far_pokew(FAR(a), (uint16_t) w);
                far_pokew(FAR(a + 2), (uint16_t) (w >> 16));
            } else
                far_write(FAR(a), &v, sizeof(T));
            return *this;
        }
        Ref &operator= (const Ref &o) { return *this = (T) o; }   // value copy

    private:
        uint32_t a;
    };

    // Use zeroed storage (init) or storage that already holds the data (use).
    void init (farptr storage) { base = storage.a; m65_dma_fill(base, 0, bytes()); }
    void use (farptr storage) { base = storage.a; }
    uint32_t addr (void) const { return base; }     // (for assembly)
    static uint16_t bytes (void) { return (uint16_t) (sizeof(T) * N); }

    Ref operator[] (int i) const { return Ref(base + (uint16_t) (i * sizeof(T))); }

private:
    uint32_t base;
};

#endif

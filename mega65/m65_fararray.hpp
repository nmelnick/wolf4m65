// A fixed-size array of T in far memory that behaves like `T a[N]`: a[i]
// is a FarRef<T> (m65_farref.hpp), which reads as a T and writes on
// assignment. Storage is set with init() or use().

#ifndef M65_FARARRAY_HPP
#define M65_FARARRAY_HPP

#include <stdint.h>
#include "m65_far.h"
#include "m65_farref.hpp"
#include "m65_video.h"      // m65_dma_fill

template <class T, int N>
class FarArray
{
public:
    // Use zeroed storage (init) or storage that already holds the data (use).
    void init (farptr storage) { base = storage.a; m65_dma_fill(base, 0, bytes()); }
    void use (farptr storage) { base = storage.a; }
    uint32_t addr (void) const { return base; }     // (for assembly)
    static uint16_t bytes (void) { return (uint16_t) (sizeof(T) * N); }

    FarRef<T> operator[] (int i) const { return FarRef<T>(base + (uint16_t) (i * sizeof(T))); }

private:
    uint32_t base;
};

#endif

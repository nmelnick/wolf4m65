// A MAPSIZE x MAPSIZE array of pointers in far memory that behaves like
// `T *grid[MAPSIZE][MAPSIZE]` (actorat), so the game code using it is
// unchanged. grid[x][y] yields a reference that reads as a T * and writes on
// assignment; it also casts to an integer, since the game stores tile numbers
// in it as well as pointers.

#ifndef M65_FARGRID_HPP
#define M65_FARGRID_HPP

#include <stdint.h>
#include "m65_far.h"
#include "m65_video.h"      // m65_dma_fill

template <class T, int N>
class FarPtrGrid
{
public:
    class Ref
    {
    public:
        explicit Ref (uint32_t addr) : a(addr) {}

        operator T * () const { return (T *) (uintptr_t) far_peekw(FAR(a)); }
        explicit operator uintptr_t () const { return far_peekw(FAR(a)); }
        T *operator-> () const { return (T *) *this; }

        Ref &operator= (T *p)
        {
            uint16_t v = (uint16_t) (uintptr_t) p;
            far_poke(FAR(a), (uint8_t) v);
            far_poke(FAR(a + 1), (uint8_t) (v >> 8));
            return *this;
        }
        // Copies the value, not the address.
        Ref &operator= (const Ref &o) { return *this = (T *) o; }

    private:
        uint32_t a;
    };

    class Row
    {
    public:
        explicit Row (uint32_t addr) : a(addr) {}
        Ref operator[] (int y) const { return Ref(a + 2 * (uint32_t) y); }
    private:
        uint32_t a;
    };

    // Storage: N*N entries of 2 bytes (pointers are 16-bit), x-major like the
    // C array. Must be set before first use.
    void init (farptr storage) { base = storage.a; }
    void clear (void) { m65_dma_fill(base, 0, (uint16_t) (2 * N * N)); }
    static uint16_t bytes (void) { return 2 * N * N; }

    Row operator[] (int x) const { return Row(base + 2 * (uint32_t) x * N); }

private:
    uint32_t base;
};

#endif

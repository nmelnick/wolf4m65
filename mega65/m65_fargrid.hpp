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

        Ref &operator= (T *p) { far_pokew(FAR(a), (uint16_t) (uintptr_t) p); return *this; }
        // Copies the value, not the address.
        Ref &operator= (const Ref &o) { return *this = (T *) o; }

    private:
        uint32_t a;
    };

    class Row
    {
    public:
        explicit Row (uint32_t addr) : a(addr) {}
        Ref operator[] (int y) const { return Ref(a + (uint16_t) (2 * y)); }
    private:
        uint32_t a;
    };

    // Storage: N*N entries of 2 bytes (pointers are 16-bit), x-major like the
    // C array. Must be set before first use.
    void init (farptr storage) { base = storage.a; }
    void clear (void) { m65_dma_fill(base, 0, (uint16_t) (2 * N * N)); }
    static uint16_t bytes (void) { return 2 * N * N; }

    // (2 * N * N fits 16 bits for the game's 64x64: offsets stay 16-bit.)
    Row operator[] (int x) const { return Row(base + (uint16_t) (2 * N * x)); }

private:
    uint32_t base;
};

#endif

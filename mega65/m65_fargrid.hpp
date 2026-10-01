// A MAPSIZE x MAPSIZE array of pointers in far memory that behaves like
// `T *grid[MAPSIZE][MAPSIZE]` (actorat), so the game code using it is
// unchanged. grid[x][y] yields a reference that reads as a T * and writes on
// assignment; it also casts to an integer, since the game stores tile numbers
// in it as well as pointers.

#ifndef M65_FARGRID_HPP
#define M65_FARGRID_HPP

#include <stdint.h>
#include "m65_far.h"
#include "m65_farref.hpp"
#include "m65_video.h"      // m65_dma_fill

template <class T, int N>
class FarPtrGrid
{
public:
    // A T * in far memory (FarRef), that also casts to an integer (tile
    // codes) and takes ->.
    class Ref : public FarRef<T *>
    {
    public:
        using FarRef<T *>::FarRef;
        using FarRef<T *>::operator=;

        explicit operator uintptr_t () const { return far_peekw(FAR(this->a)); }
        T *operator-> () const { return (T *) *this; }
    };

    class Row
    {
    public:
        constexpr explicit Row (uint32_t addr) : a(addr) {}
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

// A MAPSIZE x MAPSIZE array of bytes in far memory that behaves like
// `byte grid[N][N]` (tilemap, spotvis), including &grid[x][y], pointer
// arithmetic on that (Ptr, a 16-bit offset into the grid, so it is as small
// as the original byte *), and compound assignment. ID distinguishes grids:
// each has its own static base address.
template <int ID, int N>
class FarByteGrid
{
public:
    static uint32_t base;

    class Ptr;

    class Ref
    {
    public:
        constexpr explicit Ref (uint16_t off) : o(off) {}

        operator uint8_t () const { return far_peek(FAR(base + o)); }
        Ref &operator= (uint8_t v) { far_poke(FAR(base + o), v); return *this; }
        Ref &operator= (const Ref &r) { return *this = (uint8_t) r; }   // value copy
        Ref &operator|= (uint8_t v) { return *this = (uint8_t) ((uint8_t) *this | v); }
        Ref &operator&= (uint8_t v) { return *this = (uint8_t) ((uint8_t) *this & v); }
        Ref &operator^= (uint8_t v) { return *this = (uint8_t) ((uint8_t) *this ^ v); }
        Ref &operator+= (uint8_t v) { return *this = (uint8_t) ((uint8_t) *this + v); }
        Ref &operator-= (uint8_t v) { return *this = (uint8_t) ((uint8_t) *this - v); }
        Ref &operator++ () { return *this += 1; }
        Ref &operator-- () { return *this -= 1; }
        uint8_t operator++ (int) { uint8_t v = *this; *this += 1; return v; }
        uint8_t operator-- (int) { uint8_t v = *this; *this -= 1; return v; }
        Ptr operator& () const { return Ptr(o); }

    private:
        uint16_t o;
    };

    class Ptr
    {
    public:
        constexpr Ptr () : o(0) {}
        constexpr explicit Ptr (uint16_t off) : o(off) {}

        Ref operator* () const { return Ref(o); }
        Ref operator[] (int i) const { return Ref((uint16_t) (o + i)); }
        Ptr operator+ (int n) const { return Ptr((uint16_t) (o + n)); }
        Ptr operator- (int n) const { return Ptr((uint16_t) (o - n)); }
        bool operator== (const Ptr &p) const { return o == p.o; }
        bool operator!= (const Ptr &p) const { return o != p.o; }
        uint16_t offset () const { return o; }

    private:
        uint16_t o;
    };

    class Row
    {
    public:
        constexpr explicit Row (uint16_t off) : o(off) {}
        Ref operator[] (int y) const { return Ref((uint16_t) (o + y)); }
    private:
        uint16_t o;
    };

    Row operator[] (int x) const { return Row((uint16_t) (x * N)); }
    Ptr flat () const { return Ptr(0); }               // (byte *) grid
    farptr far () const { return FAR(base); }

    static void init (farptr storage) { base = storage.a; clear(); }
    static void clear (void) { m65_dma_fill(base, 0, (uint16_t) (N * N)); }
    static uint16_t bytes (void) { return (uint16_t) (N * N); }
};

template <int ID, int N> uint32_t FarByteGrid<ID, N>::base;

#endif

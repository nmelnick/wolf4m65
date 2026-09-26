// An array of structs in far memory, and pointers into it that behave like
// `T *` (statobjlist, laststatobj): p->field reads and writes, *p, ++, --,
// comparisons, differences, &array[i], array + i.
//
// Access goes through a one-entry near cache per T: p->field loads element
// p into the cache (one DMA) unless it is already there; switching to
// another element writes the cached one back first (every access marks it
// dirty, since -> cannot tell reads from writes). All access to the array
// must go through these types.
//
// Limitation: an expression touching two *different* elements at once (e.g.
// p->a + q->b) may see both pointing at the one cache slot. The game's uses
// work on one element at a time (checked when this was introduced).

#ifndef M65_FARSTRUCT_HPP
#define M65_FARSTRUCT_HPP

#include <stdint.h>
#include "m65_far.h"
#include "m65_video.h"      // m65_dma_fill

template <class T>
class FarStructCache
{
public:
    static uint32_t base;
    static int16_t  cached;             // element index in the cache, -1: none
    static T        cache;

    static void flush (void)
    {
        if (cached >= 0)
            far_write(FAR(base + (uint32_t) cached * sizeof(T)), &cache, sizeof(T));
    }

    static T *load (int16_t i)
    {
        if (cached != i) {
            flush();
            far_read(&cache, FAR(base + (uint32_t) i * sizeof(T)), sizeof(T));
            cached = i;
        }
        return &cache;
    }
};

template <class T> uint32_t FarStructCache<T>::base;
template <class T> int16_t  FarStructCache<T>::cached = -1;
template <class T> T        FarStructCache<T>::cache;

template <class T>
class FarStructPtr
{
public:
    FarStructPtr () : i(0) {}
    explicit FarStructPtr (int16_t index) : i(index) {}

    T *operator-> () const { return FarStructCache<T>::load(i); }
    T &operator* () const { return *FarStructCache<T>::load(i); }

    FarStructPtr &operator++ () { i++; return *this; }
    FarStructPtr operator++ (int) { FarStructPtr t = *this; i++; return t; }
    FarStructPtr &operator-- () { i--; return *this; }
    FarStructPtr operator-- (int) { FarStructPtr t = *this; i--; return t; }
    FarStructPtr operator+ (int n) const { return FarStructPtr((int16_t) (i + n)); }
    FarStructPtr operator- (int n) const { return FarStructPtr((int16_t) (i - n)); }
    int operator- (const FarStructPtr &o) const { return i - o.i; }

    bool operator== (const FarStructPtr &o) const { return i == o.i; }
    bool operator!= (const FarStructPtr &o) const { return i != o.i; }
    bool operator< (const FarStructPtr &o) const { return i < o.i; }

    int16_t index () const { return i; }

private:
    int16_t i;
};

template <class T, int N>
class FarStructArray
{
public:
    class Ref
    {
    public:
        explicit Ref (int16_t index) : i(index) {}
        FarStructPtr<T> operator& () const { return FarStructPtr<T>(i); }
        T *operator-> () const { return FarStructCache<T>::load(i); }
    private:
        int16_t i;
    };

    void init (farptr storage)
    {
        FarStructCache<T>::base = storage.a;
        FarStructCache<T>::cached = -1;
        m65_dma_fill(storage.a, 0, bytes());
    }
    static uint16_t bytes (void) { return (uint16_t) (sizeof(T) * N); }

    Ref operator[] (int i) const { return Ref((int16_t) i); }
    FarStructPtr<T> operator+ (int i) const { return FarStructPtr<T>((int16_t) i); }
};

#endif

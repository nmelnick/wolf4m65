// Proxy test: FarWordPtr (map planes) and FarPtrGrid (actorat) do exactly
// what word * and T *[N][N] do, for every operation form the game code uses.
// Each step is done on a near array and on its far twin; the results and the
// final contents must agree.
#include <stdint.h>
#include <string.h>

#include "m65_debug.h"
#include "m65_far.h"
#include "m65_farword.hpp"
#include "m65_fargrid.hpp"
#include "m65_video.h"

#define N 8                         // grid size (the game's is 64)
#define REPORT 0x5FC00UL

struct obj { int16_t flags; int16_t x; };

static uint16_t nearplane[N * N];
static obj *neargrid[N][N];
static obj objs[4];

static FarPtrGrid<obj, N> fargrid;

static uint16_t fails, checks;
static uint8_t firstfail;

static void check (bool ok, uint8_t id)
{
    checks++;
    if (!ok && !fails++)
        firstfail = id;
}

#define ISPOINTER(x) (((uintptr_t)(x)) >= 256)   // as in wl_def.h (MEGA65)

int main (void)
{
    int i, x, y;

    // --- FarWordPtr / FarWord ------------------------------------------------
    farptr planefar = far_alloc_chip(sizeof nearplane);
    FarWordPtr farplane(planefar);
    uint16_t *np;
    FarWordPtr fp;

    for (i = 0; i < N * N; i++) {                     // MAPSPOT-style writes
        nearplane[i] = (uint16_t) (i * 997 + 0x1234);
        farplane[i] = (uint16_t) (i * 997 + 0x1234);
    }
    check(farplane[5] == nearplane[5], 1);           // indexed read
    np = nearplane + 3 * N + 4;                        // mapsegs[0] + offset
    fp = farplane + 3 * N + 4;
    check(*np == *fp, 2);
    check(*(np - 1) == *(fp - 1) && *(np + N) == *(fp + N), 3);
    *np = *(np - 1);                                   // proxy = proxy (value)
    *fp = *(fp - 1);
    check(*np == *fp, 4);
    *(np + 1) = 0; *(fp + 1) = 0;                      // proxy = constant
    check(*(fp + 1) == 0, 5);
    check((*np - 7) == (*fp - 7), 6);             // arithmetic on a read
    {
        uint16_t tn = *np++, tf = *fp++;               // *p++
        check(tn == tf && *np == *fp, 7);
        tn = *--np; tf = *--fp;                        // *--p
        check(tn == tf, 8);
    }
    check((*np >= 0x1000) == (*fp >= 0x1000), 9);     // comparisons
    check((uint8_t) *np == (uint8_t) (uint16_t) *fp, 10);   // (byte) cast
    {
        uint16_t buf[N * N];
        far_read(buf, planefar, sizeof buf);
        check(!memcmp(buf, nearplane, sizeof buf), 11);   // whole plane agrees
    }

    // --- FarPtrGrid ------------------------------------------------------------
    fargrid.init(far_alloc_chip(fargrid.bytes()));
    fargrid.clear();
    memset(neargrid, 0, sizeof neargrid);
    for (x = 0; x < N; x++)
        for (y = 0; y < N; y++)
            check(fargrid[x][y] == NULL, 20);           // clear
    for (x = 0; x < N; x++)
        for (y = 0; y < N; y++) {
            uintptr_t tile = (x * N + y) % 70;          // tile codes, as stored
            neargrid[x][y] = (obj *) tile;
            fargrid[x][y] = (obj *) tile;
        }
    objs[2].flags = 0x55; objs[2].x = -3;
    neargrid[1][2] = &objs[2];
    fargrid[1][2] = &objs[2];                           // pointer store
    fargrid[3][4] = NULL;  neargrid[3][4] = NULL;        // NULL / 0 stores
    fargrid[3][5] = 0;     neargrid[3][5] = 0;
    fargrid[2][2] = fargrid[1][2];                       // ref = ref (value)
    neargrid[2][2] = neargrid[1][2];
    check(fargrid[2][2] == &objs[2], 21);
    {
        obj *check_ = fargrid[1][2];                     // check = actorat[x][y]
        check(check_ == neargrid[1][2], 22);
    }
    check(fargrid[1][2]->flags == 0x55, 23);             // ->
    check((uintptr_t) fargrid[0][5] == (uintptr_t) neargrid[0][5], 24);   // cast
    check((unsigned) (uintptr_t) fargrid[0][6] == 6, 25);
    check(!fargrid[3][4] && !neargrid[3][4], 26);        // if (!actorat[..])
    check((bool) fargrid[1][2] == (bool) neargrid[1][2], 27);
    check(ISPOINTER((obj *) fargrid[1][2]) && !ISPOINTER((obj *) fargrid[0][6]), 28);
    check(ISPOINTER(&objs[0]), 29);                      // our data is above page 0
    for (x = 0; x < N; x++)
        for (y = 0; y < N; y++)
            check(fargrid[x][y] == neargrid[x][y], 30);  // whole grid agrees

    struct { uint8_t magic; uint8_t firstfail; uint16_t fails, checks; } rep =
        { 0xEE, firstfail, fails, checks };
    m65_dma_copy(REPORT, (uint32_t)(uintptr_t)&rep, sizeof rep);
    m65_debug_puts(fails ? "PROXY-FAIL" : "PROXY-OK");
    m65_debug_puts("TEST-DONE");
    for (;;) {}
}

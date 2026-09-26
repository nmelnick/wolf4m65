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
#include "m65_fararray.hpp"
#include "m65_farstruct.hpp"
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

    // --- FarArray -----------------------------------------------------------------
    {
        static FarArray<int32_t, 16> fa32;
        static FarArray<farptr, 8> fap;
        static FarArray<int16_t, 8> fa16;
        int32_t n32[16];
        fa32.init(far_alloc_chip(fa32.bytes()));
        fap.init(far_alloc_chip(fap.bytes()));
        fa16.init(far_alloc_chip(fa16.bytes()));
        check((int32_t) fa32[3] == 0 && FAR_ISNULL(fap[5]) && (int16_t) fa16[7] == 0, 40);
        for (i = 0; i < 16; i++) {
            n32[i] = (int32_t) i * -123457L + 0x12345678L;
            fa32[i] = n32[i];
        }
        for (i = 0; i < 16; i++)
            check(fa32[i] == n32[i], 41);                // 4-byte flat reads
        fa32[1] = fa32[2] = -65536;                      // chained, like BuildTables
        check(fa32[1] == -65536 && fa32[2] == -65536, 42);
        fa32[0] = fa32[5];                               // value copy
        check(fa32[0] == n32[5], 43);
        check(-fa32[3] == -n32[3] && fa32[4] * 2 == n32[4] * 2, 44);   // arithmetic
        fap[2] = FAR(0x8123456UL);
        check(!FAR_ISNULL(fap[2]) && FAR_ADD(fap[2], 4).a == 0x812345AUL, 45);  // farptr
        fa16[3] = (int16_t) -1234;
        check((int16_t) fa16[3] == -1234, 46);           // 2-byte, signed
        // A view starting 4 elements in (as costable overlays sintable).
        {
            static FarArray<int32_t, 16> whole;
            static FarArray<int32_t, 12> view;
            farptr base = far_alloc_chip(whole.bytes());
            whole.init(base);
            for (i = 0; i < 16; i++)
                whole[i] = n32[i];
            view.use(FAR_ADD(base, 4 * sizeof(int32_t)));
            for (i = 0; i < 12; i++)
                check(view[i] == n32[i + 4], 47);
        }
    }

    // --- FarByteGrid (tilemap, spotvis) ----------------------------------------------
    {
        typedef FarByteGrid<7, N> Grid;
        static Grid fg;
        static uint8_t ng[N][N];
        Grid::Ptr fp2, fq;
        uint8_t *np2, *nq;

        fg.init(far_alloc_chip(Grid::bytes()));
        memset(ng, 0, sizeof ng);
        for (x = 0; x < N; x++)
            for (y = 0; y < N; y++)
                check(fg[x][y] == 0, 50);                           // init clears
        for (x = 0; x < N; x++)
            for (y = 0; y < N; y++) {
                ng[x][y] = (uint8_t) (x * 31 + y * 7);
                fg[x][y] = (uint8_t) (x * 31 + y * 7);
            }
        ng[2][3] |= 0x40; fg[2][3] |= 0x40;                        // SpawnDoor
        ng[4][4]++;       fg[4][4]++;                              // elevator switch
        check(fg[2][3] == ng[2][3] && fg[4][4] == ng[4][4], 51);
        {
            uint8_t a = fg[4][4]++, b = ng[4][4]++;                // postfix value
            check(a == b, 52);
        }
        fp2 = &fg[3][3]; np2 = &ng[3][3];                          // &grid[x][y]
        check(*fp2 == *np2, 53);
        check(*(fp2 - 1) == *(np2 - 1) && *(fp2 + N) == *(np2 + N)
              && *(fp2 - (N + 1)) == *(np2 - (N + 1)), 54);        // neighbours
        fq = fg.flat() + (2 * N + 5); nq = (uint8_t *) ng + (2 * N + 5);
        check(fg.flat()[2 * N + 5] == ((uint8_t *) ng)[2 * N + 5] && *fq == *nq, 55);
        *(fg.flat() + 9) = 1; *((uint8_t *) ng + 9) = 1;          // spotvis mark
        check(!*fq == !*nq && fp2 != fq && (fp2 - 0) == fp2, 56);
        for (x = 0; x < N; x++)
            for (y = 0; y < N; y++)
                check(fg[x][y] == ng[x][y], 57);                   // whole grid agrees
        fg.clear();
        check(fg[5][6] == 0, 58);
    }

    // --- FarStructArray / FarStructPtr (statobjlist) ------------------------------------
    {
        struct st { uint8_t tx, ty; int16_t shape; uint16_t vis; uint32_t flags; uint8_t item; };
        static FarStructArray<st, 10> fa;
        static st na[10];
        FarStructPtr<st> flast, fp3;
        st *nlast, *np3;
        farptr storage = far_alloc_chip(fa.bytes());

        fa.init(storage);
        memset(na, 0, sizeof na);
        flast = &fa[0]; nlast = &na[0];                         // InitStaticList
        for (i = 0; i < 6; i++) {                               // SpawnStatic
            flast->shape = (int16_t) (100 + i); nlast->shape = (int16_t) (100 + i);
            flast->tx = (uint8_t) i; nlast->tx = (uint8_t) i;
            flast->flags = 0x10; nlast->flags = 0x10;
            flast->flags |= 0x3000; nlast->flags |= 0x3000;     // |= through ->
            flast++; nlast++;
        }
        check(flast - &fa[0] == 6 && flast != &fa[10] && !(flast == &fa[10]), 60);
        {
            FarStructPtr<st> p;                                 // DrawScaleds loop
            int k = 0, sum = 0;
            for (p = &fa[0]; p != flast; p++, k++) {
                if (p->shape == -1) continue;
                sum += p->shape + p->tx;
                if (p->flags & 0x10 && p->tx == 2)
                    p->shape = -1;                              // GetBonus-like
            }
            check(k == 6 && sum == (100+101+102+103+104+105) + (0+1+2+3+4+5), 61);
            na[2].shape = -1;
        }
        fp3 = fa + 3; np3 = na + 3;                             // array + i
        fp3->item = 9; np3->item = 9;
        (&fa[5])->ty = 7; na[5].ty = 7;                         // evicts element 3
        {
            st raw;                                             // write-back reached far memory
            far_read(&raw, FAR_ADD(storage, 3 * sizeof(st)), sizeof raw);
            check(raw.item == 9 && raw.shape == 103, 62);
        }
        {
            st copy = *(fa + 2);                                // savegame: nullstat = *(p)
            check(copy.shape == -1 && copy.flags == 0x3010, 63);
            copy.shape = 55;
            *(fa + 8) = copy;                                   // *(p) = nullstat
            check((&fa[8])->shape == 55 && (&fa[2])->shape == -1, 64);
        }
        for (i = 0; i < 6; i++)                                 // whole array agrees
            check((&fa[i])->shape == na[i].shape && (&fa[i])->ty == na[i].ty &&
                  (&fa[i])->item == na[i].item && (&fa[i])->flags == na[i].flags, 65);
    }

    struct { uint8_t magic; uint8_t firstfail; uint16_t fails, checks; } rep =
        { 0xEE, firstfail, fails, checks };
    m65_dma_copy(REPORT, (uint32_t)(uintptr_t)&rep, sizeof rep);
    m65_debug_puts(fails ? "PROXY-FAIL" : "PROXY-OK");
    m65_debug_puts("TEST-DONE");
    for (;;) {}
}

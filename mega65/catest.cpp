// Cache test: the real id_ca.cpp on the MEGA65. Loads every graphics chunk
// through CA_CacheGrChunk and every map through CA_CacheMap, and checks them
// against the original code run on the host (build/huffref.h, build/mapref.h).
#include <unistd.h>
#include "../wl_def.h"
#include "m65_debug.h"
#include "m65_video.h"

#include "build/huffref.h"
#include "build/mapref.h"

// --- what id_ca.cpp needs from the rest of the game ---------------------------
boolean       param_ignorenumchunks = false;
pictabletype *pictable;
SDMode        SoundMode;
extern int    numEpisodesMissing;

// (Arguments are not printed.)
void Quit (const char *error, ...)
{
    m65_debug_puts("QUIT:");
    m65_debug_puts(error ? error : "(null)");
    m65_debug_puts("TEST-DONE");
    for (;;) {}
}

static uint8_t screen_calls;
void VL_FarPlanarToScreen (farptr pic)
{
    (void)pic;
    screen_calls++;
}

// --- test ----------------------------------------------------------------------
#define REPORT 0x50000UL

struct report {
    uint8_t  magic;
    uint8_t  screen_calls;
    uint16_t grchunks, grbad, grfirstbad;
    uint16_t maps, mapbad, mapfirstbad;
    uint8_t  pictable_ok;           // pictable == start of chunk STRUCTPIC
    uint32_t heap;
};

static struct report rep;
static uint8_t blk[256];

static void sum_far(farptr p, uint32_t len, uint16_t *pa, uint16_t *pb)
{
    uint16_t a = 0, b = 0, n, i;
    uint32_t k;
    for (k = 0; k < len; k += n) {
        n = len - k > sizeof blk ? sizeof blk : (uint16_t)(len - k);
        far_read(blk, FAR_ADD(p, k), n);
        for (i = 0; i < n; i++) { a += blk[i]; b += a; }
    }
    *pa = a; *pb = b;
}

static void sum_near(const void *p, uint16_t len, uint16_t *pa, uint16_t *pb)
{
    const uint8_t *q = (const uint8_t *)p;
    uint16_t a = 0, b = 0, i;
    for (i = 0; i < len; i++) { a += q[i]; b += a; }
    *pa = a; *pb = b;
}

int main (void)
{
    uint16_t c, a, b;
    int m, plane;

    // llvm-mos starts with a small heap limit; the game's start-up will need
    // this too.
    __set_heap_limit(__get_heap_max_safe_size());

    strcpy(extension, "wl1");
    strcpy(graphext, "wl1");
    strcpy(audioext, "wl1");
    numEpisodesMissing = 5;         // what CheckForEpisodes sets for .wl1

    CA_Startup();

    for (c = 0; c < REF_NUMCHUNKS; c++) {
        if (ref[c].explen < 0)
            continue;
        CA_CacheGrChunk(c);
        sum_far(grsegs[c], ref[c].explen, &a, &b);
        rep.grchunks++;
        if (a != ref[c].sa || b != ref[c].sb) {
            if (!rep.grbad) rep.grfirstbad = c;
            rep.grbad++;
        }
    }

    {
        uint16_t na, nb, fa, fb;
        sum_near(pictable, NUMPICS * sizeof(pictabletype), &na, &nb);
        sum_far(grsegs[STRUCTPIC], NUMPICS * sizeof(pictabletype), &fa, &fb);
        rep.pictable_ok = na == fa && nb == fb;
    }

    CA_CacheScreen(TITLEPIC);
    rep.screen_calls = screen_calls;

    for (m = 0; m < REF_NUMMAPS; m++) {
        CA_CacheMap(m);
        for (plane = 0; plane < 2; plane++) {
            sum_near(mapsegs[plane], 64 * 64 * 2, &a, &b);
            if (a != mapref[m][plane].sa || b != mapref[m][plane].sb) {
                if (!rep.mapbad) rep.mapfirstbad = (uint16_t)(m * 2 + plane);
                rep.mapbad++;
            }
        }
        rep.maps++;
    }

    rep.heap = far_heap_used();
    rep.magic = 0xEE;
    m65_dma_copy(REPORT, (uint32_t)(uintptr_t)&rep, sizeof rep);
    m65_debug_puts("TEST-DONE");
    for (;;) {}
}

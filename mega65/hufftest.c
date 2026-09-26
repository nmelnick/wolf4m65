// Huffman test: expand every graphics chunk from attic RAM with
// far_huff_expand and compare against the original decoder's results
// (build/huffref.h, from tools/gen_huffref.py).
#include <stdint.h>

#include "m65_debug.h"
#include "m65_dos.h"
#include "m65_far.h"
#include "m65_huff.h"
#include "m65_video.h"

#include "build/huffref.h"

#define REPORT 0x50000UL

struct report {
    uint8_t  magic;
    int8_t   load_rc;
    uint16_t chunks, bad, first_bad;
    uint32_t total, heap;
};

static struct report rep;
static m65_huffnode dict[255];
static uint8_t head[(REF_NUMCHUNKS + 1) * 3];
static uint8_t blk[256];

static int32_t start(uint16_t c)
{
    int32_t v = head[c * 3] | (uint16_t)head[c * 3 + 1] << 8
              | (int32_t)head[c * 3 + 2] << 16;
    return v == 0xFFFFFFL ? -1 : v;
}

int main(void)
{
    farptr f, gr;
    uint32_t len;
    uint16_t c;

    rep.load_rc = m65_dos_init();
    f = far_load_file("VGADICT.WL1", &len);
    if (FAR_ISNULL(f)) rep.load_rc = 1;
    else far_read(dict, f, sizeof dict);
    f = far_load_file("VGAHEAD.WL1", &len);
    if (FAR_ISNULL(f)) rep.load_rc = 2;
    else far_read(head, f, sizeof head);
    gr = far_load_file("VGAGRAPH.WL1", &len);
    if (FAR_ISNULL(gr)) rep.load_rc = 3;

    for (c = 0; rep.load_rc == 0 && c < REF_NUMCHUNKS; c++) {
        int32_t pos = start(c);
        farptr src, dst;
        uint32_t explen, k;
        uint16_t a = 0, b = 0, n, i;

        if (pos < 0 || ref[c].explen < 0)
            continue;
        src = FAR_ADD(gr, pos + ref[c].hdr);   // skip the length longword
        explen = (uint32_t)ref[c].explen;
        dst = far_alloc(explen);

        far_huff_expand(src, dst, explen, dict);

        for (k = 0; k < explen; k += n) {
            n = explen - k > sizeof blk ? sizeof blk : (uint16_t)(explen - k);
            far_read(blk, FAR_ADD(dst, k), n);
            for (i = 0; i < n; i++) { a += blk[i]; b += a; }
        }
        rep.chunks++;
        rep.total += explen;
        if (a != ref[c].sa || b != ref[c].sb) {
            if (!rep.bad) rep.first_bad = c;
            rep.bad++;
        }
    }
    rep.heap = far_heap_used();
    rep.magic = 0xEE;
    m65_dma_copy(REPORT, (uint32_t)(uintptr_t)&rep, sizeof rep);
    m65_debug_puts("TEST-DONE");
    for (;;) {}
}

// Overlay test driver (resident). Checks are made by tools/check_ovltest.py.
#include <stdint.h>

#include "../m65_debug.h"
#include "../m65_video.h"
#include "../ovl/ovl_load.h"
#include "../m65_platform.h"
#include "ovltest.h"

#define REPORT 0x50000UL

struct report {
    uint8_t  magic;
    int8_t   load_rc;
    uint16_t sq7, add3, fib10, call_other5, tail4, deep6;
    int32_t  big;
    uint16_t after;             // an overlay call after all the others
    uint8_t  cur;               // __ovl_cur at the end
    uint8_t  layout_ok;         // table in mid (data file), objlist high, zeroed
    uint16_t rodata_at, objlist_at;
    uint16_t inner20;           // via_inner(20): a thunk-less direct call inside ov1
};

// Layout checks: a read-only table (.rodata_far: in mid memory, loaded from
// OVLTEST.DAT by m65_startup), and an array named objlist (.highbss: $E000+,
// zeroed).
static const uint8_t table[8] = { 3, 1, 4, 1, 5, 9, 2, 6 };
uint8_t objlist[1000];

extern uint8_t __ovl_cur;
static struct report rep;

// Resident function called from an overlay, which calls back into an overlay.
int res_helper(int x) { return add3(x, x, x) - 1; }

int main(void)
{
    // Takeover, .data from OVLTEST.DAT, overlays from OVLTEST.OVL (a failure
    // stops there, and the test times out).
    m65_startup("OVLTEST.OVL", "OVLTEST.DAT");
    {
        unsigned i, zero = 1;
        for (i = 0; i < sizeof objlist; i++)
            if (objlist[i]) zero = 0;
        objlist[999] = 0xA5;
        rep.rodata_at = (uint16_t)(uintptr_t)table;
        rep.objlist_at = (uint16_t)(uintptr_t)objlist;
        rep.layout_ok = zero && objlist[999] == 0xA5
                     && table[0] == 3 && table[5] == 9 && table[7] == 6
                     && rep.rodata_at >= 0x6000 && rep.rodata_at < 0xD000
                     && rep.objlist_at >= 0xE000;
    }
    rep.load_rc = 0;
    if (rep.load_rc == 0) {
        rep.sq7         = sq_ptr(7);
        rep.add3        = add3(1, 2, 3);
        rep.big         = big(100000L, 3L);
        rep.fib10       = fib(10);
        rep.call_other5 = call_other(5);
        rep.tail4       = tail(4);
        rep.deep6       = deep(6);
        rep.after       = add3(10, 20, 30);
        rep.inner20     = via_inner(20);
        rep.cur         = __ovl_cur;
    }
    rep.magic = 0xEE;
    m65_dma_copy(REPORT, (uint32_t)(uintptr_t)&rep, sizeof rep);
    m65_debug_puts("TEST-DONE");
    for (;;) {}
}

// Overlay test driver (resident). Checks are made by tools/check_ovltest.py.
#include <stdint.h>

#include "../m65_debug.h"
#include "../m65_video.h"
#include "../ovl/ovl_load.h"
#include "ovltest.h"

#define REPORT 0x50000UL

struct report {
    uint8_t  magic;
    int8_t   load_rc;
    uint16_t sq7, add3, fib10, call_other5, tail4, deep6;
    int32_t  big;
    uint16_t after;             // an overlay call after all the others
    uint8_t  cur;               // __ovl_cur at the end
};

extern uint8_t __ovl_cur;
static struct report rep;

// Resident function called from an overlay, which calls back into an overlay.
int res_helper(int x) { return add3(x, x, x) - 1; }

int main(void)
{
    rep.load_rc = (int8_t)ovl_load("OVLTEST.OVL");
    if (rep.load_rc == 0) {
        rep.sq7         = sq_ptr(7);
        rep.add3        = add3(1, 2, 3);
        rep.big         = big(100000L, 3L);
        rep.fib10       = fib(10);
        rep.call_other5 = call_other(5);
        rep.tail4       = tail(4);
        rep.deep6       = deep(6);
        rep.after       = add3(10, 20, 30);
        rep.cur         = __ovl_cur;
    }
    rep.magic = 0xEE;
    m65_dma_copy(REPORT, (uint32_t)(uintptr_t)&rep, sizeof rep);
    m65_debug_puts("TEST-DONE");
    for (;;) {}
}

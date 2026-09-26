// Test hooks in the game build.
//
// FrameDumpHook: called by ThreeDRefresh for every frame of a demo, after
// the frame is drawn into the draw buffer and before it is shown. Built with
// -DM65_FRAMEDUMP=N (make FRAME=N run-frame), it stops at demo frame N and
// says TEST-DONE, so that a memory dump holds that frame (the draw buffer at
// $50000) for tools/check_frame.py. Otherwise it does nothing.

#include <stdint.h>

#include "m65_debug.h"

void FrameDumpHook(void)
{
#ifdef M65_FRAMEDUMP
    static uint16_t frame;
    if (++frame == M65_FRAMEDUMP) {
        m65_debug_puts("TEST-DONE");
        for (;;)
            ;
    }
#endif
}

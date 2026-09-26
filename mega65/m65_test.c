// Test hooks in the game build.
//
// FrameDumpHook: called by ThreeDRefresh for every frame of a demo, after
// the frame is drawn into the draw buffer and before it is shown.
//   - Every 100 demo frames it reports the average time per frame on the
//     debug serial port ("FPS: frames 1-101: 123 ms/frame, 8.1 fps"). Demos
//     advance a fixed number of tics per frame, so this is repeatable.
//   - Built with -DM65_FRAMEDUMP=N (make FRAME=N check-frame), it stops at
//     demo frame N and says TEST-DONE, so that a memory dump holds that
//     frame (the draw buffer at $50000) for tools/check_frame.py.

#include <stdint.h>

#include "SDL.h"
#include "m65_debug.h"

static void put_uint(uint32_t v)
{
    char buf[11];
    uint8_t i = sizeof buf;
    do {
        buf[--i] = '0' + v % 10;
        v /= 10;
    } while (v);
    while (i < sizeof buf)
        m65_debug_putc(buf[i++]);
}

static void put_str(const char *s)
{
    while (*s)
        m65_debug_putc(*s++);
}

void FrameDumpHook(void)
{
    static uint16_t frame;
    static Uint32 start;
    Uint32 now = SDL_GetTicks();

    frame++;
    if (frame == 1)
        start = now;
    else if (frame % 100 == 1) {
        Uint32 ms = now - start;
        put_str("FPS: frames ");
        put_uint(frame - 100);
        m65_debug_putc('-');
        put_uint(frame);
        put_str(": ");
        put_uint(ms / 100);
        put_str(" ms/frame, ");
        put_uint(100000UL / ms);
        m65_debug_putc('.');
        put_uint(1000000UL / ms % 10);
        m65_debug_puts(" fps");
        start = now;
    }
#ifdef M65_FRAMEDUMP
    if (frame == M65_FRAMEDUMP) {
        m65_debug_puts("TEST-DONE");
        for (;;)
            ;
    }
#endif
}

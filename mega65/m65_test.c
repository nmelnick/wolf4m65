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
//   - Built with -DM65_PROFILE as well (make profile), it samples where the
//     CPU is from demo frame 1 to frame N (m65_prof.s) and at frame N copies
//     the counts to $40000 for tools/profile.py.
// With -DM65_PROFILE_ALL (make profile-load), the sampling runs from start-up
// (m65_test_startup, from SD_Startup) to the first frame of a demo or of
// play (FrameDumpHook, GameFrameHook): the sign-on, title, menus, fades and
// level loading.

#include <stdint.h>

#include "SDL.h"
#include "m65_debug.h"
#include "m65_video.h"

#ifdef M65_PROFILE
#define PROF_BASE  0x87F0000UL      // the counters, in attic RAM (see m65_prof.s)
#define PROF_BYTES (8192U + 44U * 1024U)
#define PROF_COPY  0x40000UL        // where the memory dump finds them

extern char m65_prof_irq[];

static void prof_start(void)
{
    m65_dma_fill(PROF_BASE, 0, PROF_BYTES);
    *(volatile uint16_t *)0xFFFE = (uint16_t)(uintptr_t)m65_prof_irq;
    *(volatile uint8_t *)0xD01A = 0;        // no VIC interrupts
    *(volatile uint8_t *)0xD019 = 0xFF;
    *(volatile uint8_t *)0xDC0D = 0x7F;     // CIA 1: only timer A,
    *(volatile uint8_t *)0xDC04 = 999 & 0xFF;   // every 1000 cycles (1ms at 1MHz)
    *(volatile uint8_t *)0xDC05 = 999 >> 8;
    *(volatile uint8_t *)0xDC0E = 0x11;     // load, start, continuous
    (void)*(volatile uint8_t *)0xDC0D;
    *(volatile uint8_t *)0xDC0D = 0x81;
    __asm__ volatile("cli");
}

static void prof_stop(void)
{
    __asm__ volatile("sei");
    *(volatile uint8_t *)0xDC0D = 0x7F;
    m65_dma_copy(PROF_COPY, PROF_BASE, PROF_BYTES);
}
#endif

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

extern int8_t fpscounter;       // wl_draw.cpp

void FrameDumpHook(void)
{
    static uint16_t frame;
    static Uint32 start;
    Uint32 now = SDL_GetTicks();

    frame++;
    if (frame == 1) {
        start = now;
#ifdef M65_FRAMEDUMP
        fpscounter = 0;         // (the host's frames have no frame rate counter)
#endif
#if defined(M65_PROFILE) && !defined(M65_PROFILE_ALL)
        prof_start();
#endif
#ifdef M65_PROFILE_ALL                  // (start-up to here: the demo's first frame)
        prof_stop();
        m65_debug_puts("DEMO");
        m65_debug_puts("TEST-DONE");
        for (;;)
            ;
#endif
    }
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
#ifdef M65_PROFILE
        prof_stop();
#endif
        m65_debug_puts("TEST-DONE");
        for (;;)
            ;
    }
#endif
}

// Start-up (SD_Startup's end, after the timer interrupt is set up).
void m65_test_startup(void)
{
#ifdef M65_PROFILE_ALL
    prof_start();
#endif
}

// Every frame of play (not a demo's).
void GameFrameHook(void)
{
#ifdef M65_PROFILE_ALL
    prof_stop();
    m65_debug_puts("GAME");
    m65_debug_puts("TEST-DONE");
    for (;;)
        ;
#endif
}

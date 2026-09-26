// Implementation of the SDL subset in SDL.h, on the MEGA65 hardware.
//
// Surfaces: pixels in far memory (see SDL.h and m65_surf.c). The two screen
// surfaces are created by the video layer (m65_vl.cpp); everything else is a
// linear surface in the far heap.
// Time: CIA2 timer A divides the CIA clock to 1ms ticks, timer B counts them.
// Input: the keyboard is in m65_kbd.c; no mouse, no joysticks.

#include <stdint.h>
#include <stdlib.h>

#include "SDL.h"
#include "m65_far.h"
#include "m65_surf.h"

// ---------------------------------------------------------------------------
// General
// ---------------------------------------------------------------------------

static void timer_init(void);

int SDL_Init(Uint32 flags)
{
    (void)flags;
    timer_init();
    return 0;
}

void SDL_Quit(void) {}
const char *SDL_GetError(void) { return "SDL error"; }
void SDL_WM_SetCaption(const char *title, const char *icon) { (void)title; (void)icon; }
SDL_GrabMode SDL_WM_GrabInput(SDL_GrabMode mode) { return mode; }
int SDL_ShowCursor(int toggle) { (void)toggle; return 0; }

// ---------------------------------------------------------------------------
// Surfaces
// ---------------------------------------------------------------------------

static SDL_PixelFormat format8 = { 1 };

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int bpp,
                                  Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask)
{
    SDL_Surface *s;
    farptr p;

    (void)bpp; (void)rmask; (void)gmask; (void)bmask; (void)amask;
    p = far_alloc((uint32_t)w * h);
    if (FAR_ISNULL(p))
        return NULL;
    s = (SDL_Surface *)calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->flags = flags;
    s->format = &format8;
    s->w = w;
    s->h = h;
    s->pitch = (Uint16)w;
    s->farpixels = p.a;
    return s;
}

// The far pixels are not reclaimed: the far heap never frees (see m65_far.h).
void SDL_FreeSurface(SDL_Surface *s)
{
    free(s);
}

// Palettes belong to the display only; the video layer sets it directly.
int SDL_SetColors(SDL_Surface *s, SDL_Color *colors, int first, int n)
{
    (void)s; (void)colors; (void)first; (void)n;
    return 1;
}

int SDL_SetPalette(SDL_Surface *s, int which, SDL_Color *colors, int first, int n)
{
    (void)which;
    return SDL_SetColors(s, colors, first, n);
}

// Clip a w x h rectangle at (*x, *y) against 0..maxw x 0..maxh; adjusts the
// source offsets with it. Returns 0 if nothing is left.
static int clip(int *x, int *y, int *w, int *h, int maxw, int maxh, int *sx, int *sy)
{
    if (*x < 0) { *w += *x; *sx -= *x; *x = 0; }
    if (*y < 0) { *h += *y; *sy -= *y; *y = 0; }
    if (*x + *w > maxw) *w = maxw - *x;
    if (*y + *h > maxh) *h = maxh - *y;
    return *w > 0 && *h > 0;
}

int SDL_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
    int sx = 0, sy = 0, w = src->w, h = src->h, dx = 0, dy = 0, zx = 0, zy = 0;

    if (srcrect) {
        sx = srcrect->x; sy = srcrect->y; w = srcrect->w; h = srcrect->h;
        if (!clip(&sx, &sy, &w, &h, src->w, src->h, &zx, &zy))
            return 0;
    }
    if (dstrect) {
        dx = dstrect->x;
        dy = dstrect->y;
    }
    if (!clip(&dx, &dy, &w, &h, dst->w, dst->h, &sx, &sy))
        return 0;
    surf_copy_rect(src, sx, sy, dst, dx, dy, w, h);
    return 0;
}

int SDL_FillRect(SDL_Surface *dst, SDL_Rect *rect, Uint32 color)
{
    int x = 0, y = 0, w = dst->w, h = dst->h, zx = 0, zy = 0;

    if (rect) {
        x = rect->x; y = rect->y; w = rect->w; h = rect->h;
    }
    if (!clip(&x, &y, &w, &h, dst->w, dst->h, &zx, &zy))
        return 0;
    surf_fill_rect(dst, x, y, w, h, (uint8_t)color);
    return 0;
}

// The visible surface is displayed directly: nothing to flip.
int SDL_Flip(SDL_Surface *s) { (void)s; return 0; }

Uint32 SDL_MapRGB(SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b)
{
    (void)fmt; (void)r; (void)g; (void)b;
    return 0;
}

int SDL_SaveBMP(SDL_Surface *s, const char *file) { (void)s; (void)file; return -1; }

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

#define CIA2_TALO  (*(volatile uint8_t *)0xDD04)
#define CIA2_TAHI  (*(volatile uint8_t *)0xDD05)
#define CIA2_TBLO  (*(volatile uint8_t *)0xDD06)
#define CIA2_TBHI  (*(volatile uint8_t *)0xDD07)
#define CIA2_ICR   (*(volatile uint8_t *)0xDD0D)
#define CIA2_CRA   (*(volatile uint8_t *)0xDD0E)
#define CIA2_CRB   (*(volatile uint8_t *)0xDD0F)

// CIA clock cycles per millisecond. Measured against the video frame rate in
// Xemu (titletest): the CIAs run at 1.000 MHz, not at the C64's PAL clock.
#define CIA_PER_MS 1000

static uint16_t last_tb;
static uint32_t ticks;
static uint8_t timer_ready;

static void timer_init(void)
{
    CIA2_ICR = 0x7F;                    // no CIA2 interrupts (NMI)
    CIA2_CRA = 0;
    CIA2_CRB = 0;
    CIA2_TALO = (CIA_PER_MS - 1) & 0xFF;
    CIA2_TAHI = (CIA_PER_MS - 1) >> 8;
    CIA2_TBLO = 0xFF;
    CIA2_TBHI = 0xFF;
    CIA2_CRB = 0x51;                    // load, count timer A underflows, start
    CIA2_CRA = 0x11;                    // load, continuous, count clock, start
    last_tb = 0xFFFF;
    ticks = 0;
    timer_ready = 1;
}

static uint16_t read_tb(void)
{
    uint8_t hi, lo;
    do {
        hi = CIA2_TBHI;
        lo = CIA2_TBLO;
    } while (hi != CIA2_TBHI);
    return (uint16_t)hi << 8 | lo;
}

// Must be called at least once every 65 seconds (it is, constantly).
Uint32 SDL_GetTicks(void)
{
    uint16_t tb;

    if (!timer_ready)
        timer_init();
    tb = read_tb();
    ticks += (uint16_t)(last_tb - tb);  // timer B counts down
    last_tb = tb;
    return ticks;
}

void SDL_Delay(Uint32 ms)
{
    Uint32 start = SDL_GetTicks();
    while (SDL_GetTicks() - start < ms)
        ;
}

// ---------------------------------------------------------------------------
// Input (not yet implemented)
// ---------------------------------------------------------------------------

// (Keyboard events: m65_kbd.c.)

Uint8 SDL_EventState(Uint8 type, int state) { (void)type; (void)state; return 0; }

Uint8 SDL_GetMouseState(int *x, int *y)
{
    if (x) *x = 0;
    if (y) *y = 0;
    return 0;
}

void SDL_WarpMouse(Uint16 x, Uint16 y) { (void)x; (void)y; }

int SDL_NumJoysticks(void) { return 0; }
SDL_Joystick *SDL_JoystickOpen(int index) { (void)index; return NULL; }
void SDL_JoystickClose(SDL_Joystick *j) { (void)j; }
void SDL_JoystickUpdate(void) {}
Sint16 SDL_JoystickGetAxis(SDL_Joystick *j, int axis) { (void)j; (void)axis; return 0; }
Uint8 SDL_JoystickGetHat(SDL_Joystick *j, int hat) { (void)j; (void)hat; return 0; }
Uint8 SDL_JoystickGetButton(SDL_Joystick *j, int button) { (void)j; (void)button; return 0; }
int SDL_JoystickNumButtons(SDL_Joystick *j) { (void)j; return 0; }
int SDL_JoystickNumHats(SDL_Joystick *j) { (void)j; return 0; }

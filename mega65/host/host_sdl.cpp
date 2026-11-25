// The SDL subset in ../SDL.h, for building the original game on the host as
// a reference for the MEGA65 port (see ../Makefile, "Host reference").
//
// Nothing is displayed or heard: surfaces are 8-bit pixels in RAM. Time is
// virtual, so a run does not depend on the host's speed: SDL_Delay advances
// the clock, and every SDL_GetTicks call advances it by 1ms (so that loops
// that only watch the clock end). Input: the first wait for a key (the
// sign-on screen's "Press a key") gets SPACE; there are no other keys.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"

static Uint32 now;
static SDL_PixelFormat format8 = { 1 };
static SDL_Surface *screen;

int SDL_Init(Uint32 flags) { (void)flags; return 0; }
void SDL_Quit(void) {}
const char *SDL_GetError(void) { return "host SDL error"; }
void SDL_WM_SetCaption(const char *title, const char *icon) { (void)title; (void)icon; }
SDL_GrabMode SDL_WM_GrabInput(SDL_GrabMode mode) { return mode; }
int SDL_ShowCursor(int toggle) { (void)toggle; return 0; }

const SDL_VideoInfo *SDL_GetVideoInfo(void)
{
    static SDL_VideoFormat vfmt = { 8 };
    static SDL_VideoInfo info = { 0, &vfmt };
    return &info;
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int bpp,
                                  Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask)
{
    (void)bpp; (void)rmask; (void)gmask; (void)bmask; (void)amask;
    SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof *s);
    s->flags = flags;
    s->format = &format8;
    s->w = w;
    s->h = h;
    s->pitch = (Uint16)w;
    s->pixels = calloc(1, (size_t)w * h);
    return s;
}

SDL_Surface *SDL_SetVideoMode(int w, int h, int bpp, Uint32 flags)
{
    if (!screen)
        screen = SDL_CreateRGBSurface(flags, w, h, bpp, 0, 0, 0, 0);
    return screen;
}

void SDL_FreeSurface(SDL_Surface *s)
{
    if (s) {
        free(s->pixels);
        free(s);
    }
}

int SDL_LockSurface(SDL_Surface *s) { (void)s; return 0; }
void SDL_UnlockSurface(SDL_Surface *s) { (void)s; }
int SDL_SetColors(SDL_Surface *s, SDL_Color *c, int first, int n) { (void)s; (void)c; (void)first; (void)n; return 1; }
int SDL_SetPalette(SDL_Surface *s, int which, SDL_Color *c, int first, int n) { (void)which; return SDL_SetColors(s, c, first, n); }
int SDL_Flip(SDL_Surface *s) { (void)s; return 0; }
Uint32 SDL_MapRGB(SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b) { (void)fmt; (void)g; (void)b; return r; }

int SDL_SaveBMP(SDL_Surface *s, const char *file)
{
    FILE *f = fopen(file, "wb");
    if (!f)
        return -1;
    fwrite(s->pixels, 1, (size_t)s->pitch * s->h, f);
    fclose(f);
    return 0;
}

// Clip [*x, *x + *w) to [0, max), moving the source offset with it.
static int clip1(int *x, int *w, int max, int *sx)
{
    if (*x < 0) { *w += *x; *sx -= *x; *x = 0; }
    if (*x + *w > max) *w = max - *x;
    return *w > 0;
}

int SDL_BlitSurface(SDL_Surface *src, SDL_Rect *sr, SDL_Surface *dst, SDL_Rect *dr)
{
    int sx = sr ? sr->x : 0, sy = sr ? sr->y : 0;
    int w = sr ? sr->w : src->w, h = sr ? sr->h : src->h;
    int dx = dr ? dr->x : 0, dy = dr ? dr->y : 0;

    if (!clip1(&dx, &w, dst->w, &sx) || !clip1(&dy, &h, dst->h, &sy))
        return 0;
    if (!clip1(&sx, &w, src->w, &dx) || !clip1(&sy, &h, src->h, &dy))
        return 0;
    for (int y = 0; y < h; y++)
        memmove((Uint8 *)dst->pixels + (dy + y) * dst->pitch + dx,
                (Uint8 *)src->pixels + (sy + y) * src->pitch + sx, (size_t)w);
    return 0;
}

int SDL_FillRect(SDL_Surface *dst, SDL_Rect *r, Uint32 color)
{
    int x = r ? r->x : 0, y = r ? r->y : 0;
    int w = r ? r->w : dst->w, h = r ? r->h : dst->h, dummy = 0;

    if (!clip1(&x, &w, dst->w, &dummy) || !clip1(&y, &h, dst->h, &dummy))
        return 0;
    for (int i = 0; i < h; i++)
        memset((Uint8 *)dst->pixels + (y + i) * dst->pitch + x, (int)color, (size_t)w);
    return 0;
}

Uint32 SDL_GetTicks(void) { return now++; }
void SDL_Delay(Uint32 ms) { now += ms; }

// ---- input ----------------------------------------------------------------

static int pending_up;

int SDL_PollEvent(SDL_Event *e)
{
    if (!pending_up)
        return 0;
    pending_up = 0;
    if (e) {
        memset(e, 0, sizeof *e);
        e->type = SDL_KEYUP;
        e->key.keysym.sym = SDLK_SPACE;
    }
    return 1;
}

// Every wait for a key gets SPACE (down now, up at the next poll).
int SDL_WaitEvent(SDL_Event *e)
{
    now += 10;
    if (e) {
        memset(e, 0, sizeof *e);
        e->type = SDL_KEYDOWN;
        e->key.state = 1;
        e->key.keysym.sym = SDLK_SPACE;
    }
    pending_up = 1;
    return 1;
}

SDLMod SDL_GetModState(void) { return KMOD_NONE; }
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

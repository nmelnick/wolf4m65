// Minimal SDL 1.2 compatibility shim for the MEGA65 port.
//
// Wolf4SDL only touches a small part of SDL. This header declares just that
// part; the implementations live in m65_sdl.c and talk straight to the
// hardware. Nothing here is a general SDL replacement.

#ifndef M65_SDL_H
#define M65_SDL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;

#define SDL_VERSION(v)

// ---- video ----------------------------------------------------------------

typedef struct { Uint8 r, g, b, unused; } SDL_Color;
typedef struct { Sint16 x, y; Uint16 w, h; } SDL_Rect;

typedef struct { Uint8 BytesPerPixel; } SDL_PixelFormat;

// On the MEGA65 pixels are in far memory, so `pixels` is always NULL and
// `farpixels` holds the far address of pixel (0,0). A tiled surface uses the
// VIC-IV full-colour character layout (see m65_video.h; 320x200 only), a
// linear one is row-major with `pitch` bytes per row.
typedef struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat *format;
    int w, h;
    Uint16 pitch;
    void *pixels;
    Uint32 farpixels;
    Uint8 tiled;
} SDL_Surface;

#define SDL_SWSURFACE  0x00000000
#define SDL_HWSURFACE  0x00000001
#define SDL_HWPALETTE  0x20000000
#define SDL_DOUBLEBUF  0x40000000
#define SDL_FULLSCREEN 0x80000000
#define SDL_LOGPAL     0x01
#define SDL_PHYSPAL    0x02
#define SDL_MUSTLOCK(s) 0

typedef struct { Uint8 BitsPerPixel; } SDL_VideoFormat;
typedef struct { Uint32 video_mem; SDL_VideoFormat *vfmt; } SDL_VideoInfo;

#define SDL_INIT_VIDEO    0x00000020
#define SDL_INIT_AUDIO    0x00000010
#define SDL_INIT_JOYSTICK 0x00000200

#define SDL_DISABLE 0
#define SDL_ENABLE  1
#define SDL_IGNORE  0

int   SDL_Init(Uint32 flags);
void  SDL_Quit(void);
const char *SDL_GetError(void);
void  SDL_WM_SetCaption(const char *title, const char *icon);
typedef enum { SDL_GRAB_OFF, SDL_GRAB_ON } SDL_GrabMode;
SDL_GrabMode SDL_WM_GrabInput(SDL_GrabMode mode);
int   SDL_ShowCursor(int toggle);
const SDL_VideoInfo *SDL_GetVideoInfo(void);
SDL_Surface *SDL_SetVideoMode(int w, int h, int bpp, Uint32 flags);
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int bpp,
                                  Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask);
void  SDL_FreeSurface(SDL_Surface *s);
int   SDL_LockSurface(SDL_Surface *s);
void  SDL_UnlockSurface(SDL_Surface *s);
int   SDL_SetColors(SDL_Surface *s, SDL_Color *colors, int first, int n);
int   SDL_SetPalette(SDL_Surface *s, int which, SDL_Color *colors, int first, int n);
int   SDL_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
int   SDL_FillRect(SDL_Surface *dst, SDL_Rect *rect, Uint32 color);
int   SDL_Flip(SDL_Surface *s);
int   SDL_SaveBMP(SDL_Surface *s, const char *file);
Uint32 SDL_MapRGB(SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b);

// ---- time -----------------------------------------------------------------

Uint32 SDL_GetTicks(void);
void   SDL_Delay(Uint32 ms);

// ---- input ----------------------------------------------------------------

typedef enum {
    SDLK_UNKNOWN = 0,
    SDLK_BACKSPACE = 8, SDLK_TAB = 9, SDLK_RETURN = 13, SDLK_PAUSE = 19,
    SDLK_ESCAPE = 27, SDLK_SPACE = 32,
    SDLK_0 = 48, SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7, SDLK_8, SDLK_9,
    SDLK_a = 97, SDLK_b, SDLK_c, SDLK_d, SDLK_e, SDLK_f, SDLK_g, SDLK_h, SDLK_i,
    SDLK_j, SDLK_k, SDLK_l, SDLK_m, SDLK_n, SDLK_o, SDLK_p, SDLK_q, SDLK_r,
    SDLK_s, SDLK_t, SDLK_u, SDLK_v, SDLK_w, SDLK_x, SDLK_y, SDLK_z,
    SDLK_DELETE = 127,
    SDLK_KP2 = 258, SDLK_KP4 = 260, SDLK_KP5 = 261, SDLK_KP6 = 262, SDLK_KP8 = 264,
    SDLK_KP_ENTER = 271,
    SDLK_UP = 273, SDLK_DOWN, SDLK_RIGHT, SDLK_LEFT, SDLK_INSERT, SDLK_HOME,
    SDLK_END, SDLK_PAGEUP, SDLK_PAGEDOWN,
    SDLK_F1 = 282, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5, SDLK_F6, SDLK_F7,
    SDLK_F8, SDLK_F9, SDLK_F10, SDLK_F11, SDLK_F12,
    SDLK_CAPSLOCK = 301, SDLK_SCROLLOCK = 302,
    SDLK_RSHIFT = 303, SDLK_LSHIFT, SDLK_RCTRL, SDLK_LCTRL, SDLK_RALT, SDLK_LALT,
    SDLK_PRINT = 316,
    SDLK_LAST = 322
} SDLKey;

typedef enum {
    KMOD_NONE = 0, KMOD_LSHIFT = 0x0001, KMOD_RSHIFT = 0x0002,
    KMOD_SHIFT = 0x0003, KMOD_CAPS = 0x2000, KMOD_NUM = 0x1000
} SDLMod;
SDLMod SDL_GetModState(void);

typedef struct { Uint8 scancode; SDLKey sym; SDLMod mod; Uint16 unicode; } SDL_keysym;
typedef struct { Uint8 type, state; SDL_keysym keysym; } SDL_KeyboardEvent;
typedef struct { Uint8 type, gain, state; } SDL_ActiveEvent;

#define SDL_ACTIVEEVENT   1
#define SDL_KEYDOWN       2
#define SDL_KEYUP         3
#define SDL_MOUSEMOTION   4
#define SDL_JOYBUTTONDOWN 10
#define SDL_JOYBUTTONUP   11
#define SDL_QUIT          12
#define SDL_APPACTIVE     0x04

typedef union {
    Uint8 type;
    SDL_ActiveEvent active;
    SDL_KeyboardEvent key;
} SDL_Event;

int  SDL_PollEvent(SDL_Event *e);
int  SDL_WaitEvent(SDL_Event *e);
Uint8 SDL_EventState(Uint8 type, int state);

#define SDL_BUTTON(x)        (1 << ((x) - 1))
#define SDL_BUTTON_LEFT      1
#define SDL_BUTTON_MIDDLE    2
#define SDL_BUTTON_RIGHT     3
Uint8 SDL_GetMouseState(int *x, int *y);
void  SDL_WarpMouse(Uint16 x, Uint16 y);

typedef struct SDL_Joystick SDL_Joystick;
#define SDL_HAT_UP    0x01
#define SDL_HAT_RIGHT 0x02
#define SDL_HAT_DOWN  0x04
#define SDL_HAT_LEFT  0x08
int  SDL_NumJoysticks(void);
SDL_Joystick *SDL_JoystickOpen(int index);
void SDL_JoystickClose(SDL_Joystick *j);
void SDL_JoystickUpdate(void);
Sint16 SDL_JoystickGetAxis(SDL_Joystick *j, int axis);
Uint8  SDL_JoystickGetHat(SDL_Joystick *j, int hat);
Uint8  SDL_JoystickGetButton(SDL_Joystick *j, int button);
int    SDL_JoystickNumButtons(SDL_Joystick *j);
int    SDL_JoystickNumHats(SDL_Joystick *j);

#ifdef __cplusplus
}
#endif

#endif

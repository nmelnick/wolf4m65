// Keyboard input for the SDL shim: SDL_PollEvent, SDL_WaitEvent and
// SDL_GetModState, from the MEGA65's C65 keyboard matrix.
//
// The MEGA65 exposes the matrix directly (user guide, "C65 Keyboard Matrix"):
// write a column number (0-8) to $D614, read its rows from $D613, where a
// clear bit means the key is down. The matrix keeps the C64 way of cursor up
// and left (right shift + cursor down / right); $D60F bits 0 and 1 are set
// while the real cursor left / up keys are held, and are used to report them
// as keys of their own. Every poll scans the matrix and reports one change
// (key down or up) per event, as SDL keysyms that id_in understands.
//
// For tests, -DM65_KEYSCRIPT="{ms, scancode}, ..." presses keys at given
// times (SDL_GetTicks) through the virtual-key register $D615 (scancode
// column * 8 + row; $7F releases), so they travel the same path as real keys.

#include <stdint.h>

#include "SDL.h"
#ifdef M65_KEYSCRIPT
#include "m65_debug.h"
#endif

#define KBD_COLDATA (*(volatile uint8_t *)0xD613)
#define KBD_COLSEL  (*(volatile uint8_t *)0xD614)
#define KBD_LEFTUP  (*(volatile uint8_t *)0xD60F)
#define KBD_VIRTKEY (*(volatile uint8_t *)0xD615)

#define NCOLS 10    // the 9 matrix columns, plus column 9: bit 0 cursor left, bit 1 up

// Matrix position (column * 8 + row) -> SDL keysym. 0: not reported.
static const uint16_t keysyms[NCOLS * 8] = {
    // column 0: DEL RETURN RIGHT F7 F1 F3 F5 DOWN
    SDLK_BACKSPACE, SDLK_RETURN, SDLK_RIGHT, SDLK_F7, SDLK_F1, SDLK_F3, SDLK_F5, SDLK_DOWN,
    // column 1: 3 W A 4 Z S E LSHIFT
    SDLK_3, SDLK_w, SDLK_a, SDLK_4, SDLK_z, SDLK_s, SDLK_e, SDLK_LSHIFT,
    // column 2: 5 R D 6 C F T X
    SDLK_5, SDLK_r, SDLK_d, SDLK_6, SDLK_c, SDLK_f, SDLK_t, SDLK_x,
    // column 3: 7 Y G 8 B H U V
    SDLK_7, SDLK_y, SDLK_g, SDLK_8, SDLK_b, SDLK_h, SDLK_u, SDLK_v,
    // column 4: 9 I J 0 M K O N
    SDLK_9, SDLK_i, SDLK_j, SDLK_0, SDLK_m, SDLK_k, SDLK_o, SDLK_n,
    // column 5: + P L - . : @ ,
    '+', SDLK_p, SDLK_l, '-', '.', ':', '@', ',',
    // column 6: pound * ; HOME RSHIFT = up-arrow /
    0, '*', ';', SDLK_HOME, SDLK_RSHIFT, '=', 0, '/',
    // column 7: 1 left-arrow CTRL 2 SPACE MEGA Q RUN/STOP
    SDLK_1, 0, SDLK_LCTRL, SDLK_2, SDLK_SPACE, 0, SDLK_q, SDLK_PAUSE,
    // column 8: NO SCROLL, TAB, ALT, HELP, F9, F11, F13, ESC
    0, SDLK_TAB, SDLK_LALT, SDLK_F1, SDLK_F9, SDLK_F11, 0, SDLK_ESCAPE,
    // column 9: cursor left, cursor up
    SDLK_LEFT, SDLK_UP, 0, 0, 0, 0, 0, 0,
};

static uint8_t reported[NCOLS] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
// The MEGA65's function keys are F1/F2, F3/F4 ... F13/F14, the even one with
// SHIFT: the keys pressed with SHIFT (a bit per matrix position, like
// `reported`) report the even one, when pressed and again when let go.
static uint8_t shiftedf[NCOLS];

#ifdef M65_KEYSCRIPT
static const struct { Uint32 ms; uint8_t scancode; } keyscript[] = { M65_KEYSCRIPT };
static uint8_t keyscript_pos;

static void run_keyscript(void)
{
    while (keyscript_pos < sizeof keyscript / sizeof keyscript[0]
           && SDL_GetTicks() >= keyscript[keyscript_pos].ms) {
        uint8_t sc = keyscript[keyscript_pos++].scancode;
        KBD_VIRTKEY = sc;
        m65_debug_putc('K');            // (in the serial log: K and the scancode)
        m65_debug_putc("0123456789ABCDEF"[sc >> 4]);
        m65_debug_putc("0123456789ABCDEF"[sc & 15]);
        m65_debug_puts("");
    }
}
#endif

// Current state of the keys, in the same form as `reported`.
static void scan(uint8_t *now)
{
    uint8_t col, leftup;

    for (col = 0; col < 9; col++) {
        KBD_COLSEL = col;
        now[col] = KBD_COLDATA;
    }
    leftup = KBD_LEFTUP & 3;
    now[9] = (uint8_t)~leftup;
    if (leftup) {
        now[6] |= 1 << 4;                   // not the right shift the key implies
        if (leftup & 1)
            now[0] |= 1 << 2;               // left, not right
        if (leftup & 2)
            now[0] |= 1 << 7;               // up, not down
    }
}

int SDL_PollEvent(SDL_Event *e)
{
    uint8_t now[NCOLS], col, bit, diff, up;
    uint16_t sym;

#ifdef M65_KEYSCRIPT
    run_keyscript();
#endif
    scan(now);
    for (col = 0; col < NCOLS; col++) {
        diff = now[col] ^ reported[col];
        if (!diff)
            continue;
        for (bit = 0; !(diff & (1 << bit)); bit++)
            ;
        reported[col] ^= 1 << bit;
        sym = keysyms[col * 8 + bit];
        if (!sym)
            continue;                       // (look again next time)
        up = (now[col] & (1 << bit)) != 0;
        if (!up && sym >= SDLK_F1 && sym <= SDLK_F11 && !((sym - SDLK_F1) & 1)
            && SDL_GetModState() != KMOD_NONE)
            shiftedf[col] |= 1 << bit;
        if (shiftedf[col] & (1 << bit)) {
            sym++;                          // F1 -> F2, F3 -> F4 ...
            if (up)
                shiftedf[col] &= ~(1 << bit);
        }
        if (e) {
            e->type = up ? SDL_KEYUP : SDL_KEYDOWN;
            e->key.state = e->type == SDL_KEYDOWN;
            e->key.keysym.scancode = col * 8 + bit;
            e->key.keysym.sym = (SDLKey)sym;
            e->key.keysym.mod = SDL_GetModState();
            e->key.keysym.unicode = 0;
        }
        return 1;
    }
    return 0;
}

int SDL_WaitEvent(SDL_Event *e)
{
    while (!SDL_PollEvent(e))
        ;
    return 1;
}

// From the keys reported so far, so that it agrees with the events.
SDLMod SDL_GetModState(void)
{
    int mod = KMOD_NONE;
    if (!(reported[1] & (1 << 7)))
        mod |= KMOD_LSHIFT;
    if (!(reported[6] & (1 << 4)))
        mod |= KMOD_RSHIFT;
    return (SDLMod)mod;
}

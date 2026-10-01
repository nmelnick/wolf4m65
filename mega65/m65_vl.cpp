// ID_VL for the MEGA65: replaces id_vl.cpp.
//
// Same API and globals, on far surfaces (see SDL.h, m65_surf.c):
//   screen        the displayed VIC-IV framebuffer (tiled, M65_FB_BASE)
//   screenBuffer  the game's draw target (tiled, M65_FB2_BASE); "updating the
//                 screen" copies it to screen with one DMA job
//   latches       linear surfaces in the far heap
// The resolution is fixed at 320x200 (scaleFactor 1). The palette is the
// VIC-IV's; the palette and fade code is the original.

#include <string.h>
#include "../wl_def.h"
#include "m65_surf.h"
#include "m65_video.h"

boolean  fullscreen = true;
boolean  usedoublebuffering = true;
unsigned screenWidth = 320;
unsigned screenHeight = 200;

static SDL_Surface screensurf, buffersurf;

SDL_Surface *screen = NULL;
unsigned screenPitch;

SDL_Surface *screenBuffer = NULL;
unsigned bufferPitch;

SDL_Surface *curSurface = NULL;
unsigned curPitch;

unsigned scaleFactor;

boolean  screenfaded;

// The start of a fade is kept in chip RAM (near memory is short), and read
// back FADECHUNK colours at a time; each step is computed straight into
// curpal (the original had two 1KB palettes for this, palette1 and 2).
SDL_Color curpal[256];
static farptr fadefrom;
#define FADECHUNK 32
unsigned vl_curpalchanges;

#define RGB(r, g, b) {(r)*255/63, (g)*255/63, (b)*255/63, 0}

SDL_Color gamepal[]={
#ifdef SPEAR
    #include "sodpal.inc"
#else
    #include "wolfpal.inc"
#endif
};

//===========================================================================

void VL_Shutdown (void)
{
}

static void InitTiledSurface (SDL_Surface *s, uint32_t base)
{
    s->format = &m65_format8;
    s->w = M65_SCREEN_W;
    s->h = M65_SCREEN_H;
    s->pitch = M65_SCREEN_W;       // (only meaningful for linear surfaces)
    s->farpixels = base;
    s->tiled = 1;
}

void VL_SetVGAPlaneMode (void)
{
    m65_video_init();

    InitTiledSurface(&screensurf, M65_FB_BASE);
    InitTiledSurface(&buffersurf, M65_FB2_BASE);
    screen = &screensurf;
    screenBuffer = &buffersurf;
    SDL_FillRect(screenBuffer, NULL, 0);

    VL_SetPalette(gamepal, true);

    screenPitch = screen->pitch;
    bufferPitch = screenBuffer->pitch;

    curSurface = screenBuffer;
    curPitch = bufferPitch;

    scaleFactor = 1;

    // (in chip RAM: see wl_def.h)
    pixelangle.init(ChipAllocOrQuit(pixelangle.bytes()));
    wallheight.init(ChipAllocOrQuit(wallheight.bytes()));
}

/*
=============================================================================

                        PALETTE OPS

=============================================================================
*/

void VL_ConvertPalette(byte *srcpal, SDL_Color *destpal, int numColors)
{
    for(int i=0; i<numColors; i++)
    {
        destpal[i].r = *srcpal++ * 255 / 63;
        destpal[i].g = *srcpal++ * 255 / 63;
        destpal[i].b = *srcpal++ * 255 / 63;
    }
}

static void PushCurPal (void);

// (Straight into curpal: the original built a 256-colour palette on the
// stack, 1KB, and the C stack here has 1KB in all.)
void VL_FillPalette (int red, int green, int blue)
{
    int i;

    for(i=0; i<256; i++)
    {
        curpal[i].r = red;
        curpal[i].g = green;
        curpal[i].b = blue;
    }
    vl_curpalchanges++;
    PushCurPal();
}

void VL_SetColor (int color, int red, int green, int blue)
{
    SDL_Color col = { (Uint8) red, (Uint8) green, (Uint8) blue, 0 };
    curpal[color] = col;
    vl_curpalchanges++;
    m65_set_color(color, red, green, blue);
}

static void PushCurPal (void)
{
    int i;
    for(i=0; i<256; i++)
        m65_set_color(i, curpal[i].r, curpal[i].g, curpal[i].b);
}

void VL_SetPalette (SDL_Color *palette, bool forceupdate)
{
    (void) forceupdate;
    if (palette != curpal)
    {
        memcpy(curpal, palette, sizeof(SDL_Color) * 256);
        vl_curpalchanges++;
    }
    PushCurPal();
}

// A step's colour between from and to: from + (to - from) * frac / 256
// (frac: step * 256 / steps, one division a step instead of 768).
static inline uint8_t FadeMix (int from, int to, int32_t frac)
{
    return (uint8_t) (from + (int) (((int32_t) (to - from) * frac) >> 8));
}

// A fade's start: the palette now.
static void FadeStart (void)
{
    if (FAR_ISNULL(fadefrom))
        fadefrom = ChipAllocOrQuit(sizeof curpal);
    far_write(fadefrom, curpal, sizeof curpal);
}

// Colours j... of the fade's start (up to FADECHUNK, not past end): how many.
static int FadeFrom (SDL_Color *from, int j, int end)
{
    int n = end - j + 1 < FADECHUNK ? end - j + 1 : FADECHUNK;
    far_read(from, FAR_ADD(fadefrom, (uint16_t) j * sizeof(SDL_Color)),
             n * sizeof(SDL_Color));
    return n;
}

// The steps of a fade from the palette now to `to`: colour j to to[j], or
// with `flat`, every colour to *to. (The final colour is the caller's.)
// Inlined: each fade's loop compiled for its own case is as fast as the two
// loops written out were; the shared one took a frame longer a fade.
__attribute__((always_inline))
static inline void FadeSteps (int start, int end, const SDL_Color *to, bool flat, int steps)
{
    int         i,j,k,n;
    int32_t     frac;
    SDL_Color   from[FADECHUNK];
    const SDL_Color *t;

    VL_WaitVBL(1);
    FadeStart();

//
// fade through intermediate frames
//
    for (i=0;i<steps;i++)
    {
        frac = ((int32_t) i << 8) / steps;
        for (j=start;j<=end;j+=n)
        {
            n = FadeFrom(from, j, end);
            for (k=0;k<n;k++)
            {
                t = flat ? to : &to[j+k];
                curpal[j+k].r = FadeMix(from[k].r, t->r, frac);
                curpal[j+k].g = FadeMix(from[k].g, t->g, frac);
                curpal[j+k].b = FadeMix(from[k].b, t->b, frac);
            }
        }

        VL_WaitVBL(1);
        vl_curpalchanges++;
        PushCurPal();
    }
}

void VL_FadeOut (int start, int end, int red, int green, int blue, int steps)
{
    red = red * 255 / 63;
    green = green * 255 / 63;
    blue = blue * 255 / 63;

    SDL_Color to = { (Uint8) red, (Uint8) green, (Uint8) blue, 0 };
    FadeSteps(start, end, &to, true, steps);

//
// final color
//
    VL_FillPalette (red,green,blue);

    screenfaded = true;
}

void VL_FadeIn (int start, int end, SDL_Color *palette, int steps)
{
    FadeSteps(start, end, palette, false, steps);

//
// final color
//
    VL_SetPalette (palette, true);
    screenfaded = false;
}

/*
=============================================================================

                            PIXEL OPS

=============================================================================
*/

void VL_Plot (int x, int y, int color)
{
    assert(x >= 0 && (unsigned) x < screenWidth
            && y >= 0 && (unsigned) y < screenHeight
            && "VL_Plot: Pixel out of bounds!");

    surf_plot(curSurface, x, y, color);
}

byte VL_GetPixel (int x, int y)
{
    assert(x >= 0 && (unsigned) x < screenWidth
            && y >= 0 && (unsigned) y < screenHeight
            && "VL_GetPixel: Pixel out of bounds!");

    return surf_get(curSurface, x, y);
}

void VL_Hlin (unsigned x, unsigned y, unsigned width, int color)
{
    assert(x + width <= screenWidth && y < screenHeight
            && "VL_Hlin: Destination rectangle out of bounds!");

    surf_fill_row(curSurface, x, y, width, color);
}

void VL_Vlin (int x, int y, int height, int color)
{
    assert(x >= 0 && (unsigned) x < screenWidth
            && y >= 0 && (unsigned) y + height <= screenHeight
            && "VL_Vlin: Destination rectangle out of bounds!");

    while (height--)
        surf_plot(curSurface, x, y++, color);
}

void VL_BarScaledCoord (int scx, int scy, int scwidth, int scheight, int color)
{
    assert(scx >= 0 && (unsigned) scx + scwidth <= screenWidth
            && scy >= 0 && (unsigned) scy + scheight <= screenHeight
            && "VL_BarScaledCoord: Destination rectangle out of bounds!");

    surf_fill_rect(curSurface, scx, scy, scwidth, scheight, color);
}

/*
============================================================================

                            MEMORY OPS

============================================================================
*/

//
// One row of a part of a planar ("munged") picture: the picture is
// origwidth x origheight, stored as 4 planes of (origwidth/4) x origheight,
// plane p holding the pixels whose x & 3 == p. Offsets are 32-bit: a
// full-screen picture's planes are 16000 bytes apart, which overflows a
// 16-bit int by the fourth plane. Each plane's part of the row goes to
// every fourth byte of out by one DMA job (destination skip 4).
//
static void PlanarRow (farptr source, int origwidth, int origheight,
                       int srcx, int row, int width, byte *out)
{
    uint32_t planesize = (uint32_t) (origwidth >> 2) * origheight;
    uint32_t rowstart = source.a + (uint32_t) row * (origwidth >> 2);
    int p, x0;

    for (p = 0; p < 4; p++, rowstart += planesize)
    {
        x0 = srcx + ((p - srcx) & 3);           // the row's first x in plane p
        if (x0 >= srcx + width)
            continue;
        m65_dma_scale ((uint32_t) (uintptr_t) (out + (x0 - srcx)), rowstart + (x0 >> 2),
                       (uint16_t) ((srcx + width - 1 - x0) >> 2) + 1, 0x100, 4);
    }
}

void VL_MemToLatch (farptr source, int width, int height,
    SDL_Surface *destSurface, int x, int y)
{
    static byte row[M65_SCREEN_W];
    int j;

    assert(x >= 0 && x + width <= destSurface->w
            && y >= 0 && y + height <= destSurface->h
            && "VL_MemToLatch: Destination rectangle out of bounds!");

    for (j = 0; j < height; j++)
    {
        PlanarRow(source, width, height, 0, j, width, row);
        surf_write_row(destSurface, x, y + j, row, width);
    }
}

void VL_MemToScreenScaledCoord (farptr source, int origwidth, int origheight, int srcx, int srcy,
                                int destx, int desty, int width, int height)
{
    static byte row[M65_SCREEN_W];
    int j;

    assert(destx >= 0 && destx + width <= (int) screenWidth
            && desty >= 0 && desty + height <= (int) screenHeight
            && "VL_MemToScreenScaledCoord: Destination rectangle out of bounds!");

    for (j = 0; j < height; j++)
    {
        PlanarRow(source, origwidth, origheight, srcx, srcy + j, width, row);
        surf_write_row(curSurface, destx, desty + j, row, width);
    }
}

void VL_MemToScreenScaledCoord (farptr source, int width, int height, int destx, int desty)
{
    VL_MemToScreenScaledCoord(source, width, height, 0, 0, destx, desty, width, height);
}

void VL_FarPlanarToScreen (farptr pic)
{
    VL_MemToScreenScaledCoord(pic, M65_SCREEN_W, M65_SCREEN_H, 0, 0);
}

void VL_FarLinearToScreen (farptr pic)
{
    SDL_Surface src;

    memset(&src, 0, sizeof src);
    src.format = &m65_format8;
    src.w = M65_SCREEN_W;
    src.h = M65_SCREEN_H;
    src.pitch = M65_SCREEN_W;
    src.farpixels = pic.a;
    SDL_BlitSurface(&src, NULL, curSurface, NULL);
}

void VL_LatchToScreenScaledCoord (SDL_Surface *source, int xsrc, int ysrc,
    int width, int height, int scxdest, int scydest)
{
    assert(scxdest >= 0 && scxdest + width <= (int) screenWidth
            && scydest >= 0 && scydest + height <= (int) screenHeight
            && "VL_LatchToScreenScaledCoord: Destination rectangle out of bounds!");

    SDL_Rect srcrect = { (Sint16) xsrc, (Sint16) ysrc, (Uint16) width, (Uint16) height };
    SDL_Rect destrect = { (Sint16) scxdest, (Sint16) scydest, 0, 0 };
    SDL_BlitSurface(source, &srcrect, curSurface, &destrect);
}

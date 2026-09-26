// Title test: the real id_ca, id_vh and the MEGA65 video layer (m65_vl.cpp)
// show the title screen the way the game does, then draw text and a picture
// over it. Checks the displayed pixels against the original decoder
// (build/huffref.h) and SDL_GetTicks against the video frame rate; leaves
// the screen up for a screenshot.
#include <mega65.h>
#include <unistd.h>
#include "../wl_def.h"
#include "m65_debug.h"
#include "m65_surf.h"
#include "m65_video.h"
#include "m65_posix.h"
#include "m65_fartext.hpp"

#include "build/huffref.h"

// --- what the tested code needs from the rest of the game ---------------------
boolean       param_ignorenumchunks = false;
SDMode        SoundMode;
short        *pixelangle;
int          *wallheight;
extern int    numEpisodesMissing;
extern SDL_Color curpal[256];
void CAL_SetupGrFile (void);

void Quit (const char *error, ...)
{
    m65_debug_puts("QUIT:");
    m65_debug_puts(error ? error : "(null)");
    m65_debug_puts("TEST-DONE");
    for (;;) {}
}

void IN_StartAck (void) {}
boolean IN_CheckAck (void) { return false; }
void IN_ProcessEvents (void) {}

// --- test ----------------------------------------------------------------------
#define REPORT 0x5FC00UL        // after the second framebuffer

struct report {
    uint8_t  magic;
    uint8_t  title_ok;
    uint16_t sa, sb;            // checksum of the displayed title
    uint32_t ms_per_50_frames;  // SDL_GetTicks over 50 PAL frames (want ~1000)
    uint32_t fade_ms;           // how long the 30-step fade-in took
    uint16_t signon_sa, signon_sb;  // checksum of the displayed sign-on screen
    uint8_t  fartext_ok;        // FarText walk of the help article matches
    uint8_t  palette_ok;        // after the fade: curpal and hardware == gamepal
};

static struct report rep;

// Physical raster line from the VIC-IV's full-resolution counter.
static uint16_t fn_raster (void)
{
    uint8_t lo, hi;
    do {
        hi = VICIV.fn_raster_msb;
        lo = VICIV.fn_raster_lsb;
    } while (hi != VICIV.fn_raster_msb);
    return (uint16_t) (hi & 0x07) << 8 | lo;
}

// Wait for the next frame: the raster counter wraps back to the top.
static void wait_frame (void)
{
    uint16_t prev = fn_raster(), cur;
    for (;;) {
        cur = fn_raster();
        if (cur < prev)
            return;
        prev = cur;
    }
}

int main (void)
{
    static byte row[320];
    uint16_t a = 0, b = 0;
    uint32_t t0;
    int x, y, i;

    __set_heap_limit(__get_heap_max_safe_size());
    SDL_Init(SDL_INIT_VIDEO);

    strcpy(extension, "wl1");
    strcpy(graphext, "wl1");
    strcpy(audioext, "wl1");
    numEpisodesMissing = 5;

    // Graphics only: CA_Startup would also allocate the two 8KB map planes,
    // which do not fit next to this test program (the memory budget is still
    // open), and this test does not need them.
    CAL_SetupGrFile();
    VL_SetVGAPlaneMode();

    // Sign-on screen, as SignonScreen does it on the MEGA65.
    {
        farptr pic = m65_file_far("signon.bin", NULL);
        if (FAR_ISNULL(pic))
            Quit("no signon.bin");
        VL_FarLinearToScreen(pic);
        VW_UpdateScreen();
        a = b = 0;
        for (y = 0; y < 200; y++) {
            surf_read_row(screen, 0, y, row, 320);
            for (x = 0; x < 320; x++) { a += row[x]; b += a; }
        }
        rep.signon_sa = a; rep.signon_sb = b;
        a = b = 0;
    }

    // FarText: walk the help article like a char pointer.
    {
        FarText t, u;
        int32_t k, len = ref[T_HELPART].explen;
        uint16_t ta = 0, tb = 0;
        char c9;
        CA_CacheGrChunk(T_HELPART);
        t = grsegs[T_HELPART];
        c9 = *(t + 9);
        for (k = 0; k < len; k++) { ta += (uint8_t) *t++; tb += ta; }
        u = grsegs[T_HELPART];
        u = u + 10;
        --u;
        rep.fartext_ok = ta == ref[T_HELPART].sa && tb == ref[T_HELPART].sb
                      && *u == c9 && u < t && u != t;
    }

    // As in the game: black, draw the title, show it, fade in.
    VL_FillPalette(0, 0, 0);
    CA_CacheScreen(TITLEPIC);
    VW_UpdateScreen();
    t0 = SDL_GetTicks();
    VL_FadeIn(0, 255, gamepal, 30);
    rep.fade_ms = SDL_GetTicks() - t0;

    // The fade must end exactly on gamepal, in curpal and in the VIC-IV's
    // palette registers (which hold each channel nybble-swapped).
    rep.palette_ok = 1;
    for (i = 0; i < 256; i++) {
        uint8_t r = PALETTE.red[i], g = PALETTE.green[i], bl = PALETTE.blue[i];
        r = (uint8_t) (r << 4 | r >> 4); g = (uint8_t) (g << 4 | g >> 4);
        bl = (uint8_t) (bl << 4 | bl >> 4);
        if (curpal[i].r != gamepal[i].r || curpal[i].g != gamepal[i].g ||
            curpal[i].b != gamepal[i].b || r != gamepal[i].r ||
            g != gamepal[i].g || bl != gamepal[i].b)
            rep.palette_ok = 0;
    }

    // The displayed pixels, in screen order.
    for (y = 0; y < 200; y++) {
        surf_read_row(screen, 0, y, row, 320);
        for (x = 0; x < 320; x++) { a += row[x]; b += a; }
    }
    rep.sa = a; rep.sb = b;
    rep.title_ok = a == REF_TITLE_SA && b == REF_TITLE_SB;

    // Timer: milliseconds over 50 frames (PAL: 1 second).
    wait_frame();
    t0 = SDL_GetTicks();
    for (i = 0; i < 50; i++)
        wait_frame();
    rep.ms_per_50_frames = SDL_GetTicks() - t0;

    // Text in both fonts, and a picture, over the title (visual check).
    CA_CacheGrChunk(STARTFONT);         // as InitGame does
    CA_CacheGrChunk(STARTFONT+1);
    VWB_Bar(8, 150, 304, 44, 0x2d);
    fontnumber = 0; fontcolor = 15;
    px = 12; py = 152;
    VWB_DrawPropString("Wolf4SDL on the MEGA65 - font 0");
    fontnumber = 1; fontcolor = 14;
    px = 12; py = 164;
    VWB_DrawPropString("Font 1: 0123456789");
    CA_CacheGrChunk(C_MOUSELBACKPIC);
    VWB_DrawPic((320 - pictable[C_MOUSELBACKPIC - STARTPICS].width) & ~7, 8,
                C_MOUSELBACKPIC);        // top right
    VW_UpdateScreen();

    rep.magic = 0xEE;
    m65_dma_copy(REPORT, (uint32_t)(uintptr_t)&rep, sizeof rep);
    m65_debug_puts("TEST-DONE");
    for (;;) {}
}

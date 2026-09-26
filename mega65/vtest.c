// Video layer test: palette + geometry pattern.
#include <stdint.h>
#include "m65_video.h"

static uint8_t line[M65_SCREEN_W];

int main(void)
{
    unsigned x, y, i;

    m65_video_init();

    // Palette: 0 black, 254 white, 1..253 a hue ramp. (Index 255 renders
    // black in full-colour mode, so the frame deliberately uses 254.)
    for (i = 0; i < 256; i++) {
        uint8_t r = (uint8_t)i, g = (uint8_t)(255 - i), b = (uint8_t)((i * 3) & 255);
        m65_set_color((uint8_t)i, r, g, b);
    }
    m65_set_color(0, 0, 0, 0);
    m65_set_color(254, 255, 255, 255);

    for (y = 0; y < M65_SCREEN_H; y++) {
        for (x = 0; x < M65_SCREEN_W; x++) {
            uint8_t c;
            if (x == 0 || y == 0 || x == M65_SCREEN_W - 1 || y == M65_SCREEN_H - 1)
                c = 254;                                  // 1px frame
            else if (y < 100)
                c = (uint8_t)(x * 254 / M65_SCREEN_W + 1); // gradient
            else
                c = (uint8_t)(((x >> 3) + (y >> 3)) & 1) ? 100 : 200; // 8px checks
            line[x] = c;
        }
        m65_put_scanline(0, y, line, M65_SCREEN_W);
    }

    for (;;) {}
    return 0;
}

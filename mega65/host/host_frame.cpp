// Host reference: FrameDumpHook (called by ThreeDRefresh for every demo
// frame, see wl_draw.cpp) saves the frames listed in $FRAMES ("10,50,200")
// as frame_NNNN.raw (320x200, one palette index per pixel, row-major) and
// exits after the last one.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../wl_def.h"

extern "C" void FrameDumpHook (void)
{
    static int frame, last = -1;
    static char want[10000];

    if (last < 0) {
        const char *list = getenv("FRAMES");
        last = 0;
        for (const char *p = list ? list : ""; *p; ) {
            int n = atoi(p);
            if (n > 0 && n < (int) sizeof want) {
                want[n] = 1;
                if (n > last) last = n;
            }
            p = strchr(p, ',');
            if (!p) break;
            p++;
        }
    }

    frame++;
    if (frame < (int) sizeof want && want[frame]) {
        char name[32];
        snprintf(name, sizeof name, "frame_%04d.raw", frame);
        FILE *f = fopen(name, "wb");
        for (int y = 0; y < screenHeight; y++)
            fwrite((byte *) screenBuffer->pixels + y * screenBuffer->pitch, 1, screenWidth, f);
        fclose(f);
        printf("saved %s\n", name);
    }
    if (frame >= last) {
        printf("FRAMES-DONE\n");
        exit(0);
    }
}

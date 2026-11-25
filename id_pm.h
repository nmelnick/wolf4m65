#ifndef __ID_PM__
#define __ID_PM__

#ifdef USE_HIRES
#define PMPageSize 16384
#else
#define PMPageSize 4096
#endif

extern int ChunksInFile;
extern int PMSpriteStart;
extern int PMSoundStart;

extern bool PMSoundInfoPagePadded;

void PM_Startup();
void PM_Shutdown();

#ifdef MEGA65

// VSWAP stays whole in attic RAM, as loaded: pages are far addresses into it,
// found through the page offsets in its header (no near page table).

farptr   PM_GetPage (int page);
uint32_t PM_GetPageSize (int page);

// A wall (or door) texture: from the texture cache in colour RAM when it
// is there or can be put there (id_pm.cpp), else in place in attic RAM.
farptr   PM_GetTexture (int wallpic);
void     PM_NextFrame ();          // (the cache's clock: once per frame drawn)

// A sprite: from the sprite cache in colour RAM when it is there or can be
// put there (id_pm.cpp), else in place in attic RAM.
farptr   PM_GetSprite (int shapenum);

static inline farptr PM_GetSound(int soundpagenum)
{
    return PM_GetPage(PMSoundStart + soundpagenum);
}

#else

// ChunksInFile+1 pointers to page starts.
// The last pointer points one byte after the last page.
extern uint8_t **PMPages;

static inline uint32_t PM_GetPageSize(int page)
{
    if(page < 0 || page >= ChunksInFile)
        Quit("PM_GetPageSize: Tried to access illegal page: %i", page);
    return (uint32_t) (PMPages[page + 1] - PMPages[page]);
}

static inline uint8_t *PM_GetPage(int page)
{
    if(page < 0 || page >= ChunksInFile)
        Quit("PM_GetPage: Tried to access illegal page: %i", page);
    return PMPages[page];
}

static inline uint8_t *PM_GetEnd()
{
    return PMPages[ChunksInFile];
}

static inline byte *PM_GetTexture(int wallpic)
{
    return PM_GetPage(wallpic);
}

static inline uint16_t *PM_GetSprite(int shapenum)
{
    // correct alignment is enforced by PM_Startup()
    return (uint16_t *) (void *) PM_GetPage(PMSpriteStart + shapenum);
}

static inline byte *PM_GetSound(int soundpagenum)
{
    return PM_GetPage(PMSoundStart + soundpagenum);
}

#endif

#endif

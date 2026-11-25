#include "wl_def.h"
#ifdef MEGA65
#include "m65_posix.h"
#include "m65_video.h"
#endif

int ChunksInFile;
int PMSpriteStart;
int PMSoundStart;

bool PMSoundInfoPagePadded = false;

#ifdef MEGA65

//
// The VSWAP header: word ChunksInFile, word PMSpriteStart, word PMSoundStart,
// then ChunksInFile page offsets (uint32) and ChunksInFile page lengths
// (word). The file stays in attic RAM as loaded; pages are addressed in place
// (the original repacks them to 2-byte-align sprites, which the 6502 does not
// need).
//
static farptr   vswap;
static uint32_t vswapsize;
// The far address of each wall and sprite page (the renderer asks for one
// per wall and per sprite drawn), in chip RAM: the page offsets are in the
// file's header, in attic RAM, which is slow to read on the machine.
static farptr   pagetab;
static farptr   centof;         // the page cache (below): per page its entry plus one, 0: none

static uint32_t PageOffset (int page)
{
    return far_peekl(FAR_ADD(vswap, 6 + 4 * (uint32_t) page));
}

void PM_Startup()
{
    char fname[13] = "vswap.";
    strcat(fname,extension);

    vswap = m65_file_far(fname, &vswapsize);
    if(FAR_ISNULL(vswap))
        CA_CannotOpen(fname);

    ChunksInFile = far_peekw(vswap);
    PMSpriteStart = far_peekw(FAR_ADD(vswap, 2));
    PMSoundStart = far_peekw(FAR_ADD(vswap, 4));

    for(int i = 0; i < ChunksInFile; i++)
    {
        uint32_t offs = PageOffset(i);
        if(offs && (offs < PageOffset(0) || offs >= vswapsize))
            Quit("Illegal page offset for page %i: %lu (filesize: %lu)",
                    i, (unsigned long) offs, (unsigned long) vswapsize);
    }

    // (A whole number of 256-byte pages: the ray caster needs tilemap and
    // spotvis, allocated later, 256-byte aligned.)
    pagetab = far_alloc_chip((PMSoundStart * 4 + 255) & ~255);
    // (the page cache's page -> entry bytes: 256-byte pages too, for the same)
    centof = far_alloc_chip((PMSoundStart + 255) & ~255);
    if(!FAR_ISNULL(centof))
        m65_dma_fill(centof.a, 0, (PMSoundStart + 255) & ~255);
    for(int i = 0; i < PMSoundStart && !FAR_ISNULL(pagetab); i++)
    {
        uint32_t offs = PageOffset(i);
        farptr page = offs ? FAR_ADD(vswap, offs) : FAR_ADD(vswap, vswapsize);
        far_write(FAR_ADD(pagetab, 4 * i), &page.a, 4);
    }
}

void PM_Shutdown()
{
}

static void CheckPage (int page)
{
    if(page < 0 || page >= ChunksInFile)
        Quit("PM_GetPage: Tried to access illegal page: %i", page);
}

//
// The page cache: attic RAM is slow for the DMA controller (a frame of wall
// columns scaled from textures there took 17.5ms on the machine, 6.3ms from
// colour RAM, 4.5ms from chip RAM), so the wall textures (4KB) and sprites
// (1.2KB on average) in use are copied to colour RAM: the 30KB above the
// 2000 bytes the screen's cells use, in units of 256 bytes, a page in a run
// of them (first fit). To make room the least recently used pages go, but
// never one used in the frame being drawn or the one before: when a view
// needs more than there is, the pages in use stay and the rest are read
// from attic RAM, rather than copied in and out every frame. Pages never
// change, so the cache stays good across levels. (In the demo a frame uses
// 4.7 wall pages and 4.8 sprite pages on average: ~25KB.) Which slot holds
// a page: a byte per page in chip RAM (near memory is short).
//
#define CACHE_BASE    0xFF80800UL
#define CACHE_UNITS   120               // (30KB: to $FF87FFF, colour RAM's end)
#define CACHE_ENTRIES 24

static struct
{
    int16_t  page;
    uint8_t  start, units;
    uint16_t used;              // the frame it was last used in
} cent[CACHE_ENTRIES];
static uint8_t  ncent;
static uint8_t  cunits[(CACHE_UNITS + 7) / 8];  // the units in use, a bit each
static uint16_t cacheframe = 1;
static uint16_t fullframe;      // the frame a page of fullunits found no room
static uint8_t  fullunits;

void PM_NextFrame ()
{
    cacheframe++;
}

static void UnitsMark (uint8_t start, uint8_t n, uint8_t used)
{
    for(; n; n--, start++)
    {
        if(used) cunits[start >> 3] |= 1 << (start & 7);
        else     cunits[start >> 3] &= ~(1 << (start & 7));
    }
}

// The first run of n free units: its start, or -1.
static int UnitsFind (uint8_t n)
{
    uint8_t u, run = 0;
    for(u = 0; u < CACHE_UNITS; u++)
    {
        if(cunits[u >> 3] & (1 << (u & 7))) run = 0;
        else if(++run == n) return u - n + 1;
    }
    return -1;
}

// Drop entry e (moving the last into its place).
static void EntryDrop (uint8_t e)
{
    UnitsMark(cent[e].start, cent[e].units, 0);
    far_poke(FAR_ADD(centof, cent[e].page), 0);
    if(e != --ncent)
    {
        cent[e] = cent[ncent];
        far_poke(FAR_ADD(centof, cent[e].page), e + 1);
    }
}

// A page not in the cache: put it there if it fits, else in place.
__attribute__((noinline)) static farptr CachedPageMiss (int page)
{
    farptr r;
    uint32_t size;
    uint8_t e, n, victim;
    int start;

    r = PM_GetPage(page);
    size = PM_GetPageSize(page);
    if(FAR_ISNULL(centof) || !size || size > PMPageSize)
        return r;
    n = (uint8_t) ((size + 255) >> 8);
    if(fullframe == cacheframe && n >= fullunits)
        return r;                       // (no room for that much this frame: in place)
    while((start = UnitsFind(n)) < 0 || ncent == CACHE_ENTRIES)
    {
        victim = 0xFF;                  // the least recently used, not this frame's or the last's
        for(e = 0; e < ncent; e++)
            if((uint16_t) (cacheframe - cent[e].used) >= 2 && (victim == 0xFF
                    || (uint16_t) (cacheframe - cent[e].used) > (uint16_t) (cacheframe - cent[victim].used)))
                victim = e;
        if(victim == 0xFF)
        {
            fullframe = cacheframe;     // (all in use: in place, and so for the
            fullunits = n;              //  rest of the frame at this size or more)
            return r;
        }
        EntryDrop(victim);
    }
    cent[ncent].page = page;
    cent[ncent].start = (uint8_t) start;
    cent[ncent].units = n;
    cent[ncent].used = cacheframe;
    UnitsMark((uint8_t) start, n, 1);
    far_poke(FAR_ADD(centof, page), ++ncent);
    m65_dma_copy(CACHE_BASE + ((uint32_t) start << 8), r.a, (uint16_t) size);
    r.a = CACHE_BASE + ((uint32_t) start << 8);
    return r;
}

// A page, from the cache: the lookup inline (the renderer's per-wall path;
// the miss is out of line).
__attribute__((always_inline)) static inline farptr CachedPage (int page)
{
    farptr r;
    uint8_t e;

#ifdef M65_NOCACHE                      // (make CACHE=0: none, to compare)
    return PM_GetPage(page);
#endif
    if(!FAR_ISNULL(centof) && (e = far_peek(FAR_ADD(centof, page))) != 0)
    {
        cent[e - 1].used = cacheframe;
        r.a = CACHE_BASE + ((uint32_t) cent[e - 1].start << 8);
        return r;
    }
    return CachedPageMiss(page);
}

farptr PM_GetTexture (int page)
{
    return CachedPage(page);
}

farptr PM_GetSprite (int shapenum)
{
#ifdef M65_NOSPRITECACHE                // (make SPRITECACHE=0: walls only, to compare)
    return PM_GetPage(PMSpriteStart + shapenum);
#else
    return CachedPage(PMSpriteStart + shapenum);
#endif
}

farptr PM_GetPage (int page)
{
    if((unsigned) page < (unsigned) PMSoundStart && !FAR_ISNULL(pagetab))
    {
        farptr r;
        r.a = far_peekl(FAR_ADD(pagetab, 4 * page));
        return r;
    }
    CheckPage(page);
    uint32_t offs = PageOffset(page);
    if(!offs)                               // sparse page: no data
        return FAR_ADD(vswap, vswapsize);
    return FAR_ADD(vswap, offs);
}

uint32_t PM_GetPageSize (int page)
{
    CheckPage(page);
    uint32_t offs = PageOffset(page);
    if(!offs)
        return 0;                           // sparse page
    // As in the original: the next page's offset, or this page's length
    // entry when the next page is sparse (or this is the last page).
    uint32_t next = page + 1 < ChunksInFile ? PageOffset(page + 1) : vswapsize;
    if(!next)
        return far_peekw(FAR_ADD(vswap, 6 + 4 * (uint32_t) ChunksInFile + 2 * (uint32_t) page));
    return next - offs;
}

#else

// holds the whole VSWAP
uint32_t *PMPageData;
size_t PMPageDataSize;

// ChunksInFile+1 pointers to page starts.
// The last pointer points one byte after the last page.
uint8_t **PMPages;

void PM_Startup()
{
    char fname[13] = "vswap.";
    strcat(fname,extension);

    FILE *file = fopen(fname,"rb");
    if(!file)
        CA_CannotOpen(fname);

    ChunksInFile = 0;
    fread(&ChunksInFile, sizeof(word), 1, file);
    PMSpriteStart = 0;
    fread(&PMSpriteStart, sizeof(word), 1, file);
    PMSoundStart = 0;
    fread(&PMSoundStart, sizeof(word), 1, file);

    uint32_t* pageOffsets = (uint32_t *) malloc((ChunksInFile + 1) * sizeof(int32_t));
    CHECKMALLOCRESULT(pageOffsets);
    fread(pageOffsets, sizeof(uint32_t), ChunksInFile, file);

    word *pageLengths = (word *) malloc(ChunksInFile * sizeof(word));
    CHECKMALLOCRESULT(pageLengths);
    fread(pageLengths, sizeof(word), ChunksInFile, file);

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    long pageDataSize = fileSize - pageOffsets[0];
    if(pageDataSize > (size_t) -1)
        Quit("The page file \"%s\" is too large!", fname);

    pageOffsets[ChunksInFile] = fileSize;

    uint32_t dataStart = pageOffsets[0];
    int i;

    // Check that all pageOffsets are valid
    for(i = 0; i < ChunksInFile; i++)
    {
        if(!pageOffsets[i]) continue;   // sparse page
        if(pageOffsets[i] < dataStart || pageOffsets[i] >= (size_t) fileSize)
            Quit("Illegal page offset for page %i: %u (filesize: %u)",
                    i, pageOffsets[i], fileSize);
    }

    // Calculate total amount of padding needed for sprites and sound info page
    int alignPadding = 0;
    for(i = PMSpriteStart; i < PMSoundStart; i++)
    {
        if(!pageOffsets[i]) continue;   // sparse page
        uint32_t offs = pageOffsets[i] - dataStart + alignPadding;
        if(offs & 1)
            alignPadding++;
    }

    if((pageOffsets[ChunksInFile - 1] - dataStart + alignPadding) & 1)
        alignPadding++;

    PMPageDataSize = (size_t) pageDataSize + alignPadding;
    PMPageData = (uint32_t *) malloc(PMPageDataSize);
    CHECKMALLOCRESULT(PMPageData);

    PMPages = (uint8_t **) malloc((ChunksInFile + 1) * sizeof(uint8_t *));
    CHECKMALLOCRESULT(PMPages);

    // Load pages and initialize PMPages pointers
    uint8_t *ptr = (uint8_t *) PMPageData;
    for(i = 0; i < ChunksInFile; i++)
    {
        if(i >= PMSpriteStart && i < PMSoundStart || i == ChunksInFile - 1)
        {
            size_t offs = ptr - (uint8_t *) PMPageData;

            // pad with zeros to make it 2-byte aligned
            if(offs & 1)
            {
                *ptr++ = 0;
                if(i == ChunksInFile - 1) PMSoundInfoPagePadded = true;
            }
        }

        PMPages[i] = ptr;

        if(!pageOffsets[i])
            continue;               // sparse page

        // Use specified page length, when next page is sparse page.
        // Otherwise, calculate size from the offset difference between this and the next page.
        uint32_t size;
        if(!pageOffsets[i + 1]) size = pageLengths[i];
        else size = pageOffsets[i + 1] - pageOffsets[i];

        fseek(file, pageOffsets[i], SEEK_SET);
        fread(ptr, 1, size, file);
        ptr += size;
    }

    // last page points after page buffer
    PMPages[ChunksInFile] = ptr;

    free(pageLengths);
    free(pageOffsets);
    fclose(file);
}

void PM_Shutdown()
{
    free(PMPages);
    free(PMPageData);
}

#endif // MEGA65

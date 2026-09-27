#include "wl_def.h"
#ifdef MEGA65
#include "m65_posix.h"
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

farptr PM_GetEnd ()
{
    return FAR_ADD(vswap, vswapsize);
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

#ifndef __ID_CA__
#define __ID_CA__

//===========================================================================

#define NUMMAPS         60
#ifdef USE_FLOORCEILINGTEX
    #define MAPPLANES       3
#else
    #define MAPPLANES       2
#endif

#ifdef MEGA65
// Graphics and audio chunks live in far memory (attic RAM) and stay cached
// once loaded: all of them fit, so uncaching is a no-op.
#include "m65_far.h"
// The map planes are in far memory too; mapptr behaves like word * (see
// m65_farref.hpp), so MAPSPOT and code walking the planes are unchanged.
#include "m65_farref.hpp"
#include "m65_fararray.hpp"
typedef FarPtr<uint16_t> mapptr;
#define UNCACHEGRCHUNK(chunk)
#define UNCACHEAUDIOCHUNK(chunk)
#else
typedef word *mapptr;
#define UNCACHEGRCHUNK(chunk) {if(grsegs[chunk]) {free(grsegs[chunk]); grsegs[chunk]=NULL;}}
#define UNCACHEAUDIOCHUNK(chunk) {if(audiosegs[chunk]) {free(audiosegs[chunk]); audiosegs[chunk]=NULL;}}
#endif

//===========================================================================

typedef struct
{
    int32_t planestart[3];
    word    planelength[3];
    word    width,height;
    char    name[16];
} maptype;

//===========================================================================

extern  int   mapon;

extern  mapptr mapsegs[MAPPLANES];
#ifdef MEGA65
// A raw audio chunk, in place in AUDIOT (replaces audiosegs[]).
farptr CA_AudioChunk (int chunk);
extern  FarArray<farptr, NUMCHUNKS> grsegs; // (behaves like farptr[NUMCHUNKS])
#else
extern  byte *audiosegs[NUMSNDCHUNKS];
extern  byte *grsegs[NUMCHUNKS];
#endif

extern  char  extension[5];
extern  char  graphext[5];
extern  char  audioext[5];

//===========================================================================

boolean CA_LoadFile (const char *filename, memptr *ptr);
boolean CA_WriteFile (const char *filename, void *ptr, int32_t length);

int32_t CA_RLEWCompress (word *source, int32_t length, word *dest, word rlewtag);

void CA_RLEWexpand (word *source, word *dest, int32_t length, word rlewtag);

void CA_Startup (void);
void CA_Shutdown (void);

int32_t CA_CacheAudioChunk (int chunk);
void CA_LoadAllSounds (void);

void CA_CacheGrChunk (int chunk);
void CA_CacheMap (int mapnum);

void CA_CacheScreen (int chunk);

void CA_CannotOpen(const char *name);

#endif

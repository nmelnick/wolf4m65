// ID_SD for the MEGA65: replaces id_sd.cpp (SDL_mixer and an OPL emulator).
//
// Music: the songs are Bobby Prince's MIDI originals converted for three
// SIDs (tools/midi2sid.py --game), in MUSIC.DAT, which the user builds from
// their own MIDI files. The file is loaded whole into attic RAM, and the
// interrupt handler in m65_music.s plays it at 200 ticks per second from
// CIA 1 timer A. Without MUSIC.DAT there is no music: SD_Startup reports no
// AdLib, and the game turns its music options off itself.
//
// Sound effects are silent for now (every play request is ignored); the
// plan is the AdLib effects on the fourth SID and the digitized ones on the
// DMA audio channels. Audio chunks are already in far memory
// (CA_AudioChunk).

#include "../wl_def.h"
#include "SDL_mixer.h"      // MIX_CHANNELS
#include "m65_far.h"
#include "m65_posix.h"

globalsoundpos channelSoundPos[MIX_CHANNELS];

boolean     AdLibPresent,
            SoundBlasterPresent,
            SoundPositioned;
SDMode      SoundMode;
SMMode      MusicMode;
SDSMode     DigiMode;
int         DigiMap[LASTSOUND];
int         DigiChannel[STARTMUSIC - STARTDIGISOUNDS];

// The player (m65_music.s).
extern "C" {
    extern uint32_t m65_mus_p, m65_mus_start;
    extern uint16_t m65_mus_wait;
    extern uint8_t m65_mus_on;
    extern char m65_music_irq[];
}

#define SID_BASE   0xD400               // three SIDs, 0x20 apart
#define SID(reg)   (*(volatile uint8_t *) (SID_BASE + (reg)))
#define SIDMODE    (*(volatile uint8_t *) 0xD63C)  // bits 0-3: 8580 (1) or 6581 (0)
#define CIA1_TALO  (*(volatile uint8_t *) 0xDC04)
#define CIA1_TAHI  (*(volatile uint8_t *) 0xDC05)
#define CIA1_ICR   (*(volatile uint8_t *) 0xDC0D)
#define CIA1_CRA   (*(volatile uint8_t *) 0xDC0E)
#define CIA_HZ     1000000UL            // (the CIAs' clock; see m65_sdl.c)

static farptr   musicfile;              // MUSIC.DAT in attic RAM
static uint8_t  numsongs;

// Gates and waveforms off (the voices fall silent), full volume, no filter.
static void SID_Silence (void)
{
    for (uint8_t sid = 0; sid < 3; sid++)
    {
        for (uint8_t v = 0; v < 3; v++)
            SID(sid * 0x20 + v * 7 + 4) = 0;
        SID(sid * 0x20 + 0x17) = 0;
        SID(sid * 0x20 + 0x18) = 0x0F;
    }
}

void SD_Startup (void)
{
    int i;
    uint32_t size;

    SoundBlasterPresent = false;
    SoundMode = sdm_Off;
    MusicMode = smm_Off;
    DigiMode = sds_Off;
    for (i = 0; i < LASTSOUND; i++)
        DigiMap[i] = -1;
    for (i = 0; i < STARTMUSIC - STARTDIGISOUNDS; i++)
        DigiChannel[i] = -1;

    // MUSIC.DAT: "WMUS", version 1, song count, ticks per second, offsets.
    musicfile = m65_file_far("music.dat", &size);
    AdLibPresent = !FAR_ISNULL(musicfile) && size >= 8
        && far_peekl(musicfile) == 0x53554D57UL          // "WMUS"
        && far_peek(FAR_ADD(musicfile, 4)) == 1;
    if (!AdLibPresent)
        return;
    numsongs = far_peek(FAR_ADD(musicfile, 5));

    SIDMODE = (SIDMODE & 0x10) | 0x0F;  // 8580 sound (as the previews)
    for (i = 0; i < 0x60; i++)
        SID(i) = 0;
    SID_Silence();

    // The player's interrupt: CIA 1 timer A at the song rate.
    uint16_t latch = (uint16_t) (CIA_HZ / far_peekw(FAR_ADD(musicfile, 6)) - 1);
    __asm__ volatile ("sei");
    m65_mus_on = 0;
    *(volatile uint16_t *) 0xFFFE = (uint16_t) (uintptr_t) m65_music_irq;
    *(volatile uint8_t *) 0xD01A = 0;   // no VIC interrupts
    *(volatile uint8_t *) 0xD019 = 0xFF;
    CIA1_ICR = 0x7F;
    CIA1_TALO = (uint8_t) latch;
    CIA1_TAHI = (uint8_t) (latch >> 8);
    CIA1_CRA = 0x11;                    // load, start, continuous
    (void) CIA1_ICR;
    CIA1_ICR = 0x81;                    // timer A interrupts on
    __asm__ volatile ("cli");
}

void SD_Shutdown (void)
{
    SD_MusicOff();
    CIA1_ICR = 0x7F;
}

// Starts song `song` (musicnames) `offs` bytes into its data, which is a
// record boundary: an offset from SD_MusicOff.
static void StartSong (int song, uint16_t offs)
{
    if (!AdLibPresent || song < 0 || song >= numsongs)
        return;
    uint32_t pos = far_peekl(FAR_ADD(musicfile, 8 + 4 * song));
    if (!pos)
        return;                         // (its MIDI file was not there)
    SID_Silence();
    __asm__ volatile ("sei");
    m65_mus_start = musicfile.a + pos;
    m65_mus_p = m65_mus_start + offs;
    m65_mus_wait = 0;
    m65_mus_on = 1;
    __asm__ volatile ("cli");
}

void SD_StartMusic (int chunk)
{
    SD_MusicOff();
    if (MusicMode == smm_AdLib)
        StartSong(chunk - STARTMUSIC, 0);
}

void SD_ContinueMusic (int chunk, int startoffs)
{
    SD_MusicOff();
    if (MusicMode == smm_AdLib)
        StartSong(chunk - STARTMUSIC, (uint16_t) startoffs);
}

void SD_MusicOn (void)
{
    if (AdLibPresent && m65_mus_start)
        m65_mus_on = 1;
}

// Stops the music; returns where it was, for SD_ContinueMusic.
int SD_MusicOff (void)
{
    uint16_t offs;

    __asm__ volatile ("sei");
    m65_mus_on = 0;
    offs = (uint16_t) (m65_mus_p - m65_mus_start);
    __asm__ volatile ("cli");
    if (AdLibPresent)
        SID_Silence();
    return (int) offs;
}

void SD_FadeOutMusic (void)
{
    if (MusicMode == smm_AdLib)
        SD_MusicOff();                  // (as the original does for AdLib)
}

boolean SD_MusicPlaying (void)
{
    return m65_mus_on != 0;
}

boolean SD_SetMusicMode (SMMode mode)
{
    boolean result = false;

    SD_FadeOutMusic();
    switch (mode)
    {
        case smm_Off:
            result = true;
            break;
        case smm_AdLib:
            result = AdLibPresent;
            break;
    }
    if (result)
        MusicMode = mode;
    return result;
}

// Sound effects: none yet, whichever mode is chosen.
boolean SD_SetSoundMode (SDMode mode)
{
    if (mode == sdm_AdLib && !AdLibPresent)
        return false;
    SoundMode = mode;
    return true;
}

int SD_GetChannelForDigi (int which) { (void) which; return -1; }
void SD_PositionSound (int leftvol, int rightvol) { (void) leftvol; (void) rightvol; }
boolean SD_PlaySound (soundnames sound) { (void) sound; return false; }
void SD_SetPosition (int channel, int leftvol, int rightvol)
{
    (void) channel; (void) leftvol; (void) rightvol;
}
void SD_StopSound (void) {}
void SD_WaitSoundDone (void) {}
word SD_SoundPlaying (void) { return 0; }

void SD_SetDigiDevice (SDSMode mode) { (void) mode; DigiMode = sds_Off; }
void SD_PrepareSound (int which) { (void) which; }
int SD_PlayDigitized (word which, int leftpos, int rightpos)
{
    (void) which; (void) leftpos; (void) rightpos;
    return 0;
}
void SD_StopDigitized (void) {}

// ID_SD for the MEGA65: replaces id_sd.cpp (SDL_mixer and an OPL emulator).
//
// Music: the songs are Bobby Prince's MIDI originals converted for three
// SIDs (tools/midi2sid.py --game), in MUSIC.DAT, which the user builds from
// their own MIDI files. The file is loaded whole into attic RAM, and the
// interrupt handler in m65_music.s plays it at 200 ticks per second from
// CIA 1 timer A. Without MUSIC.DAT there is no music: SD_Startup reports no
// AdLib, and the game turns its music options off itself.
//
// Sound effects: the digitized sounds (VSWAP's sound pages: gunfire, enemy
// calls, doors...) play on the four audio DMA channels, streamed from attic
// RAM by m65_sfx.s (the same timer interrupt). The sounds that only exist as
// AdLib effects (item pickups, weapon select and the like) are converted for
// a SID by the build (tools/adlib2sid.py: SFX.DAT, from the user's own
// AUDIOT) and play on the fourth SID (m65_sidfx.s), one at a time with the
// original's priorities, as on the AdLib.

#include "../wl_def.h"
#include "SDL_mixer.h"      // MIX_CHANNELS
#include "m65_far.h"
#include "m65_posix.h"
#include "m65_video.h"      // m65_dma_copy, m65_dma_fill

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
    void m65_test_startup (void);
    // The AdLib effects on the fourth SID (m65_sidfx.s).
    extern uint32_t m65_sidfx_ptr;
    extern uint16_t m65_sidfx_left;
    extern uint8_t m65_sidfx_acc, m65_sidfx_on, m65_sidfx_gate, m65_sidfx_wave;
    // The sound effect streamer (m65_sfx.s).
    extern uint8_t m65_sfx_state[4];
    extern uint32_t m65_sfx_src[4];
    extern uint16_t m65_sfx_left[4], m65_sfx_tail[4], m65_sfx_wp[4];
}

#define SFX_RING      0x10000UL         // 4 x 2048 bytes of chip RAM (m65_sfx.s)
#define SFX_RINGSIZE  2048
#define AUDIO_CH(n, reg) (*(volatile uint8_t *) (0xD720 + (n) * 16 + (reg)))
#define AUDIO_CTRL    (*(volatile uint8_t *) 0xD711)    // bit 7: audio DMA on
// Samples at 7042Hz: the channel's timer adds this each 40.5MHz cycle and
// takes a sample when it passes 2^24.
#define SFX_TIMEBASE  ((uint32_t) (7042.0 * 16777216.0 / 40500000.0))

// The digitized sounds: where each is in attic RAM (32 bits) and how long
// (16 bits), a table in attic RAM too (near memory is short).
static farptr   digitable;
static int      numdigi;
#define DIGISRC(i) far_peekl(FAR_ADD(digitable, (uint16_t) (i) * 6))
#define DIGILEN(i) far_peekw(FAR_ADD(digitable, (uint16_t) (i) * 6 + 4))
static int      channelsound[4];        // the sound each channel plays (for SD_SoundPlaying)
static uint32_t channelstart[4];        // when it started (the oldest gives way)
static uint32_t channelend[4];          // when it should have finished (a safety net:
                                        // if the channel stalls, nothing waits for ever)
static int      nextleft, nextright;    // SD_PositionSound's, for the next sound
static boolean  nextpositioned;

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
static boolean  havemusic;
static farptr   sfxfile;                // SFX.DAT in attic RAM (the AdLib effects)
static boolean  havesfx;
static uint16_t sidfxpriority;          // the AdLib effect playing's
static int      sidfxsound;
#define SIDFX(reg) SID(0x60 + (reg))    // the fourth SID, voice 1

#define MUSIC_VOLUME 12                 // (of 15; tools/midi2sid.py's GAME_VOLUME)

// Gates and waveforms off (the voices fall silent), the music's volume, no
// filter.
static void SID_Silence (void)
{
    for (uint8_t sid = 0; sid < 3; sid++)
    {
        for (uint8_t v = 0; v < 3; v++)
            SID(sid * 0x20 + v * 7 + 4) = 0;
        SID(sid * 0x20 + 0x17) = 0;
        SID(sid * 0x20 + 0x18) = MUSIC_VOLUME;
    }
}

// The digitized sounds' places and lengths (the original's SDL_SetupDigi,
// with far pointers): a sound spans pages from its start page to the next
// sound's; its exact length is in the list. Pages are consecutive in the
// VSWAP copy in attic RAM, so a sound is one run of bytes there (else it is
// copied into one).
static void SD_SetupDigi (void)
{
    farptr info = PM_GetPage(ChunksInFile - 1);
    int n = (int) (PM_GetPageSize(ChunksInFile - 1) / 4);
    int i;

    numdigi = 0;
    digitable = far_alloc((uint16_t) n * 6);
    if (FAR_ISNULL(digitable))
        return;
    for (i = 0; i < n; i++)
    {
        int start = far_peekw(FAR_ADD(info, i * 4));
        if (start + PMSoundStart >= ChunksInFile - 1)
            break;
        int last = ChunksInFile - 1;
        if (i < n - 1)
        {
            int next = far_peekw(FAR_ADD(info, i * 4 + 4));
            if (next != 0 && next + PMSoundStart <= ChunksInFile - 1)
                last = next + PMSoundStart;
        }
        uint16_t len = far_peekw(FAR_ADD(info, i * 4 + 2));
        farptr src = PM_GetPage(PMSoundStart + start);
        boolean contiguous = true;
        for (int page = PMSoundStart + start; page + 1 < last; page++)
            if (PM_GetPage(page + 1).a != PM_GetPage(page).a + PM_GetPageSize(page))
                contiguous = false;
        if (!contiguous)
        {
            farptr copy = far_alloc(len);
            uint32_t at = 0;
            if (FAR_ISNULL(copy))
                break;
            for (int page = PMSoundStart + start; page < last && at < len; page++)
            {
                uint32_t n2 = PM_GetPageSize(page);
                if (n2 > len - at) n2 = len - at;
                m65_dma_copy(copy.a + at, PM_GetPage(page).a, (uint16_t) n2);
                at += n2;
            }
            src = copy;
        }
        far_pokew(FAR_ADD(digitable, i * 6), (uint16_t) src.a);
        far_pokew(FAR_ADD(digitable, i * 6 + 2), (uint16_t) (src.a >> 16));
        far_pokew(FAR_ADD(digitable, i * 6 + 4), len);
        numdigi = i + 1;
    }
}

void SD_Startup (void)
{
    int i;
    uint32_t size;

    SoundBlasterPresent = true;         // (the audio DMA channels)
    SoundMode = sdm_Off;
    MusicMode = smm_Off;
    DigiMode = sds_Off;
    for (i = 0; i < LASTSOUND; i++)
        DigiMap[i] = -1;
    for (i = 0; i < STARTMUSIC - STARTDIGISOUNDS; i++)
        DigiChannel[i] = -1;

    // MUSIC.DAT: "WMUS", version 1, song count, ticks per second, offsets.
    musicfile = m65_file_far("music.dat", &size);
    havemusic = !FAR_ISNULL(musicfile) && size >= 8
        && far_peekl(musicfile) == 0x53554D57UL          // "WMUS"
        && far_peek(FAR_ADD(musicfile, 4)) == 1;
    if (havemusic)
        numsongs = far_peek(FAR_ADD(musicfile, 5));
    // SFX.DAT: "WSFX", version 2, sound count, tick rate, then 13 bytes per
    // sound (offset, ticks, priority, control, AD, SR, pulse width).
    sfxfile = m65_file_far("sfx" M65_VSUFFIX ".dat", &size);   // (SFX1.DAT, SFX6.DAT: per version)
    havesfx = !FAR_ISNULL(sfxfile) && size >= 8
        && far_peekl(sfxfile) == 0x58465357UL            // "WSFX"
        && far_peek(FAR_ADD(sfxfile, 4)) == 2;
    AdLibPresent = havemusic || havesfx;

    SIDMODE = (SIDMODE & 0x10) | 0x0F;  // 8580 sound (as the previews)
    for (i = 0; i < 0x80; i++)          // all four SIDs quiet
        SID(i) = 0;
    SID_Silence();
    SID(0x78) = 0x0F;                   // (the fourth's volume)
    m65_sidfx_on = 0;

    // The digitized sounds: the list is VSWAP's last page, (start page,
    // length) pairs, as the original SDL_SetupDigi reads it.
    SD_SetupDigi();
    for (i = 0; i < 4; i++)
    {
        AUDIO_CH(i, 0) = 0;             // channels off
        m65_sfx_state[i] = 0;
    }
    AUDIO_CTRL |= 0x80;                 // audio DMA on
    for (i = 0; i < 4; i++)
        ((volatile uint8_t *) 0xD71C)[i] = 0xFF;    // (each channel on the other side too)

    // The timer interrupt: music (if any) and the sound effect streamer,
    // 200 times a second (the songs' rate).
    uint16_t latch = (uint16_t) (CIA_HZ / 200 - 1);
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
    m65_test_startup();                 // (tests: m65_test.c)
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
    if (!havemusic || song < 0 || song >= numsongs)
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
    if (havemusic && m65_mus_start)
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
    if (havemusic)
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
            result = havemusic;
            break;
    }
    if (result)
        MusicMode = mode;
    return result;
}

// Sound effects: none yet, whichever mode is chosen.
boolean SD_SetSoundMode (SDMode mode)
{
    if (mode == sdm_AdLib && !havesfx)
        return false;
    SoundMode = mode;
    return true;
}

// A channel for a sound: its fixed one (DigiChannel: e.g. the player's
// weapons share one), else a free one, else the one playing longest.
int SD_GetChannelForDigi (int which)
{
    int i, best = 0;
    if (DigiChannel[which] != -1)
        return DigiChannel[which] & 3;
    for (i = 0; i < 4; i++)
        if (!m65_sfx_state[i])
            return i;
    for (i = 1; i < 4; i++)
        if (channelstart[i] < channelstart[best])
            best = i;
    return best;
}

void SD_PositionSound (int leftvol, int rightvol)
{
    nextleft = leftvol;
    nextright = rightvol;
    nextpositioned = true;
}

// Positions are 0 (loudest) to 15 (silent) per side. For now the louder
// side sets the channel's volume (no panning yet): 0-255, full at 0.
void SD_SetPosition (int channel, int leftpos, int rightpos)
{
    int pos = leftpos < rightpos ? leftpos : rightpos;
    AUDIO_CH(channel & 3, 9) = (uint8_t) ((15 - pos) * 17);
}

int SD_PlayDigitized (word which, int leftpos, int rightpos)
{
    if (DigiMode == sds_Off || which >= numdigi)
        return 0;
    uint32_t src = DIGISRC(which);
    uint16_t len = DIGILEN(which);
    if (!len)
        return 0;
    int ch = SD_GetChannelForDigi(which);
    uint32_t ring = SFX_RING + (uint32_t) ch * SFX_RINGSIZE;
    uint16_t first = len < SFX_RINGSIZE ? len : SFX_RINGSIZE;

    __asm__ volatile ("sei");
    AUDIO_CH(ch, 0) = 0;                // (stop it, then start afresh)
    m65_sfx_state[ch] = 0;
    __asm__ volatile ("cli");

    // The ring: the sound's first bytes, then silence.
    m65_dma_copy(ring, src, first);
    if (first < SFX_RINGSIZE)
        m65_dma_fill(ring + first, 0x80, SFX_RINGSIZE - first);

    AUDIO_CH(ch, 1) = (uint8_t) ring;   // base, current, top, rate, volume
    AUDIO_CH(ch, 2) = (uint8_t) (ring >> 8);
    AUDIO_CH(ch, 3) = (uint8_t) (ring >> 16);
    // The current address, too: enabling a channel does not reload it from
    // the base (it only goes back there at the top). From power-on it is
    // $050000 (our draw buffer: a burst of pixels as noise), later wherever
    // the channel last stopped. Writing its top byte makes it take effect.
    AUDIO_CH(ch, 0xA) = (uint8_t) ring;
    AUDIO_CH(ch, 0xB) = (uint8_t) (ring >> 8);
    AUDIO_CH(ch, 0xC) = (uint8_t) (ring >> 16);
    // (the top address is where the channel loops back, not played itself:
    // the ring's end, so that all 512 bytes play)
    AUDIO_CH(ch, 7) = (uint8_t) (ring + SFX_RINGSIZE);
    AUDIO_CH(ch, 8) = (uint8_t) ((ring + SFX_RINGSIZE) >> 8);
    AUDIO_CH(ch, 4) = (uint8_t) SFX_TIMEBASE;
    AUDIO_CH(ch, 5) = (uint8_t) (SFX_TIMEBASE >> 8);
    AUDIO_CH(ch, 6) = (uint8_t) (SFX_TIMEBASE >> 16);
    SD_SetPosition(ch, leftpos, rightpos);

    __asm__ volatile ("sei");
    m65_sfx_src[ch] = src + first;
    m65_sfx_left[ch] = len - first;
    m65_sfx_tail[ch] = SFX_RINGSIZE;
    m65_sfx_wp[ch] = 0;
    m65_sfx_state[ch] = 1;
    AUDIO_CH(ch, 0) = 0xE2;             // on, looping, unsigned, 8-bit
    __asm__ volatile ("cli");
    channelstart[ch] = SDL_GetTicks();
    channelend[ch] = channelstart[ch] + (uint32_t) len * 1000 / 7042 + 200;
    return ch;
}

// An AdLib effect on the fourth SID (SFX.DAT): one at a time, as on the
// AdLib, and a lower priority does not cut a higher one off.
static boolean SD_PlaySIDEffect (soundnames sound)
{
    if (SoundMode != sdm_AdLib || !havesfx)
        return false;
    farptr e = FAR_ADD(sfxfile, 8 + (uint16_t) sound * 13);
    uint32_t off = far_peekl(e);
    uint16_t ticks = far_peekw(FAR_ADD(e, 4)), prio = far_peekw(FAR_ADD(e, 6));
    if (!off || !ticks)
        return false;
    if (m65_sidfx_on && prio < sidfxpriority)
        return false;
    uint8_t wave = far_peek(FAR_ADD(e, 8));
    uint16_t pw = far_peekw(FAR_ADD(e, 11));

    __asm__ volatile ("sei");
    m65_sidfx_on = 0;
    SIDFX(4) = 0;                       // (gate off: the envelope starts afresh)
    SIDFX(5) = far_peek(FAR_ADD(e, 9));
    SIDFX(6) = far_peek(FAR_ADD(e, 10));
    SIDFX(2) = (uint8_t) pw;
    SIDFX(3) = (uint8_t) (pw >> 8);
    m65_sidfx_ptr = sfxfile.a + off;
    m65_sidfx_left = ticks;
    m65_sidfx_acc = 60;                 // (the first tick on the next interrupt)
    m65_sidfx_gate = 0;
    m65_sidfx_wave = wave;
    m65_sidfx_on = 1;
    __asm__ volatile ("cli");
    sidfxpriority = prio;
    sidfxsound = sound;
    return true;
}

boolean SD_PlaySound (soundnames sound)
{
    int lp = nextleft, rp = nextright;
    boolean positioned = nextpositioned;

    nextleft = nextright = 0;
    nextpositioned = false;
    if (sound < 0 || sound >= LASTSOUND)
        return false;
    if (DigiMode == sds_Off || DigiMap[sound] == -1)
        return SD_PlaySIDEffect(sound);
    int ch = SD_PlayDigitized((word) DigiMap[sound], lp, rp);
    channelsound[ch] = sound;
    channelSoundPos[ch].valid = 0;      // (PlaySoundLocGlobal sets it for moving sounds)
    SoundPositioned = positioned;
    return (boolean) (ch + 1);          // (the original returns channel + 1)
}

void SD_StopDigitized (void)
{
    int i;
    __asm__ volatile ("sei");
    for (i = 0; i < 4; i++)
    {
        AUDIO_CH(i, 0) = 0;
        m65_sfx_state[i] = 0;
    }
    __asm__ volatile ("cli");
}

void SD_StopSound (void)
{
    SD_StopDigitized();
    m65_sidfx_on = 0;
    SIDFX(4) = m65_sidfx_wave;          // (key off)
}

// The sound on a channel still playing, if any (the game waits on it).
word SD_SoundPlaying (void)
{
    int i;
    uint32_t now = SDL_GetTicks();
    if (m65_sidfx_on)
        return (word) sidfxsound;
    for (i = 0; i < 4; i++)
        if (m65_sfx_state[i] && (int32_t) (channelend[i] - now) > 0)
            return (word) channelsound[i];
    return 0;
}

void SD_WaitSoundDone (void)
{
    while (SD_SoundPlaying())
        SDL_Delay(5);
}

void SD_SetDigiDevice (SDSMode mode) { DigiMode = mode; }
void SD_PrepareSound (int which) { (void) which; }  // (all set up in SD_SetupDigi)

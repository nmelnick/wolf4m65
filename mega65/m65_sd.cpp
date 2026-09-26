// ID_SD for the MEGA65: replaces id_sd.cpp (SDL_mixer and an OPL emulator).
//
// Silent for now: SD_Startup reports no sound hardware, so the game turns
// its sound options off itself, and every play request is ignored. The plan
// is SID music (the AdLib music converted) and DMA audio for the digitized
// effects; audio chunks are already in far memory (CA_AudioChunk).

#include "../wl_def.h"
#include "SDL_mixer.h"      // MIX_CHANNELS

globalsoundpos channelSoundPos[MIX_CHANNELS];

boolean     AdLibPresent,
            SoundBlasterPresent,
            SoundPositioned;
SDMode      SoundMode;
SMMode      MusicMode;
SDSMode     DigiMode;
int         DigiMap[LASTSOUND];
int         DigiChannel[STARTMUSIC - STARTDIGISOUNDS];

void SD_Startup (void)
{
    int i;

    AdLibPresent = false;
    SoundBlasterPresent = false;
    SoundMode = sdm_Off;
    MusicMode = smm_Off;
    DigiMode = sds_Off;
    for (i = 0; i < LASTSOUND; i++)
        DigiMap[i] = -1;
    for (i = 0; i < STARTMUSIC - STARTDIGISOUNDS; i++)
        DigiChannel[i] = -1;
}

void SD_Shutdown (void) {}

int SD_GetChannelForDigi (int which) { (void) which; return -1; }
void SD_PositionSound (int leftvol, int rightvol) { (void) leftvol; (void) rightvol; }
boolean SD_PlaySound (soundnames sound) { (void) sound; return false; }
void SD_SetPosition (int channel, int leftvol, int rightvol)
{
    (void) channel; (void) leftvol; (void) rightvol;
}
void SD_StopSound (void) {}
void SD_WaitSoundDone (void) {}

void SD_StartMusic (int chunk) { (void) chunk; }
void SD_ContinueMusic (int chunk, int startoffs) { (void) chunk; (void) startoffs; }
void SD_MusicOn (void) {}
void SD_FadeOutMusic (void) {}
int SD_MusicOff (void) { return 0; }
boolean SD_MusicPlaying (void) { return false; }

boolean SD_SetSoundMode (SDMode mode) { SoundMode = mode == sdm_Off ? mode : sdm_Off; return mode == sdm_Off; }
boolean SD_SetMusicMode (SMMode mode) { MusicMode = smm_Off; return mode == smm_Off; }
word SD_SoundPlaying (void) { return 0; }

void SD_SetDigiDevice (SDSMode mode) { (void) mode; DigiMode = sds_Off; }
void SD_PrepareSound (int which) { (void) which; }
int SD_PlayDigitized (word which, int leftpos, int rightpos)
{
    (void) which; (void) leftpos; (void) rightpos;
    return 0;
}
void SD_StopDigitized (void) {}

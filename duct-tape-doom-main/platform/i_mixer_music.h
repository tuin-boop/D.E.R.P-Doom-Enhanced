/* Music the SDL mixer renders itself: OPL FM from the WAD's GENMIDI
   instruments, or a SoundFont bank. See platform/audio/mixer_music.c. */
#ifndef DOOM_I_MIXER_MUSIC_H
#define DOOM_I_MIXER_MUSIC_H
#include "doomtype.h"

struct tsf;

/* All of these run with the audio lock held. */
/* soundfont: play through the bank if one is in use, else the OPL. */
boolean MixerMusic_Register (const void* data, int size, int rate, boolean soundfont);
void MixerMusic_UnRegister (void);
void MixerMusic_Play (boolean loop);
void MixerMusic_Stop (void);
void MixerMusic_Pause (void);
void MixerMusic_Resume (void);
void MixerMusic_SetVolume (int volume);
/* Puts a SoundFont bank (from SF2_Open) in use; returns the one before. */
struct tsf* MixerMusic_UseBank (struct tsf* bank);
/* Adds the next frames of music to an interleaved stereo buffer. */
void MixerMusic_Render (float* stereo, int frames);
#endif

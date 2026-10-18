/* SoundFont music through TinySoundFont (MIT): General MIDI instruments
   from a .sf2 bank, in the spirit of the Sound Canvas the scores were
   written on. */
#ifndef DOOM_AUDIO_SF2_SYNTH_H
#define DOOM_AUDIO_SF2_SYNTH_H
#include "music_player.h"

typedef struct tsf sf2_bank_t;

/* Reads a bank for the output rate; NULL if it can't. max_voices 0 lets
   the voice pool grow; otherwise it is allocated up front, so playing
   never allocates. Slow for a big bank: keep it outside the audio lock. */
sf2_bank_t* SF2_Open (const char* path, int rate, int max_voices);
void SF2_Close (sf2_bank_t* bank);
/* The bank the synth plays from now on (NULL for none); returns the last. */
sf2_bank_t* SF2_Use (sf2_bank_t* bank);

/* The bank as a music_player synth: reset, event and mix. */
extern const music_synth_t SF2_Synth;

/* Most voices sounding at once since the last reset. */
int SF2_PeakVoices (void);
#endif

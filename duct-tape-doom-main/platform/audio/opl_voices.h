/* Score channels and the 18 chip voices that play them. */
#ifndef DOOM_AUDIO_OPL_VOICES_H
#define DOOM_AUDIO_OPL_VOICES_H
#include "opl_genmidi.h"
#include "music_player.h"

#define OPL_VOICES	18	/* two banks of nine */

/* Who gives up a voice when all 18 are held, after any second layer. */
typedef enum
{
    STEAL_HIGHEST,	/* highest channel in MUS numbering, so drums first */
    STEAL_SPARE_DRUMS,	/* highest melodic channel; drums only as a last resort.
			   Worse: MIDI drum notes are often never released and
			   end up holding voices the melody needs. */
} opl_steal_t;

/* Takes the instrument bank; the chip must already be started. */
void OPLVoices_Init (const genmidi_instrument_t* bank);
/* Silences every voice and puts the channels back to their defaults. */
void OPLVoices_Reset (void);
void OPLVoices_Event (const score_event_t* event);

/* The OPL as a music_player synth: reset, event and mix. */
extern const music_synth_t OPL_Synth;

void OPLVoices_SetStealing (opl_steal_t rule);
/* Voices taken from held notes since the last reset. */
int OPLVoices_Steals (void);
/* Second layers of double instruments left out for want of a free voice. */
int OPLVoices_DroppedLayers (void);
#endif

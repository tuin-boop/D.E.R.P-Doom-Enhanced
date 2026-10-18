/* MUS and MIDI scores, read into one event list for the music synths. */
#ifndef DOOM_AUDIO_MUSIC_SCORE_H
#define DOOM_AUDIO_MUSIC_SCORE_H
#include <stdint.h>
#include "doomtype.h"

#define SCORE_CHANNELS		16
#define SCORE_PERCUSSION	15	/* MUS numbering; MIDI channel 10 lands here */

enum { EV_NOTE_OFF, EV_NOTE_ON, EV_CONTROLLER, EV_PROGRAM, EV_BEND };
enum { CTRL_VOLUME = 7, CTRL_PAN = 10, CTRL_EXPRESSION = 11, CTRL_SOUNDS_OFF = 120,
       CTRL_RESET = 121, CTRL_NOTES_OFF = 123, CTRL_MONO = 126, CTRL_POLY = 127 };

/* data1 and data2 are 7-bit. A bend is in data2, 64 centred. */
typedef struct { uint32_t frame; byte type, channel, data1, data2; } score_event_t;

typedef struct
{
    score_event_t*	events;
    int			count, capacity;
    uint32_t		length;		/* output frames to the end of the song */
} score_t;

/* Reads a MUS or MIDI score timed for the given output rate. */
boolean Score_Load (score_t* score, const void* data, int size, int rate);
void Score_Clear (score_t* score);
#endif

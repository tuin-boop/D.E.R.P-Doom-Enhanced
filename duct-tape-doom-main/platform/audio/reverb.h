/* The room's echoes and tail, run in the mixer with the audio lock held. */
#ifndef DOOM_AUDIO_REVERB_H
#define DOOM_AUDIO_REVERB_H
#include "room.h"

void Reverb_Init(int rate);
/* Forgets everything still ringing. */
void Reverb_Clear(void);
/* The room to move toward; echoes crossfade and the tail glides there. */
void Reverb_SetRoom(const room_t *room);
/* Adds the reverb of a mono send to interleaved stereo, scaled by amount. */
void Reverb_Process(const float *send, float *stereo, int frames, float amount);
#endif

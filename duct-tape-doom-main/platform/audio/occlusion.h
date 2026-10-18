/* How much of a positioned sound's treble reaches the listener. */
#ifndef DOOM_AUDIO_OCCLUSION_H
#define DOOM_AUDIO_OCCLUSION_H
#include "m_fixed.h"

#define OCCLUSION_OPEN_HZ 20000.0f

/* A lowpass cutoff in Hz for a sound at (x, y, z). With walls, sounds out of
   sight are muffled; with distance, far ones are dulled. OCCLUSION_OPEN_HZ
   when nothing is lost. */
float Occlusion_Cutoff(fixed_t x, fixed_t y, fixed_t z, int walls, int distance);
#endif

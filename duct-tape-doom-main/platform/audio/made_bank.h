/* The made-up sounds as the mixer plays them: every impact kind in a few
 * seeded variations, and every ambient kind as a few seamless loops, all made
 * once at startup (about 0.1 s) as 8-bit 11 kHz samples like DOOM's own. */
#ifndef DOOM_AUDIO_MADE_BANK_H
#define DOOM_AUDIO_MADE_BANK_H

#define MADE_RATE 11025
#define MADE_VARIANTS 6
#define MADE_LOOP_SECONDS 8   /* the wind's are longer */

typedef struct { const unsigned char *data; int length, rate; } made_sample_t;

/* Renders everything; once, before the mixer runs. */
void MadeBank_Build(void);
/* A variation of an impact kind (audio/impact_synth.h); NULL before the build. */
const made_sample_t *MadeBank_Impact(int kind, int variant);
/* A take of an ambient kind's loop (audio/ambient_synth.h); each layer of
   the kind (audio/ambience.h) plays its own. */
const made_sample_t *MadeBank_Loop(int kind, int layer);
/* A tube striking again as its light comes back: a tick and a crackle. */
const made_sample_t *MadeBank_Strike(int variant);
#endif

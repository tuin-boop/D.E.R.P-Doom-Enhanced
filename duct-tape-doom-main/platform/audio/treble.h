/* Treble an effect never had, made up as it plays (see treble.c). */
#ifndef DOOM_AUDIO_TREBLE_H
#define DOOM_AUDIO_TREBLE_H

typedef struct { float b0, b1, b2, a1, a2; } treble_filter_t;
/* One playing sound's filters, tuned to the top of its band. */
typedef struct {
    float nyquist;
    int active, air;
    treble_filter_t band[2], above[2], higher[2];
    float z[6][2];
} treble_t;

/* nyquist: the highest frequency the sample holds as it plays, in Hz at
   the output rate (half its rate, times its pitch). Cheap when unchanged. */
void Treble_Tune(treble_t *treble, float nyquist, float rate);
void Treble_Reset(treble_t *treble);
/* The resampled sound x with amount (0-2) of made-up treble added. */
float Treble_Process(treble_t *treble, float x, float amount);
#endif

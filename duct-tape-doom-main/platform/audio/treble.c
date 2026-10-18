/* Treble the samples never had, made up at playback. An 11 kHz effect holds
 * nothing above 5.5 kHz. Holding each sample fills that with mirror images
 * of the sound, which ring metallic because they turn its spectrum upside
 * down. Here the top octave the sample does hold is rectified instead:
 * that doubles its frequencies, so tones gain their own overtones and noise,
 * most of a shot or an explosion, stays noise, only higher. A second pass
 * doubles again for the air above. Only what lands above the sample's own
 * band is kept, so the recording itself comes through untouched. */
#include <math.h>
#include <string.h>
#include "treble.h"

#define PI_F 3.14159265f
/* Samples this bright miss little; and doubling them would fold back. */
#define HIGHEST_HZ 12000.0f
/* Where the source octave starts, as a fraction of the sample's Nyquist. */
#define BAND_FROM 0.5f
/* Rectifying keeps a band's level but only 0.42 of it lands an octave up;
   these bring the made-up treble at 100% near the energy holding samples
   puts there, without the metallic ring. */
#define GAIN 2.0f
#define AIR_GAIN 1.8f
/* Rare spikes, mostly on clipped samples, are rounded off here so they
   don't duck the whole mix in the limiter; the bulk passes nearly as is. */
#define CEILING 0.5f

/* Butterworth, fourth order, as two sections. */
static void highpass(treble_filter_t f[2], float hz, float rate)
{
    static const float q[2] = { 0.5412f, 1.3066f };
    float w = 2 * PI_F * hz / rate, c = cosf(w);
    for (int i = 0; i < 2; ++i)
    {
        float alpha = sinf(w) / (2 * q[i]), a0 = 1 + alpha;
        f[i].b0 = (1 + c) / 2 / a0;
        f[i].b1 = -(1 + c) / a0;
        f[i].b2 = f[i].b0;
        f[i].a1 = -2 * c / a0;
        f[i].a2 = (1 - alpha) / a0;
    }
}
static float run(const treble_filter_t f[2], float z[2][2], float x)
{
    for (int i = 0; i < 2; ++i)
    {
        float y = f[i].b0 * x + z[i][0];
        z[i][0] = f[i].b1 * x - f[i].a1 * y + z[i][1];
        z[i][1] = f[i].b2 * x - f[i].a2 * y;
        x = y;
    }
    return x;
}

void Treble_Tune(treble_t *treble, float nyquist, float rate)
{
    if (nyquist == treble->nyquist) return;
    treble->nyquist = nyquist;
    treble->active = nyquist < HIGHEST_HZ;
    treble->air = 2 * nyquist < HIGHEST_HZ * 1.5f;
    if (!treble->active) return;
    highpass(treble->band, BAND_FROM * nyquist, rate);
    highpass(treble->above, nyquist, rate);
    if (treble->air) highpass(treble->higher, 2 * nyquist, rate);
}
void Treble_Reset(treble_t *treble)
{
    memset(treble->z, 0, sizeof(treble->z));
}

float Treble_Process(treble_t *treble, float x, float amount)
{
    if (!treble->active) return x;
    float band = run(treble->band, &treble->z[0], x);
    float above = run(treble->above, &treble->z[2], fabsf(band));
    float added = above;
    if (treble->air) added += AIR_GAIN * run(treble->higher, &treble->z[4], fabsf(above));
    return x + CEILING * tanhf(amount * GAIN * added / CEILING);
}

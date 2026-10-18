/* Liquids. Nukage bubbles thick and low: a few small bloops coming in fits,
 * now and then a big one that breaks the surface with a wet pop, over a slow
 * churning gurgle. Blood is slower and deeper, its bubbles barely rising in
 * pitch and its highs cut, since a rising tone in a thick liquid sounds like
 * a beam. A slime fall pours. DOOM's water mostly stands: no waves, a faint
 * stillness and a drip now and then. */
#include <math.h>
#include <stdlib.h>
#include "ambient_synth.h"
#include "synth_kit.h"

/* Bubbles arriving at a rate that wanders lo..hi a second, in fits. */
static void bubbling(float *out, int frames, int rate, rng_t *r, float lo, float hi,
                     float low_hz, float high_hz, float rise, float linger, float amp)
{
    drift_t fits = drift_make(r, lo, hi, 0.8f, 2.5f, rate);
    for (int n = 0; n < frames; ++n)
    {
        float per_second = drift_next(&fits, r);
        if (rng_unit(r) >= per_second / rate) continue;
        /* Spread evenly in pitch, not in Hz: more low ones than a flat spread gives. */
        float freq = low_hz * powf(high_hz / low_hz, rng_unit(r)), u = rng_unit(r);
        add_bubble(out, frames, n, rate, freq, amp * (0.3f + 0.7f * u * u), rise * rng_range(r, 0.7f, 1.3f), linger);
    }
}

/* A big one reaching the surface: a deep bloop rising by rise, then the
   skin breaking. */
static void blorps(float *out, int frames, int rate, rng_t *r, float every_lo, float every_hi,
                   float low_hz, float high_hz, float rise, float amp)
{
    for (float t = rng_range(r, 0.2f, every_hi); t * rate < frames; t += rng_range(r, every_lo, every_hi))
    {
        float freq = rng_range(r, low_hz, high_hz), seconds = 0;
        add_bubble(out, frames, (int)(t * rate), rate, freq, amp, rise * rng_range(r, 0.8f, 1.2f), 1.6f);
        seconds = 1.4f / (0.13f * freq + 0.0072f * powf(freq, 1.5f));
        int at = (int)((t + seconds) * rate);
        svf_t pop = svf_make(rng_range(r, 600, 1400), 0.8f, rate);
        for (int n = 0; n < (int)(0.03f * rate) && at + n < frames; ++n)
        {
            float x = n / (float)rate, env = (1 - expf(-x / 0.001f)) * expf(-x / 0.008f);
            svf_run(&pop, rng_noise(r));
            out[at + n] += 0.5f * amp * env * pop.band;
        }
        /* A few droplets falling back. */
        for (int k = (int)rng_range(r, 1, 3.99f); k > 0; --k)
            add_bubble(out, frames, at + (int)(rng_range(r, 0.04f, 0.15f) * rate), rate,
                       rng_range(r, 500, 1200), 0.15f * amp, 0.1f, 1);
    }
}

/* Low noise heaving slowly: the body of the liquid moving. */
static void churn(float *out, int frames, int rate, rng_t *r, float cutoff, float amp)
{
    svf_t body = svf_make(cutoff, 0.9f, rate);
    drift_t swell = drift_make(r, 0.3f, 1.0f, 0.5f, 1.5f, rate);
    for (int n = 0; n < frames; ++n)
    {
        svf_run(&body, rng_noise(r));
        out[n] += amp * drift_next(&swell, r) * body.low;
    }
}

static void slimefall(float *out, int frames, int rate, rng_t *r)
{
    svf_t splash = svf_make(1200, 0.5f, rate), body = svf_make(300, 0.7f, rate);
    int grain = 0;
    float level = 1;
    for (int n = 0; n < frames; ++n)
    {
        if (--grain <= 0) { grain = (int)(rng_range(r, 0.003f, 0.008f) * rate); level = rng_range(r, 0.4f, 1.0f); }
        float noise = rng_noise(r);
        svf_run(&splash, noise); svf_run(&body, noise);
        out[n] += 0.2f * level * splash.band + 0.25f * body.low;
    }
    bubbling(out, frames, rate, r, 20, 40, 400, 1500, 0.1f, 1, 0.12f);
}

/* One drop from the ceiling: a soft tick as it hits, the plink of the
   bubble it pulls under, and a smaller ring or two after. */
static void drip(float *out, int frames, int at, int rate, rng_t *r, float freq, float amp)
{
    add_click(out, frames, at, rate, r, 0.3f * amp, 800, 2000);
    add_bubble(out, frames, at + (int)(0.002f * rate), rate, freq, amp, 0.05f, 1);
    for (int k = (int)rng_range(r, 0, 2.99f); k > 0; --k)
        add_bubble(out, frames, at + (int)(rng_range(r, 0.02f, 0.08f) * rate), rate,
                   freq * rng_range(r, 1.2f, 1.6f), 0.25f * amp, 0.05f, 1);
}

static void water(float *out, int frames, int rate, rng_t *r)
{
    /* The stillness: a faint, steady low hush. */
    svf_t still = svf_make(300, 0.7f, rate);
    for (int n = 0; n < frames; ++n)
    {
        svf_run(&still, rng_noise(r));
        out[n] += 0.03f * still.low;
    }
    /* Drops, often twice from the same spot. */
    for (float t = rng_range(r, 0.3f, 2.0f); t * rate < frames; t += rng_range(r, 1.2f, 4.0f))
    {
        float freq = rng_range(r, 900, 2400), amp = rng_range(r, 0.5f, 1.0f);
        drip(out, frames, (int)(t * rate), rate, r, freq, amp);
        if (rng_unit(r) < 0.4f)
            drip(out, frames, (int)((t + rng_range(r, 0.3f, 0.8f)) * rate), rate, r, freq * rng_range(r, 0.97f, 1.03f), 0.8f * amp);
    }
}

/* Two one-pole stages: the highs cut off above cutoff. */
static void dull(float *out, int frames, int rate, float cutoff)
{
    float keep = expf(-2 * SYNTH_PI * cutoff / rate), a = 0, b = 0;
    for (int n = 0; n < frames; ++n)
    {
        a = out[n] + (a - out[n]) * keep;
        b = a + (b - a) * keep;
        out[n] = b;
    }
}

void Liquid_Render(int kind, unsigned seed, int rate, float *out, int frames)
{
    rng_t r = rng_seed(seed);
    switch (kind)
    {
    case AMBIENT_NUKAGE:
        churn(out, frames, rate, &r, 180, 0.35f);
        bubbling(out, frames, rate, &r, 0.5f, 3.5f, 180, 700, 0.2f, 1.3f, 0.35f);
        blorps(out, frames, rate, &r, 1.0f, 3.5f, 70, 160, 0.27f, 0.6f);
        break;
    case AMBIENT_BLOOD:
    {
        /* Rendered apart so its highs can be cut without touching out. */
        float *own = calloc((size_t)frames, sizeof(float));
        if (!own) break;
        churn(own, frames, rate, &r, 130, 0.35f);
        bubbling(own, frames, rate, &r, 0.5f, 4, 90, 300, 0.05f, 1.2f, 0.35f);
        blorps(own, frames, rate, &r, 1.5f, 5.0f, 50, 110, 0.05f, 0.6f);
        dull(own, frames, rate, 700);
        for (int n = 0; n < frames; ++n) out[n] += own[n];
        free(own);
        break;
    }
    case AMBIENT_SLIMEFALL: slimefall(out, frames, rate, &r); break;
    case AMBIENT_WATER: water(out, frames, rate, &r); break;
    }
}

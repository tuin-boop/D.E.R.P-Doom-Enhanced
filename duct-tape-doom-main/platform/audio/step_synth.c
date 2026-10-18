/* Footsteps and splashes. A step is a heel and, a moment later, a softer
 * toe: a low knock with a scuff over it, colored by the floor - a faint ring
 * on metal, a hollow knock on wood, muffled on grass and carpet. In water a
 * step is a splash with droplets falling back; in nukage, slime and blood a
 * thick gloop and the foot pulling out. Kept short and plain: they play all
 * the time. */
#include <math.h>
#include "impact_synth.h"
#include "synth_kit.h"

/* A scuff: noise in a band, quickly gone. */
static void scuff(float *out, int frames, int at, int rate, rng_t *r, float centre, float seconds, float amp)
{
    svf_t band = svf_make(centre * rng_range(r, 0.8f, 1.25f), 0.7f, rate);
    int length = (int)(seconds * 6 * rate);
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float t = n / (float)rate, env = (1 - expf(-t / 0.0008f)) * expf(-t / seconds);
        svf_run(&band, rng_noise(r));
        if (at + n >= 0) out[at + n] += amp * env * band.band * 1.5f;
    }
}

/* Lowpassed noise, a soft muffled push. */
static void hush(float *out, int frames, int at, int rate, rng_t *r, float cutoff, float seconds, float amp)
{
    svf_t low = svf_make(cutoff, 0.7f, rate);
    int length = (int)(seconds * 6 * rate);
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float t = n / (float)rate, env = (1 - expf(-t / 0.003f)) * expf(-t / seconds);
        svf_run(&low, rng_noise(r));
        if (at + n >= 0) out[at + n] += amp * env * low.low * 1.5f;
    }
}

/* One footfall at frame at, loud by amp. */
static void footfall(int kind, float *out, int frames, int at, int rate, rng_t *r, float amp)
{
    switch (kind)
    {
    case IMPACT_STEP_STONE:
        add_thump(out, frames, at, rate, 120, 70, 0.012f, 0.5f * amp);
        scuff(out, frames, at, rate, r, 1400, 0.008f, 0.4f * amp);
        for (int k = (int)rng_range(r, 0, 2.99f); k > 0; --k)  /* a little grit */
            add_click(out, frames, at + (int)(rng_range(r, 0.005f, 0.04f) * rate), rate, r, 0.12f * amp, 2000, 4500);
        break;
    case IMPACT_STEP_METAL:
        /* A boot on a plate: a heavy knock, the plate booming low under it, a clank and its ring. */
        add_thump(out, frames, at, rate, 190, 85, 0.02f, 0.45f * amp);
        add_partial(out, frames, at, rate, rng_range(r, 110, 190), 0.18f * amp, 0.07f, 0);
        scuff(out, frames, at, rate, r, 2000, 0.008f, 0.38f * amp);
        for (int m = 0; m < 5; ++m)
            add_partial(out, frames, at, rate, rng_range(r, 350, 2600), 0.06f * amp, rng_range(r, 0.04f, 0.1f), rng_range(r, 0, 6.28f));
        break;
    case IMPACT_STEP_WOOD:
        add_thump(out, frames, at, rate, 220, 150, 0.02f, 0.45f * amp);
        add_partial(out, frames, at, rate, rng_range(r, 180, 260), 0.2f * amp, 0.035f, 0);
        scuff(out, frames, at, rate, r, 1100, 0.006f, 0.25f * amp);
        break;
    case IMPACT_STEP_SOFT:
        hush(out, frames, at, rate, r, 450, 0.025f, 0.8f * amp);
        for (int k = (int)rng_range(r, 1, 4.99f); k > 0; --k)  /* grass and dirt crunching */
            add_click(out, frames, at + (int)(rng_range(r, 0, 0.05f) * rate), rate, r, 0.08f * amp, 1500, 3500);
        break;
    }
}

/* Water: a burst of droplets in a band, a plunk under it, a few bloops. */
static void splash(float *out, int frames, int rate, rng_t *r)
{
    svf_t band = svf_make(rng_range(r, 1200, 1900), 0.6f, rate);
    int length = (int)(0.18f * rate), grain = (int)(0.002f * rate) + 1;
    float level = 1;
    for (int n = 0; n < length && n < frames; ++n)
    {
        float t = n / (float)rate, env = (1 - expf(-t / 0.003f)) * expf(-t / 0.05f);
        if (n % grain == 0) { float u = rng_unit(r); level = 0.3f + 0.7f * u * u; }
        svf_run(&band, rng_noise(r));
        out[n] += 0.55f * env * level * band.band * 1.5f;
    }
    add_thump(out, frames, 0, rate, 220, 120, 0.02f, 0.25f);
    for (int k = (int)rng_range(r, 2, 4.99f); k > 0; --k)
        add_bubble(out, frames, (int)(rng_range(r, 0.03f, 0.18f) * rate), rate, rng_range(r, 700, 1800),
                   rng_range(r, 0.12f, 0.28f), 0.1f, 1);
}

/* Thick liquid: a low gloop, a bloop or two, and the foot pulling out. */
static void gloop(float *out, int frames, int rate, rng_t *r)
{
    hush(out, frames, 0, rate, r, 700, 0.06f, 0.55f);
    for (int k = (int)rng_range(r, 1, 2.99f); k > 0; --k)
        add_bubble(out, frames, (int)(rng_range(r, 0.01f, 0.08f) * rate), rate, rng_range(r, 150, 350),
                   0.35f, 0.05f, 1.3f);
    hush(out, frames, (int)(rng_range(r, 0.12f, 0.2f) * rate), rate, r, 500, 0.04f, 0.25f);
}

void Step_Render(int kind, unsigned seed, int rate, float *out, int frames)
{
    rng_t r = rng_seed(seed);
    if (kind == IMPACT_SPLASH_WATER) { splash(out, frames, rate, &r); return; }
    if (kind == IMPACT_SPLASH_THICK) { gloop(out, frames, rate, &r); return; }
    footfall(kind, out, frames, 0, rate, &r, 1);
    footfall(kind, out, frames, (int)(rng_range(&r, 0.03f, 0.06f) * rate), rate, &r, 0.45f);
}

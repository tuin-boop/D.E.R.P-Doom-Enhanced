/* Sparks off hit tech, and lamps and screens shot dead. The gunshot covers
 * the moment of the hit, so these live in what comes after it: cracks that
 * thin out as the sparks die, a short arc, a hum giving up. */
#include <math.h>
#include <stdlib.h>
#include "impact_synth.h"
#include "synth_kit.h"

/* A few late cracks, sparks landing after the rest have died. */
static void stragglers(float *out, int frames, int rate, rng_t *r, float from, float to, float amp)
{
    for (int i = (int)rng_range(r, 2, 5.99f); i > 0; --i)
        add_click(out, frames, (int)(rng_range(r, from, to) * rate), rate, r, amp * rng_range(r, 0.4f, 1), 1500, 4500);
}

/* A sizzle: high noise let through in random grains, fading over tau. */
static void fizz(float *out, int frames, int rate, rng_t *r, float start, float tau, float amp)
{
    svf_t hiss = svf_make(2500, 0.7f, rate);
    int grain = (int)(0.0015f * rate), at = (int)(start * rate);
    float level = 0;
    for (int n = 0; at + n < frames; ++n)
    {
        float t = n / (float)rate, env = (1 - expf(-t / 0.002f)) * expf(-t / tau);
        if (env < 0.002f && t > tau) break;
        if (n % grain == 0) { float u = rng_unit(r); level = u * u * u; }
        svf_run(&hiss, rng_noise(r));
        out[at + n] += amp * env * level * hiss.high;
    }
}

/* An arc: noise gated by the mains, buzzing at 100 Hz, for a few cycles. */
static void arc(float *out, int frames, int rate, rng_t *r, float seconds, float amp)
{
    svf_t body = svf_make(rng_range(r, 900, 1600), 0.6f, rate);
    float base = rng_range(r, 96, 104), duty = rng_range(r, 0.2f, 0.4f), phase = 0;
    int length = (int)((seconds + 0.01f) * rate);
    for (int n = 0; n < length && n < frames; ++n)
    {
        float t = n / (float)rate;
        phase += base * (1 + 0.04f * rng_noise(r)) / rate;
        float cycle = phase - floorf(phase);
        float gate = cycle < duty ? 1 : 0.15f, saw = 2 * cycle - 1;
        svf_run(&body, rng_noise(r) * gate);
        float env = (1 - expf(-t / 0.001f)) * (t < seconds ? 1 : expf(-(t - seconds) / 0.003f));
        out[n] += amp * env * (1.4f * body.band + 0.15f * saw);
    }
}

/* The hum cutting in and out, the gaps growing, each comeback a crack. */
static float sputter(float *out, int frames, int rate, rng_t *r, unsigned seed)
{
    float seconds = rng_range(r, 0.18f, 0.4f);
    int length = (int)(seconds * rate);
    if (length > frames) length = frames;
    float *hum = calloc((size_t)length, sizeof(float));
    if (!hum) return seconds;
    Impact_Hum(0, seed, rate, hum, length);
    float t = 0, gate = 1, target = 1;
    int on = 1, next = (int)(rng_range(r, 0.008f, 0.03f) * rate);
    float smooth = expf(-1 / (0.0007f * rate));
    for (int n = 0; n < length; ++n)
    {
        t = n / (float)rate;
        if (n >= next)
        {
            float worn = t / seconds;
            on = !on;
            target = on ? rng_range(r, 0.4f, 1.0f) * (1 - 0.5f * worn) : 0;
            next = n + (int)((on ? rng_range(r, 0.008f, 0.04f) * (1 - 0.6f * worn)
                                 : rng_range(r, 0.005f, 0.03f) * (1 + 2 * worn)) * rate);
            if (on) add_click(out, frames, n, rate, r, 0.35f, 1200, 4000);
        }
        gate = target + (gate - target) * smooth;
        out[n] += gate * hum[n];
    }
    free(hum);
    return seconds;
}

void Spark_Render(int kind, unsigned seed, int rate, float *out, int frames)
{
    rng_t r = rng_seed(seed);
    switch (kind)
    {
    case IMPACT_SPARK_CRACKLE:
        add_crackle(out, frames, rate, &r, 0, 0.45f, 260, 0.07f, 1.2f, 1800, 5000);
        stragglers(out, frames, rate, &r, 0.12f, 0.45f, 0.5f);
        break;
    case IMPACT_SPARK_FIZZ:
        add_crackle(out, frames, rate, &r, 0, 0.45f, 200, 0.06f, 1.0f, 1800, 5000);
        fizz(out, frames, rate, &r, 0, 0.1f, 0.9f);
        stragglers(out, frames, rate, &r, 0.15f, 0.45f, 0.45f);
        break;
    case IMPACT_SPARK_ZAP:
    {
        float seconds = rng_range(&r, 0.04f, 0.09f);
        add_click(out, frames, 0, rate, &r, 1.0f, 1500, 3500);
        arc(out, frames, rate, &r, seconds, 0.5f);
        add_crackle(out, frames, rate, &r, seconds, 0.5f, 150, 0.06f, 0.9f, 1800, 5000);
        stragglers(out, frames, rate, &r, seconds + 0.1f, 0.5f, 0.45f);
        break;
    }
    case IMPACT_LAMP_DIES:
    {
        add_click(out, frames, 0, rate, &r, 0.9f, 1500, 3500);
        add_crackle(out, frames, rate, &r, 0, 0.15f, 180, 0.04f, 0.5f, 1800, 5000);
        float end = sputter(out, frames, rate, &r, seed);
        add_thump(out, frames, (int)(end * rate), rate, 90, 35, 0.04f, 0.6f);
        add_click(out, frames, (int)(end * rate), rate, &r, 0.6f, 1000, 3000);
        /* Bits of the fixture fall from the ceiling. */
        Glass_Shards(out, frames, rate, seed ^ 0x5bd1e995u, 4, 96, 16, 0.02f, 0.3f, 0.6f);
        break;
    }
    case IMPACT_SCREEN_DIES:
    {
        add_click(out, frames, 0, rate, &r, 0.8f, 1500, 3500);
        /* The face cracks. */
        for (int i = 0; i < 4; ++i)
            add_partial(out, frames, (int)(rng_range(&r, 0, 0.02f) * rate), rate, rng_range(&r, 3000, 4500),
                        0.25f, 0.012f, rng_range(&r, 0, 2 * SYNTH_PI));
        /* Static, then the tube thumps off. */
        float hold = rng_range(&r, 0.06f, 0.14f);
        svf_t band = svf_make(2200, 0.5f, rate);
        int grain = (int)(0.0015f * rate);
        float level = 0;
        for (int n = 0; n < frames; ++n)
        {
            float t = n / (float)rate, env = (1 - expf(-t / 0.002f)) * (t < hold ? 1 : expf(-(t - hold) / 0.07f));
            if (t > hold + 0.5f) break;
            if (n % grain == 0) { float u = rng_unit(&r); level = 0.3f + 0.7f * u * u; }
            svf_run(&band, rng_noise(&r));
            out[n] += 0.5f * env * level * band.band * 1.5f;
        }
        add_crackle(out, frames, rate, &r, 0, hold + 0.1f, 300, 0.08f, 0.5f, 1800, 5000);
        add_thump(out, frames, (int)(0.6f * hold * rate), rate, 70, 28, 0.06f, 0.55f);
        break;
    }
    }
}

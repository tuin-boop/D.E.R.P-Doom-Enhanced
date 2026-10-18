/* Machines on the walls: lamps, screens, computers, teleporters. A lamp's
 * ballast buzzes at twice the mains, 100 Hz, and follows the sector's light:
 * a flickering light's buzz drops out with it, and every time it comes back
 * the tube ticks and the starter crackles. A screen hums lower under faint
 * static, with the line whine of a CRT where the rate can hold 15.7 kHz. */
#include <math.h>
#include "ambient_synth.h"
#include "synth_kit.h"

#define TIC_RATE 35

/* The sector's light at frame n, eased over 5 ms, and whether it just jumped. */
typedef struct { const float *light; int tics, rate; float now, keep; } follow_t;

static follow_t follow_make(const float *light, int tics, int rate)
{
    follow_t f = { light, tics, rate, light && tics ? light[0] : 1, expf(-1 / (0.005f * rate)) };
    return f;
}
static float follow_target(const follow_t *f, int n)
{
    if (!f->light || f->tics <= 0) return 1;
    int tic = (int)((long)n * TIC_RATE / f->rate);
    return f->light[tic < f->tics ? tic : f->tics - 1];
}

static void lamp(float *out, int frames, int rate, rng_t *r, const float *light, int tics)
{
    float phases[10];
    for (int h = 0; h < 10; ++h) phases[h] = rng_range(r, 0, 2 * SYNTH_PI);
    svf_t hiss = svf_make(1800, 0.7f, rate);
    follow_t f = follow_make(light, tics, rate);
    float last = follow_target(&f, 0);
    for (int n = 0; n < frames; ++n)
    {
        float target = follow_target(&f, n);
        /* A jump of a third or more is the tube going out or striking again. */
        if (fabsf(target - last) > 0.3f)
        {
            if (target > last)
            {
                add_click(out, frames, n, rate, r, 0.35f, 1500, 4000);
                add_crackle(out, frames, rate, r, n / (float)rate, n / (float)rate + 0.04f, 400, 0.01f, 0.25f, 1500, 4500);
            }
            else add_click(out, frames, n, rate, r, 0.15f, 800, 2500);
        }
        last = target;
        f.now = target + (f.now - target) * f.keep;
        float t = n / (float)rate, v = 0;
        for (int h = 1; h <= 10; ++h)
            if (100.0f * h < 0.45f * rate) v += sinf(2 * SYNTH_PI * 100 * h * t + phases[h - 1]) / powf((float)h, 0.8f);
        float cycle = fmodf(t * 100, 1);
        svf_run(&hiss, rng_noise(r));
        out[n] += f.now * (0.22f * tanhf(1.8f * v) + (cycle < 0.15f ? 0.14f : 0.02f) * hiss.high);
    }
}

/* level scales the whole screen. */
static void crt(float *out, int frames, int rate, rng_t *r, float level)
{
    float phases[6];
    for (int h = 0; h < 6; ++h) phases[h] = rng_range(r, 0, 2 * SYNTH_PI);
    svf_t hiss = svf_make(3000, 0.7f, rate);
    float whine = rng_range(r, 15700, 15760), whine_phase = 0;
    int whine_heard = whine < 0.45f * rate;
    for (int n = 0; n < frames; ++n)
    {
        float t = n / (float)rate, v = 0;
        for (int h = 1; h <= 6; ++h) v += sinf(2 * SYNTH_PI * 50 * h * t + phases[h - 1]) / powf((float)h, 1.4f);
        svf_run(&hiss, rng_noise(r));
        float x = 0.16f * tanhf(1.2f * v) + 0.025f * hiss.band;
        if (whine_heard) { whine_phase += 2 * SYNTH_PI * whine / rate; x += 0.02f * sinf(whine_phase); }
        out[n] += level * x;
    }
    add_crackle(out, frames, rate, r, 0, frames / (float)rate, 25, 1e9f, 0.05f * level, 2000, 4500);
}

/* A short bleep: a soft square tone, sine and a third harmonic. */
static void bleep(float *out, int frames, int at, int rate, float freq, float seconds, float amp)
{
    int length = (int)(seconds * rate);
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float t = n / (float)rate, edge = fminf(fminf(t, seconds - t) / 0.004f, 1);
        float w = 2 * SYNTH_PI * freq * t;
        if (at + n >= 0) out[at + n] += amp * edge * (sinf(w) + (3 * freq < 0.45f * rate ? 0.3f * sinf(3 * w) : 0));
    }
}

/* A computer only bleeps: a hum and relay clatter under it were too much. */
static void computer(float *out, int frames, int rate, rng_t *r)
{
    float end = frames / (float)rate;
    /* Bleeps in little tunes of two to five. */
    for (float t = rng_range(r, 0.5f, 2.0f); t < end; t += rng_range(r, 1.0f, 4.0f))
    {
        float at = t;
        for (int k = (int)rng_range(r, 2, 5.99f); k > 0; --k)
        {
            float seconds = rng_range(r, 0.03f, 0.09f);
            bleep(out, frames, (int)(at * rate), rate, 600 * powf(2, (int)rng_range(r, 0, 9.99f) / 5.0f), seconds, 0.45f);
            at += seconds + rng_range(r, 0.01f, 0.05f);
        }
    }
}

/* Two low tones a fifth apart, beating slowly, under a shimmer that pulses. */
static void teleporter(float *out, int frames, int rate, rng_t *r)
{
    svf_t shimmer = svf_make(rng_range(r, 2500, 3500), 2.0f, rate);
    float beat = rng_range(r, 0.3f, 0.7f), pulse = rng_range(r, 5, 7);
    for (int n = 0; n < frames; ++n)
    {
        float t = n / (float)rate;
        float tone = sinf(2 * SYNTH_PI * 55 * t) + sinf(2 * SYNTH_PI * (55 + beat) * t)
                   + 0.6f * sinf(2 * SYNTH_PI * 82.5f * t) + 0.3f * sinf(2 * SYNTH_PI * 110 * t);
        svf_run(&shimmer, rng_noise(r));
        float swell = 0.5f + 0.5f * sinf(2 * SYNTH_PI * pulse * t);
        out[n] += 0.1f * tone + 0.12f * swell * swell * shimmer.band;
    }
}

void Machine_Render(int kind, unsigned seed, int rate, float *out, int frames,
                    const float *light, int tics)
{
    rng_t r = rng_seed(seed);
    switch (kind)
    {
    case AMBIENT_LAMP: lamp(out, frames, rate, &r, light, tics); break;
    case AMBIENT_CRT: crt(out, frames, rate, &r, 1); break;
    case AMBIENT_COMPUTER: computer(out, frames, rate, &r); break;
    case AMBIENT_TELEPORTER: teleporter(out, frames, rate, &r); break;
    }
}

/* Breaking glass: a bright shatter, then the shards falling. Each shard
 * falls from where it was in the pane under DOOM's gravity and lands with a
 * tick: a scratch of bright noise, rung for a few milliseconds through a
 * handful of tight bands placed at random high up. No two shards share a
 * pitch or a set of overtones, and none rings long - a stack of steady
 * partials is a bell, not glass. A shard bounces with its own bands. */
#include <math.h>
#include <stdlib.h>
#include "impact_synth.h"
#include "synth_kit.h"

/* DOOM's gravity, one unit per tic per tic, in units per second squared. */
#define GRAVITY (35.0f * 35.0f)
#define MODES 10

typedef struct { int modes; float freq[MODES], q[MODES], gain[MODES]; } shard_t;

/* size 0 a splinter, 1 a big piece: bigger ones have more bands, a bit lower.
   They tick rather than ring. */
static shard_t shard_make(rng_t *r, int rate, float size)
{
    shard_t s;
    float top = fminf(9500, 0.45f * rate), bottom = top * (0.55f - 0.15f * size);
    s.modes = 6 + (int)(4 * size);
    for (int m = 0; m < s.modes; ++m)
    {
        s.freq[m] = rng_range(r, bottom, top);
        /* Q sets the ring: 30-90 at 4 kHz dies in 2-7 ms. */
        s.q[m] = rng_range(r, 30, 60 + 30 * size);
        float u = rng_unit(r);
        s.gain[m] = u * u;
    }
    return s;
}

/* A big piece hitting hard: lower, out-of-tune bands that clink for 20-60 ms. */
static shard_t clink_make(rng_t *r, int rate)
{
    shard_t s;
    float top = fminf(3400, 0.45f * rate);
    s.modes = 8;
    for (int m = 0; m < s.modes; ++m)
    {
        s.freq[m] = rng_range(r, 1200, top);
        s.q[m] = rng_range(r, 120, 300);
        float u = rng_unit(r);
        s.gain[m] = u * u;
    }
    return s;
}

/* A shard's tick at frame at; tonal 0 is only the scratch, 1 mostly ring. */
static void ping(float *out, int frames, int at, int rate, rng_t *r, const shard_t *s, float amp, float tonal)
{
    svf_t band[MODES];
    float weight[MODES], longest = 0;
    for (int m = 0; m < s->modes; ++m)
    {
        band[m] = svf_make(s->freq[m], s->q[m], rate);
        /* A tight band answers a tick more quietly; lift it to ring as loud. */
        weight[m] = s->gain[m] * fminf((s->q[m] / 45) * (5000 / s->freq[m]), 4);
        longest = fmaxf(longest, s->q[m] / (SYNTH_PI * s->freq[m]));
    }
    svf_t bright = svf_make(0.3f * rate, 0.7f, rate);
    float burst = rng_range(r, 0.0003f, 0.0009f) * rate, scratch = rng_range(r, 0.002f, 0.007f) * rate;
    float scratch_keep = expf(-1 / scratch), env = 1;
    int length = (int)(fminf(9.2f * longest + 0.008f, 0.6f) * rate);
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float noise = rng_noise(r), excite = n < burst ? noise : 0, ring = 0;
        for (int m = 0; m < s->modes; ++m)
        {
            svf_run(&band[m], excite);
            ring += weight[m] * band[m].band * band[m].k;
        }
        svf_run(&bright, noise * env);
        env *= scratch_keep;
        if (at + n >= 0) out[at + n] += amp * (tonal * 20.0f * ring + (1 - tonal) * 3.2f * bright.high);
    }
}

void Glass_Shards(float *out, int frames, int rate, unsigned seed, int count,
                  float sill, float pane, float spread, float amp, float tonal)
{
    rng_t r = rng_seed(seed);
    for (int i = 0; i < count; ++i)
    {
        float u = rng_unit(&r), size = u * u;
        shard_t shard = shard_make(&r, rate, size);
        float t;
        /* Some stay on the sill and land at once; the rest fall to the floor. */
        if (rng_unit(&r) < 0.2f) t = rng_range(&r, 0.004f, 0.05f);
        else t = rng_range(&r, 0, spread) + sqrtf(2 * (sill + rng_range(&r, 0, pane)) / GRAVITY);
        float level = amp * (0.35f + 0.65f * size) * rng_range(&r, 0.6f, 1.0f);
        ping(out, frames, (int)(t * rate), rate, &r, &shard, level, tonal);
        float hop = rng_range(&r, 0.06f, 0.14f);
        for (int bounces = (int)rng_range(&r, 0, 2.99f); bounces > 0; --bounces)
        {
            t += hop; hop *= rng_range(&r, 0.3f, 0.55f); level *= rng_range(&r, 0.25f, 0.45f);
            ping(out, frames, (int)(t * rate), rate, &r, &shard, level, tonal);
        }
    }
}

/* Tiny bits still settling after the rest: sparse ticks thinning out over
   about a second from start. */
static void debris(float *out, int frames, int rate, rng_t *r, float start, int count, float amp)
{
    for (int i = 0; i < count; ++i)
    {
        float t = start - 0.3f * logf(fmaxf(rng_unit(r), 1e-6f));
        shard_t bit = shard_make(r, rate, 0.1f * rng_unit(r));
        ping(out, frames, (int)(t * rate), rate, r, &bit, amp * rng_range(r, 0.2f, 1.0f), 0.7f);
    }
}

void Glass_Debris(float *out, int frames, int rate, unsigned seed, float start, int count, float amp)
{
    rng_t r = rng_seed(seed ^ 0x2545f491u);
    debris(out, frames, rate, &r, start, count, amp);
}

/* The pane's own body giving way: a hard whack, low for glass. */
static shard_t pane_make(rng_t *r, int rate)
{
    shard_t s;
    s.modes = 6;
    for (int m = 0; m < s.modes; ++m)
    {
        s.freq[m] = fminf(rng_range(r, 400, 1200), 0.45f * rate);
        s.q[m] = rng_range(r, 15, 40);
        s.gain[m] = rng_range(r, 0.3f, 1.0f);
    }
    return s;
}

/* Pieces knocking into each other as the pane comes apart: each struck again
   and again a few milliseconds apart, every knock softer. Fracture sound is
   mostly this, the pieces ringing just after the break (Zheng and James,
   "Rigid-Body Fracture Sound with Precomputed Soundbanks", 2010). */
static void rattle(float *out, int frames, int rate, rng_t *r, int pieces, float seconds, float amp)
{
    for (int i = 0; i < pieces; ++i)
    {
        shard_t piece = shard_make(r, rate, rng_range(r, 0.4f, 1.0f));
        float t = rng_range(r, 0, 0.4f * seconds), level = amp * rng_range(r, 0.5f, 1.0f);
        for (int hits = (int)rng_range(r, 3, 7.99f); hits > 0; --hits)
        {
            ping(out, frames, (int)(t * rate), rate, r, &piece, level, 0.8f);
            t += rng_range(r, 0.003f, 0.015f);
            level *= rng_range(r, 0.5f, 0.85f);
        }
    }
}

/* The shatter: the pane cracks all over at once. A crack of a few
   milliseconds and a knock of the pane giving way, then a rush of splinters
   breaking off, hundreds of ringing ticks thinning out over seconds, and a
   few bigger pieces clinking among them. The ringing makes it glass: a burst
   of plain noise here would only be another gunshot. Violence, 0-1, is the
   whack and the rattle on top, driven a little into saturation, as a real
   break clips the microphone. */
static void shatter(float *out, int frames, int rate, rng_t *r, float seconds, int clinks, float amp, float violence)
{
    int burst = (int)(0.25f * rate);
    float *hit = calloc((size_t)burst, sizeof(float));
    if (hit)
    {
        shard_t pane = pane_make(r, rate);
        ping(hit, burst, 0, rate, r, &pane, 0.9f * amp * violence, 0.9f);
        rattle(hit, burst, rate, r, 6 + (int)(6 * violence), seconds, 0.45f * amp * violence);
        for (int n = 0; n < burst && n < frames; ++n) out[n] += tanhf(1.6f * hit[n]) / 1.6f;
        free(hit);
    }
    add_thump(out, frames, 0, rate, 420, 180, 0.012f, 0.3f * amp);
    svf_t bright = svf_make(3500, 0.6f, rate);
    int length = (int)(0.02f * rate);
    for (int n = 0; n < length && n < frames; ++n)
    {
        float t = n / (float)rate, env = (1 - expf(-t / 0.0003f)) * expf(-t / 0.003f);
        svf_run(&bright, rng_noise(r));
        out[n] += amp * env * bright.high * 1.2f;
    }
    float t = 0, tau = 0.8f * seconds;
    for (;;)
    {
        float density = 2500 * expf(-t / tau);
        if (density < 20) break;
        t += -logf(fmaxf(rng_unit(r), 1e-6f)) / density;
        float u = rng_unit(r), fade = sqrtf(expf(-t / tau));
        shard_t splinter = shard_make(r, rate, 0.3f * u * u * u);
        ping(out, frames, (int)(t * rate), rate, r, &splinter, 0.3f * amp * fade * (0.3f + 0.7f * u * u), 0.85f);
    }
    for (int i = 0; i < clinks; ++i)
    {
        shard_t piece = clink_make(r, rate);
        ping(out, frames, (int)(rng_range(r, 0.002f, 0.6f * seconds) * rate), rate, r, &piece,
             0.35f * amp * rng_range(r, 0.5f, 1.0f), 0.9f);
    }
}

void Glass_Render(int kind, unsigned seed, int rate, float *out, int frames)
{
    rng_t r = rng_seed(seed);
    switch (kind)
    {
    case IMPACT_GLASS_TINKLE:
        Glass_Shards(out, frames, rate, seed, 9, 32, 48, 0.03f, 0.4f, 0.6f);
        debris(out, frames, rate, &r, 0.3f, 5, 0.15f);
        break;
    case IMPACT_GLASS_SHATTER:
        shatter(out, frames, rate, &r, 0.12f, 3, 0.65f, 0.8f);
        break;
    case IMPACT_GLASS_BREAK:
        shatter(out, frames, rate, &r, 0.12f, 3, 0.65f, 0.8f);
        Glass_Shards(out, frames, rate, seed, 16, 40, 64, 0.06f, 0.35f, 0.6f);
        debris(out, frames, rate, &r, 0.35f, 10, 0.15f);
        break;
    case IMPACT_GLASS_BIG:
        shatter(out, frames, rate, &r, 0.18f, 5, 0.7f, 1.0f);
        Glass_Shards(out, frames, rate, seed, 30, 48, 128, 0.12f, 0.32f, 0.6f);
        debris(out, frames, rate, &r, 0.45f, 18, 0.15f);
        break;
    }
}

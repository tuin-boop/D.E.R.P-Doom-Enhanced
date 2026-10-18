/* Small pieces the made-up effects share: a seeded noise source, a resonant
 * filter, ringing partials and the crackle of sparks. Header-only so each
 * synth file stays self-contained. */
#ifndef DOOM_AUDIO_SYNTH_KIT_H
#define DOOM_AUDIO_SYNTH_KIT_H
#include <math.h>

#define SYNTH_PI 3.14159265358979323846f

typedef struct { unsigned state; } rng_t;

static inline rng_t rng_seed(unsigned seed)
{
    rng_t r = { seed * 2654435761u ^ 0x9e3779b9u };
    if (!r.state) r.state = 1;
    return r;
}
/* 0..1. */
static inline float rng_unit(rng_t *r)
{
    r->state ^= r->state << 13; r->state ^= r->state >> 17; r->state ^= r->state << 5;
    return (r->state >> 8) * (1.0f / 16777216.0f);
}
static inline float rng_range(rng_t *r, float lo, float hi) { return lo + (hi - lo) * rng_unit(r); }
static inline float rng_noise(rng_t *r) { return 2 * rng_unit(r) - 1; }

/* A state-variable filter (Simper's trapezoidal form): band, low and high at once. */
typedef struct { float a1, a2, a3, k, ic1, ic2, low, band, high; } svf_t;

static inline void svf_tune(svf_t *f, float freq, float q, int rate)
{
    if (freq > 0.45f * rate) freq = 0.45f * rate;
    float g = tanf(SYNTH_PI * freq / rate);
    f->k = 1 / q;
    f->a1 = 1 / (1 + g * (g + f->k)); f->a2 = g * f->a1; f->a3 = g * f->a2;
}
static inline svf_t svf_make(float freq, float q, int rate)
{
    svf_t f = {0};
    svf_tune(&f, freq, q, rate);
    return f;
}
static inline void svf_run(svf_t *f, float x)
{
    float v3 = x - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2 * v1 - f->ic1; f->ic2 = 2 * v2 - f->ic2;
    f->band = v1; f->low = v2; f->high = x - f->k * v1 - v2;
}

/* Adds a decaying sine from frame at; stops once it is inaudible. Partials
   past the rate's Nyquist frequency are left out, as the sample can't hold them. */
static inline void add_partial(float *out, int frames, int at, int rate,
                               float freq, float amp, float seconds, float phase)
{
    if (freq >= 0.47f * rate || at >= frames) return;
    float w = 2 * SYNTH_PI * freq / rate, keep = expf(-1.0f / (seconds * rate));
    int length = (int)(seconds * rate * 9.2f);  /* down 80 dB */
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        if (at + n >= 0) out[at + n] += amp * sinf(w * n + phase);
        amp *= keep;
    }
}

/* One spark crack: a short burst of noise rung through a band, 0.4-2.5 ms. */
static inline void add_click(float *out, int frames, int at, int rate, rng_t *r,
                             float amp, float lo, float hi)
{
    svf_t f = svf_make(rng_range(r, lo, hi), rng_range(r, 1.2f, 3.5f), rate);
    float burst = rng_range(r, 0.0004f, 0.0025f) * rate, keep = expf(-1 / (0.35f * burst));
    float env = 1;
    int length = (int)(burst + 0.004f * rate);
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float x = n < burst ? rng_noise(r) * env : 0;
        env *= keep;
        svf_run(&f, x);
        if (at + n >= 0) out[at + n] += amp * f.band * 2.5f;
    }
}

/* Cracks arriving at random, thick at first, thinning out over tau seconds,
   until the given second; each is louder or quieter at random. */
static inline void add_crackle(float *out, int frames, int rate, rng_t *r, float start, float end,
                               float per_second, float tau, float amp, float lo, float hi)
{
    float t = start;
    for (;;)
    {
        float density = per_second * expf(-(t - start) / tau);
        if (density < 4) break;
        t += -logf(fmaxf(rng_unit(r), 1e-6f)) / density;
        if (t >= end) break;
        float fade = sqrtf(expf(-(t - start) / tau)), u = rng_unit(r);
        add_click(out, frames, (int)(t * rate), rate, r, amp * fade * (0.25f + 0.75f * u * u), lo, hi);
    }
}

/* A dull low thump: a sine sweeping down, for a lamp's last pop or a tube going off. */
static inline void add_thump(float *out, int frames, int at, int rate, float from, float to,
                             float seconds, float amp)
{
    float phase = 0;
    int length = (int)(seconds * rate * 5);
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float t = n / (float)rate, freq = to + (from - to) * expf(-t / (0.5f * seconds));
        phase += 2 * SYNTH_PI * freq / rate;
        float env = (1 - expf(-t / 0.002f)) * expf(-t / seconds);
        if (at + n >= 0) out[at + n] += amp * env * sinf(phase);
    }
}
/* A rising "bloop": a bubble's ring, its pitch climbing as it nears the
   surface (van den Doel's bubble model). rise 0.1 is water, more is thicker;
   linger stretches the ring past water's damping. */
static inline void add_bubble(float *out, int frames, int at, int rate, float freq, float amp,
                              float rise, float linger)
{
    float damping = (0.13f * freq + 0.0072f * powf(freq, 1.5f)) / linger, phase = 0;
    int length = (int)(9.2f / damping * rate);
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float t = n / (float)rate, f = freq * (1 + rise * freq * t);
        if (f >= 0.45f * rate) break;
        phase += 2 * SYNTH_PI * f / rate;
        float env = (1 - expf(-t / 0.001f)) * expf(-damping * t);
        if (at + n >= 0) out[at + n] += amp * env * sinf(phase);
    }
}

/* A value wandering between random targets lo..hi, a new one every
   lo_seconds..hi_seconds, eased between them. */
typedef struct { float from, to, lo, hi, span_lo, span_hi; int at, span; } drift_t;

static inline drift_t drift_make(rng_t *r, float lo, float hi, float span_lo, float span_hi, int rate)
{
    drift_t d = { rng_range(r, lo, hi), rng_range(r, lo, hi), lo, hi, span_lo * rate, span_hi * rate, 0, 1 };
    d.span = (int)rng_range(r, d.span_lo, d.span_hi) + 1;
    return d;
}
static inline float drift_next(drift_t *d, rng_t *r)
{
    if (++d->at >= d->span)
    {
        d->from = d->to; d->to = rng_range(r, d->lo, d->hi);
        d->at = 0; d->span = (int)rng_range(r, d->span_lo, d->span_hi) + 1;
    }
    float x = d->at / (float)d->span;
    return d->from + (d->to - d->from) * (0.5f - 0.5f * cosf(SYNTH_PI * x));
}
#endif

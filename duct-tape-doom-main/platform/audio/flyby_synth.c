/* A fluttering whistle with a little air underneath. The whistle is the
 * source, rather than a faint tone riding on swept noise. Its pitch drops
 * modestly and its level swells in a few rounded packets as it passes;
 * nearby tones and a little pitch wobble roughen it without a spray of hiss.
 * The upper tones leave room below the 11 kHz sample's Nyquist frequency. */
#include <math.h>
#include "impact_synth.h"
#include "synth_kit.h"

void Flyby_Render(unsigned seed, int rate, float *out, int frames)
{
    rng_t r = rng_seed(seed);
    float pass = rng_range(&r, 0.018f, 0.026f);    /* the first, strongest packet */
    float high = rng_range(&r, 4050, 4450), low = high * rng_range(&r, 0.82f, 0.88f);
    float sweep = rng_range(&r, 0.035f, 0.056f);
    float before = rng_range(&r, 0.012f, 0.018f), after = rng_range(&r, 0.062f, 0.075f);
    float flutter = rng_range(&r, 32, 40);
    float upper = rng_range(&r, 1.006f, 1.011f), lower = rng_range(&r, 0.989f, 0.994f);
    float phase = rng_range(&r, 0, 2 * SYNTH_PI);
    float phase_up = rng_range(&r, 0, 2 * SYNTH_PI), phase_down = rng_range(&r, 0, 2 * SYNTH_PI);
    drift_t wobble = drift_make(&r, -0.006f, 0.006f, 0.008f, 0.018f, rate);
    svf_t air = svf_make(high, 2.5f, rate);
    for (int n = 0; n < frames; ++n)
    {
        float t = n / (float)rate, from = (t - pass) / sweep;
        float shift = 0.5f * (1 - tanhf(from));
        float freq = fminf((low + (high - low) * shift) * (1 + drift_next(&wobble, &r)), 0.42f * rate);
        float d = (t - pass) / (t < pass ? before : after), env = expf(-d * d);
        env *= 1 - expf(-t / 0.002f);
        /* One real cycle per flutter: the old abs(sin) doubled the buzz's
           rate. Rounded packets about 30 ms apart keep a whistle between. */
        float pulse = 0.5f + 0.5f * cosf(2 * SYNTH_PI * flutter * (t - pass));
        float flutter_gain = 0.12f + 0.88f * pulse * pulse;
        phase += 2 * SYNTH_PI * freq / rate;
        phase_up += 2 * SYNTH_PI * freq * upper / rate;
        phase_down += 2 * SYNTH_PI * freq * lower / rate;
        float tone = 0.44f * sinf(phase) + 0.07f * sinf(phase_up) + 0.035f * sinf(phase_down);
        svf_tune(&air, freq, 2.5f, rate);
        svf_run(&air, rng_noise(&r));
        /* Stay below the shared 8-bit converter's 0.6 soft knee: distorting
           a high whistle would fold its harmonics into a low, raspy sound. */
        out[n] += env * (flutter_gain * tone + 0.035f * tanhf(air.band));
    }
}

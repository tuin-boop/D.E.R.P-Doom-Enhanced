/* Effects are mostly 11 kHz and play at 44.1 kHz. Holding each sample
 * leaves copies of the sound mirrored above the sample's Nyquist frequency,
 * heard as a metallic sizzle; the cubic softens them, the sinc removes them.
 * Neither adds treble the sample never had. */
#include <math.h>
#include "resample.h"

#define PI 3.14159265358979323846
#define TAPS 32
#define PHASES 256
/* The sinc passes up to this fraction of the sample's Nyquist frequency;
   the Blackman window rolls off around it. */
#define CUTOFF 0.9

/* One row of taps per fraction of a sample, both ends included. */
static float kernel[PHASES + 1][TAPS];

static float at(const unsigned char *data, int length, int i)
{
    return i >= 0 && i < length ? ((int)data[i] - 128) / 128.0f : 0;
}

static float float_at(const float *data,int length,int channels,int channel,int i)
{return i>=0&&i<length?data[i*channels+channel]:0;}
float Resample_Float(const float *data,int length,int channels,int channel,double position,int mode)
{
    int i=(int)position;float f=(float)(position-i);
    if(mode==RESAMPLE_SMOOTH) {
        float a=float_at(data,length,channels,channel,i-1),b=float_at(data,length,channels,channel,i);
        float c=float_at(data,length,channels,channel,i+1),d=float_at(data,length,channels,channel,i+2);
        return b+0.5f*f*(c-a+f*(2*a-5*b+4*c-d+f*(3*(b-c)+d-a)));
    }
    if(mode==RESAMPLE_CLEAN) {
        const float *taps=kernel[(int)(f*PHASES+0.5f)];float sum=0;int first=i-(TAPS/2-1);
        for(int t=0;t<TAPS;++t)sum+=float_at(data,length,channels,channel,first+t)*taps[t];
        return sum;
    }
    return float_at(data,length,channels,channel,i);
}

void Resample_Init(void)
{
    for (int p = 0; p <= PHASES; ++p)
    {
        double sum = 0;
        for (int t = 0; t < TAPS; ++t)
        {
            /* From the point read to tap t, which sits at sample i - 15 + t. */
            double x = t - (TAPS / 2 - 1) - p / (double)PHASES;
            double arg = PI * CUTOFF * x;
            double sinc = x == 0 ? 1 : sin(arg) / arg;
            double w = (x + TAPS / 2) / TAPS;
            double window = 0.42 - 0.5 * cos(2 * PI * w) + 0.08 * cos(4 * PI * w);
            kernel[p][t] = (float)(sinc * window);
            sum += kernel[p][t];
        }
        /* Unit gain at every fraction, so steady levels don't ripple. */
        for (int t = 0; t < TAPS; ++t) kernel[p][t] = (float)(kernel[p][t] / sum);
    }
}

/* The same three readings over floats, for a mix made at a sample's rate. */
float Resample_AtFloat(const float *data, int length, double position, int mode)
{
    int i = (int)position;
    float f = (float)(position - i);
#define AT(n) ((n) >= 0 && (n) < length ? data[n] : 0.0f)
    if (mode == RESAMPLE_SMOOTH)
    {
        float a = AT(i - 1), b = AT(i), c = AT(i + 1), d = AT(i + 2);
        return b + 0.5f * f * (c - a + f * (2 * a - 5 * b + 4 * c - d + f * (3 * (b - c) + d - a)));
    }
    if (mode == RESAMPLE_CLEAN)
    {
        const float *taps = kernel[(int)(f * PHASES + 0.5f)];
        int first = i - (TAPS / 2 - 1);
        float sum = 0;
        if (first >= 0 && first + TAPS <= length)
        {
            const float *s = data + first;
            for (int t = 0; t < TAPS; ++t) sum += s[t] * taps[t];
            return sum;
        }
        for (int t = 0; t < TAPS; ++t) sum += AT(first + t) * taps[t];
        return sum;
    }
    return AT(i);
#undef AT
}

float Resample_At(const unsigned char *data, int length, double position, int mode)
{
    int i = (int)position;
    float f = (float)(position - i);
    if (mode == RESAMPLE_SMOOTH)
    {
        /* Catmull-Rom through the two samples either side. */
        float a = at(data, length, i - 1), b = at(data, length, i);
        float c = at(data, length, i + 1), d = at(data, length, i + 2);
        return b + 0.5f * f * (c - a + f * (2 * a - 5 * b + 4 * c - d + f * (3 * (b - c) + d - a)));
    }
    if (mode == RESAMPLE_CLEAN)
    {
        const float *taps = kernel[(int)(f * PHASES + 0.5f)];
        int first = i - (TAPS / 2 - 1);
        float sum = 0;
        if (first >= 0 && first + TAPS <= length)
        {
            const unsigned char *s = data + first;
            for (int t = 0; t < TAPS; ++t) sum += ((int)s[t] - 128) * taps[t];
            return sum / 128.0f;
        }
        for (int t = 0; t < TAPS; ++t) sum += at(data, length, first + t) * taps[t];
        return sum;
    }
    return at(data, length, i);
}

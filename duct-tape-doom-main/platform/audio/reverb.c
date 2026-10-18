/* Plays a room_t. The early echoes read a history of the send at each wall's
 * round trip; they feed a small feedback delay network (eight lines mixed by
 * a Householder matrix, each line damped) that makes the tail. Both are kept
 * a little dark and lo-fi, like late-90s positional audio, not a lush hall.
 * Room changes crossfade the echoes and glide the tail, so walking through a
 * door never clicks or warbles. */
#include <math.h>
#include <string.h>
#include "reverb.h"

#define HISTORY 32768   /* longer than the farthest echo, at up to 48 kHz */
#define LINES 8
#define LINE_MAX 4096
#define FADE_SECONDS 0.04f
#define GLIDE_SECONDS 0.3f

static const float line_ms[LINES] = {29.7f, 37.1f, 41.1f, 43.7f, 53.1f, 59.9f, 67.3f, 73.1f};

typedef struct { int delay; float left, right; } echo_t;
typedef struct { echo_t from, to, next; int fading, queued; float fade; } tap_t;

static int rate = 44100;
static float history[HISTORY]; static unsigned written;
static tap_t taps[ROOM_TAPS];
static float lines[LINES][LINE_MAX], line_lowpass[LINES]; static int line_length[LINES], line_at[LINES];
static float send_lowpass, decay = 0.2f, damping, late, target_decay = 0.2f, target_damping, target_late;

void Reverb_Init(int output_rate)
{
    rate = output_rate;
    for (int i = 0; i < LINES; ++i)
    {
        line_length[i] = (int)(line_ms[i] * 0.001f * rate);
        if (line_length[i] >= LINE_MAX) line_length[i] = LINE_MAX - 1;
    }
    Reverb_Clear();
}
void Reverb_Clear(void)
{
    memset(history, 0, sizeof(history));
    memset(lines, 0, sizeof(lines)); memset(line_lowpass, 0, sizeof(line_lowpass));
    send_lowpass = 0;
}

static echo_t echo(const room_t *room, int i)
{
    int delay = (int)(room->delay[i] * rate);
    if (delay < 1) delay = 1;
    if (delay >= HISTORY) delay = HISTORY - 1;
    float pan = room->pan[i] < -1 ? -1 : room->pan[i] > 1 ? 1 : room->pan[i];
    return (echo_t){delay, room->gain[i] * sqrtf(0.5f * (1 - pan)), room->gain[i] * sqrtf(0.5f * (1 + pan))};
}
void Reverb_SetRoom(const room_t *room)
{
    for (int i = 0; i < ROOM_TAPS; ++i)
    {
        tap_t *tap = &taps[i];
        echo_t target = echo(room, i);
        if (tap->fading) { tap->next = target; tap->queued = 1; }
        else { tap->to = target; tap->fading = 1; tap->fade = 0; }
    }
    target_decay = room->decay; target_damping = room->damping; target_late = room->late;
}

static float read_history(int delay) { return history[(written - (unsigned)delay) & (HISTORY - 1)]; }

void Reverb_Process(const float *send, float *stereo, int frames, float amount)
{
    float glide = 1 - expf(-frames / (GLIDE_SECONDS * rate));
    decay += (target_decay - decay) * glide;
    damping += (target_damping - damping) * glide;
    late += (target_late - late) * glide;
    float gain[LINES];
    for (int j = 0; j < LINES; ++j) gain[j] = powf(10, -3 * line_length[j] / (decay * rate));
    /* Each bounce keeps less treble in dark rooms: 7 kHz for metal, 1.5 kHz for wood. */
    float cutoff = 7000 - 5500 * damping;
    float keep = expf(-2 * (float)M_PI * cutoff / rate);
    /* The send loses its upsampling hiss before it echoes. */
    float input_keep = expf(-2 * (float)M_PI * 5000 / rate);
    float fade_step = 1 / (FADE_SECONDS * rate);
    float tail_level = late * 1.1f;

    for (int i = 0; i < frames; ++i)
    {
        send_lowpass = send[i] + (send_lowpass - send[i]) * input_keep;
        history[written & (HISTORY - 1)] = send_lowpass;

        float early_left = 0, early_right = 0;
        for (int t = 0; t < ROOM_TAPS; ++t)
        {
            tap_t *tap = &taps[t];
            float old = read_history(tap->from.delay);
            if (!tap->fading) { early_left += old * tap->from.left; early_right += old * tap->from.right; continue; }
            float now = read_history(tap->to.delay), f = tap->fade;
            early_left += old * tap->from.left * (1 - f) + now * tap->to.left * f;
            early_right += old * tap->from.right * (1 - f) + now * tap->to.right * f;
            if ((tap->fade += fade_step) >= 1)
            {
                tap->from = tap->to;
                tap->fading = tap->queued; tap->fade = 0;
                if (tap->queued) { tap->to = tap->next; tap->queued = 0; }
            }
        }

        /* The tail grows from the echoes, plus a little of the sound itself. */
        float input = 0.3f * (early_left + early_right) + 0.12f * send_lowpass;
        float out[LINES], sum = 0, tail_left = 0, tail_right = 0;
        for (int j = 0; j < LINES; ++j)
        {
            float value = lines[j][line_at[j]];
            line_lowpass[j] = value + (line_lowpass[j] - value) * keep + 1e-18f - 1e-18f;
            out[j] = line_lowpass[j] * gain[j];
            sum += out[j];
            if (j & 1) tail_right += (j & 2 ? -1 : 1) * line_lowpass[j];
            else tail_left += (j & 2 ? -1 : 1) * line_lowpass[j];
        }
        sum *= 2.0f / LINES;
        for (int j = 0; j < LINES; ++j)
        {
            lines[j][line_at[j]] = out[j] - sum + input;
            if (++line_at[j] >= line_length[j]) line_at[j] = 0;
        }
        ++written;
        stereo[2 * i] += amount * (early_left + tail_level * tail_left);
        stereo[2 * i + 1] += amount * (early_right + tail_level * tail_right);
    }
}

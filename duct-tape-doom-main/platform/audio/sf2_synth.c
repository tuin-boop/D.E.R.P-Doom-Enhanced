/* Score events become General MIDI channel messages for TinySoundFont.
   Scores arrive in MUS channel numbers (percussion on 15); the bank wants
   MIDI ones (percussion on 9), so the two swap back. */
#include <stdlib.h>
#define TSF_IMPLEMENTATION
#include "tsf.h"
#include "sf2_synth.h"

#define MIDI_PERCUSSION		9
#define DEFAULT_VOLUME		100	/* General MIDI's power-on channel volume */

static tsf*	bank;
static boolean	mono[SCORE_CHANNELS];
static int	peak_voices;

static int midi_channel (int channel)
{
    return channel == SCORE_PERCUSSION ? MIDI_PERCUSSION
	 : channel == MIDI_PERCUSSION ? SCORE_PERCUSSION : channel;
}

/* General MIDI's reset: bend and expression go back, but volume, pan
   and program stay. TinySoundFont's own reset would clear those too. */
static void reset_controllers (int ch)
{
    tsf_channel_set_pitchwheel (bank, ch, 8192);
    tsf_channel_midi_control (bank, ch, CTRL_EXPRESSION, 127);
    tsf_channel_set_sustain (bank, ch, 0);
}

static void reset (void)
{
    if (!bank)
	return;
    tsf_reset (bank);
    for (int ch = 0; ch < SCORE_CHANNELS; ++ch)
    {
	tsf_channel_midi_control (bank, ch, 0, 0);	/* bank select */
	tsf_channel_set_presetnumber (bank, ch, 0, ch == MIDI_PERCUSSION);
	tsf_channel_midi_control (bank, ch, CTRL_VOLUME, DEFAULT_VOLUME);
	tsf_channel_midi_control (bank, ch, CTRL_PAN, 64);
	reset_controllers (ch);
	mono[ch] = false;
    }
    peak_voices = 0;
}

static void controller (int ch, int control, int value)
{
    switch (control)
    {
      case CTRL_RESET:
	reset_controllers (ch);
	break;
      case CTRL_MONO:
	mono[ch] = true;
	break;
      case CTRL_POLY:
	mono[ch] = false;
	break;
      default:
	tsf_channel_midi_control (bank, ch, control, value);
	break;
    }
}

static void event (const score_event_t* e)
{
    int ch = midi_channel (e->channel);

    switch (e->type)
    {
      case EV_NOTE_OFF:
	tsf_channel_note_off (bank, ch, e->data1);
	break;
      case EV_NOTE_ON:
	if (!e->data2)
	    tsf_channel_note_off (bank, ch, e->data1);
	else
	{
	    if (mono[ch])
		tsf_channel_note_off_all (bank, ch);
	    tsf_channel_note_on (bank, ch, e->data1, e->data2 / 127.0f);
	}
	break;
      case EV_CONTROLLER:
	controller (ch, e->data1, e->data2);
	break;
      case EV_PROGRAM:
	tsf_channel_set_presetnumber (bank, ch, e->data1, ch == MIDI_PERCUSSION);
	break;
      case EV_BEND:
	tsf_channel_set_pitchwheel (bank, ch, e->data2 << 7 | e->data1);
	break;
    }
}

/* TinySoundFont's own volume reaches only notes that start after it is
   set, so the bank plays at unity and the gain is applied here. */
static void mix (float* stereo, int frames, float gain)
{
    float chunk[2 * 256];
    int voices;

    if (!bank)
	return;
    voices = tsf_active_voice_count (bank);	/* most right after the events */
    if (voices > peak_voices)
	peak_voices = voices;
    while (frames > 0)
    {
	int count = frames < 256 ? frames : 256;
	tsf_render_float (bank, chunk, count, 0);
	for (int i = 0; i < 2 * count; ++i)
	    stereo[i] += chunk[i] * gain;
	stereo += 2 * count;
	frames -= count;
    }
}

const music_synth_t SF2_Synth = { reset, event, mix };

sf2_bank_t* SF2_Open (const char* path, int rate, int max_voices)
{
    tsf* opened = tsf_load_filename (path);

    if (!opened)
	return NULL;
    tsf_set_output (opened, TSF_STEREO_INTERLEAVED, rate, 0);
    if (max_voices && !tsf_set_max_voices (opened, max_voices))
    {
	tsf_close (opened);
	return NULL;
    }
    return opened;
}

void SF2_Close (sf2_bank_t* closing)
{
    if (closing)
	tsf_close (closing);
}

sf2_bank_t* SF2_Use (sf2_bank_t* using)
{
    tsf* last = bank;

    bank = using;
    reset ();	/* also creates all 16 channels before any rendering */
    return last;
}

int SF2_PeakVoices (void) { return peak_voices; }

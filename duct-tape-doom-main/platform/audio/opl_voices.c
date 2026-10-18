/* Channels and voices, following opl_music_spec.md. A channel is only its
   last controller values; everything the chip needs is worked out from
   them when a register is written. Each of the 18 chip channels is one
   voice. A voice remembers the age counter's value at its last note-on or
   release: a free voice is taken by the longest released (one holding the
   note's patch first), and a held one is stolen by the rule in
   opl_voices.h. */
#include "opl_chip.h"
#include "opl_voices.h"

/* GENMIDI's patches are voiced an octave up: 78 of Doom's 128 run their
   carrier at half the channel frequency (MULT 0) or carry a -12 base note,
   so the chip is set an octave above the note to land them at its pitch. */
#define VOICING_OCTAVE	12

typedef struct
{
    byte	program, volume, expression, pan, bend;	/* 7-bit, as sent */
    boolean	mono;
} channel_t;

static const channel_t channel_defaults = { 0, 100, 127, 64, 64, false };

typedef struct
{
    int		bank, slot;		/* which chip channel */
    boolean	held;
    unsigned	age;
    int		channel, key, velocity;
    int		note;			/* the note asked of the patch */
    const genmidi_instrument_t*	instrument;
    int		patch;			/* 0, or 1 for a double voice's second */
    const genmidi_patch_t*	loaded;	/* what the registers hold */
    int		pitch_reg;		/* last 0xB0 value without the key bit */
} voice_t;

/* Patch bytes that go to a register unchanged, and where. */
static const struct { int byte, reg; } direct[] = {
    { PATCH_CHARACTER, 0x20 }, { PATCH_ENVELOPE, 0x60 },
    { PATCH_SUSTAIN, 0x80 }, { PATCH_WAVEFORM, 0xE0 },
};

static const genmidi_instrument_t*	bank;
static channel_t	channels[SCORE_CHANNELS];
static voice_t		voices[OPL_VOICES];
static unsigned		age;
static opl_steal_t	steal_rule = STEAL_HIGHEST;
static int		steals, dropped_layers;

static const genmidi_patch_t* patch_of (const voice_t* v)
{
    return &v->instrument->patch[v->patch];
}

static void write_reg (const voice_t* v, int reg, int value)
{
    OPLChip_Write (v->bank, reg, value);
}

/* Register offset of the voice's modulator (0) or carrier (1). */
static int operator_of (const voice_t* v, int carrier)
{
    return OPLChip_Operator (v->slot) + 3 * carrier;
}

static void write_level (const voice_t* v, int carrier, int loss)
{
    const byte* op = patch_of (v)->bytes + (carrier ? PATCH_CARRIER : PATCH_MODULATOR);
    int level = (op[PATCH_LEVEL] & 63) + loss;

    write_reg (v, 0x40 + operator_of (v, carrier),
	       (op[PATCH_KEY_SCALE] & 0xC0) | (level > OPL_MAX_LEVEL ? OPL_MAX_LEVEL : level));
}

static void write_levels (voice_t* v)
{
    const channel_t* ch = &channels[v->channel];
    int loss = OPLChip_Loss (v->velocity, ch->volume, ch->expression);
    boolean additive = patch_of (v)->bytes[PATCH_FEEDBACK] & 1;

    write_level (v, 0, additive ? loss : 0);	/* else it only shapes the timbre */
    write_level (v, 1, loss);
}

/* Register 0xC0: feedback and connection from the patch, sides from pan. */
static void write_sides (voice_t* v)
{
    int pan = channels[v->channel].pan;
    int sides = pan < 32 ? 0x10 : pan > 95 ? 0x20 : 0x30;

    write_reg (v, 0xC0 + v->slot, (patch_of (v)->bytes[PATCH_FEEDBACK] & 0x0F) | sides);
}

static void write_pitch (voice_t* v)
{
    const genmidi_patch_t* p = patch_of (v);
    int pitch = 32 * (v->note + GENMIDI_BaseNote (p) + VOICING_OCTAVE);
    int block, fnum;

    if (v->patch == 1)
	pitch += v->instrument->tuning - 128;
    if (!(v->instrument->flags & GENMIDI_FIXED))
	pitch += channels[v->channel].bend - 64;	/* 64 steps = 2 semitones */
    OPLChip_Pitch (pitch, &block, &fnum);
    v->pitch_reg = block << 2 | fnum >> 8;
    write_reg (v, 0xA0 + v->slot, fnum & 0xFF);
    write_reg (v, 0xB0 + v->slot, (v->held ? 0x20 : 0) | v->pitch_reg);
}

static void load_patch (voice_t* v)
{
    const genmidi_patch_t* p = patch_of (v);

    if (v->loaded == p)
	return;
    for (int carrier = 0; carrier < 2; ++carrier)
    {
	const byte* op = p->bytes + (carrier ? PATCH_CARRIER : PATCH_MODULATOR);
	for (int i = 0; i < (int)(sizeof(direct) / sizeof(direct[0])); ++i)
	    write_reg (v, direct[i].reg + operator_of (v, carrier), op[direct[i].byte]);
    }
    v->loaded = p;
}

static void release (voice_t* v)
{
    v->held = false;
    v->age = ++age;
    write_reg (v, 0xB0 + v->slot, v->pitch_reg);
}

/* Cuts a voice's sound at once: full attenuation, fastest release. */
static void silence (voice_t* v)
{
    for (int carrier = 0; carrier < 2; ++carrier)
    {
	write_reg (v, 0x40 + operator_of (v, carrier), OPL_MAX_LEVEL);
	write_reg (v, 0x80 + operator_of (v, carrier), 0x0F);
    }
    release (v);
    v->loaded = NULL;
}

/* The longest released voice, preferring one that already holds the patch:
   that saves reloading it, eight register writes through the slow queue. */
static voice_t* free_voice (const genmidi_patch_t* patch)
{
    voice_t* best = NULL;
    for (int i = 0; i < OPL_VOICES; ++i)
    {
	voice_t* v = &voices[i];
	if (v->held)
	    continue;
	if (!best || (v->loaded == patch) > (best->loaded == patch)
	    || ((v->loaded == patch) == (best->loaded == patch) && v->age < best->age))
	    best = v;
    }
    return best;
}

/* Higher gives up its voice first. */
static int steal_rank (const voice_t* v)
{
    if (v->patch == 1)
	return SCORE_CHANNELS + 1;
    if (steal_rule == STEAL_SPARE_DRUMS && v->channel == SCORE_PERCUSSION)
	return -1;
    return v->channel;
}

static voice_t* steal_voice (void)
{
    voice_t* best = &voices[0];
    for (int i = 1; i < OPL_VOICES; ++i)
    {
	int rank = steal_rank (&voices[i]), best_rank = steal_rank (best);
	if (rank > best_rank || (rank == best_rank && voices[i].age < best->age))
	    best = &voices[i];
    }
    ++steals;
    release (best);
    return best;
}

static void start_voice (voice_t* v, int channel, int key, int velocity,
			 const genmidi_instrument_t* instrument, int patch)
{
    v->held = true;
    v->age = ++age;
    v->channel = channel;
    v->key = key;
    v->velocity = velocity;
    v->instrument = instrument;
    v->patch = patch;
    v->note = instrument->flags & GENMIDI_FIXED ? instrument->fixed_note : key;
    load_patch (v);
    write_levels (v);
    write_sides (v);
    write_pitch (v);
}

static void note_off (int channel, int key)
{
    for (int i = 0; i < OPL_VOICES; ++i)
	if (voices[i].held && voices[i].channel == channel && voices[i].key == key)
	    release (&voices[i]);
}

static void note_on (int channel, int key, int velocity)
{
    const genmidi_instrument_t* instrument = &bank[channels[channel].program];
    voice_t* v;

    if (!velocity)
    {
	note_off (channel, key);
	return;
    }
    if (channel == SCORE_PERCUSSION)
    {
	if (key < GENMIDI_FIRST_DRUM || key >= GENMIDI_FIRST_DRUM + GENMIDI_PERCUSSION)
	    return;
	instrument = &bank[GENMIDI_MELODIC + key - GENMIDI_FIRST_DRUM];
    }
    if (channels[channel].mono)
	for (int i = 0; i < OPL_VOICES; ++i)
	    if (voices[i].held && voices[i].channel == channel)
		release (&voices[i]);
    v = free_voice (&instrument->patch[0]);
    start_voice (v ? v : steal_voice (), channel, key, velocity, instrument, 0);
    if (instrument->flags & GENMIDI_DUAL)
    {
	if ((v = free_voice (&instrument->patch[1])))
	    start_voice (v, channel, key, velocity, instrument, 1);
	else
	    ++dropped_layers;
    }
}

/* Applies a change in channel state to the notes it holds. */
static void refresh (int channel, void (*write) (voice_t*))
{
    for (int i = 0; i < OPL_VOICES; ++i)
	if (voices[i].held && voices[i].channel == channel)
	    write (&voices[i]);
}

static void controller (int channel, int control, int value)
{
    channel_t* ch = &channels[channel];

    switch (control)
    {
      case CTRL_VOLUME:
	ch->volume = value;
	refresh (channel, write_levels);
	break;
      case CTRL_EXPRESSION:
	ch->expression = value;
	refresh (channel, write_levels);
	break;
      case CTRL_PAN:
	ch->pan = value;
	refresh (channel, write_sides);
	break;
      case CTRL_SOUNDS_OFF:
	for (int i = 0; i < OPL_VOICES; ++i)
	    if (voices[i].channel == channel)
		silence (&voices[i]);
	break;
      case CTRL_NOTES_OFF:
	refresh (channel, release);
	break;
      case CTRL_RESET:	/* General MIDI: volume, pan and program stay */
	ch->bend = channel_defaults.bend;
	ch->expression = channel_defaults.expression;
	refresh (channel, write_pitch);
	refresh (channel, write_levels);
	break;
      case CTRL_MONO:
	ch->mono = true;
	break;
      case CTRL_POLY:
	ch->mono = false;
	break;
    }
}

void OPLVoices_Event (const score_event_t* event)
{
    int channel = event->channel;

    switch (event->type)
    {
      case EV_NOTE_OFF:
	note_off (channel, event->data1);
	break;
      case EV_NOTE_ON:
	note_on (channel, event->data1, event->data2);
	break;
      case EV_CONTROLLER:
	controller (channel, event->data1, event->data2);
	break;
      case EV_PROGRAM:
	channels[channel].program = event->data1 & 127;
	break;
      case EV_BEND:
	channels[channel].bend = event->data2;
	refresh (channel, write_pitch);
	break;
    }
}

void OPLVoices_Reset (void)
{
    for (int i = 0; i < OPL_VOICES; ++i)
	silence (&voices[i]);
    for (int c = 0; c < SCORE_CHANNELS; ++c)
	channels[c] = channel_defaults;
    steals = dropped_layers = 0;
}

void OPLVoices_Init (const genmidi_instrument_t* instruments)
{
    bank = instruments;
    for (int i = 0; i < OPL_VOICES; ++i)
	voices[i] = (voice_t){ .bank = i / OPL_BANK_CHANNELS, .slot = i % OPL_BANK_CHANNELS,
			       .instrument = &bank[0] };
    OPLVoices_Reset ();
}

static void mix (float* stereo, int frames, float gain)
{
    int16_t frame[2];

    gain /= 32768.0f;
    for (int i = 0; i < frames; ++i)
    {
	OPLChip_Render (frame);
	stereo[2 * i] += frame[0] * gain;
	stereo[2 * i + 1] += frame[1] * gain;
    }
}

const music_synth_t OPL_Synth = { OPLVoices_Reset, OPLVoices_Event, mix };

void OPLVoices_SetStealing (opl_steal_t rule) { steal_rule = rule; }
int OPLVoices_Steals (void) { return steals; }
int OPLVoices_DroppedLayers (void) { return dropped_layers; }

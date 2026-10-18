/* MUS and MIDI scores. Both formats become one list of events in MUS
   channel numbers, timed in output frames. */
#include <stdlib.h>
#include <string.h>
#include "i_system.h"
#include "music_score.h"

#define MUS_RATE		140	/* MUS ticks per second */

static score_t*		target;		/* the score being loaded */
static int		output_rate;

static void add_event (double seconds, int type, int channel, int data1, int data2)
{
    if (target->count == target->capacity)
    {
	target->capacity = target->capacity ? target->capacity * 2 : 1024;
	target->events = realloc (target->events, target->capacity * sizeof(*target->events));
	if (!target->events)
	    I_Error ("OPL music: out of memory");
    }
    target->events[target->count++] = (score_event_t){ (uint32_t)(seconds * output_rate + 0.5), (byte)type,
				      (byte)channel, (byte)(data1 & 127), (byte)(data2 & 127) };
}

static boolean load_mus (const byte* data, int size)
{
    static const byte controls[] = { 0, 0, 1, CTRL_VOLUME, CTRL_PAN, CTRL_EXPRESSION, 91, 93, 64, 67 };
    static const byte system[] = { CTRL_SOUNDS_OFF, CTRL_NOTES_OFF, CTRL_MONO, CTRL_POLY, CTRL_RESET };
    byte velocity[SCORE_CHANNELS];
    unsigned start, length;
    const byte *p, *end;
    double ticks = 0;

    if (size < 16 || memcmp (data, "MUS\x1a", 4))
	return false;
    length = data[4] | data[5] << 8;
    start = data[6] | data[7] << 8;
    if (start > (unsigned)size || length > (unsigned)size - start)
	return false;
    for (int c = 0; c < SCORE_CHANNELS; ++c)
	velocity[c] = 127;
    p = data + start;
    end = p + length;
    while (p < end)
    {
	byte descriptor = *p++;
	int channel = descriptor & 15;
	int type = (descriptor >> 4) & 7;
	double seconds = ticks / MUS_RATE;

	if (type == 6)
	    break;
	if (type != 5 && p == end)
	    return false;
	switch (type)
	{
	  case 0:
	    add_event (seconds, EV_NOTE_OFF, channel, *p++, 0);
	    break;
	  case 1:
	    {
		byte note = *p++;
		if (note & 128)
		{
		    if (p == end)
			return false;
		    velocity[channel] = *p++ & 127;
		}
		add_event (seconds, EV_NOTE_ON, channel, note, velocity[channel]);
	    }
	    break;
	  case 2:
	    add_event (seconds, EV_BEND, channel, 0, *p++ >> 1);
	    break;
	  case 3:
	    {
		byte number = *p++;
		if (number >= 10 && number <= 14)
		    add_event (seconds, EV_CONTROLLER, channel, system[number - 10], 0);
	    }
	    break;
	  case 4:
	    {
		byte number = *p++, value;
		if (p == end)
		    return false;
		value = *p++;
		if (value > 127)
		    value = 127;
		if (number == 0)
		    add_event (seconds, EV_PROGRAM, channel, value, 0);
		else if (number < 10)
		    add_event (seconds, EV_CONTROLLER, channel, controls[number], value);
	    }
	    break;
	  case 7:
	    p++;
	    break;
	}
	if (descriptor & 128)
	{
	    uint32_t delay = 0;
	    byte next;
	    do
	    {
		if (p == end)
		    return false;
		next = *p++;
		delay = delay << 7 | (next & 127);
	    } while (next & 128);
	    ticks += delay;
	}
    }
    target->length = (uint32_t)(ticks / MUS_RATE * output_rate);
    return true;
}

typedef struct { uint32_t tick; int order; byte status, data1, data2; uint32_t tempo; } midi_event_t;

static int compare_midi (const void* a, const void* b)
{
    const midi_event_t* x = a;
    const midi_event_t* y = b;
    if (x->tick != y->tick)
	return x->tick < y->tick ? -1 : 1;
    return x->order - y->order;
}

static uint32_t read_be (const byte* p, int bytes)
{
    uint32_t value = 0;
    while (bytes--)
	value = value << 8 | *p++;
    return value;
}

static boolean read_varlen (const byte** p, const byte* end, uint32_t* value)
{
    *value = 0;
    for (int i = 0; i < 4; ++i)
    {
	if (*p >= end)
	    return false;
	*value = *value << 7 | (**p & 127);
	if (!(*(*p)++ & 128))
	    return true;
    }
    return false;
}

/* Standard MIDI files: all tracks merge onto one tempo map. A damaged
   track ends where the damage starts. */
static boolean load_midi (const byte* data, int size)
{
    midi_event_t* list = NULL;
    int count = 0, capacity = 0;
    unsigned tracks, division;
    const byte* p;
    const byte* end = data + size;

    if (size < 14 || read_be (data + 4, 4) < 6)
	return false;
    tracks = read_be (data + 10, 2);
    division = read_be (data + 12, 2);
    p = data + 8 + read_be (data + 4, 4);
    if (!division || p > end)
	return false;
    for (unsigned t = 0; t < tracks && p + 8 <= end; ++t)
    {
	uint32_t length = read_be (p + 4, 4);
	const byte* q = p + 8;
	const byte* track_end = q + length;
	uint32_t tick = 0;
	byte status = 0;

	if (memcmp (p, "MTrk", 4) || length > (uint32_t)(end - q))
	    break;
	p = track_end;
	while (q < track_end)
	{
	    uint32_t delta;
	    midi_event_t event = { 0 };

	    if (!read_varlen (&q, track_end, &delta) || q >= track_end)
		break;
	    tick += delta;
	    if (*q & 128)
		status = *q++;
	    if (status == 0xff || status == 0xf0 || status == 0xf7)
	    {
		byte meta = status == 0xff && q < track_end ? *q++ : 0;
		uint32_t skip;
		if (!read_varlen (&q, track_end, &skip) || skip > (uint32_t)(track_end - q))
		    break;
		if (status == 0xff && meta == 0x51 && skip == 3)
		{
		    event.tempo = read_be (q, 3);
		    event.status = 0xff;
		}
		q += skip;
		status = 0;	/* system messages cancel running status */
		if (meta == 0x2f)
		    break;
		if (!event.status)
		    continue;
	    }
	    else
	    {
		int type = status >> 4;
		int bytes = type == 0xc || type == 0xd ? 1 : 2;
		if (type < 8 || q + bytes > track_end)
		    break;
		event.status = status;
		event.data1 = q[0];
		event.data2 = bytes == 2 ? q[1] : 0;
		q += bytes;
	    }
	    event.tick = tick;
	    event.order = count;
	    if (count == capacity)
	    {
		capacity = capacity ? capacity * 2 : 1024;
		list = realloc (list, capacity * sizeof(*list));
		if (!list)
		    I_Error ("OPL music: out of memory");
	    }
	    list[count++] = event;
	}
    }
    if (!count)
    {
	free (list);
	return false;
    }
    qsort (list, count, sizeof(*list), compare_midi);
    {
	double seconds = 0, per_tick;
	uint32_t last = 0;

	/* SMPTE division: frames per second times ticks per frame. */
	if (division & 0x8000)
	    per_tick = 1.0 / ((256 - (division >> 8)) * (division & 0xff));
	else
	    per_tick = 0.5 / division;	/* 120 beats per minute */
	for (int i = 0; i < count; ++i)
	{
	    const midi_event_t* event = &list[i];
	    int channel = event->status & 15;
	    int type = event->status >> 4;

	    seconds += (event->tick - last) * per_tick;
	    last = event->tick;
	    if (event->status == 0xff)
	    {
		if (!(division & 0x8000))
		    per_tick = event->tempo / 1e6 / division;
		continue;
	    }
	    /* MIDI percussion is channel 10; in MUS numbering it is 15. */
	    channel = channel == 9 ? 15 : channel == 15 ? 9 : channel;
	    if (type == 0x8)
		add_event (seconds, EV_NOTE_OFF, channel, event->data1, 0);
	    else if (type == 0x9)
		add_event (seconds, EV_NOTE_ON, channel, event->data1, event->data2);
	    else if (type == 0xb)
		add_event (seconds, EV_CONTROLLER, channel, event->data1, event->data2);
	    else if (type == 0xc)
		add_event (seconds, EV_PROGRAM, channel, event->data1, 0);
	    else if (type == 0xe)
		add_event (seconds, EV_BEND, channel, event->data1, event->data2);
	}
	target->length = (uint32_t)(seconds * output_rate);
    }
    free (list);
    return true;
}

boolean Score_Load (score_t* score, const void* data, int size, int rate)
{
    boolean loaded;

    target = score;
    output_rate = rate;
    score->count = 0;
    score->length = 0;
    loaded = size >= 4 && (!memcmp (data, "MThd", 4) ? load_midi (data, size) : load_mus (data, size));
    if (!loaded)
	score->count = 0;
    target = NULL;
    return loaded;
}

void Score_Clear (score_t* score)
{
    score->count = 0;
    score->length = 0;
}

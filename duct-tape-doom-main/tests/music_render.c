/* Renders a WAD's music to WAV files without the game, for listening:

     doom-music-render <iwad> [pwad ...] [-song D_E1M1|all] [-seconds N] [-out dir]
                       [-synth opl|sf2] [-steal highest|spare]
                       [-sf2 bank.sf2] [-voices N] [-gain G]

   Later WADs replace lumps of earlier ones, as in the game, so a PWAD's
   music plays with the IWAD's GENMIDI. Each song plays once (capped at
   -seconds, default 180) and is written as <out>/<lump>-opl-<steal>.wav or
   <out>/<lump>-sf2.wav. The synth's own numbers are printed per song: for
   the OPL, voice steals, second layers dropped and the biggest burst of
   register writes after the first second (and in it); for the SoundFont,
   peak voices. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "music_player.h"
#include "opl_chip.h"
#include "opl_genmidi.h"
#include "opl_voices.h"
#include "sf2_synth.h"

#define RATE		44100
#define OPL_GAIN	1.0f	/* mixer_music.c's gains, at full music volume */
#define SF2_GAIN	0.3f

void I_Error (char* error, ...)
{
    va_list args;
    va_start (args, error);
    vfprintf (stderr, error, args);
    va_end (args);
    fputc ('\n', stderr);
    exit (1);
}

typedef struct { char name[9]; const byte* data; int size; } lump_t;

static lump_t*	lumps;
static int	num_lumps;
static boolean	use_sf2;
static const char* steal = "highest";

/* Lump names compare without case, up to eight characters. */
static int same_name (const char* a, const char* b, int n)
{
    for (int i = 0; i < n; ++i)
    {
	if (toupper ((unsigned char)a[i]) != toupper ((unsigned char)b[i]))
	    return 0;
	if (!a[i])
	    return 1;
    }
    return 1;
}

static unsigned read32 (const byte* p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

static void load_wad (const char* path)
{
    FILE* f = fopen (path, "rb");
    long size;
    unsigned dir, count;
    byte* wad;

    if (!f)
	I_Error ("Can't open %s", path);
    fseek (f, 0, SEEK_END);
    size = ftell (f);
    rewind (f);
    wad = malloc (size);
    if (!wad || fread (wad, 1, size, f) != (size_t)size || size < 12)
	I_Error ("Can't read %s", path);
    fclose (f);
    count = read32 (wad + 4);
    dir = read32 (wad + 8);
    if (dir > (unsigned)size || count > (size - dir) / 16)
	I_Error ("%s is not a WAD", path);
    lumps = realloc (lumps, (num_lumps + count) * sizeof(*lumps));
    if (!lumps)
	I_Error ("Out of memory");
    for (unsigned n = 0; n < count; ++n)
    {
	int i = num_lumps + n;
	const byte* entry = wad + dir + 16 * n;
	unsigned pos = read32 (entry), len = read32 (entry + 4);
	memcpy (lumps[i].name, entry + 8, 8);
	if (pos > (unsigned)size || len > size - pos)
	    len = pos = 0;
	lumps[i].name[8] = 0;
	lumps[i].data = wad + pos;
	lumps[i].size = (int)len;
    }
    num_lumps += count;
}

/* The last lump of that name wins, as in the game. */
static const lump_t* find_lump (const char* name)
{
    for (int i = num_lumps - 1; i >= 0; --i)
	if (same_name (lumps[i].name, name, 8))
	    return &lumps[i];
    return NULL;
}

static void put16 (FILE* f, unsigned v) { fputc (v & 255, f); fputc (v >> 8 & 255, f); }
static void put32 (FILE* f, unsigned v) { put16 (f, v & 0xffff); put16 (f, v >> 16); }

static void write_wav (const char* path, const int16_t* samples, uint32_t frames)
{
    FILE* f = fopen (path, "wb");
    if (!f)
	I_Error ("Can't write %s", path);
    fwrite ("RIFF", 1, 4, f); put32 (f, 36 + frames * 4);
    fwrite ("WAVEfmt ", 1, 8, f); put32 (f, 16); put16 (f, 1); put16 (f, 2);
    put32 (f, RATE); put32 (f, RATE * 4); put16 (f, 4); put16 (f, 16);
    fwrite ("data", 1, 4, f); put32 (f, frames * 4);
    fwrite (samples, 4, frames, f);
    fclose (f);
}

static void render (const lump_t* song, int seconds, float gain, const char* out)
{
    score_t score = { 0 };
    music_player_t player;
    uint32_t frames;
    float* mixed;
    int16_t* samples;
    float peak = 0;
    int opening_burst = 0;
    char path[1024];

    if (!Score_Load (&score, song->data, song->size, RATE))
    {
	printf ("%-8s  not a score\n", song->name);
	return;
    }
    frames = score.length < (uint32_t)seconds * RATE ? score.length : (uint32_t)seconds * RATE;
    mixed = calloc (frames * 2 + 2, sizeof(float));
    samples = malloc (frames * 4 + 4);
    Player_Start (&player, &score, use_sf2 ? &SF2_Synth : &OPL_Synth, false);
    {
	/* The first second loads every voice's instrument at once; bursts
	   after it are what the music itself queues. */
	uint32_t opening = frames < RATE ? frames : RATE;
	Player_Mix (&player, mixed, (int)opening, gain);
	if (!use_sf2)
	    opening_burst = OPLChip_MaxBurst ();
	Player_Mix (&player, mixed + 2 * opening, (int)(frames - opening), gain);
    }
    for (uint32_t i = 0; i < frames * 2; ++i)
    {
	float v = mixed[i];
	if (v > peak || -v > peak)
	    peak = v > 0 ? v : -v;
	samples[i] = (int16_t)(v >= 1 ? 32767 : v <= -1 ? -32768 : v * 32767);
    }
    if (use_sf2)
	snprintf (path, sizeof(path), "%s/%s-sf2.wav", out, song->name);
    else
	snprintf (path, sizeof(path), "%s/%s-opl-%s.wav", out, song->name, steal);
    write_wav (path, samples, frames);
    printf ("%-8s  %6.1f s  peak %5.3f  ", song->name, frames / (double)RATE, peak);
    if (use_sf2)
	printf ("peak voices %d\n", SF2_PeakVoices ());
    else
    {
	int burst = OPLChip_MaxBurst ();	/* two chip samples per write */
	printf ("steals %d  dropped layers %d  burst %d writes (%.1f ms), opening %d\n",
		OPLVoices_Steals (), OPLVoices_DroppedLayers (), burst, burst * 2 * 1000.0 / 49716,
		opening_burst);
    }
    Player_Stop (&player);
    free (mixed);
    free (samples);
    free (score.events);
}

int main (int argc, char** argv)
{
    static genmidi_instrument_t bank[GENMIDI_COUNT];
    const char *song = "all", *out = ".", *sf2 = NULL;
    int seconds = 180, voices = 0;
    float gain = -1;

    if (argc < 2)
    {
	fprintf (stderr, "usage: %s <iwad> [pwad ...] [-song D_E1M1|all] [-seconds N] [-out dir] [-synth opl|sf2]\n"
		 "       [-steal highest|spare] [-sf2 bank.sf2] [-voices N] [-gain G]\n", argv[0]);
	return 2;
    }
    int first_option = 2;
    while (first_option < argc && argv[first_option][0] != '-')
	++first_option;
    for (int i = first_option; i + 1 < argc; i += 2)
    {
	if (!strcmp (argv[i], "-song")) song = argv[i + 1];
	else if (!strcmp (argv[i], "-seconds")) seconds = atoi (argv[i + 1]);
	else if (!strcmp (argv[i], "-out")) out = argv[i + 1];
	else if (!strcmp (argv[i], "-synth")) use_sf2 = !strcmp (argv[i + 1], "sf2");
	else if (!strcmp (argv[i], "-steal")) steal = argv[i + 1];
	else if (!strcmp (argv[i], "-sf2")) sf2 = argv[i + 1];
	else if (!strcmp (argv[i], "-voices")) voices = atoi (argv[i + 1]);
	else if (!strcmp (argv[i], "-gain")) gain = (float)atof (argv[i + 1]);
	else I_Error ("Unknown option %s", argv[i]);
    }
    for (int i = 1; i < first_option; ++i)
	load_wad (argv[i]);
    if (use_sf2)
    {
	sf2_bank_t* opened = sf2 ? SF2_Open (sf2, RATE, voices) : NULL;
	if (!opened)
	    I_Error ("Can't load the SoundFont %s (-sf2 bank.sf2)", sf2 ? sf2 : "");
	SF2_Use (opened);
	if (gain < 0)
	    gain = SF2_GAIN;
    }
    else
    {
	const lump_t* genmidi = find_lump ("GENMIDI");
	if (!genmidi || !GENMIDI_Parse (genmidi->data, genmidi->size, bank))
	    I_Error ("No GENMIDI lump in the WADs given; put the IWAD first");
	OPLChip_Start (RATE);
	OPLVoices_Init (bank);
	OPLVoices_SetStealing (!strcmp (steal, "spare") ? STEAL_SPARE_DRUMS : STEAL_HIGHEST);
	if (gain < 0)
	    gain = OPL_GAIN;
    }
    if (strcmp (song, "all"))
    {
	const lump_t* lump = find_lump (song);
	if (!lump)
	    I_Error ("No lump %s", song);
	render (lump, seconds, gain, out);
	return 0;
    }
    for (int i = 0; i < num_lumps; ++i)
	if (same_name (lumps[i].name, "D_", 2) && find_lump (lumps[i].name) == &lumps[i])
	    render (&lumps[i], seconds, gain, out);
    return 0;
}

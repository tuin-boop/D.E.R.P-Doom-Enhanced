/* The GENMIDI lump: DOOM's OPL instrument bank (Doom Wiki, "GENMIDI"). */
#ifndef DOOM_AUDIO_OPL_GENMIDI_H
#define DOOM_AUDIO_OPL_GENMIDI_H
#include "doomtype.h"

#define GENMIDI_MELODIC		128
#define GENMIDI_PERCUSSION	47	/* keys 35-81 */
#define GENMIDI_FIRST_DRUM	35
#define GENMIDI_COUNT		(GENMIDI_MELODIC + GENMIDI_PERCUSSION)

/* A voice is the lump's 16 bytes, kept as they are. Bytes 0-5 belong to
   the modulator and 7-12 to the carrier, in this order: */
enum { PATCH_CHARACTER, PATCH_ENVELOPE, PATCH_SUSTAIN, PATCH_WAVEFORM,
       PATCH_KEY_SCALE, PATCH_LEVEL, PATCH_OPERATOR_BYTES };
#define PATCH_MODULATOR		0
#define PATCH_FEEDBACK		6	/* register 0xC0: feedback << 1 | connection */
#define PATCH_CARRIER		7
#define PATCH_BASE_NOTE		14	/* signed 16-bit semitones */
#define PATCH_BYTES		16

typedef struct { byte bytes[PATCH_BYTES]; } genmidi_patch_t;

typedef struct
{
    unsigned short	flags;		/* GENMIDI_FIXED, GENMIDI_DUAL */
    byte		tuning;		/* second patch, 128 = none, 1/32 semitones */
    byte		fixed_note;
    genmidi_patch_t	patch[2];
} genmidi_instrument_t;

#define GENMIDI_FIXED		0x0001
#define GENMIDI_DUAL		0x0004

/* Base note offset of a patch, in semitones. */
static inline int GENMIDI_BaseNote (const genmidi_patch_t* p)
{
    return (short)(p->bytes[PATCH_BASE_NOTE] | p->bytes[PATCH_BASE_NOTE + 1] << 8);
}

/* Fills the bank from a GENMIDI lump; false if the lump isn't one. */
boolean GENMIDI_Parse (const byte* lump, int size, genmidi_instrument_t bank[GENMIDI_COUNT]);
#endif

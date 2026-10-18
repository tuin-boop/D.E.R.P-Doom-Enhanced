/* GENMIDI: "#OPL_II#", then 175 records of 36 bytes. A record is flags
   (u16), fine tuning (u8), fixed note (u8) and two 16-byte patches.
   Instrument names follow; they go unused. */
#include <string.h>
#include "opl_genmidi.h"

#define HEADER		"#OPL_II#"
#define RECORD		(4 + 2 * PATCH_BYTES)

boolean GENMIDI_Parse (const byte* lump, int size, genmidi_instrument_t bank[GENMIDI_COUNT])
{
    if (!lump || size < 8 + GENMIDI_COUNT * RECORD || memcmp (lump, HEADER, 8))
	return false;
    for (int i = 0; i < GENMIDI_COUNT; ++i)
    {
	const byte* p = lump + 8 + i * RECORD;

	bank[i].flags = p[0] | p[1] << 8;
	bank[i].tuning = p[2];
	bank[i].fixed_note = p[3];
	memcpy (bank[i].patch, p + 4, sizeof(bank[i].patch));
    }
    return true;
}

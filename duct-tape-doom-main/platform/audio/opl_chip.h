/* The emulated YMF262 (OPL3) and the arithmetic that drives it: operator
   offsets, pitch in F-number/block and loudness in total-level steps. */
#ifndef DOOM_AUDIO_OPL_CHIP_H
#define DOOM_AUDIO_OPL_CHIP_H
#include <stdint.h>

#define OPL_BANK_CHANNELS	9
#define OPL_MAX_LEVEL		63	/* total level: 0.75 dB steps of attenuation */

/* Resets the chip and runs the startup sequence. */
void OPLChip_Start (int rate);
/* bank 0 or 1, register 0x00-0xFF; queued in order */
void OPLChip_Write (int bank, int reg, int value);
/* One output frame, left and right. */
void OPLChip_Render (int16_t frame[2]);
/* Most writes queued between two frames since the last call (which also
   starts the count afresh); each takes
   two chip samples (40 µs) to reach the chip. */
int OPLChip_MaxBurst (void);

/* The modulator's register offset for a channel; the carrier is 3 on. */
static inline int OPLChip_Operator (int channel)
{
    return channel % 3 + 8 * (channel / 3);
}

/* pitch: 1/32 semitones above MIDI note 0, concert tuning. */
void OPLChip_Pitch (int pitch, int* block, int* fnum);
/* Attenuation in TL steps for 7-bit velocity, volume and expression;
   above OPL_MAX_LEVEL when any of them is zero. */
int OPLChip_Loss (int velocity, int volume, int expression);
#endif

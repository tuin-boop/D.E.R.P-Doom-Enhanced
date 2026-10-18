/* The YMF262 through Nuked OPL3. Everything here comes from the chip's
   register manual: the startup writes, the operator layout, the F-number
   formula and the 0.75 dB total-level step. See opl_music_spec.md. */
#include <math.h>
#include "opl3.h"
#include "opl_chip.h"

#define CHIP_RATE	49716.0		/* 14.318 MHz / 288 */
#define TL_STEP_DB	0.75

static opl3_chip	chip;
static short		shadow[0x200];	/* last value per register, -1 unknown */
static int		burst, max_burst;	/* queued writes between frames */

static void write_now (int bank, int reg, int value)
{
    OPL3_WriteReg (&chip, (uint16_t)(bank << 8 | reg), (uint8_t)value);
}

/* Queued two chip samples apart, about the pace real cards needed. It also
   lets a key-off reach the envelope before the next key-on, so a stolen
   voice restarts its attack. */
void OPLChip_Write (int bank, int reg, int value)
{
    int address = bank << 8 | reg;

    if (shadow[address] == (value & 0xFF))
	return;		/* the queue is slow enough not to fill with repeats */
    shadow[address] = value & 0xFF;
    OPL3_WriteRegBuffered (&chip, (uint16_t)address, (uint8_t)value);
    if (++burst > max_burst)
	max_burst = burst;
}

void OPLChip_Render (int16_t frame[2])
{
    burst = 0;
    OPL3_GenerateResampled (&chip, frame);
}

int OPLChip_MaxBurst (void)
{
    int most = max_burst;
    max_burst = burst = 0;
    return most;
}

void OPLChip_Start (int rate)
{
    OPL3_Reset (&chip, (uint32_t)rate);
    for (int i = 0; i < 0x200; ++i)
	shadow[i] = -1;
    write_now (1, 0x05, 0x01);	/* NEW: OPL3 mode */
    write_now (1, 0x04, 0x00);	/* all channels 2-operator */
    write_now (0, 0x01, 0x00);	/* test register clear */
    write_now (0, 0x08, 0x00);	/* no CSM, NTS 0 */
    write_now (0, 0xBD, 0x00);	/* melodic mode, shallow AM and vibrato */
    for (int bank = 0; bank < 2; ++bank)
	for (int ch = 0; ch < OPL_BANK_CHANNELS; ++ch)
	{
	    int op = OPLChip_Operator (ch);
	    write_now (bank, 0xB0 + ch, 0x00);	/* key off */
	    write_now (bank, 0xC0 + ch, 0x30);	/* both sides */
	    for (int o = op; o <= op + 3; o += 3)
	    {
		write_now (bank, 0x40 + o, OPL_MAX_LEVEL);
		write_now (bank, 0x80 + o, 0x0F);	/* fastest release */
	    }
	}
}

/* f = fnum * 49716 / 2^(20 - block): keep fnum in 512-1023 where the
   blocks allow it, for the finest steps. */
void OPLChip_Pitch (int pitch, int* block, int* fnum)
{
    double hz = 440.0 * exp2 ((pitch / 32.0 - 69.0) / 12.0);
    double x = hz * 1048576.0 / CHIP_RATE;	/* the F-number at block 0 */
    int b = 0;

    while (x >= 1023.5 && b < 7)
    {
	x *= 0.5;
	++b;
    }
    *block = b;
    *fnum = x >= 1023.5 ? 1023 : (int)lround (x);
}

/* Square law: 40 dB per decade for each of the three. */
int OPLChip_Loss (int velocity, int volume, int expression)
{
    double gain;

    if (velocity <= 0 || volume <= 0 || expression <= 0)
	return OPL_MAX_LEVEL + 1;
    gain = (velocity / 127.0) * (volume / 127.0) * (expression / 127.0);
    return (int)lround (-40.0 * log10 (gain) / TL_STEP_DB);
}

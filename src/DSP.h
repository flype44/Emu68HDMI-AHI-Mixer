/******************************************************************************
 *
 * DSP.h -- levels and spectrum of the audio that has just been played
 *
 ******************************************************************************/

#ifndef MIXER_DSP_H
#define MIXER_DSP_H

#include <exec/types.h>

#define SCOPE_FRAMES  1024     /* frames analysed per refresh (21 ms at 48 kHz) */
#define SPEC_BANDS    64       /* bars of the spectrum, 40 Hz to 20 kHz, log spaced */
#define DB_FLOOR      (-1000)  /* tenths of a dB: -100.0 dB */

/* Everything the scope class draws. All levels are tenths of a dB, 0 = full scale. */
struct ScopeFrame
{
	BOOL  active;              /* TRUE when the samples below are valid */
	LONG  left[SCOPE_FRAMES];  /* signed 24 bit samples */
	LONG  right[SCOPE_FRAMES];

	LONG  peak_l, peak_r;      /* meters with fast attack and slow release */
	LONG  rms_l, rms_r;
	LONG  hold_l, hold_r;      /* peak hold */
	ULONG clip_l, clip_r;      /* ticks left on the clip lamps */

	LONG  band[SPEC_BANDS];    /* spectrum with release */
	LONG  band_hold[SPEC_BANDS];
};

/* Sets every level to the floor and the frame inactive */
VOID DspReset(struct ScopeFrame * frame);

/* Builds the twiddle, window and band tables. FALSE when out of memory. */
BOOL DspInit(VOID);
VOID DspExit(VOID);

/* Updates the meters and the spectrum from frame->left/right (frame->active
** TRUE) or lets everything fall back to silence (FALSE). Call once per tick. */
VOID DspAnalyze(struct ScopeFrame * frame);

#endif /* MIXER_DSP_H */

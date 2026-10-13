/******************************************************************************
 *
 * DSP.c -- levels and spectrum of the audio that has just been played
 *
 * Integer only on the hot path (a 1024 point FFT with 15 bit twiddles and a
 * halving at every stage); the floating point library is used to build the
 * tables once and to turn magnitudes into dB, about 130 times a refresh.
 *
 ******************************************************************************/

#include <exec/types.h>

#include <proto/exec.h>

#include <math.h>

#include "DSP.h"

#define FFT_N       SCOPE_FRAMES
#define FFT_LOG2    10
#define FULL_SCALE  8388608L        /* 2^23: a 24 bit sample */
#define SPEC_REF    8192L           /* bin magnitude of a full scale sine after the 1/N scaling */

#define PEAK_RELEASE   15           /* tenths of a dB per refresh: 37 dB/s */
#define RMS_RELEASE    10
#define BAND_RELEASE   25
#define HOLD_TICKS     40
#define HOLD_RELEASE   20
#define CLIP_TICKS     40

STATIC WORD  CosTable[FFT_N / 2];
STATIC WORD  SinTable[FFT_N / 2];
STATIC WORD  Window[FFT_N];
STATIC UWORD Reverse[FFT_N];
STATIC UWORD BandLow[SPEC_BANDS];
STATIC UWORD BandHigh[SPEC_BANDS];

STATIC LONG  Re[FFT_N];
STATIC LONG  Im[FFT_N];

STATIC ULONG HoldAgeL = 0;
STATIC ULONG HoldAgeR = 0;
STATIC ULONG BandAge[SPEC_BANDS];

STATIC BOOL  Ready = FALSE;

/******************************************************************************
 *
 * DspInit()
 *
 ******************************************************************************/

BOOL DspInit(VOID)
{
	ULONG i, j, bits, r;
	double pi = 3.14159265358979323846;
	double ratio, low, high, bin_width;

	for (i = 0; i < FFT_N / 2; i++) {
		CosTable[i] = (WORD)(32767.0 * cos(2.0 * pi * (double)i / (double)FFT_N));
		SinTable[i] = (WORD)(32767.0 * sin(2.0 * pi * (double)i / (double)FFT_N));
	}

	for (i = 0; i < FFT_N; i++) {
		/* Hann window */
		Window[i] = (WORD)(32767.0 * 0.5 * (1.0 - cos(2.0 * pi * (double)i / (double)(FFT_N - 1))));

		r = 0;

		for (j = 0, bits = i; j < FFT_LOG2; j++) {
			r = (r << 1) | (bits & 1);
			bits >>= 1;
		}

		Reverse[i] = (UWORD)r;
	}

	/* 40 Hz .. 20 kHz in SPEC_BANDS log spaced bands; one bin is 46.875 Hz */
	bin_width = 48000.0 / (double)FFT_N;
	ratio = pow(20000.0 / 40.0, 1.0 / (double)SPEC_BANDS);
	low = 40.0;

	for (i = 0; i < SPEC_BANDS; i++) {
		high = low * ratio;

		BandLow[i]  = (UWORD)(low / bin_width + 0.5);
		BandHigh[i] = (UWORD)(high / bin_width + 0.5);

		if (BandLow[i] < 1) {
			BandLow[i] = 1;
		}

		if (BandHigh[i] < BandLow[i]) {
			BandHigh[i] = BandLow[i];
		}

		if (BandHigh[i] > FFT_N / 2 - 1) {
			BandHigh[i] = FFT_N / 2 - 1;
		}

		low = high;
	}

	Ready = TRUE;

	return TRUE;
}

VOID DspExit(VOID)
{
	Ready = FALSE;
}

/******************************************************************************
 *
 * DspReset()
 *
 ******************************************************************************/

VOID DspReset(struct ScopeFrame * f)
{
	ULONG b;

	f->active = FALSE;
	f->peak_l = f->peak_r = DB_FLOOR;
	f->rms_l  = f->rms_r  = DB_FLOOR;
	f->hold_l = f->hold_r = DB_FLOOR;
	f->clip_l = f->clip_r = 0;

	for (b = 0; b < SPEC_BANDS; b++) {
		f->band[b]      = DB_FLOOR;
		f->band_hold[b] = DB_FLOOR;
		BandAge[b]      = 0;
	}

	HoldAgeL = HoldAgeR = 0;
}

/******************************************************************************
 *
 * ISqrt() -- integer square root, bit by bit
 *
 ******************************************************************************/

STATIC ULONG ISqrt(ULONG value)
{
	ULONG result = 0;
	ULONG bit = 1UL << 30;

	while (bit > value) {
		bit >>= 2;
	}

	while (bit != 0) {
		if (value >= result + bit) {
			value -= result + bit;
			result = (result >> 1) + bit;
		} else {
			result >>= 1;
		}

		bit >>= 2;
	}

	return result;
}

/******************************************************************************
 *
 * ToDb10() -- tenths of a dB below `full`, never lower than DB_FLOOR
 *
 ******************************************************************************/

STATIC LONG ToDb10(ULONG value, ULONG full)
{
	LONG db;

	if (value == 0) {
		return DB_FLOOR;
	}

	db = (LONG)(200.0 * log10((double)value / (double)full));

	if (db < DB_FLOOR) {
		db = DB_FLOOR;
	}

	if (db > 0) {
		db = 0;
	}

	return db;
}

/******************************************************************************
 *
 * Fft() -- in place, decimation in time, halving at every stage
 *
 ******************************************************************************/

STATIC VOID Fft(VOID)
{
	ULONG size, half, step, i, j, k;
	LONG wr, wi, xr, xi, tr, ti, ur, ui;

	for (size = 2; size <= FFT_N; size <<= 1) {
		half = size >> 1;
		step = FFT_N / size;

		for (i = 0; i < FFT_N; i += size) {
			for (j = 0, k = 0; j < half; j++, k += step) {
				wr = CosTable[k];
				wi = -SinTable[k];

				xr = Re[i + j + half];
				xi = Im[i + j + half];

				tr = (xr * wr - xi * wi) >> 15;
				ti = (xr * wi + xi * wr) >> 15;

				ur = Re[i + j];
				ui = Im[i + j];

				Re[i + j + half] = (ur - tr) >> 1;
				Im[i + j + half] = (ui - ti) >> 1;
				Re[i + j]        = (ur + tr) >> 1;
				Im[i + j]        = (ui + ti) >> 1;
			}
		}
	}
}

/******************************************************************************
 *
 * Fall() -- a value that rises at once and falls by `release` per refresh
 *
 ******************************************************************************/

STATIC LONG Fall(LONG shown, LONG now, LONG release)
{
	if (now >= shown) {
		return now;
	}

	shown -= release;

	return (shown < now) ? now : shown;
}

/******************************************************************************
 *
 * Hold() -- peak hold: stays at the highest value, then falls
 *
 ******************************************************************************/

STATIC LONG Hold(LONG held, LONG now, ULONG * age)
{
	if (now >= held) {
		*age = 0;
		return now;
	}

	(*age)++;

	if (*age > HOLD_TICKS) {
		held -= HOLD_RELEASE;

		if (held < now) {
			held = now;
		}
	}

	return held;
}

/******************************************************************************
 *
 * DspAnalyze()
 *
 ******************************************************************************/

VOID DspAnalyze(struct ScopeFrame * f)
{
	ULONG i, b, k;
	LONG pl = 0, pr = 0;
	LONG s, m;
	ULONG sum_l = 0, sum_r = 0;
	LONG db_pl, db_pr, db_rl, db_rr;
	LONG db;
	ULONG mag, best;

	if (!Ready) {
		return;
	}

	if (!f->active) {
		/* No sound: everything falls back to the floor */
		f->peak_l = f->peak_l - PEAK_RELEASE > DB_FLOOR ? f->peak_l - PEAK_RELEASE : DB_FLOOR;
		f->peak_r = f->peak_r - PEAK_RELEASE > DB_FLOOR ? f->peak_r - PEAK_RELEASE : DB_FLOOR;
		f->rms_l  = f->rms_l  - RMS_RELEASE  > DB_FLOOR ? f->rms_l  - RMS_RELEASE  : DB_FLOOR;
		f->rms_r  = f->rms_r  - RMS_RELEASE  > DB_FLOOR ? f->rms_r  - RMS_RELEASE  : DB_FLOOR;
		f->hold_l = Hold(f->hold_l, DB_FLOOR, &HoldAgeL);
		f->hold_r = Hold(f->hold_r, DB_FLOOR, &HoldAgeR);

		if (f->clip_l > 0) {
			f->clip_l--;
		}

		if (f->clip_r > 0) {
			f->clip_r--;
		}

		for (b = 0; b < SPEC_BANDS; b++) {
			f->band[b]      = f->band[b] - BAND_RELEASE > DB_FLOOR ? f->band[b] - BAND_RELEASE : DB_FLOOR;
			f->band_hold[b] = Hold(f->band_hold[b], DB_FLOOR, &BandAge[b]);
		}

		return;
	}

	/* Peak and RMS (sums of squares of 16 bit values, divided by 2^12 each) */
	for (i = 0; i < SCOPE_FRAMES; i++) {
		s = f->left[i];
		m = (s < 0) ? -s : s;

		if (m > pl) {
			pl = m;
		}

		s >>= 8;
		sum_l += (ULONG)((s * s) >> 12);

		s = f->right[i];
		m = (s < 0) ? -s : s;

		if (m > pr) {
			pr = m;
		}

		s >>= 8;
		sum_r += (ULONG)((s * s) >> 12);
	}

	/* mean square in 16 bit units, then the root */
	db_pl = ToDb10((ULONG)pl, FULL_SCALE);
	db_pr = ToDb10((ULONG)pr, FULL_SCALE);
	db_rl = ToDb10(ISqrt((sum_l / SCOPE_FRAMES) << 12), 32768UL);
	db_rr = ToDb10(ISqrt((sum_r / SCOPE_FRAMES) << 12), 32768UL);

	f->peak_l = Fall(f->peak_l, db_pl, PEAK_RELEASE);
	f->peak_r = Fall(f->peak_r, db_pr, PEAK_RELEASE);
	f->rms_l  = Fall(f->rms_l,  db_rl, RMS_RELEASE);
	f->rms_r  = Fall(f->rms_r,  db_rr, RMS_RELEASE);
	f->hold_l = Hold(f->hold_l, db_pl, &HoldAgeL);
	f->hold_r = Hold(f->hold_r, db_pr, &HoldAgeR);

	if (pl >= 8388000L) {
		f->clip_l = CLIP_TICKS;
	} else if (f->clip_l > 0) {
		f->clip_l--;
	}

	if (pr >= 8388000L) {
		f->clip_r = CLIP_TICKS;
	} else if (f->clip_r > 0) {
		f->clip_r--;
	}

	/* Spectrum of the mono mix: 24 bit -> 16 bit, Hann window, FFT */
	for (i = 0; i < FFT_N; i++) {
		s = ((f->left[i] + f->right[i]) >> 1) >> 8;
		Re[Reverse[i]] = (s * Window[i]) >> 15;
		Im[Reverse[i]] = 0;
	}

	Fft();

	for (b = 0; b < SPEC_BANDS; b++) {
		best = 0;

		for (k = BandLow[b]; k <= BandHigh[b]; k++) {
			mag = ISqrt((ULONG)(Re[k] * Re[k] + Im[k] * Im[k]));

			if (mag > best) {
				best = mag;
			}
		}

		db = ToDb10(best, (ULONG)SPEC_REF);

		f->band[b]      = Fall(f->band[b], db, BAND_RELEASE);
		f->band_hold[b] = Hold(f->band_hold[b], db, &BandAge[b]);
	}
}

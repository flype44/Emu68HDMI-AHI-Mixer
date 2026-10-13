/******************************************************************************
 *
 * Driver.h -- what the HDMI AHI driver says about itself (read only)
 *
 ******************************************************************************/

#ifndef MIXER_DRIVER_H
#define MIXER_DRIVER_H

#include <exec/types.h>
#include <exec/libraries.h>

struct DriverInfo
{
	BOOL  present;            /* the driver library is in memory (AHI loaded it) */
	UWORD version;
	UWORD revision;
	TEXT  ident[48];          /* lib_IdString without the line end */
	ULONG features;           /* HDMI_QUERY_FEATURES: HDMI_FEATURE_xxx */
	ULONG gain;               /* master volume, AHI Fixed (0x10000 = unity), 0 if the driver has none */
	ULONG state;              /* HDMI_STATE_* */
	ULONG stage;
	ULONG fault;
	ULONG mix_bits;           /* 16 or 32 (HiFi) for the last START, 0 if none */
	ULONG irq_id;
	ULONG irqs;               /* DMA segments completed since the last START */
	ULONG wakes;
	ULONG fills;
	ULONG resyncs;
	ULONG resync_reason;
	ULONG resync_at;
	ULONG pre_calls;
	ULONG pre_skips;
	ULONG max_burst_us;
	ULONG max_gap_us;
};

/* Fills *info. The library is looked up by name and only opened for the
** duration of the call: an idle driver is never loaded by this program. */
VOID DrvRead(struct DriverInfo * info);

/* The master volume (AHIC_OutputVolume, a Fixed value, 0x10000 = unity). DrvReadGain() returns FALSE when the driver is not in
** memory; *features and *gain are then 0. DrvSetGain() returns FALSE when the driver is not in
** memory or has no master volume. Neither loads the driver. */
BOOL DrvReadGain(ULONG * features, ULONG * gain);
BOOL DrvSetGain(ULONG gain);

CONST_STRPTR DrvStageName(ULONG stage);
CONST_STRPTR DrvFaultName(ULONG fault);

#endif /* MIXER_DRIVER_H */

/******************************************************************************
 *
 * Test.h -- test sounds played through AHI, to listen to the HDMI driver
 *
 * The program is an ordinary AHI client: it opens ahi.device, allocates the
 * audio mode chosen on the Test page, plays, and gives the HDMI output back
 * (AHI_FreeAudio) as soon as it stops, so that another program can use it.
 *
 ******************************************************************************/

#ifndef MIXER_TEST_H
#define MIXER_TEST_H

#include <exec/types.h>

/* The test sounds, in the order of the buttons */
enum {
	SOUND_LEFT = 0,
	SOUND_RIGHT,
	SOUND_BOTH,
	SOUND_LRB,        /* 1 s left, 1 s right, 1 s both, repeating */
	SOUND_SWEEP,      /* 20 Hz to 20 kHz, logarithmic, 8 s, repeating */
	SOUND_PINK,       /* pink noise, both channels */
	SOUND_VOICES,     /* 8 voices at different pitches and positions: loads the mixer */
	SOUND_COUNT
};

/* The four AHI modes of the driver (item 0..3 of the mode cycle) */
#define TEST_MODES 4

struct TestResult
{
	BOOL  valid;
	ULONG mix_bits;
	ULONG segments;
	ULONG resyncs;
	ULONG fault;
	ULONG max_burst_us;
	ULONG pre_skips;
	BOOL  interrupt_driver;
};

/* Plays `sound` in mode number `mode` (0..3) at `level_db` (-60 .. -1). On
** failure returns FALSE and *error says why. Stops what played before. */
BOOL TestPlay(ULONG mode, ULONG sound, LONG level_db, CONST_STRPTR * error);

/* Changes the level of what plays */
VOID TestSetLevel(LONG level_db);

/* Stops, gives the HDMI output back, and fills *result from the driver */
VOID TestStop(struct TestResult * result);

BOOL TestIsPlaying(VOID);

CONST_STRPTR TestSoundName(ULONG sound);
CONST_STRPTR TestModeName(ULONG mode);

/* The same, short enough for a gadget of a small window */
CONST_STRPTR TestSoundShort(ULONG sound);
CONST_STRPTR TestModeShort(ULONG mode);

/* Builds / frees the sample tables */
BOOL TestInit(VOID);
VOID TestExit(VOID);

#endif /* MIXER_TEST_H */

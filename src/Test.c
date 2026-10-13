/******************************************************************************
 *
 * Test.c -- test sounds played through AHI
 *
 ******************************************************************************/

#include <exec/types.h>
#include <exec/memory.h>

#include <proto/exec.h>

#include "AhiPragmas.h"

#include <math.h>
#include <string.h>

#include "DrvDefs.h"
#include "Driver.h"
#include "Test.h"

struct Library * AHIBase = NULL;

#define MIX_RATE      48000UL
#define CHANNELS      8
#define SINE_SIZE     4096
#define AMPLITUDE     29000.0      /* about -1 dBFS */
#define SWEEP_SECONDS 8
#define PINK_FRAMES   96000UL
#define LRB_FRAMES    144000UL

STATIC CONST ULONG ModeIds[TEST_MODES] = { 0x001f0010UL, 0x001f0011UL, 0x001f0012UL, 0x001f0013UL };

STATIC CONST_STRPTR ModeNames[TEST_MODES] = {
	"16 bit, long ring (192 ms)",
	"HiFi 24 bit, long ring (192 ms)",
	"16 bit, low latency (48 ms)",
	"HiFi 24 bit, low latency (48 ms)"
};

STATIC CONST_STRPTR ModeShort[TEST_MODES] = { "16 bit, 192 ms", "HiFi, 192 ms", "16 bit, 48 ms", "HiFi, 48 ms" };

STATIC CONST_STRPTR SoundShort[SOUND_COUNT] = {
	"Left", "Right", "Both", "L, R, both", "Sweep", "Pink noise", "8 voices"
};

STATIC CONST_STRPTR SoundNames[SOUND_COUNT] = {
	"Left channel",
	"Right channel",
	"Both channels",
	"Left, right, both",
	"Sweep 20 Hz - 20 kHz",
	"Pink noise",
	"8 voices"
};

/* The 8 voices of the load test: a 1 kHz sine played at these rates */
STATIC CONST ULONG VoiceRates[CHANNELS] = { 48000, 44100, 32000, 22050, 37800, 41000, 24000, 35000 };

STATIC WORD * SineTable = NULL;     /* SINE_SIZE mono frames, one full cycle */
STATIC WORD * Cycle1k = NULL;       /* 48 mono frames: 1 kHz at 48 kHz */
STATIC WORD * SweepBuf = NULL;
STATIC WORD * PinkBuf = NULL;
STATIC WORD * LrbBuf = NULL;        /* stereo */

STATIC struct MsgPort *   AhiPort = NULL;
STATIC struct AHIRequest * AhiReq = NULL;
STATIC struct AHIAudioCtrl * Ctrl = NULL;
STATIC BOOL               Device = FALSE;
STATIC BOOL               Playing = FALSE;
STATIC ULONG              PlayingSound = 0;

STATIC ULONG NoiseState = 0x1234abcdUL;

/******************************************************************************
 *
 * Names
 *
 ******************************************************************************/

CONST_STRPTR TestSoundName(ULONG sound)
{
	return sound < SOUND_COUNT ? SoundNames[sound] : (CONST_STRPTR)"?";
}

CONST_STRPTR TestModeName(ULONG mode)
{
	return mode < TEST_MODES ? ModeNames[mode] : (CONST_STRPTR)"?";
}

CONST_STRPTR TestSoundShort(ULONG sound)
{
	return sound < SOUND_COUNT ? SoundShort[sound] : (CONST_STRPTR)"?";
}

CONST_STRPTR TestModeShort(ULONG mode)
{
	return mode < TEST_MODES ? ModeShort[mode] : (CONST_STRPTR)"?";
}

/******************************************************************************
 *
 * Sample generation (lazy: only what is played is built)
 *
 ******************************************************************************/

STATIC double White(VOID)
{
	/* xorshift32, scaled to -1 .. 1 */
	NoiseState ^= NoiseState << 13;
	NoiseState ^= NoiseState >> 17;
	NoiseState ^= NoiseState << 5;

	return ((double)(LONG)NoiseState) / 2147483648.0;
}

BOOL TestInit(VOID)
{
	ULONG i;
	double pi = 3.14159265358979323846;

	SineTable = AllocVec(SINE_SIZE * sizeof(WORD), MEMF_PUBLIC);
	Cycle1k   = AllocVec(48 * sizeof(WORD), MEMF_PUBLIC);

	if (SineTable == NULL || Cycle1k == NULL) {
		return FALSE;
	}

	for (i = 0; i < SINE_SIZE; i++) {
		SineTable[i] = (WORD)(AMPLITUDE * sin(2.0 * pi * (double)i / (double)SINE_SIZE));
	}

	for (i = 0; i < 48; i++) {
		Cycle1k[i] = (WORD)(AMPLITUDE * sin(2.0 * pi * (double)i / 48.0));
	}

	return TRUE;
}

VOID TestExit(VOID)
{
	TestStop(NULL);

	if (SineTable != NULL) { FreeVec(SineTable); SineTable = NULL; }
	if (Cycle1k != NULL)   { FreeVec(Cycle1k);   Cycle1k = NULL; }
	if (SweepBuf != NULL)  { FreeVec(SweepBuf);  SweepBuf = NULL; }
	if (PinkBuf != NULL)   { FreeVec(PinkBuf);   PinkBuf = NULL; }
	if (LrbBuf != NULL)    { FreeVec(LrbBuf);    LrbBuf = NULL; }
}

STATIC BOOL BuildSweep(VOID)
{
	ULONG frames = MIX_RATE * SWEEP_SECONDS;
	ULONG i, phase = 0;
	double f = 20.0;
	double factor = pow(1000.0, 1.0 / (double)frames);
	LONG fade = 240;
	LONG s;

	if (SweepBuf != NULL) {
		return TRUE;
	}

	if ((SweepBuf = AllocVec(frames * sizeof(WORD), MEMF_PUBLIC)) == NULL) {
		return FALSE;
	}

	for (i = 0; i < frames; i++) {
		s = SineTable[(phase >> 20) & (SINE_SIZE - 1)];

		/* a short fade at both ends so that the loop does not click */
		if ((LONG)i < fade) {
			s = (s * (LONG)i) / fade;
		} else if (i >= frames - (ULONG)fade) {
			s = (s * (LONG)(frames - i)) / fade;
		}

		SweepBuf[i] = (WORD)s;

		phase += (ULONG)(f / (double)MIX_RATE * 4294967296.0);
		f *= factor;
	}

	return TRUE;
}

STATIC BOOL BuildPink(VOID)
{
	ULONG i;
	double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
	double w, pink;

	if (PinkBuf != NULL) {
		return TRUE;
	}

	if ((PinkBuf = AllocVec(PINK_FRAMES * sizeof(WORD), MEMF_PUBLIC)) == NULL) {
		return FALSE;
	}

	/* Paul Kellet's economy pink noise filter */
	for (i = 0; i < PINK_FRAMES; i++) {
		w = White();

		b0 = 0.99886 * b0 + w * 0.0555179;
		b1 = 0.99332 * b1 + w * 0.0750759;
		b2 = 0.96900 * b2 + w * 0.1538520;
		b3 = 0.86650 * b3 + w * 0.3104856;
		b4 = 0.55000 * b4 + w * 0.5329522;
		b5 = -0.7616 * b5 - w * 0.0168980;

		pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
		b6 = w * 0.115926;

		PinkBuf[i] = (WORD)(pink * 0.11 * AMPLITUDE);
	}

	return TRUE;
}

STATIC BOOL BuildLrb(VOID)
{
	ULONG i;
	WORD s;

	if (LrbBuf != NULL) {
		return TRUE;
	}

	if ((LrbBuf = AllocVec(LRB_FRAMES * 2 * sizeof(WORD), MEMF_PUBLIC)) == NULL) {
		return FALSE;
	}

	for (i = 0; i < LRB_FRAMES; i++) {
		s = Cycle1k[i % 48];

		LrbBuf[i * 2]     = (i < 96000UL) ? ((i < 48000UL) ? s : 0) : s;
		LrbBuf[i * 2 + 1] = (i >= 48000UL) ? s : 0;
	}

	return TRUE;
}

/******************************************************************************
 *
 * Level: 0 dB is the sine of the buffers (about -1 dBFS), AHI volume 1.0
 *
 ******************************************************************************/

STATIC LONG VolumeFor(LONG level_db, BOOL voices)
{
	double v = pow(10.0, ((double)level_db + 1.0) / 20.0) * 65536.0;

	if (voices) {
		v /= 6.0;
	}

	if (v < 0.0) {
		v = 0.0;
	}

	if (v > 65536.0) {
		v = 65536.0;
	}

	return (LONG)v;
}

/******************************************************************************
 *
 * AHI client
 *
 ******************************************************************************/

STATIC VOID CloseAhi(VOID)
{
	if (Ctrl != NULL) {
		AHI_FreeAudio(Ctrl);
		Ctrl = NULL;
	}

	if (Device) {
		CloseDevice((struct IORequest *)AhiReq);
		Device = FALSE;
		AHIBase = NULL;
	}

	if (AhiReq != NULL) {
		DeleteIORequest((struct IORequest *)AhiReq);
		AhiReq = NULL;
	}

	if (AhiPort != NULL) {
		DeleteMsgPort(AhiPort);
		AhiPort = NULL;
	}
}

STATIC BOOL OpenAhi(ULONG mode, CONST_STRPTR * error)
{
	struct TagItem tags[5];

	if ((AhiPort = CreateMsgPort()) == NULL ||
	    (AhiReq = (struct AHIRequest *)CreateIORequest(AhiPort, sizeof(struct AHIRequest))) == NULL) {
		*error = "Out of memory";
		CloseAhi();
		return FALSE;
	}

	AhiReq->ahir_Version = 4;

	if (OpenDevice((STRPTR)AHINAME, AHI_NO_UNIT, (struct IORequest *)AhiReq, 0) != 0) {
		*error = "Can't open ahi.device (AHI 4 or later is required)";
		CloseAhi();
		return FALSE;
	}

	Device = TRUE;
	AHIBase = (struct Library *)AhiReq->ahir_Std.io_Device;

	tags[0].ti_Tag = AHIA_AudioID;  tags[0].ti_Data = ModeIds[mode];
	tags[1].ti_Tag = AHIA_MixFreq;  tags[1].ti_Data = MIX_RATE;
	tags[2].ti_Tag = AHIA_Channels; tags[2].ti_Data = CHANNELS;
	tags[3].ti_Tag = AHIA_Sounds;   tags[3].ti_Data = CHANNELS;
	tags[4].ti_Tag = TAG_DONE;      tags[4].ti_Data = 0;

	if ((Ctrl = AHI_AllocAudioA(tags)) == NULL) {
		*error = "Can't open this HDMI mode: is another program using HDMI audio, is the driver installed, "
		         "and is this mode in AHI's database (new mode file + reboot)?";
		CloseAhi();
		return FALSE;
	}

	return TRUE;
}

/******************************************************************************
 *
 * TestPlay()
 *
 ******************************************************************************/

STATIC BOOL LoadMono(ULONG slot, WORD * data, ULONG frames)
{
	struct AHISampleInfo info;

	info.ahisi_Type    = AHIST_M16S;
	info.ahisi_Address = data;
	info.ahisi_Length  = frames;

	return (BOOL)(AHI_LoadSound((UWORD)slot, AHIST_SAMPLE, &info, Ctrl) == AHIE_OK);
}

STATIC BOOL LoadStereo(ULONG slot, WORD * data, ULONG frames)
{
	struct AHISampleInfo info;

	info.ahisi_Type    = AHIST_S16S;
	info.ahisi_Address = data;
	info.ahisi_Length  = frames;

	return (BOOL)(AHI_LoadSound((UWORD)slot, AHIST_SAMPLE, &info, Ctrl) == AHIE_OK);
}

BOOL TestPlay(ULONG mode, ULONG sound, LONG level_db, CONST_STRPTR * error)
{
	struct TagItem play[2];
	LONG volume = VolumeFor(level_db, sound == SOUND_VOICES);
	ULONG i;
	BOOL ok = FALSE;

	if (mode >= TEST_MODES || sound >= SOUND_COUNT) {
		*error = "Bad mode or sound";
		return FALSE;
	}

	TestStop(NULL);

	if (SineTable == NULL || Cycle1k == NULL) {
		*error = "Out of memory";
		return FALSE;
	}

	if (!OpenAhi(mode, error)) {
		return FALSE;
	}

	switch (sound) {
	case SOUND_LEFT:
	case SOUND_RIGHT:
	case SOUND_BOTH:
		ok = LoadMono(0, Cycle1k, 48);
		break;

	case SOUND_LRB:
		ok = BuildLrb() && LoadStereo(0, LrbBuf, LRB_FRAMES);
		break;

	case SOUND_SWEEP:
		ok = BuildSweep() && LoadMono(0, SweepBuf, MIX_RATE * SWEEP_SECONDS);
		break;

	case SOUND_PINK:
		ok = BuildPink() && LoadMono(0, PinkBuf, PINK_FRAMES);
		break;

	case SOUND_VOICES:
		ok = LoadMono(0, Cycle1k, 48);

		for (i = 1; ok && i < CHANNELS; i++) {
			ok = LoadMono(i, Cycle1k, 48);
		}
		break;
	}

	if (!ok) {
		*error = "Can't prepare the sound";
		CloseAhi();
		return FALSE;
	}

	switch (sound) {
	case SOUND_LEFT:
		AHI_SetFreq(0, MIX_RATE, Ctrl, AHISF_IMM);
		AHI_SetVol(0, volume, 0x00000, Ctrl, AHISF_IMM);
		AHI_SetSound(0, 0, 0, 48, Ctrl, AHISF_IMM);
		break;

	case SOUND_RIGHT:
		AHI_SetFreq(0, MIX_RATE, Ctrl, AHISF_IMM);
		AHI_SetVol(0, volume, 0x10000, Ctrl, AHISF_IMM);
		AHI_SetSound(0, 0, 0, 48, Ctrl, AHISF_IMM);
		break;

	case SOUND_BOTH:
		AHI_SetFreq(0, MIX_RATE, Ctrl, AHISF_IMM);
		AHI_SetVol(0, volume, 0x08000, Ctrl, AHISF_IMM);
		AHI_SetSound(0, 0, 0, 48, Ctrl, AHISF_IMM);
		break;

	case SOUND_LRB:
		AHI_SetFreq(0, MIX_RATE, Ctrl, AHISF_IMM);
		AHI_SetVol(0, volume, 0x08000, Ctrl, AHISF_IMM);
		AHI_SetSound(0, 0, 0, LRB_FRAMES, Ctrl, AHISF_IMM);
		break;

	case SOUND_SWEEP:
		AHI_SetFreq(0, MIX_RATE, Ctrl, AHISF_IMM);
		AHI_SetVol(0, volume, 0x08000, Ctrl, AHISF_IMM);
		AHI_SetSound(0, 0, 0, MIX_RATE * SWEEP_SECONDS, Ctrl, AHISF_IMM);
		break;

	case SOUND_PINK:
		AHI_SetFreq(0, MIX_RATE, Ctrl, AHISF_IMM);
		AHI_SetVol(0, volume, 0x08000, Ctrl, AHISF_IMM);
		AHI_SetSound(0, 0, 0, PINK_FRAMES, Ctrl, AHISF_IMM);
		break;

	case SOUND_VOICES:
		for (i = 0; i < CHANNELS; i++) {
			AHI_SetFreq((UWORD)i, VoiceRates[i], Ctrl, AHISF_IMM);
			AHI_SetVol((UWORD)i, volume, (LONG)((i * 0x10000UL) / (CHANNELS - 1)), Ctrl, AHISF_IMM);
			AHI_SetSound((UWORD)i, (UWORD)i, 0, 48, Ctrl, AHISF_IMM);
		}
		break;
	}

	play[0].ti_Tag = AHIC_Play; play[0].ti_Data = TRUE;
	play[1].ti_Tag = TAG_DONE;  play[1].ti_Data = 0;

	if (AHI_ControlAudioA(Ctrl, play) != AHIE_OK) {
		*error = "AHI refused to start (the HDMI driver could not start playback: see the Driver page)";
		CloseAhi();
		return FALSE;
	}

	Playing = TRUE;
	PlayingSound = sound;

	return TRUE;
}

VOID TestSetLevel(LONG level_db)
{
	ULONG i;
	LONG volume;

	if (!Playing || Ctrl == NULL) {
		return;
	}

	volume = VolumeFor(level_db, PlayingSound == SOUND_VOICES);

	switch (PlayingSound) {
	case SOUND_LEFT:
		AHI_SetVol(0, volume, 0x00000, Ctrl, AHISF_IMM);
		break;

	case SOUND_RIGHT:
		AHI_SetVol(0, volume, 0x10000, Ctrl, AHISF_IMM);
		break;

	case SOUND_VOICES:
		for (i = 0; i < CHANNELS; i++) {
			AHI_SetVol((UWORD)i, volume, (LONG)((i * 0x10000UL) / (CHANNELS - 1)), Ctrl, AHISF_IMM);
		}
		break;

	default:
		AHI_SetVol(0, volume, 0x08000, Ctrl, AHISF_IMM);
		break;
	}
}

/******************************************************************************
 *
 * TestStop()
 *
 ******************************************************************************/

VOID TestStop(struct TestResult * result)
{
	struct TagItem stop[2];
	struct DriverInfo info;

	if (Playing && Ctrl != NULL) {
		stop[0].ti_Tag = AHIC_Play; stop[0].ti_Data = FALSE;
		stop[1].ti_Tag = TAG_DONE;  stop[1].ti_Data = 0;

		AHI_ControlAudioA(Ctrl, stop);

		/* The counters of the driver keep the values of the run that just ended */
		if (result != NULL) {
			DrvRead(&info);

			result->valid            = info.present;
			result->mix_bits         = info.mix_bits;
			result->segments         = ((info.features & 1) != 0) ? info.irqs : info.fills;
			result->resyncs          = info.resyncs;
			result->fault            = info.fault;
			result->max_burst_us     = info.max_burst_us;
			result->pre_skips        = info.pre_skips;
			result->interrupt_driver = (BOOL)((info.features & 1) != 0);
		}
	} else if (result != NULL) {
		result->valid = FALSE;
	}

	Playing = FALSE;
	CloseAhi();
}

BOOL TestIsPlaying(VOID)
{
	return Playing;
}

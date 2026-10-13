/******************************************************************************
 *
 * Update.c -- refreshing the pages
 *
 ******************************************************************************/

#include <exec/types.h>
#include <devices/timer.h>

#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/muimaster.h>
#include <proto/timer.h>

#include <stdio.h>
#include <string.h>

#include "DSP.h"
#include "DrvDefs.h"
#include "Driver.h"
#include "GUI.h"
#include "HW.h"
#include "Scope.h"
#include "Test.h"
#include "Update.h"

/* Main.c ticks every 20 ms; the Test page counts its time in ticks */
#define TICK_MS_FOR_TESTS 20UL

struct ScopeFrame ScopeFrameData;

STATIC BOOL HwReady = FALSE;

/* One text buffer per row: MUI keeps the pointer it is given */
STATIC TEXT RowText[DRV_ROWS][96];
STATIC TEXT HeadText[96];

/* The previous reading, to turn counters into rates */
STATIC ULONG PrevSegments = 0;
STATIC ULONG PrevSecs = 0;
STATIC ULONG PrevMicros = 0;
STATIC BOOL  HavePrev = FALSE;

/******************************************************************************
 *
 * SetRow() -- only touches MUI when the text changes
 *
 ******************************************************************************/

STATIC VOID SetRow(struct ObjApp * app, LONG row)
{
	STATIC TEXT Shown[DRV_ROWS][96];

	if (strcmp(Shown[row], RowText[row]) != 0) {
		strcpy(Shown[row], RowText[row]);
		set(app->TX_DrvValue[row], MUIA_Text_Contents, RowText[row]);
	}
}

/******************************************************************************
 *
 * UpdateDriverPage()
 *
 ******************************************************************************/

VOID UpdateDriverPage(struct ObjApp * app)
{
	struct DriverInfo info;
	struct timeval tv;
	STATIC TEXT ShownHead[96];
	ULONG dt_ms;
	ULONG rate_x10;
	ULONG i;

	DrvRead(&info);
	GetSysTime(&tv);

	if (!info.present) {
		strcpy(HeadText, "Driver not loaded");

		for (i = 0; i < DRV_ROWS; i++) {
			strcpy(RowText[i], "-");
			SetRow(app, (LONG)i);
		}

		HavePrev = FALSE;
	} else {
		BOOL playing = ((info.state & HDMI_STATE_RUNNING) != 0);
		BOOL interrupt_driver = ((info.features & 1) != 0);

		if (info.fault != 0 || (info.state & HDMI_STATE_POISONED) != 0) {
			sprintf(HeadText, "\33c\33bFAULT: %s", DrvFaultName(info.fault));
		} else if (playing) {
			sprintf(HeadText, "\33c\33bPlaying%s", info.resyncs != 0 ? " (with resyncs)" : "");
		} else if ((info.state & HDMI_STATE_OWNED) != 0) {
			strcpy(HeadText, "\33c\33bOpen, not playing");
		} else {
			strcpy(HeadText, "\33c\33bIdle");
		}

		sprintf(RowText[DRV_DRIVER], "%s (v%ld.%ld)", info.ident[0] != 0 ? (char *)info.ident : "emu68hdmi.audio", (LONG)info.version, (LONG)info.revision);

		strcpy(RowText[DRV_INTERRUPTS], interrupt_driver ? "yes" : "no (polling)");

		sprintf(RowText[DRV_STATE], "%s%s%s%s%s",
			(info.state & HDMI_STATE_OWNED) != 0 ? "owned " : "free ",
			(info.state & HDMI_STATE_WORKER) != 0 ? "worker " : "",
			playing ? "running " : "",
			(info.state & HDMI_STATE_STOPPING) != 0 ? "stopping " : "",
			(info.state & HDMI_STATE_POISONED) != 0 ? "POISONED" : "");

		sprintf(RowText[DRV_STAGE], "%s (%ld)", DrvStageName(info.stage), (LONG)info.stage);
		sprintf(RowText[DRV_FAULT], "%s (%ld)", DrvFaultName(info.fault), (LONG)info.fault);

		if (info.mix_bits == 32) {
			strcpy(RowText[DRV_FORMAT], "HiFi 32 -> 24 bit");
		} else if (info.mix_bits == 16) {
			strcpy(RowText[DRV_FORMAT], "16 bit");
		} else {
			strcpy(RowText[DRV_FORMAT], "-");
		}

		if (interrupt_driver) {
			sprintf(RowText[DRV_IRQLINE], "GIC %ld", (LONG)info.irq_id);
			sprintf(RowText[DRV_SEGMENTS], "%ld", (LONG)info.irqs);
		} else {
			strcpy(RowText[DRV_IRQLINE], "-");
			sprintf(RowText[DRV_SEGMENTS], "%ld", (LONG)info.fills);
		}

		/* Segment rate: ~10.4 per second for the 96 ms ring, ~62.5 for the 16 ms one */
		strcpy(RowText[DRV_RATE], "-");

		if (HavePrev && interrupt_driver && playing) {
			dt_ms = (tv.tv_secs - PrevSecs) * 1000UL;
			dt_ms += (tv.tv_micro / 1000UL);
			dt_ms -= (PrevMicros / 1000UL);

			if (dt_ms >= 100 && info.irqs >= PrevSegments) {
				rate_x10 = ((info.irqs - PrevSegments) * 10000UL) / dt_ms;
				sprintf(RowText[DRV_RATE], "%ld.%ld/s (%s)", (LONG)(rate_x10 / 10), (LONG)(rate_x10 % 10),
					rate_x10 > 300 ? "16 ms" : "96 ms");
			}
		}

		PrevSegments = info.irqs;
		PrevSecs = tv.tv_secs;
		PrevMicros = tv.tv_micro;
		HavePrev = TRUE;

		if (interrupt_driver) {
			sprintf(RowText[DRV_WAKES], "%ld", (LONG)info.wakes);
			sprintf(RowText[DRV_PRETIMER], "%ld calls, %ld skips", (LONG)info.pre_calls, (LONG)info.pre_skips);
			sprintf(RowText[DRV_BURST], "%ld us", (LONG)info.max_burst_us);
			sprintf(RowText[DRV_LAG], "%ld us", (LONG)info.max_gap_us);
			sprintf(RowText[DRV_RESYNCS], "%ld", (LONG)info.resyncs);
		} else {
			strcpy(RowText[DRV_WAKES], "-");
			strcpy(RowText[DRV_PRETIMER], "-");
			strcpy(RowText[DRV_BURST], "-");
			sprintf(RowText[DRV_LAG], "%ld us (poll)", (LONG)info.max_gap_us);
			sprintf(RowText[DRV_RESYNCS], "%ld", (LONG)info.resyncs);
		}

		for (i = 0; i < DRV_ROWS; i++) {
			SetRow(app, (LONG)i);
		}
	}

	if (strcmp(ShownHead, HeadText) != 0) {
		strcpy(ShownHead, HeadText);
		set(app->TX_DrvHead, MUIA_Text_Contents, HeadText);
	}
}

/******************************************************************************
 *
 * UpdateInit()
 *
 ******************************************************************************/

BOOL UpdateInit(VOID)
{
	HwReady = HwInit();

	DspReset(&ScopeFrameData);

	return DspInit();
}

/******************************************************************************
 *
 * UpdateScopePage()
 *
 * The samples are read behind the DMA read pointer of the HDMI audio channel:
 * what was just played, so the display follows what the ear hears.
 *
 ******************************************************************************/

VOID UpdateScopePage(struct ObjApp * app)
{
	STATIC struct AudioDma dma;
	STATIC TEXT Info[160];
	STATIC TEXT ShownInfo[160];
	ULONG ms;

	if (HwReady && HwFindDma(&dma) &&
	    HwReadRecent(&dma, ScopeFrameData.left, ScopeFrameData.right, SCOPE_FRAMES) == SCOPE_FRAMES) {
		ScopeFrameData.active = TRUE;
	} else {
		ScopeFrameData.active = FALSE;
	}

	DspAnalyze(&ScopeFrameData);

	if (!HwReady) {
		strcpy(Info, "Needs Emu68 on a Pi 4");
	} else if (ScopeFrameData.active) {
		ms = (dma.seg_bytes / 8) * 1000UL / 48000UL;
		sprintf(Info, "DMA channel %ld: ring of %ld x %ld ms, %ld / %ld frames",
			(LONG)dma.channel, (LONG)dma.segments, (LONG)ms,
			(LONG)dma.pos_frames, (LONG)dma.ring_frames);
	} else {
		strcpy(Info, "No HDMI audio");
	}

	if (strcmp(ShownInfo, Info) != 0) {
		strcpy(ShownInfo, Info);
		set(app->TX_ScopeInfo, MUIA_Text_Contents, Info);
	}

	DoMethod(app->SC_Meter, MUIM_Scope_Refresh);
	DoMethod(app->SC_Wave, MUIM_Scope_Refresh);
	DoMethod(app->SC_Spectrum, MUIM_Scope_Refresh);
}

/******************************************************************************
 *
 * Test page
 *
 ******************************************************************************/

STATIC ULONG PlayStartTick = 0;
STATIC ULONG PlayLimitTicks = 0;      /* 0 = no limit */
STATIC LONG  PlayLevel = 0;
STATIC ULONG PlayMode = 0;
STATIC ULONG PlaySound = 0;

STATIC TEXT StateText[200];
STATIC TEXT ResultText[220];

STATIC VOID ShowState(struct ObjApp * app, CONST_STRPTR text)
{
	strncpy(StateText, text, sizeof(StateText) - 1);
	StateText[sizeof(StateText) - 1] = 0;
	set(app->TX_TestState, MUIA_Text_Contents, StateText);
}

STATIC VOID ShowResult(struct ObjApp * app, CONST struct TestResult * r)
{
	if (!r->valid) {
		strcpy(ResultText, "Driver not loaded: no result.");
	} else {
		sprintf(ResultText, "Last run: %s, %ld segments\n%ld resyncs, fault: %s\nburst %ld us, AHI skips %ld%s",
			r->mix_bits == 32 ? "HiFi" : "16 bit", (LONG)r->segments, (LONG)r->resyncs, DrvFaultName(r->fault),
			(LONG)r->max_burst_us, (LONG)r->pre_skips,
			r->interrupt_driver ? "" : " (polling driver)");
	}

	set(app->TX_TestResult, MUIA_Text_Contents, ResultText);
}

STATIC VOID StopTest(struct ObjApp * app, CONST_STRPTR why)
{
	struct TestResult result;
	STATIC TEXT buf[120];

	TestStop(&result);

	sprintf(buf, "Stopped%s%s", why[0] != 0 ? ": " : "", why);
	ShowState(app, buf);
	ShowResult(app, &result);
}

/******************************************************************************
 *
 * Master volume
 *
 ******************************************************************************/

/* Q16 gain of a number of dB (<= 0): 0.891251 (-1 dB) multiplied in once per dB, no math library */
STATIC ULONG DbToGain(LONG db)
{
	ULONG gain = 65536;

	while (db < 0) {
		gain = (gain * 58409UL) >> 16;
		db++;
	}

	return gain;
}

VOID UpdateMaster(struct ObjApp * app)
{
	STATIC LONG shown_disabled = -1;
	ULONG features, current, wanted;
	LONG db = 0, muted = 0, disabled;

	get(app->SL_Master, MUIA_Numeric_Value, &db);
	get(app->TG_Mute, MUIA_Selected, &muted);

	wanted = muted ? 0 : DbToGain(db);

	if (!DrvReadGain(&features, &current)) {
		return;   /* the driver is not loaded: the next look gives it the volume */
	}

	disabled = ((features & HDMI_FEATURE_GAIN) == 0);

	if (disabled != shown_disabled) {
		shown_disabled = disabled;
		set(app->SL_Master, MUIA_Disabled, disabled);
		set(app->TG_Mute, MUIA_Disabled, disabled);
	}

	if (!disabled && current != wanted) {
		DrvSetGain(wanted);
	}
}

VOID HandleAction(struct ObjApp * app, ULONG id, ULONG tick)
{
	ULONG mode = 0, duration = 0;
	LONG level = 0;
	CONST_STRPTR error = NULL;
	STATIC TEXT buf[200];
	STATIC CONST ULONG limits[4] = { 5, 10, 30, 0 };

	get(app->SL_Level, MUIA_Numeric_Value, &level);

	if (id >= MID_MENU_FIRST) {
		MenuAction(app, id);
		return;
	}

	if (id == MID_MASTER || id == MID_MUTE) {
		if (id == MID_MUTE) {
			SyncMenus(app);
		}

		UpdateMaster(app);
		return;
	}

	if (id == MID_LEVEL) {
		PlayLevel = level;
		TestSetLevel(level);
		return;
	}

	if (id == MID_STOP) {
		if (TestIsPlaying()) {
			StopTest(app, "");
		}

		return;
	}

	if (id >= MID_PLAY_BASE && id < MID_PLAY_BASE + SOUND_COUNT) {
		get(app->CY_Mode, MUIA_Cycle_Active, &mode);
		get(app->CY_Duration, MUIA_Cycle_Active, &duration);

		PlaySound = id - MID_PLAY_BASE;
		PlayMode = mode;
		PlayLevel = level;
		PlayStartTick = tick;
		PlayLimitTicks = (duration < 4) ? limits[duration] * (1000UL / TICK_MS_FOR_TESTS) : 0;

		if (!TestPlay(mode, PlaySound, level, &error)) {
			sprintf(buf, "Can't play: %s", error);
			ShowState(app, buf);
			return;
		}

		sprintf(buf, "Playing: %s, %s, %ld dB", TestSoundShort(PlaySound), TestModeShort(mode), (LONG)level);
		ShowState(app, buf);
	}
}

VOID UpdateTestPage(struct ObjApp * app, ULONG tick)
{
	STATIC TEXT buf[200];
	ULONG seconds;

	if (!TestIsPlaying()) {
		return;
	}

	seconds = ((tick - PlayStartTick) * TICK_MS_FOR_TESTS) / 1000UL;

	if (PlayLimitTicks != 0 && (tick - PlayStartTick) >= PlayLimitTicks) {
		StopTest(app, "time limit reached");
		return;
	}

	sprintf(buf, "Playing: %s, %s, %ld dB (%ld s)", TestSoundShort(PlaySound), TestModeShort(PlayMode), (LONG)PlayLevel,
		(LONG)seconds);
	ShowState(app, buf);
}

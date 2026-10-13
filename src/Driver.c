/******************************************************************************
 *
 * Driver.c -- reading the status of the Emu68 HDMI AHI driver
 *
 ******************************************************************************/

#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/lists.h>

#include <proto/exec.h>

#include <string.h>

#include "DrvDefs.h"
#include "Driver.h"

extern struct ExecBase * SysBase;

/* Assembler: the HardwareControl vector of the driver, see DrvAsm.a */
LONG DrvQuery(struct Library * driver, ULONG attribute);
LONG DrvControl(struct Library * driver, ULONG attribute, LONG argument);

/* The same lookup as HDMIDiag: FindName() under Forbid(), then OpenLibrary(), so a driver that AHI
** has not loaded is never loaded (and never started) by looking at it. */
STATIC struct Library * OpenDriver(VOID)
{
	struct Library * driver = NULL;

	Forbid();

	if (FindName(&SysBase->LibList, (STRPTR)"emu68hdmi.audio") != NULL) {
		driver = OpenLibrary((STRPTR)"emu68hdmi.audio", 4);
	}

	Permit();

	return driver;
}

/******************************************************************************
 *
 * DrvRead()
 *
 * Looks the driver up with OpenDriver(): one that AHI has not loaded is never
 * loaded (and never started) by looking at it.
 *
 ******************************************************************************/

VOID DrvRead(struct DriverInfo * info)
{
	struct Library * driver = NULL;
	CONST_STRPTR id;
	ULONG len;

	memset(info, 0, sizeof(struct DriverInfo));

	driver = OpenDriver();

	if (driver == NULL) {
		return;
	}

	info->present  = TRUE;
	info->version  = driver->lib_Version;
	info->revision = driver->lib_Revision;

	id = (CONST_STRPTR)driver->lib_IdString;

	if (id != NULL) {
		len = strlen(id);

		while (len > 0 && (id[len - 1] == '\r' || id[len - 1] == '\n')) {
			len--;
		}

		if (len > sizeof(info->ident) - 1) {
			len = sizeof(info->ident) - 1;
		}

		memcpy(info->ident, id, len);
		info->ident[len] = 0;
	}

	info->features      = (ULONG)DrvQuery(driver, HDMI_QUERY_FEATURES);

	if ((info->features & HDMI_FEATURE_GAIN) != 0) {
		info->gain = (ULONG)DrvQuery(driver, HDMI_QUERY_GAIN);
	}

	info->state         = (ULONG)DrvQuery(driver, HDMI_QUERY_STATE);
	info->stage         = (ULONG)DrvQuery(driver, HDMI_QUERY_STAGE);
	info->fault         = (ULONG)DrvQuery(driver, HDMI_QUERY_FAULT);
	info->mix_bits      = (ULONG)DrvQuery(driver, HDMI_QUERY_MIX_BITS);
	info->fills         = (ULONG)DrvQuery(driver, HDMI_QUERY_FILLS);
	info->resyncs       = (ULONG)DrvQuery(driver, HDMI_QUERY_RECOVERIES);
	info->resync_reason = (ULONG)DrvQuery(driver, HDMI_QUERY_RECOVERY_FAULT);
	info->max_gap_us    = (ULONG)DrvQuery(driver, HDMI_QUERY_GAP);

	if ((info->features & 1) != 0) {
		info->irq_id       = (ULONG)DrvQuery(driver, HDMI_QUERY_IRQ_ID);
		info->irqs         = (ULONG)DrvQuery(driver, HDMI_QUERY_IRQS);
		info->wakes        = (ULONG)DrvQuery(driver, HDMI_QUERY_WAKES);
		info->resync_at    = (ULONG)DrvQuery(driver, HDMI_QUERY_RESYNC_AT);
		info->pre_calls    = (ULONG)DrvQuery(driver, HDMI_QUERY_PRE_CALLS);
		info->pre_skips    = (ULONG)DrvQuery(driver, HDMI_QUERY_PRE_SKIPS);
		info->max_burst_us = (ULONG)DrvQuery(driver, HDMI_QUERY_MAX_BURST);
	}

	CloseLibrary(driver);
}

/******************************************************************************
 *
 * DrvReadGain() / DrvSetGain()
 *
 ******************************************************************************/

BOOL DrvReadGain(ULONG * features, ULONG * gain)
{
	struct Library * driver = OpenDriver();

	*features = 0;
	*gain = 0;

	if (driver == NULL) {
		return FALSE;
	}

	*features = (ULONG)DrvQuery(driver, HDMI_QUERY_FEATURES);

	if ((*features & HDMI_FEATURE_GAIN) != 0) {
		*gain = (ULONG)DrvQuery(driver, HDMI_QUERY_GAIN);
	}

	CloseLibrary(driver);

	return TRUE;
}

BOOL DrvSetGain(ULONG gain)
{
	struct Library * driver = OpenDriver();
	BOOL done = FALSE;

	if (driver == NULL) {
		return FALSE;
	}

	if ((((ULONG)DrvQuery(driver, HDMI_QUERY_FEATURES)) & HDMI_FEATURE_GAIN) != 0) {
		done = (DrvControl(driver, HDMI_SET_GAIN, (LONG)gain) != 0);
	}

	CloseLibrary(driver);

	return done;
}

/******************************************************************************
 *
 * DrvStageName() / DrvFaultName()
 *
 ******************************************************************************/

CONST_STRPTR DrvStageName(ULONG stage)
{
	switch (stage) {
	case 1:  return "START entry";
	case 2:  return "START wake / spawn";
	case 9:  return "opening the watchdog timer";
	case 10: return "reading the DMA channel mask";
	case 11: return "allocating DMA memory";
	case 12: return "CachePreDMA";
	case 13: return "selecting a DMA channel";
	case 14: return "resetting the DMA channel";
	case 15: return "preparing HDMI";
	case 16: return "enabling DMA and HDMI";
	case 20: return "Player hook";
	case 21: return "Mixer hook";
	case 22: return "Mixer returned";
	case 29: return "waiting for the DMA interrupt";
	case 30: return "refilling the ring";
	case 40: return "stopping DMA";
	case 41: return "restoring HDMI";
	case 42: return "freeing DMA memory";
	case 50: return "worker idle";
	case 60: return "FreeAudio entered";
	case 61: return "FreeAudio waiting for the worker";
	case 62: return "worker returning to DOS";
	case 63: return "FreeAudio done";
	case 70: return "recovering after a missed deadline";
	}

	return "not started";
}

CONST_STRPTR DrvFaultName(ULONG fault)
{
	switch (fault) {
	case 0:  return "none";
	case 1:  return "mixer or hook fault";
	case 2:  return "DMA error";
	case 3:  return "gap (late observation)";
	case 4:  return "publish";
	case 5:  return "HDMI restore failed";
	case 6:  return "DMA reset";
	case 7:  return "drain";
	case 8:  return "timer port";
	case 9:  return "timer request";
	case 10: return "timer open";
	case 11: return "DMA mask";
	case 12: return "allocation";
	case 13: return "cache";
	case 14: return "no DMA channel";
	case 15: return "HDMI prepare";
	case 16: return "process";
	case 17: return "timer I/O";
	case 18: return "mix deadline";
	case 19: return "interrupt setup (gic400.library?)";
	case 20: return "interrupt lost";
	}

	return "unknown";
}

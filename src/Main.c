/******************************************************************************
 *
 * Emu68HDMI-AHI-Mixer
 *
 * Shows what the Emu68 HDMI AHI driver is doing (meters, scope, status) and
 * plays test sounds through it. It never writes to the driver or to the
 * hardware: everything it shows is read.
 *
 ******************************************************************************/

#include <stdlib.h>

#include <dos/dos.h>
#include <exec/types.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <libraries/mui.h>
#include <utility/utility.h>

#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/timer.h>
#include <proto/utility.h>

#include "GUI.h"
#include "Prefs.h"
#include "Scope.h"
#include "Test.h"
#include "Update.h"

/* MUI opens its settings window on this program's stack: the default 4 KB is not enough
** (the Guru that sent us looking). SAS/C start-up code raises the stack to this size. */
long __stack = 16384;

/* The refresh rhythm: one tick every TICK_MS, the Driver page every 12th tick (240 ms) */
#define TICK_MS      20
#define DRIVER_TICKS 12
#define MASTER_TICKS 24   /* 480 ms */

/* PAGE: the page shown at start-up (0 Driver, 1 Scope, 2 Test; the default is Scope).
** PLAY, MODE, LEVEL: start a test sound at once (sound 0..6, mode 0..3, level -60..-1 dB).
** REFRESH: scope refresh, 0 = 50 per second, 1 = 25, 2 = 12, 3 = 6, 4 = 3, 5 = paused. */
#define TEMPLATE "PAGE/N,PLAY/N,MODE/N,LEVEL/N,REFRESH/N"

enum {
	OPT_PAGE = 0,
	OPT_PLAY,
	OPT_MODE,
	OPT_LEVEL,
	OPT_REFRESH,
	OPT_COUNT
};

struct Library * MUIMasterBase = NULL;
struct Device *  TimerBase = NULL;

STATIC struct ObjApp *     App = NULL;
STATIC ULONG               Ticks = 0;
STATIC struct MsgPort *    TimerPort = NULL;
STATIC struct timerequest * TimerReq = NULL;
STATIC BOOL                TimerPending = FALSE;

/******************************************************************************
 *
 * Timer: one request re-armed after each tick. tr_time is consumed by the
 * device when the request completes, so it is written again every time.
 *
 ******************************************************************************/

STATIC BOOL OpenTimer(VOID)
{
	if ((TimerPort = CreateMsgPort()) == NULL) {
		return FALSE;
	}

	if ((TimerReq = (struct timerequest *)CreateIORequest(TimerPort, sizeof(struct timerequest))) == NULL) {
		return FALSE;
	}

	if (OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)TimerReq, 0) != 0) {
		DeleteIORequest((struct IORequest *)TimerReq);
		TimerReq = NULL;
		return FALSE;
	}

	TimerBase = TimerReq->tr_node.io_Device;

	return TRUE;
}

STATIC VOID ArmTimer(VOID)
{
	TimerReq->tr_node.io_Command = TR_ADDREQUEST;
	TimerReq->tr_time.tv_secs    = 0;
	TimerReq->tr_time.tv_micro   = TICK_MS * 1000;

	SendIO((struct IORequest *)TimerReq);
	TimerPending = TRUE;
}

STATIC VOID CloseTimer(VOID)
{
	if (TimerReq != NULL) {
		if (TimerPending) {
			AbortIO((struct IORequest *)TimerReq);
			WaitIO((struct IORequest *)TimerReq);
			TimerPending = FALSE;
		}

		if (TimerBase != NULL) {
			CloseDevice((struct IORequest *)TimerReq);
			TimerBase = NULL;
		}

		DeleteIORequest((struct IORequest *)TimerReq);
		TimerReq = NULL;
	}

	if (TimerPort != NULL) {
		DeleteMsgPort(TimerPort);
		TimerPort = NULL;
	}
}

/******************************************************************************
 *
 * ShowFatalRequester() / CleanExit() / InitApp()
 *
 ******************************************************************************/

STATIC VOID ShowFatalRequester(CONST_STRPTR text)
{
	struct EasyStruct es;

	es.es_StructSize   = sizeof(struct EasyStruct);
	es.es_Flags        = 0;
	es.es_Title        = (CONST_STRPTR)APP_NAME;
	es.es_TextFormat   = text;
	es.es_GadgetFormat = (CONST_STRPTR)"Ok";

	EasyRequest(NULL, &es, NULL, NULL);
}

STATIC VOID CleanExit(CONST_STRPTR why)
{
	TestExit();
	CloseTimer();

	if (App != NULL) {
		DisposeApp(App);
		App = NULL;
	}

	ScopeFreeClass();

	if (MUIMasterBase != NULL) {
		CloseLibrary(MUIMasterBase);
		MUIMasterBase = NULL;
	}

	if (why != NULL) {
		ShowFatalRequester(why);
		exit(RETURN_FAIL);
	}
}

STATIC BOOL InitApp(VOID)
{
	if ((MUIMasterBase = OpenLibrary("muimaster.library", 19)) == NULL) {
		CleanExit("Can't open muimaster.library v19\n");
	}

	if (!OpenTimer()) {
		CleanExit("Can't open timer.device\n");
	}

	return TRUE;
}

/******************************************************************************
 *
 * Tick() -- called every TICK_MS
 *
 ******************************************************************************/

/* Ticks (20 ms) between two redraws of the scope, by the entries of the Refresh cycle; 0 = paused */
STATIC CONST ULONG ScopeDividers[REFRESH_COUNT] = { 1, 2, 4, 8, 16, 0 };

STATIC ULONG RefreshIndex(VOID)
{
	ULONG index = 0;

	get(App->CY_Refresh, MUIA_Cycle_Active, &index);

	return (index < REFRESH_COUNT) ? index : 0;
}

STATIC VOID Tick(VOID)
{
	STATIC ULONG ticks = 0;
	ULONG page = 0;
	ULONG divider;

	ticks++;

	if (Prefs.compact) {
		page = PAGE_SCOPE;
	} else {
		get(App->GR_Pages, MUIA_Group_ActivePage, &page);
	}

	Ticks = ticks;
	divider = ScopeDividers[RefreshIndex()];

	if ((ticks % MASTER_TICKS) == 0) {
		UpdateMaster(App);
	}

	if (page == PAGE_DRIVER && (ticks % DRIVER_TICKS) == 0) {
		UpdateDriverPage(App);
	}

	if ((ticks % DRIVER_TICKS) == 0) {
		UpdateTestPage(App, ticks);
	}

	if (page == PAGE_SCOPE && divider != 0 && (ticks % divider) == 0) {
		UpdateScopePage(App);
	}
}

/******************************************************************************
 *
 * main()
 *
 ******************************************************************************/

int main(int argc, char ** argv)
{
	struct RDArgs * args;
	LONG argp[OPT_COUNT];
	ULONG sigs = 0;
	ULONG timer_mask;
	ULONG ret;

	(VOID)argc;
	(VOID)argv;

	/* argc is 0 when started from Workbench: no command line to read then */
	args = (argc > 0) ? ReadArgs(TEMPLATE, argp, NULL) : NULL;

	InitApp();

	if (!UpdateInit()) {
		CleanExit("Can't prepare the analysis tables\n");
	}

	if (!TestInit()) {
		CleanExit("Can't prepare the test sounds\n");
	}

	if (!ScopeInitClass()) {
		CleanExit("Can't create the scope class\n");
	}

	PrefsLoad();

	if ((App = CreateApp()) == NULL) {
		CleanExit("Can't create the application\n");
	}

	/* Settings (mode, level, duration, refresh) come back from the last session */
	DoMethod(App->App, MUIM_Application_Load, MUIV_Application_Load_ENV);

	set(App->GR_Pages, MUIA_Group_ActivePage, PAGE_SCOPE);

	if (args != NULL && argp[OPT_PAGE] != 0) {
		set(App->GR_Pages, MUIA_Group_ActivePage, *(LONG *)argp[OPT_PAGE]);
	}

	if (args != NULL && argp[OPT_REFRESH] != 0) {
		set(App->CY_Refresh, MUIA_Cycle_Active, *(LONG *)argp[OPT_REFRESH]);
	}

	if (args != NULL && argp[OPT_MODE] != 0) {
		set(App->CY_Mode, MUIA_Cycle_Active, *(LONG *)argp[OPT_MODE]);
	}

	if (args != NULL && argp[OPT_LEVEL] != 0) {
		set(App->SL_Level, MUIA_Numeric_Value, *(LONG *)argp[OPT_LEVEL]);
	}

	SyncMenus(App);

	set(App->WI_Main, MUIA_Window_Open, TRUE);
	UpdateMaster(App);
	UpdateDriverPage(App);

	if (args != NULL && argp[OPT_PLAY] != 0) {
		HandleAction(App, MID_PLAY_BASE + (ULONG)*(LONG *)argp[OPT_PLAY], 0);
	}

	timer_mask = 1UL << TimerPort->mp_SigBit;
	ArmTimer();

	for (;;) {
		ret = DoMethod(App->App, MUIM_Application_NewInput, &sigs);

		if (ret == MUIV_Application_ReturnID_Quit) {
			break;
		}

		if (ret != 0) {
			HandleAction(App, ret, Ticks);
		}

		if (sigs != 0) {
			sigs = Wait(sigs | SIGBREAKF_CTRL_C | timer_mask);

			if ((sigs & SIGBREAKF_CTRL_C) != 0) {
				break;
			}

			if ((sigs & timer_mask) != 0) {
				GetMsg(TimerPort);
				TimerPending = FALSE;
				Tick();
				ArmTimer();
			}
		}
	}

	/* A value forced on the command line (MODE, LEVEL, REFRESH) is for this run only: do not keep it */
	if (args == NULL || (argp[OPT_MODE] == 0 && argp[OPT_LEVEL] == 0 && argp[OPT_REFRESH] == 0)) {
		DoMethod(App->App, MUIM_Application_Save, MUIV_Application_Save_ENV);
		DoMethod(App->App, MUIM_Application_Save, MUIV_Application_Save_ENVARC);
	}

	set(App->WI_Main, MUIA_Window_Open, FALSE);
	CleanExit(NULL);

	if (args != NULL) {
		FreeArgs(args);
	}

	return RETURN_OK;
}

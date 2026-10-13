/******************************************************************************
 *
 * GUI.h -- the windows of Emu68HDMI-AHI-Mixer (hand written, laid out like the
 * MUIBuilder files of the other tools: one struct ObjApp holds every object)
 *
 ******************************************************************************/

#ifndef MIXER_GUI_H
#define MIXER_GUI_H

#include <exec/types.h>
#include <libraries/mui.h>

#include "Scope.h"
#include "Test.h"

/* MUI window/application IDs; not every SDK header defines it */
#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) \
	(((ULONG)(a) << 24) | ((ULONG)(b) << 16) | ((ULONG)(c) << 8) | (ULONG)(d))
#endif

#define APP_NAME    "Emu68HDMI-AHI-Mixer"
#define APP_VERSION "0.1"
#define APP_AUTHORS "Philippe CARPENTIER"

/* The pages of the register, in the order of PageTitles[] */
enum {
	PAGE_DRIVER = 0,
	PAGE_SCOPE,
	PAGE_TEST
};

/* Scope refresh: the cycle entries, and how many 40 ms ticks pass between two redraws (0 = paused) */
enum {
	REFRESH_50 = 0,
	REFRESH_25,
	REFRESH_12,
	REFRESH_6,
	REFRESH_3,
	REFRESH_PAUSED,
	REFRESH_COUNT
};

/* Return IDs sent to the main loop by the buttons of the Test page */
#define MID_PLAY_BASE 0x4000UL   /* + SOUND_xxx */
#define MID_STOP      0x4100UL
#define MID_LEVEL     0x4101UL
#define MASTER_MIN_DB (-60)      /* slider range of the master volume, in dB */

#define MID_MASTER    0x4102UL   /* master volume slider */
#define MID_MUTE      0x4103UL

/* Return IDs of the menus (and of the cycle, to keep the menus in step) */
#define MID_ABOUT         0x4200UL
#define MID_MUTE_MENU     0x4201UL
#define MID_VOL_UP        0x4202UL
#define MID_VOL_DOWN      0x4203UL
#define MID_MODE_NORMAL   0x4204UL
#define MID_MODE_COMPACT  0x4205UL
#define MID_REFRESH       0x4206UL   /* the refresh cycle moved */
#define MID_THEME_BASE    0x4210UL   /* + THEME_xxx */
#define MID_REFRESH_BASE  0x4220UL   /* + REFRESH_xxx */
#define MID_MENU_FIRST    0x4200UL   /* every ID from here on is for MenuAction() */

/* The rows of the table of the Driver page, in display order */
enum {
	DRV_DRIVER = 0,
	DRV_INTERRUPTS,
	DRV_STATE,
	DRV_STAGE,
	DRV_FAULT,
	DRV_FORMAT,
	DRV_IRQLINE,
	DRV_SEGMENTS,
	DRV_RATE,
	DRV_WAKES,
	DRV_RESYNCS,
	DRV_PRETIMER,
	DRV_BURST,
	DRV_LAG,
	DRV_ROWS
};

struct ObjApp
{
	APTR App;
	APTR WI_Main;
	APTR GR_Pages;
	APTR GR_Displays;       /* the meter, the oscilloscope and the spectrum */
	APTR GR_ScopePage;
	APTR WI_Hidden;         /* compact mode: a window that is never opened and keeps the pages */

	/* Menus */
	APTR MN_Main;
	APTR MN_About, MN_AboutMUI, MN_Settings, MN_Quit, MN_Louder, MN_Quieter;
	APTR MN_Mute;
	APTR MN_Normal;
	APTR MN_Compact;
	APTR MN_Refresh[REFRESH_COUNT];
	APTR MN_Theme[THEME_COUNT];

	/* Test page */
	APTR CY_Mode;
	APTR SL_Level;
	APTR CY_Duration;
	APTR BT_Play[SOUND_COUNT];
	APTR BT_Stop;
	APTR TX_TestState;
	APTR TX_TestResult;

	/* Scope page */
	APTR TX_ScopeInfo;
	APTR CY_Refresh;
	APTR SL_Master;
	APTR TG_Mute;
	APTR SC_Meter;
	APTR SC_Wave;
	APTR SC_Spectrum;

	/* Driver page */
	APTR TX_DrvHead;
	APTR TX_DrvValue[DRV_ROWS];
};

struct ObjApp * CreateApp(VOID);
VOID DisposeApp(struct ObjApp * app);

/* A menu return ID (MID_MENU_FIRST and up) */
VOID MenuAction(struct ObjApp * app, ULONG id);

/* Puts the check marks of the menus where the gadgets and the settings are */
VOID SyncMenus(struct ObjApp * app);

#endif /* MIXER_GUI_H */

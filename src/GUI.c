/******************************************************************************
 *
 * GUI.c -- the main window: a register with the Driver, Scope and Test pages
 *
 * Kept compact on purpose: MUI sizes a window to what its objects need, and a
 * Text object needs the width of its string. Every text that changes or runs
 * long is created with MUIA_Text_SetMin FALSE (it may be clipped instead of
 * widening the window), labels are short, and the Test buttons are in two
 * columns, so that the window can be made quite small on the Workbench.
 *
 ******************************************************************************/

#include <exec/types.h>
#include <libraries/gadtools.h>
#include <libraries/mui.h>

#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include "GUI.h"
#include "Prefs.h"
#include "Scope.h"
#include "Update.h"

STATIC CONST_STRPTR PageTitles[] = { "Driver", "Scope", "Test", NULL };

STATIC CONST_STRPTR RefreshEntries[] = { "50 fps", "25 fps", "12 fps", "6 fps", "3 fps", "Paused", NULL };

/* Labels of the Driver table, in the order of the DRV_ enum */
STATIC CONST_STRPTR DriverLabels[DRV_ROWS] = {
	"Driver:",
	"Interrupt:",
	"State:",
	"Stage:",
	"Fault:",
	"Mixer:",
	"IRQ line:",
	"Segments:",
	"Rate:",
	"Wake-ups:",
	"Resyncs:",
	"AHI guard:",
	"Burst:",
	"IRQ delay:"
};

/* A text that never forces the window wider than the user wants it */
#define FlexText(frame_and_contents) TextObject, frame_and_contents, MUIA_Text_SetMin, FALSE

/******************************************************************************
 *
 * BuildTestPage()
 *
 ******************************************************************************/

STATIC CONST_STRPTR ModeEntries[TEST_MODES + 1];
STATIC CONST_STRPTR DurationEntries[] = { "5 s", "10 s", "30 s", "no limit", NULL };

STATIC APTR BuildTestPage(struct ObjApp * object)
{
	APTR buttons;
	LONG i;

	for (i = 0; i < TEST_MODES; i++) {
		ModeEntries[i] = TestModeShort((ULONG)i);
	}

	ModeEntries[TEST_MODES] = NULL;

	object->CY_Mode = CycleObject,
		MUIA_ObjectID,      MAKE_ID('M','O','D','1'),
		MUIA_Cycle_Entries, ModeEntries,
	End;

	/* No MUIA_ObjectID on the level: it is not kept between sessions, so a test sound never starts loud */
	object->SL_Level = SliderObject,
		MUIA_Numeric_Min,    -60,
		MUIA_Numeric_Max,    -1,
		MUIA_Numeric_Value,  -30,
		MUIA_Numeric_Format, "%ld dB",
	End;

	object->CY_Duration = CycleObject,
		MUIA_ObjectID,      MAKE_ID('D','U','R','1'),
		MUIA_Cycle_Entries, DurationEntries,
		MUIA_Cycle_Active,  1,
	End;

	/* Two columns, four rows: tall rather than wide */
	buttons = ColGroup(2),
		GroupFrameT("Play"),
	End;

	for (i = 0; i < SOUND_COUNT; i++) {
		object->BT_Play[i] = SimpleButton(TestSoundShort((ULONG)i));
		DoMethod(buttons, OM_ADDMEMBER, object->BT_Play[i]);
	}

	object->BT_Stop = SimpleButton("\33bStop");
	DoMethod(buttons, OM_ADDMEMBER, object->BT_Stop);

	object->TX_TestState = FlexText(TextFrame),
		MUIA_Background,    MUII_TextBack,
		MUIA_Text_PreParse, "\33c",
		MUIA_Text_Contents, "Ready.",
	End;

	object->TX_TestResult = FlexText(TextFrame),
		MUIA_Background,    MUII_TextBack,
		MUIA_Text_Contents, "No test played yet.",
	End;

	return VGroup,
		Child, FlexText(TextFrame),
			MUIA_Background,    MUII_TextBack,
			MUIA_Text_Contents, "Listen to the HDMI driver:\npick a mode and a level, play a sound.",
		End,
		Child, ColGroup(2),
			GroupFrameT("Output"),
			Child, MUI_MakeObject(MUIO_Label, "Mode:", MUIO_Label_DoubleFrame),
			Child, object->CY_Mode,
			Child, MUI_MakeObject(MUIO_Label, "Level:", MUIO_Label_DoubleFrame),
			Child, object->SL_Level,
			Child, MUI_MakeObject(MUIO_Label, "Stop after:", MUIO_Label_DoubleFrame),
			Child, object->CY_Duration,
		End,
		Child, buttons,
		Child, object->TX_TestState,
		Child, object->TX_TestResult,
		Child, VSpace(0),
	End;
}

/******************************************************************************
 *
 * BuildDriverPage()
 *
 ******************************************************************************/

STATIC APTR BuildDriverPage(struct ObjApp * object)
{
	APTR table;
	APTR page;
	LONG i;

	object->TX_DrvHead = FlexText(TextFrame),
		MUIA_Background,    MUII_TextBack,
		MUIA_Text_PreParse, "\33c\33b",
		MUIA_Text_Contents, "Looking for the driver...",
	End;

	table = ColGroup(2),
		GroupFrameT("Driver"),
	End;

	for (i = 0; i < DRV_ROWS; i++) {
		object->TX_DrvValue[i] = FlexText(TextFrame),
			MUIA_Background,    MUII_TextBack,
			MUIA_Text_Contents, "-",
		End;

		DoMethod(table, OM_ADDMEMBER, MUI_MakeObject(MUIO_Label, DriverLabels[i], MUIO_Label_DoubleFrame));
		DoMethod(table, OM_ADDMEMBER, object->TX_DrvValue[i]);
	}

	page = VGroup,
		Child, object->TX_DrvHead,
		Child, table,
		Child, VSpace(0),
	End;

	return page;
}

/******************************************************************************
 *
 * BuildScopePage() -- a lamp, the ring in a few words and the refresh speed on
 * top, then the meters, the oscilloscope and the spectrum sharing the height
 *
 ******************************************************************************/

STATIC APTR BuildScopePage(struct ObjApp * object)
{
	object->CY_Refresh = CycleObject,
		MUIA_ObjectID,      MAKE_ID('R','F','R','1'),
		MUIA_Weight,        0,
		MUIA_ShortHelp,     "Scope refresh rate",
		MUIA_Cycle_Entries, RefreshEntries,
		MUIA_Cycle_Active,  REFRESH_12,
	End;

	object->TX_ScopeInfo = FlexText(TextFrame),
		MUIA_Background,    MUII_TextBack,
		MUIA_Text_Contents, "No HDMI audio",
	End;

	/* The master volume of the driver (not the AHI volume of the Test page): kept between sessions,
	** and given back to the driver whenever it is loaded */
	object->SL_Master = SliderObject,
		MUIA_ObjectID,       MAKE_ID('M','S','T','1'),
		MUIA_ShortHelp,      "Master volume of the HDMI driver",
		MUIA_Numeric_Min,    MASTER_MIN_DB,
		MUIA_Numeric_Max,    0,
		MUIA_Numeric_Value,  0,
		MUIA_Numeric_Format, "%ld dB",
	End;

	object->TG_Mute = TextObject,
		ButtonFrame,
		MUIA_Background,    MUII_ButtonBack,
		MUIA_Weight,        0,
		MUIA_InputMode,     MUIV_InputMode_Toggle,
		MUIA_Text_PreParse, "c",
		MUIA_Text_Contents, "Mute",
		MUIA_ShortHelp,     "Silence the HDMI driver",
	End;

	object->SC_Meter = ScopeObject,
		MUIA_Scope_Mode,  SCOPE_MODE_METER,
		MUIA_Scope_Theme, Prefs.theme,
		MUIA_Scope_Frame, &ScopeFrameData,
		MUIA_Frame,       MUIV_Frame_Text,
		MUIA_VertWeight,  14,
	End;

	object->SC_Wave = ScopeObject,
		MUIA_Scope_Mode,  SCOPE_MODE_WAVE,
		MUIA_Scope_Theme, Prefs.theme,
		MUIA_Scope_Frame, &ScopeFrameData,
		MUIA_Frame,       MUIV_Frame_Text,
		MUIA_VertWeight,  50,
	End;

	object->SC_Spectrum = ScopeObject,
		MUIA_Scope_Mode,  SCOPE_MODE_SPECTRUM,
		MUIA_Scope_Theme, Prefs.theme,
		MUIA_Scope_Frame, &ScopeFrameData,
		MUIA_Frame,       MUIV_Frame_Text,
		MUIA_VertWeight,  36,
	End;

	object->GR_Displays = VGroup,
		Child, object->SC_Meter,
		Child, object->SC_Wave,
		Child, object->SC_Spectrum,
	End;

	object->GR_ScopePage = VGroup,
		Child, ColGroup(2),
			GroupFrame,
			Child, object->TX_ScopeInfo,
			Child, object->CY_Refresh,
			Child, object->SL_Master,
			Child, object->TG_Mute,
		End,
		Child, object->GR_Displays,
	End;

	return object->GR_ScopePage;
}

/******************************************************************************
 *
 * Menus
 *
 ******************************************************************************/

STATIC APTR MenuTitle(CONST_STRPTR title, CONST_STRPTR key)
{
	return MenuitemObject,
		MUIA_Menuitem_Title,    title,
		MUIA_Menuitem_Shortcut, key,
	End;
}

/* A check mark (exclude 0) or one entry of a group of radio entries (exclude: the other entries) */
STATIC APTR MenuCheck(CONST_STRPTR title, CONST_STRPTR key, BOOL checked, ULONG exclude)
{
	return MenuitemObject,
		MUIA_Menuitem_Title,    title,
		MUIA_Menuitem_Shortcut, key,
		MUIA_Menuitem_Checkit,  TRUE,
		MUIA_Menuitem_Checked,  checked,
		MUIA_Menuitem_Exclude,  exclude,
	End;
}

STATIC APTR MenuBar(VOID)
{
	return MUI_MakeObject(MUIO_Menuitem, NM_BARLABEL, 0, 0, 0);
}

#define RadioMask(count, i) ((((1UL << (count)) - 1UL)) & ~(1UL << (i)))

STATIC VOID BuildMenus(struct ObjApp * object)
{
	APTR project, audio, scope, refresh, colours, window;
	LONG i;

	object->MN_About = MenuTitle("About...", "?");
	object->MN_AboutMUI = MenuTitle("About MUI...", NULL);
	object->MN_Settings = MenuTitle("MUI Settings...", NULL);
	object->MN_Quit = MenuTitle("Quit", "Q");

	project = MenuitemObject,
		MUIA_Menuitem_Title, "Project",
		MUIA_Family_Child, object->MN_About,
		MUIA_Family_Child, MenuBar(),
		MUIA_Family_Child, object->MN_AboutMUI,
		MUIA_Family_Child, object->MN_Settings,
		MUIA_Family_Child, MenuBar(),
		MUIA_Family_Child, object->MN_Quit,
	End;

	object->MN_Mute = MenuCheck("Mute", "M", FALSE, 0);
	object->MN_Louder = MenuTitle("Louder", "+");
	object->MN_Quieter = MenuTitle("Quieter", "-");

	audio = MenuitemObject,
		MUIA_Menuitem_Title, "Audio",
		MUIA_Family_Child, object->MN_Mute,
		MUIA_Family_Child, object->MN_Louder,
		MUIA_Family_Child, object->MN_Quieter,
	End;

	refresh = MenuitemObject,
		MUIA_Menuitem_Title, "Refresh",
	End;

	for (i = 0; i < REFRESH_COUNT; i++) {
		static const char * keys[REFRESH_COUNT] = { "1", "2", "3", "4", "5", "6" };

		object->MN_Refresh[i] = MenuCheck(RefreshEntries[i], keys[i], i == REFRESH_12, RadioMask(REFRESH_COUNT, i));
		DoMethod(refresh, OM_ADDMEMBER, object->MN_Refresh[i]);
	}

	colours = MenuitemObject,
		MUIA_Menuitem_Title, "Colours",
	End;

	for (i = 0; i < THEME_COUNT; i++) {
		object->MN_Theme[i] = MenuCheck(ScopeThemeName((ULONG)i), NULL, (ULONG)i == Prefs.theme, RadioMask(THEME_COUNT, i));
		DoMethod(colours, OM_ADDMEMBER, object->MN_Theme[i]);
	}

	object->MN_Normal  = MenuCheck("Normal", NULL, !Prefs.compact, RadioMask(2, 0));
	object->MN_Compact = MenuCheck("Compact (restart)", NULL, Prefs.compact, RadioMask(2, 1));

	window = MenuitemObject,
		MUIA_Menuitem_Title, "Window",
		MUIA_Family_Child, object->MN_Normal,
		MUIA_Family_Child, object->MN_Compact,
	End;

	scope = MenuitemObject,
		MUIA_Menuitem_Title, "Scope",
		MUIA_Family_Child, refresh,
		MUIA_Family_Child, colours,
		MUIA_Family_Child, MenuBar(),
		MUIA_Family_Child, window,
	End;

	object->MN_Main = MenustripObject,
		MUIA_Family_Child, project,
		MUIA_Family_Child, audio,
		MUIA_Family_Child, scope,
	End;
}

/* The notifications need the application object: called once it exists */
STATIC VOID NotifyMenus(struct ObjApp * object)
{
	LONG i;

	DoMethod(object->MN_About, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
		object->App, 2, MUIM_Application_ReturnID, MID_ABOUT);
	DoMethod(object->MN_Louder, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
		object->App, 2, MUIM_Application_ReturnID, MID_VOL_UP);
	DoMethod(object->MN_Quieter, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
		object->App, 2, MUIM_Application_ReturnID, MID_VOL_DOWN);
	DoMethod(object->MN_AboutMUI, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
		object->App, 2, MUIM_Application_AboutMUI, 0);
	DoMethod(object->MN_Settings, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
		object->App, 2, MUIM_Application_OpenConfigWindow, 0);
	DoMethod(object->MN_Quit, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
		object->App, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

	DoMethod(object->MN_Mute, MUIM_Notify, MUIA_Menuitem_Checked, MUIV_EveryTime,
		object->App, 2, MUIM_Application_ReturnID, MID_MUTE_MENU);
	DoMethod(object->MN_Normal, MUIM_Notify, MUIA_Menuitem_Checked, TRUE,
		object->App, 2, MUIM_Application_ReturnID, MID_MODE_NORMAL);
	DoMethod(object->MN_Compact, MUIM_Notify, MUIA_Menuitem_Checked, TRUE,
		object->App, 2, MUIM_Application_ReturnID, MID_MODE_COMPACT);

	for (i = 0; i < REFRESH_COUNT; i++) {
		DoMethod(object->MN_Refresh[i], MUIM_Notify, MUIA_Menuitem_Checked, TRUE,
			object->App, 2, MUIM_Application_ReturnID, MID_REFRESH_BASE + (ULONG)i);
	}

	for (i = 0; i < THEME_COUNT; i++) {
		DoMethod(object->MN_Theme[i], MUIM_Notify, MUIA_Menuitem_Checked, TRUE,
			object->App, 2, MUIM_Application_ReturnID, MID_THEME_BASE + (ULONG)i);
	}
}

/******************************************************************************
 *
 * CreateApp()
 *
 ******************************************************************************/

struct ObjApp * CreateApp(VOID)
{
	struct ObjApp * object;
	LONG i;

	if ((object = AllocVec(sizeof(struct ObjApp), MEMF_PUBLIC | MEMF_CLEAR)) == NULL) {
		return NULL;
	}

	object->GR_Pages = RegisterObject,
		MUIA_Register_Titles, PageTitles,
		Child, BuildDriverPage(object),
		Child, BuildScopePage(object),
		Child, BuildTestPage(object),
	End;

	BuildMenus(object);

	/* Compact mode: the window holds the displays alone. The pages still exist (the menus and the
	** saved settings use their gadgets) in a window that is never opened. */
	if (Prefs.compact) {
		DoMethod(object->GR_ScopePage, OM_REMMEMBER, object->GR_Displays);

		object->WI_Hidden = WindowObject,
			MUIA_Window_Title, APP_NAME,
			MUIA_Window_ID,    MAKE_ID('M','I','X','2'),
			WindowContents, VGroup,
				Child, object->GR_Pages,
			End,
		End;
	}

	/* No fixed size: MUI opens the window at the size its contents ask for, and remembers
	** the size the user chose (MUIA_Window_ID) */
	object->WI_Main = WindowObject,
		MUIA_Window_Title,    APP_NAME,
		MUIA_Window_ID,       Prefs.compact ? MAKE_ID('M','I','X','3') : MAKE_ID('M','I','X','1'),
		MUIA_Window_Menustrip, object->MN_Main,
		WindowContents, VGroup,
			Child, Prefs.compact ? object->GR_Displays : object->GR_Pages,
		End,
	End;

	object->App = ApplicationObject,
		MUIA_Application_Author,      APP_AUTHORS,
		MUIA_Application_Base,        "HDMIMIXER",
		MUIA_Application_Title,       APP_NAME,
		MUIA_Application_Version,     "$VER: " APP_NAME " " APP_VERSION " (13.10.2026)",
		MUIA_Application_Copyright,   "GPL-3.0-or-later",
		MUIA_Application_Description, "Scope, tests and status of the Emu68 HDMI AHI driver",
		SubWindow, object->WI_Main,
	End;

	if (object->App == NULL) {
		FreeVec(object);
		return NULL;
	}

	if (object->WI_Hidden != NULL) {
		DoMethod(object->App, OM_ADDMEMBER, object->WI_Hidden);
	}

	NotifyMenus(object);

	DoMethod(object->WI_Main, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
		object->App, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

	for (i = 0; i < SOUND_COUNT; i++) {
		DoMethod(object->BT_Play[i], MUIM_Notify, MUIA_Pressed, FALSE,
			object->App, 2, MUIM_Application_ReturnID, MID_PLAY_BASE + (ULONG)i);
	}

	DoMethod(object->BT_Stop, MUIM_Notify, MUIA_Pressed, FALSE,
		object->App, 2, MUIM_Application_ReturnID, MID_STOP);

	DoMethod(object->SL_Master, MUIM_Notify, MUIA_Numeric_Value, MUIV_EveryTime,
		object->App, 2, MUIM_Application_ReturnID, MID_MASTER);

	DoMethod(object->TG_Mute, MUIM_Notify, MUIA_Selected, MUIV_EveryTime,
		object->App, 2, MUIM_Application_ReturnID, MID_MUTE);

	DoMethod(object->CY_Refresh, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
		object->App, 2, MUIM_Application_ReturnID, MID_REFRESH);

	DoMethod(object->SL_Level, MUIM_Notify, MUIA_Numeric_Value, MUIV_EveryTime,
		object->App, 2, MUIM_Application_ReturnID, MID_LEVEL);

	return object;
}

/******************************************************************************
 *
 * SyncMenus() / MenuAction()
 *
 * Every change goes through a test first ("is it already so?"): setting a check
 * mark or a gadget to its present value must not start the notifications again.
 *
 ******************************************************************************/

STATIC VOID Check(APTR item, BOOL wanted)
{
	ULONG now = 0;

	get(item, MUIA_Menuitem_Checked, &now);

	if ((now != 0) != (wanted != FALSE)) {
		set(item, MUIA_Menuitem_Checked, wanted);
	}
}

VOID SyncMenus(struct ObjApp * app)
{
	ULONG value = 0;

	get(app->TG_Mute, MUIA_Selected, &value);
	Check(app->MN_Mute, value != 0);

	get(app->CY_Refresh, MUIA_Cycle_Active, &value);

	if (value < REFRESH_COUNT) {
		Check(app->MN_Refresh[value], TRUE);
	}

	Check(app->MN_Theme[Prefs.theme], TRUE);
}

VOID MenuAction(struct ObjApp * app, ULONG id)
{
	STATIC BOOL StartedCompact = FALSE;
	STATIC BOOL Started = FALSE;
	ULONG value = 0;
	LONG db = 0;

	if (!Started) {
		Started = TRUE;
		StartedCompact = Prefs.compact;
	}

	if (id == MID_ABOUT) {
		MUI_Request(app->App, app->WI_Main, 0, (STRPTR)"About " APP_NAME, (STRPTR)"*_Ok",
			(STRPTR)"\n\033c\033b" APP_NAME " " APP_VERSION "\033n\n\n"
			"Scope, tests and master volume\n"
			"of the Emu68 HDMI AHI driver\n\n"
			"\0332" APP_AUTHORS ", GPL-3.0-or-later\n\n"
			"This is a MUI application\n");
		return;
	}

	if (id == MID_MUTE_MENU) {
		get(app->MN_Mute, MUIA_Menuitem_Checked, &value);

		if (value != 0) {
			value = TRUE;
		}

		get(app->TG_Mute, MUIA_Selected, &db);

		if ((db != 0) != (value != 0)) {
			set(app->TG_Mute, MUIA_Selected, value);
		}

		UpdateMaster(app);
		return;
	}

	if (id == MID_VOL_UP || id == MID_VOL_DOWN) {
		get(app->SL_Master, MUIA_Numeric_Value, &db);
		db += (id == MID_VOL_UP) ? 3 : -3;
		db = (db > 0) ? 0 : ((db < MASTER_MIN_DB) ? MASTER_MIN_DB : db);
		set(app->SL_Master, MUIA_Numeric_Value, db);
		UpdateMaster(app);
		return;
	}

	if (id == MID_REFRESH) {
		SyncMenus(app);
		return;
	}

	if (id >= MID_REFRESH_BASE && id < MID_REFRESH_BASE + REFRESH_COUNT) {
		get(app->CY_Refresh, MUIA_Cycle_Active, &value);

		if (value != id - MID_REFRESH_BASE) {
			set(app->CY_Refresh, MUIA_Cycle_Active, id - MID_REFRESH_BASE);
		}

		return;
	}

	if (id >= MID_THEME_BASE && id < MID_THEME_BASE + THEME_COUNT) {
		Prefs.theme = id - MID_THEME_BASE;
		set(app->SC_Meter, MUIA_Scope_Theme, Prefs.theme);
		set(app->SC_Wave, MUIA_Scope_Theme, Prefs.theme);
		set(app->SC_Spectrum, MUIA_Scope_Theme, Prefs.theme);
		PrefsSave();
		return;
	}

	if (id == MID_MODE_NORMAL || id == MID_MODE_COMPACT) {
		Prefs.compact = (id == MID_MODE_COMPACT);
		PrefsSave();

		if (Prefs.compact != StartedCompact) {
			MUI_Request(app->App, app->WI_Main, 0, (STRPTR)APP_NAME, (STRPTR)"*_Ok",
				(STRPTR)"\033cRestart the program\nto change the window mode.");
		}
	}
}

/******************************************************************************
 *
 * DisposeApp()
 *
 ******************************************************************************/

VOID DisposeApp(struct ObjApp * app)
{
	if (app == NULL) {
		return;
	}

	if (app->App != NULL) {
		MUI_DisposeObject(app->App);
	}

	FreeVec(app);
}

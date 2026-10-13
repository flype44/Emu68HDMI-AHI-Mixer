/******************************************************************************
 *
 * Scope.h -- the MUI class that draws the meters, the oscilloscope, the
 * spectrum and the status lamp (a subclass of Area, in the idiom of the Chart
 * class of PMMUI)
 *
 ******************************************************************************/

#ifndef MIXER_SCOPE_H
#define MIXER_SCOPE_H

#include <exec/types.h>
#include <libraries/mui.h>

#include "DSP.h"

#define MUIA_Scope_Mode    (TAG_USER | 0x4d580001UL)  /* [ISG] SCOPE_MODE_xxx */
#define MUIA_Scope_Frame   (TAG_USER | 0x4d580002UL)  /* [ISG] struct ScopeFrame *, read, never copied */
#define MUIA_Scope_State   (TAG_USER | 0x4d580003UL)  /* [ISG] LED_xxx, for SCOPE_MODE_LED */

#define MUIA_Scope_Theme   (TAG_USER | 0x4d580004UL)  /* [ISG] THEME_xxx */

#define MUIM_Scope_Refresh (TAG_USER | 0x4d580011UL)  /* redraw from the current frame */

enum {
	SCOPE_MODE_WAVE = 0,
	SCOPE_MODE_SPECTRUM,
	SCOPE_MODE_METER,
	SCOPE_MODE_LED
};

/* The colour themes of the displays */
enum {
	THEME_PHOSPHOR = 0,   /* green on black, the default */
	THEME_AMBER,          /* amber CRT */
	THEME_ICE,            /* blue */
	THEME_RACK,           /* hi-fi rack: grey, green / yellow / red meters */
	THEME_MONO,           /* white on black */
	THEME_SYNTH,          /* magenta and cyan */
	THEME_COUNT
};

CONST_STRPTR ScopeThemeName(ULONG theme);

enum {
	LED_OFF = 0,
	LED_GREEN,
	LED_AMBER,
	LED_RED
};

extern struct MUI_CustomClass * ScopeClass;

BOOL ScopeInitClass(VOID);
VOID ScopeFreeClass(VOID);

/* ScopeObject, MUIA_Scope_Mode, SCOPE_MODE_WAVE, ..., End; */
#define ScopeObject NewObject(ScopeClass->mcc_Class, NULL

#endif /* MIXER_SCOPE_H */

/******************************************************************************
 *
 * Prefs.h -- the few settings that are not MUI object states: the colour
 * theme of the scope and the window mode (read before the window is built)
 *
 ******************************************************************************/

#ifndef MIXER_PREFS_H
#define MIXER_PREFS_H

#include <exec/types.h>

struct MixerPrefs
{
	ULONG theme;     /* THEME_xxx of Scope.h */
	BOOL  compact;   /* only the scope displays in the window (needs a restart to change) */
};

extern struct MixerPrefs Prefs;

/* Reads ENV:Emu68HDMI-AHI-Mixer.prefs, keeps the defaults if it is missing or odd */
VOID PrefsLoad(VOID);

/* Writes it to ENV: and ENVARC: */
VOID PrefsSave(VOID);

#endif /* MIXER_PREFS_H */

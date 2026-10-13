/******************************************************************************
 *
 * Prefs.c -- settings kept in a small text file, "THEME=n" and "COMPACT=n"
 *
 ******************************************************************************/

#include <exec/types.h>
#include <dos/dos.h>

#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Prefs.h"
#include "Scope.h"

#define PREFS_ENV    "ENV:Emu68HDMI-AHI-Mixer.prefs"
#define PREFS_ENVARC "ENVARC:Emu68HDMI-AHI-Mixer.prefs"

struct MixerPrefs Prefs = { THEME_PHOSPHOR, FALSE };

VOID PrefsLoad(VOID)
{
	TEXT buf[128];
	BPTR file;
	LONG len;
	STRPTR p;

	file = Open((STRPTR)PREFS_ENV, MODE_OLDFILE);

	if (file == 0) {
		file = Open((STRPTR)PREFS_ENVARC, MODE_OLDFILE);
	}

	if (file == 0) {
		return;
	}

	len = Read(file, buf, sizeof(buf) - 1);
	Close(file);

	if (len <= 0) {
		return;
	}

	buf[len] = 0;

	if ((p = strstr(buf, "THEME=")) != NULL) {
		LONG theme = atol(p + 6);

		if (theme >= 0 && theme < THEME_COUNT) {
			Prefs.theme = (ULONG)theme;
		}
	}

	if ((p = strstr(buf, "COMPACT=")) != NULL) {
		Prefs.compact = (atol(p + 8) != 0);
	}
}

STATIC VOID WriteTo(CONST_STRPTR name, CONST_STRPTR text)
{
	BPTR file = Open((STRPTR)name, MODE_NEWFILE);

	if (file != 0) {
		Write(file, (APTR)text, strlen(text));
		Close(file);
	}
}

VOID PrefsSave(VOID)
{
	TEXT buf[64];

	sprintf(buf, "THEME=%ld\nCOMPACT=%ld\n", (LONG)Prefs.theme, (LONG)(Prefs.compact ? 1 : 0));

	WriteTo(PREFS_ENV, buf);
	WriteTo(PREFS_ENVARC, buf);
}

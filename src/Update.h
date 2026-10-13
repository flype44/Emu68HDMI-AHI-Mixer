/******************************************************************************
 *
 * Update.h -- refreshing the pages from what the driver and the hardware say
 *
 ******************************************************************************/

#ifndef MIXER_UPDATE_H
#define MIXER_UPDATE_H

#include <exec/types.h>

#include "DSP.h"
#include "GUI.h"

/* The data the scope objects draw (they hold a pointer to it) */
extern struct ScopeFrame ScopeFrameData;

/* Opens what the Scope page reads (hardware, analysis tables) */
BOOL UpdateInit(VOID);

/* Reads the driver and rewrites the Driver page. Cheap: meant for ~4 Hz. */
VOID UpdateDriverPage(struct ObjApp * app);

/* A button or the level slider was used (a MID_xxx return ID) */
VOID HandleAction(struct ObjApp * app, ULONG id, ULONG tick);

/* Gives the driver the master volume the slider shows (and enables the gadgets only if the driver
** can). Called when the slider or Mute move, and every half second: a driver that was unloaded and
** loaded again starts at unity. */
VOID UpdateMaster(struct ObjApp * app);

/* Called every 240 ms: end of the duration, state text */
VOID UpdateTestPage(struct ObjApp * app, ULONG tick);

/* Reads the audio that has just been played and refreshes the Scope page. ~25 Hz. */
VOID UpdateScopePage(struct ObjApp * app);

#endif /* MIXER_UPDATE_H */

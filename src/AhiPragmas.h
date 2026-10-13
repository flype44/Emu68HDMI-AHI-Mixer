/******************************************************************************
 *
 * AhiPragmas.h -- the ahi.device calls this program uses, for SAS/C.
 *
 * The SDK on this machine only has the GCC inline version of the AHI stubs
 * (pragmas/ahi_pragmas.h there is the inline header), so the eight calls are
 * declared here with SAS/C #pragma libcall. Offsets and registers are those
 * of the AHI SDK (ahi.sfd, the same numbers as inline/ahi.h):
 *
 *   AHI_AllocAudioA    -42   a1
 *   AHI_FreeAudio      -48   a2
 *   AHI_ControlAudioA  -60   a2 a1
 *   AHI_SetVol         -66   d0 d1 d2 a2 d3
 *   AHI_SetFreq        -72   d0 d1 a2 d2
 *   AHI_SetSound       -78   d0 d1 d2 d3 a2 d4
 *   AHI_LoadSound      -90   d0 d1 a0 a2
 *   AHI_UnloadSound    -96   d0 a2
 *
 * The register string of a pragma lists the registers from the last argument
 * to the first, then the number of arguments.
 *
 ******************************************************************************/

#ifndef MIXER_AHIPRAGMAS_H
#define MIXER_AHIPRAGMAS_H

#include <exec/types.h>
#include <devices/ahi.h>

struct AHIAudioCtrl * AHI_AllocAudioA(struct TagItem * tags);
VOID  AHI_FreeAudio(struct AHIAudioCtrl * ctrl);
ULONG AHI_ControlAudioA(struct AHIAudioCtrl * ctrl, struct TagItem * tags);
VOID  AHI_SetVol(UWORD channel, Fixed volume, sposition pan, struct AHIAudioCtrl * ctrl, ULONG flags);
VOID  AHI_SetFreq(UWORD channel, ULONG freq, struct AHIAudioCtrl * ctrl, ULONG flags);
VOID  AHI_SetSound(UWORD channel, UWORD sound, ULONG offset, LONG length, struct AHIAudioCtrl * ctrl, ULONG flags);
ULONG AHI_LoadSound(UWORD sound, ULONG type, APTR info, struct AHIAudioCtrl * ctrl);
VOID  AHI_UnloadSound(UWORD sound, struct AHIAudioCtrl * ctrl);

#pragma libcall AHIBase AHI_AllocAudioA   2a 901
#pragma libcall AHIBase AHI_FreeAudio     30 A01
#pragma libcall AHIBase AHI_ControlAudioA 3c 9A02
#pragma libcall AHIBase AHI_SetVol        42 3A21005
#pragma libcall AHIBase AHI_SetFreq       48 2A1004
#pragma libcall AHIBase AHI_SetSound      4e 4A321006
#pragma libcall AHIBase AHI_LoadSound     5a A81004
#pragma libcall AHIBase AHI_UnloadSound   60 A002

extern struct Library * AHIBase;

#endif /* MIXER_AHIPRAGMAS_H */

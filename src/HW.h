/******************************************************************************
 *
 * HW.h -- read-only access to the Raspberry Pi hardware behind the HDMI audio
 *
 * Nothing here ever writes to a register or to the audio ring. The HDMI audio
 * DMA channel is recognised by what it does (its destination is the HDMI0
 * audio FIFO), not by what the driver says, so it works with any version of
 * the driver. The ring is read behind the DMA read pointer: that is audio
 * that has just been played, so the meters and the scope follow the sound.
 *
 ******************************************************************************/

#ifndef MIXER_HW_H
#define MIXER_HW_H

#include <exec/types.h>

#define HW_MAX_SEGMENTS 16

struct AudioDma
{
	BOOL  valid;           /* an active DMA channel feeds the HDMI0 audio FIFO */
	UBYTE channel;
	ULONG cs;              /* DMA control and status */
	ULONG segments;        /* control blocks in the ring */
	ULONG seg_bytes;       /* bytes per segment */
	ULONG ring_base;       /* address of the first byte of the ring */
	ULONG ring_frames;     /* frames in the ring (8 bytes each: left, right) */
	ULONG pos_frames;      /* DMA read position in the ring, in frames */
};

/* Finds the peripheral base in the device tree. FALSE when this is not an
** Emu68 on a BCM2711 (the other calls then do nothing). */
BOOL  HwInit(VOID);

/* One 32 bit register, offset from the 0x7e000000 peripheral bus base */
ULONG HwRead(ULONG offset);

/* Looks for the active HDMI audio DMA channel and fills *dma */
BOOL  HwFindDma(struct AudioDma * dma);

/* Copies the last `frames` frames played before the DMA read position as
** signed 24 bit samples (-8388608 .. 8388607). Returns the frames copied. */
ULONG HwReadRecent(const struct AudioDma * dma, LONG * left, LONG * right, ULONG frames);

/* Registers of the HDMI0 audio block, decoded for the status text */
struct HdmiAudio
{
	ULONG mai_ctl;
	ULONG ram_cfg;
	ULONG ram_status;
	ULONG rate;
	ULONG format;
	ULONG cts;
};

VOID  HwReadHdmi(struct HdmiAudio * h);

#endif /* MIXER_HW_H */

/******************************************************************************
 *
 * HW.c -- read-only access to the Raspberry Pi hardware behind the HDMI audio
 *
 ******************************************************************************/

#include <exec/types.h>

#include <proto/devicetree.h>
#include <proto/exec.h>

#include <string.h>

#include "HW.h"
#include "LE32.h"

/* devicetree.resource is a Resource: OpenResource() returns its base, and the
** DT_xxx() calls expect a global of exactly this name (see Emu68Config). */
APTR DeviceTreeBase = NULL;

/* Offsets from the 0x7e000000 bus base (see the driver's hdmi_backend.h) */
#define DMA_BASE      0x7000UL
#define DMA_CHANNEL   0x100UL
#define DMA_CHANNELS  15
#define DMA_CS        0x00UL
#define DMA_CONBLK    0x04UL
#define DMA_SOURCE    0x0cUL
#define DMA_DEST      0x10UL

#define HDMI_MAI_CTL     0xf20010UL
#define HDMI_MAI_FORMAT  0xf20018UL
#define HDMI_MAI_RATE    0xf20020UL
#define HDMI_RAM_CFG     0xf007bcUL
#define HDMI_RAM_STATUS  0xf007c4UL
#define HDMI_CTS0        0xf007ccUL

/* The destination of the audio DMA: the HDMI0 audio FIFO, as a bus address */
#define HDMI0_FIFO_BUS   0x7ef2001cUL

#define BUS_ADDRESS_MASK 0x3fffffffUL

STATIC ULONG IoBase = 0;

/* The ring of control blocks, rebuilt only when the DMA runs another one */
STATIC struct {
	BOOL  ok;
	ULONG cb[HW_MAX_SEGMENTS];
	ULONG count;
	ULONG seg_bytes;
	ULONG base;
} Ring;

/******************************************************************************
 *
 * Device tree helpers
 *
 ******************************************************************************/

STATIC CONST ULONG * Prop(APTR key, CONST_STRPTR name, ULONG * length)
{
	APTR property;

	if ((property = DT_FindProperty(key, name)) == NULL) {
		return NULL;
	}

	if (length != NULL) {
		*length = DT_GetPropLen(property);
	}

	return (CONST ULONG *)DT_GetPropValue(property);
}

STATIC ULONG Cell(APTR key, CONST_STRPTR name, ULONG fallback)
{
	ULONG length = 0;
	CONST ULONG * value = Prop(key, name, &length);

	return (value != NULL && length == 4) ? *value : fallback;
}

STATIC BOOL IsBcm2711(APTR root)
{
	ULONG length = 0;
	CONST UBYTE * p = (CONST UBYTE *)Prop(root, "compatible", &length);
	CONST UBYTE * end;

	while (p != NULL && length > 0) {
		if ((end = memchr(p, 0, length)) == NULL) {
			return FALSE;
		}

		if (strcmp((CONST_STRPTR)p, "brcm,bcm2711") == 0) {
			return TRUE;
		}

		length -= (ULONG)(end - p) + 1;
		p = end + 1;
	}

	return FALSE;
}

/******************************************************************************
 *
 * HwInit() -- the CPU address of the 0x7e000000 bus range, from /soc/ranges
 *
 ******************************************************************************/

BOOL HwInit(VOID)
{
	APTR root;
	APTR soc;
	ULONG ac, pc, sc, stride, length = 0, i;
	CONST ULONG * ranges;
	ULONG bus, cpu, size;

	if ((DeviceTreeBase = OpenResource("devicetree.resource")) == NULL) {
		return FALSE;
	}

	root = DT_OpenKey("/");
	soc  = DT_OpenKey("/soc");

	if (root == NULL || soc == NULL || !IsBcm2711(root)) {
		if (root != NULL) {
			DT_CloseKey(root);
		}

		if (soc != NULL) {
			DT_CloseKey(soc);
		}

		return FALSE;
	}

	ac = Cell(soc, "#address-cells", 1);
	pc = Cell(root, "#address-cells", 2);
	sc = Cell(soc, "#size-cells", 1);
	stride = ac + pc + sc;
	ranges = Prop(soc, "ranges", &length);

	if (ranges != NULL && ac == 1 && (pc == 1 || pc == 2) && sc == 1 && length % (stride * 4) == 0) {
		for (i = 0; i < length / 4; i += stride) {
			bus  = ranges[i];
			cpu  = ranges[i + ac + pc - 1];
			size = ranges[i + stride - 1];

			if (pc == 2 && ranges[i + ac] != 0) {
				continue;
			}

			if (bus == 0x7e000000UL && size >= 0x00f20024UL && cpu >= 0xf0000000UL && cpu <= 0xff0dffdbUL) {
				IoBase = cpu;
				break;
			}
		}
	}

	DT_CloseKey(soc);
	DT_CloseKey(root);

	return (BOOL)(IoBase != 0);
}

/******************************************************************************
 *
 * HwRead() -- registers are little endian, the 68k is big endian
 *
 ******************************************************************************/

ULONG HwRead(ULONG offset)
{
	if (IoBase == 0) {
		return 0;
	}

	return asm_le32(*(volatile ULONG *)(IoBase + offset));
}

/******************************************************************************
 *
 * Little endian word in memory (control blocks are written little endian)
 *
 ******************************************************************************/

STATIC ULONG MemLE32(ULONG address)
{
	return asm_le32(*(volatile ULONG *)address);
}

/******************************************************************************
 *
 * MapRing() -- follows the control blocks from `start` until they loop
 *
 * The ring is usable when every segment has the same length and the segments
 * follow each other in memory, as the driver builds them. Anything else is
 * left alone (valid = FALSE): better no scope than a wrong one.
 *
 ******************************************************************************/

STATIC BOOL MapRing(ULONG start)
{
	ULONG current = start;
	ULONG count = 0;
	ULONG src[HW_MAX_SEGMENTS];
	ULONG len = 0;
	ULONG base = 0xffffffffUL;
	ULONG i, j;

	/* Already known? */
	if (Ring.ok) {
		for (i = 0; i < Ring.count; i++) {
			if (Ring.cb[i] == start) {
				return TRUE;
			}
		}
	}

	Ring.ok = FALSE;

	do {
		ULONG phys = current & BUS_ADDRESS_MASK;
		ULONG next;
		ULONG seg_len;

		/* Control blocks are 32 byte aligned and live in RAM below 1 GB */
		if ((phys & 31) != 0 || phys < 0x08000000UL || phys >= 0x40000000UL || count >= HW_MAX_SEGMENTS) {
			return FALSE;
		}

		Ring.cb[count]  = current;
		src[count]      = MemLE32(phys + 4);
		seg_len         = MemLE32(phys + 12);
		next            = MemLE32(phys + 20);

		if (count == 0) {
			len = seg_len;
		} else if (seg_len != len) {
			return FALSE;
		}

		if (src[count] < base) {
			base = src[count];
		}

		count++;
		current = next;
	} while (current != start);

	if (len == 0 || (len & 7) != 0 || count < 2) {
		return FALSE;
	}

	/* The segments must be base, base + len, ... in some order */
	for (i = 0; i < count; i++) {
		if (((src[i] - base) % len) != 0 || ((src[i] - base) / len) >= count) {
			return FALSE;
		}

		for (j = i + 1; j < count; j++) {
			if (src[i] == src[j]) {
				return FALSE;
			}
		}
	}

	Ring.count     = count;
	Ring.seg_bytes = len;
	Ring.base      = base;
	Ring.ok        = TRUE;

	return TRUE;
}

/******************************************************************************
 *
 * HwFindDma()
 *
 ******************************************************************************/

BOOL HwFindDma(struct AudioDma * dma)
{
	ULONG channel;
	ULONG base;
	ULONG cs, cb, cb_again, src, pos;

	memset(dma, 0, sizeof(struct AudioDma));

	if (IoBase == 0) {
		return FALSE;
	}

	for (channel = 0; channel < DMA_CHANNELS; channel++) {
		base = DMA_BASE + channel * DMA_CHANNEL;
		cs   = HwRead(base + DMA_CS);

		if ((cs & 1) == 0 || HwRead(base + DMA_DEST) != HDMI0_FIFO_BUS) {
			continue;
		}

		/* A control block and a source address that belong together */
		cb  = HwRead(base + DMA_CONBLK);
		src = HwRead(base + DMA_SOURCE);
		cb_again = HwRead(base + DMA_CONBLK);

		if (cb != cb_again) {
			cb  = cb_again;
			src = HwRead(base + DMA_SOURCE);
		}

		if (!MapRing(cb)) {
			return FALSE;
		}

		pos = src - Ring.base;

		if (pos >= Ring.count * Ring.seg_bytes) {
			return FALSE;
		}

		dma->valid       = TRUE;
		dma->channel     = (UBYTE)channel;
		dma->cs          = cs;
		dma->segments    = Ring.count;
		dma->seg_bytes   = Ring.seg_bytes;
		dma->ring_base   = Ring.base & BUS_ADDRESS_MASK;
		dma->ring_frames = (Ring.count * Ring.seg_bytes) / 8;
		dma->pos_frames  = pos / 8;

		return TRUE;
	}

	return FALSE;
}

/******************************************************************************
 *
 * HwReadRecent()
 *
 * One frame is two IEC958 sub-frames (left, right), each a little endian
 * word with the 24 bit sample in bits 4..27.
 *
 ******************************************************************************/

ULONG HwReadRecent(const struct AudioDma * dma, LONG * left, LONG * right, ULONG frames)
{
	ULONG start;
	ULONG i;
	ULONG index;
	ULONG address;

	if (!dma->valid || frames == 0 || frames > dma->ring_frames) {
		return 0;
	}

	start = (dma->pos_frames + dma->ring_frames - frames) % dma->ring_frames;

	for (i = 0; i < frames; i++) {
		index   = (start + i) % dma->ring_frames;
		address = dma->ring_base + index * 8;

		left[i]  = ((LONG)(MemLE32(address) << 4)) >> 8;
		right[i] = ((LONG)(MemLE32(address + 4) << 4)) >> 8;
	}

	return frames;
}

/******************************************************************************
 *
 * HwReadHdmi()
 *
 ******************************************************************************/

VOID HwReadHdmi(struct HdmiAudio * h)
{
	h->mai_ctl    = HwRead(HDMI_MAI_CTL);
	h->ram_cfg    = HwRead(HDMI_RAM_CFG);
	h->ram_status = HwRead(HDMI_RAM_STATUS);
	h->rate       = HwRead(HDMI_MAI_RATE);
	h->format     = HwRead(HDMI_MAI_FORMAT);
	h->cts        = HwRead(HDMI_CTS0);
}

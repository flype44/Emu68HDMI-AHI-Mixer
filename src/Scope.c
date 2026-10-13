/******************************************************************************
 *
 * Scope.c -- meters, oscilloscope, spectrum and lamp, drawn like an
 * instrument: phosphor green on near black
 *
 * Every refresh the whole object is drawn into an off-screen bitmap and
 * copied to the window in one blit, so nothing flickers. If the bitmap can
 * not be allocated the drawing goes straight to the window.
 *
 ******************************************************************************/

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <intuition/classusr.h>
#include <libraries/mui.h>
#include <utility/tagitem.h>

#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>

#include <string.h>

#include "DSP.h"
#include "Scope.h"

struct MUI_CustomClass * ScopeClass = NULL;

/* The colours, by pen slot */
enum {
	PEN_BG = 0,
	PEN_GRID,
	PEN_GRID2,
	PEN_TRACE,
	PEN_TRACE_DIM,
	PEN_GREEN,
	PEN_YELLOW,
	PEN_RED,
	PEN_TEXT,
	PEN_AMBER,
	PEN_OFF,
	PEN_COUNT
};

struct PenColor {
	UBYTE r;
	UBYTE g;
	UBYTE b;
};

/* One row of colours per theme, in the order of the pen slots above:
** background, grid, stronger grid, trace, dim trace, green, yellow, red, text, amber, lamp off */
STATIC CONST struct PenColor Colors[THEME_COUNT][PEN_COUNT] = {
	{   /* Phosphor */
		{   4,  14,   7 }, {  14,  44,  22 }, {  26,  80,  42 }, {  90, 255, 130 }, {  34, 130,  62 },
		{  40, 215,  70 }, { 240, 215,  40 }, { 255,  60,  40 }, { 140, 205, 155 }, { 255, 170,  30 }, {  50,  62,  54 }
	},
	{   /* Amber */
		{  14,   8,   2 }, {  50,  30,   8 }, {  90,  55,  14 }, { 255, 176,  40 }, { 140,  95,  25 },
		{ 200, 130,  20 }, { 255, 190,  60 }, { 255,  70,  30 }, { 205, 150,  70 }, { 255, 170,  30 }, {  62,  50,  36 }
	},
	{   /* Ice */
		{   3,   8,  16 }, {  12,  32,  56 }, {  24,  64, 104 }, { 120, 210, 255 }, {  40, 110, 160 },
		{  50, 170, 230 }, { 130, 215, 255 }, { 255,  90,  90 }, { 140, 185, 215 }, { 255, 170,  30 }, {  40,  52,  66 }
	},
	{   /* Rack */
		{  20,  20,  22 }, {  45,  45,  50 }, {  80,  80,  88 }, { 235, 235, 235 }, { 120, 120, 130 },
		{  40, 215,  70 }, { 240, 215,  40 }, { 255,  60,  40 }, { 190, 190, 195 }, { 255, 170,  30 }, {  58,  58,  64 }
	},
	{   /* Mono */
		{   0,   0,   0 }, {  38,  38,  38 }, {  80,  80,  80 }, { 255, 255, 255 }, { 130, 130, 130 },
		{ 170, 170, 170 }, { 215, 215, 215 }, { 255, 255, 255 }, { 190, 190, 190 }, { 255, 255, 255 }, {  50,  50,  50 }
	},
	{   /* Synth */
		{  10,   4,  18 }, {  40,  16,  64 }, {  80,  30, 110 }, { 255,  80, 220 }, { 130,  40, 120 },
		{   0, 230, 200 }, { 255, 220,  60 }, { 255,  60, 100 }, { 200, 150, 220 }, { 255, 170,  30 }, {  56,  40,  70 }
	}
};

STATIC CONST_STRPTR ThemeNames[THEME_COUNT] = { "Phosphor", "Amber", "Ice", "Rack", "Mono", "Synth" };

CONST_STRPTR ScopeThemeName(ULONG theme)
{
	return theme < THEME_COUNT ? ThemeNames[theme] : (CONST_STRPTR)"?";
}

struct ScopeData
{
	ULONG               mode;
	ULONG               theme;
	BOOL                is_setup;
	struct ScopeFrame * frame;
	ULONG               state;

	LONG                pen[PEN_COUNT];
	BOOL                pens_ok;
	BOOL                visible;

	struct BitMap *     bitmap;
	struct RastPort     rp;
	WORD                bm_width;
	WORD                bm_height;
};

/******************************************************************************
 *
 * Small drawing helpers (the pen is a slot of the enum above)
 *
 ******************************************************************************/

STATIC VOID FillBox(struct RastPort * rp, LONG pen, LONG x0, LONG y0, LONG x1, LONG y1)
{
	if (x1 < x0 || y1 < y0) {
		return;
	}

	SetAPen(rp, pen);
	RectFill(rp, x0, y0, x1, y1);
}

STATIC VOID DrawLine(struct RastPort * rp, LONG pen, LONG x0, LONG y0, LONG x1, LONG y1)
{
	SetAPen(rp, pen);
	Move(rp, x0, y0);
	Draw(rp, x1, y1);
}

STATIC VOID DrawText(struct RastPort * rp, LONG pen, LONG x, LONG y, CONST_STRPTR text)
{
	/* JAM1: only the glyph pixels are drawn, not the cell behind them */
	SetDrMd(rp, JAM1);
	SetAPen(rp, pen);
	Move(rp, x, y);
	Text(rp, (STRPTR)text, strlen(text));
}

/******************************************************************************
 *
 * Pens: ObtainBestPenA() hands out shared pens on any screen depth
 *
 ******************************************************************************/

STATIC VOID ObtainPens(struct ScopeData * data, struct ColorMap * cm)
{
	LONG i;

	data->pens_ok = TRUE;

	for (i = 0; i < PEN_COUNT; i++) {
		data->pen[i] = ObtainBestPenA(cm,
			(ULONG)Colors[data->theme][i].r * 0x01010101UL,
			(ULONG)Colors[data->theme][i].g * 0x01010101UL,
			(ULONG)Colors[data->theme][i].b * 0x01010101UL, NULL);

		if (data->pen[i] < 0) {
			data->pens_ok = FALSE;
		}
	}
}

STATIC VOID ReleasePens(struct ScopeData * data, struct ColorMap * cm)
{
	LONG i;

	for (i = 0; i < PEN_COUNT; i++) {
		if (data->pen[i] >= 0) {
			ReleasePen(cm, (ULONG)data->pen[i]);
			data->pen[i] = -1;
		}
	}
}

/******************************************************************************
 *
 * Off-screen bitmap
 *
 ******************************************************************************/

STATIC VOID FreeBuffer(struct ScopeData * data)
{
	if (data->bitmap != NULL) {
		WaitBlit();
		FreeBitMap(data->bitmap);
		data->bitmap = NULL;
	}

	data->bm_width = 0;
	data->bm_height = 0;
}

STATIC BOOL EnsureBuffer(Object * obj, struct ScopeData * data)
{
	struct BitMap * friend_bm = _screen(obj)->RastPort.BitMap;
	WORD w = _mwidth(obj);
	WORD h = _mheight(obj);

	if (data->bitmap != NULL && data->bm_width == w && data->bm_height == h) {
		return TRUE;
	}

	FreeBuffer(data);

	data->bitmap = AllocBitMap(w, h, GetBitMapAttr(friend_bm, BMA_DEPTH), BMF_MINPLANES, friend_bm);

	if (data->bitmap == NULL) {
		return FALSE;
	}

	data->bm_width  = w;
	data->bm_height = h;

	InitRastPort(&data->rp);
	data->rp.BitMap = data->bitmap;
	SetFont(&data->rp, _font(obj));

	return TRUE;
}

/******************************************************************************
 *
 * Oscilloscope: left on top, right below, a min/max line for every column
 *
 ******************************************************************************/

STATIC LONG SampleToY(LONG sample, LONG centre, LONG half)
{
	return centre - (((sample >> 8) * half) >> 15);
}

STATIC VOID DrawChannel(struct RastPort * rp, struct ScopeData * data, LONG ox, LONG oy, LONG w, LONG h,
	CONST LONG * samples, CONST_STRPTR name)
{
	LONG centre = oy + h / 2;
	LONG half = h / 2 - 2;
	LONG colw = (w >= 360) ? 2 : 1;
	LONG x, i, first, last, lo, hi, step;

	if (half < 2) {
		return;
	}

	/* Grid: centre line, +-50 %, vertical divisions */
	DrawLine(rp, data->pen[PEN_GRID2], ox, centre, ox + w - 1, centre);
	DrawLine(rp, data->pen[PEN_GRID], ox, centre - half / 2, ox + w - 1, centre - half / 2);
	DrawLine(rp, data->pen[PEN_GRID], ox, centre + half / 2, ox + w - 1, centre + half / 2);

	for (x = 1; x < 8; x++) {
		DrawLine(rp, data->pen[PEN_GRID], ox + (w * x) / 8, oy, ox + (w * x) / 8, oy + h - 1);
	}

	if (!data->frame->active) {
		return;
	}

	for (x = 0; x < w; x += colw) {
		first = (x * SCOPE_FRAMES) / w;
		last  = ((x + colw) * SCOPE_FRAMES) / w;

		if (last <= first) {
			last = first + 1;
		}

		if (last > SCOPE_FRAMES) {
			last = SCOPE_FRAMES;
		}

		lo = hi = samples[first];
		step = (last - first > 16) ? ((last - first) / 16) : 1;

		for (i = first + 1; i < last; i += step) {
			if (samples[i] < lo) {
				lo = samples[i];
			}

			if (samples[i] > hi) {
				hi = samples[i];
			}
		}

		FillBox(rp, data->pen[PEN_TRACE], ox + x, SampleToY(hi, centre, half), ox + x + colw - 1, SampleToY(lo, centre, half));
	}

	DrawText(rp, data->pen[PEN_TEXT], ox + 4, oy + rp->TxHeight, name);
}

STATIC VOID DrawWave(struct RastPort * rp, struct ScopeData * data, LONG ox, LONG oy, LONG w, LONG h)
{
	LONG half_h = h / 2;

	DrawChannel(rp, data, ox, oy, w, half_h, data->frame->left, "L");
	DrawChannel(rp, data, ox, oy + half_h, w, h - half_h, data->frame->right, "R");
	DrawLine(rp, data->pen[PEN_GRID2], ox, oy + half_h, ox + w - 1, oy + half_h);

	if (!data->frame->active) {
		DrawText(rp, data->pen[PEN_TRACE_DIM], ox + w / 2 - 40, oy + h / 2 + rp->TxHeight / 2, "NO SIGNAL");
	}
}

/******************************************************************************
 *
 * Spectrum: SPEC_BANDS bars, -70 .. 0 dB, hold caps, frequency marks
 *
 ******************************************************************************/

#define SPEC_RANGE 700  /* tenths of a dB shown */

STATIC LONG DbToHeight(LONG db10, LONG height)
{
	LONG v = db10 + SPEC_RANGE;

	if (v < 0) {
		v = 0;
	}

	if (v > SPEC_RANGE) {
		v = SPEC_RANGE;
	}

	return (v * height) / SPEC_RANGE;
}

STATIC VOID DrawSpectrum(struct RastPort * rp, struct ScopeData * data, LONG ox, LONG oy, LONG w, LONG h)
{
	LONG label_h = rp->TxHeight + 3;
	LONG plot_h = h - label_h;
	LONG shown = (w >= 2 * SPEC_BANDS) ? SPEC_BANDS : w / 2;
	LONG bar_w;
	LONG x0, b, k, first, last, top, hold, db, db_hold, y_yellow, y_red, base, x;
	LONG db_marks[3] = { -200, -400, -600 };
	LONG marks[3] = { 9, 33, 57 };
	CONST_STRPTR names[3] = { "100", "1k", "10k" };
	LONG i;

	if (shown < 8) {
		shown = 8;
	}

	bar_w = w / shown;

	if (bar_w < 1) {
		bar_w = 1;
	}

	x0 = ox + (w - bar_w * shown) / 2;
	base = oy + plot_h - 1;

	/* Grid: -20, -40, -60 dB */
	for (i = 0; i < 3; i++) {
		DrawLine(rp, data->pen[PEN_GRID], ox, base - DbToHeight(db_marks[i], plot_h - 1), ox + w - 1,
			base - DbToHeight(db_marks[i], plot_h - 1));
	}

	DrawLine(rp, data->pen[PEN_GRID2], ox, base, ox + w - 1, base);

	y_yellow = base - DbToHeight(-240, plot_h - 1);
	y_red    = base - DbToHeight(-90, plot_h - 1);

	/* In a small window several of the SPEC_BANDS share one bar: the highest of them is shown */
	for (b = 0; b < shown; b++) {
		first = (b * SPEC_BANDS) / shown;
		last  = ((b + 1) * SPEC_BANDS) / shown;

		if (last <= first) {
			last = first + 1;
		}

		db = db_hold = DB_FLOOR;

		for (k = first; k < last && k < SPEC_BANDS; k++) {
			if (data->frame->band[k] > db) {
				db = data->frame->band[k];
			}

			if (data->frame->band_hold[k] > db_hold) {
				db_hold = data->frame->band_hold[k];
			}
		}

		x = x0 + b * bar_w;
		top = base - DbToHeight(db, plot_h - 1);

		if (top < base) {
			/* green up to -24 dB, yellow up to -9 dB, red above */
			FillBox(rp, data->pen[PEN_GREEN], x, (top > y_yellow) ? top : y_yellow, x + bar_w - (bar_w > 1 ? 2 : 1), base - 1);

			if (top < y_yellow) {
				FillBox(rp, data->pen[PEN_YELLOW], x, (top > y_red) ? top : y_red, x + bar_w - (bar_w > 1 ? 2 : 1), y_yellow - 1);
			}

			if (top < y_red) {
				FillBox(rp, data->pen[PEN_RED], x, top, x + bar_w - (bar_w > 1 ? 2 : 1), y_red - 1);
			}
		}

		hold = base - DbToHeight(db_hold, plot_h - 1);

		if (hold < base) {
			FillBox(rp, data->pen[PEN_TRACE], x, hold, x + bar_w - (bar_w > 1 ? 2 : 1), hold);
		}
	}

	if (w >= 120) {
		for (i = 0; i < 3; i++) {
			DrawText(rp, data->pen[PEN_TEXT], x0 + ((marks[i] * shown) / SPEC_BANDS) * bar_w, oy + h - 2, names[i]);
		}
	}

	if (!data->frame->active && w >= 100) {
		DrawText(rp, data->pen[PEN_TRACE_DIM], ox + w / 2 - 40, oy + plot_h / 2, "NO SIGNAL");
	}
}

/******************************************************************************
 *
 * Meters: peak (top half) and RMS (bottom half) of each channel, 48 segments
 * from -60 to 0 dB, a hold marker and a clip lamp
 *
 ******************************************************************************/

#define METER_MAX_SEGMENTS 48
#define METER_RANGE        600
#define METER_LABEL_W      18
#define METER_LAMP_W       12

/* Segments that fit: two pixels each at least, 48 at most */
STATIC LONG MeterSegments(LONG w)
{
	LONG n = (w - METER_LABEL_W - METER_LAMP_W - 6) / 2;

	if (n > METER_MAX_SEGMENTS) {
		n = METER_MAX_SEGMENTS;
	}

	if (n < 8) {
		n = 8;
	}

	return n;
}

STATIC LONG DbToSegments(LONG db10, LONG nseg)
{
	LONG v = db10 + METER_RANGE;

	if (v < 0) {
		v = 0;
	}

	if (v > METER_RANGE) {
		v = METER_RANGE;
	}

	return (v * nseg) / METER_RANGE;
}

STATIC LONG SegmentPen(struct ScopeData * data, LONG segment, LONG nseg)
{
	/* segment 0 is -60 dB, nseg is 0 dB: green to -12, yellow to -3, red above */
	LONG db10 = -METER_RANGE + (segment * METER_RANGE) / nseg;

	if (db10 >= -30) {
		return data->pen[PEN_RED];
	}

	if (db10 >= -120) {
		return data->pen[PEN_YELLOW];
	}

	return data->pen[PEN_GREEN];
}

STATIC VOID DrawBar(struct RastPort * rp, struct ScopeData * data, LONG x0, LONG y0, LONG seg_w, LONG height,
	LONG lit, LONG nseg, BOOL bright)
{
	LONG s;
	LONG pen;

	for (s = 0; s < nseg; s++) {
		if (s < lit) {
			pen = bright ? SegmentPen(data, s, nseg) : data->pen[PEN_TRACE_DIM];
		} else {
			pen = data->pen[PEN_GRID];
		}

		FillBox(rp, pen, x0 + s * seg_w, y0, x0 + s * seg_w + seg_w - 2, y0 + height - 1);
	}
}

STATIC VOID DrawMeterRow(struct RastPort * rp, struct ScopeData * data, LONG ox, LONG oy, LONG w, LONG h,
	CONST_STRPTR name, LONG peak, LONG rms, LONG hold, ULONG clip)
{
	LONG nseg = MeterSegments(w);
	LONG seg_w = (w - METER_LABEL_W - METER_LAMP_W - 6) / nseg;
	LONG bar_h = (h - 3) / 2;
	LONG x0 = ox + METER_LABEL_W;
	LONG hold_seg;

	if (seg_w < 2) {
		seg_w = 2;
	}

	DrawText(rp, data->pen[PEN_TEXT], ox + 2, oy + (h + rp->TxHeight) / 2 - 2, name);

	DrawBar(rp, data, x0, oy + 1, seg_w, bar_h, DbToSegments(peak, nseg), nseg, TRUE);
	DrawBar(rp, data, x0, oy + 2 + bar_h, seg_w, bar_h, DbToSegments(rms, nseg), nseg, FALSE);

	hold_seg = DbToSegments(hold, nseg);

	if (hold_seg > 0) {
		FillBox(rp, data->pen[PEN_TRACE], x0 + (hold_seg - 1) * seg_w, oy, x0 + (hold_seg - 1) * seg_w + seg_w - 2, oy);
	}

	FillBox(rp, clip > 0 ? data->pen[PEN_RED] : data->pen[PEN_OFF], x0 + nseg * seg_w + 4, oy + 1,
		x0 + nseg * seg_w + 4 + METER_LAMP_W - 1, oy + h - 2);
}

STATIC VOID DrawMeter(struct RastPort * rp, struct ScopeData * data, LONG ox, LONG oy, LONG w, LONG h)
{
	LONG scale_h = rp->TxHeight + 3;
	LONG row_h = (h - scale_h) / 2;
	LONG nseg = MeterSegments(w);
	LONG seg_w = (w - METER_LABEL_W - METER_LAMP_W - 6) / nseg;
	LONG marks[8] = { -600, -480, -360, -240, -120, -60, -30, 0 };
	CONST_STRPTR names[8] = { "-60", "-48", "-36", "-24", "-12", "-6", "-3", "0" };
	LONG i;
	LONG x;
	LONG last_x = -1000;

	if (seg_w < 2) {
		seg_w = 2;
	}

	DrawMeterRow(rp, data, ox, oy, w, row_h, "L", data->frame->peak_l, data->frame->rms_l, data->frame->hold_l,
		data->frame->clip_l);
	DrawMeterRow(rp, data, ox, oy + row_h, w, row_h, "R", data->frame->peak_r, data->frame->rms_r, data->frame->hold_r,
		data->frame->clip_r);

	/* Scale marks, only those that do not run into each other */
	for (i = 0; i < 8; i++) {
		x = ox + METER_LABEL_W + (DbToSegments(marks[i], nseg) * seg_w);

		if (x - last_x >= 30) {
			DrawText(rp, data->pen[PEN_TEXT], x - 6, oy + h - 2, names[i]);
			last_x = x;
		}
	}
}

/******************************************************************************
 *
 * Lamp: a filled disc
 *
 ******************************************************************************/

STATIC VOID DrawLed(struct RastPort * rp, struct ScopeData * data, LONG ox, LONG oy, LONG w, LONG h)
{
	LONG radius = ((w < h ? w : h) / 2) - 1;
	LONG cx = ox + w / 2;
	LONG cy = oy + h / 2;
	LONG pen;
	LONG dy, dx;

	switch (data->state) {
	case LED_GREEN: pen = data->pen[PEN_GREEN]; break;
	case LED_AMBER: pen = data->pen[PEN_AMBER]; break;
	case LED_RED:   pen = data->pen[PEN_RED];   break;
	default:        pen = data->pen[PEN_OFF];   break;
	}

	for (dy = -radius; dy <= radius; dy++) {
		dx = 0;

		while ((dx + 1) * (dx + 1) + dy * dy <= radius * radius) {
			dx++;
		}

		FillBox(rp, pen, cx - dx, cy + dy, cx + dx, cy + dy);
	}
}

/******************************************************************************
 *
 * Draw
 *
 ******************************************************************************/

/* Black band above and below the drawing, inside the MUI frame */
#define SCOPE_MARGIN 2

STATIC VOID Render(struct RastPort * rp, struct ScopeData * data, LONG ox, LONG oy, LONG w, LONG h)
{
	FillBox(rp, data->pen[PEN_BG], ox, oy, ox + w - 1, oy + h - 1);

	oy += SCOPE_MARGIN;
	h  -= 2 * SCOPE_MARGIN;

	switch (data->mode) {
	case SCOPE_MODE_WAVE:     DrawWave(rp, data, ox, oy, w, h);     break;
	case SCOPE_MODE_SPECTRUM: DrawSpectrum(rp, data, ox, oy, w, h); break;
	case SCOPE_MODE_METER:    DrawMeter(rp, data, ox, oy, w, h);    break;
	case SCOPE_MODE_LED:      DrawLed(rp, data, ox, oy, w, h);      break;
	}
}

STATIC ULONG ScopeDraw(struct IClass * cl, Object * obj, struct MUIP_Draw * msg)
{
	struct ScopeData * data = INST_DATA(cl, obj);

	DoSuperMethodA(cl, obj, (Msg)msg);

	if ((msg->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) == 0 || !data->pens_ok || data->frame == NULL ||
	    _mwidth(obj) < 8 || _mheight(obj) < 8) {
		return 0;
	}

	if (EnsureBuffer(obj, data)) {
		Render(&data->rp, data, 0, 0, _mwidth(obj), _mheight(obj));
		BltBitMapRastPort(data->bitmap, 0, 0, _rp(obj), _mleft(obj), _mtop(obj), _mwidth(obj), _mheight(obj), 0xc0);
	} else {
		Render(_rp(obj), data, _mleft(obj), _mtop(obj), _mwidth(obj), _mheight(obj));
	}

	return 0;
}

/******************************************************************************
 *
 * Class methods
 *
 ******************************************************************************/

STATIC ULONG ScopeNew(struct IClass * cl, Object * obj, struct opSet * ops)
{
	struct ScopeData * data;
	struct TagItem * tags = ops->ops_AttrList;
	LONG i;

	if ((obj = (Object *)DoSuperMethodA(cl, obj, (Msg)ops)) == NULL) {
		return 0;
	}

	data = INST_DATA(cl, obj);

	data->mode  = GetTagData(MUIA_Scope_Mode, SCOPE_MODE_WAVE, tags);
	data->frame = (struct ScopeFrame *)GetTagData(MUIA_Scope_Frame, 0, tags);
	data->state = GetTagData(MUIA_Scope_State, LED_OFF, tags);
	data->theme = GetTagData(MUIA_Scope_Theme, THEME_PHOSPHOR, tags);

	if (data->theme >= THEME_COUNT) {
		data->theme = THEME_PHOSPHOR;
	}

	for (i = 0; i < PEN_COUNT; i++) {
		data->pen[i] = -1;
	}

	return (ULONG)obj;
}

STATIC ULONG ScopeSet(struct IClass * cl, Object * obj, struct opSet * ops)
{
	struct ScopeData * data = INST_DATA(cl, obj);
	struct TagItem * tag;
	struct TagItem * tstate = ops->ops_AttrList;
	BOOL redraw = FALSE;

	while ((tag = NextTagItem(&tstate)) != NULL) {
		switch (tag->ti_Tag) {
		case MUIA_Scope_Mode:
			data->mode = tag->ti_Data;
			redraw = TRUE;
			break;

		case MUIA_Scope_Frame:
			data->frame = (struct ScopeFrame *)tag->ti_Data;
			redraw = TRUE;
			break;

		case MUIA_Scope_Theme:
			if (tag->ti_Data < THEME_COUNT && data->theme != tag->ti_Data) {
				data->theme = tag->ti_Data;

				if (data->is_setup) {
					/* Between Setup and Cleanup: swap the pens at once */
					ReleasePens(data, _screen(obj)->ViewPort.ColorMap);
					ObtainPens(data, _screen(obj)->ViewPort.ColorMap);
				}

				redraw = TRUE;
			}
			break;

		case MUIA_Scope_State:
			if (data->state != tag->ti_Data) {
				data->state = tag->ti_Data;
				redraw = TRUE;
			}
			break;
		}
	}

	if (redraw && data->visible) {
		MUI_Redraw(obj, MADF_DRAWOBJECT);
	}

	return DoSuperMethodA(cl, obj, (Msg)ops);
}

STATIC ULONG ScopeGet(struct IClass * cl, Object * obj, struct opGet * opg)
{
	struct ScopeData * data = INST_DATA(cl, obj);

	switch (opg->opg_AttrID) {
	case MUIA_Scope_Mode:
		*opg->opg_Storage = data->mode;
		return TRUE;

	case MUIA_Scope_Frame:
		*opg->opg_Storage = (ULONG)data->frame;
		return TRUE;

	case MUIA_Scope_State:
		*opg->opg_Storage = data->state;
		return TRUE;

	case MUIA_Scope_Theme:
		*opg->opg_Storage = data->theme;
		return TRUE;
	}

	return DoSuperMethodA(cl, obj, (Msg)opg);
}

STATIC ULONG ScopeSetup(struct IClass * cl, Object * obj, Msg msg)
{
	struct ScopeData * data = INST_DATA(cl, obj);

	if (!DoSuperMethodA(cl, obj, msg)) {
		return FALSE;
	}

	ObtainPens(data, _screen(obj)->ViewPort.ColorMap);
	data->is_setup = TRUE;

	return TRUE;
}

STATIC ULONG ScopeCleanup(struct IClass * cl, Object * obj, Msg msg)
{
	struct ScopeData * data = INST_DATA(cl, obj);

	FreeBuffer(data);
	ReleasePens(data, _screen(obj)->ViewPort.ColorMap);
	data->pens_ok = FALSE;
	data->is_setup = FALSE;

	return DoSuperMethodA(cl, obj, msg);
}

STATIC ULONG ScopeAskMinMax(struct IClass * cl, Object * obj, struct MUIP_AskMinMax * msg)
{
	struct ScopeData * data = INST_DATA(cl, obj);
	LONG min_h;
	LONG min_w = 96;

	DoSuperMethodA(cl, obj, (Msg)msg);

	switch (data->mode) {
	case SCOPE_MODE_METER: min_h = 40; min_w = 120; break;
	case SCOPE_MODE_LED:   min_h = 14; min_w = 14;  break;
	default:               min_h = 48; min_w = 96;  break;
	}

	msg->MinMaxInfo->MinWidth  += min_w;
	msg->MinMaxInfo->DefWidth  += min_w * 3;
	msg->MinMaxInfo->MaxWidth  += (data->mode == SCOPE_MODE_LED) ? 14 : MUI_MAXMAX;
	msg->MinMaxInfo->MinHeight += min_h + 2 * SCOPE_MARGIN;
	msg->MinMaxInfo->DefHeight += min_h * 2 + 2 * SCOPE_MARGIN;
	msg->MinMaxInfo->MaxHeight += (data->mode == SCOPE_MODE_LED) ? 14 : MUI_MAXMAX;

	return 0;
}

STATIC ULONG __saveds __asm ScopeDispatch(register __a0 struct IClass * cl, register __a2 Object * obj,
	register __a1 Msg msg)
{
	struct ScopeData * data;
	ULONG result;

	switch (msg->MethodID) {
	case OM_NEW:            return ScopeNew(cl, obj, (struct opSet *)msg);
	case OM_SET:            return ScopeSet(cl, obj, (struct opSet *)msg);
	case OM_GET:            return ScopeGet(cl, obj, (struct opGet *)msg);

	case MUIM_Setup:        return ScopeSetup(cl, obj, msg);
	case MUIM_Cleanup:      return ScopeCleanup(cl, obj, msg);
	case MUIM_AskMinMax:    return ScopeAskMinMax(cl, obj, (struct MUIP_AskMinMax *)msg);
	case MUIM_Draw:         return ScopeDraw(cl, obj, (struct MUIP_Draw *)msg);

	case MUIM_Show:
		result = DoSuperMethodA(cl, obj, msg);
		data = INST_DATA(cl, obj);
		data->visible = TRUE;
		return result;

	case MUIM_Hide:
		data = INST_DATA(cl, obj);
		data->visible = FALSE;
		FreeBuffer(data);
		return DoSuperMethodA(cl, obj, msg);

	case MUIM_Scope_Refresh:
		data = INST_DATA(cl, obj);

		if (data->visible) {
			MUI_Redraw(obj, MADF_DRAWUPDATE);
		}

		return 0;
	}

	return DoSuperMethodA(cl, obj, msg);
}

/******************************************************************************
 *
 * ScopeInitClass() / ScopeFreeClass()
 *
 ******************************************************************************/

BOOL ScopeInitClass(VOID)
{
	ScopeClass = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct ScopeData), ScopeDispatch);

	return (BOOL)(ScopeClass != NULL);
}

VOID ScopeFreeClass(VOID)
{
	if (ScopeClass != NULL) {
		MUI_DeleteCustomClass(ScopeClass);
		ScopeClass = NULL;
	}
}

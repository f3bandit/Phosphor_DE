// NOR Check - drawing (Phosphor style, mode 3, 240x160)
#include <string.h>
#include <gba_video.h>
#include <gba_input.h>
#include "ui.h"
#if defined(FONT_EZ)
#include "font_asc126.h"   // EZ-Flash kernel 6x12 font
#elif defined(FONT_SPLEEN)
#include "font_spleen.h"
#elif defined(FONT_TAMZEN)
#include "font_tamzen.h"
#else
#include "font_tamzenb.h"  // Tamzen Bold 6x12: the Phosphor_DE font
#endif

#define VRAM16 ((u16 *)0x06000000)

void ui_init(void) { SetMode(MODE_3 | BG2_ENABLE); ui_clear(); }
void ui_fill(int x, int y, int w, int h, u16 c)
{
	int i, j;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > 240) w = 240 - x;
	if (y + h > 160) h = 160 - y;
	for (j = 0; j < h; j++) { u16 *d = VRAM16 + (y + j) * 240 + x; for (i = 0; i < w; i++) d[i] = c; }
}
void ui_clear(void) { ui_fill(0, 0, 240, 160, C_BG); }
void ui_box(int x, int y, int w, int h, u16 fill, u16 edge)
{
	ui_fill(x + 1, y, w - 2, h, fill); ui_fill(x, y + 1, 1, h - 2, fill); ui_fill(x + w - 1, y + 1, 1, h - 2, fill);
	if (edge != 0xFFFF) {
		ui_fill(x + 1, y, w - 2, 1, edge); ui_fill(x + 1, y + h - 1, w - 2, 1, edge);
		ui_fill(x, y + 1, 1, h - 2, edge); ui_fill(x + w - 1, y + 1, 1, h - 2, edge);
	}
}
static void glyph(int x, int y, unsigned char ch, u16 c)
{
	int r, i;
	if (ch >= 128) ch = '?';
	const unsigned char *g = &NC_FONT[ch * 12];
	for (r = 0; r < 12; r++) {
		if (y + r < 0 || y + r >= 160) continue;
		for (i = 0; i < 6; i++)
			if ((g[r] & (0x80 >> i)) && x + i >= 0 && x + i < 240) VRAM16[(y + r) * 240 + x + i] = c;
	}
}
// Latin-1 letters (U+00C0..U+00FF) drawn as their plain ASCII letter; other non-ASCII as '?'
static const char lat1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
static unsigned char utf8_ch(const char **ps)
{
	const unsigned char *s = (const unsigned char *)*ps;
	unsigned char ch = *s++;
	if (ch >= 0x80) {
		if ((ch == 0xC3) && (*s & 0xC0) == 0x80) ch = lat1[(*s++ & 0x3F) + 0x00];
		else { while ((*s & 0xC0) == 0x80) s++; ch = '?'; }
	}
	*ps = (const char *)s; return ch;
}
int ui_text(int x, int y, const char *s, u16 c)
{
	while (*s) { glyph(x, y, utf8_ch(&s), c); x += 6; }
	return x;
}
int ui_textlen(const char *s) { int n = 0; while (*s) { unsigned char ch = (unsigned char)*s++; if (ch >= 0x80) while ((((unsigned char)*s) & 0xC0) == 0x80) s++; n++; } return n; }
void ui_textw(int x, int y, const char *s, u16 c, int maxw)
{
	int n = ui_textlen(s), max = maxw / 6, i = 0;
	if (n <= max) { ui_text(x, y, s, c); return; }
	while (*s && i < max - 2) { glyph(x + i * 6, y, utf8_ch(&s), c); i++; }
	ui_text(x + (max - 2) * 6, y, "..", c);
}
void ui_top(const char *title, const char *sub)
{
	ui_fill(0, 0, 240, 18, C_BG);
	ui_box(2, 2, strlen(title) * 6 + 8, 14, C_PANEL, 0xFFFF);
	int x = ui_text(6, 3, title, C_AC);
	if (sub) ui_text(x + 10, 3, sub, C_DIM);
}
static int btn(int x, int y, const char *l)
{
	int w = strlen(l) * 6 + 4; if (w < 10) w = 10;
	ui_box(x, y, w, 12, C_BG, C_DIM);
	ui_text(x + (w - (int)strlen(l) * 6) / 2, y, l, C_TXT);
	return x + w;
}
void ui_hints(const char *const *pairs, int n)
{
	int i, x = 3;
	ui_fill(0, 144, 240, 16, C_BG);
	ui_fill(0, 145, 240, 1, C_LINE);
	for (i = 0; i < n; i++) {
		if (pairs[i * 2][0]) x = btn(x, 147, pairs[i * 2]) + 2;
		x = ui_text(x, 147, pairs[i * 2 + 1], C_DIM) + 6;
	}
}
void ui_row_sel(int y) { ui_box(2, y, 236, 13, C_PON, 0xFFFF); ui_fill(2, y + 1, 1, 11, C_AC); }
void ui_vsync(void) { while (REG_VCOUNT >= 160) ; while (REG_VCOUNT < 160) ; }
u16 ui_keys(void) { return (~REG_KEYINPUT) & 0x3FF; }

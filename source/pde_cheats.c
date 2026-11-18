// Phosphor_DE cheats - by f3bandit
//
// One file per game in /SYSTEM/CHEATS, named by the 4-letter game code from the ROM header
// (BPEE.cht = Pokemon Emerald US), the same way the box art is named.
// Two layouts are read:
//   RetroArch .cht      cheat0_desc = "Name" / cheat0_code = "XXXXXXXX+YYYY+..." / cheat0_enable = false
//   plain text          a name line, then its codes one "XXXXXXXX YYYY" per line (paste from GameHacking.org)
// Codes are Code Breaker (unencrypted). They are compiled here into entries for the in-game
// engine (pde_engine.inc): writes, or/and/add, if ==/!=/>/</&, hold-a-button, fill.
// ROM writes become patches applied to the ROM image when it loads.
// Cheats you turn on are saved to /SYSTEM/CHEATS/<CODE>.sel ("Name" per line) and stay on.
#ifdef PDE_UI
#include <gba_base.h>
#include <gba_input.h>
#include <gba_systemcalls.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "ezkernel.h"
#include "showcht.h"
#include "ui.h"
#include "pde.h"

extern FIL gfile;
extern ST_entry pCHEAT[];
extern u32 gl_cheat_count;
extern u16 gl_cheat_on;

#define CH_MAX   448
#define CH_NAME  56
#define FL_BAD   1      // a code we can't run (encrypted, unknown type)
typedef struct { char name[CH_NAME]; u32 noff, off, end; u8 sel, fl; u8 pad[2]; } pc_cheat;
// noff: file offset of the full name; off/end: the code text (retro: end 0 = up to the closing quote)

// scratch in pReadCache above the game card's box art (0x10000..0x14B38)
#define PC_CH_OFS  0x15000
#define PC_BLK_OFS 0x1F000
#define PC_BLK_LEN 0x800
#define PC_FIL_OFS (PC_BLK_OFS + PC_BLK_LEN)
_Static_assert(PC_CH_OFS + CH_MAX * sizeof(pc_cheat) <= PC_BLK_OFS, "cheat table overlaps the read block");
_Static_assert(PC_FIL_OFS + sizeof(FIL) <= MAX_pReadCache_size, "cheat file object past pReadCache");
#define CH  ((pc_cheat *)(pReadCache + PC_CH_OFS))
#define BLK ((char *)(pReadCache + PC_BLK_OFS))
#define CF  ((FIL *)(pReadCache + PC_FIL_OFS))     // the cheat file, kept open while the screen is up

static int n_ch, retro;
static char cht_path[40], sel_path[40];
static int cf_open;
static int cht_open(void) { if (!cf_open) cf_open = (f_open(CF, cht_path, FA_READ) == FR_OK); return cf_open; }
static void cht_close(void) { if (cf_open) f_close(CF); cf_open = 0; }

// ---------------------------------------------------------------- helpers
static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t') s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
	return s;
}
static void copy_n(char *d, const char *s, int n) { strncpy(d, s, n - 1); d[n - 1] = 0; }
static int hexv(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}
static int is_code_line(const char *t)   // only hex digits and separators, at least 12 digits
{
	int d = 0;
	for (; *t; t++) { if (hexv(*t) >= 0) d++; else if (*t != ' ' && *t != '\t' && *t != '+' && *t != ':' && *t != '-') return 0; }
	return d >= 12;
}

// ---------------------------------------------------------------- parse
static int new_cheat(const char *name, u32 noff)
{
	if (n_ch >= CH_MAX) return -1;
	pc_cheat *c = &CH[n_ch];
	memset(c, 0, sizeof *c);
	copy_n(c->name, name, CH_NAME); c->noff = noff;
	return n_ch++;
}
static int pl_cur;
static void handle_line(char *raw, u32 start)
{
	char *t = trim(raw);
	if (!t[0] || t[0] == '#' || (t[0] == '/' && t[1] == '/')) return;
	if (retro) {
		// cheatN_desc = "..."   cheatN_code = "..."
		if (strncmp(t, "cheat", 5)) return;
		int idx = 0; char *p = t + 5;
		if (*p < '0' || *p > '9') return;
		while (*p >= '0' && *p <= '9') idx = idx * 10 + (*p++ - '0');
		if (idx >= CH_MAX || *p != '_') return;
		char *q = strchr(p, '"'); if (!q) return;
		u32 voff = start + (q + 1 - raw);
		while (n_ch <= idx) if (new_cheat("", 0) < 0) return;
		if (!strncmp(p, "_desc", 5)) {
			char *e = strchr(q + 1, '"'); if (e) *e = 0;
			copy_n(CH[idx].name, trim(q + 1), CH_NAME); CH[idx].noff = voff;
		} else if (!strncmp(p, "_code", 5)) {
			CH[idx].off = voff; CH[idx].end = 0;
		}
		return;
	}
	// plain text: a name line, then code lines
	if (is_code_line(t)) {
		if (pl_cur < 0) { char nm[16]; sprintf(nm, "Cheat %d", n_ch + 1); pl_cur = new_cheat(nm, 0); if (pl_cur < 0) return; }
		if (!CH[pl_cur].off) CH[pl_cur].off = start;
		CH[pl_cur].end = 0xFFFFFFFF;   // fixed up when the next name starts
		return;
	}
	if (pl_cur >= 0 && CH[pl_cur].end == 0xFFFFFFFF) CH[pl_cur].end = start;
	if (t[0] == '[') { t++; char *e = strchr(t, ']'); if (e) *e = 0; }
	int l = strlen(t); if (l && t[l - 1] == ':') t[l - 1] = 0;
	t = trim(t);
	pl_cur = new_cheat(t, start + (t - raw));
}
static int parse_cht(void)
{
	n_ch = 0; pl_cur = -1; retro = 0;
	if (!cht_open()) return 0;
	UINT br = 0, bi = 0;
	f_lseek(CF, 0);   // RetroArch layout if "cheatN_code" shows up near the top
	if (f_read(CF, BLK, PC_BLK_LEN - 1, &br) == FR_OK) { BLK[br] = 0; if (strstr(BLK, "_code") && strstr(BLK, "cheat")) retro = 1; }
	f_lseek(CF, 0); br = 0;
	char line[160]; int ll = 0;
	u32 pos = 0, line_start = 0;
	for (;;) {
		if (bi >= br) {
			pos += br; bi = 0;
			if (f_read(CF, BLK, PC_BLK_LEN, &br) != FR_OK || !br) break;
		}
		char c = BLK[bi++];
		if (c == '\n') { line[ll] = 0; handle_line(line, line_start); ll = 0; line_start = pos + bi; continue; }
		if (c == '\r') continue;
		if (ll < (int)sizeof line - 1) line[ll++] = c;
		if (pos + bi == 3 && ll == 3 && (u8)line[0] == 0xEF && (u8)line[1] == 0xBB) { ll = 0; line_start = 3; }   // UTF-8 BOM
	}
	if (ll) { line[ll] = 0; handle_line(line, line_start); }
	if (!retro && pl_cur >= 0 && CH[pl_cur].end == 0xFFFFFFFF) CH[pl_cur].end = pos;
	int w = 0;   // drop cheats with no code; unnamed ones get a number
	for (int i = 0; i < n_ch; i++) if (CH[i].off) {
		if (w != i) CH[w] = CH[i];
		if (!CH[w].name[0]) sprintf(CH[w].name, "Cheat %d", w + 1);
		w++;
	}
	n_ch = w;
	return n_ch;
}

// ---------------------------------------------------------------- Code Breaker -> engine entries
enum { OP_W8, OP_W16, OP_OR, OP_AND, OP_ADD, OP_IFEQ, OP_IFNE, OP_IFGT, OP_IFLT, OP_IFAND, OP_KEYS, OP_FILL, OP_ROM16 = 15 };
static int cb_emit_ok;          // 0 = dry run (only check), 1 = really write pCHEAT
static u32 cb_n;                // entries this cheat produced
static void emit(u32 w0, u32 w1)
{
	if (cb_emit_ok && gl_cheat_count < MAX_CHEAT_ENTRIES) { pCHEAT[gl_cheat_count].address = w0; pCHEAT[gl_cheat_count].VAL = w1; gl_cheat_count++; }
	cb_n++;
}
static void set_skip(u32 idx, u32 skip)
{
	if (cb_emit_ok && idx < gl_cheat_count) pCHEAT[idx].VAL = (pCHEAT[idx].VAL & 0xFFFF) | ((skip > 255 ? 255 : skip) << 16);
}
static int ram_ok(u32 a) { u32 r = a >> 24; return r >= 2 && r <= 7; }
static int rom_ok(u32 a) { return a >= 0x08000000 && a < 0x0A000000; }
static u16 swap16(u32 h) { return ((h >> 8) | (h << 8)) & 0xFFFF; }

// one Code Breaker line; returns 0 if it can't be run
static int fill_wait, list_left, cond_wait;
static u32 list_addr, cond_idx, cond_start;
static int cb_line(u32 op1, u32 op2)
{
	u32 type = op1 >> 28, a = op1 & 0x0FFFFFFF;
	int code_done = 1, is_cond = 0;
	static const u8 cop[16] = { [0x7] = OP_IFEQ, [0xA] = OP_IFNE, [0xB] = OP_IFGT, [0xC] = OP_IFLT, [0xF] = OP_IFAND };
	if (fill_wait) { emit(op1, op2); fill_wait = 0; }                     // repeat | value step<<16, address step
	else if (list_left) {                                                  // 5: halfwords follow, byte-swapped
		u32 h[3] = { op1 >> 16, op1 & 0xFFFF, op2 };
		for (int k = 0; k < 3 && list_left; k++, list_left--) { emit((OP_W16 << 28) | list_addr, swap16(h[k])); list_addr += 2; }
		code_done = !list_left;
	}
	else switch (type) {
	case 0x0: case 0x1: code_done = 0; break;                              // game id / hook: nothing to run
	case 0x2: if (!ram_ok(a)) return 0; emit((OP_OR << 28) | a, op2); break;
	case 0x3: if (!ram_ok(a)) return 0; emit((OP_W8 << 28) | a, op2 & 0xFF); break;
	case 0x4: if (!ram_ok(a)) return 0; emit((OP_FILL << 28) | a, op2); fill_wait = 1; code_done = 0; break;
	case 0x5: if (!ram_ok(a) || !op2) return 0; list_addr = a; list_left = op2; code_done = 0; break;
	case 0x6: if (!ram_ok(a)) return 0; emit((OP_AND << 28) | a, op2); break;
	case 0x8:
		if (rom_ok(a)) { if (cond_wait) return 0; emit((OP_ROM16 << 28) | (a - 0x08000000), op2); }
		else { if (!ram_ok(a)) return 0; emit((OP_W16 << 28) | a, op2); }
		break;
	case 0x7: case 0xA: case 0xB: case 0xC: case 0xF:
		if (!ram_ok(a)) return 0;
		emit((cop[type] << 28) | a, op2); is_cond = 1; break;
	case 0xD:
		if (a != 0x20) return 0;                                           // only "if keys held"
		emit((OP_KEYS << 28) | 0x04000130, op2); is_cond = 1; break;
	case 0xE: if (!ram_ok(a)) return 0; emit((OP_ADD << 28) | a, op2); break;
	default: return 0;                                                     // 9 = encrypted
	}
	// a condition covers the whole next code
	if (cond_wait && (code_done || is_cond) && !fill_wait && !list_left) { set_skip(cond_idx, cb_n - cond_start - (is_cond ? 0 : 0)); cond_wait = 0; }
	if (is_cond) { cond_wait = 1; cond_idx = gl_cheat_count - 1; cond_start = cb_n; }
	return 1;
}
static int compile(int ci)
{
	pc_cheat *c = &CH[ci];
	u32 tok0 = 0, cur = 0; int nt = 0, digits = 0;
	UINT br, bi;
	fill_wait = list_left = cond_wait = 0;
	if (!cht_open()) return 0;
	f_lseek(CF, c->off);
	u32 pos = c->off;
	for (;;) {
		int eof = (f_read(CF, BLK, 128, &br) != FR_OK || !br);
		if (eof) br = 0;
		for (bi = 0; bi <= br; bi++) {
			if (bi == br && !eof) break;                                   // token may carry into the next block
			char ch = bi < br ? BLK[bi] : 0;
			int end_here = (bi == br) || (!retro && pos + bi >= c->end) || (retro && ch == '"');
			int d = end_here ? -1 : hexv(ch);
			if (d >= 0) { cur = (cur << 4) | d; if (++digits > 8) return 0; continue; }
			if (digits) {
				if (nt == 0) { if (digits != 8) return 0; tok0 = cur; nt = 1; }
				else { if (digits != 4) return 0; nt = 0; if (!cb_line(tok0, cur)) return 0; }
			}
			digits = 0; cur = 0;
			if (end_here) return !(nt || fill_wait || list_left);
		}
		pos += br;
	}
}
// "Anti DMA" and "(Must Be On)" codes are what keep a game's other codes working
// (e.g. Pokemon moves its data around without Anti DMA): they go on with any other cheat
static int is_auto(int i)
{
	const char *n = CH[i].name;
	return strstr(n, "Anti DMA") || strstr(n, "Anti-DMA") || strstr(n, "Must Be On") || strstr(n, "must be on");
}
static int count_on(void);
static void apply(void)
{
	int any = count_on();
	gl_cheat_count = 0;
	cb_emit_ok = 1;
	for (int i = 0; i < n_ch; i++) if ((CH[i].sel || (any && is_auto(i))) && !(CH[i].fl & FL_BAD)) {
		u32 keep = gl_cheat_count;
		cb_n = 0;
		if (!compile(i)) gl_cheat_count = keep;
	}
}
static void check_all(void)
{
	cb_emit_ok = 0;
	for (int i = 0; i < n_ch; i++) { cb_n = 0; if (!compile(i) || !cb_n) CH[i].fl |= FL_BAD; }
}
static int count_on(void) { int n = 0; for (int i = 0; i < n_ch; i++) if (CH[i].sel && !(CH[i].fl & FL_BAD) && !is_auto(i)) n++; return n; }

// ---------------------------------------------------------------- remembered picks
static void load_sel(void)
{
	UINT br;
	if (!sel_path[0] || f_open(&gfile, sel_path, FA_READ) != FR_OK) return;
	if (f_read(&gfile, BLK, PC_BLK_LEN - 1, &br) != FR_OK) br = 0;
	f_close(&gfile);
	BLK[br] = 0;
	char *p = BLK;
	while (*p) {
		char *nl = strchr(p, '\n'); if (nl) *nl = 0;
		char *cr = strchr(p, '\r'); if (cr) *cr = 0;
		char *nm = trim(p);
		if (strlen(nm) > CH_NAME - 1) nm[CH_NAME - 1] = 0;   // names are kept to CH_NAME-1 characters
		for (int i = 0; i < n_ch; i++) if (!CH[i].sel && !strcmp(CH[i].name, nm)) { CH[i].sel = 1; break; }
		if (!nl) break;
		p = nl + 1;
	}
}
static void save_sel(void)
{
	UINT bw;
	if (!sel_path[0]) return;
	if (!count_on()) { f_unlink(sel_path); return; }
	if (f_open(&gfile, sel_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
		f_mkdir("/SYSTEM/CHEATS");
		if (f_open(&gfile, sel_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return;
	}
	for (int i = 0; i < n_ch; i++) if (CH[i].sel) {
		f_write(&gfile, CH[i].name, strlen(CH[i].name), &bw);
		f_write(&gfile, "\r\n", 2, &bw);
	}
	f_close(&gfile);
}

// ---------------------------------------------------------------- game revision
// A cheat named "... (Rev N)" fits only that revision of the game: its ROM patches were made for it
// (FireRed/LeafGreen 1.0 and 1.1 share a game code, not their code addresses). The ROM header's version
// byte (0xBC) says which revision this is; cheats for another one are left out of the list altogether.
static int rev_of(const char *name)
{
	const char *p = strstr(name, "(Rev ");
	if (!p || p[5] < '0' || p[5] > '9' || p[6] != ')') return -1;
	return p[5] - '0';
}
static int game_rev(TCHAR *gamefile)   // -1: can't tell (then nothing is left out)
{
	UINT br = 0; u8 v = 0xFF;
	if (!gamefile || f_open(&gfile, gamefile, FA_READ) != FR_OK) return -1;
	if (f_lseek(&gfile, 0xBC) != FR_OK || f_read(&gfile, &v, 1, &br) != FR_OK || br != 1) v = 0xFF;
	f_close(&gfile);
	return v == 0xFF ? -1 : v;
}
static void drop_other_revs(int rev)
{
	if (rev < 0) return;
	int k = 0;
	for (int i = 0; i < n_ch; i++) {
		int r = rev_of(CH[i].name);
		if (r >= 0 && r != rev) continue;
		if (k != i) CH[k] = CH[i];
		k++;
	}
	n_ch = k;
}

// ---------------------------------------------------------------- public
int pde_cheats_probe(TCHAR *gamefile, const char *code)
{
	gl_cheat_count = 0; n_ch = 0;
	cht_close();
	if (!code[0]) return -1;
	sprintf(cht_path, "/SYSTEM/CHEATS/%.4s.cht", code);
	sprintf(sel_path, "/SYSTEM/CHEATS/%.4s.sel", code);
	if (!parse_cht()) { cht_close(); return -1; }
	drop_other_revs(game_rev(gamefile));
	check_all();
	load_sel();
	int n = count_on();
	if (n) apply();
	cht_close();
	return n;
}

// full name of a cheat (names longer than CH_NAME are read back from the file)
static const char *full_name(int i)
{
	static char nb[100];
	pc_cheat *c = &CH[i];
	if (i >= n_ch || !c->noff || strlen(c->name) < CH_NAME - 1 || !cht_open()) return c->name;
	UINT br = 0;
	f_lseek(CF, c->noff);
	if (f_read(CF, nb, sizeof nb - 1, &br) != FR_OK) br = 0;
	nb[br] = 0;
	char *e = strpbrk(nb, retro ? "\"\r\n" : "\r\n]"); if (e) *e = 0;
	return trim(nb);
}
static const char *skip_chars(const char *s, int n)
{
	while (*s && n > 0) { s++; while ((((u8)*s) & 0xC0) == 0x80) s++; n--; }
	return s;
}

#define CR_Y0  47
#define CR_H   12
#define CR_N   8
#define NAME_W 186           // pixels for the name column (31 characters)
static void pill(int xr, int y, const char *t, int on, int sel)
{
	int w = ui_textlen(t) * 6 + 6, x = xr - w;
	if (on) ui_box(x, y + 1, w, 10, C_AC, 0xFFFF);
	else ui_box(x, y + 1, w, 10, sel ? C_PON : C_PANEL, C_FAINT);
	ui_text(x + 3, y, t, on ? C_BG : C_FAINT);
}
static void draw_row(int i, int r, int cur, int scroll)
{
	int y = CR_Y0 + r * CR_H, sel = (i == cur);
	ui_fill(6, y, 226, CR_H, C_PANEL);
	if (i >= n_ch) return;
	if (sel) { ui_box(6, y, 226, CR_H, C_PON, 0xFFFF); ui_fill(6, y + 1, 1, CR_H - 2, C_AC); }
	const pc_cheat *c = &CH[i];
	const char *nm = sel ? full_name(i) : c->name;
	u16 col = (c->fl & FL_BAD) ? C_FAINT : (sel ? C_TXT : C_DIM);
	if (sel && scroll > 0) nm = skip_chars(nm, scroll);
	ui_textw(12, y, nm, col, NAME_W);
	if (c->fl & FL_BAD) pill(230, y, "n/a", 0, sel);
	else if (is_auto(i)) pill(230, y, "auto", 0, sel);
	else pill(230, y, c->sel ? "ON" : "off", c->sel, sel);
}
int pde_cheats(TCHAR *gamefile, const char *code, const char *title)
{
	int cur = 0, top = 0, redraw = 2, dirty = 0;
	int scroll = 0, stimer = 0, slen = 0;
	char s[48];
	if (pde_cheats_probe(gamefile, code) < 0) {
		if (code[0]) sprintf(s, "Add /SYSTEM/CHEATS/%.4s.cht", code); else strcpy(s, "This ROM has no game code.");
		pde_popup("No cheats for this game", s, C_AC);
		pde_wait_release(); while (!(pde_keys_down() & (KEY_A | KEY_B))) VBlankIntrWait();
		return -1;
	}
	cht_open();
	pde_wait_release();
	for (;;) {
		if (redraw) {
			if (redraw > 1) { ui_box(4, 19, 232, 124, C_PANEL, C_AC); ui_fill(5, 20, 230, 122, C_PANEL); }
			ui_fill(5, 20, 230, 25, C_PANEL);
			int x = ui_text(10, 22, "Cheats", C_AC);
			sprintf(s, "%d on", count_on());
			int cw = ui_textlen(s) * 6;
			ui_textw(x + 8, 22, title, C_DIM, 222 - cw - x - 16);
			ui_text(228 - cw, 22, s, count_on() ? C_AC : C_FAINT);
			if (!gl_cheat_on) ui_text(10, 33, "Cheats are off in Settings > Add-ons", C_RED);
			else if (gl_cheat_count >= MAX_CHEAT_ENTRIES) ui_text(10, 33, "Too many codes on - some are dropped", C_RED);
			else { sprintf(s, "%d cheats", n_ch); ui_text(10, 33, s, C_FAINT); }
			ui_fill(8, 45, 224, 1, C_LINE);
			for (int r = 0; r < CR_N; r++) draw_row(top + r, r, cur, top + r == cur ? scroll : 0);
			if (n_ch > CR_N) {   // scroll bar
				int h = 96 * CR_N / n_ch; if (h < 6) h = 6;
				int yb = CR_Y0 + (96 - h) * top / (n_ch - CR_N);
				ui_fill(233, CR_Y0, 1, 96, C_LINE); ui_fill(233, yb, 1, h, C_AC);
			}
			if (redraw > 1) {
				static const char *const h[] = { "A", "On/off", "SL", "All off", "LR", "Page", "B", "Done" };
				ui_hints(h, 4);
			}
			redraw = 0;
			slen = ui_textlen(full_name(cur)) - NAME_W / 6;   // characters past the edge
		}
		VBlankIntrWait();
		// a long name scrolls: wait, slide along, hold at the end, start over
		if (slen > 0) {
			int step = ++stimer - 45, ns = step < 0 ? 0 : step / 6;
			if (ns > slen) ns = slen;
			if (step / 6 > slen + 10) { stimer = 0; ns = 0; }
			if (ns != scroll) { scroll = ns; draw_row(cur, cur - top, cur, scroll); }
		}
		u16 k = pde_keys_down();
		int moved = 0;
		if (k & (KEY_B | KEY_START)) break;
		if (k & KEY_DOWN) { cur = (cur + 1) % n_ch; moved = 1; }
		if (k & KEY_UP) { cur = (cur + n_ch - 1) % n_ch; moved = 1; }
		if (k & KEY_R) { cur += CR_N; if (cur >= n_ch) cur = n_ch - 1; moved = 1; }
		if (k & KEY_L) { cur -= CR_N; if (cur < 0) cur = 0; moved = 1; }
		if ((k & KEY_A) && !(CH[cur].fl & FL_BAD) && !is_auto(cur)) { CH[cur].sel = !CH[cur].sel; dirty = redraw = 1; }
		if (k & KEY_SELECT) { for (int i = 0; i < n_ch; i++) CH[i].sel = 0; dirty = redraw = 1; }
		if (moved) { redraw = 1; scroll = 0; stimer = 0; }
		if (cur < top) top = cur;
		if (cur >= top + CR_N) top = cur - CR_N + 1;
		if (dirty && redraw) apply();   // keeps the count live
	}
	// the codes are already compiled (after each change, or by the probe); only the picks file is left
	if (dirty) { ui_fill(150, 33, 80, 11, C_PANEL); ui_text(170, 33, "Saving...", C_AC); save_sel(); }
	cht_close();
	pde_wait_release();
	return count_on();
}
#endif

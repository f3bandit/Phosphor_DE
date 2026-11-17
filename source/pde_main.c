// Phosphor_DE - front end for the EZ-Flash Omega Definitive Edition
// by f3bandit. Engine: EZ-Flash kernel (Apache-2.0).
//
// This file is only the user interface. Every action calls the kernel's own engine
// (Launch_file, Boot_NOR_game, FormatNor, NorCheck_Run, ...), so loading, saving,
// add-ons and NOR writes behave exactly as they do in the EZ-Flash kernel.
#ifdef PDE_UI
#include <stdio.h>
#include <string.h>
#include <gba_base.h>
#include <gba_video.h>
#include <gba_input.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include "ff.h"
#include "ez_define.h"
#include "ezkernel.h"
#include "Ezcard_OP.h"
#include "NORflash_OP.h"
#include "draw.h"
#include "lang.h"
#include "RTC.h"
#include "ui.h"
#include "pde.h"

// ---------------------------------------------------------------- kernel engine
extern FM_FILE_FS pFilename_buffer[MAX_files];
extern FM_NOR_FS pNorFS[MAX_NOR];
extern FM_Folder_FS pFolder[MAX_folder];
extern u8 pReadCache[];
extern char p_recently_play[10][512];
extern TCHAR currentpath[MAX_path_len];
extern u32 game_total_SD, game_total_NOR, folder_total, gl_currentpage, gl_norOffset;
extern u16 gl_show_Thumbnail, gl_reset_on, gl_rts_on, gl_sleep_on, gl_cheat_on;
extern u8 GAMECODE[4];
extern FATFS EZcardFs;
extern FIL gfile;
extern DIR dir;
extern FILINFO fileinfo;
extern void CheckLanguage(void);
extern void CheckSwitch(void);
extern void Check_save_flag(void);
extern void Sort_folder(u32);
extern void Sort_file(u32);
extern u32  Load_Thumbnail(TCHAR *);
extern u32  Check_file_type(TCHAR *);
extern u8   Check_mde_file(TCHAR *);
extern u32  get_count(void);
extern void Boot_NOR_game(u32 show_offset, u32 file_select, u32 key_L);
extern void FormatNor(void);
extern void NorCheck_Run(void);
extern u8   Launch_file(TCHAR *pfilename, u32 MENU_line, u8 Save_num, u8 old_Save_num, u32 key_L, u32 is_EMU);
extern void Check_FW_update(void);

// ---------------------------------------------------------------- input
static u16 kheld, kprev, krep;
static u32 krept;
u16 pde_keys_down(void)
{
	kprev = kheld; kheld = ui_keys();
	u16 d = kheld & ~kprev;
	// up/down/left/right repeat
	u16 dirs = kheld & 0xF0;
	if (dirs && dirs == krep) { if (++krept > 18 && (krept & 3) == 0) d |= dirs; }
	else { krep = dirs; krept = 0; }
	return d;
}
void pde_wait_release(void) { while (ui_keys()) VBlankIntrWait(); kheld = kprev = 0; }

// ---------------------------------------------------------------- common chrome
enum { TAB_SD = 0, TAB_NOR, TAB_RECENT, TAB_SETTINGS, TAB_COUNT };
static const char *const tab_names[TAB_COUNT] = { "SD", "NOR", "Recent", "Settings" };
static int tab = TAB_SD;

static void draw_clock(void)
{
	u8 t[3]; char s[8];
	rtc_enable(); rtc_gettime(t); rtc_disenable();
	u32 h = UNBCD(t[0] & 0x3F), m = UNBCD(t[1] & 0x7F);
	sprintf(s, "%2lu:%02lu", h, m);
	ui_box(W_CLOCK_X, 2, 38, 14, C_PANEL, 0xFFFF);
	ui_text(W_CLOCK_X + 4, 3, s, C_TXT);
}
static void draw_top(void)
{
	ui_fill(0, 0, 240, 18, C_BG);
	ui_box(2, 2, 78, 14, C_PANEL, 0xFFFF);
	ui_text(6, 3, "PHOSPHOR", C_AC);
	ui_box(58, 4, 18, 10, C_PANEL, C_DIM);
	ui_text(61, 3, "DE", C_DIM);
	draw_clock();
}
static void draw_tabs(void)
{
	int i, x = 4;
	ui_fill(0, 18, 240, 16, C_BG);
	for (i = 0; i < TAB_COUNT; i++) {
		int w = strlen(tab_names[i]) * 6 + 8;
		if (i == tab) { ui_box(x, 19, w, 14, C_PON, 0xFFFF); ui_fill(x + 2, 32, w - 4, 1, C_AC); }
		ui_text(x + 4, 20, tab_names[i], i == tab ? C_TXT : C_FAINT);
		x += w + 2;
	}
	ui_text(240 - 6 * 3 - 4, 20, "L R", C_FAINT);
}
static void hints(const char *const *h, int n) { ui_hints(h, n); }

// small file-type badge, drawn (no image data)
static void badge(int x, int y, const char *t, u16 c)
{
	ui_box(x - 1, y + 1, 20, 11, c, 0xFFFF);       // filled tag, dark letters
	ui_text(x - 1 + (20 - (int)strlen(t) * 6) / 2 + 1, y, t, C_BG);
}
static void folder_icon(int x, int y, u16 c)
{
	ui_fill(x, y + 3, 7, 2, c); ui_fill(x, y + 4, 18, 7, c); ui_fill(x + 1, y + 5, 16, 5, C_BG);
}
static const char *ext_of(const char *f)
{
	static char e4[5]; const char *e = strrchr(f, '.'); int i = 0;
	if (e) for (e++; *e > ' ' && i < 4; e++) e4[i++] = *e;	// stop at CR/LF/space
	e4[i] = 0; return e4;
}
static void type_badge(int x, int y, const char *fname, int sel)
{
	const char *e = ext_of(fname);
	u16 c = sel ? C_TXT : C_DIM;
	if (!strcasecmp(e, "gba") || !strcasecmp(e, "agb") || !strcasecmp(e, "bin")) badge(x, y, "GBA", RGB5(15, 22, 31));
	else if (!strcasecmp(e, "gbc")) badge(x, y, "GBC", RGB5(28, 16, 26));
	else if (!strcasecmp(e, "gb")) badge(x, y, "GB", RGB5(18, 26, 12));
	else if (!strcasecmp(e, "sav") || !strcasecmp(e, "esv") || !strcasecmp(e, "rts")) badge(x, y, "SAV", C_GRN);
	else if (!strcasecmp(e, "nes")) badge(x, y, "NES", RGB5(28, 10, 10));
	else if (!strcasecmp(e, "gg") || !strcasecmp(e, "sms")) badge(x, y, "SEG", RGB5(10, 18, 30));
	else if (!strcasecmp(e, "cht")) badge(x, y, "CHT", C_AC);
	else badge(x, y, "...", c);
}
static void size_str(char *s, u32 sz)
{
	if (sz >= 0x100000) sprintf(s, "%lu MB", (sz + 0x80000) >> 20);
	else if (sz >= 1024) sprintf(s, "%lu KB", (sz + 512) >> 10);
	else sprintf(s, "%lu B", sz);
}

// ---------------------------------------------------------------- pop-ups (OSD-size, not full screen)
// ---------------------------------------------------------------- long names scroll
// One text at a time (the selected row or the card's file name) slides left when it doesn't fit:
// a short wait, slide to the end, hold, start over.
static struct { const char *s; int x, y, w, len, scroll, timer; u16 col, bg; } mq;
static const char *skip_chars(const char *s, int n)
{
	while (*s && n > 0) { s++; while ((((u8)*s) & 0xC0) == 0x80) s++; n--; }
	return s;
}
static void mq_clear(void) { mq.s = 0; }
static void mq_set(const char *s, int x, int y, int w, u16 col, u16 bg)
{
	mq.s = s; mq.x = x; mq.y = y; mq.w = w; mq.col = col; mq.bg = bg;
	mq.len = ui_textlen(s); mq.scroll = 0; mq.timer = 0;
}
static void mq_tick(void)
{
	int over = mq.s ? mq.len - mq.w / 6 : 0;
	if (over <= 0) return;
	int step = ++mq.timer - 45, ns = step < 0 ? 0 : step / 6;
	if (ns > over) ns = over;
	if (step / 6 > over + 10) { mq.timer = 0; ns = 0; }
	if (ns == mq.scroll) return;
	mq.scroll = ns;
	ui_fill(mq.x, mq.y, mq.w, 12, mq.bg);
	ui_textw(mq.x, mq.y, skip_chars(mq.s, ns), mq.col, mq.w);
}

void pde_popup(const char *title, const char *line, u16 edge)
{
	ui_box(20, 52, 200, 52, C_PANEL, edge);
	ui_fill(21, 53, 198, 50, C_PANEL);
	ui_textw(28, 58, title, edge == C_RED ? C_RED : C_TXT, 184);
	if (line) ui_textw(28, 74, line, C_DIM, 184);
}
void pde_progress(const char *msg)     // used by the kernel's ShowbootProgress
{
	pde_popup("Loading", msg, C_AC);
}
int pde_confirm(const char *title, const char *line)
{
	static const char *const h[] = { "A", "Yes", "B", "No" };
	pde_popup(title, line, C_AC);
	hints(h, 2);
	pde_wait_release();
	for (;;) { VBlankIntrWait(); u16 k = pde_keys_down(); if (k & KEY_A) return 1; if (k & KEY_B) return 0; }
}

// ---------------------------------------------------------------- SD list
#define ROWS 7
#define ROW_Y0 49
#define ROW_H 13
static u32 sel_sd, top_sd;
static u8 folder_sel_stack[64], folder_top_stack[64];
static int depth;

// Phosphor_DE is GBA only: the list shows folders and GBA ROMs
static int is_gba_rom(const char *f)
{
	const char *e = strrchr(f, '.');
	return e && (!strcasecmp(e, ".gba") || !strcasecmp(e, ".agb"));
}
static void read_sd_dir(void)
{
	folder_total = 0; game_total_SD = 0;
	if (f_opendir(&dir, currentpath) == FR_OK) {
		for (;;) {
			if (f_readdir(&dir, &fileinfo) != FR_OK || fileinfo.fname[0] == 0) break;
			if (fileinfo.fname[0] == '.') continue;
			if (fileinfo.fattrib & AM_HID) continue;
			if (fileinfo.fattrib & AM_DIR) {
				if (folder_total < MAX_folder) { memcpy(pFolder[folder_total].filename, fileinfo.fname, 100); pFolder[folder_total].filename[99] = 0; folder_total++; }
			} else if (game_total_SD < MAX_files && is_gba_rom(fileinfo.fname)) {
				memcpy(pFilename_buffer[game_total_SD].filename, fileinfo.fname, 100);
				pFilename_buffer[game_total_SD].filename[99] = 0;
				pFilename_buffer[game_total_SD].filesize = fileinfo.fsize;
				game_total_SD++;
			}
		}
		f_closedir(&dir);
	}
	Sort_folder(folder_total);
	Sort_file(game_total_SD);
}
static u32 sd_total(void) { return folder_total + game_total_SD; }
static const char *sd_name(u32 i) { return i < folder_total ? pFolder[i].filename : pFilename_buffer[i - folder_total].filename; }

static void draw_sd_row(u32 i, int r, int sel)
{
	int y = ROW_Y0 + r * ROW_H;
	char s[16];
	ui_fill(0, y, 240, ROW_H, C_BG);
	if (i >= sd_total()) return;
	if (sel) { ui_box(2, y, 236, 13, C_PON, 0xFFFF); ui_fill(2, y + 1, 1, 11, C_AC); }
	if (i < folder_total) {
		folder_icon(6, y, sel ? C_AC : C_DIM);
		ui_textw(28, y, sd_name(i), sel ? C_TXT : C_DIM, 204);
		if (sel) mq_set(sd_name(i), 28, y, 204, C_TXT, C_PON);
	} else {
		size_str(s, pFilename_buffer[i - folder_total].filesize);
		int w = 228 - strlen(s) * 6 - 8;
		ui_textw(8, y, sd_name(i), sel ? C_TXT : C_DIM, w);
		ui_text(236 - strlen(s) * 6, y, s, C_FAINT);
		if (sel) mq_set(sd_name(i), 8, y, w, C_TXT, C_PON);
	}
}
static void draw_sd_page(void)
{
	char s[40];
	int r;
	ui_fill(0, 34, 240, 110, C_BG);
	ui_textw(4, 36, currentpath, C_FAINT, 180);
	if (sd_total()) { sprintf(s, "%lu/%lu", sel_sd + 1, sd_total()); ui_text(236 - strlen(s) * 6, 36, s, C_FAINT); }
	else ui_text(8, 60, "This folder is empty.", C_DIM);
	for (r = 0; r < ROWS; r++) draw_sd_row(top_sd + r, r, top_sd + r == sel_sd);
	static const char *const h[] = { "A", "Open", "B", "Back", "ST", "Options", "LR", "Tabs" };
	hints(h, 4);
}
static void sd_enter(const char *name)
{
	if (depth < 63) { folder_sel_stack[depth] = sel_sd; folder_top_stack[depth] = top_sd; depth++; }
	if (strcmp(currentpath, "/")) strcat(currentpath, "/");
	strncat(currentpath, name, MAX_path_len - strlen(currentpath) - 1);
	f_chdir(currentpath);
	sel_sd = top_sd = 0;
	read_sd_dir();
}
static int sd_up(void)
{
	char *p = strrchr(currentpath, '/');
	if (!p || !strcmp(currentpath, "/")) return 0;
	if (p == currentpath) p[1] = 0; else *p = 0;
	f_chdir(currentpath);
	read_sd_dir();
	if (depth) { depth--; sel_sd = folder_sel_stack[depth]; top_sd = folder_top_stack[depth]; }
	if (sel_sd >= sd_total()) sel_sd = top_sd = 0;
	return 1;
}

// ---------------------------------------------------------------- game card
static const char *const save_names[6] = { "Auto", "SRAM 32K", "EEPROM 8K", "EEPROM 512B", "Flash 64K", "Flash 128K" };
enum { ACT_PLAY = 0, ACT_PLAY_CLEAN, ACT_CHEATS, ACT_NOR, ACT_NOR_CLEAN, ACT_COUNT };

static int any_addon(void) { return gl_reset_on | gl_rts_on | gl_sleep_on | gl_cheat_on; }

static void draw_art_or_title(const char *fname, int have_art)
{
	if (have_art) {
		DrawPic((u16 *)(pReadCache + 0x10036), 9, 23, 120, 80, 0, 0, 1);
	} else {
		ui_box(9, 23, 120, 80, C_BG, C_LINE);
		ui_text(9 + (120 - 18) / 2, 56, ext_of(fname)[0] ? "GBA" : "", C_FAINT);
	}
}

// returns 1 if the list must be re-read (e.g. after a NOR write)
static int game_card(u32 idx)
{
	TCHAR *fname = pFilename_buffer[idx - folder_total].filename;
	u32 is_EMU = Check_file_type(fname);
	if (is_EMU == 0xff) {
		pde_popup("Can't open this file", ext_of(fname), C_AC);
		pde_wait_release(); while (!(pde_keys_down() & (KEY_A | KEY_B))) VBlankIntrWait();
		return 0;
	}
	int is_gba = (is_EMU == 0);
	u8 old_save = is_gba ? Check_mde_file(fname) : 0;
	u8 save_num = old_save > 5 ? 0 : old_save;
	int sel = 0, redraw = 1, i;
	int have_art = 0;
	char s[48], title[13], code[5];
	memset(title, 0, sizeof title); memset(code, 0, sizeof code);
	if (is_gba) {
		f_chdir(currentpath);
		have_art = gl_show_Thumbnail ? Load_Thumbnail(fname) : 0;
		if (f_open(&gfile, fname, FA_READ) == FR_OK) {
			UINT br; f_lseek(&gfile, 0xA0); f_read(&gfile, title, 12, &br); f_read(&gfile, code, 4, &br); f_close(&gfile);
		}
	}
	int cht_n = is_gba ? pde_cheats_probe(fname, code) : -1;   // -1 no cheat file, else cheats on (loaded)
	static const char *const act_name[ACT_COUNT] = { "Play", "Play clean (no add-ons)", "Cheats", "Write to NOR", "Write to NOR clean" };
	int acts[ACT_COUNT], nacts = 0;
	acts[nacts++] = ACT_PLAY;
	if (is_gba) { acts[nacts++] = ACT_PLAY_CLEAN; acts[nacts++] = ACT_CHEATS; acts[nacts++] = ACT_NOR; acts[nacts++] = ACT_NOR_CLEAN; }
	pde_wait_release();
	for (;;) {
		if (redraw) {
			ui_box(4, 19, 232, 124, C_PANEL, C_AC);
			ui_fill(5, 20, 230, 122, C_PANEL);
			if (is_gba) {
				draw_art_or_title(fname, have_art);
				for (i = 0; i < 12 && title[i]; i++) if ((u8)title[i] < 32 || (u8)title[i] > 126) title[i] = ' ';
				ui_textw(134, 23, title[0] ? title : fname, C_TXT, 98);
				ui_text(134, 37, code, C_FAINT);
				size_str(s, pFilename_buffer[idx - folder_total].filesize); ui_text(170, 37, s, C_FAINT);
				ui_text(134, 51, "Save", C_FAINT); ui_textw(134 + 30, 51, save_names[save_num], C_TXT, 66);
				ui_text(134, 65, "Add-ons", C_FAINT); ui_text(134 + 48, 65, any_addon() ? "on" : "off", any_addon() ? C_AC : C_FAINT);
				ui_textw(134, 79, fname, C_DIM, 98);
				mq_clear(); mq_set(fname, 134, 79, 98, C_DIM, C_PANEL);
			} else {
				ui_textw(12, 26, fname, C_TXT, 216);
				ui_text(12, 42, is_EMU == 3 ? "Runs in PocketNES." : "Runs in Goomba Color.", C_DIM);
			}
			int y0 = is_gba ? 106 : 64;
			for (i = 0; i < nacts && y0 + i * 12 < 142; i++) {
				int y = y0 + i * 12;
				int a = acts[i];
				if (i == sel) ui_box(8, y, 224, 12, C_PON, 0xFFFF);
				ui_text(14, y, act_name[a], i == sel ? C_TXT : C_DIM);
				if (a == ACT_CHEATS) {
					char cs[20];
					if (cht_n < 0) strcpy(cs, "none found"); else if (cht_n == 0) strcpy(cs, "all off"); else sprintf(cs, "%d on", cht_n);
					ui_text(212 - ui_textlen(cs) * 6, y, cs, cht_n > 0 ? C_AC : C_FAINT);
				}
				if (a == ACT_PLAY && is_gba) { /* save type on the Play row */ }
			}
			if (is_gba && nacts > 3) {   // scroll hint
				ui_text(222, y0 + 2 * 12, sel < 3 ? "v" : "^", C_FAINT);
			}
			static const char *const h[] = { "A", "Choose", "B", "Close", "<>", "Save type" };
			hints(h, is_gba ? 3 : 2);
			redraw = 0;
		}
		VBlankIntrWait();
		mq_tick();
		u16 k = pde_keys_down();
		if (k & KEY_B) { mq_clear(); return 0; }
		if (k & KEY_UP) { sel = (sel + nacts - 1) % nacts; redraw = 1; }
		if (k & KEY_DOWN) { sel = (sel + 1) % nacts; redraw = 1; }
		if (is_gba && (k & KEY_LEFT) && save_num > 0) { save_num--; redraw = 1; }
		if (is_gba && (k & KEY_RIGHT) && save_num < 5) { save_num++; redraw = 1; }
		if (k & KEY_A) {
			int a = acts[sel];
			u32 line;
			if (a == ACT_CHEATS) {
				cht_n = pde_cheats(fname, code, title);
				f_chdir(currentpath);   // box art at pReadCache+0x10000 is untouched (cheat tables start at +0x15000)
				redraw = 1; continue;
			}
			if (a == ACT_PLAY) line = (is_gba && any_addon()) ? 1 : 0;
			else if (a == ACT_PLAY_CLEAN) line = 0;
			else if (a == ACT_NOR) line = any_addon() ? 3 : 2;
			else line = 2;
			f_chdir(currentpath);
			u8 r = Launch_file(fname, line, save_num, old_save, (kheld & KEY_L) ? 1 : 0, is_EMU);
			// only NOR writes come back here; games reset the cart
			if (r == 2) { tab = TAB_NOR; return 1; }
			return 1;
		}
	}
}

// ---------------------------------------------------------------- NOR list
static u32 sel_nor, top_nor;
static void read_nor(void)
{
	Read_NOR_info();
	gl_norOffset = 0;
	game_total_NOR = GetFileListFromNor();
}
static void draw_nor_page(void)
{
	char s[40];
	int r;
	ui_fill(0, 34, 240, 110, C_BG);
	sprintf(s, "%lu game%s on NOR", game_total_NOR, game_total_NOR == 1 ? "" : "s");
	ui_text(4, 36, s, C_FAINT);
	sprintf(s, "%lu MB free", (0x4000000 - gl_norOffset) >> 20); ui_text(236 - strlen(s) * 6, 36, s, C_FAINT);
	if (!game_total_NOR) {
		ui_text(8, 62, "No games on NOR.", C_DIM);
		ui_text(8, 76, "Write one from its game card", C_FAINT);
		ui_text(8, 88, "on the SD tab.", C_FAINT);
	}
	for (r = 0; r < ROWS; r++) {
		u32 i = top_nor + r; int y = ROW_Y0 + r * ROW_H;
		if (i >= game_total_NOR) break;
		int sel = i == sel_nor;
		if (sel) { ui_box(2, y, 236, 13, C_PON, 0xFFFF); ui_fill(2, y + 1, 1, 11, C_AC); }
		const char *n = strrchr(pNorFS[i].filename, '/'); n = n ? n + 1 : pNorFS[i].filename;
		size_str(s, pNorFS[i].filesize);
		int w = 228 - strlen(s) * 6 - 8;
		ui_textw(8, y, n, sel ? C_TXT : C_DIM, w);
		ui_text(236 - strlen(s) * 6, y, s, C_FAINT);
		if (sel) mq_set(n, 8, y, w, C_TXT, C_PON);
	}
	static const char *const h[] = { "A", "Open", "ST", "NOR Check", "SL", "Format", "LR", "Tabs" };
	hints(h, 4);
}
static int nor_card(u32 i)
{
	static const char *const items[] = { "Play", "Delete (last game only)", "Load save into FRAM", "Save FRAM to SD" };
	int sel = 0, redraw = 1, n = 4, k2;
	pde_wait_release();
	for (;;) {
		if (redraw) {
			ui_box(4, 19, 232, 124, C_PANEL, C_AC); ui_fill(5, 20, 230, 122, C_PANEL);
			const char *nm = strrchr(pNorFS[i].filename, '/'); nm = nm ? nm + 1 : pNorFS[i].filename;
			ui_textw(12, 26, nm, C_TXT, 216);
			mq_clear(); mq_set(nm, 12, 26, 216, C_TXT, C_PANEL);
			char s[32]; size_str(s, pNorFS[i].filesize); ui_text(12, 40, s, C_FAINT);
			ui_text(60, 40, "on NOR", C_FAINT);
			for (k2 = 0; k2 < n; k2++) { int y = 64 + k2 * 13; if (k2 == sel) ui_box(8, y, 224, 13, C_PON, 0xFFFF); ui_text(14, y, items[k2], k2 == sel ? C_TXT : C_DIM); }
			static const char *const h[] = { "A", "Choose", "B", "Close" };
			hints(h, 2); redraw = 0;
		}
		VBlankIntrWait();
		mq_tick();
		u16 k = pde_keys_down();
		if (k & KEY_B) { mq_clear(); return 0; }
		if (k & KEY_UP) { sel = (sel + n - 1) % n; redraw = 1; }
		if (k & KEY_DOWN) { sel = (sel + 1) % n; redraw = 1; }
		if (k & KEY_A) {
			if (sel == 0) { Boot_NOR_game(0, i, (kheld & KEY_L) ? 1 : 0); return 1; }
			if (sel == 1) {
				if (i + 1 != game_total_NOR) { pde_popup("Only the last game can be deleted", "NOR games are stored in order.", C_AC); pde_wait_release(); while (!(pde_keys_down() & (KEY_A | KEY_B))) VBlankIntrWait(); redraw = 1; continue; }
				{ const char *nm2 = strrchr(pNorFS[i].filename, '/'); nm2 = nm2 ? nm2 + 1 : pNorFS[i].filename;
				  if (pde_confirm("Delete this game from NOR?", nm2)) Block_Erase(gl_norOffset - pNorFS[i].filesize); }
				return 1;
			}
			extern u8 FRAM_save_op(u8 OP);
			extern void Show_error_num(u8);
			u8 e = FRAM_save_op(sel == 2 ? 1 : 2);
			if (e) Show_error_num(e);
			return 1;
		}
	}
}

// ---------------------------------------------------------------- Recent
static u32 sel_rc, n_rc;
static void read_recent(void) { n_rc = get_count(); if (n_rc > 10) n_rc = 10; if (sel_rc >= n_rc) sel_rc = 0; }
static void draw_recent_page(void)
{
	int r;
	ui_fill(0, 34, 240, 110, C_BG);
	ui_text(4, 36, "Recently played", C_FAINT);
	if (!n_rc) ui_text(8, 62, "Nothing played yet.", C_DIM);
	for (r = 0; r < (int)n_rc && r < ROWS; r++) {
		int y = ROW_Y0 + r * ROW_H, sel = (u32)r == sel_rc;
		if (sel) { ui_box(2, y, 236, 13, C_PON, 0xFFFF); ui_fill(2, y + 1, 1, 11, C_AC); }
		const char *n = strrchr(p_recently_play[r], '/'); n = n ? n + 1 : p_recently_play[r];
		ui_textw(8, y, n, sel ? C_TXT : C_DIM, 226);
		if (sel) mq_set(n, 8, y, 226, C_TXT, C_PON);
	}
	static const char *const h[] = { "A", "Open", "LR", "Tabs" };
	hints(h, 2);
}
// open a recent entry: go to its folder and open its game card
static int open_recent(u32 r)
{
	char path[MAX_path_len], *p = strrchr(p_recently_play[r], '/');
	const char *file;
	if (!p) return 0;
	memset(path, 0, sizeof path);
	strncpy(path, p_recently_play[r], p - p_recently_play[r]);
	if (!path[0]) strcpy(path, "/");
	file = p + 1;
	strcpy(currentpath, path); f_chdir(currentpath); depth = 0;
	read_sd_dir();
	u32 i;
	for (i = folder_total; i < sd_total(); i++) if (!strcmp(sd_name(i), file)) break;
	if (i >= sd_total()) { pde_popup("File not found", file, C_RED); pde_wait_release(); while (!(pde_keys_down() & (KEY_A | KEY_B))) VBlankIntrWait(); return 1; }
	sel_sd = i; top_sd = i >= ROWS ? i - ROWS + 1 : 0;
	tab = TAB_SD;
	game_card(i);
	return 1;
}

// ---------------------------------------------------------------- main
int main(void)
{
	irqInit();
	irqEnable(IRQ_VBLANK);
	REG_IME = 1;
	gl_currentpage = 0x8002;   // kernel mode
	SetMode(MODE_3 | BG2_ENABLE);
	SD_Disable();
	Set_RTC_status(1);
	Check_FW_update();         // Phosphor-style firmware page (only shows on an old cart firmware)
	pde_splash();
	CheckLanguage();
	CheckSwitch();
	if (f_mount(&EZcardFs, "", 1) != FR_OK) {
		ui_clear();
		pde_popup("No microSD card", "Insert the card, then power off and on.", C_RED);
		for (;;) VBlankIntrWait();
	}
	Check_save_flag();
	strcpy(currentpath, "/"); f_chdir("/");
	read_sd_dir();
	read_nor();
	if (game_total_NOR == 0) { memset(pNorFS, 0, sizeof(FM_NOR_FS) * MAX_NOR); Save_NOR_info((u16 *)pNorFS, sizeof(FM_NOR_FS) * MAX_NOR); }
	read_recent();

	int full = 1;
	u32 last_min = 0xFF;
	for (;;) {
		if (full) {
			mq_clear();
			ui_clear(); draw_top(); draw_tabs();
			if (tab == TAB_SD) draw_sd_page();
			else if (tab == TAB_NOR) draw_nor_page();
			else if (tab == TAB_RECENT) draw_recent_page();
			else pde_settings_page_draw();
			full = 0;
		}
		VBlankIntrWait();
		mq_tick();
		{ u8 t[3]; rtc_enable(); rtc_gettime(t); rtc_disenable(); if (t[1] != last_min) { last_min = t[1]; draw_clock(); } }
		u16 k = pde_keys_down();
		if (k & KEY_L) { tab = (tab + TAB_COUNT - 1) % TAB_COUNT; if (tab == TAB_NOR) read_nor(); if (tab == TAB_RECENT) read_recent(); full = 1; continue; }
		if (k & KEY_R) { tab = (tab + 1) % TAB_COUNT; if (tab == TAB_NOR) read_nor(); if (tab == TAB_RECENT) read_recent(); full = 1; continue; }
		if (tab == TAB_SD) {
			u32 n = sd_total(), old = sel_sd, oldtop = top_sd;
			if (!n) { if (k & KEY_B) { if (sd_up()) full = 1; } continue; }
			if (k & KEY_UP) sel_sd = sel_sd ? sel_sd - 1 : n - 1;
			if (k & KEY_DOWN) sel_sd = sel_sd + 1 < n ? sel_sd + 1 : 0;
			if (k & KEY_LEFT) sel_sd = sel_sd > ROWS ? sel_sd - ROWS : 0;
			if (k & KEY_RIGHT) sel_sd = sel_sd + ROWS < n ? sel_sd + ROWS : n - 1;
			if (sel_sd < top_sd) top_sd = sel_sd;
			if (sel_sd >= top_sd + ROWS) top_sd = sel_sd - ROWS + 1;
			if (sel_sd != old) {
				if (top_sd != oldtop) draw_sd_page();
				else { draw_sd_row(old, old - top_sd, 0); draw_sd_row(sel_sd, sel_sd - top_sd, 1);
					char s[24]; ui_fill(150, 36, 90, 12, C_BG); sprintf(s, "%lu/%lu", sel_sd + 1, n); ui_text(236 - strlen(s) * 6, 36, s, C_FAINT); }
			}
			if (k & KEY_B) { if (sd_up()) full = 1; }
			if (k & KEY_A) {
				if (sel_sd < folder_total) { sd_enter(pFolder[sel_sd].filename); full = 1; }
				else { if (game_card(sel_sd)) { read_sd_dir(); read_nor(); } full = 1; }
			}
			if (k & KEY_START && sel_sd >= folder_total) { pde_file_options(pFilename_buffer[sel_sd - folder_total].filename); read_sd_dir(); if (sel_sd >= sd_total()) sel_sd = top_sd = 0; full = 1; }
		} else if (tab == TAB_NOR) {
			u32 n = game_total_NOR, old = sel_nor;
			if (k & KEY_START) { NorCheck_Run(); read_nor(); sel_nor = top_nor = 0; full = 1; continue; }
			if (k & KEY_SELECT) {
				if (pde_confirm("Format NOR?", "Erases every game on NOR.")) { FormatNor(); read_nor(); }
				full = 1; continue;
			}
			if (!n) continue;
			if (k & KEY_UP) sel_nor = sel_nor ? sel_nor - 1 : n - 1;
			if (k & KEY_DOWN) sel_nor = sel_nor + 1 < n ? sel_nor + 1 : 0;
			if (sel_nor < top_nor) top_nor = sel_nor;
			if (sel_nor >= top_nor + ROWS) top_nor = sel_nor - ROWS + 1;
			if (sel_nor != old) draw_nor_page();
			if (k & KEY_A) { if (nor_card(sel_nor)) read_nor(); full = 1; }
		} else if (tab == TAB_RECENT) {
			u32 old = sel_rc;
			if (!n_rc) continue;
			if (k & KEY_UP) sel_rc = sel_rc ? sel_rc - 1 : n_rc - 1;
			if (k & KEY_DOWN) sel_rc = sel_rc + 1 < n_rc ? sel_rc + 1 : 0;
			if (sel_rc != old) draw_recent_page();
			if (k & KEY_A) { open_recent(sel_rc); full = 1; }
		} else {
			if (pde_settings_keys(k)) full = 1;
		}
	}
}
#endif

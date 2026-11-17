// Phosphor_DE: splash, firmware-update page, Settings, About, file options, cheats
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
#include "draw.h"
#include "ui.h"
#include "pde.h"

extern const unsigned char gImage_SPLASH[76800];
extern u16 SET_info_buffer[0x200];
extern TCHAR currentpath[];
extern void CheckSwitch(void);
extern u16 Read_FPGA_ver(void);
extern const char *ez_rev_name(u16 v);

static void wait_any(u16 mask)
{
	while (ui_keys()) VBlankIntrWait();
	while (!(ui_keys() & mask)) VBlankIntrWait();
	while (ui_keys()) VBlankIntrWait();
}

// ---------------------------------------------------------------- splash
void pde_splash(void)
{
	int f;
	DrawPic((u16 *)gImage_SPLASH, 0, 0, 240, 160, 0, 0, 1);
	ui_text(240 - 6 * (int)strlen("v" PDE_VERSION) - 4, 2, "v" PDE_VERSION, RGB5(12, 12, 13));
	for (f = 0; f < 70 && !ui_keys(); f++) VBlankIntrWait();   // ~1.2 s, any key skips
}

// ---------------------------------------------------------------- firmware-update page
void pde_fw_page(u16 readver, u16 cur, u16 latest, const char *rev)
{
	char s[48];
	ui_clear();
	ui_box(2, 2, 78, 14, C_PANEL, 0xFFFF); ui_text(6, 3, "PHOSPHOR", C_AC); ui_box(58, 4, 18, 10, C_PANEL, C_DIM); ui_text(61, 3, "DE", C_DIM);
	ui_box(4, 22, 232, 116, C_PANEL, C_AC);
	ui_fill(5, 23, 230, 114, C_PANEL);
	ui_text(12, 28, "Cart firmware update needed", C_AC);
	ui_text(12, 46, "Cart", C_FAINT);     ui_text(84, 46, rev, C_TXT);
	sprintf(s, "FW%u", cur);    ui_text(12, 58, "Firmware", C_FAINT); ui_text(84, 58, s, C_RED);
	sprintf(s, "(%04X)", readver); ui_text(84 + 6 * 5, 58, s, C_FAINT);
	sprintf(s, "FW%u", latest); ui_text(12, 70, "Latest", C_FAINT);   ui_text(84, 70, s, C_GRN);
	ui_text(12, 88, "Install the official EZ-Flash", C_DIM);
	ui_text(12, 100, "kernel once to update the cart,", C_DIM);
	ui_text(12, 112, "then reinstall Phosphor_DE.", C_DIM);
	static const char *const h[] = { "B", "Continue" };
	ui_hints(h, 1);
	wait_any(KEY_B);
}

// ---------------------------------------------------------------- settings
typedef struct { const char *section, *label; u16 addr; u8 nvals; const char *const *names; } setting_t;
static const char *const on_off[] = { "off", "on" };
static const char *const modeb[] = { "Rumble", "RAM", "Cart" };
static const char *const engine[] = { "Normal", "Fast" };
static const char *const reset_kind[] = { "Soft", "Hard" };
static const setting_t settings[] = {
	{ "Add-ons", "Reset to menu", assress_v_reset, 2, on_off },
	{ "Add-ons", "Savestates",    assress_v_rts,   2, on_off },
	{ "Add-ons", "Sleep",         assress_v_sleep, 2, on_off },
	{ "Add-ons", "Cheats",        assress_v_cheat, 2, on_off },
	{ "Add-ons", "Patch engine",  assress_engine_sel, 2, engine },
	{ "Saves",   "Auto save",     assress_auto_save_sel, 2, on_off },
	{ "Saves",   "Back up saves", assress_toggle_backup, 2, on_off },
	{ "Clock",   "Game clock (RTC)", assress_ingame_RTC_open_status, 2, on_off },
	{ "Display", "Box art",       assress_show_Thumbnail, 2, on_off },
	{ "Display", "Reset type",    assress_toggle_reset, 2, reset_kind },
	{ "LED",     "LED",           assress_led_open_sel, 2, on_off },
	{ "LED",     "Breathing red", assress_Breathing_R, 2, on_off },
	{ "LED",     "Breathing green", assress_Breathing_G, 2, on_off },
	{ "LED",     "Breathing blue", assress_Breathing_B, 2, on_off },
	{ "LED",     "SD activity red", assress_SD_R, 2, on_off },
	{ "LED",     "SD activity green", assress_SD_G, 2, on_off },
	{ "LED",     "SD activity blue", assress_SD_B, 2, on_off },
	{ "Mode B",  "DS mode B",     assress_ModeB_INIT, 3, modeb },
};
#define NSET (int)(sizeof settings / sizeof settings[0])
static const char *const sections[] = { "Add-ons", "Saves", "Clock", "Display", "LED", "Mode B", "About" };
#define NSEC 7
static int sec = 0, row = 0, in_rows = 0;

// the kernel's validated value for each setting (same defaults as CheckSwitch)
static u16 cur_val(const setting_t *t)
{
	u16 v = Read_SET_info(t->addr);
	if (v >= t->nvals) {
		CheckSwitch();   // defaults applied by the kernel
		switch (t->addr) {
		case assress_engine_sel: case assress_ingame_RTC_open_status: case assress_led_open_sel:
		case assress_Breathing_R: case assress_Breathing_G: case assress_Breathing_B: v = 1; break;
		case assress_ModeB_INIT: v = 2; break;
		default: v = 0; break;
		}
	}
	return v;
}
// same write path as the kernel's save_set_info_SELECT: read all, change one, save the block
static void set_val(u16 addr, u16 v)
{
	u32 a;
	for (a = 0; a < assress_max; a++) SET_info_buffer[a] = Read_SET_info(a);
	SET_info_buffer[addr] = v;
	Save_SET_info(SET_info_buffer, 0x200);
	CheckSwitch();   // reload globals and LED
}
static int rows_of(int s, int *idx)
{
	int i, n = 0;
	for (i = 0; i < NSET; i++) if (!strcmp(settings[i].section, sections[s])) idx[n++] = i;
	return n;
}
static void draw_about(void)
{
	char s[48];
	u16 v = Read_FPGA_ver();
	ui_text(78, 37, PDE_NAME, C_AC); ui_text(78 + 6 * 12, 37, "v" PDE_VERSION, C_DIM);
	ui_text(78, 49, "by f3bandit", C_TXT);
	sprintf(s, "%s FW%u", ez_rev_name(v), v & 0xFF); ui_text(78, 65, "Cart", C_FAINT); ui_textw(78 + 30, 65, s, C_TXT, 128);
	ui_text(78, 85, "Engine: EZ-Flash kernel", C_DIM);
	ui_text(78, 97, "(Apache-2.0)", C_FAINT);
	ui_text(78, 121, "ST", C_AC); ui_text(78 + 18, 121, "Dump boot chip", C_DIM);
}
void pde_settings_page_draw(void)
{
	int i, idx[NSET], n;
	char s[24];
	ui_fill(0, 34, 240, 110, C_BG);
	for (i = 0; i < NSEC; i++) {
		int y = 37 + i * 12;
		if (i == sec) { ui_box(2, y, 66, 12, in_rows ? C_PANEL : C_PON, 0xFFFF); ui_fill(2, y + 1, 1, 10, C_AC); }
		ui_text(6, y, sections[i], i == sec ? C_TXT : C_FAINT);
	}
	ui_fill(71, 36, 1, 106, C_LINE);
	if (!strcmp(sections[sec], "About")) { draw_about(); }
	else {
		n = rows_of(sec, idx);
		for (i = 0; i < n && i < 8; i++) {
			const setting_t *t = &settings[idx[i]];
			int y = 37 + i * 13;
			u16 v = cur_val(t);
			if (in_rows && i == row) ui_box(74, y, 163, 13, C_PON, 0xFFFF);
			ui_textw(78, y, t->label, (in_rows && i == row) ? C_TXT : C_DIM, 110);
			strcpy(s, t->names[v]);
			if (t->names == on_off) {
				if (v) { ui_box(208, y + 1, 26, 11, C_AC, 0xFFFF); ui_text(215, y, "ON", C_BG); }
				else { ui_box(208, y + 1, 26, 11, C_BG, C_FAINT); ui_text(212, y, "off", C_FAINT); }
			} else ui_text(234 - (int)strlen(s) * 6, y, s, C_AC);
		}
	}
	static const char *const h1[] = { "A", "Open", "+", "Section", "LR", "Tabs" };
	static const char *const h2[] = { "A", "Change", "<>", "Change", "B", "Back" };
	if (in_rows) ui_hints(h2, 3); else ui_hints(h1, 3);
}
int pde_settings_keys(u16 k)
{
	int idx[NSET], n = rows_of(sec, idx);
	if (!in_rows) {
		if ((k & KEY_START) && !strcmp(sections[sec], "About")) { pde_dump_bootchip(); return 1; }
		if (k & KEY_UP) { sec = (sec + NSEC - 1) % NSEC; return 1; }
		if (k & KEY_DOWN) { sec = (sec + 1) % NSEC; return 1; }
		if ((k & (KEY_A | KEY_RIGHT)) && n) { in_rows = 1; row = 0; return 1; }
		return 0;
	}
	if (k & KEY_B) { in_rows = 0; return 1; }
	if (k & KEY_UP) { row = (row + n - 1) % n; return 1; }
	if (k & KEY_DOWN) { row = (row + 1) % n; return 1; }
	if (k & (KEY_A | KEY_RIGHT | KEY_LEFT)) {
		const setting_t *t = &settings[idx[row]];
		u16 v = cur_val(t);
		v = (k & KEY_LEFT) ? (v + t->nvals - 1) % t->nvals : (v + 1) % t->nvals;
		pde_popup("Saving setting...", t->label, C_AC);
		set_val(t->addr, v);
		return 1;
	}
	return 0;
}

// ---------------------------------------------------------------- file options (START on a file)
void pde_file_options(TCHAR *file)
{
	static const char *const h[] = { "A", "Choose", "B", "Close" };
	int sel = 0, redraw = 1;
	while (ui_keys()) VBlankIntrWait();
	for (;;) {
		if (redraw) {
			ui_box(20, 44, 200, 66, C_PANEL, C_AC); ui_fill(21, 45, 198, 64, C_PANEL);
			ui_textw(28, 49, file, C_TXT, 184);
			if (sel == 0) ui_box(24, 68, 192, 13, C_PON, 0xFFFF);
			ui_text(30, 68, "Delete this file", sel == 0 ? C_RED : C_DIM);
			if (sel == 1) ui_box(24, 83, 192, 13, C_PON, 0xFFFF);
			ui_text(30, 83, "Cancel", sel == 1 ? C_TXT : C_DIM);
			ui_hints(h, 2); redraw = 0;
		}
		VBlankIntrWait();
		u16 k = ui_keys(); static u16 p; u16 d = k & ~p; p = k;
		if (d & KEY_B) return;
		if (d & (KEY_UP | KEY_DOWN)) { sel ^= 1; redraw = 1; }
		if (d & KEY_A) {
			if (sel == 1) return;
			pde_popup("Delete this file?", "A: delete   B: keep", C_RED);
			while (ui_keys()) VBlankIntrWait();
			for (;;) {
				VBlankIntrWait(); u16 k2 = ui_keys();
				if (k2 & KEY_B) return;
				if (k2 & KEY_A) { f_chdir(currentpath); f_unlink(file); while (ui_keys()) VBlankIntrWait(); return; }
			}
		}
	}
}
#endif

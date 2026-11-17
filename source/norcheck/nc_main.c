// NOR Check for the EZ-Flash Omega Definitive Edition
// Tests the 64 MB game NOR for dead cells. Built into Phosphor_DE.
// Never writes the S71 boot chip.
// by f3bandit. Flash/SD/reset routines ported from the EZ-Flash Omega DE kernel (Apache-2.0, (c) EZ-Flash).
#include <stdio.h>
#include <string.h>
#include <gba_base.h>
#include <gba_input.h>
#include <gba_interrupt.h>
#include "ezhw.h"
#include "nortest.h"
#include "ui.h"

#define K_A 0x001
#define K_B 0x002
#define K_SEL 0x004
#define K_START 0x008
#define K_RIGHT 0x010
#define K_LEFT 0x020
#define K_UP 0x040
#define K_DOWN 0x080

#include "report.h"

// ------------------------------------------------------------- keys
static u16 held, prev;
static u16 keys_down(void) { prev = held; held = ui_keys(); return held & ~prev; }
static void wait_release(void) { while (ui_keys()) ui_vsync(); held = prev = 0; }

void fmt_time(char *s, u32 ticks)
{
	u32 sec = ticks / 65536;
	sprintf(s, "%lu:%02lu", sec / 60, sec % 60);
}

// ------------------------------------------------------------- screens
static void kv(int y, const char *k, const char *v, u16 c) { ui_text(8, y, k, C_FAINT); ui_text(96, y, v, c); }

static void screen_not_found(void)
{
	char s[40];
	ui_clear(); ui_top("NOR CHECK", NULL);
	ui_box(4, 19, 232, 13, C_PANEL, C_RED); ui_text(8, 19, "No Omega DE game NOR found", C_RED);
	sprintf(s, "NOR ID %04X (expected %04X)", nor_id, S98_ID);
	ui_text(8, 40, s, C_DIM);
	ui_text(8, 58, "This tool is for the EZ-Flash", C_FAINT);
	ui_text(8, 70, "Omega Definitive Edition.", C_FAINT);
	static const char *const h[] = { "B", "Back to menu" };
	ui_hints(h, 1);
	wait_release();
	for (;;) { ui_vsync(); if (keys_down() & K_B) break; }
	wait_release();
}

static int screen_home(void)
{
	#ifdef IN_KERNEL
	static const char *const items[] = { "Read check", "Quick test", "Full test", "Back" };
#else
	static const char *const items[] = { "Read check", "Quick test", "Full test", "Back to menu" };
#endif
	static const char *const desc[] = { "Keeps your NOR games.", "Erases NOR games. 1 write pass.",
	                                    "Erases NOR games. 2 write passes.", "Leave NOR Check." };
	int sel = 0, redraw = 1, i;
	char s[48];
	wait_release();
	for (;;) {
		if (redraw) {
			ui_clear(); ui_top("NOR CHECK", "v" VERSION);
			ui_box(4, 19, 232, 13, C_PANEL, 0xFFFF); ui_text(8, 19, "Omega DE found", C_GRN);
			sprintf(s, "%s FW%u", ez_rev_name(fpga_ver), fpga_ver & 0xFF); kv(36, "Cart", s, C_TXT);
			sprintf(s, "%d on NOR", ngames); kv(48, "NOR games", s, C_TXT);
			kv(60, "Boot chip", "never written", C_AC);
			for (i = 0; i < 4; i++) {
				int y = 76 + i * 13;
				if (i == sel) ui_row_sel(y);
				ui_text(8, y, items[i], i == sel ? C_TXT : C_DIM);
			}
			ui_text(8, 130, desc[sel], i == 3 ? C_FAINT : (sel == 1 || sel == 2) ? C_RED : C_FAINT);
			static const char *const h[] = { "A", "Choose", "+", "Move" };
			ui_hints(h, 2);
			redraw = 0;
		}
		ui_vsync();
		u16 k = keys_down();
		if (k & K_UP) { sel = (sel + 3) % 4; redraw = 1; }
		if (k & K_DOWN) { sel = (sel + 1) % 4; redraw = 1; }
		if (k & K_A) return sel;
	}
}

// hold A for 3 seconds to start a test that erases the NOR
static int screen_confirm(int mode)
{
	u32 hold = 0;
	char s[48];
	wait_release();
	ui_box(14, 26, 212, 108, C_PANEL, C_RED);
	ui_text(22, 31, mode == MODE_FULL ? "Full test erases ALL" : "Quick test erases ALL", C_RED);
	ui_text(22, 43, "NOR games on this cart.", C_TXT);
	sprintf(s, "%d game%s will be gone.", ngames, ngames == 1 ? "" : "s");
	ui_text(22, 59, s, C_DIM);
	ui_text(22, 71, "Keep the console powered.", C_DIM);
	ui_text(22, 95, "Hold A for 3 sec to start", C_AC);
	ui_fill(22, 110, 196, 8, C_LINE);
	static const char *const h[] = { "A", "Hold to start", "B", "Cancel" };
	ui_hints(h, 2);
	for (;;) {
		ui_vsync();
		u16 k = ui_keys();
		if (k & K_B) return 0;
		if (k & K_A) hold++; else hold = 0;
		ui_fill(22, 110, 196, 8, C_LINE);
		ui_fill(22, 110, hold * 196 / 180, 8, C_AC);
		if (hold >= 180) return 1;
	}
}

static void draw_map(void)
{
	int i, first_pass = T.passes[0] == P_UNLOCK ? 1 : 0;
	for (i = 0; i < NOR_BLOCKS; i++) {
		int x = 8 + (i % 32) * 7, y = 36 + (i / 32) * 5;
		u16 c = C_LINE;
		int done = T.finished || i < T.block;
		int earlier = T.pass > first_pass;
		if (T.flags[i]) c = C_RED;
		else if (done) c = C_GRN;
		else if (earlier) c = RGB5(5, 12, 8);
		if (!T.finished && i == T.block) c = C_AC;
		ui_fill(x, y, 6, 4, c);
	}
}

static void draw_run_status(int paused)
{
	char s[64], t1[12], t2[12];
	u32 el = hw_ticks() - T.t_start;
	u32 pct = T.units_total ? T.units_done * 100 / T.units_total : 0;
	int npass = 0, ip = 0, i;
	for (i = 0; T.passes[i] != P_END; i++) if (T.passes[i] != P_UNLOCK) { npass++; if (i <= T.pass) ip++; }
	ui_box(4, 19, 232, 13, C_PANEL, 0xFFFF);
	sprintf(s, "Pass %d/%d: %s", ip, npass, pass_name(cur_pass()));
	ui_textw(8, 19, s, paused ? C_AC : C_TXT, 224);
	ui_fill(0, 117, 240, 26, C_BG);
	sprintf(s, "%lu%%", pct); ui_text(8, 118, s, C_AC);
	fmt_time(t1, el);
	if (T.units_done > T.units_total / 50) fmt_time(t2, (u32)((unsigned long long)el * (T.units_total - T.units_done) / T.units_done));
	else strcpy(t2, "--:--");
	sprintf(s, "%s  left %s", t1, t2); ui_text(44, 118, s, C_DIM);
	int bad = test_bad_count();
	sprintf(s, "bad %d", bad); ui_text(236 - strlen(s) * 6, 118, s, bad ? C_RED : C_FAINT);
	ui_fill(8, 132, 224, 6, C_LINE);
	ui_fill(8, 132, pct * 224 / 100 > 4 ? pct * 224 / 100 : 4, 6, C_AC);
}

static void screen_run(int mode)
{
	static const char *const mname[] = { "Read check", "Quick test", "Full test" };
	static const char *const h[] = { "ST", "Pause", "B", "Stop" };
	static const char *const hp[] = { "ST", "Resume", "B", "Stop" };
	int paused = 0;
	wait_release();
	ui_clear(); ui_top("NOR CHECK", mname[mode]);
	ui_hints(h, 2);
	test_begin(mode);
	draw_run_status(0); draw_map();
	for (;;) {
		u16 k = keys_down();
		if (k & K_START) { paused = !paused; ui_hints(paused ? hp : h, 2); draw_run_status(paused); }
		if (k & K_B) {
			ui_box(30, 50, 180, 50, C_PANEL, C_AC);
			ui_text(38, 56, "Stop the test?", C_TXT);
			ui_text(38, 70, mode == MODE_READ ? "Nothing has been changed." : "NOR is left part-erased.", C_DIM);
			static const char *const hs[] = { "A", "Stop", "B", "Keep going" };
			ui_hints(hs, 2);
			wait_release();
			for (;;) { ui_vsync(); u16 kk = keys_down(); if (kk & K_A) { T.stopped = 1; nor_reset(); return; } if (kk & K_B) break; }
			wait_release();
			ui_clear(); ui_top("NOR CHECK", mname[mode]); ui_hints(paused ? hp : h, 2);
			draw_run_status(paused); draw_map();
		}
		if (paused) { ui_vsync(); continue; }
		if (!test_step()) break;
		draw_run_status(0);
		draw_map();
	}
}

static void screen_results(void)
{
#ifdef IN_KERNEL
	int *list = NC_LIST;
#else
	static int list[NOR_BLOCKS] EWRAM_BSS;
#endif
	int bad = test_bad_count(), n = 0, i, sel = 0, top = 0, redraw = 1;
	char s[64];
	int saved = save_report();
	for (i = 0; i < NOR_BLOCKS; i++) if (T.flags[i]) list[n++] = i;
	wait_release();
	for (;;) {
		if (redraw) {
			ui_clear(); ui_top("NOR CHECK", T.stopped ? "stopped" : "done");
			if (bad) { sprintf(s, "%d bad block%s found", bad, bad == 1 ? "" : "s"); ui_box(4, 19, 232, 13, C_PANEL, C_RED); ui_text(8, 19, s, C_RED); }
			else { ui_box(4, 19, 232, 13, C_PANEL, C_GRN); ui_text(8, 19, T.stopped ? "No bad blocks so far" : "No bad blocks found", C_GRN); }
			if (n) {
				ui_text(8, 36, "Address", C_FAINT); ui_text(66, 36, "Fault", C_FAINT); ui_text(144, 36, "Bits", C_FAINT);
				for (i = 0; i < 5 && top + i < n; i++) {
					int b = list[top + i], y = 49 + i * 13; u8 f = T.flags[b];
					if (top + i == sel) ui_row_sel(y);
					sprintf(s, "%07lX", (u32)b * NOR_BLOCK); ui_text(8, y, s, C_TXT);
					ui_text(66, y, f & F_ERASE_TO ? "no erase" : f & F_PROG_TO ? "no write" : f & F_STUCK0 ? "stuck 0" : f & F_STUCK1 ? "stuck 1" : "unstable", C_RED);
					if ((f & (F_STUCK0 | F_STUCK1)) && !(f & (F_ERASE_TO | F_PROG_TO))) { sprintf(s, "%04X", T.stuck0[b] | T.stuck1[b]); ui_text(144, y, s, C_DIM); }
				}
				int g = game_at_block(list[sel]);
				if (g >= 0) { ui_text(8, 115, "Was in:", C_FAINT); ui_textw(56, 115, base_name(games[g].filename), C_DIM, 176); }
			} else {
				ui_text(8, 44, T.mode == MODE_READ ? "Every block read the same twice." : "Every block erased and held", C_DIM);
				if (T.mode != MODE_READ) ui_text(8, 56, "its test pattern.", C_DIM);
			}
			if (T.mode != MODE_READ && !n) ui_text(8, 104, T.stopped ? "NOR is left part-erased." : "NOR is now empty.", C_FAINT);
			ui_text(8, 128, saved ? "Report saved: NORCHECK.TXT" : "Report not saved (SD error)", saved ? C_GRN : C_RED);
			static const char *const h[] = { "+", "Scroll", "A", "Menu", "SL", "Save again" };
			ui_hints(h, 3);
			redraw = 0;
		}
		ui_vsync();
		u16 k = keys_down();
		if ((k & K_DOWN) && sel < n - 1) { sel++; if (sel >= top + 5) top++; redraw = 1; }
		if ((k & K_UP) && sel > 0) { sel--; if (sel < top) top--; redraw = 1; }
		if (k & K_SEL) { saved = save_report(); redraw = 1; }
		if (k & K_A) return;
	}
}

static void nc_loop(void)
{
	load_games();
	for (;;) {
		int m = screen_home();
		if (m == 3) return;
		if (m != MODE_READ && !screen_confirm(m)) continue;
		screen_run(m);
		screen_results();
		if (m != MODE_READ) ngames = 0;   // NOR games are gone (the menu clears its list on next boot)
	}
}

#ifndef IN_KERNEL
int main(void)
{
	REG_IME = 0;            // no interrupts: nothing may run from the cart's ROM area
	hw_timer_start();
	ui_init();
	if (ez_find_os_mode(&nor_id) < 0) { screen_not_found(); ez_return_to_menu(); }
	fpga_ver = ez_fpga_ver();
	nc_loop();
	ez_return_to_menu();
	return 0;
}
#else
// Phosphor_DE: called from the NOR page (START). The kernel is already in OS mode.
void NorCheck_Run(void)
{
	hw_timer_start();
	ez_os_mode();
	nor_id = nor_read_id();
	fpga_ver = ez_fpga_ver();
	if (nor_id != S98_ID) screen_not_found();
	else nc_loop();
	nor_reset();
	ez_set_rompage(KERNEL_PAGE);   // back to the kernel's own page
	wait_release();
}
#endif

// Phosphor_DE boot chip dump - by f3bandit
// Copies the S71 boot chip to the SD card so its contents can be inspected.
// READ ONLY: this file sends no erase or program commands to any flash chip. It only switches the
// ROM page the way the kernel itself does (SetRompage) and reads.
//   /SYSTEM/DUMP/BOOT8000.BIN  8 MB, page 0x8000 (the loader's view: the chip from its start)
//   /SYSTEM/DUMP/BOOT8002.BIN  8 MB, page 0x8002 (the kernel's own view)
#ifdef PDE_UI
#include <gba_base.h>
#include <gba_interrupt.h>
#include <gba_input.h>
#include <gba_systemcalls.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "ezkernel.h"
#include "Ezcard_OP.h"
#include "ui.h"
#include "pde.h"

extern FIL gfile;

#ifndef DUMP_SIZE
#define DUMP_SIZE  0x800000
#endif
#define CHIP_SIZE  DUMP_SIZE
#define CHUNK      0x8000

// runs from IWRAM with interrupts off: the ROM the kernel runs from is swapped out while it copies
static void IWRAM_CODE __attribute__((noinline)) copy_paged(u16 page, u32 off, u16 *dst, u32 halfwords)
{
	u16 ime = REG_IME;
	REG_IME = 0;
	SetRompage(page);
	vu16 *src = (vu16 *)(0x08000000 + off);
	for (u32 i = 0; i < halfwords; i++) dst[i] = src[i];
	SetRompage(0x8002);
	REG_IME = ime;
}

static int dump_one(const char *path, u16 page, const char *label, int part)
{
	UINT bw;
	char s[48];
	u16 *buf = (u16 *)pReadCache;
	if (f_open(&gfile, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return 0;
	for (u32 off = 0; off < CHIP_SIZE; off += CHUNK) {
		if (page == 0x8002) { vu16 *src = (vu16 *)(0x08000000 + off); for (u32 i = 0; i < CHUNK / 2; i++) buf[i] = src[i]; }
		else copy_paged(page, off, buf, CHUNK / 2);
		if (f_write(&gfile, buf, CHUNK, &bw) != FR_OK || bw != CHUNK) { f_close(&gfile); return 0; }
		if ((off & 0x3FFFF) == 0) {
			int pct = (off >> 10) * 100 / (CHIP_SIZE >> 10);
			ui_fill(30, 84, 180, 8, C_BG);
			ui_fill(30, 84, 180 * (off + CHUNK) / CHIP_SIZE, 8, C_AC);
			sprintf(s, "%s  %d%%  (%d of 2)", label, pct, part);
			ui_fill(28, 96, 186, 12, C_PANEL); ui_text(30, 96, s, C_DIM);
		}
	}
	f_close(&gfile);
	return 1;
}

void pde_dump_bootchip(void)
{
	if (!pde_confirm("Dump boot chip to SD?", "Read only. Needs 16 MB free.")) return;
	ui_box(24, 52, 192, 62, C_PANEL, C_AC);
	ui_text(30, 58, "Dumping boot chip", C_AC);
	ui_text(30, 70, "Don't power off.", C_FAINT);
	f_mkdir("/SYSTEM");
	f_mkdir("/SYSTEM/DUMP");
	int ok = dump_one("/SYSTEM/DUMP/BOOT8000.BIN", 0x8000, "Loader view", 1)
	      && dump_one("/SYSTEM/DUMP/BOOT8002.BIN", 0x8002, "Kernel view", 2);
	pde_popup(ok ? "Dump done" : "Dump failed (SD full?)", ok ? "Saved in /SYSTEM/DUMP" : "The cart was not changed.", ok ? C_AC : C_RED);
	pde_wait_release();
	while (!(pde_keys_down() & (KEY_A | KEY_B))) VBlankIntrWait();
}
#endif

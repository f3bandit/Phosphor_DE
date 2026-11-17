// NOR Check - NOR game list (read only) and the SD report
#include <stdio.h>
#include <string.h>
#include "report.h"
#include "ff.h"

_Static_assert(sizeof(nor_fs_t) == 132, "kernel FM_NOR_FS layout");
#ifdef IN_KERNEL
_Static_assert(sizeof(test_t) <= 0x2000, "T fits its scratch slot");
_Static_assert(sizeof(nor_fs_t) * 64 <= 0x3000, "games fit their scratch slot");
#endif

// ------------------------------------------------------------- NOR game list (read from the boot chip, read only)
#ifndef IN_KERNEL
nor_fs_t games[64] EWRAM_BSS;
#endif
int ngames;
u16 nor_id;
u16 fpga_ver;

void load_games(void)
{
	int i;
	memset(games, 0, sizeof(nor_fs_t) * 64);
	nor_read_info((u16 *)games, sizeof(nor_fs_t) * 64 / 2);
	for (ngames = 0; ngames < 64; ngames++) {
		nor_fs_t *g = &games[ngames];
		if (g->filesize == 0 || g->filesize > NOR_SIZE || g->filename[0] != '/' ||
		    (u32)g->rompage * NOR_BLOCK + g->filesize > NOR_SIZE) break;
		for (i = 0; i < 99 && g->filename[i]; i++) if ((u8)g->filename[i] < 32) break;
		if (i < 99 && g->filename[i]) break;
		g->filename[99] = 0;
	}
}
const char *base_name(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }
int game_at_block(int b)
{
	int i;
	for (i = 0; i < ngames; i++) {
		u32 s = games[i].rompage, n = (games[i].filesize + NOR_BLOCK - 1) / NOR_BLOCK;
		if ((u32)b >= s && (u32)b < s + n) return i;
	}
	return -1;
}

// ------------------------------------------------------------- report
#ifndef IN_KERNEL
static FATFS fs EWRAM_BSS;
#endif
#ifdef IN_KERNEL
#define rf (*(FIL *)NC_FIL)
_Static_assert(sizeof(FIL) <= 0x1000, "FIL fits its scratch slot");
#else
static FIL rf EWRAM_BSS;
#endif
static char line[160];
static int wr(const char *s) { UINT bw; return f_write(&rf, s, strlen(s), &bw) == FR_OK && bw == strlen(s); }

int save_report(void)
{
	static const char *const mname[] = { "Read check", "Quick test", "Full test" };
	int i, ok = 1;
	char t1[12];
#ifndef IN_KERNEL
	if (f_mount(&fs, "", 1) != FR_OK) return 0;
	if (f_open(&rf, "/NORCHECK.TXT", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) { f_mount(0, "", 0); return 0; }
#else
	if (f_open(&rf, "/NORCHECK.TXT", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return 0;   // kernel's SD mount
#endif
	fmt_time(t1, hw_ticks() - T.t_start);
	sprintf(line, "NOR Check " VERSION " for EZ-Flash Omega DE\r\n"); ok &= wr(line);
	sprintf(line, "Test: %s%s   Time: %s\r\n", mname[T.mode], T.stopped ? " (stopped early)" : "", t1); ok &= wr(line);
	sprintf(line, "NOR ID: %04X   Size: 64 MB   Blocks: %d x 128 KB\r\n", nor_id, NOR_BLOCKS); ok &= wr(line);
	sprintf(line, "Cart: %s, firmware %u (FPGA version word %04X)\r\n", ez_rev_name(fpga_ver), fpga_ver & 0xFF, fpga_ver); ok &= wr(line);
	sprintf(line, "Bad blocks: %d\r\n\r\n", test_bad_count()); ok &= wr(line);
	for (i = 0; i < NOR_BLOCKS; i++) {
		if (!T.flags[i]) continue;
		u8 f = T.flags[i];
		sprintf(line, "Block %3d  0x%07lX", i, (u32)i * NOR_BLOCK); ok &= wr(line);
		if (f & F_ERASE_TO) ok &= wr("  erase-timeout");
		if (f & F_PROG_TO)  ok &= wr("  write-timeout");
		if (f & F_STUCK0) { sprintf(line, "  stuck-0 bits %04X", T.stuck0[i]); ok &= wr(line); }
		if (f & F_STUCK1) { sprintf(line, "  stuck-1 bits %04X", T.stuck1[i]); ok &= wr(line); }
		if (f & F_UNSTABLE) ok &= wr("  unstable-read");
		if (T.badwords[i]) { sprintf(line, "  bad words %lu", T.badwords[i]); ok &= wr(line); }
		if (T.firstbad[i] != 0xFFFFFFFF) { sprintf(line, "  first 0x%07lX", T.firstbad[i]); ok &= wr(line); }
		int g = game_at_block(i);
		if (g >= 0) { sprintf(line, "  game: %.60s", base_name(games[g].filename)); ok &= wr(line); }
		ok &= wr("\r\n");
	}
	if (ngames) {
		ok &= wr("\r\nNOR game list before the test:\r\n");
		for (i = 0; i < ngames; i++) {
			sprintf(line, "  blocks %3d-%3d  %.80s\r\n", games[i].rompage,
			        games[i].rompage + (int)((games[i].filesize + NOR_BLOCK - 1) / NOR_BLOCK) - 1, games[i].filename);
			ok &= wr(line);
		}
	}
	if (f_close(&rf) != FR_OK) ok = 0;
#ifndef IN_KERNEL
	f_mount(0, "", 0);
#endif
	return ok;
}


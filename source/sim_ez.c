// Phosphor_DE emulator build (make SIM=1): stands in for the cart hardware so the
// kernel can run in mGBA. Never part of a real build.
#ifdef SIM_EZ
#include <string.h>
#include <stdio.h>
#include <gba_base.h>
#include "ez_define.h"
#include "ezkernel.h"
#include "draw.h"

extern const u8 sim_disk[];          // FAT image of a test SD card (sim_disk.s)
extern const u32 sim_disk_size;
static u16 sim_set[0x200];
static int sim_set_init;

u32 Read_SD_sectors(u32 address, u16 count, u8 *buf)
{
	if ((address + count) * 512 > sim_disk_size) { memset(buf, 0, count * 512); return 0; }
	memcpy(buf, sim_disk + address * 512, count * 512);
	return 0;
}
u32 Write_SD_sectors(u32 address, u16 count, const u8 *buf) { (void)address; (void)count; (void)buf; return 0; }  // card is read-only here
void Send_FATbuffer(u32 *buffer, u32 mode) { (void)buffer; (void)mode; }
void Save_info(u32 off, u16 *b, u32 n) { (void)off; (void)b; (void)n; }
void Read_NOR_info(void) {}
u16 Read_SET_info(u32 offset)
{
	if (!sim_set_init) {
		memset(sim_set, 0xFF, sizeof sim_set);
		sim_set[assress_language] = 0xE1E1;
		sim_set[assress_show_Thumbnail] = 1;
		sim_set[assress_v_reset] = 1; sim_set[assress_v_rts] = 1; sim_set[assress_v_sleep] = 0; sim_set[assress_v_cheat] = 1;
		sim_set_init = 1;
	}
	return offset < 0x200 ? sim_set[offset] : 0xFFFF;
}
u16 Read_sav_info(u32 offset) { (void)offset; return 0; }
#ifndef SIM_FWVER
#define SIM_FWVER 0xB007
#endif
u16 Read_FPGA_ver(void) { return SIM_FWVER; }
int rtc_get(u8 *d) { static const u8 t[7] = { 0x26, 0x11, 0x17, 0x02, 0x21, 0x31, 0x00 }; memcpy(d, t, 7); return 0; }
int rtc_gettime(u8 *d) { d[0] = 0x21; d[1] = 0x31; d[2] = 0x00; return 0; }
void rtc_set(u8 *d) { (void)d; }
u32 GetFileListFromNor(void) { return 0; }
u32 Loadfile2NOR(TCHAR *f, u32 a, u16 p, u8 s) { (void)f; (void)a; (void)p; (void)s; return 0; }
u32 Block_Erase(u32 a) { (void)a; return 0; }
u32 WriteFlash_with32word(u32 a, u8 *b, u32 s) { (void)a; (void)b; (void)s; return 0; }
extern void sim_launch_screen(u16 page, u32 bootmode);
void SetRompageWithHardReset(u16 page, u32 bootmode) { sim_launch_screen(page, bootmode); for (;;) ; }
__attribute__((weak)) void sim_launch_screen(u16 page, u32 bootmode)
{
	char m[48];
	Clear(0, 60, 240, 40, 0, 1);
	sprintf(m, "SIM: game boots here (page %X, mode %lu)", page, bootmode);
	DrawHZText12(m, 0, 4, 74, 0x7FFF, 1);
}
#endif

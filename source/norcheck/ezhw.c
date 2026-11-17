// NOR Check - EZ-Flash Omega DE hardware access
// Register sequences ported from the EZ-Flash Omega DE kernel (Ezcard_OP.c,
// NORflash_OP.c), Apache-2.0, (c) EZ-Flash. Timeouts added for NOR Check.
//
// SAFETY: this file only ever issues flash commands inside the S98 game NOR
// window (0x09000000). The S71 boot chip (bootloader, kernel, firmware,
// settings) is only ever READ (nor_read_info), never sent a command.
#include <gba_base.h>
#include <gba_timers.h>
#include <gba_dma.h>
#include <gba_interrupt.h>
#include "ezhw.h"

#define FlashBase_S98   ((u32)0x09000000)
#define FlashBase_S71   ((u32)0x08000000)
#define NOR_info_offset 0x7A0000
#define WINDOW          0x800000u

static u16 cur_page = 0;

// ---------------------------------------------------------------- timers
void hw_timer_start(void)
{
	REG_TM2CNT_H = 0; REG_TM3CNT_H = 0;
	REG_TM2CNT_L = 0; REG_TM3CNT_L = 0;
	REG_TM3CNT_H = TIMER_START | TIMER_COUNT;      // cascade
	REG_TM2CNT_H = TIMER_START | 2;                // /256 -> 65536 Hz
}
// 16.78 MHz / 256 = 65536 Hz on TM2; TM3 counts TM2 overflows.
IWRAM_CODE u32 hw_ticks(void)
{
	u16 hi, lo, hi2;
	do { hi = REG_TM3CNT_L; lo = REG_TM2CNT_L; hi2 = REG_TM3CNT_L; } while (hi != hi2);
	return ((u32)hi << 16) | lo;                   // 65536 ticks per second
}
IWRAM_CODE void hw_delay_us(u32 us)
{
	// 1 tick = 15.26 us; always wait at least 2 ticks so short delays are never 0
	u32 t = (us + 15) / 15 + 1;
	u32 s = hw_ticks();
	while (hw_ticks() - s < t) ;
}
#define TICKS_PER_SEC 65536u

#if defined(IN_KERNEL)
// ---------------------------------------------------------------- inside the Phosphor_DE kernel
// NOR Check uses the kernel's own NOR functions (the ones that write games to NOR),
// so it runs exactly the code that already works on the cart, in OS mode.
extern u32 gl_currentpage;
extern void SetRompage(u16 page);
extern void Chip_Reset(void);
extern u16  Read_S98NOR_ID(void);
extern u32  Block_Erase(u32 blockAdd);
extern u32  WriteFlash_with32word(u32 address, u8 *buffer, u32 size);
extern void Unlock_NOR_PPB(void);
void ez_set_rompage(u16 page) { SetRompage(page); cur_page = page; }
void ez_os_mode(void) { ez_set_rompage((u16)gl_currentpage); }
vu16 *nor_ptr(u32 addr)
{
	u16 page = (u16)gl_currentpage + (u16)((addr / WINDOW) * 0x1000);   // same paging as Block_Erase
	if (page != cur_page) ez_set_rompage(page);
	return (vu16 *)(FlashBase_S98 + (addr % WINDOW));
}
void nor_reset(void) { Chip_Reset(); }
u16  nor_read_id(void) { u16 id; ez_os_mode(); id = Read_S98NOR_ID(); Chip_Reset(); return id; }
void nor_unlock_ppb(void) { ez_os_mode(); Unlock_NOR_PPB(); Chip_Reset(); }
int  nor_erase_block(u32 a) { u32 r = Block_Erase(a); cur_page = (u16)gl_currentpage; return r ? FL_TIMEOUT : FL_OK; }
int  nor_program32(u32 a, const u16 *w) { u32 r = WriteFlash_with32word(a, (u8 *)w, 32); cur_page = (u16)gl_currentpage; return r ? FL_TIMEOUT : FL_OK; }
extern u16 Read_FPGA_ver(void);
u16  ez_fpga_ver(void) { return Read_FPGA_ver(); }
void nor_read_info(u16 *dst, u32 halfwords)   // read only, same reads as the kernel's Read_NOR_info
{
	u32 i;
	ez_os_mode();
	for (i = 0; i < halfwords; i++) dst[i] = *((vu16 *)(FlashBase_S71 + NOR_info_offset + i*2));
}
#endif

#if !defined(SIM) && !defined(IN_KERNEL)
// ---------------------------------------------------------------- cart registers (kernel SetRompage)
#ifndef IN_KERNEL
IWRAM_CODE static void setpage_iwram(u16 page)
{
	*(vu16 *)0x9fe0000 = 0xd200;
	*(vu16 *)0x8000000 = 0x1500;
	*(vu16 *)0x8020000 = 0xd200;
	*(vu16 *)0x8040000 = 0x1500;
	*(vu16 *)0x9880000 = page;//C4
	*(vu16 *)0x9fc0000 = 0x1500;
}
// same writes from EWRAM, Thumb: how the kernel's in-game reset add-on and ezfo-disc_io do it
__attribute__((noinline, target("thumb"))) static void setpage_ewram(u16 page)
{
	*(vu16 *)0x9fe0000 = 0xd200;
	*(vu16 *)0x8000000 = 0x1500;
	*(vu16 *)0x8020000 = 0xd200;
	*(vu16 *)0x8040000 = 0x1500;
	*(vu16 *)0x9880000 = page;//C4
	*(vu16 *)0x9fc0000 = 0x1500;
}
__attribute__((noinline, target("thumb"))) static void slow(void) { volatile int i; for (i = 0; i < 64; i++) ; }
__attribute__((noinline, target("thumb"))) static void setpage_slow(u16 page)
{
	*(vu16 *)0x9fe0000 = 0xd200; slow();
	*(vu16 *)0x8000000 = 0x1500; slow();
	*(vu16 *)0x8020000 = 0xd200; slow();
	*(vu16 *)0x8040000 = 0x1500; slow();
	*(vu16 *)0x9880000 = page;   slow();
	*(vu16 *)0x9fc0000 = 0x1500; slow();
}
int ez_switch_method = SW_EWRAM_THUMB;
void ez_set_rompage_m(u16 page, int m)
{
	if (m == SW_IWRAM_ARM) setpage_iwram(page);
	else if (m == SW_EWRAM_SLOW) setpage_slow(page);
	else setpage_ewram(page);
	cur_page = page;
}
IWRAM_CODE void ez_set_rompage(u16 page)
{
	if (ez_switch_method == SW_IWRAM_ARM) { setpage_iwram(page); cur_page = page; }
	else ez_set_rompage_m(page, ez_switch_method);
}
IWRAM_CODE static void SetSDControl(u16 control)
{
	*(vu16 *)0x9fe0000 = 0xd200;
	*(vu16 *)0x8000000 = 0x1500;
	*(vu16 *)0x8020000 = 0xd200;
	*(vu16 *)0x8040000 = 0x1500;
	*(vu16 *)0x9400000 = control;
	*(vu16 *)0x9fc0000 = 0x1500;
}
void ez_os_mode(void) { SetSDControl(0); ez_set_rompage(KERNEL_PAGE); }

// Try each way of switching the cart to OS mode until the S98 ID reads back.
// The ID read only uses the chip's read-only autoselect mode.
int ez_find_os_mode(u16 *id_out)
{
	int m;
	u16 id = 0;
	int two;
	for (m = 0; m < SW_COUNT; m++) for (two = 0; two < 2; two++) {
		ez_switch_method = m;
		cur_page = 0;
		SetSDControl(0);
		if (two) ez_set_rompage_m(BOOT_PAGE, m);      // via the bootloader page first
		ez_set_rompage_m(KERNEL_PAGE, m);
		id = nor_read_id();
		if (id == S98_ID) { if (id_out) *id_out = id; return m * 2 + two; }
	}
	ez_switch_method = SW_EWRAM_THUMB;
	if (id_out) *id_out = id;
	return -1;
}

#endif

// map a NOR offset into the 8 MB window, switching page like the kernel does
IWRAM_CODE vu16 *nor_ptr(u32 addr)
{
	u16 page = KERNEL_PAGE + (u16)((addr / WINDOW) * 0x1000);
	if (page != cur_page) ez_set_rompage(page);
	return (vu16 *)(FlashBase_S98 + (addr % WINDOW));
}
IWRAM_CODE void nor_reset(void) { *((vu16 *)(FlashBase_S98)) = 0xF0; }

u16 nor_read_id(void)
{
	u16 id;
	nor_ptr(0);
	*((vu16 *)(FlashBase_S98)) = 0xF0 ;
	*((vu16 *)(FlashBase_S98+0x555*2)) = 0xAA;
	*((vu16 *)(FlashBase_S98+0x2AA*2)) = 0x55;
	*((vu16 *)(FlashBase_S98+0x555*2)) = 0x90;
	id = *((vu16 *)(FlashBase_S98+0xE*2));
	*((vu16 *)(FlashBase_S98)) = 0xF0 ;
	return id;
}

// kernel Loadfile2NOR "erase all BBP" (clears the S98 protection bits before writing)
IWRAM_CODE void nor_unlock_ppb(void)
{
	nor_ptr(0);
	*((vu16 *)(FlashBase_S98)) = 0xF0 ;
	*((vu16 *)(FlashBase_S98+0x555*2)) = 0xAA ;
	*((vu16 *)(FlashBase_S98+0x2AA*2)) = 0x55 ;
	*((vu16 *)(FlashBase_S98+0x555*2)) = 0xC0 ;
	*((vu16 *)(FlashBase_S98+0x000*2)) = 0x80 ;
	*((vu16 *)(FlashBase_S98+0x000*2)) = 0x30 ;
	{
		// the kernel reads 0x15000 times from ROM code; keep the same read count
		// and also wait at least 1 second, whichever is longer
		int polling_counter = 0x15000;
		u32 s = hw_ticks();
		vu16 v1;
		do {
			v1 = *((vu16 *)(FlashBase_S98+ 0x5C0000));
			(void)v1;
			if (polling_counter) polling_counter--;
		} while (polling_counter || (hw_ticks() - s) < TICKS_PER_SEC);
	}
	*((vu16 *)(FlashBase_S98+0x000*2)) = 0x90 ;
	*((vu16 *)(FlashBase_S98+0x000*2)) = 0x00 ;
	*((vu16 *)(FlashBase_S98)) = 0xF0 ;
}

// wait for the toggle bit to stop (and DQ7 = 1 for erase), like the kernel, with a timeout
IWRAM_CODE static int poll_done(vu16 *p, int erase, u32 timeout_ticks)
{
	vu16 v1, v2;
	u32 s = hw_ticks();
	for (;;) {
		v1 = *p; v2 = *p;
		if (v1 == v2 && (!erase || (v1 & 0x80))) return FL_OK;
		if (hw_ticks() - s > timeout_ticks) return FL_TIMEOUT;
	}
}

// kernel Block_Erase (128 KB block; first and last block are 4 x 32 KB sectors)
IWRAM_CODE int nor_erase_block(u32 blockAdd)
{
	int r = FL_OK;
	u32 loop;
	vu16 *base = nor_ptr(blockAdd);
	u32 Address = (u32)base - FlashBase_S98;
	nor_reset();
	if ((blockAdd == 0) || (blockAdd == 0x3FE0000)) {
		for (loop = 0; loop < 0x20000; loop += 0x8000) {
			*((vu16 *)(FlashBase_S98+0x555*2)) = 0xAA ;
			*((vu16 *)(FlashBase_S98+0x2AA*2)) = 0x55 ;
			*((vu16 *)(FlashBase_S98+0x555*2)) = 0x80 ;
			*((vu16 *)(FlashBase_S98+0x555*2)) = 0xAA ;
			*((vu16 *)(FlashBase_S98+0x2AA*2)) = 0x55 ;
			*((vu16 *)(FlashBase_S98+Address+loop)) = 0x30 ;
			if (poll_done((vu16 *)(FlashBase_S98+Address+loop), 1, 6*TICKS_PER_SEC) != FL_OK) r = FL_TIMEOUT;
		}
	} else {
		*((vu16 *)(FlashBase_S98+0x555*2)) = 0xAA ;
		*((vu16 *)(FlashBase_S98+0x2AA*2)) = 0x55 ;
		*((vu16 *)(FlashBase_S98+0x555*2)) = 0x80 ;
		*((vu16 *)(FlashBase_S98+0x555*2)) = 0xAA ;
		*((vu16 *)(FlashBase_S98+0x2AA*2)) = 0x55 ;
		*((vu16 *)(FlashBase_S98+Address)) = 0x30 ;
		r = poll_done((vu16 *)(FlashBase_S98+Address), 1, 6*TICKS_PER_SEC);
	}
	nor_reset();
	return r;
}

// kernel WriteFlash_with32word, one 32-byte buffer (16 words)
IWRAM_CODE int nor_program32(u32 addr, const u16 *w)
{
	int i, r;
	vu16 *p = nor_ptr(addr);
	*((vu16 *)(FlashBase_S98+0x555*2)) = 0xAA ;
	*((vu16 *)(FlashBase_S98+0x2AA*2)) = 0x55 ;
	p[0] = 0x25;
	p[0] = 15;
	hw_delay_us(20);
	for (i = 0; i <= 15; i++) p[i] = w[i];
	p[0] = 0x29;
	hw_delay_us(80);
	r = poll_done(&p[0xF], 0, TICKS_PER_SEC / 10);   // 100 ms
	if (r != FL_OK) {
		// write-to-buffer abort reset
		*((vu16 *)(FlashBase_S98+0x555*2)) = 0xAA ;
		*((vu16 *)(FlashBase_S98+0x2AA*2)) = 0x55 ;
		*((vu16 *)(FlashBase_S98+0x555*2)) = 0xF0 ;
	}
	return r;
}

// kernel Read_NOR_info: READ ONLY from the S71 boot chip
IWRAM_CODE void nor_read_info(u16 *dst, u32 halfwords)
{
	u32 i;
	ez_set_rompage(KERNEL_PAGE);
	for (i = 0; i < halfwords; i++)
		dst[i] = *((vu16 *)(FlashBase_S71 + NOR_info_offset + i*2));
}

// READ ONLY from the S71 boot chip, kernel page (same mapping the kernel reads its info blocks with)
IWRAM_CODE void s71_read(u32 offset, u16 *dst, u32 halfwords)
{
	u32 i;
	ez_set_rompage(KERNEL_PAGE);
	for (i = 0; i < halfwords; i++) dst[i] = *((vu16 *)(FlashBase_S71 + offset + i*2));
}

// S98 autoselect and CFI query: both are read-only modes, left with 0xF0 (reset)
IWRAM_CODE void nor_query(u16 *autosel, u16 *cfi)
{
	int i;
	nor_ptr(0);
	*((vu16 *)(FlashBase_S98)) = 0xF0 ;
	*((vu16 *)(FlashBase_S98+0x555*2)) = 0xAA;
	*((vu16 *)(FlashBase_S98+0x2AA*2)) = 0x55;
	*((vu16 *)(FlashBase_S98+0x555*2)) = 0x90;
	for (i = 0; i < 0x20; i++) autosel[i] = *((vu16 *)(FlashBase_S98 + i*2));
	*((vu16 *)(FlashBase_S98)) = 0xF0 ;
	*((vu16 *)(FlashBase_S98+0x55*2)) = 0x98;
	for (i = 0; i < 0x40; i++) cfi[i] = *((vu16 *)(FlashBase_S98 + (0x10 + i)*2));
	*((vu16 *)(FlashBase_S98)) = 0xF0 ;
}

#ifndef IN_KERNEL
// kernel Read_FPGA_ver: SPI enable, read the version word, SPI disable. Read-only.
IWRAM_CODE static void SetSPIControl(u16 control)
{
	*(vu16 *)0x9fe0000 = 0xd200;
	*(vu16 *)0x8000000 = 0x1500;
	*(vu16 *)0x8020000 = 0xd200;
	*(vu16 *)0x8040000 = 0x1500;
	*(vu16 *)0x9660000 = control;
	*(vu16 *)0x9fc0000 = 0x1500;
}
IWRAM_CODE u16 ez_fpga_ver(void)
{
	u16 v;
	nor_reset();
	SetSPIControl(1);
	v = *(vu16 *)0x9E00000;
	SetSPIControl(0);
	return v;
}

// ---------------------------------------------------------------- SD (kernel Read/Write_SD_sectors)
IWRAM_CODE static int Wait_SD_Response(void)
{
	u32 count = 0;
	while (*(vu16 *)0x9E00000 == 0xEEE1) {
		if (++count > 0x100000) return 1;
	}
	return 0;
}
IWRAM_CODE int sd_read(u32 address, u32 count, u8 *SDbuffer)
{
	u32 i, blocks, times;
	nor_reset();
	SetSDControl(1);
	for (i = 0; i < count; i += 4) {
		blocks = (count - i > 4) ? 4 : (count - i);
		times = 2;
	read_again:
		*(vu16 *)0x9fe0000 = 0xd200;
		*(vu16 *)0x8000000 = 0x1500;
		*(vu16 *)0x8020000 = 0xd200;
		*(vu16 *)0x8040000 = 0x1500;
		*(vu16 *)0x9600000 = ((address+i)&0x0000FFFF) ;
		*(vu16 *)0x9620000 = ((address+i)&0xFFFF0000) >>16;
		*(vu16 *)0x9640000 = blocks;
		*(vu16 *)0x9fc0000 = 0x1500;
		SetSDControl(3);
		if (Wait_SD_Response()) {
			SetSDControl(1);
			if (--times) { hw_delay_us(20000); goto read_again; }
			SetSDControl(0);
			return 1;
		}
		SetSDControl(1);
		dmaCopy((void*)0x9E00000, SDbuffer+i*512, blocks*512);
	}
	SetSDControl(0);
	return 0;
}
IWRAM_CODE int sd_write(u32 address, u32 count, const u8 *SDbuffer)
{
	u32 i, blocks;
	nor_reset();
	SetSDControl(1);
	SetSDControl(3);
	for (i = 0; i < count; i += 4) {
		blocks = (count - i > 4) ? 4 : (count - i);
		dmaCopy(SDbuffer+i*512, (void*)0x9E00000, blocks*512);
		*(vu16 *)0x9fe0000 = 0xd200;
		*(vu16 *)0x8000000 = 0x1500;
		*(vu16 *)0x8020000 = 0xd200;
		*(vu16 *)0x8040000 = 0x1500;
		*(vu16 *)0x9600000 = ((address+i)&0x0000FFFF);
		*(vu16 *)0x9620000 = ((address+i)&0xFFFF0000) >>16;
		*(vu16 *)0x9640000 = 0x8000+blocks;
		*(vu16 *)0x9fc0000 = 0x1500;
		if (Wait_SD_Response()) { SetSDControl(0); return 1; }
	}
	hw_delay_us(12000);
	SetSDControl(0);
	return 0;
}

// ---------------------------------------------------------------- back to the cart's menu
extern void ez_soft_reset(void);   // reset.s (kernel SoftReset)
void ez_return_to_menu(void)
{
	REG_IME = 0;
	nor_reset();
	SetSDControl(0);
	ez_set_rompage(BOOT_PAGE);
	ez_soft_reset();
}

#endif
#elif defined(SIM)  // ======================================================== SIM (emulator) build
// A fake 64 MB NOR so every screen and the test logic can run in an emulator.
// Faults: block 203/204 bit 2 stuck at 0, block 377 erase times out.
#include "sim.h"
void ez_set_rompage(u16 page) { cur_page = page; }
int ez_switch_method = 0;
void ez_set_rompage_m(u16 page, int m) { (void)m; cur_page = page; }
int ez_find_os_mode(u16 *id_out) { if (id_out) *id_out = S98_ID; return 0; }
void ez_os_mode(void) {}
vu16 *nor_ptr(u32 addr) { (void)addr; return 0; }
void nor_reset(void) {}
u16  nor_read_id(void) { return S98_ID; }
void nor_unlock_ppb(void) { hw_delay_us(300000); }
int  nor_erase_block(u32 a) { return sim_erase(a); }
int  nor_program32(u32 a, const u16 *w) { return sim_program(a, w); }
void nor_read_info(u16 *dst, u32 n) { sim_info(dst, n); }
void s71_read(u32 o, u16 *d, u32 n) { u32 i; for (i = 0; i < n; i++) d[i] = (u16)(o + i); }
void nor_query(u16 *a, u16 *c) { int i; for (i = 0; i < 0x20; i++) a[i] = i == 0 ? 1 : i == 0xE ? S98_ID : 0; for (i = 0; i < 0x40; i++) c[i] = 0; c[0]='Q'; c[1]='R'; c[2]='Y'; }
int  sd_read(u32 s, u32 c, u8 *b) { (void)s; (void)c; (void)b; return 1; }
int  sd_write(u32 s, u32 c, const u8 *b) { (void)s; (void)c; (void)b; return 1; }
void ez_return_to_menu(void) { while (1) ; }
u16 ez_fpga_ver(void) { return 0xB007; }
#endif

// same test the kernel's Check_FW_update uses
const char *ez_rev_name(u16 v)
{
	return ((v & 0xF000) == 0xB000 || (v & 0xF000) == 0xA000) ? "Rev B (LX16)" : "Rev A (LX9)";
}

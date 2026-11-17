// NOR Check - EZ-Flash Omega DE hardware access
// Register sequences ported from the EZ-Flash Omega DE kernel (Ezcard_OP.c,
// NORflash_OP.c), Apache-2.0, (c) EZ-Flash. Timeouts added for NOR Check.
#ifndef EZHW_H
#define EZHW_H
#include <gba_base.h>

#define NOR_SIZE        0x4000000u   // S98 game NOR: 64 MB
#define NOR_BLOCK       0x20000u     // 128 KB erase block
#define NOR_BLOCKS      (NOR_SIZE / NOR_BLOCK)   // 512
#define S98_ID          0x223D
#define KERNEL_PAGE     0x8002       // OS mode page the kernel uses (gl_currentpage)
#define BOOT_PAGE       0x8000       // OS mode page that maps the bootloader

// results of flash operations
#define FL_OK           0
#define FL_TIMEOUT      1

void     hw_timer_start(void);
u32      hw_ticks(void);                 // 262144 ticks per second
void     hw_delay_us(u32 us);

void     ez_set_rompage(u16 page);       // as kernel SetRompage(), using the chosen switch method
// ways to write the cart's page register (from a game launch the cart may need slower writes)
enum { SW_IWRAM_ARM = 0, SW_EWRAM_THUMB, SW_EWRAM_SLOW, SW_COUNT };
extern int ez_switch_method;
void     ez_set_rompage_m(u16 page, int method);
int      ez_find_os_mode(u16 *id_out);   // tries each method; returns method that reached the NOR, or -1
void     ez_os_mode(void);               // OS mode, kernel page
u16      nor_read_id(void);              // S98 autoselect ID (0x223D)
void     nor_reset(void);                // back to read mode
void     nor_unlock_ppb(void);           // kernel's "erase all BBP" step
int      nor_erase_block(u32 addr);      // FL_OK / FL_TIMEOUT
int      nor_program32(u32 addr, const u16 *w); // 16 words (32 bytes)
vu16    *nor_ptr(u32 addr);              // map addr (0..64MB) into the 8 MB window
void     nor_read_info(u16 *dst, u32 halfwords); // NOR game list (read only)
void     s71_read(u32 offset, u16 *dst, u32 halfwords);  // READ ONLY from the boot chip
void     nor_query(u16 *autosel, u16 *cfi);
u16      ez_fpga_ver(void);                       // kernel Read_FPGA_ver (read-only SPI read)
const char *ez_rev_name(u16 ver);                // "Rev B (LX16)" / "Rev A (LX9)"       // S98 autoselect (0x20 words) + CFI (0x40 words), read-only modes

int      sd_read(u32 sector, u32 count, u8 *buf);
int      sd_write(u32 sector, u32 count, const u8 *buf);

void     ez_return_to_menu(void);        // SetRompage(0x8000) + soft reset
#endif

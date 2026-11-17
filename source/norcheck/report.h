// NOR Check - NOR game list (read only) and the SD report
#ifndef REPORT_H
#define REPORT_H
#include <gba_base.h>
#include "ezhw.h"
#include "nortest.h"
#define VERSION "1.1"
typedef struct { char filename[100]; u16 rompage, have_patch, have_RTS, is_64MBrom; u32 filesize;
                 u8 savemode, r1; u16 r2; char gamename[16]; } nor_fs_t;
#ifdef IN_KERNEL
#define games ((nor_fs_t *)NC_GAMES)
#else
extern nor_fs_t games[64];
#endif
extern int ngames;
extern u16 nor_id;
extern u16 fpga_ver;
void        load_games(void);
const char *base_name(const char *p);
int         game_at_block(int b);
int         save_report(void);
void        fmt_time(char *s, u32 ticks);
#endif

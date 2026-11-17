// NOR Check - test engine
#ifndef NORTEST_H
#define NORTEST_H
#include <gba_base.h>
#include "ezhw.h"

enum { MODE_READ = 0, MODE_QUICK, MODE_FULL };
enum { P_UNLOCK, P_ERASE, P_VBLANK, P_PROG_A, P_VER_A, P_PROG_NA, P_VER_NA, P_READ, P_END };

// per-block fault flags
#define F_ERASE_TO  0x01   // erase never finished
#define F_PROG_TO   0x02   // programming never finished
#define F_STUCK0    0x04   // bits read 0 where 1 was expected
#define F_STUCK1    0x08   // bits read 1 where 0 was written
#define F_UNSTABLE  0x10   // two reads of the same data differed

typedef struct {
	u8  flags[NOR_BLOCKS];
	u16 stuck0[NOR_BLOCKS];
	u16 stuck1[NOR_BLOCKS];
	u32 badwords[NOR_BLOCKS];
	u32 firstbad[NOR_BLOCKS];   // NOR offset of the first bad word (0xFFFFFFFF = none)
	int mode;
	const u8 *passes;
	int pass;                   // index into passes
	int block;                  // next block in this pass
	u32 units_done, units_total;
	u32 t_start, t_paused;
	int finished, stopped;
} test_t;

#ifdef IN_KERNEL
// Phosphor_DE: NOR Check's buffers live in the kernel's 128 KB scratch buffer (pReadCache),
// which is idle while NOR Check runs. EWRAM has no room for them otherwise.
extern u8 pReadCache[];
#define NC_SCRATCH(off) ((void *)(pReadCache + (off)))
#define T (*(test_t *)NC_SCRATCH(0x0000))      // ~6.7 KB
#define NC_RBUF   ((u16 *)NC_SCRATCH(0x2000))  // 4 KB
#define NC_GAMES  NC_SCRATCH(0x3000)           // 8.4 KB
#define NC_LIST   ((int *)NC_SCRATCH(0x6000))  // 2 KB
#define NC_FIL    NC_SCRATCH(0x7000)           // FatFs file object for the report
#else
extern test_t T;
#endif

// address-unique test pattern: every bit is 0 in one of A / ~A and 1 in the other
static inline u16 pat(u32 wordidx) { u32 x = wordidx * 0x9E3779B1u; return (u16)(x >> 16) ^ (u16)wordidx; }

void        test_begin(int mode);
int         test_step(void);          // runs one block; returns 1 while work remains
int         test_bad_count(void);
const char *pass_name(int p);
int         cur_pass(void);
#endif

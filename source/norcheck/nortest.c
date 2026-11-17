// NOR Check - test engine
// Passes run across the whole chip one after another (not block by block), so a
// fault where writing one block damages another block is caught too.
#include <string.h>
#include <gba_dma.h>
#include "nortest.h"
#ifdef SIM
#include "sim.h"
#endif

#ifndef IN_KERNEL
test_t T EWRAM_BSS;
#endif

static const u8 passes_read[]  = { P_READ, P_END };
static const u8 passes_quick[] = { P_UNLOCK, P_ERASE, P_VBLANK, P_PROG_A, P_VER_A, P_ERASE, P_END };
static const u8 passes_full[]  = { P_UNLOCK, P_ERASE, P_VBLANK, P_PROG_A, P_VER_A,
                                   P_ERASE, P_VBLANK, P_PROG_NA, P_VER_NA, P_ERASE, P_VBLANK, P_END };

static u32 unit_cost(int p)
{
	switch (p) {
	case P_ERASE: return 6; case P_PROG_A: case P_PROG_NA: return 6;
	case P_READ: return 2; case P_UNLOCK: return 0; default: return 1;
	}
}

const char *pass_name(int p)
{
	switch (p) {
	case P_UNLOCK:  return "Unlocking NOR";
	case P_ERASE:   return "Erase";
	case P_VBLANK:  return "Check blank";
	case P_PROG_A:  return "Write pattern A";
	case P_VER_A:   return "Check pattern A";
	case P_PROG_NA: return "Write pattern B";
	case P_VER_NA:  return "Check pattern B";
	case P_READ:    return "Read check";
	default:        return "Done";
	}
}
int cur_pass(void) { return T.passes[T.pass]; }

void test_begin(int mode)
{
	int i;
	u32 total = 0;
	memset(&T, 0, sizeof T);
	for (i = 0; i < NOR_BLOCKS; i++) T.firstbad[i] = 0xFFFFFFFF;
	T.mode = mode;
	T.passes = mode == MODE_READ ? passes_read : mode == MODE_QUICK ? passes_quick : passes_full;
	for (i = 0; T.passes[i] != P_END; i++) total += unit_cost(T.passes[i]) * NOR_BLOCKS;
	T.units_total = total;
	T.t_start = hw_ticks();
}

int test_bad_count(void)
{
	int i, n = 0;
	for (i = 0; i < NOR_BLOCKS; i++) if (T.flags[i]) n++;
	return n;
}

static void mark(int b, u32 addr, u16 got, u16 exp)
{
	u16 s0 = (u16)(~got & exp), s1 = (u16)(got & ~exp);
	if (s0) { T.flags[b] |= F_STUCK0; T.stuck0[b] |= s0; }
	if (s1) { T.flags[b] |= F_STUCK1; T.stuck1[b] |= s1; }
	T.badwords[b]++;
	if (addr < T.firstbad[b]) T.firstbad[b] = addr;
}

#ifndef SIM
#define RD(p, i, addr) ((p)[i])
#else
#define RD(p, i, addr) sim_read(addr)
#endif

// verify one block against: 0 = blank (FFFF), 1 = pattern A, 2 = pattern ~A
IWRAM_CODE static void verify_block(int b, int kind)
{
	u32 base = (u32)b * NOR_BLOCK, i, w0 = base >> 1;
	vu16 *p = nor_ptr(base);
	(void)p;
	for (i = 0; i < NOR_BLOCK / 2; i++) {
#ifdef SIM
		if ((i & 63) && i != 0x91A) continue;   // emulator build: sample, so screens can be captured quickly
#endif
		u16 exp = kind == 0 ? 0xFFFF : kind == 1 ? pat(w0 + i) : (u16)~pat(w0 + i);
		u16 got = RD(p, i, base + i * 2);
		if (got != exp) mark(b, base + i * 2, got, exp);
	}
}

IWRAM_CODE static void program_block(int b, int inv)
{
	u32 base = (u32)b * NOR_BLOCK, off, j, w0 = base >> 1;
	u16 w[16];
	for (off = 0; off < NOR_BLOCK; off += 32) {
		for (j = 0; j < 16; j++) {
			u16 v = pat(w0 + off / 2 + j);
			w[j] = inv ? (u16)~v : v;
		}
		if (nor_program32(base + off, w) != FL_OK) {
			T.flags[b] |= F_PROG_TO;
			if (base + off < T.firstbad[b]) T.firstbad[b] = base + off;
			nor_reset();
		}
#ifdef SIM
		break;   // emulator build: the fake NOR only needs the first write of each block
#endif
	}
	nor_reset();
}

// read-only: read each 4 KB twice (DMA, then CPU) and compare
#ifdef IN_KERNEL
#define rbuf NC_RBUF
#define RBUF_BYTES 4096
#else
static u16 rbuf[2048] EWRAM_BSS;
#define RBUF_BYTES sizeof rbuf
#endif
IWRAM_CODE static void read_block(int b)
{
	u32 base = (u32)b * NOR_BLOCK, off, i;
	for (off = 0; off < NOR_BLOCK; off += RBUF_BYTES) {
#ifndef SIM
		vu16 *p = nor_ptr(base + off);
		dmaCopy((void *)p, rbuf, RBUF_BYTES);   // DMA copy only reads, never writes, the NOR
		for (i = 0; i < 2048; i++) {
			u16 a = rbuf[i], c = p[i];
			if (a != c) { T.flags[b] |= F_UNSTABLE; T.badwords[b]++; if (base+off+i*2 < T.firstbad[b]) T.firstbad[b] = base+off+i*2; }
		}
#else
		for (i = 0; i < 2048; i += 64) {
			u16 a = sim_read(base + off + i * 2), c = sim_read(base + off + i * 2);
			if (a != c) { T.flags[b] |= F_UNSTABLE; T.badwords[b]++; }
		}
#endif
	}
}

int test_step(void)
{
	int p = T.passes[T.pass];
	if (p == P_END) { T.finished = 1; return 0; }
	if (p == P_UNLOCK) { nor_unlock_ppb(); T.pass++; return 1; }

	int b = T.block;
	switch (p) {
	case P_ERASE:
		if (nor_erase_block((u32)b * NOR_BLOCK) != FL_OK) {
			T.flags[b] |= F_ERASE_TO;
			if ((u32)b * NOR_BLOCK < T.firstbad[b]) T.firstbad[b] = (u32)b * NOR_BLOCK;
		}
		break;
	case P_VBLANK:  verify_block(b, 0); break;
	case P_PROG_A:  program_block(b, 0); break;
	case P_VER_A:   verify_block(b, 1); break;
	case P_PROG_NA: program_block(b, 1); break;
	case P_VER_NA:  verify_block(b, 2); break;
	case P_READ:    read_block(b); break;
	}
	T.units_done += unit_cost(p);
	if (++T.block >= NOR_BLOCKS) { T.block = 0; T.pass++; }
	if (T.passes[T.pass] == P_END) { T.finished = 1; nor_reset(); return 0; }
	return 1;
}

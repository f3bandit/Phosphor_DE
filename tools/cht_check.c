// Phosphor_DE cheat checker - by f3bandit
// The cart's cheat parser/compiler (source/pde_cheats.c) built for the PC with stubbed file I/O.
// Usage: CHROOT=<folder with SYSTEM/CHEATS> ./cht_check <GAMECODE> [cheat name ...]
// Prints every cheat ([n/a] = the engine can't run it); names given on the command line are switched
// on and their compiled engine entries are printed. make_gh_pack.py builds and runs it.
// Keep it in step with source/pde_cheats.c when the cheat code changes.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned int UINT;
typedef char TCHAR;
typedef struct { FILE *f; } FIL;
enum { FR_OK=0 }; 
#define FA_READ 1
#define FA_WRITE 2
#define FA_CREATE_ALWAYS 8
#define MAX_pReadCache_size 0x20000
u8 pReadCache[0x20000];
typedef struct { u32 address, VAL; } ST_entry;
#define MAX_CHEAT_ENTRIES 2900
ST_entry pCHEAT[MAX_CHEAT_ENTRIES]; u32 gl_cheat_count; u16 gl_cheat_on=1; FIL gfile;
#include <stdlib.h>
static const char *root;
int f_open(FIL*fp,const char*p,int m){char b[300];sprintf(b,"%s%s",root,p);fp->f=fopen(b,(m&FA_WRITE)?"wb":"rb");return fp->f?0:1;}
int f_close(FIL*fp){fclose(fp->f);return 0;}
int f_read(FIL*fp,void*b,UINT n,UINT*br){*br=fread(b,1,n,fp->f);return 0;}
int f_write(FIL*fp,const void*b,UINT n,UINT*bw){*bw=fwrite(b,1,n,fp->f);return 0;}
int f_lseek(FIL*fp,u32 o){return fseek(fp->f,o,SEEK_SET);}
int f_unlink(const char*p){char b[300];sprintf(b,"%s%s",root,p);return remove(b);} int f_mkdir(const char*p){return 0;}
#define PDE_UI
#define _GBA_BASE_H
// Phosphor_DE cheats - by f3bandit
//
// One file per game in /SYSTEM/CHEATS, named by the 4-letter game code from the ROM header
// (BPEE.cht = Pokemon Emerald US), the same way the box art is named.
// Two layouts are read:
//   RetroArch .cht      cheat0_desc = "Name" / cheat0_code = "XXXXXXXX+YYYY+..." / cheat0_enable = false
//   plain text          a name line, then its codes one "XXXXXXXX YYYY" per line (paste from GameHacking.org)
// Codes are Code Breaker (unencrypted). They are compiled here into entries for the in-game
// engine (pde_engine.inc): writes, or/and/add, if ==/!=/>/</&, hold-a-button, fill.
// ROM writes become patches applied to the ROM image when it loads.
// Cheats you turn on are saved to /SYSTEM/CHEATS/<CODE>.sel ("Name" per line) and stay on.
#ifdef PDE_UI

extern FIL gfile;
extern ST_entry pCHEAT[];
extern u32 gl_cheat_count;
extern u16 gl_cheat_on;

#define CH_MAX   448
#define CH_NAME  56
#define FL_BAD   1      // a code we can't run (encrypted, unknown type)
typedef struct { char name[CH_NAME]; u32 noff, off, end; u8 sel, fl; u8 pad[2]; } pc_cheat;
// noff: file offset of the full name; off/end: the code text (retro: end 0 = up to the closing quote)

// scratch in pReadCache above the game card's box art (0x10000..0x14B38)
#define PC_CH_OFS  0x15000
#define PC_BLK_OFS 0x1F000
#define PC_BLK_LEN 0x800
#define PC_FIL_OFS (PC_BLK_OFS + PC_BLK_LEN)
_Static_assert(PC_CH_OFS + CH_MAX * sizeof(pc_cheat) <= PC_BLK_OFS, "cheat table overlaps the read block");
_Static_assert(PC_FIL_OFS + sizeof(FIL) <= MAX_pReadCache_size, "cheat file object past pReadCache");
#define CH  ((pc_cheat *)(pReadCache + PC_CH_OFS))
#define BLK ((char *)(pReadCache + PC_BLK_OFS))
#define CF  ((FIL *)(pReadCache + PC_FIL_OFS))     // the cheat file, kept open while the screen is up

static int n_ch, retro;
static char cht_path[40], sel_path[40];
static int cf_open;
static int cht_open(void) { if (!cf_open) cf_open = (f_open(CF, cht_path, FA_READ) == FR_OK); return cf_open; }
static void cht_close(void) { if (cf_open) f_close(CF); cf_open = 0; }

// ---------------------------------------------------------------- helpers
static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t') s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
	return s;
}
static void copy_n(char *d, const char *s, int n) { strncpy(d, s, n - 1); d[n - 1] = 0; }
static int hexv(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}
static int is_code_line(const char *t)   // only hex digits and separators, at least 12 digits
{
	int d = 0;
	for (; *t; t++) { if (hexv(*t) >= 0) d++; else if (*t != ' ' && *t != '\t' && *t != '+' && *t != ':' && *t != '-') return 0; }
	return d >= 12;
}

// ---------------------------------------------------------------- parse
static int new_cheat(const char *name, u32 noff)
{
	if (n_ch >= CH_MAX) return -1;
	pc_cheat *c = &CH[n_ch];
	memset(c, 0, sizeof *c);
	copy_n(c->name, name, CH_NAME); c->noff = noff;
	return n_ch++;
}
static int pl_cur;
static void handle_line(char *raw, u32 start)
{
	char *t = trim(raw);
	if (!t[0] || t[0] == '#' || (t[0] == '/' && t[1] == '/')) return;
	if (retro) {
		// cheatN_desc = "..."   cheatN_code = "..."
		if (strncmp(t, "cheat", 5)) return;
		int idx = 0; char *p = t + 5;
		if (*p < '0' || *p > '9') return;
		while (*p >= '0' && *p <= '9') idx = idx * 10 + (*p++ - '0');
		if (idx >= CH_MAX || *p != '_') return;
		char *q = strchr(p, '"'); if (!q) return;
		u32 voff = start + (q + 1 - raw);
		while (n_ch <= idx) if (new_cheat("", 0) < 0) return;
		if (!strncmp(p, "_desc", 5)) {
			char *e = strchr(q + 1, '"'); if (e) *e = 0;
			copy_n(CH[idx].name, trim(q + 1), CH_NAME); CH[idx].noff = voff;
		} else if (!strncmp(p, "_code", 5)) {
			CH[idx].off = voff; CH[idx].end = 0;
		}
		return;
	}
	// plain text: a name line, then code lines
	if (is_code_line(t)) {
		if (pl_cur < 0) { char nm[16]; sprintf(nm, "Cheat %d", n_ch + 1); pl_cur = new_cheat(nm, 0); if (pl_cur < 0) return; }
		if (!CH[pl_cur].off) CH[pl_cur].off = start;
		CH[pl_cur].end = 0xFFFFFFFF;   // fixed up when the next name starts
		return;
	}
	if (pl_cur >= 0 && CH[pl_cur].end == 0xFFFFFFFF) CH[pl_cur].end = start;
	if (t[0] == '[') { t++; char *e = strchr(t, ']'); if (e) *e = 0; }
	int l = strlen(t); if (l && t[l - 1] == ':') t[l - 1] = 0;
	t = trim(t);
	pl_cur = new_cheat(t, start + (t - raw));
}
static int parse_cht(void)
{
	n_ch = 0; pl_cur = -1; retro = 0;
	if (!cht_open()) return 0;
	UINT br = 0, bi = 0;
	f_lseek(CF, 0);   // RetroArch layout if "cheatN_code" shows up near the top
	if (f_read(CF, BLK, PC_BLK_LEN - 1, &br) == FR_OK) { BLK[br] = 0; if (strstr(BLK, "_code") && strstr(BLK, "cheat")) retro = 1; }
	f_lseek(CF, 0); br = 0;
	char line[160]; int ll = 0;
	u32 pos = 0, line_start = 0;
	for (;;) {
		if (bi >= br) {
			pos += br; bi = 0;
			if (f_read(CF, BLK, PC_BLK_LEN, &br) != FR_OK || !br) break;
		}
		char c = BLK[bi++];
		if (c == '\n') { line[ll] = 0; handle_line(line, line_start); ll = 0; line_start = pos + bi; continue; }
		if (c == '\r') continue;
		if (ll < (int)sizeof line - 1) line[ll++] = c;
		if (pos + bi == 3 && ll == 3 && (u8)line[0] == 0xEF && (u8)line[1] == 0xBB) { ll = 0; line_start = 3; }   // UTF-8 BOM
	}
	if (ll) { line[ll] = 0; handle_line(line, line_start); }
	if (!retro && pl_cur >= 0 && CH[pl_cur].end == 0xFFFFFFFF) CH[pl_cur].end = pos;
	int w = 0;   // drop cheats with no code; unnamed ones get a number
	for (int i = 0; i < n_ch; i++) if (CH[i].off) {
		if (w != i) CH[w] = CH[i];
		if (!CH[w].name[0]) sprintf(CH[w].name, "Cheat %d", w + 1);
		w++;
	}
	n_ch = w;
	return n_ch;
}

// ---------------------------------------------------------------- Code Breaker -> engine entries
enum { OP_W8, OP_W16, OP_OR, OP_AND, OP_ADD, OP_IFEQ, OP_IFNE, OP_IFGT, OP_IFLT, OP_IFAND, OP_KEYS, OP_FILL, OP_ROM16 = 15 };
static int cb_emit_ok;          // 0 = dry run (only check), 1 = really write pCHEAT
static u32 cb_n;                // entries this cheat produced
static void emit(u32 w0, u32 w1)
{
	if (cb_emit_ok && gl_cheat_count < MAX_CHEAT_ENTRIES) { pCHEAT[gl_cheat_count].address = w0; pCHEAT[gl_cheat_count].VAL = w1; gl_cheat_count++; }
	cb_n++;
}
static void set_skip(u32 idx, u32 skip)
{
	if (cb_emit_ok && idx < gl_cheat_count) pCHEAT[idx].VAL = (pCHEAT[idx].VAL & 0xFFFF) | ((skip > 255 ? 255 : skip) << 16);
}
static int ram_ok(u32 a) { u32 r = a >> 24; return r >= 2 && r <= 7; }
static int rom_ok(u32 a) { return a >= 0x08000000 && a < 0x0A000000; }
static u16 swap16(u32 h) { return ((h >> 8) | (h << 8)) & 0xFFFF; }

// one Code Breaker line; returns 0 if it can't be run
static int fill_wait, list_left, cond_wait;
static u32 list_addr, cond_idx, cond_start;
static int cb_line(u32 op1, u32 op2)
{
	u32 type = op1 >> 28, a = op1 & 0x0FFFFFFF;
	int code_done = 1, is_cond = 0;
	static const u8 cop[16] = { [0x7] = OP_IFEQ, [0xA] = OP_IFNE, [0xB] = OP_IFGT, [0xC] = OP_IFLT, [0xF] = OP_IFAND };
	if (fill_wait) { emit(op1, op2); fill_wait = 0; }                     // repeat | value step<<16, address step
	else if (list_left) {                                                  // 5: halfwords follow, byte-swapped
		u32 h[3] = { op1 >> 16, op1 & 0xFFFF, op2 };
		for (int k = 0; k < 3 && list_left; k++, list_left--) { emit((OP_W16 << 28) | list_addr, swap16(h[k])); list_addr += 2; }
		code_done = !list_left;
	}
	else switch (type) {
	case 0x0: case 0x1: code_done = 0; break;                              // game id / hook: nothing to run
	case 0x2: if (!ram_ok(a)) return 0; emit((OP_OR << 28) | a, op2); break;
	case 0x3: if (!ram_ok(a)) return 0; emit((OP_W8 << 28) | a, op2 & 0xFF); break;
	case 0x4: if (!ram_ok(a)) return 0; emit((OP_FILL << 28) | a, op2); fill_wait = 1; code_done = 0; break;
	case 0x5: if (!ram_ok(a) || !op2) return 0; list_addr = a; list_left = op2; code_done = 0; break;
	case 0x6: if (!ram_ok(a)) return 0; emit((OP_AND << 28) | a, op2); break;
	case 0x8:
		if (rom_ok(a)) { if (cond_wait) return 0; emit((OP_ROM16 << 28) | (a - 0x08000000), op2); }
		else { if (!ram_ok(a)) return 0; emit((OP_W16 << 28) | a, op2); }
		break;
	case 0x7: case 0xA: case 0xB: case 0xC: case 0xF:
		if (!ram_ok(a)) return 0;
		emit((cop[type] << 28) | a, op2); is_cond = 1; break;
	case 0xD:
		if (a != 0x20) return 0;                                           // only "if keys held"
		emit((OP_KEYS << 28) | 0x04000130, op2); is_cond = 1; break;
	case 0xE: if (!ram_ok(a)) return 0; emit((OP_ADD << 28) | a, op2); break;
	default: return 0;                                                     // 9 = encrypted
	}
	// a condition covers the whole next code
	if (cond_wait && (code_done || is_cond) && !fill_wait && !list_left) { set_skip(cond_idx, cb_n - cond_start - (is_cond ? 0 : 0)); cond_wait = 0; }
	if (is_cond) { cond_wait = 1; cond_idx = gl_cheat_count - 1; cond_start = cb_n; }
	return 1;
}
static int compile(int ci)
{
	pc_cheat *c = &CH[ci];
	u32 tok0 = 0, cur = 0; int nt = 0, digits = 0;
	UINT br, bi;
	fill_wait = list_left = cond_wait = 0;
	if (!cht_open()) return 0;
	f_lseek(CF, c->off);
	u32 pos = c->off;
	for (;;) {
		int eof = (f_read(CF, BLK, 128, &br) != FR_OK || !br);
		if (eof) br = 0;
		for (bi = 0; bi <= br; bi++) {
			if (bi == br && !eof) break;                                   // token may carry into the next block
			char ch = bi < br ? BLK[bi] : 0;
			int end_here = (bi == br) || (!retro && pos + bi >= c->end) || (retro && ch == '"');
			int d = end_here ? -1 : hexv(ch);
			if (d >= 0) { cur = (cur << 4) | d; if (++digits > 8) return 0; continue; }
			if (digits) {
				if (nt == 0) { if (digits != 8) return 0; tok0 = cur; nt = 1; }
				else { if (digits != 4) return 0; nt = 0; if (!cb_line(tok0, cur)) return 0; }
			}
			digits = 0; cur = 0;
			if (end_here) return !(nt || fill_wait || list_left);
		}
		pos += br;
	}
}
// "Anti DMA" and "(Must Be On)" codes are what keep a game's other codes working
// (e.g. Pokemon moves its data around without Anti DMA): they go on with any other cheat
static int is_auto(int i)
{
	const char *n = CH[i].name;
	return strstr(n, "Anti DMA") || strstr(n, "Anti-DMA") || strstr(n, "Must Be On") || strstr(n, "must be on");
}
static int count_on(void);
static void apply(void)
{
	int any = count_on();
	gl_cheat_count = 0;
	cb_emit_ok = 1;
	for (int i = 0; i < n_ch; i++) if ((CH[i].sel || (any && is_auto(i))) && !(CH[i].fl & FL_BAD)) {
		u32 keep = gl_cheat_count;
		cb_n = 0;
		if (!compile(i)) gl_cheat_count = keep;
	}
}
static void check_all(void)
{
	cb_emit_ok = 0;
	for (int i = 0; i < n_ch; i++) { cb_n = 0; if (!compile(i) || !cb_n) CH[i].fl |= FL_BAD; }
}
static int count_on(void) { int n = 0; for (int i = 0; i < n_ch; i++) if (CH[i].sel && !(CH[i].fl & FL_BAD) && !is_auto(i)) n++; return n; }

// ---------------------------------------------------------------- remembered picks
static void load_sel(void)
{
	UINT br;
	if (!sel_path[0] || f_open(&gfile, sel_path, FA_READ) != FR_OK) return;
	if (f_read(&gfile, BLK, PC_BLK_LEN - 1, &br) != FR_OK) br = 0;
	f_close(&gfile);
	BLK[br] = 0;
	char *p = BLK;
	while (*p) {
		char *nl = strchr(p, '\n'); if (nl) *nl = 0;
		char *cr = strchr(p, '\r'); if (cr) *cr = 0;
		char *nm = trim(p);
		if (strlen(nm) > CH_NAME - 1) nm[CH_NAME - 1] = 0;   // names are kept to CH_NAME-1 characters
		for (int i = 0; i < n_ch; i++) if (!CH[i].sel && !strcmp(CH[i].name, nm)) { CH[i].sel = 1; break; }
		if (!nl) break;
		p = nl + 1;
	}
}
static void save_sel(void)
{
	UINT bw;
	if (!sel_path[0]) return;
	if (!count_on()) { f_unlink(sel_path); return; }
	if (f_open(&gfile, sel_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
		f_mkdir("/SYSTEM/CHEATS");
		if (f_open(&gfile, sel_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return;
	}
	for (int i = 0; i < n_ch; i++) if (CH[i].sel) {
		f_write(&gfile, CH[i].name, strlen(CH[i].name), &bw);
		f_write(&gfile, "\r\n", 2, &bw);
	}
	f_close(&gfile);
}

// ---------------------------------------------------------------- public
int pde_cheats_probe(TCHAR *gamefile, const char *code)
{
	(void)gamefile;
	gl_cheat_count = 0; n_ch = 0;
	cht_close();
	if (!code[0]) return -1;
	sprintf(cht_path, "/SYSTEM/CHEATS/%.4s.cht", code);
	sprintf(sel_path, "/SYSTEM/CHEATS/%.4s.sel", code);
	if (!parse_cht()) { cht_close(); return -1; }
	check_all();
	load_sel();
	int n = count_on();
	if (n) apply();
	cht_close();
	return n;
}

// full name of a cheat (names longer than CH_NAME are read back from the file)
#endif
int main(int c,char**v){ root=getenv("CHROOT")?getenv("CHROOT"):"/tmp/ht/sd"; int n=pde_cheats_probe("x",v[1]); printf("cheats=%d\n",n_ch);
 for(int i=0;i<n_ch;i++) printf(" %s%s%s\n",CH[i].name,(CH[i].fl&FL_BAD)?" [n/a]":"",is_auto(i)?" [auto]":"");
 for(int i=2;i<c;i++) for(int j=0;j<n_ch;j++) if(strstr(CH[j].name,v[i])) {CH[j].sel=1; printf("on: %s\n",CH[j].name); break;}
 cht_open(); apply();
 for(u32 i=0;i<gl_cheat_count;i++) printf("  op%u %07X = %X\n",pCHEAT[i].address>>28,pCHEAT[i].address&0x0FFFFFFF,pCHEAT[i].VAL);
 printf("count_on=%d\n",count_on()); return 0;}

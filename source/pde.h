// Phosphor_DE front end
#ifndef PDE_H
#define PDE_H
#include <gba_base.h>
#include "ff.h"

#define PDE_NAME     "Phosphor_DE"
#define PDE_VERSION  "0.2"
#define W_CLOCK_X    198

void pde_splash(void);
void pde_popup(const char *title, const char *line, u16 edge);
void pde_progress(const char *msg);
void pde_fw_page(u16 readver, u16 cur, u16 latest, const char *rev);
// cheats (pde_cheats.c)
int  pde_cheats_probe(TCHAR *gamefile, const char *code);   // -1 no cheat file, else number of cheats on (loaded for launch)
int  pde_cheats(TCHAR *gamefile, const char *code, const char *title);    // returns cheats on, -1 none found
u16  pde_keys_down(void);
void pde_dump_bootchip(void);            // pde_dump.c, read-only
void pde_wait_release(void);
int  pde_confirm(const char *title, const char *line);
void pde_file_options(TCHAR *file);
void pde_settings_page_draw(void);
int  pde_settings_keys(u16 keys_down);   // returns 1 when the whole page must be redrawn
#endif

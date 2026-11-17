// NOR Check - drawing (Phosphor style, mode 3)
#ifndef UI_H
#define UI_H
#include <gba_base.h>
#define RGB5(r,g,b) ((r)|((g)<<5)|((b)<<10))
#define C_BG    RGB5(5,5,6)
#define C_PANEL RGB5(7,7,8)
#define C_PON   RGB5(11,9,5)
#define C_AC    RGB5(30,21,6)
#define C_TXT   RGB5(29,29,29)
#define C_DIM   RGB5(23,23,24)
#define C_FAINT RGB5(16,16,17)
#define C_LINE  RGB5(10,10,11)
#define C_GRN   RGB5(11,26,17)
#define C_RED   RGB5(28,12,11)

void ui_init(void);
void ui_clear(void);
void ui_fill(int x, int y, int w, int h, u16 c);
void ui_box(int x, int y, int w, int h, u16 fill, u16 edge);   // edge 0xFFFF = none
int  ui_text(int x, int y, const char *s, u16 c);               // returns end x
int  ui_textlen(const char *s);                                 // characters (UTF-8 aware)
void ui_textw(int x, int y, const char *s, u16 c, int maxw);
void ui_top(const char *title, const char *sub);
void ui_hints(const char *const *pairs, int n);                 // {"A","Start",...}
void ui_row_sel(int y);
void ui_vsync(void);
u16  ui_keys(void);                                             // held keys (1 = pressed)
#endif

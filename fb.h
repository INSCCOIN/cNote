#ifndef CDOCK_FB_H
#define CDOCK_FB_H
#include <stdint.h>

extern unsigned FB_W, FB_H;

int fb_open(void);
void fb_close(void);
uint16_t rgb565(int r, int g, int b);
void px(int x, int y, uint16_t c);
void fill(int x, int y, int w, int h, uint16_t c);
void hline(int x, int y, int w, uint16_t c);
void vline(int x, int y, int h, uint16_t c);
void rect(int x, int y, int w, int h, uint16_t c);
void text(int x, int y, const char *s, uint16_t c);
void text_s(int x, int y, const char *s, uint16_t c, int sc);
int font_w(int sc);
int font_h(int sc);
void line(int x0, int y0, int x1, int y1, uint16_t c);
void fb_clear(uint16_t c);
void fb_flip(void);

#endif

/* Monochrome frame buffer with drawing primitives and bitmap fonts (Adafruit GFX /
 * tinyfont glyph format). Shared by the firmware (SSD1306 over I2C) and the host
 * simulator (ASCII rendering). Perl sees it as the Gfx package. */
#ifndef PP_GFX_H
#define PP_GFX_H
#include <stdint.h>
#include "picoperl.h"

typedef struct { uint16_t off; uint8_t w, h, adv; int8_t xo, yo; } pp_glyph;
typedef struct {
    const char     *name;
    const uint8_t  *bitmap;
    const pp_glyph *glyphs;
    uint16_t        first, last;
    uint8_t         yadv;
} pp_font;
extern const pp_font pp_fonts[];
extern const int     pp_nfonts;

typedef struct {
    int      w, h;      /* physical size (h multiple of 8) */
    int      rot;       /* 0, 1, 2, 3 = 0/90/180/270 degrees, applied to logical coordinates */
    uint8_t *buf;       /* SSD1306 layout: page-major, bit = y % 8 */
    int      pages;
} pp_gfx;

pp_gfx *pp_gfx_new(int w, int h);
void    pp_gfx_free(pp_gfx *g);
void    pp_gfx_size(const pp_gfx *g, int *w, int *h);   /* logical (rotated) size */
void    pp_gfx_clear(pp_gfx *g);
void    pp_gfx_pixel(pp_gfx *g, int x, int y, int on);
int     pp_gfx_get_pixel(const pp_gfx *g, int x, int y);
void    pp_gfx_line(pp_gfx *g, int x0, int y0, int x1, int y1, int on);
void    pp_gfx_rect(pp_gfx *g, int x, int y, int w, int h, int on);
void    pp_gfx_fill_rect(pp_gfx *g, int x, int y, int w, int h, int on);
void    pp_gfx_circle(pp_gfx *g, int cx, int cy, int r, int on);
void    pp_gfx_fill_circle(pp_gfx *g, int cx, int cy, int r, int on);
void    pp_gfx_triangle(pp_gfx *g, int x0, int y0, int x1, int y1, int x2, int y2, int on);
void    pp_gfx_fill_triangle(pp_gfx *g, int x0, int y0, int x1, int y1, int x2, int y2, int on);
const pp_font *pp_gfx_font(const char *name);
/* draws text with (x, y) at the baseline like tinyfont.WriteLine; returns the advance */
int     pp_gfx_text(pp_gfx *g, const pp_font *f, int x, int y, const char *s, int on, int scale);
int     pp_gfx_text_width(const pp_font *f, const char *s, int scale);

/* Gfx::_* natives (frame buffers by handle) */
void    pp_gfx_register(pp_interp *I);
pp_gfx *pp_gfx_handle(int h);            /* NULL if not allocated */

#endif

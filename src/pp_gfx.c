/* Frame buffer, drawing primitives, bitmap fonts and the Gfx::_* natives. */
#include <stdlib.h>
#include <string.h>
#include "pp_gfx.h"
#include "pp_internal.h"

/* ------------------------------------------------------------------ */
/* frame buffer                                                        */
/* ------------------------------------------------------------------ */

pp_gfx *pp_gfx_new(int w, int h) {
    if (w <= 0 || h <= 0) return NULL;
    pp_gfx *g = pp_xmalloc(sizeof *g);
    g->w = w; g->h = h; g->rot = 0;
    g->pages = (h + 7) / 8;
    g->buf = pp_xmalloc((size_t)w * g->pages);
    memset(g->buf, 0, (size_t)w * g->pages);
    return g;
}
void pp_gfx_free(pp_gfx *g) { if (g) { free(g->buf); free(g); } }
void pp_gfx_size(const pp_gfx *g, int *w, int *h) {
    if (g->rot & 1) { *w = g->h; *h = g->w; } else { *w = g->w; *h = g->h; }
}
void pp_gfx_clear(pp_gfx *g) { memset(g->buf, 0, (size_t)g->w * g->pages); }

static int phys(const pp_gfx *g, int *x, int *y) {
    int lx = *x, ly = *y, px, py;
    switch (g->rot & 3) {
    case 0: px = lx; py = ly; break;
    case 1: px = g->w - 1 - ly; py = lx; break;
    case 2: px = g->w - 1 - lx; py = g->h - 1 - ly; break;
    default: px = ly; py = g->h - 1 - lx; break;
    }
    if (px < 0 || py < 0 || px >= g->w || py >= g->h) return 0;
    *x = px; *y = py;
    return 1;
}
void pp_gfx_pixel(pp_gfx *g, int x, int y, int on) {
    if (!phys(g, &x, &y)) return;
    uint8_t *b = &g->buf[(y >> 3) * g->w + x];
    if (on) *b |= (uint8_t)(1 << (y & 7)); else *b &= (uint8_t)~(1 << (y & 7));
}
int pp_gfx_get_pixel(const pp_gfx *g, int x, int y) {
    if (!phys(g, &x, &y)) return 0;
    return (g->buf[(y >> 3) * g->w + x] >> (y & 7)) & 1;
}

/* ------------------------------------------------------------------ */
/* primitives (tinydraw equivalents)                                   */
/* ------------------------------------------------------------------ */

static void swap(int *a, int *b) { int t = *a; *a = *b; *b = t; }

void pp_gfx_line(pp_gfx *g, int x0, int y0, int x1, int y1, int on) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pp_gfx_pixel(g, x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void pp_gfx_rect(pp_gfx *g, int x, int y, int w, int h, int on) {
    if (w <= 0 || h <= 0) return;
    pp_gfx_line(g, x, y, x + w - 1, y, on);
    pp_gfx_line(g, x, y + h - 1, x + w - 1, y + h - 1, on);
    pp_gfx_line(g, x, y, x, y + h - 1, on);
    pp_gfx_line(g, x + w - 1, y, x + w - 1, y + h - 1, on);
}
void pp_gfx_fill_rect(pp_gfx *g, int x, int y, int w, int h, int on) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) pp_gfx_pixel(g, x + i, y + j, on);
}
void pp_gfx_circle(pp_gfx *g, int cx, int cy, int r, int on) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        pp_gfx_pixel(g, cx + x, cy + y, on); pp_gfx_pixel(g, cx - x, cy + y, on);
        pp_gfx_pixel(g, cx + x, cy - y, on); pp_gfx_pixel(g, cx - x, cy - y, on);
        pp_gfx_pixel(g, cx + y, cy + x, on); pp_gfx_pixel(g, cx - y, cy + x, on);
        pp_gfx_pixel(g, cx + y, cy - x, on); pp_gfx_pixel(g, cx - y, cy - x, on);
        y++;
        if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
    }
}
void pp_gfx_fill_circle(pp_gfx *g, int cx, int cy, int r, int on) {
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
        pp_gfx_line(g, cx - dx, cy + dy, cx + dx, cy + dy, on);
    }
}
void pp_gfx_triangle(pp_gfx *g, int x0, int y0, int x1, int y1, int x2, int y2, int on) {
    pp_gfx_line(g, x0, y0, x1, y1, on);
    pp_gfx_line(g, x1, y1, x2, y2, on);
    pp_gfx_line(g, x2, y2, x0, y0, on);
}
void pp_gfx_fill_triangle(pp_gfx *g, int x0, int y0, int x1, int y1, int x2, int y2, int on) {
    /* sort by y */
    if (y0 > y1) { swap(&y0, &y1); swap(&x0, &x1); }
    if (y1 > y2) { swap(&y2, &y1); swap(&x2, &x1); }
    if (y0 > y1) { swap(&y0, &y1); swap(&x0, &x1); }
    if (y0 == y2) { int a = x0, b = x0; if (x1 < a) a = x1; if (x1 > b) b = x1; if (x2 < a) a = x2; if (x2 > b) b = x2; pp_gfx_line(g, a, y0, b, y0, on); return; }
    for (int y = y0; y <= y2; y++) {
        int a, b;
        a = (y2 == y0) ? x0 : x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        if (y < y1) b = (y1 == y0) ? x1 : x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        else b = (y2 == y1) ? x2 : x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        if (a > b) swap(&a, &b);
        pp_gfx_line(g, a, y, b, y, on);
    }
}

/* ------------------------------------------------------------------ */
/* fonts                                                               */
/* ------------------------------------------------------------------ */

const pp_font *pp_gfx_font(const char *name) {
    if (!name || !*name) return &pp_fonts[0];
    for (int i = 0; i < pp_nfonts; i++) if (strcmp(pp_fonts[i].name, name) == 0) return &pp_fonts[i];
    return NULL;
}

static const pp_glyph *glyph_of(const pp_font *f, unsigned c) {
    if (c < f->first || c > f->last) return NULL;
    return &f->glyphs[c - f->first];
}

/* decode one UTF-8 character; returns the code point and advances *s */
static unsigned next_char(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    unsigned c = p[0]; int n = 1;
    if (c >= 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) { c = ((c & 7) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); n = 4; }
    else if (c >= 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) { c = ((c & 0xF) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); n = 3; }
    else if (c >= 0xC0 && (p[1] & 0xC0) == 0x80) { c = ((c & 0x1F) << 6) | (p[1] & 0x3F); n = 2; }
    *s += n;
    return c;
}

/* glyph for codepoint c: fills *tmp, returns the bitmap bytes (NULL if the font
 * has no usable glyph). Sparse fonts fall back to their first glyph, like tinyfont. */
static const uint8_t *find_glyph(const pp_font *f, unsigned c, pp_glyph *tmp) {
    if (f->map) {
        int lo = 0, hi = f->nmap - 1, hit = -1;
        while (lo <= hi) {
            int m = (lo + hi) / 2;
            const uint8_t *r = f->map + m * 6;
            unsigned rc = ((unsigned)r[0] << 16) | ((unsigned)r[1] << 8) | r[2];
            if (rc < c) lo = m + 1;
            else if (rc > c) hi = m - 1;
            else { hit = m; break; }
        }
        const uint8_t *rec = f->map + (hit >= 0 ? hit : 0) * 6;
        uint32_t off = ((uint32_t)rec[3] << 16) | ((uint32_t)rec[4] << 8) | rec[5];
        const uint8_t *d = f->data + off;
        tmp->off = 0; tmp->w = d[0]; tmp->h = d[1]; tmp->adv = d[2];
        tmp->xo = (int8_t)d[3]; tmp->yo = (int8_t)d[4];
        return d + 5;
    }
    const pp_glyph *gl = glyph_of(f, c);
    if (!gl) gl = glyph_of(f, '?');
    if (!gl) return NULL;
    *tmp = *gl;
    return f->bitmap + gl->off;
}

int pp_gfx_text(pp_gfx *g, const pp_font *f, int x, int y, const char *s, int on, int scale) {
    if (scale < 1) scale = 1;
    int start = x;
    while (*s) {
        unsigned c = next_char(&s);
        pp_glyph gl;
        const uint8_t *bm = find_glyph(f, c, &gl);
        if (!bm) continue;
        unsigned bits = 0, bit = 0;
        for (int yy = 0; yy < gl.h; yy++) {
            for (int xx = 0; xx < gl.w; xx++) {
                if (!(bit & 7)) bits = *bm++;
                bit++;
                if (bits & 0x80) {
                    if (scale == 1) pp_gfx_pixel(g, x + gl.xo + xx, y + gl.yo + yy, on);
                    else pp_gfx_fill_rect(g, x + (gl.xo + xx) * scale, y + (gl.yo + yy) * scale, scale, scale, on);
                }
                bits <<= 1;
            }
        }
        x += gl.adv * scale;
    }
    return x - start;
}
int pp_gfx_text_width(const pp_font *f, const char *s, int scale) {
    if (scale < 1) scale = 1;
    int w = 0;
    while (*s) { pp_glyph gl; if (find_glyph(f, next_char(&s), &gl)) w += gl.adv * scale; }
    return w;
}

/* ------------------------------------------------------------------ */
/* natives: package Gfx (frame buffers by handle)                      */
/* ------------------------------------------------------------------ */

#define MAX_GFX 4
static pp_gfx *gfxs[MAX_GFX];

pp_gfx *pp_gfx_handle(int h) { return (h >= 0 && h < MAX_GFX) ? gfxs[h] : NULL; }

void pp_gfx_reset(void) {
    for (int i = 0; i < MAX_GFX; i++) { pp_gfx_free(gfxs[i]); gfxs[i] = NULL; }
}

#define ARG(i) ((int)pp_list_arg_int(a, i, 0))
#define NATIVE(name) static pp_val name(pp_interp *I, pp_list *a, int ctx, pp_list *out)
#define UNUSED (void)ctx; (void)out
static pp_gfx *gfx_arg(pp_interp *I, pp_list *a) {
    pp_gfx *g = pp_gfx_handle(a->n ? (int)pp_to_int(a->v[0]) : -1);
    if (!g) pp_die(I, "Gfx: bad frame buffer handle");
    return g;
}
static const char *str_arg(pp_list *a, int i, pp_str **tmp) {
    *tmp = NULL;
    if (i >= a->n) return "";
    return pp_to_cstr(a->v[i], tmp);
}
static void str_done(pp_str *tmp) { if (tmp) { pp_val t; t.t = PP_STR; t.u.s = tmp; pp_release(t); } }

/* Gfx::_new($w, $h) -> handle */
NATIVE(n_new) {
    UNUSED;
    for (int i = 0; i < MAX_GFX; i++) if (!gfxs[i]) { gfxs[i] = pp_gfx_new(ARG(0), ARG(1)); if (!gfxs[i]) return pp_die(I, "Gfx: bad size"); return pp_int(i); }
    return pp_die(I, "Gfx: too many frame buffers");
}
NATIVE(n_free) { UNUSED; (void)I; int h = ARG(0); if (h >= 0 && h < MAX_GFX) { pp_gfx_free(gfxs[h]); gfxs[h] = NULL; } return pp_undef(); }
NATIVE(n_clear) { UNUSED; pp_gfx *g = gfx_arg(I, a); if (g) pp_gfx_clear(g); return pp_undef(); }
NATIVE(n_width) { UNUSED; pp_gfx *g = gfx_arg(I, a); if (!g) return pp_undef(); int w, h; pp_gfx_size(g, &w, &h); return pp_int(w); }
NATIVE(n_height) { UNUSED; pp_gfx *g = gfx_arg(I, a); if (!g) return pp_undef(); int w, h; pp_gfx_size(g, &w, &h); return pp_int(h); }
NATIVE(n_rotation) { UNUSED; pp_gfx *g = gfx_arg(I, a); if (!g) return pp_undef(); if (a->n > 1) g->rot = ARG(1) & 3; return pp_int(g->rot); }
NATIVE(n_pixel) { UNUSED; pp_gfx *g = gfx_arg(I, a); if (g) pp_gfx_pixel(g, ARG(1), ARG(2), a->n > 3 ? ARG(3) : 1); return pp_undef(); }
NATIVE(n_get_pixel) { UNUSED; pp_gfx *g = gfx_arg(I, a); return g ? pp_int(pp_gfx_get_pixel(g, ARG(1), ARG(2))) : pp_undef(); }
NATIVE(n_line) { UNUSED; pp_gfx *g = gfx_arg(I, a); if (g) pp_gfx_line(g, ARG(1), ARG(2), ARG(3), ARG(4), a->n > 5 ? ARG(5) : 1); return pp_undef(); }
NATIVE(n_rect) { UNUSED; pp_gfx *g = gfx_arg(I, a); if (g) pp_gfx_rect(g, ARG(1), ARG(2), ARG(3), ARG(4), a->n > 5 ? ARG(5) : 1); return pp_undef(); }
NATIVE(n_fill_rect) { UNUSED; pp_gfx *g = gfx_arg(I, a); if (g) pp_gfx_fill_rect(g, ARG(1), ARG(2), ARG(3), ARG(4), a->n > 5 ? ARG(5) : 1); return pp_undef(); }
NATIVE(n_circle) { UNUSED; pp_gfx *g = gfx_arg(I, a); if (g) pp_gfx_circle(g, ARG(1), ARG(2), ARG(3), a->n > 4 ? ARG(4) : 1); return pp_undef(); }
NATIVE(n_fill_circle) { UNUSED; pp_gfx *g = gfx_arg(I, a); if (g) pp_gfx_fill_circle(g, ARG(1), ARG(2), ARG(3), a->n > 4 ? ARG(4) : 1); return pp_undef(); }
NATIVE(n_triangle) { UNUSED; pp_gfx *g = gfx_arg(I, a); if (g) pp_gfx_triangle(g, ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), a->n > 7 ? ARG(7) : 1); return pp_undef(); }
NATIVE(n_fill_triangle) { UNUSED; pp_gfx *g = gfx_arg(I, a); if (g) pp_gfx_fill_triangle(g, ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), a->n > 7 ? ARG(7) : 1); return pp_undef(); }
/* Gfx::_text($h, $font, $x, $y, $string, $on, $scale) -> advance in pixels */
NATIVE(n_text) {
    UNUSED;
    pp_gfx *g = gfx_arg(I, a); if (!g) return pp_undef();
    pp_str *t1, *t2;
    const char *fname = str_arg(a, 1, &t1);
    const pp_font *f = pp_gfx_font(fname);
    if (!f) { pp_val e = pp_die(I, "Gfx: unknown font '%s'", fname); str_done(t1); return e; }
    const char *s = str_arg(a, 4, &t2);
    int adv = pp_gfx_text(g, f, ARG(2), ARG(3), s, a->n > 5 ? ARG(5) : 1, a->n > 6 ? ARG(6) : 1);
    str_done(t1); str_done(t2);
    return pp_int(adv);
}
/* Gfx::_text_width($font, $string, $scale) */
NATIVE(n_text_width) {
    UNUSED;
    pp_str *t1, *t2;
    const char *fname = str_arg(a, 0, &t1);
    const pp_font *f = pp_gfx_font(fname);
    if (!f) { pp_val e = pp_die(I, "Gfx: unknown font '%s'", fname); str_done(t1); return e; }
    const char *s = str_arg(a, 1, &t2);
    int w = pp_gfx_text_width(f, s, a->n > 2 ? ARG(2) : 1);
    str_done(t1); str_done(t2);
    return pp_int(w);
}
/* Gfx::_font_height($font) -> line advance */
NATIVE(n_font_height) {
    UNUSED;
    pp_str *t1; const char *fname = str_arg(a, 0, &t1);
    const pp_font *f = pp_gfx_font(fname);
    str_done(t1);
    return f ? pp_int(f->yadv) : pp_undef();
}
/* Gfx::_fonts() -> comma separated font names */
NATIVE(n_fonts) {
    UNUSED; (void)I; (void)a;
    pp_str *r = pp_str_new(NULL, 0);
    for (int i = 0; i < pp_nfonts; i++) {
        int l = (int)strlen(pp_fonts[i].name);
        r = pp_str_reserve(r, l + 1);
        if (i) r->d[r->len++] = ',';
        memcpy(r->d + r->len, pp_fonts[i].name, l); r->len += l; r->d[r->len] = 0;
    }
    return pp_str_val(r);
}

void pp_gfx_register(pp_interp *I) {
    pp_register(I, "Gfx::_new", n_new);
    pp_register(I, "Gfx::_free", n_free);
    pp_register(I, "Gfx::_clear", n_clear);
    pp_register(I, "Gfx::_width", n_width);
    pp_register(I, "Gfx::_height", n_height);
    pp_register(I, "Gfx::_rotation", n_rotation);
    pp_register(I, "Gfx::_pixel", n_pixel);
    pp_register(I, "Gfx::_get_pixel", n_get_pixel);
    pp_register(I, "Gfx::_line", n_line);
    pp_register(I, "Gfx::_rect", n_rect);
    pp_register(I, "Gfx::_fill_rect", n_fill_rect);
    pp_register(I, "Gfx::_circle", n_circle);
    pp_register(I, "Gfx::_fill_circle", n_fill_circle);
    pp_register(I, "Gfx::_triangle", n_triangle);
    pp_register(I, "Gfx::_fill_triangle", n_fill_triangle);
    pp_register(I, "Gfx::_text", n_text);
    pp_register(I, "Gfx::_text_width", n_text_width);
    pp_register(I, "Gfx::_font_height", n_font_height);
    pp_register(I, "Gfx::_fonts", n_fonts);
}

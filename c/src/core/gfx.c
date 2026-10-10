#include "gfx.h"

#include <string.h>

uint16_t gfx_fb[SCREEN_LONG * SCREEN_SHORT];
int gfx_w = SCREEN_LONG, gfx_h = SCREEN_SHORT;
static orient_t orientation = ORIENT_LANDSCAPE;

void gfx_set_orientation(orient_t o) {
    orientation = o;
    gfx_w = o == ORIENT_LANDSCAPE ? SCREEN_LONG : SCREEN_SHORT;
    gfx_h = o == ORIENT_LANDSCAPE ? SCREEN_SHORT : SCREEN_LONG;
}

orient_t gfx_orientation(void) { return orientation; }

uint16_t rgb_lerp(uint16_t a, uint16_t b, int t) {
    if (t <= 0) return a;
    if (t >= 255) return b;
    int r = rgb_r(a) + (rgb_r(b) - rgb_r(a)) * t / 255;
    int g = rgb_g(a) + (rgb_g(b) - rgb_g(a)) * t / 255;
    int bl = rgb_b(a) + (rgb_b(b) - rgb_b(a)) * t / 255;
    return rgb((uint8_t)r, (uint8_t)g, (uint8_t)bl);
}

uint16_t rgb_hsv(int h, int s, int v) {
    h %= 360;
    if (h < 0) h += 360;
    int region = h / 60, rem = (h % 60) * 255 / 60;
    int p = v * (255 - s) / 255;
    int q = v * (255 - s * rem / 255) / 255;
    int t = v * (255 - s * (255 - rem) / 255) / 255;
    switch (region) {
    case 0: return rgb((uint8_t)v, (uint8_t)t, (uint8_t)p);
    case 1: return rgb((uint8_t)q, (uint8_t)v, (uint8_t)p);
    case 2: return rgb((uint8_t)p, (uint8_t)v, (uint8_t)t);
    case 3: return rgb((uint8_t)p, (uint8_t)q, (uint8_t)v);
    case 4: return rgb((uint8_t)t, (uint8_t)p, (uint8_t)v);
    default: return rgb((uint8_t)v, (uint8_t)p, (uint8_t)q);
    }
}

void gfx_clear(uint16_t c) {
    int n = SCREEN_LONG * SCREEN_SHORT;
    for (int i = 0; i < n; i++) gfx_fb[i] = c;
}

void gfx_pixel(int x, int y, uint16_t c) {
    if ((unsigned)x < (unsigned)gfx_w && (unsigned)y < (unsigned)gfx_h) gfx_fb[y * gfx_w + x] = c;
}

uint16_t gfx_get(int x, int y) {
    if ((unsigned)x < (unsigned)gfx_w && (unsigned)y < (unsigned)gfx_h) return gfx_fb[y * gfx_w + x];
    return 0;
}

void gfx_rect(int x, int y, int w, int h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > gfx_w) w = gfx_w - x;
    if (y + h > gfx_h) h = gfx_h - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        uint16_t *p = &gfx_fb[(y + j) * gfx_w + x];
        for (int i = 0; i < w; i++) p[i] = c;
    }
}

void gfx_hline(int x, int y, int w, uint16_t c) { gfx_rect(x, y, w, 1, c); }
void gfx_vline(int x, int y, int h, uint16_t c) { gfx_rect(x, y, 1, h, c); }

void gfx_frame(int x, int y, int w, int h, uint16_t c) {
    gfx_hline(x, y, w, c);
    gfx_hline(x, y + h - 1, w, c);
    gfx_vline(x, y, h, c);
    gfx_vline(x + w - 1, y, h, c);
}

void gfx_round_rect(int x, int y, int w, int h, int r, uint16_t c) {
    gfx_rect(x + r, y, w - 2 * r, h, c);
    for (int j = 0; j < r; j++) {
        // Inset for row j of the corner, so the corners are quarter circles.
        int dy = r - j;
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r) dx++;
        int inset = r - dx;
        gfx_rect(x + inset, y + j, r - inset, 1, c);
        gfx_rect(x + w - r, y + j, r - inset, 1, c);
        gfx_rect(x + inset, y + h - 1 - j, r - inset, 1, c);
        gfx_rect(x + w - r, y + h - 1 - j, r - inset, 1, c);
    }
    gfx_rect(x, y + r, r, h - 2 * r, c);
    gfx_rect(x + w - r, y + r, r, h - 2 * r, c);
}

void gfx_line(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_circle(int cx, int cy, int r, uint16_t c) {
    for (int y = -r; y <= r; y++) {
        int x = 0;
        while ((x + 1) * (x + 1) + y * y <= r * r + r) x++;
        gfx_hline(cx - x, cy + y, 2 * x + 1, c);
    }
}

void gfx_ring(int cx, int cy, int r, uint16_t c) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        gfx_pixel(cx + x, cy + y, c); gfx_pixel(cx - x, cy + y, c);
        gfx_pixel(cx + x, cy - y, c); gfx_pixel(cx - x, cy - y, c);
        gfx_pixel(cx + y, cy + x, c); gfx_pixel(cx - y, cy + x, c);
        gfx_pixel(cx + y, cy - x, c); gfx_pixel(cx - y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void gfx_blend_rect(int x, int y, int w, int h, uint16_t c, int alpha) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > gfx_w) w = gfx_w - x;
    if (y + h > gfx_h) h = gfx_h - y;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            uint16_t *p = &gfx_fb[(y + j) * gfx_w + x + i];
            *p = rgb_lerp(*p, c, alpha);
        }
}

void gfx_vgradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom) {
    for (int j = 0; j < h; j++) gfx_hline(x, y + j, w, rgb_lerp(top, bottom, h > 1 ? j * 255 / (h - 1) : 0));
}

static int glyph_draw(int x, int y, unsigned ch, uint16_t c, const font_t *f, int scale) {
    if (ch < f->first || ch > f->last) ch = '?';
    int i = (int)ch - f->first;
    const uint16_t *rows = &f->rows[f->offsets[i]];
    for (int r = 0; r < f->height; r++) {
        uint16_t bits = rows[r];
        for (int b = 0; bits; b++, bits >>= 1)
            if (bits & 1) {
                if (scale == 1) gfx_pixel(x + b, y + r, c);
                else gfx_rect(x + b * scale, y + r * scale, scale, scale, c);
            }
    }
    return f->widths[i] * scale;
}

int gfx_text(int x, int y, const char *s, uint16_t c, const font_t *f, int scale) {
    int x0 = x;
    for (; *s; s++) {
        if (*s == '\n') { x = x0; y += (f->height + 1) * scale; continue; }
        x += glyph_draw(x, y, (unsigned char)*s, c, f, scale);
    }
    return x - x0;
}

int gfx_text_width(const char *s, const font_t *f, int scale) {
    int w = 0, best = 0;
    for (; *s; s++) {
        unsigned ch = (unsigned char)*s;
        if (ch == '\n') { if (w > best) best = w; w = 0; continue; }
        if (ch < f->first || ch > f->last) ch = '?';
        w += f->widths[ch - f->first] * scale;
    }
    return w > best ? w : best;
}

void gfx_text_center(int cx, int y, const char *s, uint16_t c, const font_t *f, int scale) {
    gfx_text(cx - gfx_text_width(s, f, scale) / 2, y, s, c, f, scale);
}

void gfx_text_outline(int x, int y, const char *s, uint16_t c, uint16_t outline, const font_t *f, int scale) {
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy) gfx_text(x + dx, y + dy, s, outline, f, scale);
    gfx_text(x, y, s, c, f, scale);
}

void gfx_sprite_cell(const sprite_t *s, int cx, int cy, int cw, int ch, int x, int y, int scale, int flags) {
    for (int j = 0; j < ch; j++) {
        int dy = y + j * scale;
        if (dy + scale <= 0 || dy >= gfx_h) continue;
        const uint8_t *row = &s->pixels[(cy + j) * s->w + cx];
        for (int i = 0; i < cw; i++) {
            uint8_t idx = row[(flags & SPR_FLIP_X) ? cw - 1 - i : i];
            if (!idx) continue;
            uint16_t col = s->palette[idx];
            if (scale == 1) gfx_pixel(x + i, dy, col);
            else gfx_rect(x + i * scale, dy, scale, scale, col);
        }
    }
}

void gfx_sprite(const sprite_t *s, int x, int y, int scale, int flags, uint16_t solid) {
    if (!(flags & SPR_SOLID)) {
        gfx_sprite_cell(s, 0, 0, s->w, s->h, x, y, scale, flags);
        return;
    }
    for (int j = 0; j < s->h; j++)
        for (int i = 0; i < s->w; i++) {
            uint8_t idx = s->pixels[j * s->w + ((flags & SPR_FLIP_X) ? s->w - 1 - i : i)];
            if (idx) gfx_rect(x + i * scale, y + j * scale, scale, scale, solid);
        }
}

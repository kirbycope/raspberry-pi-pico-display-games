// Framebuffer graphics for the Pico Display Pack's 240x135 screen.
//
// Every app draws into one RGB565 framebuffer. Its logical size follows the
// current orientation: 240x135 in landscape, 135x240 in portrait. Nothing is
// drawn to the panel until the frame loop calls hal_present().
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SCREEN_LONG 240
#define SCREEN_SHORT 135

typedef enum { ORIENT_LANDSCAPE, ORIENT_PORTRAIT } orient_t;

extern uint16_t gfx_fb[SCREEN_LONG * SCREEN_SHORT];
extern int gfx_w, gfx_h;

// A macro rather than a function so it works in static initialisers.
#define rgb(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xFF) >> 3)))
static inline uint8_t rgb_r(uint16_t c) { return (uint8_t)(((c >> 11) & 0x1F) * 255 / 31); }
static inline uint8_t rgb_g(uint16_t c) { return (uint8_t)(((c >> 5) & 0x3F) * 255 / 63); }
static inline uint8_t rgb_b(uint16_t c) { return (uint8_t)((c & 0x1F) * 255 / 31); }

// t is 0..255, 0 returns a and 255 returns b.
uint16_t rgb_lerp(uint16_t a, uint16_t b, int t);
// h is 0..359, s and v are 0..255.
uint16_t rgb_hsv(int h, int s, int v);

#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_RED rgb(230, 40, 40)
#define C_GREEN rgb(60, 200, 80)
#define C_BLUE rgb(50, 110, 230)
#define C_YELLOW rgb(250, 210, 40)
#define C_ORANGE rgb(250, 140, 30)
#define C_PURPLE rgb(160, 80, 220)
#define C_CYAN rgb(40, 200, 220)
#define C_GREY rgb(128, 128, 128)
#define C_DARK rgb(24, 24, 32)

void gfx_set_orientation(orient_t o);
orient_t gfx_orientation(void);

void gfx_clear(uint16_t c);
void gfx_pixel(int x, int y, uint16_t c);
uint16_t gfx_get(int x, int y);
void gfx_hline(int x, int y, int w, uint16_t c);
void gfx_vline(int x, int y, int h, uint16_t c);
void gfx_rect(int x, int y, int w, int h, uint16_t c);
void gfx_frame(int x, int y, int w, int h, uint16_t c);
void gfx_round_rect(int x, int y, int w, int h, int r, uint16_t c);
void gfx_line(int x0, int y0, int x1, int y1, uint16_t c);
void gfx_circle(int cx, int cy, int r, uint16_t c);
void gfx_ring(int cx, int cy, int r, uint16_t c);
// Blends c over what is already there; alpha is 0..255.
void gfx_blend_rect(int x, int y, int w, int h, uint16_t c, int alpha);
// Vertical gradient from top to bottom colour.
void gfx_vgradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom);

// Bitmap fonts made by tools/gen_font.py from Kenney's pixel fonts.
typedef struct {
    uint8_t height;      // rows per glyph
    uint8_t first, last; // character range
    const uint8_t *widths;   // advance per glyph
    const uint16_t *offsets; // index into rows per glyph
    const uint16_t *rows;    // one row per entry, bit 0 = leftmost pixel
} font_t;

extern const font_t font_mini;   // mixed case, 8 px line
extern const font_t font_blocks; // capitals, chunky, for titles

int gfx_text(int x, int y, const char *s, uint16_t c, const font_t *f, int scale);
int gfx_text_width(const char *s, const font_t *f, int scale);
void gfx_text_center(int cx, int y, const char *s, uint16_t c, const font_t *f, int scale);
// Text with a one pixel outline, readable over busy art.
void gfx_text_outline(int x, int y, const char *s, uint16_t c, uint16_t outline, const font_t *f, int scale);

// Indexed sprites made by tools/gen_sprites.py. Index 0 is transparent.
typedef struct {
    uint16_t w, h;
    const uint16_t *palette;
    const uint8_t *pixels;
} sprite_t;

#define SPR_FLIP_X 1
// Draws every opaque pixel in one colour, for hit flashes and silhouettes.
#define SPR_SOLID 2

void gfx_sprite(const sprite_t *s, int x, int y, int scale, int flags, uint16_t solid);
// Draws a w x h cell of a sheet sprite, for tile sheets.
void gfx_sprite_cell(const sprite_t *s, int cx, int cy, int cw, int ch, int x, int y, int scale, int flags);

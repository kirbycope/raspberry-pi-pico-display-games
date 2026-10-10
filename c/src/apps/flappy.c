// Flappy Bird: one button to flap through the gaps between green pipes.
// The sky scrolls in layers (clouds, a skyline, bushes) behind the pipes,
// the score is drawn big at the top, and the game over panel awards a medal.
// The best score is kept across power cycles.
//
// A, B, X or Y: flap (and start, and play again after a crash)
#include "flappy.h"

#include <stdio.h>
#include <string.h>

#include "../core/app.h"
#include "../core/hal.h"
#include "../core/save.h"

// ---------------------------------------------------------------- rules

int flappy_next_gap(int prev) {
    int lo = prev - FLAP_GAP_STEP, hi = prev + FLAP_GAP_STEP;
    if (lo < FLAP_GAP_MIN) lo = FLAP_GAP_MIN;
    if (hi > FLAP_GAP_MAX) hi = FLAP_GAP_MAX;
    if (hi < lo) hi = lo = (FLAP_GAP_MIN + FLAP_GAP_MAX) / 2;
    return lo + (int)(hal_random() % (uint32_t)(hi - lo + 1));
}

void flappy_reset(flappy_t *f, int screen_w) {
    memset(f, 0, sizeof *f);
    f->y = (FLAP_GROUND_Y / 2 - 8) << 8;
    int gap = (FLAP_GAP_MIN + FLAP_GAP_MAX) / 2;
    for (int i = 0; i < FLAP_PIPES; i++) {
        gap = flappy_next_gap(gap);
        f->pipe[i].x = (screen_w + 40 + i * FLAP_PIPE_SPACING) << 8;
        f->pipe[i].gap_y = (int16_t)gap;
    }
}

void flappy_flap(flappy_t *f) { f->vy = FLAP_IMPULSE * 256; }

void flappy_fall(flappy_t *f, uint32_t dt) {
    int32_t v0 = f->vy;
    int32_t v1 = v0 + FLAP_GRAVITY * 256 / 1000 * (int32_t)dt;
    if (v1 > FLAP_MAX_FALL * 256) v1 = FLAP_MAX_FALL * 256;
    // The average of the two speeds is exact for steady acceleration, so the
    // arc does not depend on the frame rate.
    f->y += (v0 + v1) / 2 * (int32_t)dt / 1000;
    f->vy = v1;
    if (f->y < FLAP_CEILING * 256) {
        f->y = FLAP_CEILING * 256;
        if (f->vy < 0) f->vy = 0;
    }
}

int flappy_scroll(flappy_t *f, uint32_t dt) {
    int32_t step = FLAP_SPEED * 256 * (int32_t)dt / 1000;
    int points = 0;
    f->scroll += (uint32_t)step;
    for (int i = 0; i < FLAP_PIPES; i++) {
        flappy_pipe_t *p = &f->pipe[i];
        p->x -= step;
        // A point as the bird's centre passes the middle of the pipe.
        if (!p->scored && p->x + (FLAP_PIPE_W << 7) < (FLAP_BIRD_X << 8)) {
            p->scored = true;
            points++;
        }
    }
    for (int i = 0; i < FLAP_PIPES; i++) {
        flappy_pipe_t *p = &f->pipe[i];
        if (p->x + ((FLAP_PIPE_W + 4) << 8) >= 0) continue;
        // Goes to the back of the row, after the pipe furthest right.
        int last = 0;
        for (int j = 1; j < FLAP_PIPES; j++)
            if (f->pipe[j].x > f->pipe[last].x) last = j;
        p->x = f->pipe[last].x + (FLAP_PIPE_SPACING << 8);
        p->gap_y = (int16_t)flappy_next_gap(f->pipe[last].gap_y);
        p->scored = false;
    }
    f->score += (uint32_t)points;
    return points;
}

bool flappy_hits_pipe(const flappy_t *f) {
    int top = (f->y >> 8) - FLAP_BIRD_HH, bottom = (f->y >> 8) + FLAP_BIRD_HH;
    int left = FLAP_BIRD_X - FLAP_BIRD_HW, right = FLAP_BIRD_X + FLAP_BIRD_HW;
    for (int i = 0; i < FLAP_PIPES; i++) {
        int px = f->pipe[i].x >> 8;
        // The caps, which line the gap, are two pixels wider each side.
        if (right < px - 2 || left > px + FLAP_PIPE_W + 1) continue;
        if (top < f->pipe[i].gap_y || bottom >= f->pipe[i].gap_y + FLAP_GAP) return true;
    }
    return false;
}

bool flappy_hits_ground(const flappy_t *f) { return (f->y >> 8) + FLAP_BIRD_HH >= FLAP_GROUND_Y; }

int flappy_medal(uint32_t score) { return score >= 40 ? 3 : score >= 20 ? 2 : score >= 10 ? 1 : 0; }

// ---------------------------------------------------------------- art

// The bird, 17x12. k outline, y yellow, l highlight, o orange belly,
// w white, r beak. The wing is drawn over it in one of three poses.
#define BIRD_W 17
#define BIRD_H 12
static const char *const BIRD[BIRD_H] = {
    "......kkkkkk.....",
    "....kkllyykwwk...",
    "...klyyyykwwwwk..",
    "..klyyyyykwwwkwk.",
    ".kyyyyyyykwwwkwk.",
    ".kyyyyyyyykwwwwk.",
    ".kyyyyyyyyykkkkkk",
    ".kyyyyyyykrrrrrrk",
    ".koyyyyykrkkkkkk.",
    "..koooooookrrrrk.",
    "...kkooooookkkk..",
    ".....kkkkkk......",
};
#define WING_W 7
#define WING_H 5
static const struct { int row; const char *px[WING_H]; } WINGS[3] = {
    {2, {".kk....", "kwwk...", "kwwwk..", "kwwwwk.", ".kkkkk."}}, // up
    {4, {".kkkk..", "kwwwwk.", "kwwwwwk", "klwwwk.", ".kkkk.."}}, // level
    {6, {".kkkkk.", "kwwwwk.", "kwwwk..", "kwwk...", ".kk...."}}, // down
};

static uint16_t bird_colour(char c) {
    switch (c) {
    case 'k': return rgb(56, 40, 48);
    case 'y': return rgb(250, 200, 50);
    case 'l': return rgb(255, 240, 150);
    case 'o': return rgb(240, 140, 40);
    case 'w': return rgb(250, 250, 245);
    case 'r': return rgb(240, 90, 50);
    default: return 0;
    }
}

// sin of 0, 5, 10 ... 90 degrees, times 256.
static const int16_t SIN5[19] = {0, 22, 44, 66, 88, 108, 128, 147, 165, 181, 196, 210, 222, 232, 241, 247, 252, 255, 256};

// Scale2x: doubles a sprite, rounding off diagonal steps instead of making
// blocks of them. Run twice before rotating, it keeps a turned bird's
// outline clean (the idea behind RotSprite).
static void scale2x(const char *src, int w, int h, char *dst) {
#define AT(x, y) ((x) < 0 || (y) < 0 || (x) >= w || (y) >= h ? '.' : src[(y) * w + (x)])
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            char p = AT(x, y), a = AT(x, y - 1), b = AT(x + 1, y), c = AT(x - 1, y), d = AT(x, y + 1);
            char *o = &dst[(2 * y) * 2 * w + 2 * x];
            o[0] = (c == a && c != d && a != b) ? a : p;
            o[1] = (a == b && a != c && b != d) ? b : p;
            o[2 * w] = (d == c && d != b && c != a) ? c : p;
            o[2 * w + 1] = (b == d && b != a && d != c) ? d : p;
        }
#undef AT
}

// Draws the bird centred on (cx, cy), turned by deg (multiples of 5, nose
// down positive), with wing pose 0..2.
static void draw_bird(int cx, int cy, int deg, int wing) {
    static char px[BIRD_H][BIRD_W], x2[BIRD_H * 2][BIRD_W * 2], x4[BIRD_H * 4][BIRD_W * 4];
    for (int r = 0; r < BIRD_H; r++) memcpy(px[r], BIRD[r], BIRD_W);
    for (int r = 0; r < WING_H; r++)
        for (int c = 0; c < WING_W; c++)
            if (WINGS[wing].px[r][c] != '.') px[WINGS[wing].row + r][c] = WINGS[wing].px[r][c];

    if (deg == 0) {
        for (int r = 0; r < BIRD_H; r++)
            for (int c = 0; c < BIRD_W; c++)
                if (px[r][c] != '.') gfx_pixel(cx - BIRD_W / 2 + c, cy - BIRD_H / 2 + r, bird_colour(px[r][c]));
        return;
    }
    scale2x(&px[0][0], BIRD_W, BIRD_H, &x2[0][0]);
    scale2x(&x2[0][0], BIRD_W * 2, BIRD_H * 2, &x4[0][0]);
    int a = deg < 0 ? -deg : deg;
    if (a > 90) a = 90;
    int s = SIN5[a / 5], co = SIN5[18 - a / 5];
    if (deg < 0) s = -s;
    // Each screen pixel looks back through the rotation to a point on the
    // four times sprite. Coordinates in 1/256 pixel from the sprite's centre.
    const int R = 11;
    for (int dy = -R; dy <= R; dy++)
        for (int dx = -R; dx <= R; dx++) {
            int qx = dx * 256 + 128, qy = dy * 256 + 128;
            int sx = (qx * co + qy * s) / 64 + BIRD_W * 512;
            int sy = (-qx * s + qy * co) / 64 + BIRD_H * 512;
            if (sx < 0 || sy < 0) continue;
            sx >>= 8;
            sy >>= 8;
            if (sx >= BIRD_W * 4 || sy >= BIRD_H * 4 || x4[sy][sx] == '.') continue;
            gfx_pixel(cx + dx, cy + dy, bird_colour(x4[sy][sx]));
        }
}

#define SKY_TOP rgb(70, 165, 225)
#define SKY_LOW rgb(160, 222, 238)
#define CLOUD_C rgb(232, 247, 250)
#define CLOUD_SHADE rgb(205, 235, 242)
#define CITY_C rgb(150, 210, 200)
#define CITY_WIN rgb(185, 232, 222)
#define BUSH_C rgb(96, 196, 88)
#define BUSH_DARK rgb(78, 170, 72)
#define OUTLINE rgb(56, 40, 48)
#define PANEL_C rgb(222, 216, 150)
#define PANEL_EDGE rgb(200, 160, 90)

// Building heights and widths along a 120 pixel stretch of skyline.
static const uint8_t CITY[][2] = {{14, 26}, {10, 34}, {12, 22}, {8, 40}, {14, 28}, {10, 18}, {12, 31}, {16, 24}, {12, 36}, {12, 20}};
#define CITY_PERIOD 120
#define HORIZON (FLAP_GROUND_Y - 14)

static int wrap(int v, int period) { return ((v % period) + period) % period; }

static void draw_background(uint32_t scroll_px) {
    gfx_vgradient(0, 0, gfx_w, HORIZON - 20, SKY_TOP, SKY_LOW);
    gfx_rect(0, HORIZON - 20, gfx_w, FLAP_GROUND_Y - HORIZON + 20, SKY_LOW);

    // Clouds: a band of puffs, moving at an eighth of the speed.
    int off = wrap((int)(scroll_px / 8), 64);
    for (int x = -off - 32; x < gfx_w + 32; x += 64) {
        static const int8_t puffs[][3] = {{0, 4, 12}, {14, 0, 14}, {30, 6, 10}, {44, 2, 13}, {58, 7, 9}};
        for (int i = 0; i < 5; i++) gfx_circle(x + puffs[i][0], HORIZON - 34 + puffs[i][1], puffs[i][2], CLOUD_SHADE);
        for (int i = 0; i < 5; i++) gfx_circle(x + puffs[i][0] - 1, HORIZON - 35 + puffs[i][1], puffs[i][2] - 1, CLOUD_C);
    }
    gfx_rect(0, HORIZON - 30, gfx_w, 30, CLOUD_C);

    // The skyline, at a quarter of the speed.
    off = wrap((int)(scroll_px / 4), CITY_PERIOD);
    for (int base = -off; base < gfx_w; base += CITY_PERIOD) {
        int x = base;
        for (unsigned i = 0; i < sizeof CITY / sizeof CITY[0]; i++) {
            int w = CITY[i][0], h = CITY[i][1], top = HORIZON - h;
            gfx_rect(x, top, w, h, CITY_C);
            for (int wy = top + 3; wy < HORIZON - 4; wy += 4)
                for (int wx = x + 2; wx < x + w - 2; wx += 3) gfx_rect(wx, wy, 1, 2, CITY_WIN);
            x += w;
        }
    }

    // Bushes, at half the speed.
    off = wrap((int)(scroll_px / 2), 48);
    for (int x = -off - 24; x < gfx_w + 24; x += 48) {
        gfx_circle(x + 6, HORIZON + 4, 10, BUSH_DARK);
        gfx_circle(x + 20, HORIZON + 2, 12, BUSH_DARK);
        gfx_circle(x + 36, HORIZON + 5, 9, BUSH_DARK);
        gfx_circle(x + 6, HORIZON + 5, 9, BUSH_C);
        gfx_circle(x + 20, HORIZON + 3, 11, BUSH_C);
        gfx_circle(x + 36, HORIZON + 6, 8, BUSH_C);
    }
    gfx_rect(0, HORIZON + 6, gfx_w, FLAP_GROUND_Y - HORIZON - 6, BUSH_C);
}

// One column of a pipe, lit from the left.
static uint16_t pipe_shade(int i, int w) {
    if (i == 0 || i == w - 1) return OUTLINE;
    if (i == 1) return rgb(150, 220, 80);
    if (i <= 4) return rgb(215, 250, 140);
    if (i >= w - 3) return rgb(70, 140, 30);
    if (i >= w - 6) return rgb(95, 170, 40);
    return rgb(116, 191, 46);
}

static void draw_pipe(int x, int gap_y) {
    const int cap_w = FLAP_PIPE_W + 4, cap_h = 9;
    int body_top_end = gap_y - cap_h, bottom_start = gap_y + FLAP_GAP + cap_h;
    for (int i = 0; i < FLAP_PIPE_W; i++) {
        uint16_t c = pipe_shade(i, FLAP_PIPE_W);
        gfx_vline(x + i, 0, body_top_end, c);
        gfx_vline(x + i, bottom_start, FLAP_GROUND_Y - bottom_start, c);
    }
    for (int i = 0; i < cap_w; i++) {
        uint16_t c = pipe_shade(i, cap_w);
        gfx_vline(x - 2 + i, body_top_end, cap_h, c);
        gfx_vline(x - 2 + i, gap_y + FLAP_GAP, cap_h, c);
    }
    gfx_hline(x - 2, body_top_end, cap_w, OUTLINE);
    gfx_hline(x - 2, gap_y - 1, cap_w, OUTLINE);
    gfx_hline(x - 2, gap_y + FLAP_GAP, cap_w, OUTLINE);
    gfx_hline(x - 2, bottom_start - 1, cap_w, OUTLINE);
    // A shadow under each lip.
    gfx_hline(x + 1, body_top_end - 1, FLAP_PIPE_W - 2, rgb(70, 140, 30));
    gfx_hline(x + 1, bottom_start, FLAP_PIPE_W - 2, rgb(70, 140, 30));
}

static void draw_ground(uint32_t scroll_px) {
    const int y = FLAP_GROUND_Y;
    gfx_hline(0, y, gfx_w, OUTLINE);
    gfx_hline(0, y + 1, gfx_w, rgb(225, 250, 160));
    // Diagonal stripes, scrolling with the pipes.
    int off = (int)(scroll_px % 12);
    for (int r = 0; r < 7; r++)
        for (int x = 0; x < gfx_w; x++) {
            int k = wrap(x + off + r, 12);
            gfx_pixel(x, y + 2 + r, k < 6 ? rgb(155, 228, 89) : rgb(115, 192, 48));
        }
    gfx_hline(0, y + 9, gfx_w, rgb(85, 128, 40));
    gfx_hline(0, y + 10, gfx_w, rgb(214, 168, 80));
    gfx_rect(0, y + 11, gfx_w, gfx_h - y - 11, PANEL_C);
    // Specks in the sand.
    for (int x = 0; x < gfx_w + 20; x += 20) {
        int sx = x - (int)(scroll_px % 20);
        gfx_pixel(sx + 3, y + 15, rgb(200, 190, 120));
        gfx_pixel(sx + 13, y + 20, rgb(200, 190, 120));
    }
}

// font_blocks draws each letter as an outline. This fills the inside too,
// so a title reads as solid letters: fill inside, edge on the font's own
// pixels, and a one pixel outer rim plus a drop shadow in outer.
static void text_title(int cx, int y, const char *s, uint16_t fill, uint16_t edge, uint16_t outer, int scale) {
    const font_t *f = &font_blocks;
    int x0 = cx - gfx_text_width(s, f, scale) / 2;
    for (int pass = 0; pass < 2; pass++) {
        int x = x0;
        for (const char *p = s; *p; p++) {
            int i = (unsigned char)*p - f->first;
            int w = f->widths[i], h = f->height;
            const uint16_t *rows = &f->rows[f->offsets[i]];
            // Flood the outside from a one cell border; the rest is the letter.
            uint8_t cell[10][12] = {{0}}; // 0 unknown, 1 lit, 2 outside
            for (int r = 0; r < h; r++)
                for (int c = 0; c < w; c++)
                    if (rows[r] >> c & 1) cell[r + 1][c + 1] = 1;
            bool grew = true;
            for (int r = 0; r < h + 2; r++) cell[r][0] = cell[r][w + 1] = 2;
            for (int c = 0; c < w + 2; c++) cell[0][c] = cell[h + 1][c] = 2;
            while (grew) {
                grew = false;
                for (int r = 1; r <= h; r++)
                    for (int c = 1; c <= w; c++)
                        if (!cell[r][c] && (cell[r - 1][c] == 2 || cell[r + 1][c] == 2 || cell[r][c - 1] == 2 || cell[r][c + 1] == 2)) {
                            cell[r][c] = 2;
                            grew = true;
                        }
            }
            for (int r = 1; r <= h; r++)
                for (int c = 1; c <= w; c++) {
                    if (cell[r][c] == 2) continue;
                    int px = x + (c - 1) * scale, py = y + (r - 1) * scale;
                    if (pass == 0) gfx_rect(px - 1, py - 1, scale + 2, scale + 3, outer);
                    else gfx_rect(px, py, scale, scale, cell[r][c] == 1 ? edge : fill);
                }
            x += w * scale;
        }
    }
}

// Small centred text with a drop shadow.
static void text_small(int cx, int y, const char *s, uint16_t fill, uint16_t shadow) {
    int x = cx - gfx_text_width(s, &font_mini, 1) / 2;
    gfx_text(x + 1, y + 1, s, shadow, &font_mini, 1);
    gfx_text(x, y, s, fill, &font_mini, 1);
}

// Big score digits: font_mini's digits blown up three times, white with a
// thick dark outline and a drop shadow, like the original.
static void draw_score(int cx, int y, uint32_t v, int scale) {
    char buf[12];
    snprintf(buf, sizeof buf, "%lu", (unsigned long)v);
    const font_t *f = &font_mini;
    int w = 0;
    for (char *p = buf; *p; p++) w += (f->widths[*p - f->first]) * scale;
    int x0 = cx - (w - scale) / 2;
    for (int pass = 0; pass < 2; pass++) {
        int x = x0;
        for (char *p = buf; *p; p++) {
            int i = *p - f->first;
            const uint16_t *rows = &f->rows[f->offsets[i]];
            for (int r = 0; r < f->height; r++)
                for (int b = 0; b < 8; b++) {
                    if (!(rows[r] >> b & 1)) continue;
                    int px = x + b * scale, py = y + r * scale;
                    if (pass == 0) gfx_rect(px - 2, py - 2, scale + 4, scale + 5, OUTLINE);
                    else gfx_rect(px, py, scale, scale, C_WHITE);
                }
            x += f->widths[i] * scale;
        }
    }
}

static void draw_medal(int cx, int cy, int medal) {
    static const uint16_t face[4] = {rgb(205, 196, 130), rgb(214, 132, 62), rgb(205, 210, 220), rgb(250, 205, 50)};
    static const uint16_t rim[4] = {rgb(190, 180, 115), rgb(160, 92, 40), rgb(150, 155, 170), rgb(205, 150, 20)};
    if (!medal) {
        gfx_circle(cx, cy, 11, rim[0]);
        gfx_circle(cx, cy, 9, face[0]);
        return;
    }
    gfx_circle(cx, cy + 1, 12, OUTLINE);
    gfx_circle(cx, cy, 11, rim[medal]);
    gfx_circle(cx, cy, 9, face[medal]);
    gfx_ring(cx, cy, 7, rim[medal]);
    // A star in the middle and a glint on the rim.
    uint16_t hi = rgb_lerp(face[medal], C_WHITE, 170);
    gfx_vline(cx, cy - 4, 9, hi);
    gfx_hline(cx - 4, cy, 9, hi);
    gfx_rect(cx - 1, cy - 1, 3, 3, hi);
    gfx_pixel(cx - 2, cy - 2, hi);
    gfx_pixel(cx + 2, cy - 2, hi);
    gfx_pixel(cx - 2, cy + 2, hi);
    gfx_pixel(cx + 2, cy + 2, hi);
    gfx_rect(cx - 7, cy - 6, 2, 2, C_WHITE);
}

// ---------------------------------------------------------------- app

enum { ST_READY, ST_PLAY, ST_DYING, ST_OVER };
#define RESTART_MS 700

static flappy_t game;
static int state;
static bool first_ready;
static bool new_best;
static uint32_t state_ms, anim_ms, flash_ms, led_ms;
static uint32_t idle_scroll; // keeps the ground moving before the game starts
static uint8_t led_rgb[3];

static void led_flash(uint8_t r, uint8_t g, uint8_t b, uint32_t ms) {
    led_rgb[0] = r;
    led_rgb[1] = g;
    led_rgb[2] = b;
    led_ms = ms;
}

static void get_ready(void) {
    flappy_reset(&game, gfx_w);
    state = ST_READY;
    state_ms = 0;
    new_best = false;
}

static void flappy_enter(void) {
    first_ready = true;
    anim_ms = flash_ms = led_ms = idle_scroll = 0;
    get_ready();
}

static void crash(void) {
    state = ST_DYING;
    state_ms = 0;
    flash_ms = 160;
    led_flash(150, 0, 0, 500);
    if (game.vy < 0) game.vy = 0;
}

static void game_over(void) {
    state = ST_OVER;
    state_ms = 0;
    if (game.score > g_save.flappy_best) {
        g_save.flappy_best = game.score;
        new_best = true;
    }
}

// Nose up after a flap, tipping over as the fall speeds up.
static int bird_angle(void) {
    int v = game.vy / 256;
    int deg = v < 60 ? -20 : -20 + (v - 60) * 110 / 180;
    if (deg > 90) deg = 90;
    return deg / 5 * 5;
}

static void draw_over_panel(void) {
    // The title drops in, then the panel rises.
    int t = (int)state_ms;
    int title_y = t < 250 ? 30 + (t - 250) / 8 : 30;
    text_title(gfx_w / 2, title_y, "GAME OVER", rgb(252, 160, 60), C_WHITE, OUTLINE, 2);

    if (t < 250) return;
    int py = 64 + (t < 500 ? (500 - t) * 160 / 250 : 0);
    const int px = 7, pw = gfx_w - 14, ph = 74;
    gfx_round_rect(px - 1, py - 1, pw + 2, ph + 3, 5, OUTLINE);
    gfx_round_rect(px, py, pw, ph, 4, PANEL_EDGE);
    gfx_round_rect(px + 2, py + 2, pw - 4, ph - 5, 3, PANEL_C);

    gfx_text_center(px + 28, py + 8, "MEDAL", rgb(220, 120, 70), &font_mini, 1);
    draw_medal(px + 28, py + 40, flappy_medal(game.score));

    char buf[16];
    int rx = px + pw - 8;
    gfx_text(rx - gfx_text_width("SCORE", &font_mini, 1), py + 8, "SCORE", rgb(220, 120, 70), &font_mini, 1);
    snprintf(buf, sizeof buf, "%lu", (unsigned long)game.score);
    gfx_text_outline(rx - gfx_text_width(buf, &font_mini, 2), py + 17, buf, C_WHITE, OUTLINE, &font_mini, 2);
    gfx_text(rx - gfx_text_width("BEST", &font_mini, 1), py + 38, "BEST", rgb(220, 120, 70), &font_mini, 1);
    snprintf(buf, sizeof buf, "%lu", (unsigned long)g_save.flappy_best);
    int bw = gfx_text_width(buf, &font_mini, 2);
    gfx_text_outline(rx - bw, py + 47, buf, C_WHITE, OUTLINE, &font_mini, 2);
    if (new_best) {
        int nx = rx - bw - 22;
        gfx_round_rect(nx, py + 49, 18, 9, 2, rgb(240, 70, 60));
        gfx_text(nx + 2, py + 51, "NEW", C_WHITE, &font_mini, 1);
    }

    if (t >= RESTART_MS && (t / 400) % 2 == 0)
        text_small(gfx_w / 2, py + ph + 14, "press to play", C_WHITE, OUTLINE);
}

static void draw_ready(void) {
    if (first_ready) {
        text_title(gfx_w / 2, 28, "FLAPPY", rgb(255, 215, 70), C_WHITE, OUTLINE, 2);
        text_title(gfx_w / 2, 50, "BIRD", rgb(255, 215, 70), C_WHITE, OUTLINE, 2);
    } else {
        text_title(gfx_w / 2, 44, "GET READY", rgb(120, 220, 90), C_WHITE, OUTLINE, 2);
    }
    // A hint: the bird goes up when a button is pressed.
    int hx = FLAP_BIRD_X + 40, hy = (game.y >> 8) - 4;
    for (int i = 0; i < 4; i++) gfx_hline(hx - i, hy + i, 1 + 2 * i, C_WHITE);
    gfx_rect(hx - 1, hy + 4, 3, 6, C_WHITE);
    text_small(gfx_w / 2, 140, "press any button", C_WHITE, OUTLINE);
    if (g_save.flappy_best) {
        char buf[24];
        snprintf(buf, sizeof buf, "BEST %lu", (unsigned long)g_save.flappy_best);
        text_small(gfx_w / 2, 154, buf, rgb(255, 225, 90), OUTLINE);
    }
}

static bool flappy_frame(const input_t *in, uint32_t dt) {
    anim_ms += dt;
    state_ms += dt;
    bool tap = in->pressed != 0;

    switch (state) {
    case ST_READY: {
        // Bob gently until the first flap.
        int phase = (int)(anim_ms % 800);
        int bob = phase < 400 ? phase * 6 / 400 : (800 - phase) * 6 / 400;
        game.y = (FLAP_GROUND_Y / 2 - 8 - 3 + bob) << 8;
        idle_scroll += FLAP_SPEED * 256 * dt / 1000;
        if (tap) {
            state = ST_PLAY;
            state_ms = 0;
            first_ready = false;
            flappy_flap(&game);
        }
        break;
    }
    case ST_PLAY:
        if (tap) flappy_flap(&game);
        flappy_fall(&game, dt);
        if (flappy_scroll(&game, dt)) led_flash(0, 90, 0, 120);
        if (flappy_hits_ground(&game)) {
            game.y = (FLAP_GROUND_Y - FLAP_BIRD_HH) << 8;
            crash();
            game_over();
        } else if (flappy_hits_pipe(&game)) {
            crash();
        }
        break;
    case ST_DYING:
        flappy_fall(&game, dt);
        if (flappy_hits_ground(&game)) {
            game.y = (FLAP_GROUND_Y - FLAP_BIRD_HH) << 8;
            game_over();
        }
        break;
    case ST_OVER:
        if (state_ms >= RESTART_MS && tap) get_ready();
        break;
    }

    uint32_t scroll_px = (state == ST_READY ? idle_scroll : game.scroll + idle_scroll) >> 8;
    draw_background(scroll_px);
    if (state != ST_READY)
        for (int i = 0; i < FLAP_PIPES; i++) draw_pipe(game.pipe[i].x >> 8, game.pipe[i].gap_y);
    draw_ground(scroll_px);

    bool alive = state == ST_READY || state == ST_PLAY;
    static const uint8_t wing_cycle[4] = {0, 1, 2, 1};
    int wing = alive ? wing_cycle[(anim_ms / (state == ST_PLAY ? 70 : 120)) % 4] : 1;
    int angle = state == ST_READY ? 0 : bird_angle();
    if (state == ST_OVER) angle = 90;
    // A bird nose down on the ground rests on it rather than in it.
    int bird_y = game.y >> 8;
    if (state == ST_OVER && bird_y > FLAP_GROUND_Y - 9) bird_y = FLAP_GROUND_Y - 9;
    draw_bird(FLAP_BIRD_X, bird_y, angle, wing);

    if (state == ST_READY) draw_ready();
    if (state == ST_PLAY || state == ST_DYING) draw_score(gfx_w / 2, 12, game.score, 3);
    if (state == ST_OVER) draw_over_panel();

    if (flash_ms) {
        flash_ms = flash_ms > dt ? flash_ms - dt : 0;
        gfx_blend_rect(0, 0, gfx_w, gfx_h, C_WHITE, (int)(flash_ms * 220 / 160));
    }
    if (led_ms) {
        led_ms = led_ms > dt ? led_ms - dt : 0;
        hal_led(led_rgb[0], led_rgb[1], led_rgb[2]);
    } else {
        hal_led(0, 0, 0);
    }
    return true;
}

const app_t app_flappy = {
    .name = "Flappy Bird",
    .orient = ORIENT_PORTRAIT,
    .accent = rgb(250, 200, 40),
    .fps = 60,
    .enter = flappy_enter,
    .frame = flappy_frame,
};

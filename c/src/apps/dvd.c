// The bouncing DVD logo. It changes colour at every wall, and a corner hit
// sets off confetti and adds to a count kept across power cycles.
//
// A: faster  B: slower  X: new direction  Y: show or hide the corner count
#include "dvd.h"

#include <stdio.h>

#include "../core/app.h"
#include "../core/hal.h"
#include "../core/save.h"

#define LOGO_W 58
#define LOGO_H 29
#define CONFETTI 40

static dvd_t logo;
static int hue;
static int speed = 50; // pixels per second
static bool show_count = true;
static uint32_t corner_flash_ms;
static struct { int32_t x, y, vx, vy; uint16_t c; int life; } confetti[CONFETTI];

int dvd_step(dvd_t *d, uint32_t dt_ms, int screen_w, int screen_h) {
    int hits = 0;
    d->x += d->vx * (int32_t)dt_ms / 1000;
    d->y += d->vy * (int32_t)dt_ms / 1000;
    int32_t max_x = (int32_t)(screen_w - d->w) << 8, max_y = (int32_t)(screen_h - d->h) << 8;
    if (d->x <= 0) { d->x = -d->x; d->vx = d->vx < 0 ? -d->vx : d->vx; hits |= DVD_HIT_X; }
    if (d->x >= max_x) { d->x = 2 * max_x - d->x; d->vx = d->vx > 0 ? -d->vx : d->vx; hits |= DVD_HIT_X; }
    if (d->y <= 0) { d->y = -d->y; d->vy = d->vy < 0 ? -d->vy : d->vy; hits |= DVD_HIT_Y; }
    if (d->y >= max_y) { d->y = 2 * max_y - d->y; d->vy = d->vy > 0 ? -d->vy : d->vy; hits |= DVD_HIT_Y; }
    return hits;
}

static void set_speed(int pixels_per_second) {
    speed = pixels_per_second < 10 ? 10 : pixels_per_second > 200 ? 200 : pixels_per_second;
    logo.vx = (logo.vx < 0 ? -1 : 1) * speed * 256;
    logo.vy = (logo.vy < 0 ? -1 : 1) * speed * 256 * 3 / 4;
}

static void new_direction(void) {
    logo.vx = (hal_random() & 1) ? 1 : -1;
    logo.vy = (hal_random() & 1) ? 1 : -1;
    set_speed(speed);
}

static void dvd_enter(void) {
    logo.w = LOGO_W;
    logo.h = LOGO_H;
    logo.x = (int32_t)(hal_random() % (SCREEN_LONG - LOGO_W)) << 8;
    logo.y = (int32_t)(hal_random() % (SCREEN_SHORT - LOGO_H)) << 8;
    hue = (int)(hal_random() % 360);
    new_direction();
    corner_flash_ms = 0;
    for (int i = 0; i < CONFETTI; i++) confetti[i].life = 0;
}

static void burst(int x, int y) {
    for (int i = 0; i < CONFETTI; i++) {
        confetti[i].x = x << 8;
        confetti[i].y = y << 8;
        confetti[i].vx = (int32_t)(hal_random() % 241) - 120;
        confetti[i].vy = (int32_t)(hal_random() % 241) - 120;
        confetti[i].c = rgb_hsv((int)(hal_random() % 360), 220, 255);
        confetti[i].life = 900 + (int)(hal_random() % 600);
    }
}

// The logo: a slanted "DVD" over a flattened disc with "VIDEO" cut out of it.
static const char *const LETTER_D[] = {
    "1111111111000", "1111111111110", "1110000001111", "1110000000111", "1110000000111", "1110000000111",
    "1110000000111", "1110000000111", "1110000000111", "1110000001111", "1111111111110", "1111111111000",
};
static const char *const LETTER_V[] = {
    "1110000000111", "1110000000111", "0111000001110", "0111000001110", "0011100011100", "0011100011100",
    "0001110111000", "0001110111000", "0000111110000", "0000111110000", "0000011100000", "0000001000000",
};

static void draw_letter(const char *const rows[12], int x, int y, uint16_t c) {
    for (int r = 0; r < 12; r++) {
        int slant = (11 - r) / 3;
        for (int i = 0; rows[r][i]; i++)
            if (rows[r][i] == '1') gfx_pixel(x + slant + i, y + r, c);
    }
}

static void draw_logo(int x, int y, uint16_t c) {
    draw_letter(LETTER_D, x + 4, y + 1, c);
    draw_letter(LETTER_V, x + 21, y + 1, c);
    draw_letter(LETTER_D, x + 38, y + 1, c);
    int cx = x + LOGO_W / 2, cy = y + 22, rx = 27, ry = 6;
    for (int j = -ry + 1; j < ry; j++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) * ry * ry <= rx * rx * (ry * ry - j * j)) dx++;
        gfx_hline(cx - dx, cy + j, 2 * dx + 1, c);
    }
    gfx_text_center(cx + 1, cy - 3, "VIDEO", C_BLACK, &font_mini, 1);
}

static bool dvd_frame(const input_t *in, uint32_t dt) {
    if (in->pressed & BTN_A) set_speed(speed + 15);
    if (in->pressed & BTN_B) set_speed(speed - 15);
    if (in->pressed & BTN_X) new_direction();
    if (in->pressed & BTN_Y) show_count = !show_count;

    int hits = dvd_step(&logo, dt, SCREEN_LONG, SCREEN_SHORT);
    if (hits) hue = (hue + 67 + (int)(hal_random() % 90)) % 360;
    if ((hits & DVD_CORNER) == DVD_CORNER) {
        g_save.dvd_corners++;
        corner_flash_ms = 1500;
        burst((logo.x >> 8) + LOGO_W / 2, (logo.y >> 8) + LOGO_H / 2);
    }
    if (corner_flash_ms) corner_flash_ms = corner_flash_ms > dt ? corner_flash_ms - dt : 0;

    uint16_t colour = rgb_hsv(hue, 230, 255);
    gfx_clear(C_BLACK);
    for (int i = 0; i < CONFETTI; i++) {
        if (confetti[i].life <= 0) continue;
        confetti[i].life -= (int)dt;
        confetti[i].vy += (int32_t)dt / 4;
        confetti[i].x += confetti[i].vx * (int32_t)dt / 4;
        confetti[i].y += confetti[i].vy * (int32_t)dt / 4;
        gfx_rect(confetti[i].x >> 8, confetti[i].y >> 8, 2, 2, confetti[i].c);
    }
    draw_logo(logo.x >> 8, logo.y >> 8, colour);

    if (corner_flash_ms && (corner_flash_ms / 150) % 2 == 0)
        gfx_text_outline(SCREEN_LONG / 2 - 30, SCREEN_SHORT / 2 - 8, "CORNER!", C_WHITE, colour, &font_blocks, 1);
    if (show_count) {
        char buf[32];
        snprintf(buf, sizeof buf, "corners %lu", (unsigned long)g_save.dvd_corners);
        gfx_text(3, SCREEN_SHORT - 10, buf, rgb(90, 90, 100), &font_mini, 1);
    }
    // The LED glows the logo's colour.
    hal_led(rgb_r(colour) / 4, rgb_g(colour) / 4, rgb_b(colour) / 4);
    return true;
}

// Attract mode always gets its corner: the logo starts on a line that meets
// the bottom-right corner 2.6 seconds in, at the normal speed.
static int real_speed;
static bool real_show_count;

static void dvd_attract(void) {
    real_speed = speed;
    real_show_count = show_count;
    speed = 50;
    logo.vx = speed * 256;
    logo.vy = speed * 256 * 3 / 4;
    int32_t t_ms = 2600;
    logo.x = ((int32_t)(SCREEN_LONG - LOGO_W) << 8) - logo.vx * t_ms / 1000;
    logo.y = ((int32_t)(SCREEN_SHORT - LOGO_H) << 8) - logo.vy * t_ms / 1000;
    show_count = false;
}

// The demo's speed and hidden counter are not the player's.
static void dvd_leave(void) {
    if (!g_attract) return;
    speed = real_speed;
    show_count = real_show_count;
}

const app_t app_dvd = {
    .name = "Bouncing DVD",
    .orient = ORIENT_LANDSCAPE,
    .accent = rgb(170, 60, 230),
    .fps = 30,
    .enter = dvd_enter,
    .frame = dvd_frame,
    .leave = dvd_leave,
    .attract = dvd_attract,
};

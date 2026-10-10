// A Pomodoro timer. Focus for 25 minutes, take a 5 minute break, and after
// every fourth focus a 15 minute one. The dots show how far through the set
// of four you are and the count at the bottom is every focus ever finished.
//
// The clock runs in background(), so it keeps going while other apps are
// open. When a phase runs out the next one waits for A; the screen flashes
// "Break time!" or "Back to work" and so does the LED while the app is open.
//
// A (top left):     start / pause, or start the next phase once one ends
// B (bottom left):  reset the current phase to its full length
// X (top right):    skip to the next phase
// Y (bottom right): focus length, 25 / 50 / 15 minutes. The breaks scale
//                   with it: 5+15, 10+30, 3+10 minutes.
//
// A and B act when let go, and not at all if both were down together, so
// holding A+B to leave the app does not pause or reset the timer.
#include "pomodoro.h"

#include <stdio.h>

#include "../core/app.h"
#include "../core/hal.h"
#include "../core/save.h"

#define MIN_MS 60000u

// Minutes for focus, short break and long break, per preset.
static const uint8_t PRESETS[POMO_PRESETS][3] = {{25, 5, 15}, {50, 10, 30}, {15, 3, 10}};

static pomo_t timer;
static bool timer_ready;

void pomo_init(pomo_t *p, int preset) {
    p->phase = POMO_FOCUS;
    p->set_pos = 0;
    p->preset = (uint8_t)(preset >= 0 && preset < POMO_PRESETS ? preset : 0);
    p->running = false;
    p->done = false;
    p->remaining_ms = pomo_duration(p, POMO_FOCUS);
}

uint32_t pomo_duration(const pomo_t *p, pomo_phase_t phase) {
    return PRESETS[p->preset][phase] * MIN_MS;
}

pomo_phase_t pomo_next_phase(const pomo_t *p) {
    if (p->phase != POMO_FOCUS) return POMO_FOCUS;
    return p->set_pos + 1 >= POMO_SET ? POMO_LONG : POMO_SHORT;
}

// Moves to the next phase at its full length; running is left to the caller.
static void advance(pomo_t *p) {
    pomo_phase_t next = pomo_next_phase(p);
    if (p->phase == POMO_FOCUS) p->set_pos++;
    else if (p->phase == POMO_LONG) p->set_pos = 0;
    p->phase = next;
    p->done = false;
    p->remaining_ms = pomo_duration(p, next);
}

bool pomo_tick(pomo_t *p, uint32_t dt_ms, uint32_t *focus_count) {
    if (!p->running || p->done) return false;
    if (dt_ms < p->remaining_ms) {
        p->remaining_ms -= dt_ms;
        return false;
    }
    p->remaining_ms = 0;
    p->running = false;
    p->done = true;
    if (p->phase == POMO_FOCUS && focus_count) (*focus_count)++;
    return true;
}

void pomo_toggle(pomo_t *p) {
    if (p->done) {
        advance(p);
        p->running = true;
    } else {
        p->running = !p->running;
    }
}

void pomo_reset(pomo_t *p) {
    p->done = false;
    p->running = false;
    p->remaining_ms = pomo_duration(p, p->phase);
}

void pomo_skip(pomo_t *p) {
    bool run = p->running || p->done;
    advance(p);
    p->running = run;
}

void pomo_next_preset(pomo_t *p) {
    p->preset = (uint8_t)((p->preset + 1) % POMO_PRESETS);
    if (!p->done) p->remaining_ms = pomo_duration(p, p->phase);
}

pomo_t *pomodoro_state(void) {
    if (!timer_ready) {
        pomo_init(&timer, 0);
        timer_ready = true;
    }
    return &timer;
}

// ---- the app ----

static uint32_t anim_ms;
static bool chord; // A and B were down together since the last time both were up

static const uint16_t THEME[3] = {rgb(240, 84, 62), rgb(72, 204, 112), rgb(76, 146, 244)};
static const char *const PHASE_NAME[3] = {"Focus", "Short break", "Long break"};

static void pomodoro_background(uint32_t dt) {
    if (pomo_tick(pomodoro_state(), dt, &g_save.pomodoro_done) && timer.phase == POMO_FOCUS)
        save_commit(); // a finished focus is worth keeping even if the board is unplugged
}

static void pomodoro_enter(void) {
    pomodoro_state();
    anim_ms = 0;
    chord = false;
}

// 0..255..0 over period ms.
static int triangle(uint32_t t, uint32_t period) {
    uint32_t p = t % period;
    uint32_t half = period / 2;
    return (int)((p < half ? p : period - p) * 255 / half);
}

// ---- seven segment digits ----

#define DIG_W 30
#define DIG_H 56
#define SEG_HALF 3 // segments are 2 * SEG_HALF + 1 thick
#define SEG_GAP 1

static void seg_h(int x0, int x1, int yc, uint16_t c, uint16_t hi) {
    for (int k = -SEG_HALF; k <= SEG_HALF; k++) {
        int a = k < 0 ? -k : k;
        gfx_hline(x0 + a + SEG_GAP, yc + k, x1 - x0 - 2 * a - 2 * SEG_GAP + 1, k == -1 || k == 0 ? hi : c);
    }
}

static void seg_v(int xc, int y0, int y1, uint16_t c, uint16_t hi) {
    for (int k = -SEG_HALF; k <= SEG_HALF; k++) {
        int a = k < 0 ? -k : k;
        gfx_vline(xc + k, y0 + a + SEG_GAP, y1 - y0 - 2 * a - 2 * SEG_GAP + 1, k == -1 || k == 0 ? hi : c);
    }
}

// Segments a..g as bits 0..6.
static const uint8_t DIGIT_SEGS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

static void draw_digit(int x, int y, int d, uint16_t on, uint16_t on_hi, uint16_t off) {
    int xl = x + SEG_HALF, xr = x + DIG_W - 1 - SEG_HALF;
    int yt = y + SEG_HALF, ym = y + DIG_H / 2, yb = y + DIG_H - 1 - SEG_HALF;
    uint8_t m = DIGIT_SEGS[d % 10];
#define SEG(bit) ((m >> (bit)) & 1 ? on : off), ((m >> (bit)) & 1 ? on_hi : off)
    seg_h(xl, xr, yt, SEG(0));
    seg_v(xr, yt, ym, SEG(1));
    seg_v(xr, ym, yb, SEG(2));
    seg_h(xl, xr, yb, SEG(3));
    seg_v(xl, ym, yb, SEG(4));
    seg_v(xl, yt, ym, SEG(5));
    seg_h(xl, xr, ym, SEG(6));
#undef SEG
}

static void draw_time(uint32_t ms, int y, uint16_t on, uint16_t off, bool colon) {
    uint32_t secs = (ms + 999) / 1000;
    int mm = (int)(secs / 60), ss = (int)(secs % 60);
    if (mm > 99) mm = 99;
    uint16_t hi = rgb_lerp(on, C_WHITE, 90);
    const int pair = 6, colon_w = 16;
    int x = SCREEN_LONG / 2 - (4 * DIG_W + 2 * pair + colon_w) / 2;
    draw_digit(x, y, mm / 10, on, hi, off);
    draw_digit(x + DIG_W + pair, y, mm % 10, on, hi, off);
    int cx = x + 2 * DIG_W + pair + colon_w / 2;
    uint16_t cc = colon ? on : off;
    gfx_round_rect(cx - 3, y + DIG_H / 2 - 12, 7, 7, 2, cc);
    gfx_round_rect(cx - 3, y + DIG_H / 2 + 6, 7, 7, 2, cc);
    x += 2 * DIG_W + pair + colon_w;
    draw_digit(x, y, ss / 10, on, hi, off);
    draw_digit(x + DIG_W + pair, y, ss % 10, on, hi, off);
}

// ---- the rest of the screen ----

static void draw_tomato(int cx, int cy, uint16_t c) {
    gfx_circle(cx, cy + 1, 4, c);
    gfx_pixel(cx - 2, cy - 1, rgb_lerp(c, C_WHITE, 140));
    uint16_t leaf = rgb(80, 190, 80);
    gfx_hline(cx - 2, cy - 3, 5, leaf);
    gfx_vline(cx, cy - 5, 2, leaf);
}

static void draw_cup(int cx, int cy, uint16_t c) {
    gfx_rect(cx - 4, cy - 2, 7, 6, c);
    gfx_vline(cx + 3, cy - 1, 3, c);
    gfx_pixel(cx + 4, cy - 1, c);
    gfx_pixel(cx + 4, cy + 1, c);
    gfx_pixel(cx + 4, cy, c);
    gfx_hline(cx - 4, cy + 4, 7, rgb_lerp(c, C_BLACK, 90));
    // steam
    gfx_pixel(cx - 2, cy - 4, c);
    gfx_pixel(cx - 1, cy - 5, c);
    gfx_pixel(cx + 1, cy - 4, c);
    gfx_pixel(cx + 2, cy - 5, c);
}

// A small key cap with the button's letter, and what it does beside it.
static void hint(int edge_x, int y, bool right, char key, const char *label, uint16_t cap, uint16_t text) {
    char k[2] = {key, 0};
    int cap_x = right ? edge_x - 10 : edge_x;
    gfx_round_rect(cap_x, y, 10, 10, 2, cap);
    gfx_text_center(cap_x + 5, y + 2, k, C_BLACK, &font_mini, 1);
    int w = gfx_text_width(label, &font_mini, 1);
    gfx_text(right ? cap_x - 3 - w : cap_x + 13, y + 2, label, text, &font_mini, 1);
}

static void draw_bar(int x, int y, int w, int h, uint32_t elapsed, uint32_t total, uint16_t c, uint16_t track) {
    gfx_round_rect(x, y, w, h, h / 2, track);
    int fill = total ? (int)(elapsed * (uint32_t)w / total) : 0;
    if (fill > w) fill = w;
    if (fill >= h) gfx_round_rect(x, y, fill, h, h / 2, c);
    else if (fill > 0) gfx_circle(x + h / 2, y + h / 2, h / 2, c);
    if (fill > 0) {
        int hx = x + (fill < h / 2 ? h / 2 : fill - h / 2 - 1);
        gfx_circle(hx, y + h / 2, h / 2 + 1, rgb_lerp(c, C_WHITE, 150));
    }
}

static void draw_dots(int cy, int filled, bool current, uint32_t t) {
    const uint16_t red = THEME[POMO_FOCUS];
    for (int i = 0; i < POMO_SET; i++) {
        int cx = SCREEN_LONG / 2 + (2 * i - 3) * 9;
        if (i < filled) {
            gfx_circle(cx, cy, 4, red);
            gfx_pixel(cx - 2, cy - 2, rgb_lerp(red, C_WHITE, 150));
        } else {
            gfx_circle(cx, cy, 4, rgb(52, 52, 62));
            gfx_circle(cx, cy, 3, rgb(18, 18, 24));
            if (i == filled && current) gfx_circle(cx, cy, 2, rgb_lerp(rgb(60, 30, 30), red, triangle(t, 1600)));
        }
    }
}

static void set_led(uint16_t c, int level) {
    hal_led((uint8_t)(rgb_r(c) * level / 255), (uint8_t)(rgb_g(c) * level / 255), (uint8_t)(rgb_b(c) * level / 255));
}

static bool pomodoro_frame(const input_t *in, uint32_t dt) {
    pomo_t *p = pomodoro_state();
    anim_ms += dt;

    if ((in->held & (BTN_A | BTN_B)) == (BTN_A | BTN_B)) chord = true;
    if (!chord) {
        if (in->released & BTN_A) pomo_toggle(p);
        if (in->released & BTN_B) pomo_reset(p);
    }
    if (!(in->held & (BTN_A | BTN_B))) chord = false;
    if (in->pressed & BTN_X) pomo_skip(p);
    if (in->pressed & BTN_Y) pomo_next_preset(p);

    // When a phase has ended, everything shows the phase that comes next.
    pomo_phase_t shown = p->done ? pomo_next_phase(p) : p->phase;
    uint16_t theme = THEME[shown];
    bool flash_on = (anim_ms / 350) % 2 == 0;

    uint16_t top = rgb_lerp(rgb(10, 10, 14), theme, p->done ? 40 + triangle(anim_ms, 700) / 5 : 46);
    gfx_vgradient(0, 0, SCREEN_LONG, SCREEN_SHORT, top, rgb(8, 8, 12));

    // Phase name with its icon.
    const char *label = PHASE_NAME[shown];
    int lw = gfx_text_width(label, &font_mini, 2);
    int lx = SCREEN_LONG / 2 - (lw + 14) / 2;
    if (shown == POMO_FOCUS) draw_tomato(lx + 4, 9, theme);
    else draw_cup(lx + 4, 9, theme);
    gfx_text(lx + 14, 4, label, theme, &font_mini, 2);

    uint16_t ghost = rgb_lerp(rgb(10, 10, 14), theme, 34);
    uint32_t total = pomo_duration(p, p->phase);
    if (p->done) {
        uint16_t c = flash_on ? C_WHITE : theme, shadow = rgb_lerp(C_BLACK, theme, 60);
        const char *l1 = shown == POMO_FOCUS ? "Back to" : "Break";
        const char *l2 = shown == POMO_FOCUS ? "work" : "time!";
        int x1 = SCREEN_LONG / 2 - gfx_text_width(l1, &font_mini, 4) / 2;
        int x2 = SCREEN_LONG / 2 - gfx_text_width(l2, &font_mini, 4) / 2;
        gfx_text(x1 + 2, 26, l1, shadow, &font_mini, 4);
        gfx_text(x2 + 2, 54, l2, shadow, &font_mini, 4);
        gfx_text(x1, 24, l1, c, &font_mini, 4);
        gfx_text(x2, 52, l2, c, &font_mini, 4);
        char buf[40];
        snprintf(buf, sizeof buf, "press A to start the %u min %s", (unsigned)(pomo_duration(p, shown) / MIN_MS),
                 shown == POMO_FOCUS ? "focus" : "break");
        gfx_text_center(SCREEN_LONG / 2, 87, buf, flash_on ? rgb(230, 230, 236) : rgb(150, 150, 160), &font_mini, 1);
    } else {
        // Paused, the digits breathe; running, the colon ticks.
        uint16_t on = p->running ? theme : rgb_lerp(rgb_lerp(theme, C_BLACK, 110), theme, triangle(anim_ms, 2000));
        bool colon = !p->running || (p->remaining_ms % 1000) >= 500;
        draw_time(p->remaining_ms, 22, on, ghost, colon);
        draw_bar(36, 87, 168, 7, total - p->remaining_ms, total, theme, rgb_lerp(rgb(10, 10, 14), theme, 50));
    }

    // A finished focus fills its dot straight away; a finished long break empties them for the next set.
    int filled = p->set_pos + (p->done && p->phase == POMO_FOCUS ? 1 : 0);
    if (p->done && p->phase == POMO_LONG) filled = 0;
    draw_dots(106, filled, p->phase == POMO_FOCUS && !p->done, anim_ms);

    char count[32];
    snprintf(count, sizeof count, "%lu done", (unsigned long)g_save.pomodoro_done);
    gfx_text_center(SCREEN_LONG / 2, 120, count, rgb(120, 120, 132), &font_mini, 1);

    // Button hints beside the buttons they belong to.
    uint16_t cap = rgb_lerp(theme, C_WHITE, 60), txt = rgb(170, 170, 180);
    hint(3, 3, false, 'A', p->running ? "pause" : "start", cap, txt);
    hint(3, SCREEN_SHORT - 13, false, 'B', "reset", cap, txt);
    hint(SCREEN_LONG - 3, 3, true, 'X', "skip", cap, txt);
    char preset[16];
    snprintf(preset, sizeof preset, "%u min", (unsigned)PRESETS[p->preset][POMO_FOCUS]);
    hint(SCREEN_LONG - 3, SCREEN_SHORT - 13, true, 'Y', preset, cap, txt);

    // The LED: steady while running, breathing while paused, flashing at the end.
    if (p->done) set_led(theme, flash_on ? 255 : 0);
    else if (p->running) set_led(theme, 40);
    else set_led(theme, 10 + triangle(anim_ms, 2000) * 70 / 255);
    return true;
}

const app_t app_pomodoro = {
    .name = "Pomodoro",
    .orient = ORIENT_LANDSCAPE,
    .accent = rgb(230, 60, 50),
    .fps = 30,
    .enter = pomodoro_enter,
    .frame = pomodoro_frame,
    .background = pomodoro_background,
};

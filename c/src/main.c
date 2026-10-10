// The launcher and the frame loop every app runs inside.
//
// The menu keeps the MicroPython version's layout: each button's job is
// written on the coloured bar beside it. A moves up, B moves down, X resets
// the board and Y opens the highlighted app.
#include <stdio.h>
#include <string.h>

#include "core/app.h"
#include "core/hal.h"
#include "core/save.h"

#define HOLD_MS 1000

static const app_t *const apps[] = {
    &app_bad_apple, &app_tetris, &app_flappy, &app_pet, &app_pomodoro, &app_life, &app_dvd, &app_rpg,
};
#define APP_COUNT ((int)(sizeof apps / sizeof apps[0]))

static uint32_t last_ms;

static void background_ticks(uint32_t dt) {
    for (int i = 0; i < APP_COUNT; i++)
        if (apps[i]->background) apps[i]->background(dt);
}

static input_t read_input(uint8_t *prev) {
    input_t in;
    in.held = hal_buttons();
    in.pressed = in.held & (uint8_t)~*prev;
    in.released = *prev & (uint8_t)~in.held;
    *prev = in.held;
    return in;
}

static uint32_t tick(void) {
    uint32_t now = hal_millis();
    uint32_t dt = now - last_ms;
    last_ms = now;
    return dt > 250 ? 250 : dt; // a stall never turns into a huge physics step
}

static void pace(uint32_t frame_start, int fps) {
    uint32_t budget = 1000u / (uint32_t)fps;
    uint32_t spent = hal_millis() - frame_start;
    if (spent < budget) hal_sleep_ms(budget - spent);
}

// Draws how far a two-button hold has got, as a bar along the top.
static void hold_bar(uint32_t held_ms, uint16_t colour) {
    if (held_ms < 150) return;
    int w = (int)(held_ms * (uint32_t)gfx_w / HOLD_MS);
    gfx_rect(0, 0, gfx_w, 3, C_BLACK);
    gfx_rect(0, 0, w, 3, colour);
}

static void run_app(const app_t *app) {
    gfx_set_orientation(app->orient);
    hal_set_orientation(app->orient, g_save.portrait_flip);
    hal_led(0, 0, 0);
    app->enter();
    uint8_t prev = hal_buttons();
    uint32_t exit_ms = 0, flip_ms = 0;
    last_ms = hal_millis();
    while (hal_running()) {
        uint32_t start = hal_millis();
        hal_wait_present();
        input_t in = read_input(&prev);
        uint32_t dt = tick();
        background_ticks(dt);

        exit_ms = (in.held & (BTN_A | BTN_B)) == (BTN_A | BTN_B) ? exit_ms + dt : 0;
        flip_ms = app->orient == ORIENT_PORTRAIT && (in.held & (BTN_X | BTN_Y)) == (BTN_X | BTN_Y) ? flip_ms + dt : 0;
        if (exit_ms >= HOLD_MS) break;
        if (flip_ms >= HOLD_MS) {
            // Portrait apps can be turned the other way up; it is remembered.
            g_save.portrait_flip ^= 1;
            hal_set_orientation(app->orient, g_save.portrait_flip);
            flip_ms = 0;
        }

        if (!app->frame(&in, dt)) break;
        hold_bar(exit_ms, C_RED);
        hold_bar(flip_ms, C_CYAN);
        hal_present();
        pace(start, app->fps ? app->fps : 30);
    }
    if (app->leave) app->leave();
    hal_led(0, 0, 0);
    save_commit();
    // Wait for every button to come up, so the press that left the app does
    // not also act on the menu.
    while (hal_running() && hal_buttons()) {
        background_ticks(tick());
        hal_sleep_ms(10);
    }
}

// The four coloured bars down the sides, each labelled with what its button does.
static void draw_button_bars(void) {
    const int bw = 12, half = SCREEN_SHORT / 2;
    gfx_rect(0, 0, bw, half, rgb(70, 190, 70));
    gfx_rect(0, half + 1, bw, SCREEN_SHORT - half - 1, rgb(230, 80, 60));
    gfx_rect(SCREEN_LONG - bw, 0, bw, half, rgb(60, 140, 230));
    gfx_rect(SCREEN_LONG - bw, half + 1, bw, SCREEN_SHORT - half - 1, rgb(240, 200, 50));
    // A: up arrow, B: down arrow.
    for (int i = 0; i < 4; i++) {
        gfx_hline(6 - i, half / 2 - 2 + i, 1 + 2 * i, C_BLACK);
        gfx_hline(6 - i, half + half / 2 + 2 - i, 1 + 2 * i, C_BLACK);
    }
    // X: reset, a small ring. Y: select, a left-pointing arrow.
    gfx_ring(SCREEN_LONG - 6, half / 2, 3, C_BLACK);
    gfx_pixel(SCREEN_LONG - 6, half / 2 - 3, rgb(60, 140, 230));
    for (int i = 0; i < 4; i++) gfx_vline(SCREEN_LONG - 8 + i, half + half / 2 - i, 1 + 2 * i, C_BLACK);
}

static void draw_menu(int sel) {
    gfx_clear(rgb(30, 32, 44));
    draw_button_bars();
    const int left = 16, right = SCREEN_LONG - 16, row_h = 15, top = 6;
    for (int i = 0; i < APP_COUNT; i++) {
        int y = top + i * row_h + (i >= 4 ? 4 : 0);
        bool on = i == sel;
        if (on) gfx_round_rect(left, y - 2, right - left, row_h, 3, rgb(235, 235, 240));
        gfx_rect(left + 5, y + 2, 7, 7, apps[i]->accent);
        gfx_frame(left + 4, y + 1, 9, 9, on ? C_BLACK : rgb(90, 90, 110));
        gfx_text(left + 18, y + 2, apps[i]->name, on ? C_BLACK : C_WHITE, &font_mini, 1);
        if (apps[i]->orient == ORIENT_PORTRAIT)
            gfx_text(right - 26, y + 2, "tall", on ? C_GREY : rgb(110, 110, 130), &font_mini, 1);
    }
    gfx_hline(left + 4, top + 4 * row_h, right - left - 8, rgb(60, 62, 80));
}

static int find_app(const char *name) {
    for (int i = 0; i < APP_COUNT; i++)
        if (strcmp(apps[i]->name, name) == 0) return i;
    return -1;
}

int main(void) {
    hal_init();
    save_load();
    int sel = g_save.menu_index < APP_COUNT ? g_save.menu_index : 0;

#ifdef PDG_SIM
    // The simulator can open an app straight away: PDG_APP="Tetris".
    extern const char *sim_start_app(void);
    const char *start = sim_start_app();
    if (start && find_app(start) >= 0) {
        sel = find_app(start);
        run_app(apps[sel]);
    }
#else
    (void)find_app;
#endif

    uint8_t prev = hal_buttons();
    last_ms = hal_millis();
    while (hal_running()) {
        uint32_t start = hal_millis();
        gfx_set_orientation(ORIENT_LANDSCAPE);
        hal_set_orientation(ORIENT_LANDSCAPE, false);
        hal_wait_present();
        input_t in = read_input(&prev);
        background_ticks(tick());
        if (in.pressed & BTN_A) sel = (sel + APP_COUNT - 1) % APP_COUNT;
        if (in.pressed & BTN_B) sel = (sel + 1) % APP_COUNT;
        if (in.pressed & BTN_X) {
            save_commit();
            hal_reset();
        }
        if (in.pressed & BTN_Y) {
            g_save.menu_index = (uint8_t)sel;
            run_app(apps[sel]);
            prev = hal_buttons();
            last_ms = hal_millis();
            continue;
        }
        draw_menu(sel);
        hal_present();
        pace(start, 30);
    }
    save_commit();
    return 0;
}

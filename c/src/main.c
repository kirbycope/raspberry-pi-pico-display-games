// The launcher, the frame loop every app runs inside, and attract mode.
//
// The menu keeps the MicroPython version's layout: each button's job is
// written on the coloured bar beside it. A moves up, B moves down, X resets
// the board and Y opens the highlighted app.
//
// Attract mode starts after half a minute untouched in the menu: the apps
// take turns playing themselves, like an arcade cabinet. Any real button
// press goes back to the menu, and nothing done in attract mode is saved.
//
// Remote commands, over the Pico's USB serial port (tools/remote.py) or a
// simulator script:
//   attract            the attract rotation
//   attract <app>      one app playing itself until stopped
//   open <app>         open an app as if picked from the menu
//   menu               back to the menu
#include <stdio.h>
#include <string.h>

#include "core/app.h"
#include "core/hal.h"
#include "core/save.h"

#define HOLD_MS 1000
#define IDLE_ATTRACT_MS 30000
#define FOREVER 0xFFFFFFFFu



static const app_t *const apps[] = {
    &app_bad_apple, &app_tetris, &app_flappy, &app_pet, &app_pomodoro, &app_life, &app_dvd, &app_rpg,
};
#define APP_COUNT ((int)(sizeof apps / sizeof apps[0]))

// The attract rotation and how long each app gets.
static const struct { const app_t *app; uint32_t ms; } rotation[] = {
    {&app_tetris, 25000}, {&app_flappy, 20000}, {&app_rpg, 30000}, {&app_bad_apple, 20000},
    {&app_pet, 15000},    {&app_life, 15000},   {&app_dvd, 15000},
};
#define ROTATION_COUNT ((int)(sizeof rotation / sizeof rotation[0]))

static uint32_t last_ms;
static char queued[96]; // a remote command that arrived while an app was open

static void background_ticks(uint32_t dt) {
    for (int i = 0; i < APP_COUNT; i++)
        if (apps[i]->background) apps[i]->background(dt);
}

static input_t edges(uint8_t held, uint8_t *prev) {
    input_t in;
    in.held = held;
    in.pressed = held & (uint8_t)~*prev;
    in.released = *prev & (uint8_t)~held;
    *prev = held;
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

static int find_app(const char *name) {
    for (int i = 0; i < APP_COUNT; i++)
        if (strcmp(apps[i]->name, name) == 0) return i;
    return -1;
}

// Runs one app until it quits, A+B is held, a remote command arrives, or, in
// attract mode, its time is up or a real button is pressed. Returns true only
// when an attract turn simply ran out of time.
static bool run_app(const app_t *app, uint32_t attract_ms) {
    bool attract = attract_ms != 0;
    bool timed_out = false;
    g_attract = attract;
    gfx_set_orientation(app->orient);
    hal_set_orientation(app->orient, g_save.portrait_flip);
    hal_led(0, 0, 0);
    app->enter();
    if (attract && app->attract) app->attract();
    uint8_t prev = hal_buttons(), real_prev = hal_physical_buttons(), auto_prev = 0;
    uint32_t exit_ms = 0, flip_ms = 0, elapsed = 0;
    last_ms = hal_millis();
    while (hal_running()) {
        uint32_t start = hal_millis();
        hal_wait_present();
        const char *cmd = hal_remote_command();
        if (cmd) {
            strncpy(queued, cmd, sizeof queued - 1);
            break;
        }
        uint32_t dt = tick();
        background_ticks(dt);
        input_t in;
        if (attract) {
            uint8_t real = hal_physical_buttons();
            bool touched = (real & (uint8_t)~real_prev) != 0;
            real_prev = real;
            if (touched) break;
            elapsed += dt;
            if (attract_ms != FOREVER && elapsed >= attract_ms) {
                timed_out = true;
                break;
            }
            in = edges(app->autopilot ? app->autopilot(dt) : 0, &auto_prev);
        } else {
            in = edges(hal_buttons(), &prev);
            exit_ms = (in.held & (BTN_A | BTN_B)) == (BTN_A | BTN_B) ? exit_ms + dt : 0;
            flip_ms = app->orient == ORIENT_PORTRAIT && (in.held & (BTN_X | BTN_Y)) == (BTN_X | BTN_Y) ? flip_ms + dt : 0;
            if (exit_ms >= HOLD_MS) break;
            if (flip_ms >= HOLD_MS) {
                // Portrait apps can be turned the other way up; it is remembered.
                g_save.portrait_flip ^= 1;
                hal_set_orientation(app->orient, g_save.portrait_flip);
                flip_ms = 0;
            }
        }

        if (!app->frame(&in, dt)) break;
        hold_bar(exit_ms, C_RED);
        hold_bar(flip_ms, C_CYAN);
        hal_present();
        pace(start, app->fps ? app->fps : 30);
    }
    if (app->leave) app->leave();
    hal_led(0, 0, 0);
    g_attract = false;
    if (!attract) save_commit();
    // Wait for every real button to come up, so the press that left the app
    // does not also act on the menu.
    while (hal_running() && hal_physical_buttons()) {
        background_ticks(tick());
        hal_sleep_ms(10);
    }
    return timed_out;
}

// The apps take turns playing themselves until a real button or a remote
// command stops them. only >= 0 keeps to that one app.
static void attract_mode(int only) {
    save_t before = g_save;
    hal_remote_reply("attract");
    for (int i = 0; hal_running(); i = (i + 1) % ROTATION_COUNT) {
        const app_t *app = only >= 0 ? apps[only] : rotation[i].app;
        bool next = run_app(app, only >= 0 ? FOREVER : rotation[i].ms);
        // Undo whatever the demo did, except the pet's own aging, which is real.
        uint8_t pet[SAVE_PET_BYTES];
        memcpy(pet, g_save.pet, sizeof pet);
        g_save = before;
        memcpy(g_save.pet, pet, sizeof pet);
        if (!next) break;
    }
    hal_remote_reply("menu");
}

// Acts on a remote command. Returns false if it was not one.
static bool dispatch(const char *cmd, int *sel) {
    char verb[16] = {0};
    const char *arg = "";
    sscanf(cmd, "%15s", verb);
    const char *space = strchr(cmd, ' ');
    if (space) arg = space + 1;
    int which = find_app(arg);
    if (!strcmp(verb, "attract")) {
        if (*arg && which < 0) { hal_remote_reply("no such app"); return true; }
        attract_mode(which);
    } else if (!strcmp(verb, "open")) {
        if (which < 0) { hal_remote_reply("no such app"); return true; }
        *sel = which;
        hal_remote_reply("ok");
        run_app(apps[which], 0);
    } else if (!strcmp(verb, "menu")) {
        hal_remote_reply("ok");
    } else {
        hal_remote_reply("unknown command");
        return false;
    }
    return true;
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
        run_app(apps[sel], 0);
    }
#endif

    uint8_t prev = hal_buttons();
    uint32_t idle_ms = 0;
    last_ms = hal_millis();
    while (hal_running()) {
        uint32_t start = hal_millis();
        gfx_set_orientation(ORIENT_LANDSCAPE);
        hal_set_orientation(ORIENT_LANDSCAPE, false);
        hal_wait_present();

        // A command that ended an app is acted on first, then any new one.
        const char *cmd = queued[0] ? queued : hal_remote_command();
        if (cmd) {
            char copy[96];
            strncpy(copy, cmd, sizeof copy - 1);
            copy[sizeof copy - 1] = 0;
            queued[0] = 0;
            dispatch(copy, &sel);
            prev = hal_buttons();
            idle_ms = 0;
            last_ms = hal_millis();
            continue;
        }

        input_t in = edges(hal_buttons(), &prev);
        uint32_t dt = tick();
        background_ticks(dt);
        idle_ms = in.held ? 0 : idle_ms + dt;
        if (idle_ms >= IDLE_ATTRACT_MS) {
            attract_mode(-1);
            prev = hal_buttons();
            idle_ms = 0;
            last_ms = hal_millis();
            continue;
        }
        if (in.pressed & BTN_A) sel = (sel + APP_COUNT - 1) % APP_COUNT;
        if (in.pressed & BTN_B) sel = (sel + 1) % APP_COUNT;
        if (in.pressed & BTN_X) {
            save_commit();
            hal_reset();
        }
        if (in.pressed & BTN_Y) {
            g_save.menu_index = (uint8_t)sel;
            run_app(apps[sel], 0);
            prev = hal_buttons();
            idle_ms = 0;
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

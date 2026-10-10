// Conway's Game of Life on an 80x45 grid of 3 pixel cells that wraps at
// every edge. Newborn cells flash white and cool through cyan, blue and
// violet as they age; cells that die leave a short fading trail. A world
// that settles down (dies out, or only repeats itself) is replaced with a
// fresh random soup after a few seconds. Every generation is added to a
// lifetime count kept across power cycles.
//
// A (top left):     play / pause
// B (bottom left):  new random soup
// X (top right):    paused, step one generation; running, change speed
//                   (5, 10, 20, 30 generations a second)
// Y (bottom right): next pattern: random soup, Gosper glider gun, pulsar
//                   field, R-pentomino, acorn, spaceship fleet, diehard
#include "life.h"

#include <stdio.h>
#include <string.h>

#include "../core/app.h"
#include "../core/hal.h"
#include "../core/save.h"

// ---- the rules ----

void life_clear(life_t *l) {
    memset(l->cells, 0, sizeof l->cells);
    l->generation = 0;
    l->population = 0;
    l->hash = 0;
}

static int wrap(int v, int n) {
    v %= n;
    return v < 0 ? v + n : v;
}

void life_set(life_t *l, int x, int y, bool alive) {
    uint8_t *c = &l->cells[wrap(y, LIFE_H)][wrap(x, LIFE_W)];
    bool was = *c & LIFE_ALIVE;
    if (alive && !was) l->population++;
    if (!alive && was) l->population--;
    *c = alive ? LIFE_ALIVE | 1 : 0;
}

bool life_get(const life_t *l, int x, int y) { return l->cells[wrap(y, LIFE_H)][wrap(x, LIFE_W)] & LIFE_ALIVE; }

int life_count(const life_t *l) {
    int n = 0;
    for (int y = 0; y < LIFE_H; y++)
        for (int x = 0; x < LIFE_W; x++) n += l->cells[y][x] >> 7;
    return n;
}

// Each row is worked out from three rows of 0/1 live bits. Summing them per
// column first leaves three additions per cell. The rows above and below are
// copies taken before they are overwritten, so the step runs in place with
// a few hundred bytes of stack.
void life_step(life_t *l) {
    uint8_t up[LIFE_W], mid[LIFE_W], down[LIFE_W], first[LIFE_W];
    uint8_t col[LIFE_W + 2];
    for (int x = 0; x < LIFE_W; x++) {
        first[x] = l->cells[0][x] >> 7;
        up[x] = l->cells[LIFE_H - 1][x] >> 7;
    }
    memcpy(mid, first, LIFE_W);
    int pop = 0;
    uint32_t hash = 2166136261u;
    for (int y = 0; y < LIFE_H; y++) {
        if (y == LIFE_H - 1) memcpy(down, first, LIFE_W);
        else
            for (int x = 0; x < LIFE_W; x++) down[x] = l->cells[y + 1][x] >> 7;
        for (int x = 0; x < LIFE_W; x++) col[x + 1] = (uint8_t)(up[x] + mid[x] + down[x]);
        col[0] = col[LIFE_W];
        col[LIFE_W + 1] = col[1];
        uint8_t *row = l->cells[y];
        for (int x = 0; x < LIFE_W; x++) {
            int n = col[x] + col[x + 1] + col[x + 2] - mid[x];
            uint8_t c = row[x];
            if (c & LIFE_ALIVE) {
                if (n == 2 || n == 3) {
                    uint8_t age = c & 0x7F;
                    row[x] = (uint8_t)(LIFE_ALIVE | (age < LIFE_AGE_MAX ? age + 1 : age));
                } else {
                    row[x] = LIFE_TRAIL;
                    continue;
                }
            } else if (n == 3) {
                row[x] = LIFE_ALIVE | 1;
            } else {
                if (c) row[x] = (uint8_t)(c - 1);
                continue;
            }
            pop++;
            hash = (hash ^ (uint32_t)(y * LIFE_W + x)) * 16777619u;
        }
        memcpy(up, mid, LIFE_W);
        memcpy(mid, down, LIFE_W);
    }
    l->population = pop;
    l->hash = hash;
    l->generation++;
}

void life_stamp(life_t *l, const char *const *rows, int x, int y, bool flip_x) {
    for (int j = 0; rows[j]; j++) {
        int w = (int)strlen(rows[j]);
        for (int i = 0; i < w; i++)
            if (rows[j][i] == 'O') life_set(l, flip_x ? x + w - 1 - i : x + i, y + j, true);
    }
}

void life_watch_reset(life_watch_t *w) { memset(w, 0, sizeof *w); }

bool life_watch_push(life_watch_t *w, const life_t *l) {
    bool seen_hash = false;
    uint32_t n = w->seen < LIFE_HASHES ? w->seen : LIFE_HASHES;
    for (uint32_t i = 0; i < n; i++)
        if (w->hashes[i] == l->hash) seen_hash = true;
    w->repeat_run = seen_hash ? w->repeat_run + 1 : 0;
    w->hashes[w->seen % LIFE_HASHES] = l->hash;

    bool pop_stuck = false;
    for (int p = 1; p <= LIFE_POPS; p++) {
        uint16_t *run = &w->pop_runs[p - 1];
        bool same = w->seen >= (uint32_t)p && w->pops[(w->seen - (uint32_t)p) % LIFE_POPS] == l->population;
        *run = same ? (uint16_t)(*run < 60000 ? *run + 1 : *run) : 0;
        if (*run >= LIFE_POP_GENS) pop_stuck = true;
    }
    w->pops[w->seen % LIFE_POPS] = l->population;
    w->seen++;
    return l->population == 0 || w->repeat_run >= LIFE_REPEAT_GENS || pop_stuck;
}

// ---- patterns ----

static const char *const GOSPER_GUN[] = {
    "........................O...........",
    "......................O.O...........",
    "............OO......OO............OO",
    "...........O...O....OO............OO",
    "OO........O.....O...OO..............",
    "OO........O...O.OO....O.O...........",
    "..........O.....O.......O...........",
    "...........O...O....................",
    "............OO......................",
    NULL,
};
static const char *const PULSAR[] = {
    "..OOO...OOO..", ".............", "O....O.O....O", "O....O.O....O", "O....O.O....O",
    "..OOO...OOO..", ".............", "..OOO...OOO..", "O....O.O....O", "O....O.O....O",
    "O....O.O....O", ".............", "..OOO...OOO..", NULL,
};
static const char *const R_PENTOMINO[] = {".OO", "OO.", ".O.", NULL};
static const char *const ACORN[] = {".O.....", "...O...", "OO..OOO", NULL};
static const char *const DIEHARD[] = {"......O.", "OO......", ".O...OOO", NULL};
// These three fly left; stamped flipped they fly right.
static const char *const LWSS[] = {".O..O", "O....", "O...O", "OOOO.", NULL};
static const char *const MWSS[] = {"...O..", ".O...O", "O.....", "O....O", "OOOOO.", NULL};
static const char *const HWSS[] = {"...OO..", ".O....O", "O......", "O.....O", "OOOOOO.", NULL};
// Flies down and to the right.
static const char *const GLIDER[] = {".O.", "..O", "OOO", NULL};

static void load_soup(life_t *l) {
    for (int y = 0; y < LIFE_H; y++)
        for (int x = 0; x < LIFE_W; x++)
            if ((hal_random() & 0xFF) < 80) life_set(l, x, y, true);
}

static void load_gun(life_t *l) { life_stamp(l, GOSPER_GUN, 4, 3, false); }

static void load_pulsars(life_t *l) {
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 4; i++) life_stamp(l, PULSAR, 3 + i * 20, 5 + j * 22, false);
}

static void load_r_pentomino(life_t *l) { life_stamp(l, R_PENTOMINO, 39, 21, false); }
static void load_acorn(life_t *l) { life_stamp(l, ACORN, 36, 21, false); }
static void load_diehard(life_t *l) { life_stamp(l, DIEHARD, 36, 21, false); }

static void load_fleet(life_t *l) {
    life_stamp(l, LWSS, 6, 3, true);
    life_stamp(l, MWSS, 50, 12, false);
    life_stamp(l, HWSS, 20, 21, true);
    life_stamp(l, LWSS, 60, 30, false);
    life_stamp(l, MWSS, 30, 38, true);
    // Two gliders cut across the lanes, so sooner or later something collides.
    life_stamp(l, GLIDER, 40, 2, false);
    life_stamp(l, GLIDER, 70, 18, false);
}

static const struct {
    const char *name;
    void (*load)(life_t *l);
} PATTERNS[] = {
    {"Random soup", load_soup},     {"Gosper glider gun", load_gun}, {"Pulsar field", load_pulsars},
    {"R-pentomino", load_r_pentomino}, {"Acorn", load_acorn},       {"Spaceship fleet", load_fleet},
    {"Diehard", load_diehard},
};
#define PATTERN_COUNT ((int)(sizeof PATTERNS / sizeof PATTERNS[0]))

// ---- the app ----

static const uint8_t SPEEDS[] = {5, 10, 20, 30};
#define SPEED_COUNT ((int)sizeof SPEEDS)
#define OVERLAY_MS 2200
#define SETTLE_MS 3000

static life_t world;
static life_watch_t watch;
static bool ready, running = true;
static int pattern, speed = 1;
static uint32_t acc, overlay_ms, settle_ms, anim_ms;
static bool help;

static uint16_t pal_alive[LIFE_AGE_MAX + 1], pal_trail[LIFE_TRAIL + 1];
#define BG rgb(8, 10, 22)
#define BG_DOT rgb(18, 22, 42) // a faint dot in each empty cell, like graph paper

static void build_palettes(void) {
    // Colour of a live cell by age: white when born, then cyan, blue, violet,
    // and a dusky rose for anything that has sat still a long time.
    static const struct { uint8_t age; uint16_t c; } RAMP[] = {
        {1, rgb(255, 255, 255)}, {2, rgb(170, 250, 255)}, {4, rgb(60, 225, 245)}, {8, rgb(50, 150, 255)},
        {16, rgb(120, 90, 250)},  {32, rgb(180, 70, 220)},  {64, rgb(210, 70, 160)}, {127, rgb(200, 80, 120)},
    };
    const int n = (int)(sizeof RAMP / sizeof RAMP[0]);
    pal_alive[0] = RAMP[0].c;
    for (int a = 1; a <= LIFE_AGE_MAX; a++) {
        int k = 0;
        while (k < n - 2 && a > RAMP[k + 1].age) k++;
        int a0 = RAMP[k].age, a1 = RAMP[k + 1].age;
        int t = a >= a1 ? 255 : (a - a0) * 255 / (a1 - a0);
        pal_alive[a] = rgb_lerp(RAMP[k].c, RAMP[k + 1].c, t);
    }
    pal_trail[0] = BG;
    for (int t = 1; t <= LIFE_TRAIL; t++) pal_trail[t] = rgb_lerp(BG, rgb(44, 54, 120), t * 255 / (LIFE_TRAIL + 1));
}

static void show_overlay(void) { overlay_ms = OVERLAY_MS; }

static void load_pattern(int p) {
    pattern = p;
    life_clear(&world);
    PATTERNS[p].load(&world);
    life_watch_reset(&watch);
    settle_ms = 0;
    acc = 0;
    show_overlay();
}

static void life_enter(void) {
    if (!ready) {
        build_palettes();
        load_pattern(0);
        ready = true;
    }
    running = true;
    help = true;
    anim_ms = 0;
    show_overlay();
}

static void step_world(void) {
    life_step(&world);
    g_save.life_generations++;
    if (!settle_ms && life_watch_push(&watch, &world)) settle_ms = SETTLE_MS;
}

static void draw_world(void) {
    for (int y = 0; y < LIFE_H; y++) {
        uint16_t *p = &gfx_fb[y * LIFE_CELL * SCREEN_LONG];
        const uint8_t *row = world.cells[y];
        for (int x = 0; x < LIFE_W; x++, p += LIFE_CELL) {
            uint8_t c = row[x];
            uint16_t col = (c & LIFE_ALIVE) ? pal_alive[c & 0x7F] : pal_trail[c];
            p[0] = p[1] = p[2] = col;
            p[SCREEN_LONG] = p[SCREEN_LONG + 1] = p[SCREEN_LONG + 2] = col;
            p[2 * SCREEN_LONG] = p[2 * SCREEN_LONG + 1] = p[2 * SCREEN_LONG + 2] = col;
            if (!c) p[SCREEN_LONG + 1] = BG_DOT;
        }
    }
}

// A dark panel with lines of text, faded by alpha 0..255.
static void panel(int x, int y, const char *const *lines, int count, const uint16_t *colours, int alpha) {
    int w = 0;
    for (int i = 0; i < count; i++) {
        int lw = gfx_text_width(lines[i], &font_mini, 1);
        if (lw > w) w = lw;
    }
    int pw = w + 10, ph = count * 9 + 7;
    if (x < 0) x = SCREEN_LONG / 2 - pw / 2;
    gfx_blend_rect(x, y, pw, ph, rgb(4, 4, 10), alpha * 225 / 255);
    for (int i = 0; i < count; i++) {
        uint16_t c = rgb_lerp(rgb(20, 20, 30), colours[i], alpha);
        gfx_text(x + 5, y + 4 + i * 9, lines[i], c, &font_mini, 1);
    }
}

static bool life_frame(const input_t *in, uint32_t dt) {
    anim_ms += dt;
    if (in->pressed & BTN_A) {
        running = !running;
        acc = 0;
        show_overlay();
    }
    if (in->pressed & BTN_B) load_pattern(0);
    if (in->pressed & BTN_X) {
        if (running) speed = (speed + 1) % SPEED_COUNT;
        else step_world();
        show_overlay();
    }
    if (in->pressed & BTN_Y) load_pattern((pattern + 1) % PATTERN_COUNT);

    if (running) {
        acc += dt * SPEEDS[speed];
        int steps = 0;
        while (acc >= 1000 && steps < 4) {
            step_world();
            acc -= 1000;
            steps++;
        }
        if (acc >= 1000) acc = 0; // too far behind: drop it rather than race
        if (settle_ms) {
            settle_ms = settle_ms > dt ? settle_ms - dt : 0;
            if (!settle_ms) {
                load_pattern(0);
                help = false;
            }
        }
    }

    draw_world();

    if (overlay_ms) {
        overlay_ms = overlay_ms > dt ? overlay_ms - dt : 0;
        int alpha = overlay_ms >= 400 ? 255 : (int)overlay_ms * 255 / 400;
        char stats[48];
        if (running)
            snprintf(stats, sizeof stats, "gen %lu  pop %d  %d/s", (unsigned long)world.generation, world.population,
                     SPEEDS[speed]);
        else
            snprintf(stats, sizeof stats, "gen %lu  pop %d  paused", (unsigned long)world.generation, world.population);
        const char *lines[3] = {PATTERNS[pattern].name, stats,
                                running ? "A pause B soup X speed Y next" : "A play B soup X step Y next"};
        const uint16_t colours[3] = {C_WHITE, rgb(150, 200, 230), rgb(150, 150, 180)};
        panel(4, 4, lines, help ? 3 : 2, colours, alpha);
        if (!overlay_ms) help = false;
    }

    if (!running) {
        // A pause sign in the top right corner while stopped.
        gfx_blend_rect(SCREEN_LONG - 17, 4, 13, 13, rgb(4, 4, 10), 170);
        gfx_rect(SCREEN_LONG - 14, 7, 3, 7, C_WHITE);
        gfx_rect(SCREEN_LONG - 9, 7, 3, 7, C_WHITE);
    }

    if (settle_ms && running) {
        char msg[40];
        snprintf(msg, sizeof msg, "%s - new soup in %lu", world.population ? "Settled down" : "All gone",
                 (unsigned long)(settle_ms + 999) / 1000);
        const char *lines[1] = {msg};
        const uint16_t colours[1] = {rgb(255, 220, 120)};
        int pw = gfx_text_width(msg, &font_mini, 1) + 10;
        panel(-1, SCREEN_SHORT - 24, lines, 1, colours, 255);
        gfx_frame(SCREEN_LONG / 2 - pw / 2, SCREEN_SHORT - 24, pw, 16, rgb(120, 100, 50));
    }

    // The LED glows faintly with the colony's size.
    int glow = world.population / 16;
    if (glow > 40) glow = 40;
    hal_led(0, (uint8_t)(glow / 2), (uint8_t)glow);
    return true;
}

const app_t app_life = {
    .name = "Game of Life",
    .orient = ORIENT_LANDSCAPE,
    .accent = rgb(80, 220, 120),
    .fps = 30,
    .enter = life_enter,
    .frame = life_frame,
};

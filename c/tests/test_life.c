#include "apps/life.h"
#include "core/app.h"
#include "core/hal.h"
#include "core/save.h"
#include "test.h"

static life_t w;

// True if exactly the cells in rows (offset by x, y) are alive.
static bool matches(const life_t *l, const char *const *rows, int x, int y) {
    life_t want;
    life_clear(&want);
    life_stamp(&want, rows, x, y, false);
    for (int j = 0; j < LIFE_H; j++)
        for (int i = 0; i < LIFE_W; i++)
            if (life_get(l, i, j) != life_get(&want, i, j)) return false;
    return true;
}

static const char *const BLINKER_H[] = {"OOO", NULL};
static const char *const BLINKER_V[] = {"O", "O", "O", NULL};
static const char *const BLOCK[] = {"OO", "OO", NULL};
static const char *const GLIDER[] = {".O.", "..O", "OOO", NULL};
static const char *const LWSS[] = {".O..O", "O....", "O...O", "OOOO.", NULL};

TEST(life_blinker_oscillates_with_period_two) {
    life_clear(&w);
    life_stamp(&w, BLINKER_H, 10, 10, false);
    life_step(&w);
    CHECK(matches(&w, BLINKER_V, 11, 9));
    CHECK(!matches(&w, BLINKER_H, 10, 10));
    life_step(&w);
    CHECK(matches(&w, BLINKER_H, 10, 10));
    CHECK_EQ(w.generation, 2);
    CHECK_EQ(w.population, 3);
}

TEST(life_block_is_stable_and_ages) {
    life_clear(&w);
    life_stamp(&w, BLOCK, 40, 20, false);
    for (int i = 0; i < 10; i++) {
        life_step(&w);
        CHECK(matches(&w, BLOCK, 40, 20));
    }
    CHECK_EQ(w.cells[20][40] & 0x7F, 11); // born at age 1, ten generations older
}

TEST(life_glider_moves_one_cell_diagonally_every_four_generations) {
    life_clear(&w);
    life_stamp(&w, GLIDER, 5, 5, false);
    for (int k = 1; k <= 5; k++) {
        for (int i = 0; i < 4; i++) life_step(&w);
        CHECK(matches(&w, GLIDER, 5 + k, 5 + k));
        CHECK_EQ(w.population, 5);
    }
}

TEST(life_wraps_across_both_edges) {
    // A blinker straddling the left/right edge.
    life_clear(&w);
    life_set(&w, LIFE_W - 1, 10, true);
    life_set(&w, 0, 10, true);
    life_set(&w, 1, 10, true);
    life_step(&w);
    CHECK(life_get(&w, 0, 9) && life_get(&w, 0, 10) && life_get(&w, 0, 11));
    CHECK_EQ(w.population, 3);
    // A blinker straddling the top/bottom edge.
    life_clear(&w);
    life_set(&w, 20, LIFE_H - 1, true);
    life_set(&w, 20, 0, true);
    life_set(&w, 20, 1, true);
    life_step(&w);
    CHECK(life_get(&w, 19, 0) && life_get(&w, 20, 0) && life_get(&w, 21, 0));
    CHECK_EQ(w.population, 3);
    // A glider crossing the bottom right corner comes out at the top left.
    life_clear(&w);
    life_stamp(&w, GLIDER, LIFE_W - 2, LIFE_H - 2, false);
    for (int i = 0; i < 8; i++) life_step(&w);
    CHECK(matches(&w, GLIDER, 0, 0));
    CHECK_EQ(w.population, 5);
}

TEST(life_population_count_is_correct) {
    life_clear(&w);
    CHECK_EQ(w.population, 0);
    life_stamp(&w, LWSS, 30, 20, false);
    life_stamp(&w, BLOCK, 5, 5, false);
    CHECK_EQ(w.population, 13);
    CHECK_EQ(life_count(&w), 13);
    life_set(&w, 5, 5, true); // already alive
    life_set(&w, 6, 6, false);
    CHECK_EQ(w.population, 12);
    life_clear(&w);
    // A dense pseudo-random field: the step keeps the count true.
    uint32_t r = 12345;
    for (int y = 0; y < LIFE_H; y++)
        for (int x = 0; x < LIFE_W; x++) {
            r = r * 1103515245u + 12345u;
            if ((r >> 16) % 3 == 0) life_set(&w, x, y, true);
        }
    CHECK_EQ(w.population, life_count(&w));
    for (int i = 0; i < 50; i++) {
        life_step(&w);
        CHECK_EQ(w.population, life_count(&w));
    }
}

TEST(life_spaceship_flies_and_flips) {
    life_clear(&w);
    life_stamp(&w, LWSS, 40, 20, false);
    for (int i = 0; i < 4; i++) life_step(&w);
    CHECK(matches(&w, LWSS, 38, 20)); // two cells left every four generations
    life_clear(&w);
    life_stamp(&w, LWSS, 40, 20, true);
    for (int i = 0; i < 4; i++) life_step(&w);
    life_t want;
    life_clear(&want);
    life_stamp(&want, LWSS, 42, 20, true);
    for (int y = 0; y < LIFE_H; y++)
        for (int x = 0; x < LIFE_W; x++) CHECK_EQ(life_get(&w, x, y), life_get(&want, x, y));
}

TEST(life_dead_cells_leave_a_fading_trail) {
    life_clear(&w);
    life_set(&w, 10, 10, true); // a lone cell dies at once
    life_step(&w);
    CHECK(!life_get(&w, 10, 10));
    CHECK_EQ(w.cells[10][10], LIFE_TRAIL);
    for (int i = 0; i < LIFE_TRAIL; i++) life_step(&w);
    CHECK_EQ(w.cells[10][10], 0);
}

TEST(life_watch_spots_settled_worlds) {
    life_watch_t watch;
    // A block settles once it has repeated for long enough.
    life_clear(&w);
    life_stamp(&w, BLOCK, 10, 10, false);
    life_watch_reset(&watch);
    int gens = 0;
    while (gens < 1000) {
        life_step(&w);
        gens++;
        if (life_watch_push(&watch, &w)) break;
    }
    CHECK(gens > 100 && gens < 200);
    // A blinker too.
    life_clear(&w);
    life_stamp(&w, BLINKER_H, 10, 10, false);
    life_watch_reset(&watch);
    gens = 0;
    while (gens < 1000 && !(life_step(&w), life_watch_push(&watch, &w))) gens++;
    CHECK(gens < 200);
    // A world that dies out settles at once.
    life_clear(&w);
    life_set(&w, 3, 3, true);
    life_watch_reset(&watch);
    life_step(&w);
    CHECK(life_watch_push(&watch, &w));
    // A lone glider never repeats exactly, but its population does, so it settles eventually.
    life_clear(&w);
    life_stamp(&w, GLIDER, 10, 10, false);
    life_watch_reset(&watch);
    gens = 0;
    while (gens < 2000 && !(life_step(&w), life_watch_push(&watch, &w))) gens++;
    CHECK(gens >= LIFE_POP_GENS - 1 && gens < 2000);
}

TEST(life_app_counts_generations_into_the_save) {
    app_life.enter();
    uint32_t before = g_save.life_generations;
    input_t none = {0, 0, 0};
    for (int i = 0; i < 30; i++) app_life.frame(&none, 100); // 3 s at 10 generations a second
    CHECK_EQ(g_save.life_generations, before + 30);
    // Paused, X steps exactly one generation.
    input_t a = {BTN_A, BTN_A, 0}, x = {BTN_X, BTN_X, 0};
    app_life.frame(&a, 10);
    app_life.frame(&none, 1000);
    CHECK_EQ(g_save.life_generations, before + 30);
    app_life.frame(&x, 10);
    CHECK_EQ(g_save.life_generations, before + 31);
    app_life.frame(&a, 10);
}

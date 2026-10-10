// Attract mode for the apps whose demo is a setup rather than a player:
// the DVD logo's guaranteed corner, the pet's borrowed egg, Life's patterns.
#include <string.h>

#include "apps/pet.h"
#include "core/app.h"
#include "core/hal.h"
#include "core/save.h"
#include "test.h"

static void run(const app_t *app, int frames, uint8_t (*pilot)(uint32_t)) {
    input_t in = {0};
    uint8_t prev = 0;
    for (int i = 0; i < frames; i++) {
        uint8_t held = pilot ? pilot(33) : 0;
        in.held = held;
        in.pressed = held & (uint8_t)~prev;
        in.released = prev & (uint8_t)~held;
        prev = held;
        if (app->background) app->background(33);
        app->frame(&in, 33);
    }
}

TEST(attract_dvd_hits_a_corner_within_three_seconds) {
    save_reset();
    gfx_set_orientation(app_dvd.orient);
    app_dvd.enter();
    g_attract = true;
    app_dvd.attract();
    run(&app_dvd, 3000 / 33, NULL);
    app_dvd.leave();
    g_attract = false;
    CHECK_EQ(g_save.dvd_corners, 1);
}

TEST(attract_pet_hatches_a_demo_egg_and_gives_the_real_pet_back) {
    save_reset();
    pet_t *p = pet_saved();
    pet_init(p);
    p->stage = PET_ADULT;
    p->age_s = 5000;
    pet_t real = *p;
    gfx_set_orientation(app_pet.orient);
    app_pet.enter();
    g_attract = true;
    app_pet.attract();
    CHECK_EQ(pet_saved()->stage, PET_EGG);
    run(&app_pet, 15000 / 33, app_pet.autopilot);
    CHECK_EQ(pet_saved()->stage, PET_BABY);
    app_pet.leave();
    g_attract = false;
    CHECK_EQ(pet_saved()->stage, PET_ADULT);
    CHECK_EQ(pet_saved()->age_s, real.age_s);
}

TEST(attract_life_moves_to_a_new_pattern_every_five_seconds) {
    save_reset();
    gfx_set_orientation(app_life.orient);
    app_life.enter();
    g_attract = true;
    app_life.attract();
    int presses = 0;
    for (int i = 0; i < 16000 / 33; i++)
        if (app_life.autopilot(33) & BTN_Y) presses++;
    app_life.leave();
    g_attract = false;
    CHECK_EQ(presses, 3);
    // Afterwards Life runs at the player's own speed again: 10 a second.
    uint32_t before = g_save.life_generations;
    input_t none = {0, 0, 0};
    app_life.enter();
    for (int i = 0; i < 10; i++) app_life.frame(&none, 100);
    CHECK_EQ(g_save.life_generations, before + 10);
}

#include <string.h>

#include "apps/pet.h"
#include "core/app.h"
#include "core/hal.h"
#include "core/save.h"
#include "test.h"

#define MIN_MS (60u * 1000u)

static void fresh(pet_t *p) {
    memset(p, 0, sizeof *p);
    pet_tick(p, 0);
}

static void hatched(pet_t *p) {
    fresh(p);
    pet_tick(p, PET_HATCH_S * 1000u);
}

static bool needs_in_range(const pet_t *p) {
    for (int i = 0; i < PET_NEEDS; i++)
        if (pet_need(p, i) < 0 || pet_need(p, i) > 100 || p->need[i] > PET_FULL) return false;
    return true;
}

TEST(pet_state_fits_save_slot) {
    CHECK(sizeof(pet_t) <= SAVE_PET_BYTES);
    CHECK(sizeof(((save_t *)0)->pet) == SAVE_PET_BYTES);
}

TEST(pet_fresh_zero_slot_becomes_an_egg) {
    pet_t p;
    fresh(&p);
    CHECK_EQ(p.magic, PET_MAGIC);
    CHECK_EQ(p.stage, PET_EGG);
    CHECK_EQ(p.age_s, 0);
    CHECK_EQ(p.poops, 0);
    for (int i = 0; i < PET_NEEDS; i++) CHECK_EQ(pet_need(&p, i), 100);

    // The same through the save slot and the background hook every app runs.
    memset(g_save.pet, 0, SAVE_PET_BYTES);
    app_pet.background(16);
    CHECK_EQ(pet_saved()->magic, PET_MAGIC);
    CHECK_EQ(pet_saved()->stage, PET_EGG);
}

TEST(pet_garbage_slot_becomes_an_egg) {
    pet_t p;
    memset(&p, 0xAB, sizeof p);
    pet_tick(&p, 0);
    CHECK_EQ(p.stage, PET_EGG);
    CHECK(needs_in_range(&p));
}

TEST(pet_egg_hatches_at_documented_time) {
    pet_t p;
    fresh(&p);
    pet_tick(&p, (PET_HATCH_S - 1) * 1000u + 999);
    CHECK_EQ(p.stage, PET_EGG);
    for (int i = 0; i < PET_NEEDS; i++) CHECK_EQ(pet_need(&p, i), 100); // eggs need nothing
    pet_tick(&p, 1);
    CHECK_EQ(p.stage, PET_BABY);
    CHECK(p.flags & PET_NEWS);
    CHECK_EQ(PET_HATCH_S, 120);
}

TEST(pet_baby_grows_up_at_documented_time) {
    pet_t p;
    hatched(&p);
    p.flags &= (uint8_t)~PET_NEWS;
    pet_tick(&p, (PET_GROW_S - 1) * 1000u);
    CHECK_EQ(p.stage, PET_BABY);
    pet_tick(&p, 1000);
    CHECK_EQ(p.stage, PET_ADULT);
    CHECK(p.flags & PET_NEWS);
    CHECK_EQ(p.age_s, PET_ADULT_S);
    CHECK_EQ(PET_GROW_S, 20 * 60);
}

TEST(pet_needs_decay_and_stay_in_range) {
    pet_t p;
    hatched(&p);
    int before[PET_NEEDS];
    for (int i = 0; i < PET_NEEDS; i++) before[i] = pet_need(&p, i);
    pet_tick(&p, 5 * MIN_MS);
    for (int i = 0; i < PET_NEEDS; i++) CHECK(pet_need(&p, i) < before[i]);
    // Ten hours of neglect in frame-sized and huge steps: always 0..100.
    for (int i = 0; i < 2000; i++) {
        pet_tick(&p, 33);
        CHECK(needs_in_range(&p));
    }
    for (int i = 0; i < 60; i++) {
        pet_tick(&p, 10 * MIN_MS);
        CHECK(needs_in_range(&p));
    }
    CHECK_EQ(pet_need(&p, NEED_HUNGER), 0);
}

TEST(pet_needs_attention_every_10_to_20_minutes) {
    pet_t p;
    hatched(&p);
    for (int i = 0; i < PET_NEEDS; i++) p.need[i] = PET_FULL;
    int minutes = 0;
    while (pet_wish(&p) == WISH_NONE && minutes < 120) {
        pet_tick(&p, MIN_MS);
        minutes++;
    }
    CHECK(minutes >= 10);
    CHECK(minutes <= 20);
}

TEST(pet_feeding_raises_hunger_and_is_capped) {
    pet_t p;
    hatched(&p);
    p.need[NEED_HUNGER] = PET_PCT(40);
    CHECK_EQ(pet_act(&p, ACT_FEED), PET_OK);
    CHECK_EQ(pet_need(&p, NEED_HUNGER), 70);
    CHECK_EQ(pet_act(&p, ACT_FEED), PET_OK);
    CHECK_EQ(pet_need(&p, NEED_HUNGER), 100);
    CHECK_EQ(pet_act(&p, ACT_FEED), PET_R_FULL);
    CHECK_EQ(pet_need(&p, NEED_HUNGER), 100);
}

TEST(pet_egg_refuses_actions) {
    pet_t p;
    fresh(&p);
    CHECK_EQ(pet_act(&p, ACT_FEED), PET_R_EGG);
    CHECK_EQ(pet_act(&p, ACT_SLEEP), PET_R_EGG);
    CHECK_EQ(p.flags & PET_ASLEEP, 0);
}

TEST(pet_sleeping_slows_decay_and_restores_energy) {
    pet_t awake, asleep;
    hatched(&awake);
    awake.need[NEED_ENERGY] = PET_PCT(50);
    asleep = awake;
    CHECK_EQ(pet_act(&asleep, ACT_SLEEP), PET_OK);
    CHECK(asleep.flags & PET_ASLEEP);
    pet_tick(&awake, 4 * MIN_MS);
    pet_tick(&asleep, 4 * MIN_MS);
    CHECK(pet_need(&asleep, NEED_HUNGER) > pet_need(&awake, NEED_HUNGER));
    CHECK(pet_need(&asleep, NEED_HAPPY) > pet_need(&awake, NEED_HAPPY));
    CHECK(pet_need(&asleep, NEED_HYGIENE) > pet_need(&awake, NEED_HYGIENE));
    CHECK(pet_need(&asleep, NEED_ENERGY) > 50);
    CHECK(pet_need(&awake, NEED_ENERGY) < 50);
    // Rested, it wakes by itself.
    for (int s = 0; s < 600 && (asleep.flags & PET_ASLEEP); s++) pet_tick(&asleep, 1000);
    CHECK_EQ(asleep.flags & PET_ASLEEP, 0);
    CHECK_EQ(pet_need(&asleep, NEED_ENERGY), 100);
}

TEST(pet_any_action_wakes_it) {
    pet_t p;
    hatched(&p);
    p.need[NEED_ENERGY] = PET_PCT(30);
    CHECK_EQ(pet_act(&p, ACT_SLEEP), PET_OK);
    CHECK_EQ(pet_act(&p, ACT_CLEAN), PET_OK);
    CHECK_EQ(p.flags & PET_ASLEEP, 0);
    CHECK_EQ(pet_act(&p, ACT_SLEEP), PET_OK);
    CHECK_EQ(pet_act(&p, ACT_SLEEP), PET_R_WOKE);
    CHECK_EQ(p.flags & PET_ASLEEP, 0);
    p.need[NEED_ENERGY] = PET_FULL;
    CHECK_EQ(pet_act(&p, ACT_SLEEP), PET_R_NOT_SLEEPY);
}

TEST(pet_droppings_appear_and_cleaning_removes_them) {
    pet_t p;
    hatched(&p);
    pet_tick(&p, (PET_POOP_MIN_S - 1) * 1000u);
    CHECK_EQ(p.poops, 0);
    pet_tick(&p, 1000);
    CHECK_EQ(p.poops, 1);
    pet_tick(&p, 120 * MIN_MS);
    CHECK_EQ(p.poops, PET_MAX_POOPS);
    CHECK_EQ(pet_need(&p, NEED_HYGIENE), 0);
    CHECK_EQ(pet_act(&p, ACT_CLEAN), PET_OK);
    CHECK_EQ(p.poops, 0);
    CHECK_EQ(pet_need(&p, NEED_HYGIENE), 100);
}

TEST(pet_neglect_makes_it_sick_and_medicine_cures) {
    pet_t p;
    hatched(&p);
    CHECK_EQ(pet_act(&p, ACT_MEDICINE), PET_R_NOT_SICK);
    p.need[NEED_HUNGER] = 0;
    pet_tick(&p, (PET_SICK_AFTER_S - 1) * 1000u);
    CHECK_EQ(p.flags & PET_SICK, 0);
    CHECK(pet_mood(&p) == MOOD_URGENT);
    pet_tick(&p, 1000);
    CHECK(p.flags & PET_SICK);
    CHECK_EQ(pet_wish(&p), WISH_MEDICINE);
    CHECK_EQ(pet_act(&p, ACT_PLAY), PET_R_SICK);
    CHECK_EQ(pet_act(&p, ACT_MEDICINE), PET_OK);
    CHECK_EQ(p.flags & PET_SICK, 0);
}

TEST(pet_wish_and_mood_follow_the_lowest_need) {
    pet_t p;
    hatched(&p);
    for (int i = 0; i < PET_NEEDS; i++) p.need[i] = PET_FULL;
    CHECK_EQ(pet_wish(&p), WISH_NONE);
    CHECK_EQ(pet_mood(&p), MOOD_HAPPY);
    p.need[NEED_HAPPY] = PET_PCT(30);
    p.need[NEED_ENERGY] = PET_PCT(20);
    CHECK_EQ(pet_wish(&p), WISH_SLEEP);
    CHECK_EQ(pet_mood(&p), MOOD_NEEDY);
    p.need[NEED_HUNGER] = PET_PCT(5);
    CHECK_EQ(pet_wish(&p), WISH_FOOD);
    CHECK_EQ(pet_mood(&p), MOOD_URGENT);
}

TEST(pet_clock_starts_in_the_morning_and_rolls_over) {
    pet_t p;
    fresh(&p);
    CHECK_EQ(pet_clock(&p), 8 * 60);
    CHECK_EQ(pet_day(&p), 1);
    pet_tick(&p, (PET_DAY_S - PET_DAWN_S) * 1000u);
    CHECK_EQ(pet_clock(&p), 0);
    CHECK_EQ(pet_day(&p), 2);
}

TEST(pet_app_runs_through_hatching) {
    // Smoke test: the app draws every frame from egg to adult without trouble.
    memset(g_save.pet, 0, SAVE_PET_BYTES);
    gfx_set_orientation(app_pet.orient);
    app_pet.enter();
    input_t none = {0, 0, 0}, x = {BTN_X, BTN_X, 0};
    for (int i = 0; i < 30 * 130; i++) {
        app_pet.background(33);
        CHECK(app_pet.frame(i % 300 == 0 ? &x : &none, 33));
    }
    CHECK_EQ(pet_saved()->stage, PET_BABY);
    pet_tick(pet_saved(), PET_GROW_S * 1000u);
    for (int i = 0; i < 30 * 4; i++) CHECK(app_pet.frame(&none, 33));
    CHECK_EQ(pet_saved()->stage, PET_ADULT);
    CHECK_EQ(pet_saved()->flags & PET_NEWS, 0);
}

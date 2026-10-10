#include "apps/pomodoro.h"
#include "core/app.h"
#include "core/hal.h"
#include "core/save.h"
#include "test.h"

#define MIN 60000u

// Runs the current phase out in one-second ticks and returns how many ticks it took.
static int run_out(pomo_t *p, uint32_t *count) {
    int ticks = 0;
    while (!pomo_tick(p, 1000, count) && ticks < 200000) ticks++;
    return ticks + 1;
}

TEST(pomodoro_starts_paused_at_full_focus) {
    pomo_t p;
    pomo_init(&p, 0);
    CHECK_EQ(p.phase, POMO_FOCUS);
    CHECK(!p.running);
    CHECK_EQ(p.remaining_ms, 25 * MIN);
    CHECK_EQ(pomo_duration(&p, POMO_SHORT), 5 * MIN);
    CHECK_EQ(pomo_duration(&p, POMO_LONG), 15 * MIN);
}

TEST(pomodoro_phase_sequence_has_long_break_after_fourth_focus) {
    pomo_t p;
    uint32_t count = 0;
    pomo_init(&p, 0);
    const pomo_phase_t want[] = {POMO_FOCUS, POMO_SHORT, POMO_FOCUS, POMO_SHORT, POMO_FOCUS,
                                 POMO_SHORT, POMO_FOCUS, POMO_LONG,  POMO_FOCUS, POMO_SHORT};
    pomo_toggle(&p);
    for (int i = 0; i < (int)(sizeof want / sizeof want[0]); i++) {
        CHECK_EQ(p.phase, want[i]);
        CHECK_EQ(p.remaining_ms, pomo_duration(&p, want[i]));
        CHECK(p.running);
        CHECK_EQ(run_out(&p, &count), (int)(pomo_duration(&p, want[i]) / 1000));
        CHECK(p.done);
        CHECK(!p.running);
        pomo_toggle(&p); // confirm: the next phase starts
    }
    CHECK_EQ(count, 5);
}

TEST(pomodoro_next_phase_waits_for_confirmation) {
    pomo_t p;
    pomo_init(&p, 0);
    pomo_toggle(&p);
    run_out(&p, NULL);
    CHECK(p.done);
    CHECK_EQ(pomo_next_phase(&p), POMO_SHORT);
    // More time passing changes nothing until A is pressed.
    CHECK(!pomo_tick(&p, 10 * MIN, NULL));
    CHECK_EQ(p.phase, POMO_FOCUS);
    CHECK_EQ(p.remaining_ms, 0);
    pomo_toggle(&p);
    CHECK_EQ(p.phase, POMO_SHORT);
    CHECK(p.running);
}

TEST(pomodoro_pause_stops_time) {
    pomo_t p;
    pomo_init(&p, 0);
    pomo_toggle(&p);
    pomo_tick(&p, 5000, NULL);
    CHECK_EQ(p.remaining_ms, 25 * MIN - 5000);
    pomo_toggle(&p);
    CHECK(!p.running);
    pomo_tick(&p, 60000, NULL);
    CHECK_EQ(p.remaining_ms, 25 * MIN - 5000);
    pomo_toggle(&p);
    pomo_tick(&p, 1000, NULL);
    CHECK_EQ(p.remaining_ms, 25 * MIN - 6000);
}

TEST(pomodoro_skip_advances_without_counting) {
    pomo_t p;
    uint32_t count = 0;
    pomo_init(&p, 0);
    pomo_skip(&p);
    CHECK_EQ(p.phase, POMO_SHORT);
    CHECK_EQ(p.set_pos, 1);
    CHECK_EQ(p.remaining_ms, 5 * MIN);
    CHECK(!p.running); // it was paused, so it stays paused
    pomo_toggle(&p);
    pomo_skip(&p);
    CHECK_EQ(p.phase, POMO_FOCUS);
    CHECK(p.running);
    pomo_skip(&p);
    pomo_skip(&p);
    pomo_skip(&p);
    pomo_skip(&p);
    pomo_skip(&p);
    CHECK_EQ(p.phase, POMO_LONG);
    pomo_skip(&p);
    CHECK_EQ(p.phase, POMO_FOCUS);
    CHECK_EQ(p.set_pos, 0);
    CHECK_EQ(count, 0);
}

TEST(pomodoro_reset_restores_full_duration) {
    pomo_t p;
    pomo_init(&p, 0);
    pomo_toggle(&p);
    pomo_tick(&p, 7 * MIN + 1234, NULL);
    pomo_reset(&p);
    CHECK_EQ(p.remaining_ms, 25 * MIN);
    CHECK(!p.running);
    CHECK_EQ(p.phase, POMO_FOCUS);
    pomo_skip(&p);
    pomo_toggle(&p);
    pomo_tick(&p, 2 * MIN, NULL);
    pomo_reset(&p);
    CHECK_EQ(p.remaining_ms, 5 * MIN);
}

TEST(pomodoro_completed_focus_counts_once) {
    pomo_t p;
    uint32_t count = 0;
    pomo_init(&p, 0);
    pomo_toggle(&p);
    CHECK(pomo_tick(&p, 25 * MIN, &count));
    CHECK_EQ(count, 1);
    for (int i = 0; i < 100; i++) CHECK(!pomo_tick(&p, 1000, &count));
    CHECK_EQ(count, 1);
    pomo_toggle(&p); // short break
    run_out(&p, &count);
    CHECK_EQ(count, 1); // a finished break is not a focus
}

TEST(pomodoro_presets_scale_the_breaks) {
    pomo_t p;
    pomo_init(&p, 0);
    pomo_next_preset(&p);
    CHECK_EQ(p.remaining_ms, 50 * MIN);
    CHECK_EQ(pomo_duration(&p, POMO_SHORT), 10 * MIN);
    CHECK_EQ(pomo_duration(&p, POMO_LONG), 30 * MIN);
    pomo_next_preset(&p);
    CHECK_EQ(p.remaining_ms, 15 * MIN);
    pomo_next_preset(&p);
    CHECK_EQ(p.remaining_ms, 25 * MIN);
}

TEST(pomodoro_background_time_accumulates_in_other_apps) {
    pomo_t *p = pomodoro_state();
    pomo_init(p, 0);
    app_pomodoro.enter();
    input_t none = {0, 0, 0};
    input_t down = {BTN_A, BTN_A, 0}, up = {0, 0, BTN_A};
    app_pomodoro.frame(&down, 0);
    app_pomodoro.frame(&up, 0); // A acts on release
    CHECK(p->running);
    // Another app is open now: only the frame loop's background ticks reach the timer.
    for (int i = 0; i < 240; i++) app_pomodoro.background(250);
    CHECK_EQ(p->remaining_ms, 24 * MIN);
    // Back in the app, drawing frames does not move the clock on its own.
    for (int i = 0; i < 10; i++) app_pomodoro.frame(&none, 100);
    CHECK_EQ(p->remaining_ms, 24 * MIN);
    // Run it out in the background: the save counter goes up once.
    uint32_t before = g_save.pomodoro_done;
    for (int i = 0; i < 24 * 60 * 4 + 100; i++) app_pomodoro.background(250);
    CHECK(p->done);
    CHECK_EQ(g_save.pomodoro_done, before + 1);
    app_pomodoro.frame(&none, 100);
    pomo_init(p, 0);
}

TEST(pomodoro_holding_a_and_b_to_leave_changes_nothing) {
    pomo_t *p = pomodoro_state();
    pomo_init(p, 0);
    pomo_toggle(p);
    pomo_tick(p, 3000, NULL);
    app_pomodoro.enter();
    input_t a = {BTN_A, BTN_A, 0}, ab = {BTN_A | BTN_B, BTN_B, 0}, b = {BTN_B, 0, BTN_A}, none = {0, 0, BTN_B};
    app_pomodoro.frame(&a, 16);
    app_pomodoro.frame(&ab, 16);
    app_pomodoro.frame(&b, 16);
    app_pomodoro.frame(&none, 16);
    CHECK(p->running);
    CHECK_EQ(p->remaining_ms, 25 * MIN - 3000);
    pomo_init(p, 0);
}

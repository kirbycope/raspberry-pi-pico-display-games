#include <string.h>

#include "apps/flappy.h"
#include "test.h"

static flappy_t f;

// A bird with the pipes moved far away, so nothing scrolls into it.
static void clear_sky(void) {
    flappy_reset(&f, 135);
    for (int i = 0; i < FLAP_PIPES; i++) f.pipe[i].x = (1000 + i * FLAP_PIPE_SPACING) << 8;
}

TEST(flappy_bird_falls_under_gravity) {
    clear_sky();
    int32_t y0 = f.y;
    flappy_fall(&f, 100);
    CHECK(f.y > y0);
    CHECK(f.vy > 0);
    int32_t v1 = f.vy, y1 = f.y;
    flappy_fall(&f, 100);
    CHECK(f.vy > v1);
    CHECK(f.y - y1 > y1 - y0); // speeding up
    // Terminal speed.
    for (int i = 0; i < 50; i++) flappy_fall(&f, 100);
    CHECK_EQ(f.vy, FLAP_MAX_FALL * 256);
}

TEST(flappy_bird_rises_on_flap) {
    clear_sky();
    flappy_fall(&f, 200);
    int32_t y0 = f.y;
    flappy_flap(&f);
    CHECK(f.vy < 0);
    flappy_fall(&f, 50);
    CHECK(f.y < y0);
    // The arc peaks after about v / g seconds and comes back down.
    int32_t peak = f.y;
    for (int i = 0; i < 60; i++) {
        flappy_fall(&f, 10);
        if (f.y < peak) peak = f.y;
    }
    CHECK(f.y > peak);
    int rise = (y0 - peak) >> 8;
    CHECK(rise >= 15 && rise <= 35);
}

TEST(flappy_fall_is_framerate_independent) {
    clear_sky();
    flappy_flap(&f);
    flappy_t g = f;
    for (int i = 0; i < 30; i++) flappy_fall(&f, 10);
    for (int i = 0; i < 6; i++) flappy_fall(&g, 50);
    int diff = (f.y - g.y) >> 8;
    CHECK(diff >= -1 && diff <= 1);
}

TEST(flappy_collides_with_a_pipe) {
    clear_sky();
    f.pipe[0].x = (FLAP_BIRD_X - FLAP_PIPE_W / 2) << 8;
    f.pipe[0].gap_y = 80;
    // In the middle of the gap: safe.
    f.y = (80 + FLAP_GAP / 2) << 8;
    CHECK(!flappy_hits_pipe(&f));
    // Touching the top pipe or the bottom pipe.
    f.y = (80 + FLAP_BIRD_HH - 1) << 8;
    CHECK(flappy_hits_pipe(&f));
    f.y = (80 + FLAP_GAP - FLAP_BIRD_HH) << 8;
    CHECK(flappy_hits_pipe(&f));
    // Far above the screen the pipe still reaches.
    f.y = FLAP_CEILING * 256;
    CHECK(flappy_hits_pipe(&f));
    // Same height, but the pipe is still well to the right.
    f.y = 20 << 8;
    f.pipe[0].x = (FLAP_BIRD_X + FLAP_BIRD_HW + 10) << 8;
    CHECK(!flappy_hits_pipe(&f));
}

TEST(flappy_collides_with_the_ground) {
    clear_sky();
    f.y = (FLAP_GROUND_Y - FLAP_BIRD_HH - 1) << 8;
    CHECK(!flappy_hits_ground(&f));
    f.y += 256;
    CHECK(flappy_hits_ground(&f));
    // Falling from the middle lands within a couple of seconds.
    clear_sky();
    int ms = 0;
    while (!flappy_hits_ground(&f) && ms < 5000) {
        flappy_fall(&f, 16);
        ms += 16;
    }
    CHECK(ms < 2000);
}

TEST(flappy_scores_once_per_pipe_passed) {
    flappy_reset(&f, 135);
    // Hold the bird in each gap so it never crashes, and fly through ten pipes.
    int total = 0;
    for (int ms = 0; ms < 60000 && f.score < 10; ms += 16) {
        total += flappy_scroll(&f, 16);
        CHECK(total == (int)f.score);
    }
    CHECK_EQ(f.score, 10);
    // Each pipe counts once even as it lingers left of the bird.
    for (int i = 0; i < FLAP_PIPES; i++) f.pipe[i].x = (FLAP_BIRD_X - FLAP_PIPE_W) << 8, f.pipe[i].scored = true;
    CHECK_EQ(flappy_scroll(&f, 16), 0);
    // A pipe whose middle has just crossed the bird scores exactly one.
    flappy_reset(&f, 135);
    f.pipe[0].x = ((FLAP_BIRD_X - FLAP_PIPE_W / 2) << 8) + 100;
    int got = 0;
    for (int i = 0; i < 20; i++) got += flappy_scroll(&f, 16);
    CHECK_EQ(got, 1);
}

TEST(flappy_pipe_gaps_stay_on_screen) {
    flappy_reset(&f, 135);
    for (int ms = 0; ms < 600000; ms += 33) {
        flappy_scroll(&f, 33);
        for (int i = 0; i < FLAP_PIPES; i++) {
            CHECK(f.pipe[i].gap_y >= FLAP_GAP_MIN);
            CHECK(f.pipe[i].gap_y + FLAP_GAP <= FLAP_GROUND_Y - 10);
        }
    }
    // Neighbouring gaps are never further apart than a bird can climb.
    for (int prev = FLAP_GAP_MIN; prev <= FLAP_GAP_MAX; prev++)
        for (int k = 0; k < 20; k++) {
            int g = flappy_next_gap(prev);
            CHECK(g >= FLAP_GAP_MIN && g <= FLAP_GAP_MAX);
            CHECK(g - prev <= FLAP_GAP_STEP && prev - g <= FLAP_GAP_STEP);
        }
    // Pipes stay evenly spaced as they are recycled.
    for (int i = 0; i < FLAP_PIPES; i++)
        for (int j = 0; j < FLAP_PIPES; j++) {
            int d = (f.pipe[i].x - f.pipe[j].x) >> 8;
            if (d < 0) d = -d;
            CHECK(d == 0 || d % FLAP_PIPE_SPACING <= 1 || d % FLAP_PIPE_SPACING >= FLAP_PIPE_SPACING - 1);
        }
}

TEST(flappy_medals_at_10_20_40) {
    CHECK_EQ(flappy_medal(9), 0);
    CHECK_EQ(flappy_medal(10), 1);
    CHECK_EQ(flappy_medal(19), 1);
    CHECK_EQ(flappy_medal(20), 2);
    CHECK_EQ(flappy_medal(39), 2);
    CHECK_EQ(flappy_medal(40), 3);
}

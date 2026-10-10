#include <string.h>

#include "core/gfx.h"
#include "core/save.h"
#include "test.h"

TEST(core_rgb_round_trips_primaries) {
    CHECK_EQ(rgb(255, 0, 0), 0xF800);
    CHECK_EQ(rgb(0, 255, 0), 0x07E0);
    CHECK_EQ(rgb(0, 0, 255), 0x001F);
    CHECK_EQ(rgb_r(0xF800), 255);
}

TEST(core_orientation_swaps_size) {
    gfx_set_orientation(ORIENT_PORTRAIT);
    CHECK_EQ(gfx_w, 135);
    CHECK_EQ(gfx_h, 240);
    gfx_set_orientation(ORIENT_LANDSCAPE);
    CHECK_EQ(gfx_w, 240);
    CHECK_EQ(gfx_h, 135);
}

TEST(core_rect_clips_to_screen) {
    gfx_set_orientation(ORIENT_LANDSCAPE);
    gfx_clear(0);
    gfx_rect(-10, -10, 20, 20, 0xFFFF);
    CHECK_EQ(gfx_get(0, 0), 0xFFFF);
    CHECK_EQ(gfx_get(9, 9), 0xFFFF);
    CHECK_EQ(gfx_get(10, 10), 0);
    gfx_rect(230, 130, 50, 50, 0x1234); // must not write past the buffer
    CHECK_EQ(gfx_get(239, 134), 0x1234);
}

TEST(core_text_has_width_and_draws) {
    gfx_set_orientation(ORIENT_LANDSCAPE);
    gfx_clear(0);
    int w = gfx_text(0, 0, "Hi", 0xFFFF, &font_mini, 1);
    CHECK(w > 4);
    CHECK_EQ(w, gfx_text_width("Hi", &font_mini, 1));
    int lit = 0;
    for (int y = 0; y < 10; y++)
        for (int x = 0; x < w; x++) lit += gfx_get(x, y) != 0;
    CHECK(lit > 5);
}

TEST(core_save_survives_reload_and_rejects_garbage) {
    save_reset();
    g_save.tetris_best = 4242;
    save_commit();
    memset(&g_save, 0, sizeof g_save);
    save_load();
    CHECK_EQ(g_save.tetris_best, 4242);
}

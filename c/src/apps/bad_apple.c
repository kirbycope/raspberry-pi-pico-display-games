// Bad Apple!!, the shadow-art video, played from flash in black and white at
// 180x135 and 30 frames a second. No sound: the Display Pack has no speaker.
//
// A: pause  B: back to the start  X: skip ahead 10 seconds  Y: invert
//
// The frames are not in this repository. tools/encode_bad_apple.py makes
// data/bad_apple.bin from a copy of the video, and the next build links it
// in; without it this app explains how.
#include "bad_apple.h"

#include <stdio.h>
#include <string.h>

#include "../core/app.h"
#include "../core/hal.h"

extern const uint8_t bad_apple_data[], bad_apple_data_end[];

static uint32_t read_u32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t read_u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static inline void br_fill(ba_t *b) {
    while (b->bits <= 24) {
        uint32_t byte = b->p < b->end ? *b->p++ : 0;
        b->buf |= byte << (24 - b->bits);
        b->bits += 8;
    }
}

static inline uint32_t br_read(ba_t *b, int n) {
    if (n == 0) return 0;
    br_fill(b);
    uint32_t v = b->buf >> (32 - n);
    b->buf <<= n;
    b->bits -= n;
    return v;
}

static inline uint32_t br_golomb(ba_t *b, int k) {
    int zeros = 0;
    br_fill(b);
    while (!(b->buf & 0x80000000u)) {
        // Skip whole runs of zero bits at a time once the buffer is all zeros.
        if (b->buf == 0) {
            zeros += b->bits;
            b->bits = 0;
            if (b->p >= b->end || zeros > 32) return 0xFFFFFFFFu;
        } else {
            int lz = __builtin_clz(b->buf);
            zeros += lz;
            b->buf <<= lz;
            b->bits -= lz;
        }
        br_fill(b);
    }
    int n = zeros + k + 1;
    uint32_t v = n > 24 ? (br_read(b, n - 16) << 16) | br_read(b, 16) : br_read(b, n);
    return v - (1u << k);
}

// Drops the bits left in the current byte, since every frame starts on one.
static void br_align(ba_t *b) {
    int consumed = (int)((b->p - b->first_frame) * 8) - b->bits;
    br_read(b, (8 - (consumed & 7)) & 7);
}

bool ba_open(ba_t *b, const uint8_t *data, size_t len) {
    memset(b, 0, sizeof *b);
    if (len < 16 || memcmp(data, "BAD1", 4) != 0) return false;
    b->data = data;
    b->end = data + len;
    b->w = read_u16(data + 4);
    b->h = read_u16(data + 6);
    b->fps = read_u16(data + 8);
    b->frames = read_u32(data + 12);
    if (!b->w || !b->h || b->w > BA_MAX_W || b->h > BA_MAX_H || !b->fps) return false;
    b->first_frame = data + 16;
    ba_restart(b);
    return true;
}

void ba_restart(ba_t *b) {
    b->p = b->first_frame;
    b->buf = 0;
    b->bits = 0;
    b->index = 0;
    b->cur = 0;
    memset(b->planes, 0, sizeof b->planes);
}

// Sets pixels [pos, pos + n) of the plane, counting row by row.
static void set_run(uint32_t plane[BA_MAX_H][BA_STRIDE], uint32_t w, uint32_t pos, uint32_t n) {
    while (n) {
        uint32_t y = pos / w, x = pos % w;
        uint32_t len = w - x < n ? w - x : n;
        uint32_t *row = plane[y];
        uint32_t x0 = x, x1 = x + len; // [x0, x1)
        while (x0 < x1) {
            uint32_t word = x0 >> 5, lo = x0 & 31;
            uint32_t hi = (x1 - (word << 5)) < 32 ? (x1 - (word << 5)) : 32;
            uint32_t mask = (hi == 32 ? 0xFFFFFFFFu : ((1u << hi) - 1)) & ~((1u << lo) - 1);
            row[word] |= mask;
            x0 = (word + 1) << 5;
        }
        pos += len;
        n -= len;
    }
}

bool ba_next(ba_t *b) {
    if (b->index >= b->frames) return false;
    int next = b->cur ^ 1;
    uint32_t (*plane)[BA_STRIDE] = b->planes[next];
    const uint32_t (*prev)[BA_STRIDE] = (const uint32_t (*)[BA_STRIDE])b->planes[b->cur];
    memset(plane, 0, sizeof b->planes[0]);

    uint32_t mode = br_read(b, 2);
    int k0 = (int)br_read(b, 3), k1 = (int)br_read(b, 3);
    uint32_t total = (uint32_t)b->w * b->h, pos = 0;
    for (int value = 0; pos < total; value ^= 1) {
        uint32_t n = br_golomb(b, value ? k1 : k0);
        if (n > total - pos) n = total - pos; // a damaged stream stops at the frame's end
        if (value) set_run(plane, b->w, pos, n);
        pos += n;
    }
    br_align(b);

    int words = (b->w + 31) / 32;
    if (mode == 2 || mode == 3)
        for (int y = 1; y < b->h; y++)
            for (int i = 0; i < words; i++) plane[y][i] ^= plane[y - 1][i];
    if (mode == 1 || mode == 3)
        for (int y = 0; y < b->h; y++)
            for (int i = 0; i < words; i++) plane[y][i] ^= prev[y][i];
    b->cur = next;
    b->index++;
    return true;
}

static ba_t video;
static bool have_video, paused, inverted;
static uint32_t play_ms, hint_ms;

static void ba_enter(void) {
    have_video = ba_open(&video, bad_apple_data, (size_t)(bad_apple_data_end - bad_apple_data));
    paused = false;
    play_ms = 0;
    hint_ms = 3000;
    if (have_video) ba_next(&video);
}

static void draw_missing(void) {
    gfx_clear(rgb(16, 16, 20));
    gfx_text_center(SCREEN_LONG / 2, 14, "BAD APPLE", C_WHITE, &font_blocks, 2);
    const char *lines[] = {
        "The video frames are not built in.",
        "On your computer, run:",
        "python c/tools/encode_bad_apple.py",
        "    <your copy of the video>",
        "then build and flash again.",
    };
    for (int i = 0; i < 5; i++)
        gfx_text(14, 46 + i * 14, lines[i], i == 2 || i == 3 ? C_YELLOW : rgb(200, 200, 210), &font_mini, 1);
}

static void draw_frame(void) {
    uint16_t on = inverted ? C_BLACK : C_WHITE, off = inverted ? C_WHITE : C_BLACK;
    int ox = (SCREEN_LONG - video.w) / 2, oy = (SCREEN_SHORT - video.h) / 2;
    gfx_clear(C_BLACK);
    uint32_t lit = 0;
    for (int y = 0; y < video.h; y++) {
        uint16_t *row = &gfx_fb[(oy + y) * SCREEN_LONG + ox];
        const uint32_t *bits = video.planes[video.cur][y];
        for (int x = 0; x < video.w; x++) {
            int v = (int)((bits[x >> 5] >> (x & 31)) & 1u);
            row[x] = v ? on : off;
            lit += (uint32_t)v;
        }
    }
    // The LED glows with how much of the picture is white.
    uint8_t level = (uint8_t)(lit * 48u / ((uint32_t)video.w * video.h));
    if (inverted) level = (uint8_t)(48 - level);
    hal_led(level, level, level);
}

static void draw_overlay(void) {
    uint32_t total_s = video.frames / video.fps, now_s = video.index / video.fps;
    char buf[32];
    snprintf(buf, sizeof buf, "%lu:%02lu / %lu:%02lu", (unsigned long)(now_s / 60), (unsigned long)(now_s % 60),
             (unsigned long)(total_s / 60), (unsigned long)(total_s % 60));
    gfx_blend_rect(0, SCREEN_SHORT - 22, SCREEN_LONG, 22, C_BLACK, 190);
    int bar = (int)((uint64_t)video.index * (SCREEN_LONG - 20) / video.frames);
    gfx_rect(10, SCREEN_SHORT - 20, SCREEN_LONG - 20, 3, rgb(70, 70, 80));
    gfx_rect(10, SCREEN_SHORT - 20, bar, 3, rgb(230, 60, 60));
    gfx_text(10, SCREEN_SHORT - 13, buf, C_WHITE, &font_mini, 1);
    gfx_text(SCREEN_LONG - 10 - gfx_text_width("A pause  X +10s", &font_mini, 1), SCREEN_SHORT - 13, "A pause  X +10s",
             rgb(170, 170, 180), &font_mini, 1);
    if (paused) {
        gfx_rect(SCREEN_LONG / 2 - 10, SCREEN_SHORT / 2 - 14, 7, 24, C_RED);
        gfx_rect(SCREEN_LONG / 2 + 3, SCREEN_SHORT / 2 - 14, 7, 24, C_RED);
    }
}

static bool ba_frame(const input_t *in, uint32_t dt) {
    if (!have_video) {
        draw_missing();
        return true;
    }
    if (in->pressed & BTN_A) { paused = !paused; hint_ms = 0; }
    if (in->pressed & BTN_Y) inverted = !inverted;
    if (in->pressed & BTN_B) {
        ba_restart(&video);
        ba_next(&video);
        play_ms = 0;
        hint_ms = 2000;
    }
    if (in->pressed & BTN_X) {
        play_ms += 10000;
        hint_ms = 2000;
    }
    if (!paused) play_ms += dt;
    if (hint_ms) hint_ms = hint_ms > dt ? hint_ms - dt : 0;

    // Decode up to the frame the clock says should be showing; a skip ahead
    // decodes the frames in between without drawing them.
    uint32_t target = (uint32_t)((uint64_t)play_ms * video.fps / 1000);
    if (target >= video.frames) {
        // The end: start again after a breath.
        if (play_ms > (uint64_t)video.frames * 1000 / video.fps + 2000) {
            ba_restart(&video);
            ba_next(&video);
            play_ms = 0;
        }
    } else {
        while (video.index <= target && ba_next(&video)) {}
    }
    draw_frame();
    if (paused || hint_ms) draw_overlay();
    return true;
}

const app_t app_bad_apple = {
    .name = "Bad Apple",
    .orient = ORIENT_LANDSCAPE,
    .accent = rgb(235, 235, 235),
    .fps = 30,
    .enter = ba_enter,
    .frame = ba_frame,
};

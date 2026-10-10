// The decoder against streams written here by a small encoder that follows
// the format in tools/encode_bad_apple.py, one test per coding mode.
#include <stdlib.h>
#include <string.h>

#include "apps/bad_apple.h"
#include "test.h"

typedef struct {
    uint8_t data[8192];
    size_t len;
    uint32_t acc;
    int n;
} writer_t;

static void put(writer_t *w, uint32_t v, int bits) {
    for (int i = bits - 1; i >= 0; i--) {
        w->acc = (w->acc << 1) | ((v >> i) & 1);
        if (++w->n == 8) w->data[w->len++] = (uint8_t)w->acc, w->acc = 0, w->n = 0;
    }
}

static void golomb(writer_t *w, uint32_t v, int k) {
    v += 1u << k;
    int nb = 32 - __builtin_clz(v);
    put(w, 0, nb - 1 - k);
    put(w, v, nb);
}

static void align(writer_t *w) {
    if (w->n) put(w, 0, 8 - w->n);
}

static void header(writer_t *w, int width, int height, int frames) {
    memset(w, 0, sizeof *w);
    memcpy(w->data, "BAD1", 4);
    uint8_t h[12] = {(uint8_t)width, (uint8_t)(width >> 8), (uint8_t)height, (uint8_t)(height >> 8), 30, 0, 0, 0,
                     (uint8_t)frames, 0, 0, 0};
    memcpy(w->data + 4, h, 12);
    w->len = 16;
}

// Writes bits (width*height values of 0/1, already transformed for mode).
static void frame(writer_t *w, int mode, const uint8_t *bits, int count, int k0, int k1) {
    put(w, (uint32_t)mode, 2);
    put(w, (uint32_t)k0, 3);
    put(w, (uint32_t)k1, 3);
    int value = 0, run = 0, i = 0, nth = 0;
    while (i < count) {
        if (bits[i] == value) { run++; i++; continue; }
        golomb(w, (uint32_t)run, nth % 2 ? k1 : k0);
        nth++;
        value ^= 1;
        run = 0;
    }
    golomb(w, (uint32_t)run, nth % 2 ? k1 : k0);
    align(w);
}

static ba_t b; // too big for comfortable stack use

#define W 37
#define H 9

static void pattern(uint8_t *px, int seed) {
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) px[y * W + x] = (uint8_t)(((x * 7 + y * 3 + seed) % 11) < 4);
}

static int matches(const uint8_t *px) {
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (ba_pixel(&b, x, y) != px[y * W + x]) return 0;
    return 1;
}

TEST(bad_apple_rejects_missing_or_foreign_data) {
    uint8_t junk[32] = "NOPE";
    CHECK(!ba_open(&b, junk, sizeof junk));
    CHECK(!ba_open(&b, junk, 0));
}

TEST(bad_apple_decodes_each_mode) {
    static writer_t w;
    uint8_t f0[W * H], f1[W * H], f2[W * H], f3[W * H], t[W * H];
    pattern(f0, 0);
    pattern(f1, 3);
    pattern(f2, 5);
    pattern(f3, 9);
    header(&w, W, H, 4);
    // Frame 0, direct.
    frame(&w, 0, f0, W * H, 2, 1);
    // Frame 1, XOR with frame 0.
    for (int i = 0; i < W * H; i++) t[i] = f1[i] ^ f0[i];
    frame(&w, 1, t, W * H, 0, 3);
    // Frame 2, each row XOR the row above.
    for (int i = 0; i < W * H; i++) t[i] = (uint8_t)(i < W ? f2[i] : f2[i] ^ f2[i - W]);
    frame(&w, 2, t, W * H, 5, 0);
    // Frame 3, XOR with frame 2 then row delta.
    uint8_t x[W * H];
    for (int i = 0; i < W * H; i++) x[i] = f3[i] ^ f2[i];
    for (int i = 0; i < W * H; i++) t[i] = (uint8_t)(i < W ? x[i] : x[i] ^ x[i - W]);
    frame(&w, 3, t, W * H, 1, 1);

    CHECK(ba_open(&b, w.data, w.len));
    CHECK_EQ(b.w, W);
    CHECK_EQ(b.h, H);
    CHECK_EQ(b.frames, 4);
    CHECK(ba_next(&b)); CHECK(matches(f0));
    CHECK(ba_next(&b)); CHECK(matches(f1));
    CHECK(ba_next(&b)); CHECK(matches(f2));
    CHECK(ba_next(&b)); CHECK(matches(f3));
    CHECK(!ba_next(&b));

    // Restarting plays it again from the first frame.
    ba_restart(&b);
    CHECK(ba_next(&b)); CHECK(matches(f0));
}

TEST(bad_apple_handles_long_runs_and_full_frames) {
    static writer_t w;
    static uint8_t ones[180 * 135], zeros[180 * 135];
    memset(ones, 1, sizeof ones);
    header(&w, 180, 135, 2);
    w.data[4] = 180, w.data[6] = 135;
    frame(&w, 0, ones, 180 * 135, 0, 7);  // a zero-length run, then 24,300 ones
    frame(&w, 0, zeros, 180 * 135, 7, 0); // a single run of zeros
    CHECK(ba_open(&b, w.data, w.len));
    CHECK(ba_next(&b));
    CHECK_EQ(ba_pixel(&b, 0, 0), 1);
    CHECK_EQ(ba_pixel(&b, 179, 134), 1);
    CHECK_EQ(ba_pixel(&b, 90, 67), 1);
    CHECK(ba_next(&b));
    CHECK_EQ(ba_pixel(&b, 0, 0), 0);
    CHECK_EQ(ba_pixel(&b, 179, 134), 0);
}

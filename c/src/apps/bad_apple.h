// The Bad Apple!! decoder: reads the stream tools/encode_bad_apple.py writes
// (its docstring is the format) one frame at a time into a 1-bit plane.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BA_MAX_W 240
#define BA_MAX_H 135
#define BA_STRIDE ((BA_MAX_W + 31) / 32) // 32-bit words per row

typedef struct {
    const uint8_t *data, *end, *first_frame, *p;
    uint32_t buf;
    int bits;
    uint16_t w, h, fps;
    uint32_t frames, index; // index = frames decoded so far
    uint32_t planes[2][BA_MAX_H][BA_STRIDE];
    int cur; // which plane holds the latest frame
} ba_t;

// False if the data is missing, too short, or not a "BAD1" stream.
bool ba_open(ba_t *b, const uint8_t *data, size_t len);
void ba_restart(ba_t *b);
// Decodes the next frame. False at the end of the stream.
bool ba_next(ba_t *b);

static inline int ba_pixel(const ba_t *b, int x, int y) {
    return (int)((b->planes[b->cur][y][x >> 5] >> (x & 31)) & 1u);
}

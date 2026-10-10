// Bouncing DVD logo: the motion, kept apart from drawing so it can be tested.
#pragma once

#include <stdint.h>

// Positions and speeds are in 1/256 pixel.
typedef struct {
    int32_t x, y, vx, vy;
    int w, h;
} dvd_t;

#define DVD_HIT_X 1
#define DVD_HIT_Y 2
#define DVD_CORNER (DVD_HIT_X | DVD_HIT_Y)

// Moves the logo dt_ms within a w x h screen, bouncing off the edges.
// Returns which walls it touched.
int dvd_step(dvd_t *d, uint32_t dt_ms, int screen_w, int screen_h);

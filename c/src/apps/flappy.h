// Flappy Bird's physics, pipes and scoring, kept apart from drawing so they
// can be tested. Positions are in 1/256 pixel and speeds in 1/256 pixel per
// second, so the motion is the same at any frame rate.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FLAP_GROUND_Y 214  // top of the ground strip
#define FLAP_BIRD_X 40     // the bird's centre, it never moves sideways
#define FLAP_BIRD_HW 6     // half the hitbox, a little inside the sprite
#define FLAP_BIRD_HH 5
#define FLAP_PIPE_W 22
#define FLAP_GAP 54
#define FLAP_PIPE_SPACING 80
#define FLAP_PIPES 3
#define FLAP_GAP_MIN 28                                // highest top of a gap
#define FLAP_GAP_MAX (FLAP_GROUND_Y - 22 - FLAP_GAP)   // lowest top of a gap
#define FLAP_GAP_STEP 70   // most a gap moves from the one before
#define FLAP_SPEED 62      // pixels per second
#define FLAP_GRAVITY 560   // pixels per second squared
#define FLAP_IMPULSE (-160)
#define FLAP_MAX_FALL 260
#define FLAP_CEILING (-24) // the bird may leave the top, but not far

typedef struct {
    int32_t x;     // left edge, 1/256 pixel
    int16_t gap_y; // top of the gap, pixels
    bool scored;
} flappy_pipe_t;

typedef struct {
    int32_t y, vy; // the bird's centre
    flappy_pipe_t pipe[FLAP_PIPES];
    uint32_t score;
    uint32_t scroll; // distance travelled, 1/256 pixel, for the ground stripes
} flappy_t;

// The bird mid-screen and a fresh row of pipes off the right edge.
void flappy_reset(flappy_t *f, int screen_w);
void flappy_flap(flappy_t *f);
// Gravity for dt milliseconds.
void flappy_fall(flappy_t *f, uint32_t dt_ms);
// Scrolls the pipes, recycles those that left, and returns the points scored.
int flappy_scroll(flappy_t *f, uint32_t dt_ms);
// A random gap top within reach of the previous one, always fully on screen.
int flappy_next_gap(int prev);
bool flappy_hits_pipe(const flappy_t *f);
bool flappy_hits_ground(const flappy_t *f);
// 0 none, 1 bronze (10), 2 silver (20), 3 gold (40).
int flappy_medal(uint32_t score);

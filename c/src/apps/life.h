// Conway's Game of Life on an 80x45 torus: the rules, kept apart from
// drawing so they can be tested.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LIFE_W 80
#define LIFE_H 45
#define LIFE_CELL 3 // pixels per cell, so the grid fills 240x135 exactly

// One byte per cell. A live cell has LIFE_ALIVE set and its age in
// generations (1..LIFE_AGE_MAX) in the low bits. A dead cell holds how many
// generations of fading trail it has left, 0..LIFE_TRAIL.
#define LIFE_ALIVE 0x80
#define LIFE_AGE_MAX 127
#define LIFE_TRAIL 5

typedef struct {
    uint8_t cells[LIFE_H][LIFE_W];
    uint32_t generation;
    int population;
    uint32_t hash; // of the live cells' positions, to spot repeats
} life_t;

void life_clear(life_t *l);
// Coordinates wrap, so any x and y address a cell.
void life_set(life_t *l, int x, int y, bool alive);
bool life_get(const life_t *l, int x, int y);
// Counts the live cells. life_t.population keeps the same number up to date.
int life_count(const life_t *l);
// Advances one generation in place.
void life_step(life_t *l);
// Stamps a pattern written as rows of '.' and 'O', ended by NULL, with its
// top left at x, y.
// flip_x mirrors it, so a spaceship can face the other way.
void life_stamp(life_t *l, const char *const *rows, int x, int y, bool flip_x);

// Watches for a world that has settled: no live cells, a pattern that keeps
// coming back within a short period (still lifes, oscillators), or a
// population stuck in a short cycle for a long time (a lone glider).
#define LIFE_HASHES 32
#define LIFE_POPS 8
typedef struct {
    uint32_t hashes[LIFE_HASHES];
    int pops[LIFE_POPS];
    uint32_t seen;      // generations pushed
    uint32_t repeat_run; // generations in a row that matched an earlier hash
    uint16_t pop_runs[LIFE_POPS]; // per period p: generations in a row whose population matched p ago
} life_watch_t;

#define LIFE_REPEAT_GENS 150 // a repeating pattern settles after this many generations
#define LIFE_POP_GENS 500    // a population cycle settles after this many

void life_watch_reset(life_watch_t *w);
// Feed it each new generation; true once the world counts as settled.
bool life_watch_push(life_watch_t *w, const life_t *l);

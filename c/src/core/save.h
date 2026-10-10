// Everything kept across power cycles, in one struct.
//
// Apps write their fields in g_save and the frame loop stores it when an app
// is left. An app that changes state while open for a long time (the pet)
// calls save_commit() itself now and then. A struct that fails its check,
// including one from an older layout, is replaced with defaults.
#pragma once

#include <stdint.h>

#define SAVE_PET_BYTES 64
#define SAVE_RPG_BYTES 128

typedef struct {
    uint32_t magic;
    uint8_t portrait_flip;
    uint8_t menu_index;
    uint8_t reserved[2];
    uint32_t tetris_best;
    uint32_t flappy_best;
    uint32_t dvd_corners;
    uint32_t pomodoro_done;
    uint32_t life_generations;
    uint8_t pet[SAVE_PET_BYTES];
    uint8_t rpg[SAVE_RPG_BYTES];
    uint32_t check;
} save_t;

extern save_t g_save;

void save_load(void);
// Writes only when something changed since the last load or commit.
void save_commit(void);
void save_reset(void);

// What every app gives the launcher.
//
// The frame loop in main.c owns timing, input and presenting. Each frame it
// calls frame() with the buttons and the milliseconds since the last frame;
// the app updates and draws into gfx_fb and returns false to go back to the
// menu. Holding A+B for a second also leaves any app, so apps never need an
// exit control of their own.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

typedef struct {
    uint8_t held;     // buttons down now
    uint8_t pressed;  // went down this frame
    uint8_t released; // came up this frame
} input_t;

typedef struct {
    const char *name;
    orient_t orient;
    uint16_t accent; // colour of the app's swatch in the menu
    uint8_t fps;     // frames per second the loop aims for
    void (*enter)(void);
    bool (*frame)(const input_t *in, uint32_t dt_ms);
    void (*leave)(void);
    // Optional. Runs every frame whatever app is open, so the pet keeps
    // getting hungry while you play Tetris.
    void (*background)(uint32_t dt_ms);
    // Optional, for attract mode, where the apps take turns playing
    // themselves. attract() runs after enter() to set up something worth
    // watching; autopilot() returns the buttons to hold this frame. An app
    // with neither just runs untouched. Nothing done in attract mode is saved.
    void (*attract)(void);
    uint8_t (*autopilot)(uint32_t dt_ms);
} app_t;

// True while attract mode is running the current app.
extern bool g_attract;

extern const app_t app_bad_apple, app_tetris, app_flappy, app_pet, app_pomodoro, app_life, app_dvd, app_rpg;

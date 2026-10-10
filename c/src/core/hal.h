// The hardware layer. hal_pico.c drives the real Display Pack; hal_sim.c
// stands in for it on a desktop so every app can be run, scripted and
// screenshotted without flashing a board.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

// Button bits, named as printed on the Display Pack.
#define BTN_A 1
#define BTN_B 2
#define BTN_X 4
#define BTN_Y 8

void hal_init(void);
// False once the simulator window is closed or its script has ended.
bool hal_running(void);
uint32_t hal_millis(void);
void hal_sleep_ms(uint32_t ms);
uint8_t hal_buttons(void);
void hal_led(uint8_t r, uint8_t g, uint8_t b);
void hal_backlight(uint8_t level);
// Applies gfx's orientation to the panel. flip turns portrait upside down.
void hal_set_orientation(orient_t o, bool flip);
// Sends gfx_fb to the panel. On the Pico this starts a DMA transfer and
// returns; hal_wait_present() blocks until the framebuffer is free again.
void hal_present(void);
void hal_wait_present(void);
uint32_t hal_random(void);
// Restarts the board. The simulator just quits.
void hal_reset(void);

// One small blob of save data, kept in the last flash sector on the Pico and
// in a file beside the simulator.
#define HAL_SAVE_SIZE 1024
bool hal_load(void *data, size_t len);
bool hal_store(const void *data, size_t len);

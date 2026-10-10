// Pomodoro timer: the phase logic, kept apart from drawing so it can be tested.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { POMO_FOCUS, POMO_SHORT, POMO_LONG } pomo_phase_t;

#define POMO_SET 4     // focus sessions before a long break
#define POMO_PRESETS 3 // focus lengths Y cycles through

typedef struct {
    pomo_phase_t phase;
    uint8_t set_pos;       // focus sessions finished in this set of four, 0..4
    uint8_t preset;        // 0: 25/5/15, 1: 50/10/30, 2: 15/3/10 minutes
    bool running;
    bool done;             // the phase ran out and waits to be confirmed
    uint32_t remaining_ms;
} pomo_t;

// Starts a fresh set: focus, paused, full length.
void pomo_init(pomo_t *p, int preset);
// Length of a phase under the timer's preset, in ms.
uint32_t pomo_duration(const pomo_t *p, pomo_phase_t phase);
// The phase that follows the current one.
pomo_phase_t pomo_next_phase(const pomo_t *p);
// Runs the clock. Returns true on the tick a phase runs out; a finished focus
// adds one to *focus_count.
bool pomo_tick(pomo_t *p, uint32_t dt_ms, uint32_t *focus_count);
// A: start or pause; when a phase has ended, starts the next one.
void pomo_toggle(pomo_t *p);
// B: back to the full length of the current phase, paused.
void pomo_reset(pomo_t *p);
// X: on to the next phase. A skipped focus moves the set along but is not
// counted as done. Keeps running if it was running.
void pomo_skip(pomo_t *p);
// Y: next focus length preset; the current phase restarts at its new length.
void pomo_next_preset(pomo_t *p);

// The app's own timer, for tests.
pomo_t *pomodoro_state(void);

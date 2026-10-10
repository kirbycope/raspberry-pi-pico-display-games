// The virtual pet's life and needs, kept apart from drawing so tests can run
// hours of it in a moment.
//
// Everything here lives in g_save.pet, so it survives power cycles. Time is
// powered time only: the Pico has no battery-backed clock, so the pet ages
// and gets hungry while the board is on, in whatever app is open.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PET_MAGIC 0xD7 // anything else in the slot (a fresh all-zero one) is a new egg

enum { PET_EGG, PET_BABY, PET_ADULT };
enum { NEED_HUNGER, NEED_HAPPY, NEED_ENERGY, NEED_HYGIENE, PET_NEEDS };
enum { ACT_FEED, ACT_PLAY, ACT_CLEAN, ACT_SLEEP, ACT_MEDICINE, ACT_STATS, PET_ACTIONS };

// What an action did, so the app can say so.
enum {
    PET_OK,
    PET_R_EGG,        // eggs only wobble
    PET_R_FULL,       // not hungry
    PET_R_TIRED,      // too sleepy to play
    PET_R_SICK,       // too ill to play
    PET_R_NOT_SLEEPY, // energy is nearly full
    PET_R_NOT_SICK,   // medicine is wasted on a healthy pet
    PET_R_WOKE,       // the sleep action woke it up
};

// What the thought bubble asks for.
enum { WISH_NONE, WISH_FOOD, WISH_PLAY, WISH_SLEEP, WISH_CLEAN, WISH_MEDICINE };
enum { MOOD_HAPPY, MOOD_NEEDY, MOOD_URGENT };

// A need at 100. Needs are kept in 1/360 of a percent so every per-second
// rate is a whole number.
#define PET_FULL 36000
#define PET_PCT(n) ((uint16_t)((n) * (PET_FULL / 100)))

// Life cycle, in powered seconds.
#define PET_HATCH_S 120                     // a new egg hatches after 2 minutes
#define PET_GROW_S (20 * 60)                // and is a baby for 20 minutes
#define PET_ADULT_S (PET_HATCH_S + PET_GROW_S)

// Decay per second awake, in PET_FULL units: food lasts 25 minutes from
// full, fun 33, energy and hygiene 50, so something wants attention every
// 10 to 20 minutes. Asleep, food, fun and hygiene drop at a third of that
// and energy climbs back in 10 minutes.
#define PET_DECAY_HUNGER 24
#define PET_DECAY_HAPPY 18
#define PET_DECAY_ENERGY 12
#define PET_DECAY_HYGIENE 12
#define PET_DECAY_POOP 12     // extra hygiene lost per dropping on the floor
#define PET_SLEEP_GAIN 60     // energy gained per second asleep
#define PET_POOP_MIN_S (8 * 60)
#define PET_POOP_MAX_S (15 * 60)
#define PET_MAX_POOPS 3
#define PET_NEEDY PET_PCT(40)   // below this the pet asks for it
#define PET_URGENT PET_PCT(15)  // below this the LED pulses red
#define PET_NEGLECT PET_PCT(10) // at or below this neglect builds up
#define PET_SICK_AFTER_S 300    // five minutes of neglect makes it ill

// One day of the window's sky passes in 24 powered minutes, a minute an hour.
// A new egg is laid at 08:00.
#define PET_DAY_S (24 * 60)
#define PET_DAWN_S (8 * 60)

#define PET_ASLEEP 1
#define PET_SICK 2
#define PET_NEWS 4 // the stage changed and the app has not shown it yet

typedef struct {
    uint8_t magic, stage, flags, poops;
    uint32_t age_s;     // powered seconds since the egg was laid
    uint16_t age_ms;    // milliseconds toward the next second
    uint16_t neglect_s; // seconds spent neglected
    uint16_t need[PET_NEEDS];
    uint16_t poop_s;    // seconds until the next dropping
    uint16_t reserved;
    uint32_t rng;
} pet_t;

// The pet kept in g_save.pet.
pet_t *pet_saved(void);

void pet_init(pet_t *p);
// Advances the pet dt_ms of powered time. A slot that is not a pet becomes a new egg.
void pet_tick(pet_t *p, uint32_t dt_ms);
// Does an ACT_ (not ACT_STATS) and returns PET_OK or why not.
int pet_act(pet_t *p, int act);
// A need as 0..100.
int pet_need(const pet_t *p, int need);
int pet_mood(const pet_t *p);
int pet_wish(const pet_t *p);
// Minutes into the window's day (0..1439) and which day it is, from 1.
int pet_clock(const pet_t *p);
int pet_day(const pet_t *p);

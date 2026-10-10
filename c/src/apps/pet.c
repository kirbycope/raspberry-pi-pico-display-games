// Virtual Pet: a dragon egg that hatches, grows up and needs looking after.
//
// The egg hatches after 2 minutes of powered time, the baby grows up after
// 20 more. Food, fun, energy and hygiene run down in every app, not just this
// one, because the Pico has no clock that runs while it is off. Neglect makes
// the dragon sad and then sick; medicine cures it. Nothing ever dies.
//
// B (bottom left): previous action   Y (bottom right): next action
// X (top right):   do the action     A (top left):     show or hide the stats
//
// Actions along the bottom: Feed, Play, Clean, Sleep (lights off; any other
// action wakes it), Medicine, Stats.
//
// LED, only while this app is open: soft green when content, amber when it
// wants something, a slow red pulse when it is urgent or sick.
#include "pet.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../assets/assets.h"
#include "../core/app.h"
#include "../core/hal.h"
#include "../core/save.h"

_Static_assert(sizeof(pet_t) <= SAVE_PET_BYTES, "pet state must fit its save slot");
_Static_assert(offsetof(save_t, pet) % 4 == 0, "the pet slot must be word aligned");

// ---------------------------------------------------------------------------
// The pet's life, no drawing.

pet_t *pet_saved(void) { return (pet_t *)(void *)g_save.pet; }

static uint32_t pet_rand(pet_t *p) {
    p->rng = p->rng * 1664525u + 1013904223u;
    return p->rng >> 16;
}

static uint16_t next_poop(pet_t *p) {
    return (uint16_t)(PET_POOP_MIN_S + pet_rand(p) % (PET_POOP_MAX_S - PET_POOP_MIN_S));
}

void pet_init(pet_t *p) {
    memset(p, 0, sizeof *p);
    p->magic = PET_MAGIC;
    p->stage = PET_EGG;
    for (int i = 0; i < PET_NEEDS; i++) p->need[i] = PET_FULL;
    p->rng = 0x2545F491u;
    p->poop_s = PET_POOP_MIN_S;
}

static void change(pet_t *p, int need, int by) {
    int v = (int)p->need[need] + by;
    p->need[need] = (uint16_t)(v < 0 ? 0 : v > PET_FULL ? PET_FULL : v);
}

static void pet_second(pet_t *p) {
    p->age_s++;
    if (p->stage == PET_EGG) {
        if (p->age_s >= PET_HATCH_S) {
            // Hatchlings arrive hungry, so there is something to do at once.
            p->stage = PET_BABY;
            p->flags |= PET_NEWS;
            p->need[NEED_HUNGER] = PET_PCT(50);
            p->need[NEED_HAPPY] = PET_PCT(80);
            p->need[NEED_ENERGY] = PET_PCT(80);
            p->need[NEED_HYGIENE] = PET_FULL;
        }
        return;
    }
    if (p->stage == PET_BABY && p->age_s >= PET_ADULT_S) {
        p->stage = PET_ADULT;
        p->flags |= PET_NEWS;
    }

    bool asleep = p->flags & PET_ASLEEP, sick = p->flags & PET_SICK;
    int ill = sick ? 2 : 1;
    if (asleep) {
        change(p, NEED_HUNGER, -PET_DECAY_HUNGER / 3);
        change(p, NEED_HAPPY, -PET_DECAY_HAPPY * ill / 3);
        change(p, NEED_HYGIENE, -(PET_DECAY_HYGIENE + p->poops * PET_DECAY_POOP) / 3);
        change(p, NEED_ENERGY, PET_SLEEP_GAIN);
        if (p->need[NEED_ENERGY] >= PET_FULL) p->flags &= (uint8_t)~PET_ASLEEP; // wakes up rested
    } else {
        change(p, NEED_HUNGER, -PET_DECAY_HUNGER);
        change(p, NEED_HAPPY, -PET_DECAY_HAPPY * ill);
        change(p, NEED_ENERGY, -PET_DECAY_ENERGY * ill);
        change(p, NEED_HYGIENE, -(PET_DECAY_HYGIENE + p->poops * PET_DECAY_POOP));
        if (p->need[NEED_ENERGY] == 0) p->flags |= PET_ASLEEP; // dozes off where it stands
        if (p->poop_s) p->poop_s--;
        if (!p->poop_s) {
            if (p->poops < PET_MAX_POOPS) p->poops++;
            p->poop_s = next_poop(p);
        }
    }

    bool neglected = p->poops >= PET_MAX_POOPS;
    for (int i = 0; i < PET_NEEDS; i++)
        if (p->need[i] <= PET_NEGLECT) neglected = true;
    if (neglected && !sick) {
        if (++p->neglect_s >= PET_SICK_AFTER_S) {
            p->flags |= PET_SICK;
            p->neglect_s = 0;
        }
    } else if (!neglected && p->neglect_s) {
        p->neglect_s--;
    }
}

void pet_tick(pet_t *p, uint32_t dt_ms) {
    if (p->magic != PET_MAGIC || p->stage > PET_ADULT) pet_init(p);
    uint32_t total = p->age_ms + dt_ms;
    p->age_ms = (uint16_t)(total % 1000);
    for (uint32_t s = total / 1000; s; s--) pet_second(p);
}

int pet_act(pet_t *p, int act) {
    if (p->stage == PET_EGG) return PET_R_EGG;
    if (act == ACT_SLEEP) {
        if (p->flags & PET_ASLEEP) {
            p->flags &= (uint8_t)~PET_ASLEEP;
            return PET_R_WOKE;
        }
        if (p->need[NEED_ENERGY] >= PET_PCT(95)) return PET_R_NOT_SLEEPY;
        p->flags |= PET_ASLEEP;
        return PET_OK;
    }
    p->flags &= (uint8_t)~PET_ASLEEP; // anything else wakes it
    switch (act) {
    case ACT_FEED:
        if (p->need[NEED_HUNGER] >= PET_PCT(95)) return PET_R_FULL;
        change(p, NEED_HUNGER, PET_PCT(30));
        return PET_OK;
    case ACT_PLAY:
        if (p->flags & PET_SICK) return PET_R_SICK;
        if (p->need[NEED_ENERGY] < PET_PCT(15)) return PET_R_TIRED;
        change(p, NEED_HAPPY, PET_PCT(25));
        change(p, NEED_ENERGY, -PET_PCT(10));
        change(p, NEED_HUNGER, -PET_PCT(5));
        return PET_OK;
    case ACT_CLEAN:
        p->poops = 0;
        p->need[NEED_HYGIENE] = PET_FULL;
        return PET_OK;
    case ACT_MEDICINE:
        if (!(p->flags & PET_SICK)) return PET_R_NOT_SICK;
        p->flags &= (uint8_t)~PET_SICK;
        p->neglect_s = 0;
        change(p, NEED_HAPPY, -PET_PCT(10)); // it tastes awful
        return PET_OK;
    }
    return PET_OK;
}

int pet_need(const pet_t *p, int need) { return p->need[need] * 100 / PET_FULL; }

int pet_mood(const pet_t *p) {
    if (p->stage == PET_EGG) return MOOD_HAPPY;
    if (p->flags & PET_SICK || p->poops >= PET_MAX_POOPS) return MOOD_URGENT;
    int mood = MOOD_HAPPY;
    for (int i = 0; i < PET_NEEDS; i++) {
        if (p->need[i] < PET_URGENT) return MOOD_URGENT;
        if (p->need[i] < PET_NEEDY) mood = MOOD_NEEDY;
    }
    return mood;
}

int pet_wish(const pet_t *p) {
    static const uint8_t wish_for[PET_NEEDS] = {WISH_FOOD, WISH_PLAY, WISH_SLEEP, WISH_CLEAN};
    if (p->stage == PET_EGG) return WISH_NONE;
    if (p->flags & PET_SICK) return WISH_MEDICINE;
    int low = -1;
    for (int i = 0; i < PET_NEEDS; i++)
        if (p->need[i] < PET_NEEDY && (low < 0 || p->need[i] < p->need[low])) low = i;
    if (low >= 0) return wish_for[low];
    return p->poops >= 2 ? WISH_CLEAN : WISH_NONE;
}

int pet_clock(const pet_t *p) { return (int)((p->age_s + PET_DAWN_S) % PET_DAY_S); }
int pet_day(const pet_t *p) { return (int)((p->age_s + PET_DAWN_S) / PET_DAY_S) + 1; }

// ---------------------------------------------------------------------------
// Drawing.

#define BAR_Y 115 // the action bar fills the rest of the screen
#define FLOOR_Y 78
#define WAINSCOT_Y 56
#define WIN_X 22
#define WIN_Y 11
#define WIN_W 46
#define WIN_H 36
#define SLOT_X 18
#define SLOT_W 34
#define COMMIT_MS (5u * 60u * 1000u)
#define HATCH_SEQ_MS 2800
#define GROW_SEQ_MS 2600
#define EFFECT_MS 1600
#define PARTS 28

static const char *const ACTION_NAMES[PET_ACTIONS] = {"Feed", "Play", "Clean", "Sleep", "Medicine", "Stats"};

// Where each stage stands: its sprite, its top edge, how far its centre may
// wander, and where its head and mouth are inside the sprite.
typedef struct {
    const sprite_t *spr;
    int top, min_cx, max_cx;
    int head_x, head_y, mouth_x, mouth_y;
    int speed; // pixels per second
} look_t;

static const look_t LOOKS[3] = {
    {&spr_battler_26, 59, 120, 120, 22, 6, 22, 20, 0},
    {&spr_battler_27, 65, 40, 200, 32, 6, 32, 27, 16},
    {&spr_battler_28, 18, 72, 168, 72, 18, 72, 54, 10},
};

enum { P_HEART = 1, P_SPARK, P_BUBBLE, P_CRUMB, P_SHELL, P_PLUS };

static struct {
    int32_t x, y, vx, vy; // 1/16 pixel, and 1/16 pixel per second
    int16_t life, max;
    uint8_t kind;
} parts[PARTS];

static int sel;
static bool panel;
static uint32_t anim_ms, commit_ms, label_ms, toast_ms, poke_ms;
static char toast[28];
static int32_t pet_cx; // centre, 1/256 pixel
static int target_cx, idle_ms;
static bool facing_right;
static int effect, effect_ms; // ACT_ animation playing
static int seq_stage, seq_ms; // a hatch or grow-up sequence playing
static int dark, dark_step_ms; // 0..3, lights going off in steps

static uint32_t rnd(uint32_t n) { return hal_random() % n; }

// --- Small helpers ---------------------------------------------------------

static void fill_ellipse(int cx, int cy, int rx, int ry, uint16_t c) {
    for (int j = -ry; j <= ry; j++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) * ry * ry <= rx * rx * (ry * ry - j * j)) dx++;
        gfx_hline(cx - dx, cy + j, 2 * dx + 1, c);
    }
}

static void hline_clip(int x, int y, int w, uint16_t c, int x0, int x1) {
    if (x < x0) { w -= x0 - x; x = x0; }
    if (x + w > x1) w = x1 - x;
    if (w > 0) gfx_hline(x, y, w, c);
}

// Darkens rows of the screen in steps: cheap shifts instead of a blend.
static void dim_rows(int y0, int y1, int level) {
    if (level <= 0) return;
    for (int i = y0 * gfx_w; i < y1 * gfx_w; i++) {
        uint16_t c = gfx_fb[i];
        if (level == 1) c = (uint16_t)((c >> 1) & 0x7BEF);
        else if (level == 2) c = (uint16_t)(((c >> 2) & 0x39E7) + ((c >> 3) & 0x18E3));
        else c = (uint16_t)(((c >> 2) & 0x39E7) + 0x0023); // a little moonlight blue
        gfx_fb[i] = c;
    }
}

// Pixel-art icons as strings. Each character picks a colour; '.' is clear.
static uint16_t ink(char ch) {
    switch (ch) {
    case 'k': return rgb(40, 24, 30);
    case 'w': return rgb(250, 246, 236);
    case 'g': return rgb(196, 188, 176);
    case 'N': return rgb(222, 132, 64);
    case 'n': return rgb(160, 78, 40);
    case 'Y': return rgb(255, 222, 96);
    case 'y': return rgb(226, 170, 40);
    case 'R': return rgb(232, 64, 64);
    case 'r': return rgb(170, 36, 48);
    case 'B': return rgb(80, 150, 240);
    case 'b': return rgb(44, 96, 190);
    case 'G': return rgb(90, 200, 90);
    case 'P': return rgb(255, 170, 200);
    case 'p': return rgb(240, 90, 140);
    case 'c': return rgb(190, 236, 255);
    case 'C': return rgb(80, 170, 220);
    case 'o': return rgb(120, 72, 40);
    case 'O': return rgb(170, 112, 60);
    default: return 0;
    }
}

static void draw_icon_scaled(const char *const *rows, int n, int x, int y, int max_cols, int scale) {
    for (int j = 0; j < n; j++)
        for (int i = 0; rows[j][i] && i < max_cols; i++)
            if (rows[j][i] != '.') gfx_rect(x + i * scale, y + j * scale, scale, scale, ink(rows[j][i]));
}

static void draw_icon(const char *const *rows, int n, int x, int y, int max_cols) {
    draw_icon_scaled(rows, n, x, y, max_cols, 1);
}

static const char *const ICON_MEAT[12] = {
    ".....kkkk...", "....kNNNNk..", "...kNNwNNNk.", "...kNwNNNnk.", "...kNNNNNnk.", "....knNNnnk.",
    "....kknnnk..", "...kwkkkk...", "..kwwk......", "kkwwk.......", "kwwgk.......", ".kkk........",
};
static const char *const ICON_BALL[12] = {
    "....kkkk....", "..kkRRwwkk..", ".kRRRRwwBBk.", ".kRwRwwwBBk.", "kRRwRwwwBBBk", "kRRRwwwwBBBk",
    "kRRRwwwBBBBk", "kRRwwwwBBBbk", ".krwwwwBBbk.", ".krrwwgbbbk.", "..kkggbbkk..", "....kkkk....",
};
static const char *const ICON_STATS[12] = {
    "............", "........kkk.", "........kGk.", "....kkk.kGk.", "....kYk.kGk.", "....kYk.kGk.",
    "kkk.kYk.kGk.", "kRk.kYk.kGk.", "kRk.kYk.kGk.", "kRk.kYk.kGk.", "kkkkkkkkkkkk", "............",
};
static const char *const ICON_POOP[9] = {
    "....kk....", "...kOOk...", "...koOk...", "..kOOOOk..", "..kooook..",
    ".kOOOOOOk.", "kooooooook", "kOOOOOOOOk", ".kkkkkkkk.",
};
static const char *const ICON_HEART[6] = {
    ".pp.pp.", "pPppppp", "ppppppp", ".ppppp.", "..ppp..", "...p...",
};
static const char *const ICON_THERMO[10] = {
    ".kkk.", ".kwk.", ".kwk.", ".kRk.", ".kRk.", ".kRk.", "kkRkk", "kRRRk", "kRRRk", ".kkk.",
};
static const char *const ICON_POTION[12] = {
    "....kkkk....", "....kOOk....", "....kkkk....", "....kcck....", "...kcwcck...", "..kcRRRRck..",
    ".kRRwRRRRrk.", ".kRwRRRRRrk.", ".kRRRRRRRrk.", ".kRRRRRRrrk.", "..krrrrrrk..", "...kkkkkk...",
};
static const char *const ICON_DROP[5] = {".k..", "kck.", "kcCk", "kCCk", ".kk."};

// A crescent moon, worked out per pixel with a one pixel outline.
static bool in_moon(int i, int j) {
    if (i < 0 || j < 0 || i > 11 || j > 11) return false;
    int a = (2 * i - 11) * (2 * i - 11) + (2 * j - 11) * (2 * j - 11);
    int b = (2 * i - 17) * (2 * i - 17) + (2 * j - 7) * (2 * j - 7);
    return a <= 110 && b > 72;
}

static void draw_moon_icon(int x, int y) {
    for (int j = -1; j <= 12; j++)
        for (int i = -1; i <= 12; i++) {
            if (in_moon(i, j)) gfx_pixel(x + i, y + j, (i + j) < 9 ? ink('Y') : ink('y'));
            else if (in_moon(i - 1, j) || in_moon(i + 1, j) || in_moon(i, j - 1) || in_moon(i, j + 1))
                gfx_pixel(x + i, y + j, ink('k'));
        }
    gfx_pixel(x + 10, y + 8, ink('Y'));
    gfx_pixel(x + 9, y + 8, ink('y'));
    gfx_pixel(x + 11, y + 8, ink('y'));
    gfx_pixel(x + 10, y + 7, ink('y'));
    gfx_pixel(x + 10, y + 9, ink('y'));
}

static void draw_bubbles_icon(int x, int y) {
    gfx_circle(x + 4, y + 7, 4, ink('c'));
    gfx_ring(x + 4, y + 7, 4, ink('C'));
    gfx_pixel(x + 3, y + 5, C_WHITE);
    gfx_pixel(x + 2, y + 6, C_WHITE);
    gfx_circle(x + 10, y + 3, 2, ink('c'));
    gfx_ring(x + 10, y + 3, 2, ink('C'));
    gfx_pixel(x + 9, y + 2, C_WHITE);
    gfx_ring(x + 10, y + 10, 1, ink('C'));
}

// The 12x12 icon for an action.
static void draw_action_icon(int act, int x, int y) {
    switch (act) {
    case ACT_FEED: draw_icon(ICON_MEAT, 12, x, y, 12); break;
    case ACT_PLAY: draw_icon(ICON_BALL, 12, x, y, 12); break;
    case ACT_CLEAN: draw_bubbles_icon(x, y); break;
    case ACT_SLEEP: draw_moon_icon(x, y); break;
    case ACT_MEDICINE: draw_icon(ICON_POTION, 12, x, y, 12); break;
    case ACT_STATS: draw_icon(ICON_STATS, 12, x, y, 12); break;
    }
}

// --- The room --------------------------------------------------------------

// Sky colours through the day: minute, top, bottom.
static const struct { int16_t m; uint16_t top, bottom; } SKY[] = {
    {0, rgb(12, 16, 44), rgb(30, 36, 84)},       {300, rgb(12, 16, 44), rgb(30, 36, 84)},
    {370, rgb(96, 88, 170), rgb(250, 160, 120)}, {460, rgb(84, 156, 236), rgb(176, 218, 246)},
    {1020, rgb(84, 156, 236), rgb(176, 218, 246)}, {1110, rgb(112, 80, 160), rgb(250, 140, 80)},
    {1190, rgb(12, 16, 44), rgb(30, 36, 84)},    {1440, rgb(12, 16, 44), rgb(30, 36, 84)},
};

static int night_amount(int m) { // 0 by day, 255 at night
    if (m < 300 || m >= 1190) return 255;
    if (m < 460) return 255 - (m - 300) * 255 / 160;
    if (m < 1020) return 0;
    return (m - 1020) * 255 / 170;
}

static void draw_window(const pet_t *p) {
    int m = pet_clock(p);
    int k = 0;
    while (SKY[k + 1].m <= m) k++;
    int t = (m - SKY[k].m) * 255 / (SKY[k + 1].m - SKY[k].m);
    uint16_t top = rgb_lerp(SKY[k].top, SKY[k + 1].top, t);
    uint16_t bottom = rgb_lerp(SKY[k].bottom, SKY[k + 1].bottom, t);
    const int x0 = WIN_X, x1 = WIN_X + WIN_W;
    gfx_vgradient(WIN_X, WIN_Y, WIN_W, WIN_H, top, bottom);

    int night = night_amount(m);
    if (night > 60) {
        static const uint8_t STARS[][2] = {{4, 4}, {13, 9}, {25, 3}, {35, 7}, {41, 15}, {8, 17}, {30, 20}, {19, 14}};
        for (int i = 0; i < 8; i++) {
            bool twinkle = ((anim_ms / 400) + (uint32_t)i * 3) % 7 == 0;
            uint16_t c = rgb_lerp(top, C_WHITE, twinkle ? night / 2 : night);
            gfx_pixel(WIN_X + STARS[i][0], WIN_Y + STARS[i][1], c);
        }
    }
    // The sun crosses by day and the moon by night, each on a low arc.
    int arc_t = -1;
    bool sun = m >= 360 && m < 1110;
    if (sun) arc_t = (m - 360) * 256 / 750;
    else arc_t = ((m + 1440 - 1140) % 1440) * 256 / 600;
    if (arc_t >= 0 && arc_t < 256) {
        int bx = WIN_X + 6 + arc_t * (WIN_W - 12) / 256;
        int by = WIN_Y + WIN_H - 7 - 4 * (WIN_H - 16) * arc_t * (256 - arc_t) / (256 * 256);
        if (sun) {
            gfx_circle(bx, by, 5, rgb_lerp(bottom, rgb(255, 240, 160), 120));
            gfx_circle(bx, by, 3, rgb(255, 236, 120));
        } else {
            gfx_circle(bx, by, 3, rgb(240, 240, 220));
            gfx_circle(bx + 2, by - 1, 2, top);
        }
    }
    // Clouds drift past by day.
    if (night < 200) {
        uint16_t cl = rgb_lerp(rgb(255, 255, 255), top, night);
        for (int i = 0; i < 2; i++) {
            int cx = WIN_X - 12 + (int)((anim_ms / (90 + 40 * (uint32_t)i) + (uint32_t)i * 31) % (WIN_W + 24));
            int cy = WIN_Y + 8 + i * 13;
            hline_clip(cx + 3, cy, 6, cl, x0, x1);
            hline_clip(cx + 1, cy + 1, 11, cl, x0, x1);
            hline_clip(cx, cy + 2, 14, cl, x0, x1);
        }
    }
    // Frame, cross bars and sill.
    uint16_t wood = rgb(130, 84, 52), wood_hi = rgb(176, 120, 76);
    gfx_rect(WIN_X - 3, WIN_Y - 3, WIN_W + 6, 3, wood);
    gfx_rect(WIN_X - 3, WIN_Y + WIN_H, WIN_W + 6, 3, wood);
    gfx_rect(WIN_X - 3, WIN_Y, 3, WIN_H, wood);
    gfx_rect(WIN_X + WIN_W, WIN_Y, 3, WIN_H, wood);
    gfx_rect(WIN_X + WIN_W / 2 - 1, WIN_Y, 2, WIN_H, wood);
    gfx_rect(WIN_X, WIN_Y + WIN_H / 2 - 1, WIN_W, 2, wood);
    gfx_rect(WIN_X - 6, WIN_Y + WIN_H + 3, WIN_W + 12, 3, wood_hi);
    gfx_hline(WIN_X - 6, WIN_Y + WIN_H + 6, WIN_W + 12, rgb(70, 44, 30));
    // A glint on the glass.
    gfx_line(WIN_X + 3, WIN_Y + 9, WIN_X + 9, WIN_Y + 3, rgb_lerp(top, C_WHITE, 70));
}

static void draw_curtain(int x, bool right) {
    uint16_t c = rgb(222, 190, 132), shade = rgb(186, 148, 96), hem = rgb(150, 112, 70);
    for (int j = 0; j < WIN_H + 12; j++) {
        // Tied back: narrow at the tie, full at top and bottom.
        int w = 10 - (j > 20 && j < 34 ? 4 - (j - 27 < 0 ? 27 - j : j - 27) / 2 : 0);
        if (w > 10) w = 10;
        int xx = right ? x + 10 - w : x;
        gfx_hline(xx, WIN_Y - 6 + j, w, c);
        gfx_pixel(xx + (right ? 3 : w - 4), WIN_Y - 6 + j, shade);
        gfx_pixel(xx + (right ? 6 : w - 7), WIN_Y - 6 + j, shade);
    }
    gfx_hline(x, WIN_Y + WIN_H + 6, 10, hem);
    gfx_rect(x + (right ? 4 : 0), WIN_Y + 20, 6, 3, rgb(180, 60, 60)); // the tie
}

static void draw_room(const pet_t *p) {
    // Wallpaper with soft stripes.
    gfx_rect(0, 0, SCREEN_LONG, WAINSCOT_Y, rgb(62, 102, 112));
    for (int x = 4; x < SCREEN_LONG; x += 12) gfx_rect(x, 0, 3, WAINSCOT_Y, rgb(70, 112, 122));
    for (int x = 10; x < SCREEN_LONG; x += 24)
        for (int y = 6; y < WAINSCOT_Y - 4; y += 14) gfx_pixel(x, y + (x / 24 % 2) * 7, rgb(92, 136, 140));
    // Wood panelling below a dado rail.
    gfx_rect(0, WAINSCOT_Y, SCREEN_LONG, FLOOR_Y - WAINSCOT_Y, rgb(112, 70, 50));
    for (int x = 6; x < SCREEN_LONG; x += 26) {
        gfx_frame(x, WAINSCOT_Y + 5, 20, FLOOR_Y - WAINSCOT_Y - 9, rgb(92, 56, 40));
        gfx_hline(x + 1, WAINSCOT_Y + 6, 18, rgb(128, 84, 60));
    }
    gfx_rect(0, WAINSCOT_Y, SCREEN_LONG, 3, rgb(164, 112, 74));
    gfx_hline(0, WAINSCOT_Y + 3, SCREEN_LONG, rgb(80, 50, 36));
    gfx_rect(0, FLOOR_Y - 2, SCREEN_LONG, 2, rgb(70, 42, 30));

    // Floor boards, wider toward the front.
    static const uint8_t ROWS[] = {5, 6, 7, 8, 9, 3};
    int y = FLOOR_Y;
    for (int r = 0; r < 6 && y < BAR_Y; r++) {
        int h = ROWS[r];
        gfx_rect(0, y, SCREEN_LONG, h, r % 2 ? rgb(156, 102, 62) : rgb(146, 94, 58));
        gfx_hline(0, y, SCREEN_LONG, rgb(110, 68, 42));
        for (int x = (r * 37) % 52; x < SCREEN_LONG; x += 52) gfx_vline(x, y, h, rgb(110, 68, 42));
        y += h;
    }

    // A round rug in the middle of the floor.
    fill_ellipse(120, 104, 82, 10, rgb(214, 168, 84));
    fill_ellipse(120, 104, 78, 8, rgb(98, 66, 128));
    fill_ellipse(120, 104, 62, 6, rgb(214, 168, 84));
    fill_ellipse(120, 104, 60, 5, rgb(124, 86, 156));
    fill_ellipse(120, 104, 30, 2, rgb(150, 110, 180));

    // Curtain rod, curtains and window.
    gfx_rect(WIN_X - 14, WIN_Y - 8, WIN_W + 28, 2, rgb(70, 44, 30));
    gfx_rect(WIN_X - 16, WIN_Y - 9, 3, 4, rgb(214, 168, 84));
    gfx_rect(WIN_X + WIN_W + 13, WIN_Y - 9, 3, 4, rgb(214, 168, 84));
    draw_window(p);
    draw_curtain(WIN_X - 13, false);
    draw_curtain(WIN_X + WIN_W + 3, true);

    // A painting of the mountains dragons come from.
    const int px = 184, py = 12, pw = 36, ph = 26;
    gfx_rect(px - 3, py - 3, pw + 6, ph + 6, rgb(190, 146, 64));
    gfx_frame(px - 3, py - 3, pw + 6, ph + 6, rgb(110, 76, 34));
    gfx_vgradient(px, py, pw, ph, rgb(250, 190, 140), rgb(250, 230, 190));
    gfx_circle(px + 26, py + 8, 4, rgb(255, 250, 220));
    for (int j = 0; j < 16; j++) {
        gfx_hline(px + 10 - j * 3 / 4, py + 10 + j, j * 3 / 2 + 1, rgb(110, 90, 130));
        if (j > 4) gfx_hline(px + 24 - (j - 4), py + 10 + j, (j - 4) * 2 + 1, rgb(80, 66, 110));
    }
    gfx_rect(px, py + ph - 3, pw, 3, rgb(80, 120, 80));

    // A potted fern in the corner.
    gfx_rect(214, 66, 16, 13, rgb(176, 92, 60));
    gfx_rect(212, 64, 20, 3, rgb(200, 112, 72));
    gfx_hline(215, 70, 14, rgb(150, 76, 50));
    static const uint8_t LEAVES[][4] = {{222, 63, 210, 50}, {222, 63, 234, 48}, {222, 63, 222, 44},
                                       {222, 63, 214, 46}, {222, 63, 230, 54}, {222, 63, 206, 58}};
    for (int i = 0; i < 6; i++) {
        int lx = LEAVES[i][0], ly = LEAVES[i][1], ex = LEAVES[i][2], ey = LEAVES[i][3];
        gfx_line(lx, ly, ex, ey, rgb(60, 140, 70));
        gfx_line(lx + 1, ly, ex + 1, ey, rgb(90, 176, 90));
    }
}

// --- The pet ---------------------------------------------------------------

// Like gfx_sprite, with an optional sickly green tint.
static void blit(const sprite_t *s, int x, int y, bool flip, bool sick) {
    if (!sick) {
        gfx_sprite(s, x, y, 1, flip ? SPR_FLIP_X : 0, 0);
        return;
    }
    const uint16_t green = (uint16_t)((rgb(150, 230, 110) >> 3) & 0x18E3);
    for (int j = 0; j < s->h; j++) {
        int sy = y + j;
        if (sy < 0 || sy >= gfx_h) continue;
        for (int i = 0; i < s->w; i++) {
            int sx = x + i;
            if (sx < 0 || sx >= gfx_w) continue;
            uint8_t idx = s->pixels[j * s->w + (flip ? s->w - 1 - i : i)];
            if (!idx) continue;
            uint16_t c = s->palette[idx];
            // Seven eighths of the colour and one of green: queasy, still red.
            gfx_fb[sy * gfx_w + sx] = (uint16_t)(((c >> 1) & 0x7BEF) + ((c >> 2) & 0x39E7) + ((c >> 3) & 0x18E3) + green);
        }
    }
}

static int pet_left(const look_t *lk) { return (pet_cx >> 8) - lk->spr->w / 2; }

static void draw_nest(int cx, bool front) {
    if (!front) {
        fill_ellipse(cx, 104, 34, 8, rgb(120, 80, 40));
        fill_ellipse(cx, 103, 28, 5, rgb(80, 52, 28));
        return;
    }
    for (int i = -30; i <= 30; i += 3) {
        int y = 106 + (i * i) / 300;
        gfx_line(cx + i - 3, y + 2, cx + i + 4, y - 3, rgb(196, 150, 70));
        gfx_line(cx + i - 1, y + 3, cx + i + 3, y - 1, rgb(150, 104, 50));
    }
}

// Cracks in the shell, from none (0) to all of them (256).
static void draw_cracks(int x, int y, int amount) {
    static const int8_t PTS[][2] = {{20, 5}, {17, 9}, {21, 12}, {16, 17}, {21, 21}, {17, 26}, {22, 30}};
    static const int8_t BRANCH[][2] = {{21, 12}, {27, 14}, {25, 18}, {31, 21}};
    int n = amount * 9 / 256;
    for (int i = 0; i < 6 && i < n; i++)
        gfx_line(x + PTS[i][0], y + PTS[i][1], x + PTS[i + 1][0], y + PTS[i + 1][1], rgb(50, 30, 30));
    for (int i = 0; i < 3 && i + 6 < n; i++)
        gfx_line(x + BRANCH[i][0], y + BRANCH[i][1], x + BRANCH[i + 1][0], y + BRANCH[i + 1][1], rgb(50, 30, 30));
}

static void spawn(int kind, int x, int y, int vx, int vy, int life) {
    for (int i = 0; i < PARTS; i++)
        if (parts[i].life <= 0) {
            parts[i].kind = (uint8_t)kind;
            parts[i].x = x * 16;
            parts[i].y = y * 16;
            parts[i].vx = vx * 16;
            parts[i].vy = vy * 16;
            parts[i].life = parts[i].max = (int16_t)life;
            return;
        }
}

static void update_parts(uint32_t dt) {
    for (int i = 0; i < PARTS; i++) {
        if (parts[i].life <= 0) continue;
        parts[i].life = (int16_t)(parts[i].life - (int)dt);
        if (parts[i].kind == P_CRUMB || parts[i].kind == P_SHELL) parts[i].vy += 16 * 300 * (int32_t)dt / 1000;
        parts[i].x += parts[i].vx * (int32_t)dt / 1000;
        parts[i].y += parts[i].vy * (int32_t)dt / 1000;
        if (parts[i].kind == P_CRUMB && parts[i].y > 112 * 16) parts[i].life = 0;
        if (parts[i].kind == P_SHELL && parts[i].y > 110 * 16) {
            parts[i].y = 110 * 16;
            parts[i].vx = parts[i].vx / 2;
            parts[i].vy = -parts[i].vy / 3;
        }
    }
}

static void draw_sparkle(int x, int y, int size, uint16_t c) {
    gfx_hline(x - size, y, 2 * size + 1, c);
    gfx_vline(x, y - size, 2 * size + 1, c);
    if (size > 1) gfx_pixel(x, y, C_WHITE);
}

static void draw_parts(void) {
    for (int i = 0; i < PARTS; i++) {
        if (parts[i].life <= 0) continue;
        int x = parts[i].x / 16, y = parts[i].y / 16;
        int age = (parts[i].max - parts[i].life) * 256 / parts[i].max; // 0..256
        switch (parts[i].kind) {
        case P_HEART: draw_icon(ICON_HEART, 6, x - 3, y - 3, 7); break;
        case P_SPARK: draw_sparkle(x, y, age < 128 ? 1 + age / 43 : 1 + (256 - age) / 43, rgb(255, 250, 190)); break;
        case P_BUBBLE:
            gfx_ring(x, y, 2 + age / 128, ink('C'));
            gfx_pixel(x - 1, y - 1, C_WHITE);
            break;
        case P_CRUMB: gfx_rect(x, y, 2, 2, ink('n')); break;
        case P_SHELL:
            gfx_rect(x - 1, y - 1, 4, 3, rgb(232, 220, 196));
            gfx_pixel(x, y, rgb(150, 70, 60));
            break;
        case P_PLUS:
            gfx_rect(x - 1, y - 3, 3, 7, rgb(120, 230, 120));
            gfx_rect(x - 3, y - 1, 7, 3, rgb(120, 230, 120));
            gfx_rect(x, y - 2, 1, 5, C_WHITE);
            gfx_rect(x - 2, y, 5, 1, C_WHITE);
            break;
        }
    }
}

static void draw_thought(int wish, int hx, int hy, int bx, int by) {
    if (wish == WISH_NONE) return;
    // Two little puffs lead from the head to the bubble.
    int mx = (hx * 2 + bx + 12) / 3, my = (hy * 2 + by + 20) / 3;
    gfx_circle(mx, my, 2, C_WHITE);
    gfx_ring(mx, my, 2, rgb(60, 50, 60));
    gfx_circle((mx + bx + 12) / 2, (my + by + 19) / 2, 3, C_WHITE);
    gfx_ring((mx + bx + 12) / 2, (my + by + 19) / 2, 3, rgb(60, 50, 60));
    gfx_round_rect(bx - 1, by - 1, 26, 22, 7, rgb(60, 50, 60));
    gfx_round_rect(bx, by, 24, 20, 6, C_WHITE);
    int ix = bx + 6, iy = by + 4;
    switch (wish) {
    case WISH_FOOD: draw_action_icon(ACT_FEED, ix, iy); break;
    case WISH_PLAY: draw_action_icon(ACT_PLAY, ix, iy); break;
    case WISH_SLEEP: draw_action_icon(ACT_SLEEP, ix, iy); break;
    case WISH_CLEAN: draw_icon(ICON_POOP, 9, ix + 1, iy + 2, 10); break;
    case WISH_MEDICINE: draw_action_icon(ACT_MEDICINE, ix, iy); break;
    }
}

static void draw_zzz(int hx, int hy) {
    for (int i = 0; i < 3; i++) {
        int t = (int)((anim_ms + (uint32_t)i * 700) % 2100); // 0..2100
        int x = hx + 6 + t / 70 + (t / 300 % 2), y = hy - 4 - t / 60;
        gfx_text_outline(x, y, t > 700 ? "Z" : "z", rgb(200, 220, 255), rgb(30, 30, 70), &font_mini, t > 1400 ? 2 : 1);
    }
}

// --- Behaviour -------------------------------------------------------------

static void show_toast(const char *s) {
    snprintf(toast, sizeof toast, "%s", s);
    toast_ms = 1800;
}

static void do_action(pet_t *p, int act) {
    if (act == ACT_STATS) {
        panel = !panel;
        return;
    }
    panel = false;
    bool was_asleep = p->flags & PET_ASLEEP;
    int r = pet_act(p, act);
    static const char *const DONE[PET_ACTIONS] = {"Yum!", "Wheee!", "Squeaky clean!", "Good night...", "All better!", ""};
    switch (r) {
    case PET_OK:
        show_toast(DONE[act]);
        if (act != ACT_SLEEP) {
            effect = act;
            effect_ms = EFFECT_MS;
        }
        return;
    case PET_R_EGG: show_toast("Still an egg..."); poke_ms = 900; return;
    case PET_R_FULL: show_toast(was_asleep ? "Not hungry, just sleepy" : "Not hungry"); return;
    case PET_R_TIRED: show_toast("Too sleepy to play"); return;
    case PET_R_SICK: show_toast("Too poorly to play"); return;
    case PET_R_NOT_SLEEPY: show_toast("Not sleepy"); return;
    case PET_R_NOT_SICK: show_toast("Not sick!"); return;
    case PET_R_WOKE: show_toast("Good morning!"); return;
    }
}

static void wander(const pet_t *p, const look_t *lk, uint32_t dt) {
    int cx = pet_cx >> 8;
    if (cx < lk->min_cx || cx > lk->max_cx) {
        cx = cx < lk->min_cx ? lk->min_cx : lk->max_cx;
        pet_cx = cx << 8;
    }
    if (target_cx < lk->min_cx || target_cx > lk->max_cx) target_cx = cx;
    bool busy = effect_ms > 0 || seq_ms > 0 || (p->flags & PET_ASLEEP) || lk->speed == 0;
    if (busy) return;
    if (idle_ms > 0) {
        idle_ms -= (int)dt;
        return;
    }
    int mood = pet_mood(p);
    int speed = mood == MOOD_HAPPY ? lk->speed : lk->speed / 2;
    int32_t goal = target_cx << 8, step = speed * 256 * (int32_t)dt / 1000;
    if (pet_cx < goal) {
        facing_right = true;
        pet_cx = pet_cx + step > goal ? goal : pet_cx + step;
    } else if (pet_cx > goal) {
        facing_right = false;
        pet_cx = pet_cx - step < goal ? goal : pet_cx - step;
    }
    if (pet_cx == goal) {
        idle_ms = (int)(1200 + rnd(mood == MOOD_HAPPY ? 2500 : 5000));
        target_cx = lk->min_cx + (int)rnd((uint32_t)(lk->max_cx - lk->min_cx + 1));
    }
}

static void effect_particles(const look_t *lk, int left, int top) {
    int hx = left + lk->head_x, hy = top + lk->head_y;
    int t = EFFECT_MS - effect_ms;
    switch (effect) {
    case ACT_FEED:
        if (t % 450 < 20) for (int i = 0; i < 4; i++)
                spawn(P_CRUMB, left + lk->mouth_x - 2 + (int)rnd(6), top + lk->mouth_y + 4, (int)rnd(60) - 30, -(int)rnd(60), 900);
        break;
    case ACT_PLAY:
        if (t % 200 < 34) spawn(P_HEART, hx - 12 + (int)rnd(24), hy + 4, (int)rnd(30) - 15, -40 - (int)rnd(20), 1100);
        break;
    case ACT_CLEAN:
        if (t % 70 < 34) {
            spawn(P_SPARK, left + 4 + (int)rnd((uint32_t)lk->spr->w - 8), top + 4 + (int)rnd((uint32_t)lk->spr->h - 8), 0, 0, 500);
            spawn(P_BUBBLE, left + (int)rnd((uint32_t)lk->spr->w), top + lk->spr->h - 6, (int)rnd(20) - 10, -30 - (int)rnd(30), 900);
        }
        break;
    case ACT_MEDICINE:
        if (t > 700 && t % 120 < 34) spawn(P_PLUS, hx - 16 + (int)rnd(32), hy + 6 + (int)rnd(12), 0, -30, 700);
        break;
    }
}

// --- App -------------------------------------------------------------------

static void pet_background(uint32_t dt) { pet_tick(pet_saved(), dt); }

static void pet_enter(void) {
    pet_t *p = pet_saved();
    pet_tick(p, 0);
    const look_t *lk = &LOOKS[p->stage];
    pet_cx = ((lk->min_cx + lk->max_cx) / 2) << 8;
    target_cx = pet_cx >> 8;
    idle_ms = 1500;
    panel = false;
    effect_ms = seq_ms = 0;
    toast_ms = poke_ms = 0;
    label_ms = 2500;
    commit_ms = 0;
    dark = (p->flags & PET_ASLEEP) ? 3 : 0;
    for (int i = 0; i < PARTS; i++) parts[i].life = 0;
    if (sel < 0 || sel >= PET_ACTIONS) sel = 0;
}

static void draw_pet(const pet_t *p, int wish) {
    const look_t *lk = &LOOKS[p->stage];
    int left = pet_left(lk), top = lk->top;
    bool walking = idle_ms <= 0 && effect_ms <= 0 && !(p->flags & PET_ASLEEP) && lk->speed && (pet_cx >> 8) != target_cx;
    int bob = walking ? -(int)((anim_ms / 160) % 2) : -(int)((anim_ms / 700) % 2);
    if (p->flags & PET_ASLEEP) bob = -(int)((anim_ms / 1400) % 2);
    int jx = 0;

    if (p->stage == PET_EGG) {
        // The egg wobbles, more often and harder as it gets close to hatching.
        int left_s = PET_HATCH_S - (int)p->age_s;
        uint32_t every = left_s > 60 ? 4000 : left_s > 20 ? 2000 : 700;
        int amp = left_s > 60 ? 1 : 2;
        uint32_t ph = anim_ms % every;
        if (ph < 500) jx = ((ph / 80) % 2 ? amp : -amp);
        if (poke_ms) jx = ((poke_ms / 70) % 2) ? 2 : -2;
        bob = 0;
        if (ph < 500 && left_s <= 20) bob = -(int)((ph / 120) % 2);
        draw_nest(120, false);
        blit(lk->spr, left + jx, top + bob, false, false);
        if (left_s <= 20) draw_cracks(left + jx, top + bob, (20 - left_s) * 256 / 20);
        draw_nest(120, true);
        return;
    }

    // Small hops while eating and bigger ones while playing.
    int t = EFFECT_MS - effect_ms;
    if (effect_ms > 0 && effect == ACT_FEED && t < 1350 && t % 450 > 300) bob = -2;
    if (effect_ms > 0 && effect == ACT_PLAY) {
        int h = t % 500;
        bob = -h * (500 - h) / (p->stage == PET_ADULT ? 15000 : 8000);
    }
    blit(lk->spr, left, top + bob, facing_right, p->flags & PET_SICK);
    int hx = left + lk->head_x, hy = top + lk->head_y + bob;

    // Feeding: a drumstick in front of the mouth, gone in three bites.
    int big = p->stage == PET_ADULT ? 2 : 1;
    if (effect_ms > 0 && effect == ACT_FEED && t < 1350)
        draw_icon_scaled(ICON_MEAT, 12, left + lk->mouth_x - 6 * big, top + lk->mouth_y + bob - 2 * big,
                         12 - (t / 450) * 4, big);
    // Playing: a ball bouncing about in front.
    if (effect_ms > 0 && effect == ACT_PLAY) {
        int h = t % 400;
        int bx = left + lk->spr->w / 2 - 30 + t * 60 / EFFECT_MS - 6;
        int by = 101 - h * (400 - h) / 1500;
        draw_icon(ICON_BALL, 12, bx, by, 12);
    }
    // Medicine: the potion comes down to the head and is drunk.
    if (effect_ms > 0 && effect == ACT_MEDICINE && t < 800)
        gfx_sprite(&spr_potion_red, hx - 8, hy - 26 + t / 50, 1, 0, 0);

    if (p->flags & PET_SICK)
        draw_icon_scaled(ICON_THERMO, 10, hx + 14 * big, hy + 4 * big + (int)((anim_ms / 500) % 2), 5, big);
    else if (pet_mood(p) == MOOD_URGENT && (anim_ms / 900) % 2) draw_icon(ICON_DROP, 5, hx - 16, hy + 4, 4);

    if (!(p->flags & PET_ASLEEP) && effect_ms <= 0 && seq_ms <= 0) {
        int bx = hx + (p->stage == PET_ADULT ? 34 : 20), by = hy - 20;
        if (bx + 26 > SCREEN_LONG - 2) bx = hx - (p->stage == PET_ADULT ? 34 : 20) - 24;
        if (by < 2) by = 2;
        if ((anim_ms / 250) % 12 != 0) draw_thought(wish, hx, hy, bx, by);
    }
}

// Droppings, fixed spots at the front of the floor so the pet never hides them.
static void draw_poops(const pet_t *p) {
    static const uint8_t SPOTS[PET_MAX_POOPS][2] = {{14, 102}, {214, 103}, {178, 104}};
    for (int i = 0; i < p->poops && i < PET_MAX_POOPS; i++) {
        int x = SPOTS[i][0], y = SPOTS[i][1];
        draw_icon(ICON_POOP, 9, x, y, 10);
        // Wavy stink lines.
        for (int s = 0; s < 2; s++) {
            int phase = (int)((anim_ms / 200 + (uint32_t)(s * 2 + i)) % 4);
            for (int k = 0; k < 5; k++)
                gfx_pixel(x + 2 + s * 5 + ((k + phase) % 4 < 2 ? 0 : 1), y - 2 - k - (phase % 2), rgb(150, 190, 110));
        }
    }
}

static void draw_sequence(const pet_t *p) {
    if (seq_stage == PET_BABY) {
        const look_t *egg = &LOOKS[PET_EGG], *baby = &LOOKS[PET_BABY];
        int ex = 120 - egg->spr->w / 2;
        draw_nest(120, false);
        if (seq_ms < 1500) {
            int jx = (seq_ms / 50) % 2 ? 2 : -2;
            int hop = (seq_ms / 300) % 2 && seq_ms > 600 ? -2 : 0;
            blit(egg->spr, ex + jx, egg->top + hop, false, false);
            draw_cracks(ex + jx, egg->top + hop, 256);
        } else {
            blit(baby->spr, 120 - baby->spr->w / 2, baby->top, false, false);
        }
        draw_nest(120, true);
        if (seq_ms >= 1500 && seq_ms < 1800) {
            // A burst of light: a flash that opens into a ring.
            int t = seq_ms - 1500, r = 8 + t / 7;
            uint16_t c = rgb_lerp(C_WHITE, rgb(255, 220, 140), t * 255 / 300);
            if (t < 120) gfx_circle(120, 84, r, c);
            else for (int k = 0; k < 3; k++) gfx_ring(120, 84, r - k, c);
        }
    } else {
        // Growing up: baby and adult shapes flicker faster and faster.
        const look_t *baby = &LOOKS[PET_BABY], *adult = &LOOKS[PET_ADULT];
        int cx = pet_cx >> 8;
        if (seq_ms < 1700) {
            bool big = (seq_ms * seq_ms / 60000) % 2;
            const look_t *lk = big ? adult : baby;
            blit(lk->spr, cx - lk->spr->w / 2, lk->top, facing_right, false);
            gfx_sprite(lk->spr, cx - lk->spr->w / 2, lk->top, 1, SPR_SOLID | (facing_right ? SPR_FLIP_X : 0),
                       rgb(255, 250, 230));
        } else {
            blit(adult->spr, cx - adult->spr->w / 2, adult->top, facing_right, false);
        }
    }
    (void)p;
}

static void update_sequence(pet_t *p, uint32_t dt) {
    if (!seq_ms && (p->flags & PET_NEWS)) {
        seq_stage = p->stage;
        seq_ms = 1;
        effect_ms = 0;
        panel = false;
        if (p->stage == PET_ADULT) {
            const look_t *lk = &LOOKS[PET_ADULT];
            int cx = pet_cx >> 8;
            pet_cx = (cx < lk->min_cx ? lk->min_cx : cx > lk->max_cx ? lk->max_cx : cx) << 8;
        } else {
            pet_cx = 120 << 8;
        }
        target_cx = pet_cx >> 8;
    }
    if (!seq_ms) return;
    int before = seq_ms;
    seq_ms += (int)dt;
    if (seq_stage == PET_BABY && before < 1500 && seq_ms >= 1500) {
        for (int i = 0; i < 14; i++) spawn(P_SHELL, 112 + (int)rnd(16), 72 + (int)rnd(14), (int)rnd(160) - 80, -60 - (int)rnd(120), 1400);
        for (int i = 0; i < 8; i++) spawn(P_SPARK, 90 + (int)rnd(60), 50 + (int)rnd(40), 0, 0, 700);
        show_toast("It hatched!");
    }
    if (seq_stage == PET_ADULT) {
        if (seq_ms < 1700 && seq_ms % 100 < (int)dt + 1)
            spawn(P_SPARK, (pet_cx >> 8) - 60 + (int)rnd(120), 24 + (int)rnd(80), 0, 0, 600);
        if (before < 1700 && seq_ms >= 1700) {
            for (int i = 0; i < 12; i++) spawn(P_SPARK, (pet_cx >> 8) - 70 + (int)rnd(140), 20 + (int)rnd(90), 0, 0, 900);
            show_toast("All grown up!");
        }
    }
    if (seq_ms >= (seq_stage == PET_BABY ? HATCH_SEQ_MS : GROW_SEQ_MS)) {
        seq_ms = 0;
        p->flags &= (uint8_t)~PET_NEWS;
        idle_ms = 1500;
    }
}

static void draw_bar(void) {
    gfx_rect(0, BAR_Y, SCREEN_LONG, SCREEN_SHORT - BAR_Y, rgb(52, 34, 40));
    gfx_hline(0, BAR_Y, SCREEN_LONG, rgb(110, 76, 66));
    gfx_hline(0, BAR_Y + 1, SCREEN_LONG, rgb(80, 54, 54));
    uint16_t arrow = rgb(236, 210, 160);
    int my = BAR_Y + 10;
    for (int i = 0; i < 4; i++) {
        gfx_vline(5 + i, my - i, 1 + 2 * i, arrow);                   // B: previous
        gfx_vline(SCREEN_LONG - 6 - i, my - i, 1 + 2 * i, arrow);    // Y: next
    }
    for (int a = 0; a < PET_ACTIONS; a++) {
        int x = SLOT_X + a * SLOT_W;
        if (a == sel) {
            gfx_round_rect(x + 2, BAR_Y + 2, SLOT_W - 4, 17, 4, rgb(250, 226, 168));
            gfx_round_rect(x + 3, BAR_Y + 3, SLOT_W - 6, 15, 3, rgb(255, 240, 200));
        }
        draw_action_icon(a, x + SLOT_W / 2 - 6, BAR_Y + 4);
    }
}

static void draw_label(const pet_t *p) {
    const char *name = ACTION_NAMES[sel];
    if (sel == ACT_SLEEP && (p->flags & PET_ASLEEP)) name = "Wake";
    int tw = gfx_text_width(name, &font_mini, 1);
    int w = tw + 20, cx = SLOT_X + sel * SLOT_W + SLOT_W / 2;
    int x = cx - w / 2;
    if (x < 2) x = 2;
    if (x + w > SCREEN_LONG - 2) x = SCREEN_LONG - 2 - w;
    int y = BAR_Y - 13;
    gfx_round_rect(x, y, w, 12, 3, rgb(52, 34, 40));
    gfx_round_rect(x + 1, y + 1, w - 2, 10, 2, rgb(255, 240, 200));
    // A blue badge for the X button, coloured as on the menu.
    gfx_round_rect(x + 3, y + 2, 9, 8, 2, rgb(60, 140, 230));
    gfx_text(x + 5, y + 3, "X", C_WHITE, &font_mini, 1);
    gfx_text(x + 15, y + 3, name, rgb(52, 34, 40), &font_mini, 1);
}

static void draw_toast(void) {
    int w = gfx_text_width(toast, &font_mini, 1) + 14;
    int x = SCREEN_LONG / 2 - w / 2, y = 3;
    gfx_round_rect(x, y, w, 13, 4, rgb(52, 34, 40));
    gfx_round_rect(x + 1, y + 1, w - 2, 11, 3, rgb(255, 240, 200));
    gfx_text(x + 7, y + 4, toast, rgb(52, 34, 40), &font_mini, 1);
}

static void format_age(char *buf, size_t n, uint32_t s) {
    if (s < 3600) snprintf(buf, n, "Age %lum %02lus", (unsigned long)(s / 60), (unsigned long)(s % 60));
    else if (s < 86400) snprintf(buf, n, "Age %luh %02lum", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
    else snprintf(buf, n, "Age %lud %luh", (unsigned long)(s / 86400), (unsigned long)(s / 3600 % 24));
}

static void draw_panel(const pet_t *p) {
    static const char *const STAGES[3] = {"DRAGON EGG", "BABY DRAGON", "RED DRAGON"};
    static const char *const LABELS[PET_NEEDS] = {"Hunger", "Happy", "Energy", "Hygiene"};
    static const uint8_t ICON_FOR[PET_NEEDS] = {ACT_FEED, ACT_PLAY, ACT_SLEEP, ACT_CLEAN};
    const int x = 22, y = 6, w = 196, h = 104;
    dim_rows(0, BAR_Y, 1);
    gfx_round_rect(x - 1, y - 1, w + 2, h + 2, 6, rgb(236, 200, 130));
    gfx_round_rect(x + 1, y + 1, w - 2, h - 2, 5, rgb(44, 30, 42));
    uint16_t ink_c = rgb(255, 240, 200), soft = rgb(190, 160, 140);
    gfx_text(x + 8, y + 6, STAGES[p->stage], ink_c, &font_blocks, 1);
    char buf[32];
    int m = pet_clock(p);
    snprintf(buf, sizeof buf, "Day %d  %02d:%02d", pet_day(p), m / 60, m % 60);
    gfx_text(x + w - 8 - gfx_text_width(buf, &font_mini, 1), y + 7, buf, soft, &font_mini, 1);
    gfx_hline(x + 6, y + 17, w - 12, rgb(90, 64, 70));
    for (int i = 0; i < PET_NEEDS; i++) {
        int ry = y + 22 + i * 15;
        draw_action_icon(ICON_FOR[i], x + 8, ry);
        gfx_text(x + 26, ry + 3, LABELS[i], ink_c, &font_mini, 1);
        int v = pet_need(p, i);
        const int bx = x + 72, bw = 92;
        gfx_round_rect(bx, ry + 2, bw, 9, 2, rgb(24, 16, 22));
        uint16_t c = rgb_hsv(v * 120 / 100, 190, 235);
        int fill = (bw - 2) * v / 100;
        if (fill > 0) {
            gfx_rect(bx + 1, ry + 3, fill, 7, c);
            gfx_hline(bx + 1, ry + 3, fill, rgb_lerp(c, C_WHITE, 110));
        }
        snprintf(buf, sizeof buf, "%d", v);
        gfx_text(x + w - 8 - gfx_text_width(buf, &font_mini, 1), ry + 3, buf, soft, &font_mini, 1);
    }
    format_age(buf, sizeof buf, p->age_s);
    gfx_text(x + 8, y + h - 13, buf, ink_c, &font_mini, 1);
    const char *status = "Content";
    uint16_t sc = rgb(140, 220, 140);
    if (p->stage == PET_EGG) {
        int left = PET_HATCH_S - (int)p->age_s;
        snprintf(buf, sizeof buf, "Hatches in %d:%02d", left / 60, left % 60);
        status = buf;
        sc = rgb(255, 210, 120);
    } else if (p->flags & PET_SICK) {
        status = "Sick! Needs medicine", sc = rgb(255, 120, 110);
    } else if (p->flags & PET_ASLEEP) {
        status = "Asleep", sc = rgb(160, 180, 255);
    } else if (pet_mood(p) == MOOD_URGENT) {
        status = "Very unhappy", sc = rgb(255, 120, 110);
    } else if (pet_mood(p) == MOOD_NEEDY) {
        status = "Wants something", sc = rgb(255, 210, 120);
    } else if (p->poops) {
        status = "Needs a clean", sc = rgb(255, 210, 120);
    }
    gfx_text(x + w - 8 - gfx_text_width(status, &font_mini, 1), y + h - 13, status, sc, &font_mini, 1);
}

static void set_led(const pet_t *p) {
    int mood = pet_mood(p);
    if (mood == MOOD_URGENT) {
        uint32_t ph = anim_ms % 2400;
        int tri = (int)(ph < 1200 ? ph : 2400 - ph); // 0..1200
        hal_led((uint8_t)(8 + tri * 72 / 1200), 0, 0);
    } else if (mood == MOOD_NEEDY) {
        hal_led(60, 26, 0);
    } else if (p->flags & PET_ASLEEP) {
        hal_led(0, 0, 6);
    } else {
        hal_led(0, 36, 10);
    }
}

static bool pet_frame(const input_t *in, uint32_t dt) {
    pet_t *p = pet_saved();
    pet_tick(p, 0); // makes sure the slot holds a pet
    anim_ms += dt;

    if (in->pressed & BTN_B) { sel = (sel + PET_ACTIONS - 1) % PET_ACTIONS; label_ms = 2000; }
    if (in->pressed & BTN_Y) { sel = (sel + 1) % PET_ACTIONS; label_ms = 2000; }
    if (in->pressed & BTN_A) panel = !panel;
    if ((in->pressed & BTN_X) && !seq_ms) do_action(p, sel);

    update_sequence(p, dt);
    const look_t *lk = &LOOKS[p->stage];
    wander(p, lk, dt);
    if (effect_ms > 0) {
        effect_particles(lk, pet_left(lk), lk->top);
        effect_ms -= (int)dt;
    }
    update_parts(dt);
    label_ms = label_ms > dt ? label_ms - dt : 0;
    toast_ms = toast_ms > dt ? toast_ms - dt : 0;
    poke_ms = poke_ms > dt ? poke_ms - dt : 0;
    // Lights go off and on in three steps.
    int want = (p->flags & PET_ASLEEP) && !seq_ms ? 3 : 0;
    dark_step_ms += (int)dt;
    if (dark != want && dark_step_ms >= 140) {
        dark += want > dark ? 1 : -1;
        dark_step_ms = 0;
    }

    draw_room(p);
    if (seq_ms) {
        draw_sequence(p);
    } else {
        draw_pet(p, pet_wish(p));
    }
    draw_poops(p);
    draw_parts();
    if (dark) {
        dim_rows(0, BAR_Y, dark);
        // The window still lets the sky in.
        draw_window(p);
        if (p->flags & PET_ASLEEP && dark == 3) {
            int left = pet_left(lk), top = lk->top;
            draw_zzz(left + lk->head_x, top + lk->head_y);
        }
    }
    if (panel) draw_panel(p);
    if (toast_ms && !panel) draw_toast();
    draw_bar();
    if (label_ms && !panel) draw_label(p);

    set_led(p);
    commit_ms += dt;
    if (commit_ms >= COMMIT_MS) {
        commit_ms = 0;
        save_commit();
    }
    return true;
}

const app_t app_pet = {
    .name = "Virtual Pet",
    .orient = ORIENT_LANDSCAPE,
    .accent = rgb(240, 110, 150),
    .fps = 30,
    .enter = pet_enter,
    .frame = pet_frame,
    .background = pet_background,
};

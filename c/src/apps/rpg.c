// Pico Quest: a small Dragon Quest-style RPG, played in portrait.
//
// Walk through the Whispering Forest, the Old Ruins and the Dragon's Lair,
// one battle at a time, resting at a campfire in between, and slay the red
// dragon at the end. Progress is saved, so leaving and coming back resumes.
//
// Controls, with the Display Pack held upright:
//   Y (bottom-left)   cursor up / previous
//   X (bottom-right)  cursor down / next
//   A (top-right)     confirm, or hurry a message along
//   B (top-left)      back
// Holding A+B leaves the game; holding X+Y turns the screen upside down.
//
// The RGB LED flashes red when the hero is hit, green on a heal and gold on
// a level up.
//
// In attract mode a demo hero is dropped straight into a boss fight, the Red
// Dragon or the forest's Horn Beast by turns, and the autopilot works the
// menus at a person's pace. The demo hero is never saved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/app.h"
#include "../core/hal.h"
#include "../core/save.h"
#include "rpg.h"

// Screen bands, top to bottom.
#define TOP_H 18
#define SCENE_Y 18
#define SCENE_H 120
#define MSG_Y 138
#define HERO_Y 152
#define MENU_Y 194
#define GROUND_Y (SCENE_Y + SCENE_H - 4)

#define C_GOLD rgb(255, 214, 92)
#define C_TEXT rgb(246, 244, 236)
#define C_DIM rgb(120, 124, 160)
#define C_INK rgb(20, 16, 40)

enum { SCR_TITLE, SCR_CAMP, SCR_SHOP, SCR_STATUS, SCR_BATTLE, SCR_ENDING };
enum { BP_INTRO, BP_MENU, BP_EVENTS, BP_DONE };
enum { MENU_MAIN, MENU_MAGIC, MENU_ITEM };

static rpg_hero_t hero;
static int screen, cursor;
static uint32_t now_ms, play_ms;
static char note[RPG_TEXT];
static uint32_t note_ms, banner_ms;
static bool confirm_erase;

// Battle playback.
static rpg_battle_t battle;
static rpg_events_t evs;
static int phase, menu, ev_index, start_zone;
static uint32_t ev_ms;
static bool ev_impacted;
static int shown_enemy, shown_hp, shown_mp;      // what the bars show
static int target_enemy, target_hp, target_mp;   // where they are heading
static int bx0, by0, bx1, by1;                   // the monster's opaque box
static uint32_t intro_ms, flash_ms, shake_ms, lunge_ms, slash_ms, fire_ms, heal_ms, screen_shake_ms, dissolve_ms;
static uint32_t level_ms, lost_ms;
static uint16_t heal_colour;

typedef struct {
    int16_t x, y;
    uint16_t age;
    uint16_t colour;
    uint8_t big;
    char text[8];
} floater_t;
static floater_t floats[4];

static uint32_t led_ms;
static uint8_t led_rgb[3];

// Attract mode.
static bool demo;           // the hero is attract mode's and is never saved
static int demo_next;       // which demo hero plays next
static rpg_demo_t demo_memo; // the demo's choices so far this battle
static int ap_cmd, ap_arg;  // the command the autopilot is steering towards
static uint32_t ap_wait;    // ms before the autopilot's next press
static int ap_seen = -1;    // the screen and menu it last looked at
static bool ap_tapped;      // a button went down last frame; let it go

// ---- helpers ----

static void led(uint8_t r, uint8_t g, uint8_t b, uint32_t ms) {
    led_rgb[0] = r, led_rgb[1] = g, led_rgb[2] = b;
    led_ms = ms;
}

static void save_hero(void) {
    if (demo) return;
    hero.play_s += play_ms / 1000;
    play_ms %= 1000;
    memset(g_save.rpg, 0, sizeof g_save.rpg);
    memcpy(g_save.rpg, &hero, sizeof hero);
}

static void say(const char *s) {
    snprintf(note, sizeof note, "%s", s);
    note_ms = 2200;
}

static void add_float(int x, int y, const char *text, uint16_t colour, bool big) {
    int slot = 0;
    for (int i = 0; i < 4; i++)
        if (floats[i].age > floats[slot].age || !floats[i].text[0]) slot = i;
    for (int i = 0; i < 4; i++)
        if (!floats[i].text[0]) { slot = i; break; }
    floater_t *f = &floats[slot];
    f->x = (int16_t)x, f->y = (int16_t)y, f->age = 0, f->colour = colour, f->big = big;
    snprintf(f->text, sizeof f->text, "%s", text);
}

static uint32_t count_down(uint32_t v, uint32_t dt) { return v > dt ? v - dt : 0; }

static int approach(int v, int target, uint32_t dt) {
    int step = (int)dt / 12 + 1;
    if (v < target) return v + step > target ? target : v + step;
    if (v > target) return v - step < target ? target : v - step;
    return v;
}

static void text_right(int right, int y, const char *s, uint16_t c) {
    gfx_text(right - gfx_text_width(s, &font_mini, 1), y, s, c, &font_mini, 1);
}

static void small_flame(int x, int y) {
    // A 7x8 flame icon.
    static const char *const rows[] = {"...#...", "..##...", "..###..", ".##o##.", ".#ooo#.", "#oo*oo#", "#o***o#", ".#####."};
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 7; i++) {
            char ch = rows[j][i];
            if (ch == '.') continue;
            gfx_pixel(x + i, y + j, ch == '#' ? rgb(230, 60, 30) : ch == 'o' ? rgb(255, 160, 40) : rgb(255, 240, 150));
        }
}

// ---- shared panels ----

static void draw_top(const char *left, uint16_t left_colour, const char *right) {
    rpg_draw_panel(0, 0, gfx_w, TOP_H);
    gfx_text(6, 6, left, left_colour, &font_mini, 1);
    if (right) text_right(gfx_w - 6, 6, right, C_TEXT);
}

static void draw_message(const char *s, bool more) {
    rpg_draw_panel(0, MSG_Y, gfx_w, 14);
    gfx_text(5, MSG_Y + 4, s, C_TEXT, &font_mini, 1);
    if (more && (now_ms / 300) % 2) {
        gfx_hline(gfx_w - 10, MSG_Y + 6, 5, C_GOLD);
        gfx_hline(gfx_w - 9, MSG_Y + 7, 3, C_GOLD);
        gfx_pixel(gfx_w - 8, MSG_Y + 8, C_GOLD);
    }
}

static void draw_hero_panel(int hp, int mp, bool hurt) {
    int y = HERO_Y;
    rpg_draw_panel(0, y, gfx_w, 42);
    // Portrait in a gold frame, the sword held at its side.
    gfx_rect(4, y + 4, 34, 34, hurt ? rgb(255, 80, 80) : rgb(240, 214, 140));
    gfx_vgradient(5, y + 5, 32, 32, rgb(120, 190, 250), rgb(70, 150, 90));
    gfx_rect(5, y + 29, 32, 8, rgb(70, 150, 90));
    gfx_sprite(&spr_hero, 5, y + 5, 2, 0, 0);
    if (hurt) gfx_blend_rect(5, y + 5, 32, 32, rgb(255, 40, 40), 110);
    gfx_sprite(&spr_sword, 25, y + 22, 1, 0, 0);

    int max_hp = rpg_max_hp(hero.level), max_mp = rpg_max_mp(hero.level);
    char buf[24];
    gfx_text(43, y + 5, "HERO", C_TEXT, &font_mini, 1);
    snprintf(buf, sizeof buf, "Lv %d", hero.level);
    text_right(gfx_w - 5, y + 5, buf, C_GOLD);

    rpg_draw_heart(42, y + 16);
    uint16_t hi = hp * 2 > max_hp ? rgb(110, 236, 110) : hp * 4 > max_hp ? rgb(250, 220, 70) : rgb(255, 80, 70);
    uint16_t lo = hp * 2 > max_hp ? rgb(30, 150, 60) : hp * 4 > max_hp ? rgb(200, 130, 20) : rgb(170, 20, 30);
    rpg_draw_bar(51, y + 16, 40, 6, hp, max_hp, hi, lo);
    snprintf(buf, sizeof buf, "%d/%d", hp, max_hp);
    text_right(gfx_w - 5, y + 16, buf, C_TEXT);

    rpg_draw_star(42, y + 26, rgb(110, 180, 255));
    rpg_draw_bar(51, y + 27, 40, 6, mp, max_mp, rgb(120, 200, 255), rgb(40, 80, 200));
    snprintf(buf, sizeof buf, "%d/%d", mp, max_mp);
    text_right(gfx_w - 5, y + 27, buf, C_TEXT);
}

typedef struct {
    const char *label;
    const char *right; // small text at the right of the cell, or NULL
    int icon;          // ICON_*
    bool disabled;
} cell_t;

enum { ICON_NONE, ICON_SWORD, ICON_FLAME, ICON_POTION, ICON_ETHER, ICON_RUN, ICON_HEART, ICON_STAR, ICON_CHEST, ICON_SHIELD, ICON_TENT };

static void draw_icon(int icon, int x, int y) {
    switch (icon) {
    case ICON_SWORD: gfx_sprite(&spr_sword, x, y, 1, 0, 0); break;
    case ICON_FLAME: small_flame(x + 4, y + 4); break;
    case ICON_POTION: gfx_sprite(&spr_potion_red, x, y, 1, 0, 0); break;
    case ICON_ETHER: gfx_sprite(&spr_potion_blue, x, y, 1, 0, 0); break;
    case ICON_HEART: rpg_draw_heart(x + 4, y + 5); break;
    case ICON_STAR: rpg_draw_star(x + 4, y + 4, C_GOLD); break;
    case ICON_CHEST: gfx_sprite(&spr_chest, x, y, 1, 0, 0); break;
    case ICON_SHIELD: gfx_sprite(&spr_shield, x, y, 1, 0, 0); break;
    case ICON_TENT:
        for (int j = 0; j < 9; j++) gfx_hline(x + 8 - j, y + 4 + j, 2 * j + 1, j > 4 ? rgb(166, 140, 104) : rgb(214, 190, 140));
        gfx_vline(x + 8, y + 9, 4, rgb(46, 30, 34));
        break;
    case ICON_RUN:
        // A chunky arrow pointing away, with speed lines.
        for (int k = 0; k < 5; k++) gfx_vline(x + 2 + k, y + 8 - k, 1 + 2 * k, rgb(120, 220, 140));
        gfx_rect(x + 7, y + 6, 6, 5, rgb(120, 220, 140));
        gfx_vline(x + 2, y + 8, 1, rgb(60, 140, 80));
        gfx_hline(x + 7, y + 10, 6, rgb(60, 140, 80));
        gfx_hline(x + 14, y + 6, 2, C_TEXT);
        gfx_hline(x + 14, y + 8, 2, C_TEXT);
        gfx_hline(x + 14, y + 10, 2, C_TEXT);
        break;
    default: break;
    }
}

// A 2x2 command grid in the bottom band.
static void draw_grid(const cell_t cells[4], int sel) {
    rpg_draw_panel(0, MENU_Y, gfx_w, 46);
    for (int i = 0; i < 4; i++) {
        if (!cells[i].label) continue;
        int x = 4 + (i % 2) * 64, y = MENU_Y + 4 + (i / 2) * 19;
        bool on = i == sel;
        if (on) {
            gfx_round_rect(x, y, 63, 18, 3, rgb(255, 238, 180));
            gfx_round_rect(x + 1, y + 1, 61, 16, 2, rgb(255, 250, 226));
        }
        draw_icon(cells[i].icon, x + 1, y + 1);
        uint16_t c = cells[i].disabled ? (on ? rgb(150, 140, 130) : C_DIM) : on ? C_INK : C_TEXT;
        gfx_text(x + 19, y + 6, cells[i].label, c, &font_mini, 1);
        if (cells[i].right) text_right(x + 60, y + 6, cells[i].right, on ? rgb(150, 90, 30) : C_GOLD);
        if (on && (now_ms / 250) % 2) {
            // A small pointer at the cell's left edge.
            for (int k = 0; k < 3; k++) gfx_vline(x - 3 + k, y + 6 + k, 5 - 2 * k, C_GOLD);
        }
    }
}

static int grid_move(int sel, const input_t *in, int n) {
    if (in->pressed & BTN_Y) sel = (sel + n - 1) % n;
    if (in->pressed & BTN_X) sel = (sel + 1) % n;
    return sel;
}

static void draw_floats(uint32_t dt) {
    for (int i = 0; i < 4; i++) {
        floater_t *f = &floats[i];
        if (!f->text[0]) continue;
        f->age = (uint16_t)(f->age + dt);
        if (f->age > 900) {
            f->text[0] = 0;
            continue;
        }
        int rise = f->age < 300 ? f->age * 18 / 300 : 18 + (f->age - 300) * 4 / 600;
        int scale = f->big ? 3 : 2;
        int w = gfx_text_width(f->text, &font_mini, scale);
        gfx_text_outline(f->x - w / 2, f->y - rise - 3 * scale, f->text, f->colour, C_BLACK, &font_mini, scale);
    }
}

static void shake_screen(int dx) {
    if (!dx) return;
    for (int y = 0; y < gfx_h; y++) {
        uint16_t *row = &gfx_fb[y * gfx_w];
        if (dx > 0) {
            memmove(row + dx, row, (size_t)(gfx_w - dx) * 2);
            for (int x = 0; x < dx; x++) row[x] = C_BLACK;
        } else {
            memmove(row, row - dx, (size_t)(gfx_w + dx) * 2);
            for (int x = gfx_w + dx; x < gfx_w; x++) row[x] = C_BLACK;
        }
    }
}

// ---- screens: camp ----

static void go_camp(void) {
    screen = SCR_CAMP;
    cursor = 0;
    save_hero();
    hal_led(0, 0, 0);
}

static void camp_scene(bool shop) {
    int zone = hero.zone < RPG_ZONES ? hero.zone : RPG_ZONES - 1;
    rpg_draw_backdrop(zone, SCENE_Y, SCENE_H, now_ms, true);
    int base = SCENE_Y + SCENE_H - 10;
    if (shop) {
        // The wandering wizard behind his counter.
        rpg_glow(gfx_w / 2, base - 40, 44, rgb(255, 200, 120), 12);
        gfx_sprite(&spr_wizard, gfx_w / 2 - 24, base - 66, 3, 0, 0);
        gfx_rect(8, base - 22, gfx_w - 16, 24, rgb(122, 76, 44));
        gfx_rect(8, base - 22, gfx_w - 16, 3, rgb(176, 120, 70));
        for (int x = 24; x < gfx_w - 8; x += 22) gfx_vline(x, base - 19, 21, rgb(90, 54, 32));
        gfx_hline(8, base + 2, gfx_w - 16, rgb(60, 36, 24));
        gfx_sprite(&spr_potion_red, 14, base - 37, 1, 0, 0);
        gfx_sprite(&spr_potion_blue, 30, base - 37, 1, 0, 0);
        gfx_sprite(&spr_sword, 88, base - 37, 1, 0, 0);
        gfx_sprite(&spr_shield, 104, base - 37, 1, 0, 0);
        gfx_sprite(&spr_coin, 62, base - 14, 1, 0, 0);
        return;
    }
    rpg_draw_tent(4, base - 2);
    rpg_draw_campfire(70, base + 2, now_ms);
    gfx_sprite(&spr_hero, 90, base - 30, 2, SPR_FLIP_X, 0);
    gfx_sprite(&spr_sword, 116, base - 16, 1, 0, 0);
    if (banner_ms) {
        // The zone's name, shown on arrival.
        const char *name = rpg_zones[zone].name;
        int w = gfx_text_width(name, &font_mini, 1) + 16;
        rpg_draw_panel(gfx_w / 2 - w / 2, SCENE_Y + 20, w, 26);
        gfx_text_center(gfx_w / 2, SCENE_Y + 25, "Now entering", C_GOLD, &font_mini, 1);
        gfx_text_center(gfx_w / 2, SCENE_Y + 35, name, C_TEXT, &font_mini, 1);
    }
}

static void draw_inventory_line(void) {
    char buf[16];
    rpg_draw_panel(0, MSG_Y, gfx_w, 14);
    gfx_sprite_cell(&spr_coin, 3, 3, 10, 10, 3, MSG_Y + 2, 1, 0);
    snprintf(buf, sizeof buf, "%lu", (unsigned long)hero.gold);
    gfx_text(15, MSG_Y + 4, buf, C_GOLD, &font_mini, 1);
    gfx_sprite_cell(&spr_potion_red, 3, 2, 10, 12, 62, MSG_Y + 1, 1, 0);
    snprintf(buf, sizeof buf, "x%d", hero.potions);
    gfx_text(74, MSG_Y + 4, buf, C_TEXT, &font_mini, 1);
    gfx_sprite_cell(&spr_potion_blue, 3, 2, 10, 12, 96, MSG_Y + 1, 1, 0);
    snprintf(buf, sizeof buf, "x%d", hero.ethers);
    gfx_text(108, MSG_Y + 4, buf, C_TEXT, &font_mini, 1);
}

static void start_battle(void);

static void camp_frame(const input_t *in) {
    cursor = grid_move(cursor, in, 4);
    if (in->pressed & BTN_A) {
        switch (cursor) {
        case 0: start_battle(); return;
        case 1:
            if (hero.hp == rpg_max_hp(hero.level) && hero.mp == rpg_max_mp(hero.level)) say("You are already rested.");
            else if (rpg_rest(&hero)) {
                say("You rest. HP and MP restored!");
                led(0, 120, 30, 500);
                save_hero();
            } else say("Not enough gold to rest.");
            break;
        case 2: screen = SCR_SHOP, cursor = 0; say("Welcome, traveller!"); break;
        case 3: screen = SCR_STATUS; break;
        }
    }
    char buf[24];
    int zone = hero.zone < RPG_ZONES ? hero.zone : RPG_ZONES - 1;
    snprintf(buf, sizeof buf, "%d/%d", hero.battle + 1, rpg_zones[zone].battles);
    draw_top(rpg_zones[zone].name, C_GOLD, buf);
    camp_scene(false);
    if (note_ms) draw_message(note, false);
    else draw_inventory_line();
    draw_hero_panel(hero.hp, hero.mp, false);
    char rest[12];
    snprintf(rest, sizeof rest, "%dG", rpg_rest_price(&hero));
    cell_t cells[4] = {
        {hero.battle == rpg_zones[zone].battles - 1 ? "Boss!" : "Onward", NULL, ICON_SWORD, false},
        {"Rest", rest, ICON_TENT, hero.gold < (uint32_t)rpg_rest_price(&hero)},
        {"Shop", NULL, ICON_CHEST, false},
        {"Status", NULL, ICON_SHIELD, false},
    };
    draw_grid(cells, cursor);
}

// ---- shop ----

static void shop_frame(const input_t *in) {
    cursor = grid_move(cursor, in, RPG_SHOP_ITEMS);
    if (in->pressed & BTN_B) {
        go_camp();
        return;
    }
    if (in->pressed & BTN_A) {
        if (rpg_buy(&hero, cursor)) {
            static const char *const thanks[] = {"A potion. Use it well!", "An ether. Mind the MP!", "Your sword grows sharper!",
                                                 "Your armour grows thicker!"};
            say(thanks[cursor]);
            led(120, 90, 0, 250);
            save_hero();
        } else {
            bool maxed = cursor == RPG_SHOP_SWORD ? hero.sword >= RPG_UPGRADES
                         : cursor == RPG_SHOP_ARMOR ? hero.armor >= RPG_UPGRADES
                         : cursor == RPG_SHOP_POTION ? hero.potions >= RPG_MAX_ITEMS
                                                     : hero.ethers >= RPG_MAX_ITEMS;
            say(maxed ? "You can't carry more." : "You need more gold.");
        }
    }
    char buf[24];
    snprintf(buf, sizeof buf, "%luG", (unsigned long)hero.gold);
    draw_top("Wandering Shop", C_GOLD, buf);
    camp_scene(true);
    static const char *const about[] = {"Heals half your HP", "Restores 20 MP", "Attack +4", "Defense +3"};
    draw_message(note_ms ? note : about[cursor], false);

    rpg_draw_panel(0, HERO_Y, gfx_w, gfx_h - HERO_Y);
    for (int i = 0; i < RPG_SHOP_ITEMS; i++) {
        int y = HERO_Y + 4 + i * 21;
        bool on = i == cursor;
        if (on) {
            gfx_round_rect(4, y, gfx_w - 8, 20, 3, rgb(255, 238, 180));
            gfx_round_rect(5, y + 1, gfx_w - 10, 18, 2, rgb(255, 250, 226));
        }
        static const int icons[] = {ICON_POTION, ICON_ETHER, ICON_SWORD, ICON_SHIELD};
        draw_icon(icons[i], 6, y + 2);
        char name[20], detail[24], price[12];
        int cost;
        bool maxed;
        switch (i) {
        case RPG_SHOP_POTION:
            snprintf(name, sizeof name, "Potion");
            snprintf(detail, sizeof detail, "You have %d", hero.potions);
            cost = RPG_POTION_PRICE, maxed = hero.potions >= RPG_MAX_ITEMS;
            break;
        case RPG_SHOP_ETHER:
            snprintf(name, sizeof name, "Ether");
            snprintf(detail, sizeof detail, "You have %d", hero.ethers);
            cost = RPG_ETHER_PRICE, maxed = hero.ethers >= RPG_MAX_ITEMS;
            break;
        case RPG_SHOP_SWORD:
            snprintf(name, sizeof name, "Sword +%d", hero.sword + 1);
            snprintf(detail, sizeof detail, "ATK %d", rpg_attack(&hero));
            cost = rpg_upgrade_price(hero.sword), maxed = hero.sword >= RPG_UPGRADES;
            break;
        default:
            snprintf(name, sizeof name, "Armour +%d", hero.armor + 1);
            snprintf(detail, sizeof detail, "DEF %d", rpg_defense(&hero));
            cost = rpg_upgrade_price(hero.armor), maxed = hero.armor >= RPG_UPGRADES;
            break;
        }
        if (maxed) snprintf(price, sizeof price, "MAX");
        else snprintf(price, sizeof price, "%dG", cost);
        bool poor = !maxed && hero.gold < (uint32_t)cost;
        gfx_text(26, y + 3, name, on ? C_INK : C_TEXT, &font_mini, 1);
        gfx_text(26, y + 11, detail, on ? rgb(110, 100, 120) : C_DIM, &font_mini, 1);
        text_right(gfx_w - 9, y + 7, price, poor ? rgb(230, 90, 80) : on ? rgb(150, 90, 30) : C_GOLD);
    }
}

// ---- status ----

static void status_frame(const input_t *in) {
    if (in->pressed & (BTN_A | BTN_B)) {
        go_camp();
        cursor = 3;
        return;
    }
    draw_top("Status", C_GOLD, hero.ng ? "New Game+" : NULL);
    rpg_draw_panel(0, TOP_H, gfx_w, gfx_h - TOP_H);
    int y = TOP_H + 6;
    gfx_rect(6, y, 52, 52, rgb(240, 214, 140));
    gfx_vgradient(7, y + 1, 50, 50, rgb(120, 190, 250), rgb(70, 150, 90));
    gfx_sprite(&spr_hero, 8, y + 2, 3, 0, 0);
    gfx_sprite(&spr_sword, 40, y + 32, 1, 0, 0);
    char buf[32];
    gfx_text(64, y + 2, "HERO", C_TEXT, &font_blocks, 1);
    snprintf(buf, sizeof buf, "Level %d", hero.level);
    gfx_text(64, y + 14, buf, C_GOLD, &font_mini, 1);
    uint32_t lo = rpg_xp_for_level(hero.level), hi = rpg_xp_for_level(hero.level + 1);
    gfx_text(64, y + 26, "Next level", C_DIM, &font_mini, 1);
    rpg_draw_bar(64, y + 35, 64, 6, (int)(hero.xp - lo), (int)(hi - lo), rgb(255, 230, 120), rgb(200, 140, 30));
    snprintf(buf, sizeof buf, "%lu XP to go", (unsigned long)(hi - hero.xp));
    gfx_text(64, y + 44, buf, C_TEXT, &font_mini, 1);

    y += 60;
    unsigned secs = (unsigned)(hero.play_s + play_ms / 1000);
    char spells[24];
    snprintf(spells, sizeof spells, "Fire Heal%s", rpg_knows_spell(&hero, RPG_SPELL_BLAZE) ? " Blaze" : "");
    struct { const char *k; char v[24]; } rows[11];
    int n = 0;
    rows[n].k = "HP", snprintf(rows[n++].v, 24, "%d / %d", hero.hp, rpg_max_hp(hero.level));
    rows[n].k = "MP", snprintf(rows[n++].v, 24, "%d / %d", hero.mp, rpg_max_mp(hero.level));
    rows[n].k = "Attack", snprintf(rows[n++].v, 24, "%d  (sword +%d)", rpg_attack(&hero), hero.sword);
    rows[n].k = "Defense", snprintf(rows[n++].v, 24, "%d  (armour +%d)", rpg_defense(&hero), hero.armor);
    rows[n].k = "Experience", snprintf(rows[n++].v, 24, "%lu", (unsigned long)hero.xp);
    rows[n].k = "Gold", snprintf(rows[n++].v, 24, "%lu", (unsigned long)hero.gold);
    rows[n].k = "Items", snprintf(rows[n++].v, 24, "%d potion, %d ether", hero.potions, hero.ethers);
    rows[n].k = "Spells", snprintf(rows[n++].v, 24, "%s", spells);
    rows[n].k = "Victories", snprintf(rows[n++].v, 24, "%lu", (unsigned long)hero.wins);
    rows[n].k = "Defeats", snprintf(rows[n++].v, 24, "%lu", (unsigned long)hero.defeats);
    rows[n].k = "Time", snprintf(rows[n++].v, 24, "%u:%02u:%02u", secs / 3600, secs / 60 % 60, secs % 60);
    for (int i = 0; i < n; i++) {
        int ry = y + i * 13;
        if (i % 2 == 0) gfx_blend_rect(4, ry - 3, gfx_w - 8, 12, C_BLACK, 70);
        gfx_text(8, ry, rows[i].k, C_GOLD, &font_mini, 1);
        text_right(gfx_w - 8, ry, rows[i].v, C_TEXT);
    }
    gfx_text_center(gfx_w / 2, gfx_h - 10, "A or B: back to camp", C_DIM, &font_mini, 1);
}

// ---- battle ----

static void measure_monster(const sprite_t *s) {
    bx0 = s->w, by0 = s->h, bx1 = 0, by1 = 0;
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++)
            if (s->pixels[y * s->w + x]) {
                if (x < bx0) bx0 = x;
                if (x > bx1) bx1 = x;
                if (y < by0) by0 = y;
                if (y > by1) by1 = y;
            }
}

static void start_battle(void) {
    int m = rpg_pick_monster(&hero, hal_random());
    rpg_battle_start(&battle, &hero, m);
    measure_monster(rpg_monsters[m].spr);
    screen = SCR_BATTLE;
    phase = BP_INTRO;
    menu = MENU_MAIN;
    cursor = 0;
    start_zone = hero.zone;
    intro_ms = 600;
    flash_ms = shake_ms = lunge_ms = slash_ms = fire_ms = heal_ms = screen_shake_ms = dissolve_ms = 0;
    level_ms = lost_ms = 0;
    shown_enemy = target_enemy = battle.hp;
    shown_hp = target_hp = hero.hp;
    shown_mp = target_mp = hero.mp;
    for (int i = 0; i < 4; i++) floats[i].text[0] = 0;
    memset(&demo_memo, 0, sizeof demo_memo);
    evs.count = 1;
    ev_index = 0;
    ev_ms = 0;
    ev_impacted = true;
    evs.ev[0].kind = RPG_EV_TEXT;
    evs.ev[0].enemy_hp = battle.hp;
    evs.ev[0].hero_hp = (int16_t)hero.hp;
    evs.ev[0].hero_mp = (int16_t)hero.mp;
    const char *name = rpg_monsters[m].name;
    if (rpg_monsters[m].boss) snprintf(evs.ev[0].text, RPG_TEXT, "%s blocks the way!", name);
    else snprintf(evs.ev[0].text, RPG_TEXT, "%s appears!", name);
    if (gfx_text_width(evs.ev[0].text, &font_mini, 1) > 127) snprintf(evs.ev[0].text, RPG_TEXT, "%s!", name);
}

static int monster_cx(void) { return gfx_w / 2; }
static int monster_cy(void) {
    int s4 = rpg_monsters[battle.monster].scale4;
    return GROUND_Y - (by1 - by0) * s4 / 8;
}

// How long into an event its effect lands, and how long the event lasts.
static uint32_t impact_at(int kind) {
    return kind == RPG_EV_SLASH || kind == RPG_EV_CRIT ? 200 : kind == RPG_EV_FIRE ? 420 : 0;
}
static uint32_t event_length(int kind) {
    switch (kind) {
    case RPG_EV_DEFEATED: return 1400;
    case RPG_EV_REWARD: case RPG_EV_LEVEL: case RPG_EV_LOST: return 1700;
    case RPG_EV_FIRE: return 1300;
    default: return 1100;
    }
}

static void begin_event(void) {
    const rpg_event_t *e = &evs.ev[ev_index];
    ev_ms = 0;
    ev_impacted = false;
    if (e->kind == RPG_EV_SLASH || e->kind == RPG_EV_CRIT) slash_ms = 200;
    if (e->kind == RPG_EV_FIRE) fire_ms = 520;
    if (e->kind == RPG_EV_HERO_HIT) lunge_ms = 160;
}

static void impact(void) {
    const rpg_event_t *e = &evs.ev[ev_index];
    char buf[8];
    ev_impacted = true;
    target_enemy = e->enemy_hp;
    target_hp = e->hero_hp;
    target_mp = e->hero_mp;
    int mx = monster_cx(), my = monster_cy();
    switch (e->kind) {
    case RPG_EV_SLASH:
    case RPG_EV_CRIT:
    case RPG_EV_FIRE:
        flash_ms = 240;
        shake_ms = 320;
        snprintf(buf, sizeof buf, "%d", e->value);
        add_float(mx, my, buf, e->kind == RPG_EV_CRIT ? C_GOLD : e->kind == RPG_EV_FIRE ? rgb(255, 170, 60) : C_WHITE,
                  e->kind == RPG_EV_CRIT);
        if (e->kind == RPG_EV_CRIT) screen_shake_ms = 200;
        break;
    case RPG_EV_MISS: add_float(mx, my, "MISS", rgb(190, 190, 200), false); break;
    case RPG_EV_HERO_HIT:
        screen_shake_ms = 320;
        snprintf(buf, sizeof buf, "%d", e->value);
        add_float(21, HERO_Y + 34, buf, rgb(255, 90, 80), false);
        led(255, 0, 0, 260);
        break;
    case RPG_EV_HERO_DODGE: add_float(21, HERO_Y + 34, "MISS", rgb(190, 190, 200), false); break;
    case RPG_EV_HEAL:
    case RPG_EV_MANA:
        heal_ms = 700;
        heal_colour = e->kind == RPG_EV_HEAL ? rgb(120, 255, 140) : rgb(120, 190, 255);
        snprintf(buf, sizeof buf, "+%d", e->value);
        add_float(21, HERO_Y + 34, buf, heal_colour, false);
        if (e->kind == RPG_EV_HEAL) led(0, 255, 60, 400);
        else led(0, 80, 255, 400);
        break;
    case RPG_EV_DEFEATED: dissolve_ms = 1; break;
    case RPG_EV_LEVEL:
        if (e->value) {
            level_ms = 1700;
            led(255, 170, 0, 1200);
        }
        break;
    case RPG_EV_LOST: lost_ms = lost_ms ? lost_ms : 1; break;
    default: break;
    }
}

static void finish_battle(void) {
    if (battle.state == RPG_WON && rpg_cleared(&hero)) {
        screen = SCR_ENDING;
        cursor = 0;
        save_hero();
        return;
    }
    go_camp();
    if (battle.state == RPG_WON && hero.zone != start_zone) banner_ms = 2600;
    if (battle.state == RPG_LOST) say("You wake by the campfire...");
    if (battle.state == RPG_FLED) say("You got away safely.");
}

static void choose(int cmd, int arg) {
    if (!rpg_battle_turn(&battle, &hero, cmd, arg, &evs)) return;
    phase = BP_EVENTS;
    ev_index = 0;
    begin_event();
    menu = MENU_MAIN;
}

static void battle_input(const input_t *in) {
    const rpg_monster_t *m = &rpg_monsters[battle.monster];
    if (phase == BP_INTRO || phase == BP_EVENTS) {
        const rpg_event_t *e = &evs.ev[ev_index];
        bool last = ev_index == evs.count - 1;
        bool over = battle.state != RPG_ONGOING;
        uint32_t len = event_length(e->kind);
        bool skip = (in->pressed & BTN_A) && ev_impacted && ev_ms > impact_at(e->kind) + 200;
        if (last && over) {
            // The final message waits for A.
            if (skip || ((in->pressed & BTN_A) && ev_ms > len)) phase = BP_DONE;
            if (phase != BP_DONE) return;
        } else if (skip || ev_ms >= len) {
            if (last) {
                phase = BP_MENU;
                cursor = 0;
                menu = MENU_MAIN;
            } else {
                ev_index++;
                begin_event();
            }
            return;
        }
        if (phase != BP_DONE) return;
    }
    if (phase == BP_DONE) {
        finish_battle();
        return;
    }
    // Command menus.
    if (menu == MENU_MAIN) {
        cursor = grid_move(cursor, in, 4);
        if (in->pressed & BTN_A) {
            switch (cursor) {
            case 0: choose(RPG_CMD_ATTACK, 0); break;
            case 1: menu = MENU_MAGIC, cursor = 0; break;
            case 2: menu = MENU_ITEM, cursor = 0; break;
            case 3:
                if (m->boss) say("There is no escape!");
                else choose(RPG_CMD_RUN, 0);
                break;
            }
        }
    } else if (menu == MENU_MAGIC) {
        cursor = grid_move(cursor, in, RPG_SPELLS);
        if (in->pressed & BTN_B) menu = MENU_MAIN, cursor = 1;
        else if (in->pressed & BTN_A) {
            if (!rpg_knows_spell(&hero, cursor)) say("You learn Blaze at Lv 8.");
            else if (!rpg_can_cast(&hero, cursor)) say("Not enough MP!");
            else choose(RPG_CMD_MAGIC, cursor);
        }
    } else {
        cursor = grid_move(cursor, in, RPG_ITEMS);
        if (in->pressed & BTN_B) menu = MENU_MAIN, cursor = 2;
        else if (in->pressed & BTN_A) {
            if (cursor == RPG_ITEM_POTION && !hero.potions) say("You have no potions.");
            else if (cursor == RPG_ITEM_ETHER && !hero.ethers) say("You have no ethers.");
            else choose(RPG_CMD_ITEM, cursor);
        }
    }
}

static void draw_effects(void) {
    int mx = monster_cx(), my = monster_cy();
    if (slash_ms) {
        // Three sword streaks sweeping down across the monster.
        int p = 200 - (int)slash_ms; // 0..200
        int len = 20 + p * 50 / 200;
        for (int k = -1; k <= 1; k++) {
            int x0 = mx + 28 + k * 6, y0 = my - 34 + k * 2;
            int x1 = x0 - len, y1 = y0 + len;
            gfx_line(x0, y0, x1, y1, k ? rgb(255, 240, 160) : C_WHITE);
            gfx_line(x0 + 1, y0, x1 + 1, y1, k ? rgb(255, 200, 80) : C_WHITE);
        }
    }
    if (fire_ms) {
        int p = 520 - (int)fire_ms;
        rpg_glow(mx, my, 40, rgb(255, 120, 30), 16);
        for (int k = 0; k < 5; k++) {
            int fx = mx - 28 + k * 14 + (int)(rpg_hash((uint32_t)k) % 7u) - 3;
            int fy = my + 26 - (p * (20 + k * 5)) / 520;
            int size = 3 + (p / 70 + k) % 4;
            rpg_draw_flame(fx, fy, size, now_ms + (uint32_t)k * 100);
        }
    }
    if (heal_ms) {
        int p = 700 - (int)heal_ms;
        for (int k = 0; k < 6; k++) {
            int sx = 8 + (int)(rpg_hash((uint32_t)k + 11u) % 26u);
            int sy = HERO_Y + 34 - (p * (16 + k * 3)) / 700 - (k * 5) % 9;
            gfx_hline(sx - 1, sy, 3, heal_colour);
            gfx_vline(sx, sy - 1, 3, heal_colour);
            gfx_pixel(sx, sy, C_WHITE);
        }
    }
    if (level_ms && (now_ms / 120) % 4) {
        rpg_draw_panel(14, SCENE_Y + 10, gfx_w - 28, 20);
        gfx_text_center(gfx_w / 2, SCENE_Y + 16, "LEVEL UP!", C_GOLD, &font_blocks, 1);
        for (int k = 0; k < 8; k++) {
            uint32_t r = rpg_hash((uint32_t)k + now_ms / 100u);
            rpg_draw_star(10 + (int)(r % 115u), SCENE_Y + 36 + (int)((r >> 8) % 60u), C_GOLD);
        }
    }
}

static void battle_frame(const input_t *in, uint32_t dt) {
    const rpg_monster_t *m = &rpg_monsters[battle.monster];
    ev_ms += dt;
    if (phase == BP_INTRO || phase == BP_EVENTS) {
        const rpg_event_t *e = &evs.ev[ev_index];
        if (!ev_impacted && ev_ms >= impact_at(e->kind)) impact();
    }
    battle_input(in);
    if (screen != SCR_BATTLE) return; // the battle ended this frame

    intro_ms = count_down(intro_ms, dt);
    flash_ms = count_down(flash_ms, dt);
    shake_ms = count_down(shake_ms, dt);
    lunge_ms = count_down(lunge_ms, dt);
    slash_ms = count_down(slash_ms, dt);
    fire_ms = count_down(fire_ms, dt);
    heal_ms = count_down(heal_ms, dt);
    screen_shake_ms = count_down(screen_shake_ms, dt);
    level_ms = count_down(level_ms, dt);
    if (dissolve_ms) dissolve_ms += dt;
    if (lost_ms) lost_ms += dt;
    shown_enemy = approach(shown_enemy, target_enemy, dt);
    shown_hp = approach(shown_hp, target_hp, dt);
    shown_mp = approach(shown_mp, target_mp, dt);

    // Enemy name and health.
    draw_top(m->name, m->boss ? C_GOLD : C_TEXT, NULL);
    rpg_draw_bar(70, 6, 59, 6, shown_enemy, battle.max_hp, rgb(255, 140, 90), rgb(200, 30, 40));
    gfx_text(58, 6, "HP", rgb(255, 160, 120), &font_mini, 1);

    // The scene and the monster.
    rpg_draw_backdrop(start_zone, SCENE_Y, SCENE_H, now_ms, false);
    int s4 = m->scale4;
    int bob = (now_ms / 160) % 8;
    bob = bob < 4 ? bob : 8 - bob; // 0..4..0
    int x = monster_cx() - (bx0 + bx1 + 1) * s4 / 8;
    int y = GROUND_Y - (by1 + 1) * s4 / 4 + bob / 2;
    if (shake_ms) x += ((shake_ms / 40) % 2) ? 3 : -3;
    if (lunge_ms) y += 6;
    int dissolve = 0;
    if (intro_ms) dissolve = (int)intro_ms * 256 / 600;
    if (dissolve_ms) dissolve = dissolve_ms >= 700 ? 256 : (int)dissolve_ms * 256 / 700;
    if (dissolve < 256) {
        bool white = flash_ms && (flash_ms / 60) % 2 == 1;
        if (dissolve_ms && (dissolve_ms / 70) % 2) white = true;
        rpg_draw_scaled(m->spr, x, y, s4, white, C_WHITE, dissolve);
    }
    draw_effects();

    // Message line, hero, commands.
    const char *msg = NULL;
    bool more = false;
    if (phase == BP_INTRO || phase == BP_EVENTS || phase == BP_DONE) {
        msg = evs.ev[ev_index].text;
        more = ev_index == evs.count - 1 && battle.state != RPG_ONGOING && ev_ms > 400;
    } else if (note_ms) msg = note;
    else msg = menu == MENU_MAGIC ? "Cast which spell?" : menu == MENU_ITEM ? "Use which item?" : "What will you do?";
    draw_message(msg, more);
    bool hurt = screen_shake_ms > 0 && phase != BP_MENU && evs.ev[ev_index].kind == RPG_EV_HERO_HIT;
    draw_hero_panel(shown_hp, shown_mp, hurt);

    if (phase == BP_MENU) {
        char a[8], b[8], c[8], d[8];
        if (menu == MENU_MAIN) {
            cell_t cells[4] = {{"Attack", NULL, ICON_SWORD, false},
                               {"Magic", NULL, ICON_FLAME, false},
                               {"Item", NULL, ICON_POTION, false},
                               {"Run", NULL, ICON_RUN, m->boss}};
            draw_grid(cells, cursor);
        } else if (menu == MENU_MAGIC) {
            snprintf(a, sizeof a, "%d", rpg_spell_cost(RPG_SPELL_FIRE));
            snprintf(b, sizeof b, "%d", rpg_spell_cost(RPG_SPELL_HEAL));
            snprintf(c, sizeof c, "%s", rpg_knows_spell(&hero, RPG_SPELL_BLAZE) ? "8" : "Lv8");
            cell_t cells[4] = {{"Fire", a, ICON_FLAME, !rpg_can_cast(&hero, RPG_SPELL_FIRE)},
                               {"Heal", b, ICON_HEART, !rpg_can_cast(&hero, RPG_SPELL_HEAL)},
                               {"Blaze", c, ICON_STAR, !rpg_can_cast(&hero, RPG_SPELL_BLAZE)},
                               {NULL, NULL, 0, false}};
            draw_grid(cells, cursor);
        } else {
            snprintf(a, sizeof a, "x%d", hero.potions);
            snprintf(d, sizeof d, "x%d", hero.ethers);
            cell_t cells[4] = {{"Potion", a, ICON_POTION, !hero.potions},
                               {"Ether", d, ICON_ETHER, !hero.ethers},
                               {NULL, NULL, 0, false},
                               {NULL, NULL, 0, false}};
            draw_grid(cells, cursor);
        }
    } else {
        // While messages play the command box shows the battle's stakes.
        rpg_draw_panel(0, MENU_Y, gfx_w, 46);
        char buf[32];
        gfx_text_center(gfx_w / 2, MENU_Y + 9, rpg_zones[start_zone].name, C_GOLD, &font_mini, 1);
        snprintf(buf, sizeof buf, "Reward  %u XP  %u gold", battle.xp, battle.gold);
        gfx_text_center(gfx_w / 2, MENU_Y + 21, buf, C_DIM, &font_mini, 1);
        if (phase == BP_DONE || (battle.state != RPG_ONGOING && ev_index == evs.count - 1))
            gfx_text_center(gfx_w / 2, MENU_Y + 32, "A: continue", (now_ms / 400) % 2 ? C_TEXT : C_DIM, &font_mini, 1);
    }
    if (lost_ms) gfx_blend_rect(0, 0, gfx_w, gfx_h, rgb(90, 0, 10), lost_ms > 600 ? 120 : (int)lost_ms / 5);
    draw_floats(dt);
    if (screen_shake_ms) shake_screen(((screen_shake_ms / 40) % 2) ? 3 : -3);
}

// ---- title and ending ----

static void title_frame(const input_t *in) {
    bool can_continue = rpg_save_valid(&hero);
    int n = can_continue ? 2 : 1;
    if (confirm_erase) {
        if (in->pressed & BTN_A) {
            rpg_new_game(&hero);
            confirm_erase = false;
            go_camp();
            banner_ms = 2600;
            return;
        }
        if (in->pressed & BTN_B) confirm_erase = false;
    } else {
        cursor = grid_move(cursor, in, n);
        if (in->pressed & BTN_A) {
            if (can_continue && cursor == 0) {
                if (rpg_cleared(&hero)) screen = SCR_ENDING, cursor = 0;
                else go_camp();
                return;
            }
            if (can_continue) confirm_erase = true;
            else {
                rpg_new_game(&hero);
                go_camp();
                banner_ms = 2600;
                return;
            }
        }
    }

    // A dusk forest, the dragon looming over it, the hero facing it.
    rpg_draw_backdrop(0, 0, 170, now_ms, false);
    gfx_vgradient(0, 0, gfx_w, 60, rgb(40, 30, 90), rgb(120, 110, 200));
    gfx_blend_rect(0, 60, gfx_w, 110, rgb(60, 40, 110), 70);
    int hover = (int)((now_ms / 200) % 8);
    hover = hover < 4 ? hover : 8 - hover;
    rpg_draw_scaled(&spr_battler_28, gfx_w / 2 - 72, 50 + hover, 4, false, 0, 0);
    gfx_sprite(&spr_hero, gfx_w / 2 - 16, 126, 2, 0, 0);
    gfx_sprite(&spr_sword, gfx_w / 2 + 10, 140, 1, 0, 0);

    gfx_text_outline(gfx_w / 2 - gfx_text_width("PICO", &font_blocks, 2) / 2, 8, "PICO", C_GOLD, C_INK, &font_blocks, 2);
    gfx_text_outline(gfx_w / 2 - gfx_text_width("QUEST", &font_blocks, 2) / 2, 26, "QUEST", C_GOLD, C_INK, &font_blocks, 2);

    rpg_draw_panel(0, 170, gfx_w, 70);
    if (confirm_erase) {
        gfx_text_center(gfx_w / 2, 182, "Start over and erase", C_TEXT, &font_mini, 1);
        gfx_text_center(gfx_w / 2, 192, "your saved journey?", C_TEXT, &font_mini, 1);
        gfx_text_center(gfx_w / 2, 210, "A: yes      B: no", C_GOLD, &font_mini, 1);
        return;
    }
    const char *items[2];
    int k = 0;
    if (can_continue) items[k++] = "Continue";
    items[k++] = "New Game";
    for (int i = 0; i < n; i++) {
        int y = 178 + i * 20;
        bool on = i == cursor;
        if (on) {
            gfx_round_rect(18, y, gfx_w - 36, 17, 3, rgb(255, 238, 180));
            gfx_round_rect(19, y + 1, gfx_w - 38, 15, 2, rgb(255, 250, 226));
        }
        gfx_text_center(gfx_w / 2, y + 5, items[i], on ? C_INK : C_TEXT, &font_mini, 1);
    }
    if (can_continue) {
        char buf[32];
        snprintf(buf, sizeof buf, "Lv %d  -  %s", hero.level,
                 rpg_cleared(&hero) ? "Journey's end" : rpg_zones[hero.zone].name);
        gfx_text_center(gfx_w / 2, 220, buf, C_DIM, &font_mini, 1);
    }
    gfx_text_center(gfx_w / 2, 230, "Y/X move  A choose", C_DIM, &font_mini, 1);
}

static void ending_frame(const input_t *in) {
    cursor = grid_move(cursor, in, 2);
    if (in->pressed & BTN_A) {
        if (cursor == 0) {
            rpg_new_game_plus(&hero);
            go_camp();
            banner_ms = 2600;
        } else {
            screen = SCR_TITLE;
            cursor = 0;
        }
        return;
    }
    rpg_draw_backdrop(0, 0, 130, now_ms, true);
    rpg_draw_campfire(gfx_w / 2, 118, now_ms);
    gfx_sprite(&spr_hero, gfx_w / 2 - 40, 88, 2, 0, 0);
    gfx_sprite(&spr_sword, gfx_w / 2 - 16, 70, 2, 0, 0);
    gfx_sprite(&spr_chest, gfx_w / 2 + 14, 96, 2, 0, 0);
    gfx_text_outline(gfx_w / 2 - gfx_text_width("THE END", &font_blocks, 2) / 2, 10, "THE END", C_GOLD, C_INK,
                     &font_blocks, 2);
    gfx_text_outline(gfx_w / 2 - gfx_text_width("The dragon is slain!", &font_mini, 1) / 2, 32, "The dragon is slain!",
                     C_TEXT, C_INK, &font_mini, 1);

    rpg_draw_panel(0, 130, gfx_w, 110);
    unsigned secs = (unsigned)(hero.play_s + play_ms / 1000);
    char rows[5][24];
    snprintf(rows[0], 24, "%d", hero.level);
    snprintf(rows[1], 24, "%lu", (unsigned long)hero.gold);
    snprintf(rows[2], 24, "%lu", (unsigned long)hero.wins);
    snprintf(rows[3], 24, "%lu", (unsigned long)hero.defeats);
    snprintf(rows[4], 24, "%u:%02u:%02u", secs / 3600, secs / 60 % 60, secs % 60);
    static const char *const keys[] = {"Level", "Gold", "Victories", "Defeats", "Time"};
    for (int i = 0; i < 5; i++) {
        gfx_text(10, 138 + i * 11, keys[i], C_GOLD, &font_mini, 1);
        text_right(gfx_w - 10, 138 + i * 11, rows[i], C_TEXT);
    }
    static const char *const items[] = {"New Game+", "Title"};
    for (int i = 0; i < 2; i++) {
        int y = 196 + i * 19;
        bool on = i == cursor;
        if (on) {
            gfx_round_rect(18, y, gfx_w - 36, 17, 3, rgb(255, 238, 180));
            gfx_round_rect(19, y + 1, gfx_w - 38, 15, 2, rgb(255, 250, 226));
        }
        gfx_text_center(gfx_w / 2, y + 5, items[i], on ? C_INK : C_TEXT, &font_mini, 1);
    }
}

// ---- app ----

static void rpg_enter(void) {
    memcpy(&hero, g_save.rpg, sizeof hero);
    if (!rpg_save_valid(&hero)) memset(&hero, 0, sizeof hero);
    screen = SCR_TITLE;
    cursor = 0;
    confirm_erase = false;
    note_ms = banner_ms = 0;
    play_ms = 0;
    led_ms = 0;
    demo = false;
    rpg_set_random(NULL);
#ifdef PDG_SIM
    // Screenshots of later zones: PDG_RPG_JUMP=zone,battle,level starts a
    // fresh hero there, one XP short of the next level.
    const char *jump = getenv("PDG_RPG_JUMP");
    int z, b, l;
    if (jump && sscanf(jump, "%d,%d,%d", &z, &b, &l) == 3) {
        rpg_new_game(&hero);
        hero.zone = (uint8_t)z, hero.battle = (uint8_t)b, hero.level = (uint8_t)l;
        hero.xp = rpg_xp_for_level(l + 1) - 1;
        hero.hp = (uint16_t)rpg_max_hp(l), hero.mp = (uint16_t)rpg_max_mp(l);
        hero.gold = 40u * (uint32_t)l, hero.potions = 3, hero.ethers = 1;
        hero.sword = (uint8_t)(z * 2), hero.armor = (uint8_t)z;
    }
#endif
}

static bool rpg_frame(const input_t *in, uint32_t dt) {
    now_ms += dt;
    note_ms = count_down(note_ms, dt);
    banner_ms = count_down(banner_ms, dt);
    if (screen != SCR_TITLE && screen != SCR_ENDING) play_ms += dt;
    switch (screen) {
    case SCR_TITLE: title_frame(in); break;
    case SCR_CAMP: camp_frame(in); break;
    case SCR_SHOP: shop_frame(in); break;
    case SCR_STATUS: status_frame(in); break;
    case SCR_BATTLE: battle_frame(in, dt); break;
    default: ending_frame(in); break;
    }
    // A screen that changed this frame draws itself next frame; draw it now
    // so no frame is left half-drawn.
    static int drawn_screen = -1;
    if (screen != drawn_screen) {
        drawn_screen = screen;
        input_t none = {0, 0, 0};
        switch (screen) {
        case SCR_TITLE: title_frame(&none); break;
        case SCR_CAMP: camp_frame(&none); break;
        case SCR_SHOP: shop_frame(&none); break;
        case SCR_STATUS: status_frame(&none); break;
        case SCR_BATTLE: battle_frame(&none, 0); break;
        default: ending_frame(&none); break;
        }
    }
    led_ms = count_down(led_ms, dt);
    if (led_ms) hal_led(led_rgb[0], led_rgb[1], led_rgb[2]);
    else hal_led(0, 0, 0);
    return true;
}

static void rpg_leave(void) {
    if (rpg_save_valid(&hero)) save_hero();
}

// ---- attract mode ----

const rpg_hero_t *rpg_current_hero(void) { return &hero; }

static void rpg_attract(void) {
    demo = true;
    rpg_demo_hero(&hero, demo_next);
    demo_next = (demo_next + 1) % RPG_DEMOS;
    note_ms = banner_ms = 0;
    ap_wait = 0, ap_seen = -1, ap_tapped = false;
    start_battle();
}

// A press, held for this one frame, then nothing for wait ms. A little
// unevenness in the timing reads as a person rather than a machine.
static uint8_t ap_tap(uint8_t button, uint32_t wait) {
    ap_tapped = true;
    ap_wait = wait ? wait + rpg_hash(now_ms) % 90u : 0;
    return button;
}

// Moves the cursor a step the short way round towards target, or presses A
// on it and then waits after_ms.
static uint8_t ap_steer(int target, int n, uint32_t after_ms) {
    if (cursor == target) return ap_tap(BTN_A, after_ms);
    int ahead = (target - cursor + n) % n;
    return ap_tap(ahead <= n - ahead ? BTN_X : BTN_Y, 280);
}

static uint8_t rpg_autopilot(uint32_t dt) {
    if (ap_tapped) {
        ap_tapped = false;
        return 0;
    }
    // Something new on screen: give it a moment to be read.
    int seen = screen * 100 + (screen == SCR_BATTLE ? phase * 10 + menu : 0);
    if (seen != ap_seen) {
        ap_seen = seen;
        if (screen == SCR_CAMP) ap_wait = banner_ms ? 2700 : 1300;
        else if (screen == SCR_ENDING) ap_wait = 4500;
        else if (screen == SCR_BATTLE && phase == BP_MENU) {
            ap_wait = menu == MENU_MAIN ? 600 : 420;
            if (menu == MENU_MAIN) ap_cmd = rpg_demo_command(&battle, &hero, &demo_memo, &ap_arg);
        } else ap_wait = 700;
    }

    if (screen == SCR_BATTLE && phase != BP_MENU) {
        // Messages: let the boss's entrance play out, hurry the rest along
        // once each has landed, and dwell a little on the last one.
        if (phase == BP_DONE) return ap_tap(BTN_A, 0);
        if (phase == BP_INTRO) return 0;
        const rpg_event_t *e = &evs.ev[ev_index];
        bool final = ev_index == evs.count - 1 && battle.state != RPG_ONGOING;
        uint32_t at = final ? 1500 : event_length(e->kind) * 3 / 4;
        return ev_impacted && ev_ms >= at ? ap_tap(BTN_A, 0) : 0;
    }

    if (ap_wait > dt) {
        ap_wait -= dt;
        return 0;
    }
    ap_wait = 0;
    switch (screen) {
    case SCR_BATTLE:
        if (menu == MENU_MAIN) return ap_steer(ap_cmd == RPG_CMD_ITEM ? 2 : ap_cmd == RPG_CMD_MAGIC ? 1 : 0, 4, 0);
        return ap_steer(ap_arg, menu == MENU_MAGIC ? RPG_SPELLS : RPG_ITEMS, 0);
    case SCR_CAMP: {
        // Rest first when worn down, then on to the next fight.
        bool worn = hero.hp * 2 < rpg_max_hp(hero.level) && hero.gold >= (uint32_t)rpg_rest_price(&hero);
        return ap_steer(worn ? 1 : 0, 4, 1300);
    }
    case SCR_ENDING:
        // The journey's end has been seen; start the next demo.
        rpg_attract();
        return 0;
    case SCR_SHOP: return ap_tap(BTN_B, 0);
    default: return ap_tap(BTN_A, 0); // the title or the status screen
    }
}

const app_t app_rpg = {
    .name = "Pico Quest",
    .orient = ORIENT_PORTRAIT,
    .accent = rgb(60, 160, 80),
    .fps = 30,
    .enter = rpg_enter,
    .frame = rpg_frame,
    .leave = rpg_leave,
    .attract = rpg_attract,
    .autopilot = rpg_autopilot,
};

// Pico Quest: the rules, kept apart from drawing so tests can play it.
//
// The hero's whole state is one small struct that is copied into
// g_save.rpg as it is. Stats come from the level and the shop upgrades,
// so only those are stored.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../assets/assets.h"

#define RPG_ZONES 3
#define RPG_MAX_LEVEL 40
#define RPG_SAVE_MAGIC 0x5251u // "QR"
#define RPG_SAVE_VERSION 1

// What one potion and one ether restore, and what they cost.
#define RPG_POTION_PRICE 12
#define RPG_ETHER_PRICE 30
#define RPG_ETHER_MP 20
#define RPG_MAX_ITEMS 9
#define RPG_UPGRADES 5

typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t level;
    uint8_t zone;   // 0..2, RPG_ZONES once the dragon is beaten
    uint8_t battle; // index within the zone
    uint8_t sword, armor; // shop upgrades, 0..RPG_UPGRADES
    uint8_t potions, ethers;
    uint8_t ng;     // New Game+ count
    uint8_t pad;
    uint16_t hp, mp;
    uint32_t xp, gold;
    uint32_t wins, defeats;
    uint32_t play_s; // seconds played
} rpg_hero_t;

enum { RPG_SP_NONE, RPG_SP_SPELL, RPG_SP_BREATH };

typedef struct {
    const char *name;
    const sprite_t *spr;
    uint8_t scale4; // drawn size in quarters: 4 = 1x, 6 = 1.5x, 8 = 2x
    uint16_t hp;
    uint8_t atk, def;
    uint16_t xp, gold;
    uint8_t special;
    uint8_t boss; // no running away
} rpg_monster_t;

typedef struct {
    const char *name;
    uint8_t battles;  // the last one is the guardian
    uint8_t first, count; // the zone's ordinary monsters, weakest first
    uint8_t guardian;
} rpg_zone_t;

extern const rpg_monster_t rpg_monsters[];
extern const rpg_zone_t rpg_zones[RPG_ZONES];
extern const int rpg_monster_count;

// Stats for a level, without and with the shop upgrades.
int rpg_max_hp(int level);
int rpg_max_mp(int level);
int rpg_attack(const rpg_hero_t *h);
int rpg_defense(const rpg_hero_t *h);
// Total XP needed to stand at a level; level 1 needs none.
uint32_t rpg_xp_for_level(int level);
// Adds XP, levelling up as often as it reaches. A level up adds the gain in
// maximum HP and MP to the current values too. Returns the levels gained.
int rpg_gain_xp(rpg_hero_t *h, uint32_t xp);

// Physical damage. base = atk - def / 2, at least 1; the result is
// base * (224..288) / 256, so 7/8 to 9/8 of base, and never below 1.
int rpg_damage(int atk, int def, uint32_t r);
int rpg_damage_min(int atk, int def);
int rpg_damage_max(int atk, int def);

enum { RPG_SPELL_FIRE, RPG_SPELL_HEAL, RPG_SPELL_BLAZE, RPG_SPELLS };
extern const char *const rpg_spell_names[RPG_SPELLS];
int rpg_spell_cost(int spell);
bool rpg_knows_spell(const rpg_hero_t *h, int spell);
bool rpg_can_cast(const rpg_hero_t *h, int spell);

enum { RPG_ITEM_POTION, RPG_ITEM_ETHER, RPG_ITEMS };
// Restores half the maximum HP (at least 30), capped at the maximum.
// Returns the HP restored, 0 without a potion.
int rpg_use_potion(rpg_hero_t *h);
int rpg_use_ether(rpg_hero_t *h);

// Shop. Returns false when unaffordable or maxed out.
int rpg_upgrade_price(int tier);
int rpg_rest_price(const rpg_hero_t *h);
bool rpg_buy(rpg_hero_t *h, int what); // RPG_SHOP_*
bool rpg_rest(rpg_hero_t *h);
enum { RPG_SHOP_POTION, RPG_SHOP_ETHER, RPG_SHOP_SWORD, RPG_SHOP_ARMOR, RPG_SHOP_ITEMS };

void rpg_new_game(rpg_hero_t *h);
// Keeps level, gold, items and upgrades and starts the journey again, harder.
void rpg_new_game_plus(rpg_hero_t *h);
bool rpg_save_valid(const rpg_hero_t *h);
bool rpg_cleared(const rpg_hero_t *h);
// The monster for the hero's current battle; r picks among the zone's.
int rpg_pick_monster(const rpg_hero_t *h, uint32_t r);

// ---- battles ----

enum { RPG_CMD_ATTACK, RPG_CMD_MAGIC, RPG_CMD_ITEM, RPG_CMD_RUN };
enum { RPG_ONGOING, RPG_WON, RPG_LOST, RPG_FLED };

typedef struct {
    uint8_t monster;
    uint8_t state;
    uint8_t turn;
    int16_t hp, max_hp;
    int16_t atk, def;
    uint16_t xp, gold;
} rpg_battle_t;

// What the screen plays back, one message at a time.
enum {
    RPG_EV_TEXT,
    RPG_EV_SLASH,      // hero hits the monster; value = damage
    RPG_EV_CRIT,       // the same, critical
    RPG_EV_FIRE,       // a spell hits the monster
    RPG_EV_MISS,       // hero misses
    RPG_EV_HERO_HIT,   // monster hits the hero
    RPG_EV_HERO_DODGE, // monster misses
    RPG_EV_HEAL,       // hero regains HP
    RPG_EV_MANA,       // hero regains MP
    RPG_EV_DEFEATED,   // monster falls
    RPG_EV_REWARD,
    RPG_EV_LEVEL,
    RPG_EV_LOST,
    RPG_EV_FLED,
};

#define RPG_MAX_EVENTS 10
#define RPG_TEXT 32

typedef struct {
    uint8_t kind;
    int16_t value;
    // State after this event, so bars move as each message plays.
    int16_t enemy_hp, hero_hp, hero_mp;
    char text[RPG_TEXT];
} rpg_event_t;

typedef struct {
    rpg_event_t ev[RPG_MAX_EVENTS];
    int count;
} rpg_events_t;

// Replaces hal_random for the battle rolls; NULL puts hal_random back.
void rpg_set_random(uint32_t (*fn)(void));

void rpg_battle_start(rpg_battle_t *b, const rpg_hero_t *h, int monster);
// Plays a whole round: the hero's command, then the monster's answer.
// Winning hands out XP and gold and moves the hero on to the next battle;
// losing halves the gold, heals the hero and steps back one battle.
// Returns false (and adds no events) when the command cannot be used.
bool rpg_battle_turn(rpg_battle_t *b, rpg_hero_t *h, int cmd, int arg, rpg_events_t *out);

// ---- attract mode ----

// The heroes attract mode plays, each dropped straight in front of a boss:
// the Red Dragon at Lv 20, or the forest's Horn Beast at Lv 7, a few XP
// short of learning Blaze, then on into the Old Ruins.
enum { RPG_DEMO_DRAGON, RPG_DEMO_FOREST, RPG_DEMOS };
void rpg_demo_hero(rpg_hero_t *h, int which);

// What the demo has done so far in a battle; zero it when one starts.
typedef struct {
    uint8_t round, heals;
    uint8_t last; // the command chosen last round
} rpg_demo_t;

// The command the demo picks this round, chosen to look good and to win:
// it opens with Attack, then alternates magic (Blaze, or Fire before Blaze
// is learned) with Attack, and heals whenever the monster's worst next blow
// could bring the hero near zero: Heal, or a potion when the hero is nearly
// down, and the other of the two the next time.
// Sets *arg to the spell or item.
int rpg_demo_command(const rpg_battle_t *b, const rpg_hero_t *h, rpg_demo_t *d, int *arg);

// The hero the RPG is playing now (in rpg.c), for tests.
const rpg_hero_t *rpg_current_hero(void);

// ---- drawing helpers, in rpg_art.c ----

// The zone's backdrop in a band of the screen, animated by t (ms). Night
// darkens it for the camp and lights it with a campfire glow at fire_x.
void rpg_draw_backdrop(int zone, int y0, int h, uint32_t t, bool night);
// A framed UI panel.
void rpg_draw_panel(int x, int y, int w, int h);
void rpg_draw_bar(int x, int y, int w, int h, int value, int max, uint16_t hi, uint16_t lo);
void rpg_draw_heart(int x, int y);
void rpg_draw_star(int x, int y, uint16_t c);
void rpg_draw_flame(int cx, int base, int size, uint32_t t);
void rpg_draw_campfire(int cx, int base, uint32_t t);
void rpg_draw_tent(int x, int base);
// Blends c into a soft disc, strongest (alpha 0..32) at the centre.
void rpg_glow(int cx, int cy, int r, uint16_t c, int alpha);
// A sprite at scale4 / 4 size. solid != 0 draws every pixel in solid_colour;
// dissolve 0..256 drops that share of the pixels.
void rpg_draw_scaled(const sprite_t *s, int x, int y, int scale4, bool solid, uint16_t solid_colour, int dissolve);
// Fast RGB565 mix, alpha 0..32.
uint16_t rpg_mix(uint16_t a, uint16_t b, int alpha);
uint32_t rpg_hash(uint32_t x);

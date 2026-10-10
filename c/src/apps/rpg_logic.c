// Pico Quest's rules: stats, damage, the shop and whole battle rounds.
// Nothing here draws; rpg.c plays the events back on screen.
#include <stdio.h>
#include <string.h>

#include "../core/gfx.h"
#include "../core/hal.h"
#include "../core/save.h"
#include "rpg.h"

_Static_assert(sizeof(rpg_hero_t) <= SAVE_RPG_BYTES, "rpg save must fit g_save.rpg");

// Scales are chosen so every monster stands about 90 to 115 pixels tall.
const rpg_monster_t rpg_monsters[] = {
    // Whispering Forest
    {"Shellbug", &spr_battler_22, 8, 16, 9, 2, 4, 6, RPG_SP_NONE, 0},
    {"Blue Jelly", &spr_battler_23, 8, 22, 11, 3, 5, 8, RPG_SP_NONE, 0},
    {"Mushroom", &spr_battler_29, 8, 28, 13, 5, 7, 11, RPG_SP_SPELL, 0},
    {"Pink Imp", &spr_battler_19, 8, 34, 16, 7, 9, 13, RPG_SP_NONE, 0},
    {"Ghost", &spr_battler_12, 8, 40, 19, 9, 11, 16, RPG_SP_SPELL, 0},
    {"Horn Beast", &spr_battler_04, 8, 70, 22, 10, 30, 50, RPG_SP_NONE, 1},
    // Old Ruins
    {"Sandworm", &spr_battler_16, 8, 50, 24, 12, 14, 18, RPG_SP_NONE, 0},
    {"Wyvern", &spr_battler_06, 8, 58, 27, 14, 16, 21, RPG_SP_NONE, 0},
    {"Brute", &spr_battler_25, 6, 68, 30, 17, 19, 24, RPG_SP_NONE, 0},
    {"Lizard", &spr_battler_17, 6, 76, 33, 19, 22, 27, RPG_SP_NONE, 0},
    {"Gold Knight", &spr_battler_08, 6, 86, 36, 22, 25, 31, RPG_SP_NONE, 0},
    {"Witch", &spr_battler_20, 8, 130, 40, 24, 60, 90, RPG_SP_SPELL, 1},
    // Dragon's Lair
    {"Ogre", &spr_battler_18, 6, 110, 44, 26, 32, 36, RPG_SP_NONE, 0},
    {"Crab Demon", &spr_battler_15, 5, 125, 48, 29, 36, 41, RPG_SP_NONE, 0},
    {"Golem", &spr_battler_11, 4, 140, 52, 33, 40, 46, RPG_SP_SPELL, 0},
    {"Red Dragon", &spr_battler_28, 4, 420, 58, 34, 200, 300, RPG_SP_BREATH, 1},
};
const int rpg_monster_count = (int)(sizeof rpg_monsters / sizeof rpg_monsters[0]);

const rpg_zone_t rpg_zones[RPG_ZONES] = {
    {"Whispering Forest", 10, 0, 5, 5},
    {"Old Ruins", 12, 6, 5, 11},
    {"Dragon's Lair", 12, 12, 3, 15},
};

const char *const rpg_spell_names[RPG_SPELLS] = {"Fire", "Heal", "Blaze"};
static const uint8_t spell_cost[RPG_SPELLS] = {3, 4, 8};
#define BLAZE_LEVEL 8

static uint32_t (*rng)(void) = hal_random;
void rpg_set_random(uint32_t (*fn)(void)) { rng = fn ? fn : hal_random; }

int rpg_max_hp(int level) { return 22 + 8 * level; }
int rpg_max_mp(int level) { return 4 + 3 * level; }
int rpg_attack(const rpg_hero_t *h) { return 5 + 3 * h->level + 4 * h->sword; }
int rpg_defense(const rpg_hero_t *h) { return 1 + 2 * h->level + 3 * h->armor; }
uint32_t rpg_xp_for_level(int level) { return level <= 1 ? 0 : 4u * (uint32_t)(level - 1) * (uint32_t)level; }

int rpg_gain_xp(rpg_hero_t *h, uint32_t xp) {
    h->xp += xp;
    int gained = 0;
    while (h->level < RPG_MAX_LEVEL && h->xp >= rpg_xp_for_level(h->level + 1)) {
        h->hp = (uint16_t)(h->hp + rpg_max_hp(h->level + 1) - rpg_max_hp(h->level));
        h->mp = (uint16_t)(h->mp + rpg_max_mp(h->level + 1) - rpg_max_mp(h->level));
        h->level++;
        gained++;
    }
    return gained;
}

static int damage_base(int atk, int def) {
    int base = atk - def / 2;
    return base < 1 ? 1 : base;
}
int rpg_damage(int atk, int def, uint32_t r) {
    int d = damage_base(atk, def) * (224 + (int)(r % 65)) / 256;
    return d < 1 ? 1 : d;
}
int rpg_damage_min(int atk, int def) {
    int d = damage_base(atk, def) * 224 / 256;
    return d < 1 ? 1 : d;
}
int rpg_damage_max(int atk, int def) {
    int d = damage_base(atk, def) * 288 / 256;
    return d < 1 ? 1 : d;
}

int rpg_spell_cost(int spell) { return spell >= 0 && spell < RPG_SPELLS ? spell_cost[spell] : 255; }
bool rpg_knows_spell(const rpg_hero_t *h, int spell) {
    return spell == RPG_SPELL_FIRE || spell == RPG_SPELL_HEAL || (spell == RPG_SPELL_BLAZE && h->level >= BLAZE_LEVEL);
}
bool rpg_can_cast(const rpg_hero_t *h, int spell) {
    return rpg_knows_spell(h, spell) && h->mp >= rpg_spell_cost(spell);
}

static int heal_hp(rpg_hero_t *h, int amount) {
    int room = rpg_max_hp(h->level) - h->hp;
    if (amount > room) amount = room;
    if (amount < 0) amount = 0;
    h->hp = (uint16_t)(h->hp + amount);
    return amount;
}

int rpg_use_potion(rpg_hero_t *h) {
    if (!h->potions) return 0;
    h->potions--;
    int amount = rpg_max_hp(h->level) / 2;
    return heal_hp(h, amount < 30 ? 30 : amount);
}

int rpg_use_ether(rpg_hero_t *h) {
    if (!h->ethers) return 0;
    h->ethers--;
    int room = rpg_max_mp(h->level) - h->mp, amount = RPG_ETHER_MP < room ? RPG_ETHER_MP : room;
    if (amount < 0) amount = 0;
    h->mp = (uint16_t)(h->mp + amount);
    return amount;
}

static const uint16_t upgrade_price[RPG_UPGRADES] = {60, 140, 260, 420, 600};
int rpg_upgrade_price(int tier) { return tier >= 0 && tier < RPG_UPGRADES ? upgrade_price[tier] : 0; }
int rpg_rest_price(const rpg_hero_t *h) { return 2 + 2 * h->level; }

bool rpg_buy(rpg_hero_t *h, int what) {
    int price;
    uint8_t *count;
    int max;
    switch (what) {
    case RPG_SHOP_POTION: price = RPG_POTION_PRICE, count = &h->potions, max = RPG_MAX_ITEMS; break;
    case RPG_SHOP_ETHER: price = RPG_ETHER_PRICE, count = &h->ethers, max = RPG_MAX_ITEMS; break;
    case RPG_SHOP_SWORD: price = rpg_upgrade_price(h->sword), count = &h->sword, max = RPG_UPGRADES; break;
    case RPG_SHOP_ARMOR: price = rpg_upgrade_price(h->armor), count = &h->armor, max = RPG_UPGRADES; break;
    default: return false;
    }
    if (*count >= max || h->gold < (uint32_t)price) return false;
    h->gold -= (uint32_t)price;
    (*count)++;
    return true;
}

bool rpg_rest(rpg_hero_t *h) {
    uint32_t price = (uint32_t)rpg_rest_price(h);
    if (h->gold < price) return false;
    h->gold -= price;
    h->hp = (uint16_t)rpg_max_hp(h->level);
    h->mp = (uint16_t)rpg_max_mp(h->level);
    return true;
}

void rpg_new_game(rpg_hero_t *h) {
    memset(h, 0, sizeof *h);
    h->magic = RPG_SAVE_MAGIC;
    h->version = RPG_SAVE_VERSION;
    h->level = 1;
    h->hp = (uint16_t)rpg_max_hp(1);
    h->mp = (uint16_t)rpg_max_mp(1);
    h->potions = 2;
    h->gold = 10;
}

void rpg_new_game_plus(rpg_hero_t *h) {
    h->zone = 0;
    h->battle = 0;
    if (h->ng < 255) h->ng++;
    h->hp = (uint16_t)rpg_max_hp(h->level);
    h->mp = (uint16_t)rpg_max_mp(h->level);
}

bool rpg_save_valid(const rpg_hero_t *h) {
    return h->magic == RPG_SAVE_MAGIC && h->version == RPG_SAVE_VERSION && h->level >= 1 &&
           h->level <= RPG_MAX_LEVEL && h->zone <= RPG_ZONES &&
           (h->zone == RPG_ZONES || h->battle < rpg_zones[h->zone].battles);
}

bool rpg_cleared(const rpg_hero_t *h) { return h->zone >= RPG_ZONES; }

int rpg_pick_monster(const rpg_hero_t *h, uint32_t r) {
    const rpg_zone_t *z = &rpg_zones[h->zone < RPG_ZONES ? h->zone : RPG_ZONES - 1];
    if (h->battle >= z->battles - 1) return z->guardian;
    // Early battles draw from the weakest monsters, later ones from the strongest.
    int lo = h->battle * z->count / (z->battles - 1) - 1;
    if (lo < 0) lo = 0;
    if (lo > z->count - 1) lo = z->count - 1;
    int hi = lo + 1 < z->count ? lo + 1 : lo;
    return z->first + lo + (int)(r % (uint32_t)(hi - lo + 1));
}

// New Game+ makes everything half as strong again per round, up to ten times.
static int ng_scale(const rpg_hero_t *h, int v, int pct) {
    int ng = h->ng > 9 ? 9 : h->ng;
    return v * (100 + pct * ng) / 100;
}

void rpg_battle_start(rpg_battle_t *b, const rpg_hero_t *h, int monster) {
    const rpg_monster_t *m = &rpg_monsters[monster];
    memset(b, 0, sizeof *b);
    b->monster = (uint8_t)monster;
    b->state = RPG_ONGOING;
    b->max_hp = b->hp = (int16_t)ng_scale(h, m->hp, 60);
    b->atk = (int16_t)ng_scale(h, m->atk, 60);
    b->def = (int16_t)ng_scale(h, m->def, 60);
    b->xp = (uint16_t)ng_scale(h, m->xp, 50);
    b->gold = (uint16_t)ng_scale(h, m->gold, 50);
}

// Copies a message into a text slot, cutting it short if it is too long.
static void copy_text(char *out, const char *s) {
    size_t n = 0;
    while (s[n] && n < RPG_TEXT - 1) out[n] = s[n], n++;
    out[n] = 0;
}

// Writes the long message if it fits the message line, the short one if not.
static void fit(char *out, const char *longer, const char *shorter) {
    copy_text(out, gfx_text_width(longer, &font_mini, 1) <= 127 ? longer : shorter);
}

static rpg_event_t *push(rpg_events_t *out, const rpg_battle_t *b, const rpg_hero_t *h, int kind, int value) {
    if (out->count >= RPG_MAX_EVENTS) out->count = RPG_MAX_EVENTS - 1;
    rpg_event_t *e = &out->ev[out->count++];
    e->kind = (uint8_t)kind;
    e->value = (int16_t)value;
    e->enemy_hp = b->hp;
    e->hero_hp = (int16_t)h->hp;
    e->hero_mp = (int16_t)h->mp;
    e->text[0] = 0;
    return e;
}

static void hurt_monster(rpg_battle_t *b, int dmg) {
    b->hp = (int16_t)(b->hp > dmg ? b->hp - dmg : 0);
}

static void monster_turn(rpg_battle_t *b, rpg_hero_t *h, rpg_events_t *out) {
    const char *name = rpg_monsters[b->monster].name;
    int special = rpg_monsters[b->monster].special;
    int def = rpg_defense(h), dmg;
    char a[48], s[48];
    if (special == RPG_SP_BREATH && b->turn % 3 == 2) {
        dmg = rpg_damage(b->atk * 5 / 4, def / 2, rng());
        snprintf(a, sizeof a, "Fire breath! %d damage", dmg);
        snprintf(s, sizeof s, "Fire breath! %d dmg", dmg);
    } else if (special == RPG_SP_SPELL && rng() % 3 == 0) {
        dmg = rpg_damage(b->atk, def / 2, rng());
        snprintf(a, sizeof a, "%s casts Hex! %d damage", name, dmg);
        snprintf(s, sizeof s, "%s: Hex! %d dmg", name, dmg);
    } else if (rng() % 16 == 0) {
        snprintf(a, sizeof a, "%s attacks! You dodge", name);
        snprintf(s, sizeof s, "%s misses!", name);
        fit(push(out, b, h, RPG_EV_HERO_DODGE, 0)->text, a, s);
        return;
    } else {
        dmg = rpg_damage(b->atk, def, rng());
        snprintf(a, sizeof a, "%s attacks! %d damage", name, dmg);
        snprintf(s, sizeof s, "%s hits! %d dmg", name, dmg);
    }
    h->hp = (uint16_t)(h->hp > dmg ? h->hp - dmg : 0);
    fit(push(out, b, h, RPG_EV_HERO_HIT, dmg)->text, a, s);
}

static void win(rpg_battle_t *b, rpg_hero_t *h, rpg_events_t *out) {
    const rpg_monster_t *m = &rpg_monsters[b->monster];
    char a[48], s[48];
    b->state = RPG_WON;
    snprintf(a, sizeof a, "%s is defeated!", m->name);
    snprintf(s, sizeof s, "%s falls!", m->name);
    fit(push(out, b, h, RPG_EV_DEFEATED, 0)->text, a, s);

    int old_level = h->level, old_hp = rpg_max_hp(old_level), old_mp = rpg_max_mp(old_level);
    int old_atk = rpg_attack(h), old_def = rpg_defense(h);
    h->gold += b->gold;
    h->wins++;
    int levels = rpg_gain_xp(h, b->xp);
    snprintf(push(out, b, h, RPG_EV_REWARD, b->xp)->text, RPG_TEXT, "+%u XP   +%u gold", b->xp, b->gold);
    if (levels) {
        snprintf(push(out, b, h, RPG_EV_LEVEL, h->level)->text, RPG_TEXT, "Level up! You are Lv %d", h->level);
        snprintf(a, sizeof a, "HP+%d MP+%d ATK+%d DEF+%d", rpg_max_hp(h->level) - old_hp,
                 rpg_max_mp(h->level) - old_mp, rpg_attack(h) - old_atk, rpg_defense(h) - old_def);
        copy_text(push(out, b, h, RPG_EV_LEVEL, 0)->text, a);
        if (old_level < BLAZE_LEVEL && h->level >= BLAZE_LEVEL)
            snprintf(push(out, b, h, RPG_EV_LEVEL, 0)->text, RPG_TEXT, "You learned Blaze!");
    }
    // On to the next battle, or the next zone.
    if (h->zone < RPG_ZONES && ++h->battle >= rpg_zones[h->zone].battles) {
        h->battle = 0;
        h->zone++;
    }
}

static void lose(rpg_battle_t *b, rpg_hero_t *h, rpg_events_t *out) {
    b->state = RPG_LOST;
    uint32_t lost = h->gold - h->gold / 2;
    h->gold /= 2;
    h->defeats++;
    snprintf(push(out, b, h, RPG_EV_LOST, 0)->text, RPG_TEXT, "You have fallen...");
    h->hp = (uint16_t)rpg_max_hp(h->level);
    h->mp = (uint16_t)rpg_max_mp(h->level);
    if (h->battle) h->battle--;
    snprintf(push(out, b, h, RPG_EV_LOST, 0)->text, RPG_TEXT, "You lost %lu gold", (unsigned long)lost);
}

bool rpg_battle_turn(rpg_battle_t *b, rpg_hero_t *h, int cmd, int arg, rpg_events_t *out) {
    out->count = 0;
    if (b->state != RPG_ONGOING) return false;
    const rpg_monster_t *m = &rpg_monsters[b->monster];
    char a[48], s[48];
    int dmg;

    switch (cmd) {
    case RPG_CMD_ATTACK:
        if (rng() % 16 == 0) {
            snprintf(push(out, b, h, RPG_EV_MISS, 0)->text, RPG_TEXT, "You swing... and miss!");
        } else if (rng() % 12 == 0) {
            dmg = 2 * rpg_damage(rpg_attack(h), b->def, rng());
            hurt_monster(b, dmg);
            snprintf(a, sizeof a, "Critical hit! %d damage!", dmg);
            fit(push(out, b, h, RPG_EV_CRIT, dmg)->text, a, a);
        } else {
            dmg = rpg_damage(rpg_attack(h), b->def, rng());
            hurt_monster(b, dmg);
            snprintf(a, sizeof a, "You attack! %d damage", dmg);
            fit(push(out, b, h, RPG_EV_SLASH, dmg)->text, a, a);
        }
        break;
    case RPG_CMD_MAGIC:
        if (!rpg_can_cast(h, arg)) return false;
        h->mp = (uint16_t)(h->mp - rpg_spell_cost(arg));
        if (arg == RPG_SPELL_HEAL) {
            int healed = heal_hp(h, 15 + 5 * h->level);
            snprintf(push(out, b, h, RPG_EV_HEAL, healed)->text, RPG_TEXT, "Heal! You recover %d HP", healed);
        } else {
            int lv = h->level;
            dmg = arg == RPG_SPELL_FIRE ? 8 + 2 * lv + (int)(rng() % (uint32_t)(lv + 2))
                                        : 20 + 4 * lv + (int)(rng() % (uint32_t)(2 * lv + 1));
            hurt_monster(b, dmg);
            snprintf(a, sizeof a, "%s! %s takes %d", rpg_spell_names[arg], m->name, dmg);
            snprintf(s, sizeof s, "%s! %d damage", rpg_spell_names[arg], dmg);
            fit(push(out, b, h, RPG_EV_FIRE, dmg)->text, a, s);
        }
        break;
    case RPG_CMD_ITEM:
        if (arg == RPG_ITEM_POTION) {
            if (!h->potions) return false;
            int healed = rpg_use_potion(h);
            snprintf(push(out, b, h, RPG_EV_HEAL, healed)->text, RPG_TEXT, "Potion! You recover %d HP", healed);
        } else if (arg == RPG_ITEM_ETHER) {
            if (!h->ethers) return false;
            int got = rpg_use_ether(h);
            snprintf(push(out, b, h, RPG_EV_MANA, got)->text, RPG_TEXT, "Ether! You recover %d MP", got);
        } else {
            return false;
        }
        break;
    case RPG_CMD_RUN:
        if (m->boss) return false;
        if (rng() % 4 != 0) {
            b->state = RPG_FLED;
            snprintf(push(out, b, h, RPG_EV_FLED, 0)->text, RPG_TEXT, "You run back to camp!");
            return true;
        }
        snprintf(push(out, b, h, RPG_EV_TEXT, 0)->text, RPG_TEXT, "You couldn't get away!");
        break;
    default:
        return false;
    }

    if (b->hp <= 0) {
        win(b, h, out);
        return true;
    }
    monster_turn(b, h, out);
    b->turn++;
    if (h->hp == 0) lose(b, h, out);
    return true;
}

// ---- attract mode ----

void rpg_demo_hero(rpg_hero_t *h, int which) {
    rpg_new_game(h);
    if (which == RPG_DEMO_DRAGON) {
        // At the dragon's door, well equipped, a level up away.
        h->zone = 2, h->battle = (uint8_t)(rpg_zones[2].battles - 1);
        h->level = 20, h->sword = 4, h->armor = 3;
        h->hp = (uint16_t)rpg_max_hp(20), h->mp = (uint16_t)rpg_max_mp(20);
        h->xp = rpg_xp_for_level(21) - 150;
        h->potions = 3, h->ethers = 2, h->gold = 1460;
        h->wins = 38, h->defeats = 2, h->play_s = 2 * 3600 + 14 * 60 + 9;
    } else {
        // Facing the Horn Beast a little worn from the walk, 12 XP from Lv 8.
        h->zone = 0, h->battle = (uint8_t)(rpg_zones[0].battles - 1);
        h->level = 7, h->sword = 1, h->armor = 1;
        h->hp = 36, h->mp = (uint16_t)rpg_max_mp(7);
        h->xp = rpg_xp_for_level(8) - 12;
        h->potions = 2, h->ethers = 1, h->gold = 96;
        h->wins = 9, h->play_s = 27 * 60 + 40;
    }
}

// The most the monster can do to the hero on its next turn.
static int worst_blow(const rpg_battle_t *b, const rpg_hero_t *h) {
    int special = rpg_monsters[b->monster].special, def = rpg_defense(h);
    int worst = rpg_damage_max(b->atk, def);
    if (special == RPG_SP_BREATH && b->turn % 3 == 2) worst = rpg_damage_max(b->atk * 5 / 4, def / 2);
    else if (special == RPG_SP_SPELL) worst = rpg_damage_max(b->atk, def / 2);
    return worst;
}

int rpg_demo_command(const rpg_battle_t *b, const rpg_hero_t *h, rpg_demo_t *d, int *arg) {
    int lv = h->level, max_hp = rpg_max_hp(lv);
    int fire_min = 8 + 2 * lv, blaze_min = 20 + 4 * lv;
    int worst = worst_blow(b, h);
    bool danger = h->hp < max_hp && (h->hp <= worst * 3 / 2 || h->hp * 3 < max_hp);
    int cmd = RPG_CMD_ATTACK;
    *arg = 0;
    if (danger && rpg_can_cast(h, RPG_SPELL_FIRE) && b->hp <= fire_min) {
        cmd = RPG_CMD_MAGIC, *arg = RPG_SPELL_FIRE; // a sure finishing blow beats healing
    } else if (danger && rpg_can_cast(h, RPG_SPELL_BLAZE) && b->hp <= blaze_min) {
        cmd = RPG_CMD_MAGIC, *arg = RPG_SPELL_BLAZE;
    } else if (danger && (h->potions || rpg_can_cast(h, RPG_SPELL_HEAL))) {
        // Heal, or a potion when the hero is nearly down; then the other of
        // the two next time, so both get seen.
        bool potion = h->potions && (h->hp * 4 < max_hp || d->heals % 2 == 1 || !rpg_can_cast(h, RPG_SPELL_HEAL));
        if (potion) cmd = RPG_CMD_ITEM, *arg = RPG_ITEM_POTION;
        else cmd = RPG_CMD_MAGIC, *arg = RPG_SPELL_HEAL;
        d->heals++;
    } else if (d->round > 0 && d->last == RPG_CMD_ATTACK) {
        if (rpg_can_cast(h, RPG_SPELL_BLAZE)) cmd = RPG_CMD_MAGIC, *arg = RPG_SPELL_BLAZE;
        else if (rpg_can_cast(h, RPG_SPELL_FIRE)) cmd = RPG_CMD_MAGIC, *arg = RPG_SPELL_FIRE;
    }
    // A heal does not count as the round's move: the next one still alternates.
    if (!(cmd == RPG_CMD_ITEM || (cmd == RPG_CMD_MAGIC && *arg == RPG_SPELL_HEAL))) d->last = (uint8_t)cmd;
    d->round++;
    return cmd;
}

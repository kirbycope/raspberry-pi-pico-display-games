// Pico Quest's rules, played without drawing anything.
#include <string.h>

#include "apps/rpg.h"
#include "core/app.h"
#include "core/hal.h"
#include "core/save.h"
#include "test.h"

static uint32_t seed;
static uint32_t fixed_random(void) {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
}
static void use_seed(uint32_t s) {
    seed = s;
    rpg_set_random(fixed_random);
}

TEST(rpg_damage_stays_within_bounds_and_above_zero) {
    const int atks[] = {1, 5, 9, 20, 58, 120};
    const int defs[] = {0, 2, 10, 40, 300};
    for (int a = 0; a < 6; a++)
        for (int d = 0; d < 5; d++) {
            int lo = rpg_damage_min(atks[a], defs[d]), hi = rpg_damage_max(atks[a], defs[d]);
            CHECK(lo >= 1);
            CHECK(lo <= hi);
            for (uint32_t r = 0; r < 200; r++) {
                int dmg = rpg_damage(atks[a], defs[d], r * 2654435761u);
                CHECK(dmg >= 1);
                CHECK(dmg >= lo && dmg <= hi);
            }
        }
    // base = 20 - 4 / 2 = 18, so 7/8 to 9/8 of 18.
    CHECK_EQ(rpg_damage_min(20, 4), 15);
    CHECK_EQ(rpg_damage_max(20, 4), 20);
    // Hopeless attacks still scratch.
    CHECK_EQ(rpg_damage(1, 300, 0), 1);
}

TEST(rpg_xp_thresholds_level_up_and_raise_stats) {
    rpg_hero_t h;
    rpg_new_game(&h);
    CHECK_EQ(h.level, 1);
    CHECK_EQ(rpg_xp_for_level(1), 0);
    CHECK(rpg_xp_for_level(3) > rpg_xp_for_level(2));
    int hp = rpg_max_hp(1), mp = rpg_max_mp(1), atk = rpg_attack(&h), def = rpg_defense(&h);
    CHECK_EQ(rpg_gain_xp(&h, rpg_xp_for_level(2) - 1), 0);
    CHECK_EQ(h.level, 1);
    CHECK_EQ(rpg_gain_xp(&h, 1), 1);
    CHECK_EQ(h.level, 2);
    CHECK(rpg_max_hp(2) > hp);
    CHECK(rpg_max_mp(2) > mp);
    CHECK(rpg_attack(&h) > atk);
    CHECK(rpg_defense(&h) > def);
    // The gain in maximum HP is added to the current HP too.
    CHECK_EQ(h.hp, rpg_max_hp(2));
    // A big haul can give several levels at once.
    CHECK(rpg_gain_xp(&h, rpg_xp_for_level(6)) >= 3);
    CHECK_EQ(h.level, 6);
}

TEST(rpg_potions_cap_at_max_hp) {
    rpg_hero_t h;
    rpg_new_game(&h);
    h.potions = 3;
    h.hp = (uint16_t)(rpg_max_hp(1) - 4);
    CHECK_EQ(rpg_use_potion(&h), 4);
    CHECK_EQ(h.hp, rpg_max_hp(1));
    CHECK_EQ(h.potions, 2);
    h.hp = 1;
    CHECK_EQ(rpg_use_potion(&h), rpg_max_hp(1) - 1); // capped
    CHECK_EQ(h.hp, rpg_max_hp(1));
    // At a high level a potion restores half the maximum.
    h.level = 20;
    h.hp = 1;
    CHECK_EQ(rpg_use_potion(&h), rpg_max_hp(20) / 2);
    h.potions = 0;
    h.hp = 1;
    CHECK_EQ(rpg_use_potion(&h), 0);
    CHECK_EQ(h.hp, 1);
    // Ethers cap at max MP the same way.
    h.ethers = 1;
    h.mp = (uint16_t)(rpg_max_mp(h.level) - 2);
    CHECK_EQ(rpg_use_ether(&h), 2);
    CHECK_EQ(h.mp, rpg_max_mp(h.level));
}

TEST(rpg_magic_needs_mp_and_turns_are_refused_without_it) {
    rpg_hero_t h;
    rpg_battle_t b;
    rpg_events_t ev;
    use_seed(1);
    rpg_new_game(&h);
    rpg_battle_start(&b, &h, 0);
    h.mp = (uint16_t)(rpg_spell_cost(RPG_SPELL_FIRE) - 1);
    CHECK(!rpg_can_cast(&h, RPG_SPELL_FIRE));
    CHECK(!rpg_battle_turn(&b, &h, RPG_CMD_MAGIC, RPG_SPELL_FIRE, &ev));
    CHECK_EQ(ev.count, 0);
    CHECK_EQ(b.hp, b.max_hp);
    // Blaze is not known at level 1, whatever the MP.
    h.mp = 99;
    CHECK(!rpg_can_cast(&h, RPG_SPELL_BLAZE));
    CHECK(rpg_battle_turn(&b, &h, RPG_CMD_MAGIC, RPG_SPELL_FIRE, &ev));
    CHECK_EQ(h.mp, 99 - rpg_spell_cost(RPG_SPELL_FIRE));
    CHECK(b.hp < b.max_hp);
    // No potions, no item turn.
    h.potions = 0;
    CHECK(!rpg_battle_turn(&b, &h, RPG_CMD_ITEM, RPG_ITEM_POTION, &ev));
    rpg_set_random(NULL);
}

TEST(rpg_scripted_battle_is_won_and_pays_out) {
    rpg_hero_t h;
    rpg_battle_t b;
    rpg_events_t ev;
    use_seed(7);
    rpg_new_game(&h);
    rpg_battle_start(&b, &h, 0); // the Shellbug, the weakest monster
    uint32_t gold = h.gold;
    int turns = 0;
    while (b.state == RPG_ONGOING && turns < 50) {
        CHECK(rpg_battle_turn(&b, &h, RPG_CMD_ATTACK, 0, &ev));
        CHECK(ev.count > 0);
        turns++;
    }
    CHECK_EQ(b.state, RPG_WON);
    CHECK_EQ(h.xp, rpg_monsters[0].xp);
    CHECK_EQ(h.gold, gold + rpg_monsters[0].gold);
    CHECK_EQ(h.wins, 1);
    CHECK_EQ(h.battle, 1);
    CHECK_EQ(ev.ev[ev.count - 1].kind == RPG_EV_REWARD || ev.ev[ev.count - 1].kind == RPG_EV_LEVEL, 1);
    // A finished battle takes no more turns.
    CHECK(!rpg_battle_turn(&b, &h, RPG_CMD_ATTACK, 0, &ev));
    rpg_set_random(NULL);
}

TEST(rpg_defeat_halves_gold_heals_and_steps_back) {
    rpg_hero_t h;
    rpg_battle_t b;
    rpg_events_t ev;
    use_seed(3);
    rpg_new_game(&h);
    h.zone = 2, h.battle = 5, h.gold = 101;
    rpg_battle_start(&b, &h, rpg_zones[2].guardian);
    int turns = 0;
    while (b.state == RPG_ONGOING && turns < 50) {
        rpg_battle_turn(&b, &h, RPG_CMD_ATTACK, 0, &ev);
        turns++;
    }
    CHECK_EQ(b.state, RPG_LOST);
    CHECK_EQ(h.gold, 50);
    CHECK_EQ(h.hp, rpg_max_hp(1));
    CHECK_EQ(h.battle, 4);
    CHECK_EQ(h.defeats, 1);
    rpg_set_random(NULL);
}

TEST(rpg_bosses_cannot_be_fled) {
    rpg_hero_t h;
    rpg_battle_t b;
    rpg_events_t ev;
    rpg_new_game(&h);
    rpg_battle_start(&b, &h, rpg_zones[RPG_ZONES - 1].guardian);
    CHECK(rpg_monsters[b.monster].boss);
    CHECK(!rpg_battle_turn(&b, &h, RPG_CMD_RUN, 0, &ev));
    CHECK_EQ(b.state, RPG_ONGOING);
}

TEST(rpg_save_struct_fits_and_round_trips) {
    CHECK(sizeof(rpg_hero_t) <= SAVE_RPG_BYTES);
    rpg_hero_t h, back;
    rpg_new_game(&h);
    CHECK(rpg_save_valid(&h));
    h.level = 7, h.gold = 1234, h.zone = 1, h.battle = 3;
    memcpy(g_save.rpg, &h, sizeof h);
    memcpy(&back, g_save.rpg, sizeof back);
    CHECK(memcmp(&h, &back, sizeof h) == 0);
    // Garbage or a battle past the zone's end is not a save.
    memset(&back, 0xA5, sizeof back);
    CHECK(!rpg_save_valid(&back));
    h.battle = rpg_zones[1].battles;
    CHECK(!rpg_save_valid(&h));
}

TEST(rpg_zone_progression_reaches_the_dragon) {
    rpg_hero_t h;
    rpg_battle_t b;
    rpg_events_t ev;
    use_seed(11);
    rpg_new_game(&h);
    h.level = 30; // strong enough to sweep through
    h.hp = (uint16_t)rpg_max_hp(30), h.mp = (uint16_t)rpg_max_mp(30);
    int battles = 0, seen_zone[RPG_ZONES] = {0};
    bool fought_dragon = false;
    while (!rpg_cleared(&h) && battles < 100) {
        seen_zone[h.zone]++;
        int m = rpg_pick_monster(&h, fixed_random());
        CHECK(m >= rpg_zones[h.zone].first || m == rpg_zones[h.zone].guardian);
        if (h.battle == rpg_zones[h.zone].battles - 1) CHECK_EQ(m, rpg_zones[h.zone].guardian);
        if (m == rpg_zones[RPG_ZONES - 1].guardian) fought_dragon = true;
        rpg_battle_start(&b, &h, m);
        while (b.state == RPG_ONGOING) {
            rpg_rest(&h);
            h.gold += 100;
            rpg_battle_turn(&b, &h, RPG_CMD_ATTACK, 0, &ev);
        }
        CHECK_EQ(b.state, RPG_WON);
        battles++;
    }
    CHECK(fought_dragon);
    CHECK(rpg_cleared(&h));
    CHECK(rpg_save_valid(&h));
    int total = 0;
    for (int z = 0; z < RPG_ZONES; z++) {
        CHECK_EQ(seen_zone[z], rpg_zones[z].battles);
        total += rpg_zones[z].battles;
    }
    CHECK_EQ(battles, total);
    // New Game+ starts over with the hero's level and gold kept.
    uint32_t gold = h.gold;
    rpg_new_game_plus(&h);
    CHECK_EQ(h.zone, 0);
    CHECK_EQ(h.level, 30);
    CHECK_EQ(h.gold, gold);
    CHECK_EQ(h.ng, 1);
    rpg_set_random(NULL);
}

TEST(rpg_shop_and_rest_spend_gold) {
    rpg_hero_t h;
    rpg_new_game(&h);
    h.gold = (uint32_t)RPG_POTION_PRICE;
    CHECK(rpg_buy(&h, RPG_SHOP_POTION));
    CHECK_EQ(h.gold, 0);
    CHECK_EQ(h.potions, 3);
    CHECK(!rpg_buy(&h, RPG_SHOP_ETHER));
    h.gold = 10000;
    int atk = rpg_attack(&h);
    CHECK(rpg_buy(&h, RPG_SHOP_SWORD));
    CHECK(rpg_attack(&h) > atk);
    for (int i = 0; i < 10; i++) rpg_buy(&h, RPG_SHOP_SWORD);
    CHECK_EQ(h.sword, RPG_UPGRADES);
    h.hp = 1;
    uint32_t before = h.gold;
    CHECK(rpg_rest(&h));
    CHECK_EQ(h.hp, rpg_max_hp(h.level));
    CHECK_EQ(h.gold, before - (uint32_t)rpg_rest_price(&h));
}

// ---- attract mode ----

TEST(rpg_attract_demo_heroes_win_their_boss_fights) {
    // The demo's own choices, played as rules only, over many seeds: the boss
    // always falls, in a few rounds rather than at a stroke, and the hero
    // levels up.
    for (int which = 0; which < RPG_DEMOS; which++)
        for (uint32_t s = 1; s <= 200; s++) {
            rpg_hero_t h;
            rpg_battle_t b;
            rpg_events_t ev;
            rpg_demo_t d = {0};
            use_seed(s * 7919u);
            rpg_demo_hero(&h, which);
            CHECK(rpg_save_valid(&h));
            int level = h.level;
            bool magic = false;
            rpg_battle_start(&b, &h, rpg_pick_monster(&h, fixed_random()));
            CHECK(rpg_monsters[b.monster].boss);
            while (b.state == RPG_ONGOING && d.round < 20) {
                int arg, cmd = rpg_demo_command(&b, &h, &d, &arg);
                if (cmd == RPG_CMD_MAGIC && arg != RPG_SPELL_HEAL) magic = true;
                CHECK(rpg_battle_turn(&b, &h, cmd, arg, &ev)); // never a refused command
            }
            CHECK_EQ(b.state, RPG_WON);
            CHECK(d.round >= 2 && d.round <= 10);
            CHECK(magic);
            CHECK_EQ(h.level, level + 1);
        }
    rpg_set_random(NULL);
}

// Runs Pico Quest in attract mode the way main.c does: autopilot's held
// buttons turned into pressed and released edges, fed to frame().
typedef struct {
    int wins, losses, level_ups;
    bool a_with_b;
} attract_tally_t;

static attract_tally_t attract_run(uint32_t ms) {
    attract_tally_t t = {0};
    uint8_t prev = 0;
    const rpg_hero_t *h = rpg_current_hero();
    uint32_t last_wins = h->wins, last_defeats = h->defeats, last_level = h->level;
    for (uint32_t elapsed = 0; elapsed < ms; elapsed += 33) {
        uint8_t held = app_rpg.autopilot(33);
        if ((held & (BTN_A | BTN_B)) == (BTN_A | BTN_B)) t.a_with_b = true;
        input_t in = {held, (uint8_t)(held & ~prev), (uint8_t)(prev & ~held)};
        prev = held;
        CHECK(app_rpg.frame(&in, 33));
        // A new demo hero jumps these about; only a step of one is a battle.
        if (h->wins == last_wins + 1) t.wins++;
        if (h->defeats == last_defeats + 1) t.losses++;
        if (h->level == last_level + 1) t.level_ups++;
        last_wins = h->wins, last_defeats = h->defeats, last_level = h->level;
    }
    return t;
}

TEST(rpg_attract_autopilot_wins_and_never_presses_a_with_b) {
    gfx_set_orientation(app_rpg.orient);
    CHECK(app_rpg.attract != NULL && app_rpg.autopilot != NULL);
    for (uint32_t s = 1; s <= 6; s++) {
        // Each turn plays the next demo, so both are covered three times.
        app_rpg.enter();
        app_rpg.attract();
        use_seed(s * 104729u);
        attract_tally_t t = attract_run(30000);
        CHECK(t.wins >= 1);
        CHECK_EQ(t.losses, 0);
        CHECK(t.level_ups >= 1);
        CHECK(!t.a_with_b);
        app_rpg.leave();
    }
    rpg_set_random(NULL);
}

TEST(rpg_attract_leaves_the_real_save_and_hero_alone) {
    gfx_set_orientation(app_rpg.orient);
    save_t real_save = g_save;
    rpg_hero_t real;
    rpg_new_game(&real);
    real.level = 3, real.zone = 1, real.battle = 4, real.gold = 77, real.potions = 1;
    memset(g_save.rpg, 0, sizeof g_save.rpg);
    memcpy(g_save.rpg, &real, sizeof real);
    uint8_t before[SAVE_RPG_BYTES];
    memcpy(before, g_save.rpg, sizeof before);

    // An attract turn long enough to win, camp and fight again.
    app_rpg.enter();
    app_rpg.attract();
    CHECK(rpg_current_hero()->level != real.level);
    CHECK(attract_run(40000).wins >= 1);
    app_rpg.leave();
    // The demo wrote nothing, even before main.c puts the save back.
    CHECK(memcmp(g_save.rpg, before, sizeof before) == 0);

    // Opening the game normally afterwards brings back the player's hero.
    app_rpg.enter();
    CHECK(memcmp(rpg_current_hero(), &real, sizeof real) == 0);
    input_t none = {0, 0, 0}, a = {BTN_A, BTN_A, 0};
    CHECK(app_rpg.frame(&none, 33));
    CHECK(app_rpg.frame(&a, 33)); // Continue
    for (int i = 0; i < 10; i++) CHECK(app_rpg.frame(&none, 33));
    app_rpg.leave();
    rpg_hero_t back;
    memcpy(&back, g_save.rpg, sizeof back);
    CHECK_EQ(back.level, 3);
    CHECK_EQ(back.zone, 1);
    CHECK_EQ(back.battle, 4);
    CHECK_EQ(back.gold, 77);
    g_save = real_save;
}

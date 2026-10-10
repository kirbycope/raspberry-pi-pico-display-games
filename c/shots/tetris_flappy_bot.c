// Writes shots/tetris.txt and shots/flappy.txt: it runs the real app frame by
// frame with a bot at the controls (Tetris: a placement search that saves the
// right column for a four-line clear; Flappy: aim for the middle of the next
// gap) and records the bot's taps as a sim script, with shots at good moments.
// Not part of the build. From c/:
//   cc -DPDG_SIM -DPLAN_TETRIS -Isrc -o /tmp/bot shots/tetris_flappy_bot.c \
//      src/core/gfx.c src/core/save.c src/assets/assets.c src/platform/hal_sim.c
//   /tmp/bot shots/tetris.txt      (and -DPLAN_FLAPPY for shots/flappy.txt)
// then replay with PDG_SEED=7, as the scripts' first line says. The timing
// model (script read from t=0, one frame up after each tap) follows hal_sim.c.
#include <stdlib.h>
#include <string.h>
#ifdef PLAN_TETRIS
#include "apps/tetris.c"
#define APP app_tetris
#else
#include "apps/flappy.c"
#define APP app_flappy
#endif

static FILE *out;
static int cursor = 0, last_tap = -10; // the sim reads the script from t=0

static void emit_wait_to(int boundary) {
    if (boundary > cursor) fprintf(out, "wait %d\n", 16 * (boundary - cursor));
    cursor = boundary;
}
static bool can_tap(int k) { return k >= last_tap + 2 && k >= cursor; }
static void emit_tap(int k, char b) {
    emit_wait_to(k);
    fprintf(out, "tap %c 16\n", b);
    cursor = k + 2; // the sim reads again a frame after the release
    last_tap = k;
}
static void emit_shot(int k, const char *name) {
    emit_wait_to(k + 1);
    fprintf(out, "shot shots/%s.bmp\n", name);
}

#ifdef PLAN_TETRIS
// ---- Tetris bot
static double evaluate(const tetris_t *t, int lines) {
    int heights[TET_W], holes = 0, agg = 0, bump = 0, well = 0;
    for (int c = 0; c < TET_W; c++) {
        heights[c] = 0;
        for (int r = 0; r < TET_H; r++)
            if (t->board[r][c]) { heights[c] = TET_H - r; break; }
        for (int r = TET_H - heights[c] + 1; r < TET_H; r++)
            if (!t->board[r][c]) holes++;
        agg += heights[c];
        if (c) bump += abs(heights[c] - heights[c - 1]);
    }
    well = heights[TET_W - 1];
    double s = -0.51 * agg - 0.36 * 4 * holes - 0.18 * bump;
    s += lines == 4 ? 20 : lines * -1.0;
    s -= well * 1.5;
    return s;
}

static int plan_rot, plan_x, plan_state; // 0 idle, 1 executing
static bool want_plan = true;

static void make_plan(void) {
    double best = -1e9;
    plan_rot = 0;
    plan_x = game.x;
    for (int r = 0; r < 4; r++)
        for (int x = -2; x < TET_W; x++) {
            tetris_t t = game;
            bool ok = true;
            for (int i = 0; i < r && ok; i++) ok = tetris_rotate(&t);
            if (!ok) continue;
            while (ok && t.x != x) ok = tetris_move(&t, x > t.x ? 1 : -1, 0);
            if (!ok) continue;
            tetris_hard_drop(&t);
            int lines = tetris_lock(&t);
            if (t.over) continue;
            tetris_clear_lines(&t);
            double s = evaluate(&t, lines);
            if (s > best) { best = s; plan_rot = r; plan_x = x; }
        }
}

static int pieces;
static int mode; // 0 play well, 1 dump to lose
static char bot(int k) {
    if (state == ST_TITLE) return k > 60 && can_tap(k) ? 'A' : 0;
    if (state != ST_PLAY) { want_plan = true; return 0; }
    if (!can_tap(k)) return 0;
    if (mode == 1) return 'B';
    if (want_plan) { make_plan(); want_plan = false; }
    if (plan_rot > 0) { plan_rot--; return 'A'; }
    if (game.x < plan_x) return 'X';
    if (game.x > plan_x) return 'Y';
    want_plan = true;
    pieces++;
    return 'B';
}
#else
// ---- Flappy bot
static int crash_at = 41;
static char bot(int k) {
    if (!can_tap(k)) return 0;
    if (state == ST_READY) return k > 50 ? 'A' : 0;
    if (state != ST_PLAY) return 0;
    if ((int)game.score >= crash_at) return 0; // stop flapping
    int best = -1;
    for (int i = 0; i < FLAP_PIPES; i++) {
        int px = game.pipe[i].x >> 8;
        if (px + FLAP_PIPE_W + 2 < FLAP_BIRD_X - FLAP_BIRD_HW) continue;
        if (best < 0 || game.pipe[i].x < game.pipe[best].x) best = i;
    }
    int target = best >= 0 ? game.pipe[best].gap_y + FLAP_GAP / 2 + 6 : 110;
    int y = game.y >> 8, vy = game.vy / 256;
    if (y > target && vy > -40) return 'A';
    return 0;
}
#endif

int main(int argc, char **argv) {
    setenv("PDG_SCRIPT", "/dev/null", 1);
    setenv("PDG_SEED", "7", 1);
    hal_init();
    out = fopen(argv[1], "w");
    gfx_set_orientation(ORIENT_PORTRAIT);
    APP.enter();
    uint8_t prev = 0;
    int shots = 0;
    for (int k = 0; k < 60 * 400; k++) {
        char b = bot(k);
        uint8_t held = b == 'A' ? BTN_A : b == 'B' ? BTN_B : b == 'X' ? BTN_X : b == 'Y' ? BTN_Y : 0;
        if (b) emit_tap(k, b);
        input_t in = {held, (uint8_t)(held & ~prev), (uint8_t)(prev & ~held)};
        prev = held;
        APP.frame(&in, k == 0 ? 0 : 16);
#ifdef PLAN_TETRIS
        if (k == 50) emit_shot(k, "tetris_title");
        static int clears[5];
        if (state == ST_CLEAR && state_ms == 0) clears[clear_count]++;
        if (shots == 0 && pieces >= 30 && state == ST_PLAY && game.y >= 2 && game.y <= 3 && trail.ms == 0) { emit_shot(k, "tetris_play"); shots++; }
        if (shots == 1 && state == ST_CLEAR && clear_count == 4 && state_ms >= 240 && state_ms < 256) { emit_shot(k, "tetris_clear"); shots++; }
        if (shots == 1 && pieces > 400) shots++;
        if (shots == 2 && state == ST_PLAY) mode = 1;
        if (shots == 2 && state == ST_OVER && state_ms >= 1600) { emit_shot(k, "tetris_over"); shots++; fprintf(stderr, "score %u lines %u pieces %d clears %d %d %d %d\n", game.score, game.lines, pieces, clears[1], clears[2], clears[3], clears[4]); break; }
        if (k == 60 * 400 - 1) fprintf(stderr, "ran out: shots %d pieces %d state %d score %u\n", shots, pieces, state, game.score);
#else
        bool near = false;
        for (int i = 0; i < FLAP_PIPES; i++) near |= (game.pipe[i].x >> 8) >= 60 && (game.pipe[i].x >> 8) < 62;
        if (k == 40) emit_shot(k, "flappy_title");
        if (shots == 0 && state == ST_PLAY && game.score == 6 && near) { emit_shot(k, "flappy_play"); shots++; }
        if (shots == 1 && state == ST_PLAY && game.score == 23 && game.vy < -100 * 256) { emit_shot(k, "flappy_play2"); shots++; }
        if (shots == 2 && state == ST_OVER && state_ms >= 1200) { emit_shot(k, "flappy_over"); shots++; fprintf(stderr, "score %u\n", game.score); break; }
#endif
    }
    fprintf(out, "quit\n");
    fclose(out);
    return 0;
}

// Tetris on a 10x20 well of 11 pixel blocks, with the score along the top
// and the next three pieces, the lines and the level down the right.
// 7-bag pieces, SRS rotation with wall kicks, a ghost piece, and gravity that
// speeds up every ten lines. The best score is kept across power cycles.
//
// A (top right): rotate clockwise   B (top left): hard drop
// X (bottom right): move right      Y (bottom left): move left
// Holding X or Y keeps moving after a short delay. Any button starts.
#include "tetris.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../core/app.h"
#include "../core/hal.h"
#include "../core/save.h"

// ---------------------------------------------------------------- rules

// State 0 of each piece in its rotation box; the box is 4 wide for I and O,
// 3 for the rest. Clockwise turns rotate the box.
static const int8_t SHAPES[TET_PIECES][4][2] = {
    [TET_I] = {{0, 1}, {1, 1}, {2, 1}, {3, 1}},
    [TET_O] = {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
    [TET_T] = {{1, 0}, {0, 1}, {1, 1}, {2, 1}},
    [TET_S] = {{1, 0}, {2, 0}, {0, 1}, {1, 1}},
    [TET_Z] = {{0, 0}, {1, 0}, {1, 1}, {2, 1}},
    [TET_J] = {{0, 0}, {0, 1}, {1, 1}, {2, 1}},
    [TET_L] = {{2, 0}, {0, 1}, {1, 1}, {2, 1}},
};

// SRS kicks for a clockwise turn out of each state, with y pointing down.
static const int8_t KICKS_JLSTZ[4][5][2] = {
    {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}},
    {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}},
    {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}},
    {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}},
};
static const int8_t KICKS_I[4][5][2] = {
    {{0, 0}, {-2, 0}, {1, 0}, {-2, 1}, {1, -2}},
    {{0, 0}, {-1, 0}, {2, 0}, {-1, -2}, {2, 1}},
    {{0, 0}, {2, 0}, {-1, 0}, {2, -1}, {-1, 2}},
    {{0, 0}, {1, 0}, {-2, 0}, {1, 2}, {-2, -1}},
};

// Guideline gravity, (0.8 - (level - 1) * 0.007) ^ (level - 1) seconds a row.
static const uint16_t GRAVITY_MS[] = {1000, 793, 618, 473, 355, 262, 190, 135, 94, 64, 43, 28, 18, 11, 7};

void tetris_cells(int type, int rot, int8_t out[4][2]) {
    int n = (type == TET_I || type == TET_O) ? 4 : 3;
    for (int i = 0; i < 4; i++) {
        int x = SHAPES[type][i][0], y = SHAPES[type][i][1];
        if (type != TET_O)
            for (int r = 0; r < (rot & 3); r++) {
                int nx = n - 1 - y;
                y = x;
                x = nx;
            }
        out[i][0] = (int8_t)x;
        out[i][1] = (int8_t)y;
    }
}

bool tetris_fits(const tetris_t *t, int type, int rot, int x, int y) {
    int8_t c[4][2];
    tetris_cells(type, rot, c);
    for (int i = 0; i < 4; i++) {
        int cx = x + c[i][0], cy = y + c[i][1];
        if (cx < 0 || cx >= TET_W || cy >= TET_H) return false;
        if (cy >= 0 && t->board[cy][cx]) return false;
    }
    return true;
}

int tetris_bag_draw(tetris_t *t) {
    if (t->bag_left <= 0) {
        for (int i = 0; i < TET_PIECES; i++) t->bag[i] = (uint8_t)i;
        for (int i = TET_PIECES - 1; i > 0; i--) {
            int j = (int)(hal_random() % (uint32_t)(i + 1));
            uint8_t tmp = t->bag[i];
            t->bag[i] = t->bag[j];
            t->bag[j] = tmp;
        }
        t->bag_left = TET_PIECES;
    }
    return t->bag[--t->bag_left];
}

bool tetris_spawn(tetris_t *t) {
    t->type = t->next[0];
    t->next[0] = t->next[1];
    t->next[1] = t->next[2];
    t->next[2] = tetris_bag_draw(t);
    t->rot = 0;
    t->x = 3;
    t->y = t->type == TET_I ? -1 : 0;
    if (!tetris_fits(t, t->type, t->rot, t->x, t->y)) {
        t->over = true;
        return false;
    }
    return true;
}

void tetris_reset(tetris_t *t) {
    memset(t, 0, sizeof *t);
    t->level = 1;
    for (int i = 0; i < 3; i++) t->next[i] = tetris_bag_draw(t);
    tetris_spawn(t);
}

bool tetris_move(tetris_t *t, int dx, int dy) {
    if (!tetris_fits(t, t->type, t->rot, t->x + dx, t->y + dy)) return false;
    t->x += dx;
    t->y += dy;
    return true;
}

bool tetris_rotate(tetris_t *t) {
    if (t->type == TET_O) return true;
    const int8_t(*kicks)[2] = t->type == TET_I ? KICKS_I[t->rot & 3] : KICKS_JLSTZ[t->rot & 3];
    int to = (t->rot + 1) & 3;
    for (int k = 0; k < 5; k++) {
        int nx = t->x + kicks[k][0], ny = t->y + kicks[k][1];
        if (tetris_fits(t, t->type, to, nx, ny)) {
            t->rot = to;
            t->x = nx;
            t->y = ny;
            return true;
        }
    }
    return false;
}

int tetris_drop_y(const tetris_t *t) {
    int y = t->y;
    while (tetris_fits(t, t->type, t->rot, t->x, y + 1)) y++;
    return y;
}

int tetris_hard_drop(tetris_t *t) {
    int rows = tetris_drop_y(t) - t->y;
    t->y += rows;
    t->score += 2u * (uint32_t)rows;
    return rows;
}

uint32_t tetris_full_rows(const tetris_t *t) {
    uint32_t mask = 0;
    for (int r = 0; r < TET_H; r++) {
        bool full = true;
        for (int c = 0; c < TET_W && full; c++) full = t->board[r][c] != 0;
        if (full) mask |= 1u << r;
    }
    return mask;
}

int tetris_lock(tetris_t *t) {
    int8_t c[4][2];
    tetris_cells(t->type, t->rot, c);
    bool any_visible = false;
    for (int i = 0; i < 4; i++) {
        int cx = t->x + c[i][0], cy = t->y + c[i][1];
        if (cy < 0) continue;
        t->board[cy][cx] = (uint8_t)(t->type + 1);
        any_visible = true;
    }
    if (!any_visible) t->over = true;
    int n = 0;
    for (uint32_t m = tetris_full_rows(t); m; m &= m - 1) n++;
    return n;
}

uint32_t tetris_line_score(int lines, uint32_t level) {
    static const uint16_t base[5] = {0, 100, 300, 500, 800};
    return (lines < 0 || lines > 4 ? 0 : base[lines]) * level;
}

int tetris_clear_lines(tetris_t *t) {
    int n = 0;
    for (int r = TET_H - 1; r >= 0;) {
        bool full = true;
        for (int c = 0; c < TET_W && full; c++) full = t->board[r][c] != 0;
        if (!full) { r--; continue; }
        // Shift everything above down by one and test this row again.
        memmove(&t->board[1], &t->board[0], (size_t)r * TET_W);
        memset(&t->board[0], 0, TET_W);
        n++;
    }
    if (n) {
        t->score += tetris_line_score(n, t->level);
        t->lines += (uint32_t)n;
        t->level = (t->start_level ? t->start_level : 1) + t->lines / 10;
    }
    return n;
}

uint32_t tetris_gravity_ms(uint32_t level) {
    const uint32_t count = sizeof GRAVITY_MS / sizeof GRAVITY_MS[0];
    if (level < 1) level = 1;
    return GRAVITY_MS[level > count ? count - 1 : level - 1];
}

// ---------------------------------------------------------------- autopilot rules

// How good a board is after a placement, in hundredths. These are Pierre
// Dellacherie's features with the El-Tetris weights: how high the piece
// landed (in half rows), how many piece cells went with the cleared rows,
// how often a row or a column changes between filled and empty, holes, and
// how deep the wells are. Holes and wells weigh more than El-Tetris has
// them: a row with a hole in it never joins a four-line clear, and a second
// deep well would want a long I of its own. While the stack is low the right-hand column is
// left out of all of it and kept empty, and only a four-line clear pays, so
// the player builds up and waits for a long I. Once the stack is high
// (`danger`) the whole width counts and any line is welcome.
static int32_t plan_score(const tetris_t *t, int land2, int lines, int eroded, bool danger) {
    const int w = danger ? TET_W : TET_W - 1;
    int row_tr = 0, col_tr = 0, holes = 0, wells = 0;
    for (int r = 0; r < TET_H; r++) {
        bool prev = true; // the walls count as filled
        for (int c = 0; c < w; c++) {
            bool f = t->board[r][c] != 0;
            row_tr += f != prev;
            prev = f;
        }
        row_tr += !prev;
    }
    for (int c = 0; c < w; c++) {
        bool prev = false; // above the top is empty, below the floor is filled
        bool covered = false;
        int depth = 0;
        for (int r = 0; r < TET_H; r++) {
            bool f = t->board[r][c] != 0;
            col_tr += f != prev;
            prev = f;
            if (f) covered = true;
            else if (covered) holes++;
            // A well cell: empty, with both neighbours filled; deeper counts more.
            bool left = c == 0 || t->board[r][c - 1], right = c == w - 1 || t->board[r][c + 1];
            if (!f && left && right) wells += ++depth;
            else depth = 0;
        }
        col_tr += !prev;
    }
    int32_t s = -225 * land2 - 322 * row_tr - 935 * col_tr - 2000 * holes - 450 * wells;
    if (danger) return s + 342 * eroded;
    // Anything in the right-hand column spoils the next four-line clear, and
    // covering it over is worst of all.
    int well_cells = 0, well_holes = 0;
    for (int r = 0; r < TET_H; r++) {
        if (t->board[r][TET_W - 1]) well_cells++;
        else if (well_cells) well_holes++;
    }
    return s - 1000 * well_cells - 790 * well_holes + (lines == 4 ? 4000 : -300 * lines);
}

void tetris_plan(const tetris_t *t, int *rot, int *x) {
    *rot = t->rot;
    *x = t->x;
    // The stack is high when anything reaches above the bottom ten rows,
    // judged before the move.
    int top = TET_H;
    for (int r = 0; r < TET_H && top == TET_H; r++)
        for (int c = 0; c < TET_W; c++)
            if (t->board[r][c]) { top = r; break; }
    bool danger = TET_H - top > 10;
    int32_t best = INT32_MIN;
    tetris_t sim;
    for (int turns = 0; turns < 4; turns++) {
        if (turns && t->type == TET_O) break; // an O looks the same every way up
        for (int col = -2; col < TET_W; col++) {
            sim = *t;
            bool ok = true;
            for (int i = 0; i < turns && ok; i++) ok = tetris_rotate(&sim);
            while (ok && sim.x != col) ok = tetris_move(&sim, col > sim.x ? 1 : -1, 0);
            if (!ok) continue;
            tetris_hard_drop(&sim);
            int8_t c[4][2];
            tetris_cells(sim.type, sim.rot, c);
            int lo = 9, hi = -1;
            for (int i = 0; i < 4; i++) {
                if (c[i][1] < lo) lo = c[i][1];
                if (c[i][1] > hi) hi = c[i][1];
            }
            int land2 = 2 * TET_H - (2 * sim.y + lo + hi) - 1;
            int lines = tetris_lock(&sim);
            if (sim.over) continue;
            int eroded = 0;
            uint32_t full = tetris_full_rows(&sim);
            for (int i = 0; i < 4; i++)
                if (sim.y + c[i][1] >= 0) eroded += (int)(full >> (sim.y + c[i][1]) & 1);
            eroded *= lines;
            tetris_clear_lines(&sim);
            int32_t s = plan_score(&sim, land2, lines, eroded, danger);
            if (s > best) {
                best = s;
                *rot = (t->rot + turns) & 3;
                *x = col;
            }
        }
    }
}

// ---------------------------------------------------------------- app

#define CELL 11
#define WELL_X 1
#define WELL_Y 20
#define SIDE_X (WELL_X + TET_W * CELL + 1)
#define DAS_MS 170
#define ARR_MS 50
#define LOCK_MS 450
#define LOCK_RESETS 15
#define CLEAR_MS 360
#define RESTART_MS 900

enum { ST_TITLE, ST_PLAY, ST_CLEAR, ST_OVER };

static tetris_t game;
static int state;
static uint32_t fall_ms, lock_ms, state_ms, anim_ms;
static int lock_resets, lowest_y;
static int das_dir;
static int32_t das_ms;
static uint32_t clear_mask;
static int clear_count;
static bool new_best;
static uint32_t led_ms;
static uint8_t led_rgb[3];
static uint32_t start_level = 1; // attract mode starts higher, where it is brisk
static uint32_t piece_no;        // counts pieces in, so the autopilot sees a new one
static tetris_attract_stats_t stats;
// The streak a hard drop leaves behind.
static struct { int type, rot, x, y0, y1; uint32_t ms; } trail;

static const uint16_t FACE[TET_PIECES] = {
    [TET_I] = rgb(40, 205, 230), [TET_O] = rgb(245, 205, 40), [TET_T] = rgb(170, 80, 225),
    [TET_S] = rgb(80, 210, 80),  [TET_Z] = rgb(235, 55, 60),  [TET_J] = rgb(55, 105, 235),
    [TET_L] = rgb(245, 140, 30),
};

#define WELL_BG rgb(12, 12, 22)
#define WELL_DOT rgb(34, 36, 58)
#define PANEL_BG rgb(26, 26, 42)
#define FRAME_C rgb(88, 92, 130)
#define LABEL_C rgb(130, 134, 170)

static void led_flash(uint8_t r, uint8_t g, uint8_t b, uint32_t ms) {
    led_rgb[0] = r;
    led_rgb[1] = g;
    led_rgb[2] = b;
    led_ms = ms;
}

static void new_game(void) {
    tetris_reset(&game);
    game.start_level = game.level = start_level;
    piece_no++;
    state = ST_PLAY;
    fall_ms = lock_ms = state_ms = 0;
    lock_resets = 0;
    lowest_y = game.y;
    das_dir = 0;
    trail.ms = 0;
    new_best = false;
}

static void tetris_enter(void) {
    start_level = 1;
    state = ST_TITLE;
    state_ms = anim_ms = 0;
    led_ms = 0;
    memset(&game, 0, sizeof game);
}

// A bevelled block: light top and left edges, dark bottom and right, and a
// small shine in the top-left corner.
static void draw_block(int x, int y, int size, uint16_t face) {
    uint16_t light = rgb_lerp(face, C_WHITE, 120), dark = rgb_lerp(face, C_BLACK, 120);
    uint16_t edge = rgb_lerp(face, C_BLACK, 190);
    gfx_rect(x, y, size, size, face);
    if (size >= 8) {
        gfx_hline(x, y, size, light);
        gfx_vline(x, y, size, light);
        gfx_hline(x + 1, y + 1, size - 2, rgb_lerp(face, C_WHITE, 60));
        gfx_vline(x + 1, y + 1, size - 2, rgb_lerp(face, C_WHITE, 60));
        gfx_hline(x + 1, y + size - 2, size - 2, dark);
        gfx_vline(x + size - 2, y + 1, size - 2, dark);
        gfx_hline(x, y + size - 1, size, edge);
        gfx_vline(x + size - 1, y, size, edge);
        gfx_rect(x + 2, y + 2, 2, 2, rgb_lerp(face, C_WHITE, 200));
    } else {
        gfx_hline(x, y, size, light);
        gfx_vline(x, y, size, light);
        gfx_hline(x, y + size - 1, size, dark);
        gfx_vline(x + size - 1, y, size, dark);
    }
}

static void draw_piece(int type, int rot, int px, int py, int size, uint16_t face) {
    int8_t c[4][2];
    tetris_cells(type, rot, c);
    for (int i = 0; i < 4; i++) draw_block(px + c[i][0] * size, py + c[i][1] * size, size, face);
}

// The ghost: an outline of where the piece will land.
static void draw_ghost(void) {
    int gy = tetris_drop_y(&game);
    if (gy == game.y) return;
    int8_t c[4][2];
    tetris_cells(game.type, game.rot, c);
    uint16_t col = rgb_lerp(FACE[game.type], WELL_BG, 120);
    for (int i = 0; i < 4; i++) {
        int cy = gy + c[i][1];
        if (cy < 0) continue;
        int x = WELL_X + (game.x + c[i][0]) * CELL, y = WELL_Y + cy * CELL;
        gfx_rect(x + 1, y + 1, CELL - 2, CELL - 2, rgb_lerp(FACE[game.type], WELL_BG, 215));
        gfx_frame(x + 1, y + 1, CELL - 2, CELL - 2, col);
    }
}

static void draw_well(void) {
    gfx_rect(WELL_X, WELL_Y, TET_W * CELL, TET_H * CELL, WELL_BG);
    for (int r = 1; r < TET_H; r++)
        for (int c = 1; c < TET_W; c++) gfx_pixel(WELL_X + c * CELL, WELL_Y + r * CELL, WELL_DOT);
    gfx_vline(WELL_X - 1, WELL_Y - 1, TET_H * CELL + 1, FRAME_C);
    gfx_vline(WELL_X + TET_W * CELL, WELL_Y - 1, TET_H * CELL + 1, FRAME_C);
    gfx_hline(WELL_X - 1, WELL_Y - 1, TET_W * CELL + 2, FRAME_C);

    // Rows grey out from the bottom once the game is over.
    int grey_from = TET_H;
    if (state == ST_OVER) grey_from = TET_H - (int)(state_ms / 30);
    for (int r = 0; r < TET_H; r++) {
        bool flashing = state == ST_CLEAR && (clear_mask >> r & 1);
        for (int c = 0; c < TET_W; c++) {
            uint8_t v = game.board[r][c];
            if (!v) continue;
            uint16_t face = FACE[v - 1];
            if (r >= grey_from) face = state_ms < 600 ? rgb(92, 94, 110) : rgb(66, 68, 84);
            if (flashing) {
                // Flash, then shrink away from the middle outwards.
                int phase = (int)(state_ms * 255 / CLEAR_MS);
                if ((state_ms / 60) % 2 == 0) face = rgb_lerp(face, C_WHITE, 200);
                int dist = c < TET_W / 2 ? TET_W / 2 - 1 - c : c - TET_W / 2;
                if (phase > 128 + dist * 25) continue;
            }
            draw_block(WELL_X + c * CELL, WELL_Y + r * CELL, CELL, face);
        }
    }
    if (state == ST_PLAY) {
        draw_ghost();
        if (trail.ms) {
            int8_t c[4][2];
            tetris_cells(trail.type, trail.rot, c);
            int alpha = (int)trail.ms; // fades from 140 to nothing
            for (int i = 0; i < 4; i++) {
                int top = trail.y0 + c[i][1], bottom = trail.y1 + c[i][1];
                if (top < 0) top = 0;
                if (bottom <= top) continue;
                int x = WELL_X + (trail.x + c[i][0]) * CELL;
                gfx_blend_rect(x + 2, WELL_Y + top * CELL, CELL - 4, (bottom - top) * CELL, FACE[trail.type], alpha);
            }
        }
        int8_t c[4][2];
        tetris_cells(game.type, game.rot, c);
        for (int i = 0; i < 4; i++) {
            int cy = game.y + c[i][1];
            if (cy < 0) continue;
            draw_block(WELL_X + (game.x + c[i][0]) * CELL, WELL_Y + cy * CELL, CELL, FACE[game.type]);
        }
    }
}

// A preview piece centred in a w-wide slot.
static void draw_preview(int type, int cx, int y, int size, uint16_t face) {
    int8_t c[4][2];
    tetris_cells(type, 0, c);
    int minx = 9, maxx = -1, miny = 9, maxy = -1;
    for (int i = 0; i < 4; i++) {
        if (c[i][0] < minx) minx = c[i][0];
        if (c[i][0] > maxx) maxx = c[i][0];
        if (c[i][1] < miny) miny = c[i][1];
        if (c[i][1] > maxy) maxy = c[i][1];
    }
    int w = (maxx - minx + 1) * size, h = (maxy - miny + 1) * size;
    int ox = cx - w / 2 - minx * size, oy = y + (2 * size - h) / 2 - miny * size;
    draw_piece(type, 0, ox, oy, size, face);
}

static void draw_number(int cx, int y, uint32_t v, uint16_t c) {
    char buf[12];
    snprintf(buf, sizeof buf, "%lu", (unsigned long)v);
    gfx_text_center(cx, y, buf, c, &font_blocks, 1);
}

static void draw_hud(void) {
    // Top strip.
    gfx_rect(0, 0, gfx_w, WELL_Y - 1, PANEL_BG);
    gfx_hline(0, WELL_Y - 2, gfx_w, rgb(40, 40, 62));
    char buf[24];
    gfx_text(4, 7, "SCORE", LABEL_C, &font_mini, 1);
    snprintf(buf, sizeof buf, "%lu", (unsigned long)game.score);
    gfx_text(32, 6, buf, C_WHITE, &font_blocks, 1);
    snprintf(buf, sizeof buf, "HI %lu", (unsigned long)(g_save.tetris_best > game.score ? g_save.tetris_best : game.score));
    gfx_text(gfx_w - 3 - gfx_text_width(buf, &font_mini, 1), 7, buf, rgb(90, 94, 128), &font_mini, 1);

    // Side column.
    int sw = gfx_w - SIDE_X, cx = SIDE_X + sw / 2;
    gfx_rect(SIDE_X, WELL_Y - 1, sw, gfx_h - WELL_Y + 1, PANEL_BG);
    gfx_text_center(cx + 1, WELL_Y + 3, "NEXT", LABEL_C, &font_mini, 1);
    draw_preview(game.next[0], cx, WELL_Y + 13, 5, FACE[game.next[0]]);
    draw_preview(game.next[1], cx, WELL_Y + 30, 4, rgb_lerp(FACE[game.next[1]], PANEL_BG, 70));
    draw_preview(game.next[2], cx, WELL_Y + 43, 4, rgb_lerp(FACE[game.next[2]], PANEL_BG, 130));

    gfx_hline(SIDE_X + 3, WELL_Y + 58, sw - 6, rgb(44, 44, 68));
    gfx_text_center(cx + 1, WELL_Y + 64, "LINES", LABEL_C, &font_mini, 1);
    draw_number(cx + 1, WELL_Y + 73, game.lines, C_WHITE);
    gfx_text_center(cx + 1, WELL_Y + 90, "LV", LABEL_C, &font_mini, 1);
    draw_number(cx + 1, WELL_Y + 99, game.level, rgb_hsv((int)(game.level * 47 % 360), 150, 255));

    // Lines to the next level, as a bar that fills upwards.
    int bar_top = WELL_Y + 116, bar_h = gfx_h - 6 - bar_top, bar_x = cx - 3;
    gfx_frame(bar_x - 1, bar_top - 1, 8, bar_h + 2, rgb(50, 52, 80));
    int fill = (int)(game.lines % 10) * bar_h / 10;
    for (int i = 0; i < 10; i++) {
        int seg_y = bar_top + bar_h - (i + 1) * bar_h / 10;
        int seg_h = bar_h / 10 - 1;
        uint16_t col = (i * bar_h / 10) < fill ? rgb_hsv(190 - i * 14, 190, 240) : rgb(20, 20, 34);
        gfx_rect(bar_x, seg_y + 1, 6, seg_h, col);
    }
}

// The title: the falling pieces behind, the coloured name, and what each
// button does written beside it.
static void draw_title(void) {
    gfx_clear(WELL_BG);
    for (int r = 1; r < 22; r++)
        for (int c = 1; c < 13; c++) gfx_pixel(c * CELL - 4, r * CELL - 2, WELL_DOT);
    // Pieces drifting down the background, dimmed.
    for (int i = 0; i < 7; i++) {
        int speed = 18 + i * 5;
        int y = (int)((anim_ms * (uint32_t)speed / 1000 + (uint32_t)i * 71) % 300) - 40;
        int x = (i * 37 + 6) % 112;
        draw_piece(i, (int)(anim_ms / 1500 + (uint32_t)i) & 3, x, y, 9, rgb_lerp(FACE[i], WELL_BG, 175));
    }
    // A stack along the bottom.
    static const uint8_t stack[3][12] = {
        {0, 0, 3, 3, 0, 0, 0, 1, 0, 0, 6, 0},
        {5, 3, 3, 7, 7, 0, 2, 2, 4, 6, 6, 6},
        {5, 5, 1, 7, 4, 4, 2, 2, 4, 4, 1, 1},
    };
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 12; c++)
            if (stack[r][c]) draw_block(c * 11 + 2, gfx_h - 33 + r * 11, 11, FACE[stack[r][c] - 1]);

    gfx_rect(0, 52, gfx_w, 58, WELL_BG);
    gfx_hline(0, 52, gfx_w, rgb(34, 36, 58));
    gfx_hline(0, 109, gfx_w, rgb(34, 36, 58));
    static const char *const letters[] = {"T", "E", "T", "R", "I", "S"};
    static const uint8_t colours[] = {TET_Z, TET_L, TET_O, TET_S, TET_I, TET_T};
    int x = 6;
    for (int i = 0; i < 6; i++) {
        int bob = ((anim_ms / 120 + (uint32_t)(6 - i)) % 12) == 0 ? -1 : 0;
        gfx_text_outline(x + 1, 62 + bob + 1, letters[i], C_BLACK, C_BLACK, &font_blocks, 3);
        gfx_text_outline(x, 62 + bob, letters[i], FACE[colours[i]], rgb(10, 10, 16), &font_blocks, 3);
        x += 21;
    }

    char buf[32];
    snprintf(buf, sizeof buf, "BEST %lu", (unsigned long)g_save.tetris_best);
    gfx_text_center(gfx_w / 2, 98, buf, rgb(200, 200, 225), &font_mini, 1);
    if ((anim_ms / 500) % 2 == 0) gfx_text_center(gfx_w / 2, 126, "press any button", C_WHITE, &font_mini, 1);

    // Button labels beside the buttons: B and A above the screen, Y and X below.
    gfx_round_rect(2, 2, 46, 13, 3, PANEL_BG);
    gfx_text(6, 5, "B drop", C_YELLOW, &font_mini, 1);
    gfx_round_rect(gfx_w - 50, 2, 48, 13, 3, PANEL_BG);
    gfx_text(gfx_w - 46, 5, "A rotate", C_CYAN, &font_mini, 1);
    gfx_round_rect(2, gfx_h - 50, 42, 13, 3, PANEL_BG);
    gfx_text(6, gfx_h - 47, "Y left", C_GREEN, &font_mini, 1);
    gfx_round_rect(gfx_w - 46, gfx_h - 50, 44, 13, 3, PANEL_BG);
    gfx_text(gfx_w - 42, gfx_h - 47, "X right", C_ORANGE, &font_mini, 1);
}

static void draw_game_over(void) {
    if (state_ms < 600) return;
    const int x = WELL_X + 6, y = 70, w = TET_W * CELL - 12, h = 96;
    gfx_round_rect(x - 1, y - 1, w + 2, h + 2, 5, FRAME_C);
    gfx_round_rect(x, y, w, h, 4, rgb(20, 20, 34));
    int cx = x + w / 2;
    gfx_text_outline(cx - gfx_text_width("GAME", &font_blocks, 2) / 2, y + 8, "GAME", C_RED, C_BLACK, &font_blocks, 2);
    gfx_text_outline(cx - gfx_text_width("OVER", &font_blocks, 2) / 2, y + 26, "OVER", C_RED, C_BLACK, &font_blocks, 2);
    gfx_text_center(cx, y + 48, "SCORE", LABEL_C, &font_mini, 1);
    draw_number(cx, y + 56, game.score, C_WHITE);
    if (new_best) {
        if ((state_ms / 250) % 2 == 0) gfx_text_center(cx, y + 72, "NEW BEST!", C_YELLOW, &font_mini, 1);
    } else {
        char buf[24];
        snprintf(buf, sizeof buf, "BEST %lu", (unsigned long)g_save.tetris_best);
        gfx_text_center(cx, y + 72, buf, rgb(200, 200, 225), &font_mini, 1);
    }
    if (state_ms >= RESTART_MS && (state_ms / 400) % 2 == 0)
        gfx_text_center(cx, y + 84, "press to play", LABEL_C, &font_mini, 1);
}

static void end_game(void) {
    state = ST_OVER;
    state_ms = 0;
    stats.games++;
    if (game.score > g_save.tetris_best) {
        g_save.tetris_best = game.score;
        new_best = true;
    }
    led_flash(120, 0, 0, 700);
}

// After a piece lands: clear rows or bring in the next piece.
static void land(void) {
    int full = tetris_lock(&game);
    lock_ms = 0;
    lock_resets = 0;
    fall_ms = 0;
    if (game.over) { end_game(); return; }
    if (full) {
        clear_mask = tetris_full_rows(&game);
        clear_count = full;
        state = ST_CLEAR;
        state_ms = 0;
        if (full == 4) led_flash(120, 120, 120, CLEAR_MS + 100);
        else led_flash(0, 60, 90, CLEAR_MS);
        return;
    }
    if (!tetris_spawn(&game)) { end_game(); return; }
    piece_no++;
    lowest_y = game.y;
}

// A successful move or turn while resting on the stack buys more time.
static void moved(void) {
    if (!tetris_fits(&game, game.type, game.rot, game.x, game.y + 1) && lock_resets < LOCK_RESETS) {
        lock_ms = 0;
        lock_resets++;
    }
}

static void play(const input_t *in, uint32_t dt) {
    if ((in->pressed & BTN_A) && tetris_rotate(&game)) moved();
    if (in->pressed & BTN_B) {
        trail.type = game.type;
        trail.rot = game.rot;
        trail.x = game.x;
        trail.y0 = game.y;
        int rows = tetris_hard_drop(&game);
        trail.y1 = game.y;
        trail.ms = rows > 0 ? 140 : 0;
        land();
        return;
    }

    // Delayed auto shift on X and Y; both down is the flip gesture, so neither moves.
    bool left = in->held & BTN_Y, right = in->held & BTN_X;
    int dir = left && !right ? -1 : right && !left ? 1 : 0;
    if (dir != das_dir) {
        das_dir = dir;
        das_ms = DAS_MS;
        if (dir && tetris_move(&game, dir, 0)) moved();
    } else if (dir) {
        das_ms -= (int32_t)dt;
        while (das_ms <= 0) {
            das_ms += ARR_MS;
            if (tetris_move(&game, dir, 0)) moved();
        }
    }

    // Gravity, then the lock delay once the piece is resting.
    uint32_t g = tetris_gravity_ms(game.level);
    fall_ms += dt;
    while (fall_ms >= g) {
        fall_ms -= g;
        if (!tetris_move(&game, 0, 1)) { fall_ms = 0; break; }
        if (game.y > lowest_y) {
            lowest_y = game.y;
            lock_resets = 0;
        }
        lock_ms = 0;
    }
    if (!tetris_fits(&game, game.type, game.rot, game.x, game.y + 1)) {
        lock_ms += dt;
        if (lock_ms >= LOCK_MS) land();
    }
}

static bool tetris_frame(const input_t *in, uint32_t dt) {
    anim_ms += dt;
    state_ms += dt;
    trail.ms = trail.ms > dt ? trail.ms - dt : 0;

    switch (state) {
    case ST_TITLE:
        if (in->pressed) new_game();
        break;
    case ST_PLAY:
        play(in, dt);
        break;
    case ST_CLEAR:
        if (state_ms >= CLEAR_MS) {
            int n = tetris_clear_lines(&game);
            stats.lines += (uint32_t)n;
            if (n == 4) stats.tetrises++;
            state = ST_PLAY;
            state_ms = 0;
            if (!tetris_spawn(&game)) end_game();
            else lowest_y = game.y, piece_no++;
        }
        break;
    case ST_OVER:
        if (state_ms >= RESTART_MS && in->pressed) new_game();
        break;
    }

    if (state == ST_TITLE) {
        draw_title();
    } else {
        gfx_clear(PANEL_BG);
        draw_well();
        draw_hud();
        if (state == ST_CLEAR && clear_count == 4 && (state_ms / 90) % 2 == 0)
            gfx_text_outline(WELL_X + (TET_W * CELL - gfx_text_width("TETRIS!", &font_blocks, 2)) / 2, 100, "TETRIS!",
                             C_WHITE, rgb(170, 80, 225), &font_blocks, 2);
        if (state == ST_OVER) draw_game_over();
    }

    if (led_ms) {
        led_ms = led_ms > dt ? led_ms - dt : 0;
        hal_led(led_rgb[0], led_rgb[1], led_rgb[2]);
    } else {
        hal_led(0, 0, 0);
    }
    return true;
}

// ---------------------------------------------------------------- attract mode

// The autopilot plays like an unhurried good player: a moment to look at
// each new piece, then a tap per turn and per column at a human pace, a beat
// to line it up, and a hard drop. Everything is quicker once gravity is.
#define ATTRACT_LEVEL 5
#define OVER_SHOW_MS 2000

static struct {
    uint32_t piece;     // piece_no the plan is for
    int rot, x;         // where it is going
    int32_t wait_ms;    // until the next tap
    bool lined_up;      // in place, waiting out the beat before the drop
    bool tapped;        // a button was held last frame, so release it now
    int last_x, last_rot; // where the piece was before the last tap
    int misses;           // taps that changed nothing
    uint8_t last_btn;     // the last turn or move tapped
    uint32_t rng;
} pilot;

// The autopilot's own dice, so its pacing leaves the piece order alone.
static int pilot_rand(int lo, int hi) {
    pilot.rng ^= pilot.rng << 13;
    pilot.rng ^= pilot.rng >> 17;
    pilot.rng ^= pilot.rng << 5;
    return lo + (int)(pilot.rng % (uint32_t)(hi - lo + 1));
}

// A delay between lo and hi milliseconds, shortened as gravity speeds up:
// full length at level 5 and below, down to three eighths at level 10.
static int32_t pace_ms(int lo, int hi) {
    uint32_t g = tetris_gravity_ms(game.level), full = tetris_gravity_ms(ATTRACT_LEVEL);
    uint32_t k = g >= full ? 256 : g * 256 / full;
    if (k < 96) k = 96;
    return (int32_t)((uint32_t)pilot_rand(lo, hi) * k / 256);
}

static uint8_t tap(uint8_t btn, int32_t then_wait) {
    pilot.tapped = true;
    pilot.last_btn = btn & (BTN_A | BTN_X | BTN_Y);
    pilot.wait_ms = then_wait;
    return btn;
}

// Rows at the bottom that are full but for the right-hand column.
static int ready_rows(void) {
    int n = 0;
    for (int r = TET_H - 1; r >= 0 && !game.board[r][TET_W - 1]; r--, n++)
        for (int c = 0; c < TET_W - 1; c++)
            if (!game.board[r][c]) return n;
    return n;
}

static void tetris_attract(void) {
    memset(&pilot, 0, sizeof pilot);
    memset(&stats, 0, sizeof stats);
    pilot.rng = hal_random() | 1u;
    start_level = ATTRACT_LEVEL;
    new_game();
    // Play unseen, the same way, until four rows wait on a long I, so the
    // demo opens on a stack ready for a TETRIS! rather than an empty well.
    for (int n = 0; n < 24 && ready_rows() < 4; n++) {
        int rot, x;
        tetris_plan(&game, &rot, &x);
        for (int i = 0; i < 4 && game.rot != rot; i++) tetris_rotate(&game);
        while (game.x != x && tetris_move(&game, x > game.x ? 1 : -1, 0)) {}
        tetris_hard_drop(&game);
        tetris_lock(&game);
        tetris_clear_lines(&game);
        if (game.over || !tetris_spawn(&game)) {
            new_game();
            break;
        }
    }
    lowest_y = game.y;
    piece_no++;
}

static uint8_t tetris_autopilot(uint32_t dt) {
    if (pilot.tapped) { // one frame down, then up, so every tap is a fresh press
        pilot.tapped = false;
        return 0;
    }
    pilot.wait_ms -= (int32_t)dt;
    if (state == ST_TITLE) return tap(BTN_A, 0);
    if (state == ST_OVER) return state_ms >= OVER_SHOW_MS ? tap(BTN_A, 0) : 0;
    if (state != ST_PLAY) return 0;

    if (pilot.piece != piece_no) {
        pilot.piece = piece_no;
        tetris_plan(&game, &pilot.rot, &pilot.x);
        pilot.wait_ms = pace_ms(140, 260); // a look at the new piece
        pilot.lined_up = false;
        pilot.misses = 0;
        pilot.last_btn = 0;
        stats.pieces++;
    }
    if (pilot.wait_ms > 0) return 0;

    // A turn or a move that went nowhere: the piece fell into something, so
    // think again from here, and after two of those just drop it.
    if (pilot.last_btn && game.x == pilot.last_x && game.rot == pilot.last_rot && ++pilot.misses <= 2)
        tetris_plan(&game, &pilot.rot, &pilot.x);
    pilot.last_btn = 0;
    if (pilot.misses > 2) pilot.rot = game.rot, pilot.x = game.x;
    pilot.last_x = game.x;
    pilot.last_rot = game.rot;
    if (game.rot != pilot.rot) return tap(BTN_A, pace_ms(60, 95));
    if (game.x != pilot.x) return tap(game.x < pilot.x ? BTN_X : BTN_Y, pace_ms(60, 90));
    if (!pilot.lined_up) {
        pilot.lined_up = true;
        pilot.wait_ms = pace_ms(90, 200);
        return 0;
    }
    return tap(BTN_B, 0);
}

const tetris_attract_stats_t *tetris_attract_stats(void) { return &stats; }

const app_t app_tetris = {
    .name = "Tetris",
    .orient = ORIENT_PORTRAIT,
    .accent = rgb(60, 200, 230),
    .fps = 60,
    .enter = tetris_enter,
    .frame = tetris_frame,
    .attract = tetris_attract,
    .autopilot = tetris_autopilot,
};

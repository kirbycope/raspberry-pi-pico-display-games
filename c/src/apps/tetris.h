// Tetris rules: the board, the pieces and the scoring, kept apart from
// drawing and timing so they can be tested.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TET_W 10
#define TET_H 20

// Piece types, in the order of the colour table.
enum { TET_I, TET_O, TET_T, TET_S, TET_Z, TET_J, TET_L, TET_PIECES };

typedef struct {
    // 0 is empty, otherwise the piece type plus one. Row 0 is the top.
    uint8_t board[TET_H][TET_W];
    // The falling piece: its type, rotation (0..3, clockwise) and the
    // top-left of its rotation box, which may sit above the board.
    int type, rot, x, y;
    int next[3]; // upcoming pieces, next[0] first
    uint8_t bag[TET_PIECES];
    int bag_left;
    uint32_t score, lines, level;
    uint32_t start_level; // the level at no lines; 0 counts as 1
    bool over;
} tetris_t;

// Clears the board, refills the bag and spawns the first piece.
void tetris_reset(tetris_t *t);
// The 7-bag randomiser: every run of seven draws holds each piece once.
int tetris_bag_draw(tetris_t *t);
// The four cells of a piece, relative to its rotation box.
void tetris_cells(int type, int rot, int8_t out[4][2]);
// True when the piece fits on the board there. Cells above the top are allowed.
bool tetris_fits(const tetris_t *t, int type, int rot, int x, int y);
bool tetris_move(tetris_t *t, int dx, int dy);
// Turns the piece clockwise, trying the SRS wall kicks. False if nothing fit.
bool tetris_rotate(tetris_t *t);
// The row the piece would land on, for the ghost.
int tetris_drop_y(const tetris_t *t);
// Drops the piece to the floor, scores two points a row, returns the rows fallen.
int tetris_hard_drop(tetris_t *t);
// Writes the piece into the board and returns how many rows are now full.
// A piece locked wholly above the board ends the game.
int tetris_lock(tetris_t *t);
// Bit r is set when row r is full.
uint32_t tetris_full_rows(const tetris_t *t);
// Removes full rows, scores them at the current level, levels up every ten
// lines (counting from the start level), and returns how many went.
int tetris_clear_lines(tetris_t *t);
// 100, 300, 500 or 800 for one to four lines, times the level.
uint32_t tetris_line_score(int lines, uint32_t level);
// Brings in the next piece. False, and over set, when it is blocked.
bool tetris_spawn(tetris_t *t);
// Milliseconds per row of gravity at a level.
uint32_t tetris_gravity_ms(uint32_t level);

// The attract-mode player's choice for the falling piece: the rotation
// (0..3, as in tetris_t) and box column that leave the best board after a
// hard drop from where the piece is now. It keeps the right-hand column open
// for four-line clears until the stack gets high, then takes whatever lines
// it can. One piece deep and integer only, so it is cheap on the Pico.
void tetris_plan(const tetris_t *t, int *rot, int *x);

// What attract mode has played since it last opened Tetris, for tests and
// tuning.
typedef struct {
    uint32_t games;    // games that ended
    uint32_t pieces;   // pieces the autopilot placed
    uint32_t lines;
    uint32_t tetrises; // four-line clears
} tetris_attract_stats_t;
const tetris_attract_stats_t *tetris_attract_stats(void);

#include <string.h>

#include "apps/tetris.h"
#include "test.h"

static tetris_t t;

// An empty board with a chosen piece at a chosen place.
static void place(int type, int rot, int x, int y) {
    tetris_reset(&t);
    memset(t.board, 0, sizeof t.board);
    t.over = false;
    t.type = type;
    t.rot = rot;
    t.x = x;
    t.y = y;
}

static void fill_row(int r, int gap) {
    for (int c = 0; c < TET_W; c++) t.board[r][c] = c == gap ? 0 : 1;
}

TEST(tetris_collides_with_walls_floor_and_stack) {
    place(TET_O, 0, 3, 0);
    // O fills columns x+1 and x+2.
    CHECK(tetris_fits(&t, TET_O, 0, -1, 0));
    CHECK(!tetris_fits(&t, TET_O, 0, -2, 0));
    CHECK(tetris_fits(&t, TET_O, 0, TET_W - 3, 0));
    CHECK(!tetris_fits(&t, TET_O, 0, TET_W - 2, 0));
    CHECK(tetris_fits(&t, TET_O, 0, 3, TET_H - 2));
    CHECK(!tetris_fits(&t, TET_O, 0, 3, TET_H - 1));
    // Moving left stops at the wall.
    int moves = 0;
    while (tetris_move(&t, -1, 0)) moves++;
    CHECK_EQ(moves, 4);
    CHECK_EQ(t.x, -1);
    // Falling stops on the stack.
    t.board[TET_H - 1][0] = 1;
    t.board[TET_H - 1][1] = 1;
    while (tetris_move(&t, 0, 1)) {}
    CHECK_EQ(t.y, TET_H - 3);
    CHECK_EQ(tetris_drop_y(&t), t.y);
    // Cells above the top of the board do not collide.
    CHECK(tetris_fits(&t, TET_I, 0, 3, -1));
}

TEST(tetris_pieces_have_four_distinct_cells_in_every_rotation) {
    for (int p = 0; p < TET_PIECES; p++)
        for (int r = 0; r < 4; r++) {
            int8_t c[4][2];
            tetris_cells(p, r, c);
            for (int i = 0; i < 4; i++)
                for (int j = i + 1; j < 4; j++) CHECK(c[i][0] != c[j][0] || c[i][1] != c[j][1]);
        }
}

TEST(tetris_rotation_kicks_off_the_left_wall) {
    // T pointing right against the left wall: its stem is in column 0, and a
    // plain turn would push a cell into column -1.
    place(TET_T, 1, -1, 5);
    CHECK(tetris_fits(&t, TET_T, 1, -1, 5));
    CHECK(!tetris_fits(&t, TET_T, 2, -1, 5));
    CHECK(tetris_rotate(&t));
    CHECK_EQ(t.rot, 2);
    CHECK_EQ(t.x, 0);
    CHECK_EQ(t.y, 5);
}

TEST(tetris_rotation_kicks_the_i_off_the_right_wall) {
    // Vertical I in the rightmost column; turning back to flat needs a kick left.
    place(TET_I, 1, TET_W - 3, 5);
    int8_t c[4][2];
    tetris_cells(TET_I, 1, c);
    CHECK_EQ(t.x + c[0][0], TET_W - 1);
    CHECK(tetris_rotate(&t));
    CHECK_EQ(t.rot, 2);
    tetris_cells(TET_I, 2, c);
    for (int i = 0; i < 4; i++) CHECK(t.x + c[i][0] < TET_W);
    CHECK(tetris_fits(&t, t.type, t.rot, t.x, t.y));
}

TEST(tetris_rotation_fails_when_boxed_in) {
    place(TET_T, 0, 3, TET_H - 2);
    // Fill everything but the T's own cells.
    for (int r = 0; r < TET_H; r++)
        for (int c = 0; c < TET_W; c++) t.board[r][c] = 1;
    int8_t c[4][2];
    tetris_cells(TET_T, 0, c);
    for (int i = 0; i < 4; i++) t.board[t.y + c[i][1]][t.x + c[i][0]] = 0;
    CHECK(!tetris_rotate(&t));
    CHECK_EQ(t.rot, 0);
}

TEST(tetris_clearing_one_line_shifts_rows_and_scores) {
    place(TET_I, 0, 0, 0);
    fill_row(TET_H - 1, -1);
    for (int c = 0; c < 4; c++) t.board[TET_H - 1][c] = 0;
    t.board[TET_H - 2][7] = 3;
    t.board[TET_H - 3][7] = 4;
    t.score = 0;
    CHECK_EQ(tetris_hard_drop(&t), TET_H - 2); // the I's cells are in box row 1
    CHECK_EQ(t.score, 2u * (TET_H - 2));
    CHECK_EQ(tetris_lock(&t), 1);
    CHECK_EQ(tetris_full_rows(&t), 1u << (TET_H - 1));
    uint32_t before = t.score;
    CHECK_EQ(tetris_clear_lines(&t), 1);
    CHECK_EQ(t.score - before, 100);
    CHECK_EQ(t.lines, 1);
    CHECK_EQ(t.board[TET_H - 1][7], 3);
    CHECK_EQ(t.board[TET_H - 2][7], 4);
    CHECK_EQ(t.board[TET_H - 3][7], 0);
    CHECK_EQ(t.board[TET_H - 1][0], 0);
    CHECK_EQ(tetris_full_rows(&t), 0);
}

TEST(tetris_clearing_four_lines_shifts_rows_and_scores_at_level) {
    place(TET_I, 1, 0, 0);
    t.level = 3;
    t.lines = 20;
    for (int r = TET_H - 4; r < TET_H; r++) fill_row(r, 9);
    t.board[TET_H - 5][2] = 6; // stays, drops by four
    t.board[TET_H - 6][4] = 2;
    // Vertical I sits in box column 2, so x = 7 puts it in column 9.
    t.x = 7;
    t.y = -1;
    CHECK(tetris_fits(&t, t.type, t.rot, t.x, t.y));
    tetris_hard_drop(&t);
    CHECK_EQ(tetris_lock(&t), 4);
    uint32_t before = t.score;
    CHECK_EQ(tetris_clear_lines(&t), 4);
    CHECK_EQ(t.score - before, 800 * 3);
    CHECK_EQ(t.lines, 24);
    CHECK_EQ(t.board[TET_H - 1][2], 6);
    CHECK_EQ(t.board[TET_H - 2][4], 2);
    for (int r = 0; r < TET_H; r++)
        for (int c = 0; c < TET_W; c++)
            if (!(r == TET_H - 1 && c == 2) && !(r == TET_H - 2 && c == 4)) CHECK_EQ(t.board[r][c], 0);
}

TEST(tetris_clears_separated_rows) {
    place(TET_T, 0, 0, 0);
    fill_row(TET_H - 1, -1);
    fill_row(TET_H - 2, 5);
    fill_row(TET_H - 3, -1);
    t.board[TET_H - 4][1] = 7;
    CHECK_EQ(tetris_full_rows(&t), (1u << (TET_H - 1)) | (1u << (TET_H - 3)));
    CHECK_EQ(tetris_clear_lines(&t), 2);
    CHECK_EQ(t.score, 300);
    CHECK_EQ(t.board[TET_H - 1][5], 0); // the gap row moved to the bottom
    CHECK_EQ(t.board[TET_H - 1][4], 1);
    CHECK_EQ(t.board[TET_H - 2][1], 7);
}

TEST(tetris_levels_up_every_ten_lines) {
    CHECK_EQ(tetris_line_score(1, 1), 100);
    CHECK_EQ(tetris_line_score(2, 1), 300);
    CHECK_EQ(tetris_line_score(3, 2), 1000);
    CHECK_EQ(tetris_line_score(4, 5), 4000);
    place(TET_T, 0, 0, 0);
    t.lines = 9;
    fill_row(TET_H - 1, -1);
    tetris_clear_lines(&t);
    CHECK_EQ(t.level, 2);
    CHECK(tetris_gravity_ms(2) < tetris_gravity_ms(1));
    CHECK(tetris_gravity_ms(99) > 0);
}

TEST(tetris_bag_yields_each_piece_once_per_seven) {
    tetris_reset(&t);
    t.bag_left = 0;
    for (int round = 0; round < 20; round++) {
        int seen[TET_PIECES] = {0};
        for (int i = 0; i < TET_PIECES; i++) {
            int p = tetris_bag_draw(&t);
            CHECK(p >= 0 && p < TET_PIECES);
            seen[p]++;
        }
        for (int p = 0; p < TET_PIECES; p++) CHECK_EQ(seen[p], 1);
    }
}

TEST(tetris_game_over_when_spawn_is_blocked) {
    tetris_reset(&t);
    CHECK(!t.over);
    for (int c = 3; c < 7; c++) t.board[0][c] = t.board[1][c] = 1;
    CHECK(!tetris_spawn(&t));
    CHECK(t.over);
}

TEST(tetris_game_over_when_locked_above_the_top) {
    place(TET_O, 0, 3, -2);
    CHECK(tetris_fits(&t, TET_O, 0, 3, -2));
    tetris_lock(&t);
    CHECK(t.over);
}

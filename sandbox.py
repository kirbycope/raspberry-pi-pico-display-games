import picodisplay as display
import random
import utime
from helpers import *
from tetris import *


def main():
    init_display()
    # debug_turn_on_axis()
    # debug_turn_on_diag()
    global falling_piece
    falling_piece = None
    draw_brick_borders()
    show_control_labels()
    display.update()
    while True:
        if display.is_pressed(display.BUTTON_A):
            display_clear()
            display_set_pen_color(WTE)
            display.text("End", 10, 10, 240, 4)
            display.update()
            break
        elif display.is_pressed(display.BUTTON_B):
            if falling_piece != None:
                clear_falling_piece()
            falling_piece = FallingPiece()
        elif display.is_pressed(display.BUTTON_X):
            if falling_piece != None:
                move_falling_piece_right()
        elif display.is_pressed(display.BUTTON_Y):
            if falling_piece != None:
                move_falling_piece_left()
        if falling_piece != None:
            if falling_piece.row+1 < 18:
                clear_falling_piece()
                falling_piece.row += 1
                draw_tetrominoe(falling_piece, falling_piece.color)
                show_control_labels()
                display.update()
        utime.sleep(.2)


def clear_falling_piece():
    try:
        draw_tetrominoe(falling_piece, BLK)
    except:
        pass


def move_falling_piece_right():
    if falling_piece.column < 10:
        clear_falling_piece()
        falling_piece.column += 1  # "move" it right a column
        # re-draw the piece
        draw_tetrominoe(falling_piece, falling_piece.color)


def move_falling_piece_left():
    if falling_piece.column > 1:
        clear_falling_piece()
        falling_piece.column -= 1  # "move" it left a column
        # re-draw the piece
        draw_tetrominoe(falling_piece, falling_piece.color)


if __name__ == "__main__":
    main()

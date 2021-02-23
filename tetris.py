import random
import utime
from helpers import *

column_start_pixel = [0, 108, 98, 88, 78, 68, 58, 48, 38, 28, 18]
row_start_pixel = [0, 30, 40, 50, 60, 70, 80, 90, 100,
                   110, 120, 130, 140, 150, 160, 170, 180, 190, 200, 210]


class FallingPiece:
    def __init__(self):
        self.column = 5  # of 10
        self.row = 1  # of 18
        self.shape = random.randint(1, 7)
        self.shap_name = determine_tetrominoe_shape_name(self.shape)
        self.orientation = 1  # of 4
        self.color = determine_tetrominoe_color(self.shape)

    def __str__(self):
        return str(self.__class__) + ": " + str(self.__dict__)


def clear_piece(tetrominoe):
    try:
        draw_tetrominoe(tetrominoe, BLK)
    except:
        pass


def determine_tetrominoe_color(shape):
    if shape == 1:
        return LBL
    elif shape == 2:
        return YEL
    elif shape == 3:
        return PUR
    elif shape == 4:
        return DBL
    elif shape == 5:
        return ORG
    elif shape == 6:
        return GRN
    else:
        return RED


def determine_tetrominoe_shape_name(shape):
    if shape == 1:
        return "I"
    elif shape == 2:
        return "O"
    elif shape == 3:
        return "T"
    elif shape == 4:
        return "J"
    elif shape == 5:
        return "L"
    elif shape == 6:
        return "S"
    else:
        return "Z"


def draw_brick_block(x, y):
    # draw the 10x10 gray base
    draw_rectangle(x, y, 10, 10, GRY)
    # draw the top horizontal line
    draw_line(x+3, y, x+3, y+10, BLK)
    # draw the bottom horizontal line
    draw_line(x+8, y, x+8, y+10, BLK)
    # draw the top right vertical line
    draw_line(x, y+3, x+3, y+3, BLK)
    # draw the top left vertical line
    draw_line(x, y+8, x+3, y+8, BLK)
    # draw the middle right vertical line
    draw_line(x+3, y, x+8, y, BLK)
    # draw the middle left vertical line
    draw_line(x+3, y+5, x+8, y+5, BLK)
    # draw the bottom right "line"
    draw_pixel(x+9, y+3, BLK)
    # draw the bottom left "line"
    draw_pixel(x+9, y+8, BLK)


def draw_brick_borders():
    # Draw right side brick border
    for x in range(31, 209, 10):
        draw_brick_block(x, 1)
    for x in range(31, 209, 10):
        draw_brick_block(x, 10)
    # Draw left side brick border
    for x in range(31, 209, 10):
        draw_brick_block(x, 118)
    for x in range(31, 209, 10):
        draw_brick_block(x, 128)


def draw_tetrominoe(tetrominoe, color):
    if tetrominoe.shape == 1:  # i
        if tetrominoe.orientation == 1 or tetrominoe.orientation == 3:  # horizontal
            draw_rectangle(
                row_start_pixel[tetrominoe.row], column_start_pixel[tetrominoe.column]-20, 10, 40, color)
        else:  # vertical
            draw_rectangle(
                row_start_pixel[tetrominoe.row], column_start_pixel[tetrominoe.column], 40, 10, color)
    elif tetrominoe.shape == 2:  # o
        draw_rectangle(row_start_pixel[tetrominoe.row],
                       column_start_pixel[tetrominoe.column]-10, 20, 20, color)
    elif tetrominoe.shape == 3:  # t
        if tetrominoe.orientation == 1:  # horizontal, down
            draw_rectangle(
                row_start_pixel[tetrominoe.row], column_start_pixel[tetrominoe.column]-10, 10, 30, color)
            draw_rectangle(row_start_pixel[tetrominoe.row]+10,
                           column_start_pixel[tetrominoe.column], 10, 10, color)
        # todo
    elif tetrominoe.shape == 4:  # j
        if tetrominoe.orientation == 1:  # horizontal, down
            draw_rectangle(
                row_start_pixel[tetrominoe.row], column_start_pixel[tetrominoe.column]-10, 10, 30, color)
            draw_rectangle(row_start_pixel[tetrominoe.row]+10,
                           column_start_pixel[tetrominoe.column]-10, 10, 10, color)
        # todo
    elif tetrominoe.shape == 5:  # l
        if tetrominoe.orientation == 1:  # horizontal, down
            draw_rectangle(
                row_start_pixel[tetrominoe.row], column_start_pixel[tetrominoe.column]-10, 10, 30, color)
            draw_rectangle(row_start_pixel[tetrominoe.row]+10,
                           column_start_pixel[tetrominoe.column]+10, 10, 10, color)
        # todo
    elif tetrominoe.shape == 6:  # s
        if tetrominoe.orientation == 1 or tetrominoe.orientation == 3:  # horizontal, down
            draw_rectangle(
                row_start_pixel[tetrominoe.row], column_start_pixel[tetrominoe.column]-10, 10, 20, color)
            draw_rectangle(row_start_pixel[tetrominoe.row]+10,
                           column_start_pixel[tetrominoe.column], 10, 20, color)
        # todo
    elif tetrominoe.shape == 7:  # z
        if tetrominoe.orientation == 1 or tetrominoe.orientation == 3:  # horizontal, down
            draw_rectangle(
                row_start_pixel[tetrominoe.row], column_start_pixel[tetrominoe.column], 10, 20, color)
            draw_rectangle(row_start_pixel[tetrominoe.row]+10,
                           column_start_pixel[tetrominoe.column]-10, 10, 20, color)
        # todo


def move_falling_piece_left(falling_piece):
    if falling_piece.column > 1:
        clear_piece(falling_piece)
        falling_piece.column -= 1
        draw_tetrominoe(falling_piece, falling_piece.color)
    return falling_piece


def move_falling_piece_right(falling_piece):
    if falling_piece.column < 10:
        clear_piece(falling_piece)
        falling_piece.column += 1
        draw_tetrominoe(falling_piece, falling_piece.color)
    return falling_piece


def show_control_labels():
    # Clear any overflow on the top
    draw_rectangle(1, 1, 30, display_height, BLK)
    # Upper-bottom border
    draw_line(30, 1, 30, display_height, WTE)
    # Upper divider
    draw_line(1, 67, 30, 67, WTE)
    # B-button label
    draw_line(10, 98, 20, 98, WTE)
    draw_line(20, 98, 17, 101, WTE)
    draw_line(20, 98, 17, 95, WTE)
    # A-button label
    draw_ring(15, 30, 5, WTE)
    draw_pixel(16, 25, BLK)
    draw_pixel(15, 25, BLK)
    draw_line(14, 26, 14, 27, WTE)
    draw_line(12, 25, 13, 25, WTE)
    # Clear any overflow on the bottom
    draw_rectangle(211, 1, 30, display_height, BLK)
    # Y-button label
    draw_line(230, 93, 230, 103, WTE)
    draw_line(231, 101, 233, 100, WTE)
    draw_line(229, 101, 227, 100, WTE)
    # X-button label
    draw_line(230, 27, 230, 37, WTE)
    draw_line(231, 28, 233, 30, WTE)
    draw_line(229, 28, 227, 30, WTE)
    # Lower-top border
    draw_line(211, 1, 211, display_height, WTE)
    # Lower divider
    draw_line(211, 67, 239, 67, WTE)


def tetris_start():
    display_clear()
    falling_piece = None
    draw_brick_borders()
    show_control_labels()
    display.update()
    while True:
        if display.is_pressed(display.BUTTON_A) and display.is_pressed(display.BUTTON_B):
            break
        elif display.is_pressed(display.BUTTON_A):
            # rotate
            pass
        elif display.is_pressed(display.BUTTON_B):
            if falling_piece != None:
                clear_piece(falling_piece)
            falling_piece = FallingPiece()
            # print(falling_piece) # DEBUGGING
        elif display.is_pressed(display.BUTTON_X):
            if falling_piece != None:
                move_falling_piece_right(falling_piece)
        elif display.is_pressed(display.BUTTON_Y):
            if falling_piece != None:
                move_falling_piece_left(falling_piece)
        if falling_piece != None:
            if falling_piece.row+1 < 18:
                clear_piece(falling_piece)
                falling_piece.row += 1
                draw_tetrominoe(falling_piece, falling_piece.color)
                show_control_labels()
                display.update()
                # print(falling_piece) # DEBUGGING
        utime.sleep(.2)

import picodisplay as display
import random, utime
from helpers import *
from tetris import *

def main():
    init_display()
    draw_brick_borders()
    #debug_turn_on_axis()
    #debug_turn_on_diag

    falling_piece = FailingPiece()
    draw_tetrominoe(falling_piece, falling_piece.color)
    show_control_labels()
    display.update()
    
    for x in range(16):
        lower_piece(falling_piece)
        show_control_labels()
        display.update()
        utime.sleep(.2)

def lower_piece(falling_piece):
    draw_tetrominoe(falling_piece, BLK) # clear the piece
    falling_piece.row+=1; # "move" it down a row
    draw_tetrominoe(falling_piece, falling_piece.color) # draw the piece in its new position

def draw_tetrominoe(tetrominoe, color):
    if tetrominoe.shape == 1: # i
        if tetrominoe.orientation == 1 or tetrominoe.orientation == 3: # horizontal
            draw_rectangle(row_start_pixel[tetrominoe.row], column_start_pixel[tetrominoe.column]-20, 10, 40, color)
        else: # vertical
            draw_rectangle(row_start_pixel[tetrominoe.row], column_start_pixel[tetrominoe.column], 40, 10, color)
    elif tetrominoe.shape == 2: # o
        draw_rectangle(row_start_pixel[tetrominoe.row], column_start_pixel[tetrominoe.column]-10, 20, 20, color)
    elif tetrominoe.shape == 3: # t
        if tetrominoe.orientation == 1: # horizontal, down
            draw_rectangle(row_start_pixel[tetrominoe.row], column_start_pixel[tetrominoe.column]-10, 10, 30, color)
            draw_rectangle(row_start_pixel[tetrominoe.row]+10, column_start_pixel[tetrominoe.column], 10, 10, color)
        # todo
    elif tetrominoe.shape == 4: # j
        if tetrominoe.orientation == 1: # horizontal, down
            draw_rectangle(row_start_pixel[tetrominoe.row], column_start_pixel[tetrominoe.column]-10, 10, 30, color)
            draw_rectangle(row_start_pixel[tetrominoe.row]+10, column_start_pixel[tetrominoe.column]-10, 10, 10, color)
        #todo
    elif tetrominoe.shape == 5: # l
        if tetrominoe.orientation == 1: # horizontal, down
            draw_rectangle(row_start_pixel[tetrominoe.row], column_start_pixel[tetrominoe.column]-10, 10, 30, color)
            draw_rectangle(row_start_pixel[tetrominoe.row]+10, column_start_pixel[tetrominoe.column]+10, 10, 10, color)
        #todo
    elif tetrominoe.shape == 6: # s
        if tetrominoe.orientation == 1 or tetrominoe.orientation == 3: # horizontal, down
            draw_rectangle(row_start_pixel[tetrominoe.row], column_start_pixel[tetrominoe.column]-10, 10, 20, color)
            draw_rectangle(row_start_pixel[tetrominoe.row]+10, column_start_pixel[tetrominoe.column], 10, 20, color)
        #todo
    elif tetrominoe.shape == 7: # z
        if tetrominoe.orientation == 1 or tetrominoe.orientation == 3: # horizontal, down
            draw_rectangle(row_start_pixel[tetrominoe.row], column_start_pixel[tetrominoe.column], 10, 20, color)
            draw_rectangle(row_start_pixel[tetrominoe.row]+10, column_start_pixel[tetrominoe.column]-10, 10, 20, color)
        #todo

if __name__ == "__main__":
    main()

import picodisplay as display
import random
from helpers import *
from pong import *
from tetris import *
from water import *
from screen import img_array

def main():
    init_display()
    # debug_turn_on_axis()
    # debug_turn_on_diag()
    # draw_pixel(0, 0, GRN) # A
    # draw_pixel(0, 134, RED) # B
    # draw_pixel(239, 134, YEL) # Y
    # draw_pixel(239, 0, BLU) # X
    # display.update()

    draw_text("Tetris", 6, 22, 239, 3, GRN)
    draw_text("Pong", 6, 102, 239, 3, WTE)
    draw_text("Water", 150, 22, 239, 3, BLU)
    draw_text("Debug", 150, 102, 239, 3, RED)
    display.update()
    while True:
        if display.is_pressed(display.BUTTON_A):
            tetris_start()
            break
        elif display.is_pressed(display.BUTTON_B):
            pong_start()
            print("Done")
            break
        elif display.is_pressed(display.BUTTON_X):
            water_start()
            print("Done")
            break
        elif display.is_pressed(display.BUTTON_Y):
            draw_image_from_array(img_array)
            print("Done")
            break
        utime.sleep(.1)


if __name__ == "__main__":
    main()

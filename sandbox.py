import picodisplay as display
import random
from helpers import *
from pong import *
from tetris import *
from water import *
from screen import img_array


def main():
    init_display()
    draw_text("Tetris", 6, 22, 239, 3, GRN)
    draw_text("Pong", 6, 102, 239, 3, WTE)
    draw_text("Water", 150, 22, 239, 3, BLU)
    draw_text("Debug", 150, 102, 239, 3, RED)
    display.update()
    while True:
        if display.is_pressed(display.BUTTON_A):
            tetris_start()
            display_clear()
            display.update()
            print("Tetris closed.")
            break
        elif display.is_pressed(display.BUTTON_B):
            pong_start()
            display_clear()
            display.update()
            print("Pong closed.")
            break
        elif display.is_pressed(display.BUTTON_X):
            water_start()
            display_clear()
            display.update()
            print("Water closed.")
            break
        elif display.is_pressed(display.BUTTON_Y):
            draw_image_from_array(img_array)
            display_clear()
            display.update()
            print("Debug closed.")
            break
        utime.sleep(.1)


if __name__ == "__main__":
    main()


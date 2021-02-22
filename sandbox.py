import picodisplay as display
import random
from helpers import *
from pong import *
from tetris import *
from water import *
from screen import img_array


def main():
    menu_displayed = False
    init_display()
    menu_displayed = display_game_menu()
    display.update()
    while True:
        if menu_displayed == False:
            menu_displayed = display_game_menu()
            display.update()
        if display.is_pressed(display.BUTTON_A):
            menu_displayed = False
            tetris_start()
            display_clear()
            display.update()
        elif display.is_pressed(display.BUTTON_B):
            menu_displayed = False
            pong_start()
            display_clear()
            display.update()
        elif display.is_pressed(display.BUTTON_X):
            menu_displayed = False
            water_start()
            display_clear()
            display.update()
        elif display.is_pressed(display.BUTTON_Y):
            menu_displayed = False
            draw_image_from_array(img_array)
            press_any_key()
            display_clear()
            display.update()
        utime.sleep(.2)


def display_game_menu():
    display_clear()
    draw_text("Tetris", 6, 22, 239, 3, GRN)
    draw_text("Pong", 6, 102, 239, 3, WTE)
    draw_text("Water", 150, 22, 239, 3, BLU)
    draw_text("Debug", 150, 102, 239, 3, RED)
    return True

if __name__ == "__main__":
    main()


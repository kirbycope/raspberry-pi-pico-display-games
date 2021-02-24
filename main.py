import picodisplay as display
import gc
import random
from helpers import *
from pong import *
from tetris import *
from water import *
from screen import img_array


class Selector:
    def __init__(self, y=10, value=0, page=0):
        self.y = y
        self.value = value
        self.page = page
    def __str__(self):
       return str(self.__class__) + ": " + str(self.__dict__)


def main():
    game_list = ["a","Image", "Pong", "Sketch", "Snake", "Tetris", "Water","z"]
    menu_displayed = False
    selector = None
    init_display()
    while True:
        if menu_displayed == False:
            display_clear()
            draw_button_controlls()
            selector = Selector()
            selector = display_game_menu(selector, game_list)
            menu_displayed = True
            display.update()
        if display.is_pressed(display.BUTTON_A):
            select_index = selector.value + (6*selector.page)
            if select_index - 1 >= 0:
                if selector.value > 0:
                    selector.value -= 1
                    selector.y -= 20
                elif selector.value == 0 and selector.page > 0:
                    selector.page -= 1
                    selector.value = 5
                    selector.y = 110
                selector = display_game_menu(selector, game_list)
                display.update()
        elif display.is_pressed(display.BUTTON_B):
            select_index = selector.value + (6*selector.page)
            if  select_index + 1 < len(game_list):
                if selector.value < 5:
                    selector.value += 1
                    selector.y += 20
                elif selector.value == 5 and len(game_list) > 6:
                    selector.page += 1
                    selector.value = 0
                    selector.y = 10
                selector = display_game_menu(selector, game_list)
                display.update()
        elif display.is_pressed(display.BUTTON_X):
            #break
            pass
        elif display.is_pressed(display.BUTTON_Y):
            if game_list[selector.value] == "Image":
                menu_displayed = False
                draw_image_from_array(img_array)
                press_any_key()
                display_clear()
                display.update()
                gc.collect()
            elif game_list[selector.value] == "Pong":
                menu_displayed = False
                pong_start()
                display_clear()
                display.update()
                gc.collect()
            elif game_list[selector.value] == "Tetris":
                menu_displayed = False
                tetris_start()
                display_clear()
                display.update()
                gc.collect()
        utime.sleep(.1)


def display_game_menu(selector, game_list):
    draw_rectangle(14, selector.y-10, 211, 135, BLK) # clear entire game list area
    for i in range(6):
        if 0 <= i < len(game_list):
            j = i+(6*selector.page) # this handles "paging"
            if j < len(game_list): # is something at the current index?
                if i == selector.value: # is the index the same as the selector?
                    draw_rectangle(14, selector.y-10, 211, 20, WTE) # white background
                    draw_text(game_list[j], 26, i*20, 239, 3, BLK) # black text
                else:
                    draw_rectangle(14, i*20, 211, 20, BLK) # black background
                    draw_text(game_list[j], 26, i*20, 239, 3, WTE) # white text
            if len(game_list) > 6 and selector.page == 0:
                draw_text("...", 26, 110, 239, 3, WTE)
    return selector


def draw_button_controlls():
    # (A) button
    draw_rectangle(0, 0, 13, 67, GRN)
    draw_pixel(6, 32, BLK)
    draw_line(5, 33, 7, 33, BLK)
    draw_line(4, 34, 8, 34, BLK)
    draw_line(3, 35, 9, 35, BLK)
    # (B) Button
    draw_rectangle(0, 67, 13, 68, RED)
    draw_line(3, 99, 9, 99, BLK)
    draw_line(4, 100, 8, 100, BLK)
    draw_line(5, 101, 7, 101, BLK)
    draw_pixel(6, 102, BLK)
    # (X) Button
    draw_rectangle(226, 0, 13, 67, BLU)
    draw_line(231, 30, 231, 36, BLK)
    draw_line(232, 31, 232, 35, BLK)
    draw_line(233, 32, 233, 34, BLK)
    draw_pixel(234, 33, BLK)
    # (Y) Button
    draw_rectangle(226, 67, 13, 68, YEL)
    draw_pixel(231, 101, BLK)
    draw_line(232, 100, 232, 102, BLK)
    draw_line(233, 99, 233, 103, BLK)
    draw_line(234, 98, 234, 104, BLK)


if __name__ == "__main__":
    main()



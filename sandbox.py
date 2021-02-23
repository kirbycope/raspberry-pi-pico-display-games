import picodisplay as display
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
    game_list = ["Image", "Pong", "Sketch", "Snake", "Tetris", "Water"]
    menu_displayed = False
    selector = None
    init_display()
    while True:
        if menu_displayed == False:
            selector = Selector()
            selector = display_game_menu(selector, game_list)
            menu_displayed = True
            display.update()
        if display.is_pressed(display.BUTTON_A):
            if selector.value == 0 and selector.page != 0 and len(game_list) > 6:
                display_selector(selector, BLK)
                selector.page-=1
                selector.value = 6
                selector.y = 130
                selector = display_game_menu(selector, game_list)
                display_selector(selector)
            selector = move_selector_up(selector)
            display.update()
        elif display.is_pressed(display.BUTTON_B):
            if selector.value == 5 and selector.page == 0 and len(game_list) > 6:
                display_selector(selector, BLK)
                selector.page+=1
                selector.value = 0
                selector.y = 10
                selector = display_game_menu(selector, game_list)
                display_selector(selector)
            else:
                selector = move_selector_down(selector)
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
            elif game_list[selector.value] == "Pong":
                menu_displayed = False
                pong_start()
                display_clear()
                display.update()
            elif game_list[selector.value] == "Tetris":
                menu_displayed = False
                tetris_start()
                display_clear()
                display.update()
        utime.sleep(.1)

def display_game_menu(selector, game_list):
    display_clear()
    display_selector(selector)
    draw_rectangle(0, 0, 13, 67, GRN)
    draw_pixel(6, 32, BLK)
    draw_line(5, 33, 7, 33, BLK)
    draw_line(4, 34, 8, 34, BLK)
    draw_line(3, 35, 9, 35, BLK)
    draw_rectangle(0, 67, 13, 67, RED)
    draw_line(3, 99, 9, 99, BLK)
    draw_line(4, 100, 8, 100, BLK)
    draw_line(5, 101, 7, 101, BLK)
    draw_pixel(6, 102, BLK)
    draw_rectangle(226, 0, 13, 67, BLU)
    draw_line(231, 30, 231, 36, BLK)
    draw_line(232, 31, 232, 35, BLK)
    draw_line(233, 32, 233, 34, BLK)
    draw_pixel(234, 33, BLK)
    draw_rectangle(226, 67, 13, 67, YEL)
    draw_pixel(231, 101, BLK)
    draw_line(232, 100, 232, 102, BLK)
    draw_line(233, 99, 233, 103, BLK)
    draw_line(234, 98, 234, 104, BLK)
    for i in range(6):
        if 0 <= i < len(game_list):
            j = i+(6*selector.page)
            if j < len(game_list):
                draw_text(game_list[j], 26, i*20, 239, 3, WTE)
            if len(game_list) > 6 and selector.page == 0:
                draw_text("...", 26, 110, 239, 3, WTE)
    return selector


def display_selector(selector, rgb = WTE):
    #draw_rectangle(20, selector.y-10, 110, 20, WTE)
    draw_line(130, selector.y, 150, selector.y, rgb)


def move_selector_down(selector):
    if selector.value < 5:
        display_selector(selector, BLK)
        selector.y+=20
        selector.value+=1
        display_selector(selector)
    return selector


def move_selector_up(selector):
    if selector.value > 0:
        display_selector(selector, BLK)
        selector.y-=20
        selector.value-=1
        display_selector(selector)
    return selector


if __name__ == "__main__":
    main()



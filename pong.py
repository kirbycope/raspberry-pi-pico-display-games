from helpers import *
import utime


def pong_start():
    display_clear()
    draw_rectangle(180, 48, 10, 40, BLU)
    display.update()
    while True:
         if display.is_pressed(display.BUTTON_A) and display.is_pressed(display.BUTTON_B):
            break
    utime.sleep(.2)


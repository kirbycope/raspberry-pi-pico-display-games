from helpers import *
import utime
from doom_guy import img_array

def doom_start():
    display_clear()
    display.update()
    display_hud()
    display_doom_guy()
    display.update()
    while True:
        if display.is_pressed(display.BUTTON_A) and display.is_pressed(display.BUTTON_B):
            break
        utime.sleep(.1)

def display_doom_guy():
    draw_image_from_array(img_array, 112, 108)

def display_hud():
    draw_rectangle(1, 106, 35, 28, GRY)
    draw_rectangle(37, 106, 43, 28, GRY)
    draw_rectangle(81, 106, 97, 28, GRY)
    draw_rectangle(179, 106, 9, 9, GRY)
    draw_rectangle(179, 116, 9, 9, GRY)
    draw_rectangle(179, 126, 9, 9, GRY)
    draw_rectangle(189, 106, 50, 28, GRY)
    draw_rectangle(82, 107, 7, 9, BLK)
    draw_rectangle(91, 107, 7, 9, BLK)
    draw_rectangle(100, 107, 7, 9, BLK)
    draw_rectangle(82, 117, 7, 9, BLK)
    draw_rectangle(91, 117, 7, 9, BLK)
    draw_rectangle(100, 117, 7, 9, BLK)
    draw_rectangle(109, 107, 25, 27, BLK)

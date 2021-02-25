from helpers import *
import utime
from doom_guy import img_doom_guy
#from doom_shotgun import img_doom_shotgun

def doom_start():
    display_clear()
    #display_weapon()
    #display.update()
    display_hud()
    display.update()
    while True:
        if display.is_pressed(display.BUTTON_A) and display.is_pressed(display.BUTTON_B):
            break
        utime.sleep(.1)

#def display_weapon():
    #draw_image_from_array(img_doom_shotgun)

def display_hud():
    # Ammo
    draw_rectangle(0, 106, 32, 29, GRY)
    draw_text("50", 6, 109, 240, 2, RED)
    draw_text("Ammo", 4, 126, 240, 1, BLK)
    # Health
    draw_rectangle(33, 106, 42, 29, GRY)
    draw_text("100%", 34, 109, 240, 2, RED)
    draw_text("Health", 38, 126, 240, 1, BLK)
    # Arms
    draw_rectangle(76, 106, 102, 29, GRY)
    draw_rectangle(79, 107, 8, 9, BLK)
    draw_character(ord('2'), 81, 108, 1, WTE)
    draw_rectangle(88, 107, 8, 9, BLK)
    draw_character(ord('3'), 90, 108, 1, WTE)
    draw_rectangle(97, 107, 8, 9, BLK)
    draw_character(ord('4'), 99, 108, 1, WTE)
    draw_rectangle(79, 117, 8, 9, BLK)
    draw_character(ord('5'), 81, 118, 1, WTE)
    draw_rectangle(88, 117, 8, 9, BLK)
    draw_character(ord('6'), 90, 118, 1, WTE)
    draw_rectangle(97, 117, 8, 9, BLK)
    draw_character(ord('7'), 99, 118, 1, WTE)
    draw_text("Arms", 81, 126, 240, 1, BLK)
    # Doom Guy's face
    draw_rectangle(108, 107, 25, 27, BLK)
    draw_image_from_array(img_doom_guy, 112, 107)
    # Armor
    draw_text("100%", 136, 109, 240, 2, RED)
    draw_text("Armor", 136, 126, 240, 1, BLK)
    # Keys
    draw_rectangle(179, 106, 10, 9, GRY)
    draw_rectangle(179, 116, 10, 9, GRY)
    draw_rectangle(179, 126, 10, 9, GRY)
    draw_key(180, 108, YEL)
    draw_key(180, 118, BLU)
    draw_key(180, 128, RED)
    # All Ammo
    draw_rectangle(190, 106, 50, 29, GRY)
    draw_text("BULL", 192, 107, 240, 1, BLK)
    draw_text("200", 221, 107, 240, 1, RED)
    draw_text("SHEL", 192, 114, 240, 1, BLK)
    draw_text("50", 227, 114, 240, 1, RED)
    draw_text("RCKT", 192, 121, 240, 1, BLK)
    draw_text("50", 227, 121, 240, 1, RED)
    draw_text("CELL", 192, 128, 240, 1, BLK)
    draw_text("300", 221, 128, 240, 1, RED)


def draw_key(x, y, rgb):
    draw_rectangle(x, y, 8, 5, rgb)
    draw_pixel(x, y, GRY)
    draw_pixel_span(x, y+4, 2, GRY)
    draw_pixel(x+2, y+1, BLK)
    draw_pixel(x+4, y+1, BLK)
    draw_pixel(x+6, y+1, BLK)


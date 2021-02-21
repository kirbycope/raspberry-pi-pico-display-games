import picodisplay as display
import random
from helpers import *
from tetris import *
from screen import img_array
import micropython

micropython.alloc_emergency_exception_buf(100)


def main():
    init_display()
    # debug_turn_on_axis()
    # debug_turn_on_diag()
    # draw_pixel(0, 0, GRN) # A
    # draw_pixel(0, 134, RED) # B
    # draw_pixel(239, 134, YEL) # Y
    # draw_pixel(239, 0, BLU) # X
    # display.update()

    draw_text("Tetris", 10, 10, 240, 4, WTE)
    draw_text("Pong", 10, 100, 240, 4, WTE)
    display.update()
    while True:
        if display.is_pressed(display.BUTTON_A):
            tetris_start()
            break
        elif display.is_pressed(display.BUTTON_X):
            display_clear()
            draw_image_from_array()
            display.update()
            print("Done")
            break
        utime.sleep(.1)


def draw_image_from_array():
    for x in range(len(img_array)):
        pixel = img_array[x]
        pixel_info = pixel.split(",")
        rgb = (int(pixel_info[2]), int(pixel_info[3]), int(pixel_info[4]))
        draw_pixel(int(pixel_info[1]), int(pixel_info[0]), rgb)


if __name__ == "__main__":
    main()

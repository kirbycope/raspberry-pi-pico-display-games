import picodisplay as display
from colors import *

corner_b = [1, 134]
corner_a = [1, 1]
corner_y = [239, 134]
corner_x = [239, 1]

display_height = display.get_height()
display_width = display.get_width()


def init_display():
    buffer = bytearray(display_width * display_height * 2)
    display.init(buffer)
    display.set_backlight(1.0)
    display_clear()


def debug_turn_on_axis():
    hH = int(display_height/2)
    hW = int(display_width/2)
    draw_line(display_width, hH, 1, hH, RED)
    draw_line(hW, 1, hW, display_height, RED)


def debug_turn_on_diag():
    draw_line(corner_b[0], corner_b[1], corner_x[0], corner_x[1], RED)
    draw_line(corner_a[0], corner_a[1], corner_y[0], corner_y[1], RED)


def display_clear():
    display.set_pen(0, 0, 0)
    display.clear()
    display.remove_clip()
    display.update()


def display_set_pen_color(rgb):
    display.set_pen(rgb[0], rgb[1], rgb[2])

# //instructables.com/Pimoroni-Pico-Display-Workout/


def draw_line(x, y, xx, yy, rgb):
    display_set_pen_color(rgb)
    if x > xx:
        t = x
        x = xx
        xx = t
        t = y
        y = yy
        yy = t
    if xx-x == 0:
        n = max(y, yy)-min(y, yy)+1
        for i in range(n):
            display.pixel(x, min(y, yy)+i)
    else:
        n = xx-x+1
        grad = float((yy-y)/(xx-x))
        for i in range(n):
            y3 = y + int(grad * i)
            display.pixel(x+i, y3)


def draw_pixel(x, y, rgb):
    display_set_pen_color(rgb)
    display.pixel(x, y)


def draw_rectangle(x, y, w, h, rgb):
    display_set_pen_color(rgb)
    display.rectangle(x, y, w, h)


def draw_ring(xc, yc, r, rgb):  # //geeksforgeeks.org/bresenhams-circle-drawing-algorithm/
    display_set_pen_color(rgb)
    x = 0
    y = r
    d = 3 - 2 * r
    draw_ring_points(xc, yc, x, y)
    while y >= x:
        x += 1
        if d > 0:
            y -= 1
            d = d + 4 * (x - y) + 10
        else:
            d = d + 4 * x + 6
        draw_ring_points(xc, yc, x, y)


def draw_ring_points(xc, yc, x, y):  # Draw 8 octants
    display.pixel(xc+x, yc+y)
    display.pixel(xc-x, yc+y)
    display.pixel(xc+x, yc-y)
    display.pixel(xc-x, yc-y)
    display.pixel(xc+y, yc+x)
    display.pixel(xc-y, yc+x)
    display.pixel(xc+y, yc-x)
    display.pixel(xc-y, yc-x)

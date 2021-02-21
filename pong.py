from helpers import *
import utime


blue = [180, 48]
red = [55, 48]
ball = [119, 66]

def pong_start():
    display_clear()
    ball_direction = 0
    draw_rectangle(red[0], red[1], 10, 40, RED)
    draw_rectangle(blue[0], blue[1], 10, 40, BLU)
    draw_circle(ball[0], ball[1], 4, WTE)
    display.update()
    while True:
        if display.is_pressed(display.BUTTON_A) and display.is_pressed(display.BUTTON_B):
            break
        elif display.is_pressed(display.BUTTON_A) and display.is_pressed(display.BUTTON_X):
            move_red_right()
            move_blue_right()
        elif display.is_pressed(display.BUTTON_A) and display.is_pressed(display.BUTTON_Y):
            move_red_right()
            move_blue_left()
        elif display.is_pressed(display.BUTTON_B) and display.is_pressed(display.BUTTON_X):
            move_red_left()
            move_blue_right()
        elif display.is_pressed(display.BUTTON_B) and display.is_pressed(display.BUTTON_Y):
            move_red_left()
            move_blue_left()
        elif display.is_pressed(display.BUTTON_A):
            move_red_right()
        elif display.is_pressed(display.BUTTON_B):
            move_red_left()
        elif display.is_pressed(display.BUTTON_X):
            move_blue_right()
        elif display.is_pressed(display.BUTTON_Y):
            move_blue_left()
        ball_direction = move_ball(ball_direction)
        utime.sleep(.1)


def move_ball(ball_direction):
    draw_circle(ball[0], ball[1], 4, BLK)
    display.update()
    if ball_direction == 0:
        ball[0] = ball[0]+5
    else:
        ball[0] = ball[0]-5
    draw_circle(ball[0], ball[1], 4, WTE)
    display.update()
    if ball_direction == 0 and ball[0] > 170:
        return 1
    elif ball_direction == 1 and ball[0] < 74:
        return 0
    else:
        return ball_direction


def move_blue_left():
    draw_rectangle(blue[0], blue[1], 10, 40, BLK)
    display.update()
    blue[1]=blue[1]+5
    draw_rectangle(blue[0], blue[1], 10, 40, BLU)
    display.update()


def move_blue_right():
    draw_rectangle(blue[0], blue[1], 10, 40, BLK)
    display.update()
    blue[1]=blue[1]-5
    draw_rectangle(blue[0], blue[1], 10, 40, BLU)
    display.update()


def move_red_left():
    draw_rectangle(red[0], red[1], 10, 40, BLK)
    display.update()
    red[1]=red[1]+5
    draw_rectangle(red[0], red[1], 10, 40, RED)
    display.update()


def move_red_right():
    draw_rectangle(red[0], red[1], 10, 40, BLK)
    display.update()
    red[1]=red[1]-5
    draw_rectangle(red[0], red[1], 10, 40, RED)
    display.update()


from helpers import *
import utime


class Ball:
    def __init__(self, x, y):
        self.x = x
        self.y = y
        self.direction = 0
        self.angle = 0
    def __str__(self):
       return str(self.__class__) + ": " + str(self.__dict__)


class Paddle:
    def __init__(self, x, y, color):
        self.x = x
        self.y = y
        self.color = color
        self.score = 0
    def __str__(self):
       return str(self.__class__) + ": " + str(self.__dict__)

def pong_start():
    display_clear()
    blue_paddle = Paddle(180, 48, BLU)
    draw_rectangle(blue_paddle.x, blue_paddle.y, 10, 40, blue_paddle.color)
    red_paddle = Paddle(55, 48, RED)
    draw_rectangle(red_paddle.x, red_paddle.y, 10, 40, red_paddle.color)
    ball = None
    display.update()
    while True:
        if ball == None:
            ball = Ball(119, 66)
            draw_circle(ball.x, ball.y, 4, WTE)
        if display.is_pressed(display.BUTTON_A) and display.is_pressed(display.BUTTON_B):
            break
        elif display.is_pressed(display.BUTTON_A) and display.is_pressed(display.BUTTON_X):
            move_right(red_paddle)
            move_right(blue_paddle)
        elif display.is_pressed(display.BUTTON_A) and display.is_pressed(display.BUTTON_Y):
            move_right(red_paddle)
            move_left(blue_paddle)
        elif display.is_pressed(display.BUTTON_B) and display.is_pressed(display.BUTTON_X):
            move_left(red_paddle)
            move_right(blue_paddle)
        elif display.is_pressed(display.BUTTON_B) and display.is_pressed(display.BUTTON_Y):
            move_left(red_paddle)
            move_left(blue_paddle)
        elif display.is_pressed(display.BUTTON_A):
            move_right(red_paddle)
        elif display.is_pressed(display.BUTTON_B):
            move_left(red_paddle)
        elif display.is_pressed(display.BUTTON_X):
            move_right(blue_paddle)
        elif display.is_pressed(display.BUTTON_Y):
            move_left(blue_paddle)
        ball = move_ball(ball, red_paddle, blue_paddle)
        #print("")
        #print(red_paddle.x, red_paddle.y)
        #print(ball.x, ball.y)
        #print(blue_paddle.x, blue_paddle.y)
        if ball.x < -3:
            blue_paddle.score = blue_paddle.score + 1
            ball = None
        elif ball.x > 239:
            red_paddle.score = red_paddle.score + 1
            ball = None
        show_scores(red_paddle.score, blue_paddle.score)
        if red_paddle.score > 9 or blue_paddle.score > 9:
            break
        utime.sleep(.1)


def move_ball(ball, red_paddle, blue_paddle):
    if ball.x > 171: # contact blue?
        # check collision
        right_edge = blue_paddle.y
        left_edge = right_edge + 40
        if ball.y + 2 > left_edge:
            pass
        elif ball.y-2 < right_edge:
            pass
        else:
            ball.direction = 1
    if ball.x < 77: # contact red?
        # check collision
        right_edge = red_paddle.y
        left_edge = right_edge + 40
        if ball.y + 2 > left_edge:
            pass
        elif ball.y-2 < right_edge:
            pass
        else:
            ball.direction = 0
    draw_circle(ball.x, ball.y, 4, BLK)
    if ball.direction == 0:
        ball.x = ball.x+6
    else:
        ball.x = ball.x-6
    draw_circle(ball.x, ball.y, 4, WTE)
    display.update()
    return ball


def move_left(paddle):
    if (paddle.y+5) < 98:
        draw_rectangle(paddle.x, paddle.y, 10, 40, BLK)
        paddle.y = paddle.y+5
        draw_rectangle(paddle.x, paddle.y, 10, 40, paddle.color)
        display.update()
    return paddle


def move_right(paddle):
    if (paddle.y-5) > 0:
        draw_rectangle(paddle.x, paddle.y, 10, 40, BLK)
        paddle.y = paddle.y-5
        draw_rectangle(paddle.x, paddle.y, 10, 40, paddle.color)
        display.update()
    return paddle


def show_scores(red_score, blue_score):
    draw_rectangle(0, 121, 10, 10, BLK)
    r = ""+str(red_score)
    draw_text(r, 0, 121, 239, 2, RED)
    draw_rectangle(230, 0, 10, 10, BLK)
    b = ""+str(blue_score)
    draw_text(b, 230, 0, 239, 2, BLU)
 

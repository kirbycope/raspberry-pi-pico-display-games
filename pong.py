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


def determine_angle(ball, paddle):
    diff = (ball.y+2)/(paddle.y+20)
    if diff == 1:
        return 0
    elif diff < 1.1:
        return 2
    elif diff < 1:
        return 1
    elif diff > 1:
        return -1


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
        if ball.x < 50:
            blue_paddle.score = blue_paddle.score + 1
            draw_circle(ball.x, ball.y, 4, BLK)
            ball = None
        elif ball.x > 190:
            red_paddle.score = red_paddle.score + 1
            draw_circle(ball.x, ball.y, 4, BLK)
            ball = None
        show_scores(red_paddle.score, blue_paddle.score)
        if red_paddle.score == 3:
            display_clear()
            draw_text("Red wins!", 6, 22, 239, 3, RED)
            display.update()
            press_any_key()
            break
        elif blue_paddle.score == 3:
            display_clear()
            draw_text("Blue wins!", 6, 22, 239, 3, BLU)
            display.update()
            press_any_key()
            break
        utime.sleep(.1)


def move_ball(ball, red_paddle, blue_paddle):
    if ball.angle == None:
        ball.angle = 0
    if ball.x > 171:
        right_edge = blue_paddle.y
        left_edge = right_edge + 40
        if ball.y + 2 > left_edge:
            pass
        elif ball.y-2 < right_edge:
            pass
        else:
            ball.direction = 1
            ball.angle = determine_angle(ball, blue_paddle) * -1
    if ball.x < 77:  # contact red?
        right_edge = red_paddle.y
        left_edge = right_edge + 40
        if ball.y + 2 > left_edge:
            pass
        elif ball.y-2 < right_edge:
            pass
        else:
            ball.direction = 0
            ball.angle = determine_angle(ball, red_paddle) * -1
    draw_circle(ball.x, ball.y, 4, BLK)
    if ball != None:
        if ball.y < 2 or ball.y > 137:
            ball.angle = ball.angle * -1
        if ball.direction == 0:
            ball.x = ball.x+6
        else:
            ball.x = ball.x-6
        ball.y = ball.y+ball.angle
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

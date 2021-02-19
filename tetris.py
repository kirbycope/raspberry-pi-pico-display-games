import random
from helpers import *

column_start_pixel = [0, 108, 98, 88, 78, 68, 58, 48, 38, 28, 18]
row_start_pixel = [0, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190, 200, 210]

class FailingPiece:
    def __init__(self):
        self.column = 5 # of 10
        self.row = 1 # of 18
        self.shape = random.randint(1, 7) #1=i,2=o,3=t,4=j,5=l,6=s,7=z
        self.orientation = 1 # of 4
        if self.shape == 1:
            self.color = LBL
        elif self.shape == 2:
            self.color = YEL
        elif self.shape == 3:
            self.color = PUR
        elif self.shape == 4:
            self.color = DBL
        elif self.shape == 5:
            self.color = ORG
        elif self.shape == 6:
            self.color = GRN
        else:
            self.color = RED

def draw_brick_block(x, y):
    # draw the 10x10 gray base
    draw_rectangle(x, y, 10, 10, GRY)
    # draw the top horizontal line
    draw_line(x+3, y, x+3, y+10, BLK)
    # draw the bottom horizontal line 
    draw_line(x+8, y, x+8, y+10, BLK)
    # draw the top right vertical line
    draw_line(x, y+3, x+3, y+3, BLK)
    # draw the top left vertical line
    draw_line(x, y+8, x+3, y+8, BLK)
    # draw the middle right vertical line
    draw_line(x+3, y, x+8, y, BLK)
    # draw the middle left vertical line
    draw_line(x+3, y+5, x+8, y+5, BLK)
    # draw the bottom right "line"
    draw_pixel(x+9, y+3, BLK)
    # draw the bottom left "line"
    draw_pixel(x+9, y+8, BLK)

def draw_brick_borders():
    # Draw right side brick border
    for x in range(31, 209, 10):
        draw_brick_block(x, 1)
    for x in range(31, 209, 10):
        draw_brick_block(x, 10)
    # Draw left side brick border
    for x in range(31, 209, 10):
        draw_brick_block(x, 118)
    for x in range(31, 209, 10):
        draw_brick_block(x, 128)
    # Clear any brick overflow on the playfield
    draw_rectangle(31, 18, 180, 100, BLK)

def show_control_labels():
    # Clear any overflow on the top
    draw_rectangle(1, 1, 30, display_height, BLK)
    # Upper-bottom border
    draw_line(30, 1, 30, display_height, WTE)
    # Upper divider
    draw_line(1, 67, 30, 67, WTE)
    # B-button label
    draw_line(10, 98, 20, 98, WTE)
    draw_line(20, 98, 17, 101, WTE)
    draw_line(20, 98, 17, 95, WTE)
    # A-button label
    draw_ring(15, 30, 5, WTE)
    draw_pixel(16, 25, BLK)
    draw_pixel(15, 25, BLK)
    draw_line(14, 26, 14, 27, WTE)
    draw_line(12, 25, 13, 25, WTE)
    # Clear any overflow on the bottom
    draw_rectangle(211, 1, 30, display_height, BLK)
    # Y-button label
    draw_line(230, 93, 230, 103, WTE)
    draw_line(231, 101, 233, 100, WTE)
    draw_line(229, 101, 227, 100, WTE)
    # X-button label
    draw_line(230, 27, 230, 37, WTE)
    draw_line(231, 28, 233, 30, WTE)
    draw_line(229, 28, 227, 30, WTE)
    # Lower-top border
    draw_line(211, 1, 211, display_height, WTE)
    # Lower divider
    draw_line(211, 67, 239, 67, WTE)
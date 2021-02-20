import picodisplay as display
import utime
import helpers


def clip(section):
    if section == 0:
        display.set_clip(1, 1, 120, 67)  # only draw in the top-right section
    elif section == 1:
        # only draw in the bottom-right section
        display.set_clip(120, 1, 120, 67)
    elif section == 2:
        # only draw in the bottom-left section
        display.set_clip(120, 67, 120, 67)
    else:
        display.set_clip(1, 67, 120, 67)  # only draw in the top-left section
        section = -1
    return section+1


def rainbow_wheel(width, height):
    section = 0
    while True:
        display.set_pen(255, 0, 0)
        display.circle(width, height, 66)
        section = clip(section)
        display.set_pen(0, 255, 0)
        display.circle(width, height, 66)
        display.update()
        utime.sleep(0.1)
        if display.is_pressed(display.BUTTON_A):
            break


helpers.init_display()
rainbow_wheel(120, 66)

# https://pillow.readthedocs.io/en/stable/index.html
from PIL import Image, ImageOps


def rgb2hex(r, g, b):  # https://stackoverflow.com/a/47496156/1106708
    return f'#{int(round(r)):02x}{int(round(g)):02x}{int(round(b)):02x}'


def hex_to_rgb(hex):  # https://gist.github.com/matthewkremer/3295567#gistcomment-3098081
    hex = hex.lstrip('#')
    hlen = len(hex)
    return tuple(int(hex[i:i + hlen // 3], 16) for i in range(0, hlen, hlen // 3))


def get_repeating_pattern(s):  # https://stackoverflow.com/a/29489919/1106708
    i = (s+s).find(s, 1, -1)
    return s if i == -1 else s[:i]


image_file_name = "doom_shotgun.bmp"
py_file_name = image_file_name.split(".")[0]
im = Image.open(image_file_name)
im_mirror = ImageOps.mirror(im)
pix = im_mirror.load()
h = im.size[0]
w = im.size[1]
with open(py_file_name + ".py", 'a') as the_file:
    the_file.truncate(0)
    the_file.write("img_array = [")
    for x in range(h):
        for y in range(w):
            rgb = pix[x, y]
            if rgb != (0, 0, 0):
                the_file.write("\"" + str(x) + "," + str(y) + "," +
                               str(rgb[0]) + "," + str(rgb[1]) + "," + str(rgb[2]) + "\",")
    the_file.write("]")

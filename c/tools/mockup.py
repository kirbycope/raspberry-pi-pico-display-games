"""Draws a Pico Display Pack around a screen image, for previews and the trailer.

    from mockup import device, read_recording

The board is drawn rather than photographed: a black PCB with gold mounting
holes, the four tactile buttons with their letters, the RGB LED, and the
screen scaled up by a whole number so every pixel stays square. A button
that is held is drawn pressed and the LED glows the colour the app set,
both taken from a simulator recording (PDG_RECORD, see hal_sim.c).

Button positions follow the real board: in landscape A and B are left of the
screen and X and Y right of it; held upright (USB at the top) B and A are
above the screen and Y and X below it.
"""

import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

BTN_A, BTN_B, BTN_X, BTN_Y = 1, 2, 4, 8

PCB = (22, 22, 26)
PCB_EDGE = (44, 44, 52)
GOLD = (196, 160, 80)
CAP = (236, 226, 204)
CAP_DOWN = (255, 214, 120)
SILK = (225, 225, 230)


def rgb565_to_rgb(raw, w, h):
    px = np.frombuffer(raw, "<u2").reshape(h, w).astype(np.uint32)
    r = ((px >> 11) & 31) * 255 // 31
    g = ((px >> 5) & 63) * 255 // 63
    b = (px & 31) * 255 // 31
    return np.stack([r, g, b], axis=-1).astype(np.uint8)


def read_recording(path):
    """Yields (ms, rgb array, buttons, (r, g, b)) for every frame in a PDG_RECORD file."""
    with open(path, "rb") as f:
        while True:
            hdr = f.read(16)
            if len(hdr) < 16:
                return
            ms, w, h, buttons, r, g, b = struct.unpack("<IHHBBBB4x", hdr)
            raw = f.read(w * h * 2)
            if len(raw) < w * h * 2:
                return
            yield ms, rgb565_to_rgb(raw, w, h), buttons, (r, g, b)


def _font(size):
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    return ImageFont.truetype(os.path.join(here, "..", "assets", "fonts", "Kenney Mini.ttf"), size)


def device(screen, scale, buttons=0, led=(0, 0, 0), side=16, end=58):
    """The board around screen (an H x W x 3 array), drawn at scale pixels per
    screen pixel. side and end are the board's margins in screen pixels: end on
    the two sides that carry the buttons."""
    h, w = screen.shape[:2]
    portrait = h > w
    s = scale
    if portrait:
        bw, bh = (w + 2 * side) * s, (h + 2 * end) * s
        sx, sy = side * s, end * s
    else:
        bw, bh = (w + 2 * end) * s, (h + 2 * side) * s
        sx, sy = end * s, side * s

    im = Image.new("RGBA", (bw, bh), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    r = 7 * s
    d.rounded_rectangle((0, 0, bw - 1, bh - 1), r, fill=PCB, outline=PCB_EDGE, width=max(1, s))

    # Mounting holes at the corners.
    for cx, cy in ((r, r), (bw - r, r), (r, bh - r), (bw - r, bh - r)):
        d.ellipse((cx - 3.2 * s, cy - 3.2 * s, cx + 3.2 * s, cy + 3.2 * s), fill=GOLD)
        d.ellipse((cx - 1.8 * s, cy - 1.8 * s, cx + 1.8 * s, cy + 1.8 * s), fill=(8, 8, 10))

    # The glass: a dark border round the active area.
    g = 3 * s
    d.rounded_rectangle((sx - g, sy - g, sx + w * s + g - 1, sy + h * s + g - 1), 2 * s, fill=(6, 6, 8))
    pic = Image.fromarray(screen, "RGB").resize((w * s, h * s), Image.NEAREST)
    im.paste(pic, (sx, sy))

    # Buttons, each a square switch with a round cap and its letter beside it.
    def button(cx, cy, letter, down, label_dx, label_dy):
        k = 11 * s
        d.rounded_rectangle((cx - k, cy - k, cx + k, cy + k), 2 * s, fill=(170, 170, 176), outline=(120, 120, 128), width=max(1, s // 2))
        c = 7 * s
        off = s if down else 0
        d.ellipse((cx - c, cy - c + off, cx + c, cy + c + off), fill=CAP_DOWN if down else CAP, outline=(150, 140, 120), width=max(1, s // 2))
        lr = 5 * s
        lx, ly = cx + label_dx, cy + label_dy
        d.ellipse((lx - lr, ly - lr, lx + lr, ly + lr), fill=CAP_DOWN if down else SILK)
        f = _font(8 * s)
        d.text((lx, ly + s // 2), letter, font=f, fill=(20, 20, 24), anchor="mm")

    e = end * s
    if portrait:
        top, bottom = e // 2, bh - e // 2
        left, right = int(bw * 0.24), int(bw * 0.76)
        button(left, top, "B", buttons & BTN_B, -16 * s, 0)
        button(right, top, "A", buttons & BTN_A, 16 * s, 0)
        button(left, bottom, "Y", buttons & BTN_Y, -16 * s, 0)
        button(right, bottom, "X", buttons & BTN_X, 16 * s, 0)
        led_xy = (bw // 2, bottom)
    else:
        lx_, rx_ = e // 2, bw - e // 2
        top, bottom = int(bh * 0.27), int(bh * 0.73)
        button(lx_, top, "A", buttons & BTN_A, 0, -15 * s)
        button(lx_, bottom, "B", buttons & BTN_B, 0, 15 * s)
        button(rx_, top, "X", buttons & BTN_X, 0, -15 * s)
        button(rx_, bottom, "Y", buttons & BTN_Y, 0, 15 * s)
        led_xy = (rx_, bh // 2)

    # The RGB LED: a small white package that glows the app's colour. Apps
    # drive it dimly, so the glow is boosted to read on screen.
    lx, ly = led_xy
    q = 3 * s
    d.rectangle((lx - q, ly - q, lx + q, ly + q), fill=(230, 230, 225), outline=(150, 150, 150))
    glow = tuple(min(255, v * 4) for v in led)
    if max(glow) > 12:
        halo = Image.new("RGBA", im.size, (0, 0, 0, 0))
        hd = ImageDraw.Draw(halo)
        hr = 12 * s
        hd.ellipse((lx - hr, ly - hr, lx + hr, ly + hr), fill=glow + (min(255, max(glow) + 40),))
        halo = halo.filter(ImageFilter.GaussianBlur(5 * s))
        im.alpha_composite(halo)
        d.rectangle((lx - q, ly - q, lx + q, ly + q), fill=tuple(min(255, 150 + v // 2) for v in glow))
    return im


def board_size(screen_w, screen_h, scale, side=16, end=58):
    if screen_h > screen_w:
        return (screen_w + 2 * side) * scale, (screen_h + 2 * end) * scale
    return (screen_w + 2 * end) * scale, (screen_h + 2 * side) * scale

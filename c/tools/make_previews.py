"""Draws the 960x540 preview images: one per app and one for the repository.

    python c/tools/make_previews.py

Frames come straight from scripted simulator runs (c/trailer/takes.py), so a
preview always shows what the current firmware draws. Screens are scaled by
whole numbers so every pixel stays square. Writes c/docs/previews/<app>.png,
the repository's preview.png at the top level, and refreshes the launcher
screenshot c/docs/menu.png.
"""

import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

TOOLS = os.path.dirname(os.path.abspath(__file__))
C = os.path.dirname(TOOLS)
REPO = os.path.dirname(C)
sys.path.insert(0, os.path.join(C, "trailer"))
sys.path.insert(0, TOOLS)

import takes  # noqa: E402
from mockup import device, read_recording  # noqa: E402

W, H = 960, 540
FONT_BLOCKS = os.path.join(C, "assets", "fonts", "Kenney Blocks.ttf")
FONT_MINI = os.path.join(C, "assets", "fonts", "Kenney Mini.ttf")
OUT = os.path.join(C, "docs", "previews")

# file, title, tagline, accent, take, frame times in seconds (two for an upright app)
PREVIEWS = [
    ("bad_apple", "BAD APPLE!!", "The whole video, 30 fps, from flash", (230, 230, 230), "badapple", [53.0]),
    ("tetris", "TETRIS", "Wall kicks, ghost piece, 7-bag", (40, 190, 230), "tetris", [14.45, 46.0]),
    ("flappy", "FLAPPY BIRD", "Any button flaps", (250, 190, 40), "flappy", [13.0, 31.7]),
    ("pet", "VIRTUAL PET", "Hatch and raise a dragon", (240, 110, 150), "pet", [128.4]),
    ("pomodoro", "POMODORO", "Keeps time while you play", (230, 60, 50), "pomodoro", [437.5]),
    ("life", "GAME OF LIFE", "3,600 cells, coloured by age", (80, 220, 120), "life", [4.0]),
    ("dvd", "BOUNCING DVD", "Waiting for the corner", (170, 60, 230), "dvd", [26.0]),
    ("pico_quest", "PICO QUEST", "A pocket RPG with a dragon at the end", (60, 160, 80), "rpg", [4.6, ("dragon", 7.95)]),
]


def frame_at(take, seconds):
    rec = os.path.join(takes.WORK, f"preview_{take}.rec")
    ms = int(seconds * 1000)
    takes.record(take, rec, start=ms - 40, stop=ms + 40)
    frames = list(read_recording(rec))
    os.remove(rec)
    best = min(frames, key=lambda f: abs(f[0] - ms))
    return best[1]


def background(accent):
    y = np.linspace(0, 1, H)[:, None, None]
    field = np.array(accent) * 0.32 * (1 - y) + np.array([8, 8, 14]) * y
    img = np.broadcast_to(field, (H, W, 3)).copy()
    yy, xx = np.mgrid[0:H, 0:W]
    d = np.sqrt(((xx - W * 0.6) / 520) ** 2 + ((yy - H * 0.6) / 360) ** 2)
    img += np.clip(1 - d, 0, 1)[..., None] ** 2 * np.array(accent) * 0.35
    img[((xx % 16) == 0) & ((yy % 16) == 0)] += 16
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB").convert("RGBA")


def bezel(screen, scale, pad=8):
    """The screen at a whole-number scale in a slim rounded black frame."""
    h, w = screen.shape[:2]
    im = Image.new("RGBA", (w * scale + 2 * pad, h * scale + 2 * pad), (0, 0, 0, 0))
    ImageDraw.Draw(im).rounded_rectangle((0, 0, im.width - 1, im.height - 1), pad + 2, fill=(14, 14, 18, 255),
                                         outline=(60, 60, 70, 255), width=2)
    im.paste(Image.fromarray(screen).resize((w * scale, h * scale), Image.NEAREST), (pad, pad))
    return im


def drop(canvas, im, x, y):
    shadow = Image.new("RGBA", im.size, (0, 0, 0, 0))
    shadow.paste((0, 0, 0, 140), (0, 0, im.width, im.height), im.getchannel("A"))
    canvas.alpha_composite(shadow, (x + 8, y + 12))
    canvas.alpha_composite(im, (x, y))


def title(d, x, y, text, sub, accent, anchor="la", size=52):
    dark = (12, 10, 20, 255)
    d.text((x, y), text, font=ImageFont.truetype(FONT_BLOCKS, size), fill=tuple(accent) + (255,), anchor=anchor,
           stroke_width=5, stroke_fill=dark)
    if sub:
        sy = y + size + 14 if anchor[1] == "a" else y + 40
        d.text((x, sy), sub, font=ImageFont.truetype(FONT_MINI, 24), fill=(240, 240, 245, 255), anchor=anchor,
               stroke_width=3, stroke_fill=dark)


def app_preview(name, text, sub, accent, take, times):
    frames = [frame_at(*t) if isinstance(t, tuple) else frame_at(take, t) for t in times]
    canvas = background(accent)
    d = ImageDraw.Draw(canvas)
    if frames[0].shape[0] < frames[0].shape[1]:
        # Landscape: the title across the top, the screen at three times size below it.
        title(d, W // 2, 12, text, None, accent, anchor="ma", size=44)
        d.text((W // 2, 66), sub, font=ImageFont.truetype(FONT_MINI, 24), fill=(240, 240, 245, 255), anchor="ma",
               stroke_width=3, stroke_fill=(12, 10, 20, 255))
        b = bezel(frames[0], 3)
        drop(canvas, b, (W - b.width) // 2, H - b.height - 14)
    else:
        # Upright: the title on the left, two moments side by side at twice size.
        lines = text.split(" ")
        y = 150 if len(lines) == 1 else 120
        for i, word in enumerate(lines):
            title(d, 40, y + i * 66, word, None, accent, size=52)
        # The tagline wraps to the column left of the screens.
        f = ImageFont.truetype(FONT_MINI, 24)
        words, rows = sub.split(" "), [""]
        for w in words:
            rows[-1] = (rows[-1] + " " + w).strip() if f.getlength(rows[-1] + " " + w) <= 330 else rows[-1]
            if f.getlength(rows[-1]) and not rows[-1].endswith(w):
                rows.append(w)
        for i, row in enumerate(rows):
            d.text((42, y + len(lines) * 66 + 14 + i * 32), row, font=f, fill=(240, 240, 245, 255),
                   stroke_width=3, stroke_fill=(12, 10, 20, 255))
        x = W - 30
        for f in reversed(frames):
            b = bezel(f, 2)
            x -= b.width
            drop(canvas, b, x, (H - b.height) // 2)
            x -= 22
    path = os.path.join(OUT, f"{name}.png")
    save(canvas, path)


def repo_preview():
    menu = frame_at("menu", 3.1)
    canvas = background((120, 90, 230))
    d = ImageDraw.Draw(canvas)
    title(d, W // 2, 26, "PICO DISPLAY GAMES", "8 games and toys in C for Raspberry Pi Pico", (150, 120, 255), anchor="ma", size=50)
    board = device(menu, 2, 0, (0, 0, 0), end=44)
    drop(canvas, board, (W - board.width) // 2, H - board.height - 30)
    save(canvas, os.path.join(REPO, "preview.png"))
    # The README's launcher screenshot, at three times size like the others.
    Image.fromarray(menu).resize((720, 405), Image.NEAREST).save(os.path.join(C, "docs", "menu.png"), optimize=True)


def save(canvas, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    canvas.convert("RGB").save(path, optimize=True)
    print("wrote", path)


def main():
    takes.WORK = os.path.join(C, "trailer", "work", "previews")
    os.makedirs(takes.WORK, exist_ok=True)
    for p in PREVIEWS:
        app_preview(*p)
    repo_preview()


if __name__ == "__main__":
    main()

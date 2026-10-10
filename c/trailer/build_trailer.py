"""Builds the vertical (YouTube Shorts) trailer from simulator footage.

    python c/trailer/build_trailer.py --music Hero.wav --sfx <folder with the two SFX packs>

Every shot is a window of a scripted simulator run (takes.py), drawn into a
Pico Display Pack mockup (tools/mockup.py) at 1080x1920 and 30 fps, with the
buttons going down as the script presses them and the LED glowing. Captions
are drawn here in the project's own Kenney fonts. The shots, cards, music and
sound effects then go to the trailer skill's assemble.py, which cuts on the
beat, ducks the music under the effects and masters to -14 LUFS.

The footage is the simulator's, frame for frame what the firmware draws; the
board around it is drawn, not filmed.

Nothing here is committed but the scripts: recordings, frames and the result
go to c/trailer/work/.
"""

import argparse
import glob
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
C = os.path.dirname(HERE)
WORK = os.path.join(HERE, "work")
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(C, "tools"))

import takes  # noqa: E402
from mockup import BTN_A, BTN_B, BTN_X, BTN_Y, device, read_recording  # noqa: E402

W, H, FPS = 1080, 1920, 30
ASSEMBLE = os.path.expanduser("~/GitHub/.claude/skills/trailer/assemble.py")
FONT_BLOCKS = os.path.join(C, "assets", "fonts", "Kenney Blocks.ttf")
FONT_MINI = os.path.join(C, "assets", "fonts", "Kenney Mini.ttf")

# take, in (s), beats, accent, big caption, small caption, button sound, events [(seconds into the shot, sound, dB)]
SHOTS = [
    ("dragon", 6.55, 6, (230, 70, 40), "AN RPG", "on a Raspberry Pi Pico", "ui", []),
    ("menu", 0.45, 8, (120, 90, 230), "PICO DISPLAY\nGAMES", "8 apps. 1 Pico. Written in C.", "ui", []),
    ("tetris", 12.6, 6, (40, 190, 230), "TETRIS", "wall kicks, ghost piece, 7-bag", None, []),
    ("flappy", 24.0, 4, (250, 190, 40), "FLAPPY BIRD", "any button flaps", "flap", []),
    ("badapple", 51.6, 6, (230, 230, 230), "BAD APPLE!!", "3:39 of video in 1.4 MB", None, []),
    ("pet", 120.4, 6, (240, 110, 150), "VIRTUAL PET", "hatch and raise a dragon", "ui", []),
    ("life", 0.5, 4, (80, 220, 120), "GAME OF LIFE", "3,600 cells, coloured by age", None, []),
    ("pomodoro", 1498.9, 6, (230, 60, 50), "POMODORO", "keeps time while you play", "ui", []),
    ("dvd", 24.6, 6, (170, 60, 230), "BOUNCING DVD", "it hit the corner", None, []),
    ("rpg", 3.3, 6, (60, 160, 80), "PICO QUEST", "three zones and a dragon", "ui", []),
    ("menu", 3.9, 8, (120, 90, 230), "", "", None, []),
]

# When each shot's event happens, in seconds from the shot's start, measured
# from the recordings: the first frame of the Blaze flash, the four-line
# clear, the egg cracking, "Break time!", the "CORNER!" text and the sword hit.
EVENTS = {
    0: [(1.60, "explosion", -2)],
    2: [(1.59, "success", -2)],
    5: [(1.11, "powerup", -1)],
    7: [(1.74, "achieve", -3)],
    8: [(1.37, "stinger", -3)],
    9: [(1.55, "hit", -2)],
}


def sounds(root):
    retro = os.path.join(root, "Retro Pixel Sound Effect Pack", "AUDIO")
    cine = os.path.join(root, "Cinematic Impacts, Risers, Whooshes, Drops", "SFXs", "wav")
    pick = lambda folder, name: os.path.join(folder, name)
    return {
        "ui": pick(f"{retro}/UI/Select", "SFX_Arcade_Retro_UI_Select_Set_01_Positive_001.wav"),
        "flap": pick(f"{retro}/Jump, Fall, Rise", "SFX_Arcade_Retro_Jump_Set_01_001.wav"),
        "hit": pick(f"{retro}/Hit, Take Damage", "SFX_Arcade_Retro_Hit_Fight_Set_01_003.wav"),
        "explosion": pick(f"{retro}/Explosion", "SFX_Arcade_Retro_Explosion_Set_01_Soft_001.wav"),
        "success": pick(f"{retro}/Success", "SFX_Arcade_Retro_Success_Set_01_001.wav"),
        "achieve": pick(f"{retro}/Success", "SFX_Arcade_Retro_Success_Set_02_Achieve_001.wav"),
        "powerup": pick(f"{retro}/Power up", "SFX_Arcade_Retro_Powerup_Upgrade_Set_01_002.wav"),
        "stinger": pick(f"{retro}/Stinger", "STGR_Arcade_Retro_Stinger_Success_Set_01_Dry_001.wav"),
        "whoosh": pick(f"{cine}/Whooshes", "cinematic_whoosh_a_03.wav"),
        "impact": pick(f"{cine}/Impacts", "cinematic_impact_a_01.wav"),
        "riser": pick(f"{cine}/Risers", "cinematic_riser_a_02.wav"),
    }


def background(accent):
    """A dark field tinted with the app's colour, a glow behind the board and a faint dot grid."""
    y = np.linspace(0, 1, H)[:, None, None]
    top = np.array(accent) * 0.30
    bottom = np.array([8, 8, 14])
    field = top * (1 - y) + bottom * y
    img = np.broadcast_to(field, (H, W, 3)).copy()
    yy, xx = np.mgrid[0:H, 0:W]
    d = np.sqrt(((xx - W / 2) / 620) ** 2 + ((yy - H * 0.55) / 900) ** 2)
    img += np.clip(1 - d, 0, 1)[..., None] ** 2 * np.array(accent) * 0.35
    dots = ((xx % 24) == 0) & ((yy % 24) == 0)
    img[dots] += 18
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")


def place(screen_shape):
    h, w = screen_shape
    if h > w:
        scale, end = 4, 58
        bw, bh = (w + 32) * scale, (h + 2 * end) * scale
        return scale, end, ((W - bw) // 2, 400)
    scale, end = 3, 44
    bw, bh = (w + 2 * end) * scale, (h + 32) * scale
    return scale, end, ((W - bw) // 2, 820)


def compose_shot(i, shot, length):
    take, start, _, accent = shot[:4]
    rec = os.path.join(WORK, f"shot{i:02d}.rec")
    takes.record(take, rec, start=int(start * 1000) - 200, stop=int((start + length) * 1000) + 200)
    frames = list(read_recording(rec))
    os.remove(rec)
    bg = background(accent)
    out = os.path.join(WORK, "takes", f"shot{i:02d}.mp4")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    ff = subprocess.Popen(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
                           "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-preset", "medium", "-crf", "14",
                           "-pix_fmt", "yuv420p", out], stdin=subprocess.PIPE)
    presses = []
    held_before = 0
    k = 0
    n = int(round(length * FPS)) + 2
    for f in range(n):
        t = start * 1000 + f * 1000 / FPS
        while k + 1 < len(frames) and frames[k + 1][0] <= t:
            k += 1
        ms, rgb, buttons, led = frames[k]
        new = buttons & ~held_before
        if new and f > 0:
            presses.append((f / FPS, new))
        held_before = buttons
        scale, end, (x, y) = place(rgb.shape[:2])
        board = device(rgb, scale, buttons, led, end=end)
        canvas = bg.copy().convert("RGBA")
        shadow = Image.new("RGBA", board.size, (0, 0, 0, 0))
        shadow.paste((0, 0, 0, 150), (0, 0, board.width, board.height), board.getchannel("A"))
        canvas.alpha_composite(shadow, (x + 14, y + 22))
        canvas.alpha_composite(board, (x, y))
        ff.stdin.write(canvas.convert("RGB").tobytes())
    ff.stdin.close()
    ff.wait()
    return presses


def text_card(name, big, small, accent, end=False):
    """A transparent 1080x1920 card: the big line in Kenney Blocks, the small one in Kenney Mini."""
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    dark = (12, 10, 20, 255)
    acc = tuple(accent) + (255,)
    if end:
        d.rectangle((0, 0, W, H), fill=(8, 8, 14, 205))
        f1 = ImageFont.truetype(FONT_BLOCKS, 100)
        f2 = ImageFont.truetype(FONT_MINI, 56)
        f3 = ImageFont.truetype(FONT_MINI, 40)
        d.text((W // 2, 640), "PICO DISPLAY", font=f1, fill=acc, anchor="mm", stroke_width=10, stroke_fill=dark)
        d.text((W // 2, 790), "GAMES", font=f1, fill=acc, anchor="mm", stroke_width=10, stroke_fill=dark)
        d.text((W // 2, 950), "8 games and toys in C", font=f2, fill=(255, 255, 255, 255), anchor="mm")
        d.text((W // 2, 1020), "for Raspberry Pi Pico and", font=f2, fill=(255, 255, 255, 255), anchor="mm")
        d.text((W // 2, 1090), "Pimoroni Pico Display Pack", font=f2, fill=(255, 255, 255, 255), anchor="mm")
        d.text((W // 2, 1260), "Free and open source on GitHub", font=f3, fill=(200, 200, 215, 255), anchor="mm")
        d.text((W // 2, 1320), "kirbycope/raspberry-pi-pico-display-games", font=f3, fill=acc, anchor="mm")
    else:
        lines = big.split("\n")
        size = 104 if max(map(len, lines)) <= 12 else 88
        f1 = ImageFont.truetype(FONT_BLOCKS, size)
        f2 = ImageFont.truetype(FONT_MINI, 56)
        y = 200 if len(lines) == 1 else 150
        for line in lines:
            d.text((W // 2, y), line, font=f1, fill=acc, anchor="mm", stroke_width=9, stroke_fill=dark)
            y += size + 14
        if small:
            d.text((W // 2, y + 6), small, font=f2, fill=(255, 255, 255, 255), anchor="mm", stroke_width=5, stroke_fill=dark)
    path = os.path.join(WORK, "cards", f"{name}.png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im.save(path)
    return name


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--music", required=True, help="Polyshade Chiptunes 'Hero' (wav)")
    ap.add_argument("--sfx", required=True, help="folder holding the Retro Pixel and Cinematic packs")
    ap.add_argument("--out", default=os.path.join(WORK, "pico_display_games_short.mp4"))
    ap.add_argument("--only", type=int, nargs="*", help="recompose only these shots")
    o = ap.parse_args()
    os.makedirs(WORK, exist_ok=True)
    sfx = sounds(o.sfx)

    # Hero is a 12-bar loop of 22 s; two passes cover the trailer.
    music = os.path.join(WORK, "music.wav")
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-stream_loop", "1", "-i", o.music, "-c:a", "pcm_s16le", music], check=True)
    beat = 60 / 130.0

    edit = {"size": [W, H], "music": {"file": music, "start": "beat", "bpm": 130, "fade_out": 1.5},
            "duck_db": 6, "sfx_gain_db": -2, "shots": [], "sfx": []}
    presses_path = os.path.join(WORK, "presses.json")
    all_presses = json.load(open(presses_path)) if os.path.exists(presses_path) else {}
    for i, shot in enumerate(SHOTS):
        take, start, beats, accent, big, small, press_sound, _ = shot
        length = beats * beat
        if o.only is None or i in o.only:
            all_presses[str(i)] = compose_shot(i, shot, length)
            print(f"shot {i}: {take} at {start}s, {length:.2f}s, {len(all_presses[str(i)])} presses")
        last = i == len(SHOTS) - 1
        card = text_card(f"card{i:02d}", big, small, accent, end=last) if (big or last) else None
        s = {"take": f"shot{i:02d}", "in": 0, "beats": beats, "motion": "none"}
        if card:
            s["card"] = card
            s["card_in"] = 0.0 if i else 0.25
        if i == 0:
            s["fade_in"] = 0.15
        if last:
            s["card_in"] = 0.3
            s["fade_out"] = 0.8
        edit["shots"].append(s)
        if press_sound:
            for t, _ in all_presses.get(str(i), []):
                edit["sfx"].append({"file": sfx[press_sound], "shot": i, "offset": t, "gain_db": -12 if press_sound == "ui" else -8})
        for t, name, db in EVENTS.get(i, []):
            edit["sfx"].append({"file": sfx[name], "shot": i, "offset": t, "gain_db": db})
    json.dump(all_presses, open(presses_path, "w"))

    # The title lands on an impact with a riser into it; whooshes carry two scene changes.
    edit["sfx"] += [
        {"file": sfx["riser"], "shot": 1, "offset": -2.6, "gain_db": -10},
        {"file": sfx["impact"], "shot": 1, "offset": 0.0, "gain_db": -5},
        {"file": sfx["whoosh"], "shot": 2, "offset": -0.35, "gain_db": -9},
        {"file": sfx["whoosh"], "shot": 4, "offset": -0.35, "gain_db": -9},
        {"file": sfx["whoosh"], "shot": 9, "offset": -0.35, "gain_db": -9},
        {"file": sfx["impact"], "shot": 10, "offset": 0.3, "gain_db": -6},
    ]
    edit_path = os.path.join(WORK, "edit.json")
    json.dump(edit, open(edit_path, "w"), indent=1)
    cut = os.path.join(WORK, "assembled.mp4")
    subprocess.run([sys.executable, ASSEMBLE, edit_path, "--takes", os.path.join(WORK, "takes"),
                    "--cards", os.path.join(WORK, "cards"), "--out", cut], check=True)
    # Keep only the picture and the sound; the concat step leaves a data stream behind.
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", cut, "-map", "0:v", "-map", "0:a", "-c", "copy",
                    "-map_chapters", "-1", "-movflags", "+faststart", o.out], check=True)
    print("trailer:", o.out)


if __name__ == "__main__":
    main()

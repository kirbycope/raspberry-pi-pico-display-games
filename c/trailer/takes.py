"""The trailer's takes: what each one runs in the simulator, and a survey of it.

    python c/trailer/takes.py survey [name ...]    # contact sheets of whole runs, to choose in-points
    python c/trailer/takes.py record name from_ms to_ms

A take is a scripted simulator run (see the top of src/platform/hal_sim.c).
Most replay the scripts the screenshots were made with; their "shot" lines
are dropped. survey records every few frames and lays them out with their
times; record keeps every frame of a window, for composing a shot.
Recordings go to c/trailer/work/, which is git-ignored.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
C = os.path.dirname(HERE)
WORK = os.path.join(HERE, "work")
SIM = os.path.join(C, "build-host", "pdg_sim")

sys.path.insert(0, os.path.join(C, "tools"))

MENU = ["wait 700"] + [x for _ in range(7) for x in ("tap B", "wait 330")] + ["wait 600"] + \
       [x for _ in range(7) for x in ("tap A", "wait 220")] + ["wait 1500"]

# The dragon fight: from camp into the boss battle, a sword hit, Blaze, then more hits.
DRAGON = ["wait 500", "tap A", "wait 800", "tap A", "wait 1300", "tap A", "wait 2600",
          "tap X", "wait 300", "tap A", "wait 500", "tap X", "wait 300", "tap X", "wait 300", "tap A", "wait 3000",
          "tap A", "wait 2600", "tap A", "wait 2600", "tap A", "wait 2600", "tap A", "wait 2600"]

TAKES = {
    # name: (app, env, script file under c/ or inline lines)
    "dragon": ("Pico Quest", {"PDG_RPG_JUMP": "2,11,14"}, DRAGON),
    "rpg": ("Pico Quest", {}, "shots/rpg.txt"),
    "ruins": ("Pico Quest", {"PDG_RPG_JUMP": "1,4,9"}, "shots/rpg_ruins.txt"),
    "menu": (None, {}, MENU),
    "tetris": ("Tetris", {}, "shots/tetris.txt"),
    "flappy": ("Flappy Bird", {}, "shots/flappy.txt"),
    "badapple": ("Bad Apple", {}, ["wait 220000"]),
    "pet": ("Virtual Pet", {}, "shots/pet.txt"),
    "life": ("Game of Life", {}, "shots/life.txt"),
    "pomodoro": ("Pomodoro", {}, "shots/pomodoro.txt"),
    "dvd": ("Bouncing DVD", {"PDG_SEED": "2"}, ["wait 40000"]),
}


def script_lines(spec):
    if isinstance(spec, list):
        return spec
    with open(os.path.join(C, spec)) as f:
        # Keep running a while after the script's last step, so a shot can run on past it.
        return [l.rstrip("\n") for l in f if not l.startswith("shot ") and not l.startswith("quit")] + ["wait 6000"]


def record(name, out, every=1, start=None, stop=None):
    app, env, spec = TAKES[name]
    os.makedirs(WORK, exist_ok=True)
    script = os.path.join(WORK, f"{name}.txt")
    with open(script, "w") as f:
        f.write("\n".join(script_lines(spec) + ["quit"]) + "\n")
    e = dict(os.environ, PDG_SCRIPT=script, PDG_RECORD=out, PDG_RECORD_EVERY=str(every), PDG_SEED="7")
    e.update(env)
    if app:
        e["PDG_APP"] = app
    if start is not None:
        e["PDG_RECORD_FROM"] = str(start)
    if stop is not None:
        e["PDG_RECORD_TO"] = str(stop)
    # Stop the run at the end of the window rather than playing out the script.
    subprocess.run([SIM], env=e, cwd=C, check=True, stdout=subprocess.DEVNULL)
    return out


def survey(name):
    from PIL import Image, ImageDraw
    from mockup import read_recording
    rec = record(name, os.path.join(WORK, f"{name}_survey.rec"), every=15)
    frames = list(read_recording(rec))
    os.remove(rec)
    if not frames:
        print(f"{name}: no frames")
        return
    h, w = frames[0][1].shape[:2]
    cols = 10 if h > w else 6
    step = max(1, len(frames) // 60)
    pick = frames[::step][:60]
    rows = (len(pick) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (w + 4), rows * (h + 14)), (40, 0, 40))
    d = ImageDraw.Draw(sheet)
    for i, (ms, rgb, buttons, led) in enumerate(pick):
        x, y = (i % cols) * (w + 4), (i // cols) * (h + 14)
        if rgb.shape[:2] != (h, w):
            continue
        sheet.paste(Image.fromarray(rgb), (x, y + 12))
        d.text((x + 2, y), f"{ms / 1000:.1f}s", fill=(255, 255, 0))
    path = os.path.join(WORK, f"{name}_survey.png")
    sheet.save(path)
    print(f"{name}: {len(frames)} frames to {frames[-1][0] / 1000:.1f}s, sheet {path}")


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "survey"
    if cmd == "survey":
        for n in sys.argv[2:] or TAKES:
            survey(n)
    elif cmd == "record":
        n, a, b = sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
        print(record(n, os.path.join(WORK, f"{n}.rec"), start=a, stop=b))

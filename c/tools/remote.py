"""Drives a Pico running this firmware over its USB cable, for filming.

    python c/tools/remote.py ping
    python c/tools/remote.py attract                # the apps take turns playing themselves
    python c/tools/remote.py attract Tetris         # one app playing itself until stopped
    python c/tools/remote.py open "Pico Quest"      # open an app as if picked from the menu
    python c/tools/remote.py menu
    python c/tools/remote.py tap A                  # any single remote command
    python c/tools/remote.py play shots/rpg.txt --countdown 5
    python c/tools/remote.py flash build-pico/pico_display_games.uf2

"play" runs a simulator script (wait, hold, release, tap, and remote
commands such as "attract Tetris") in real time against the board; "shot"
lines are skipped. --countdown counts down on the terminal first, so the
camera can start rolling. "flash" restarts the board into its USB drive
mode and copies a UF2 onto it, so it can be reflashed without touching
BOOTSEL once this firmware is on it.

Only the standard library is used. The port is the first /dev/cu.usbmodem*
(macOS) or /dev/ttyACM* (Linux) unless --port says otherwise.
"""

import argparse
import glob
import os
import shutil
import sys
import termios
import time


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
    if not ports:
        sys.exit("no Pico found on USB (is it plugged in and running this firmware?)")
    return ports[0]


class Board:
    def __init__(self, port, baud=115200):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = 0                                        # iflag
        attrs[1] = 0                                        # oflag
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[3] = 0                                        # lflag: raw
        speed = getattr(termios, f"B{baud}")
        attrs[4] = attrs[5] = speed
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        self.buf = b""

    def send(self, line):
        os.write(self.fd, (line.strip() + "\n").encode())

    def replies(self, wait=0.3):
        """Lines the board printed within wait seconds."""
        end, out = time.time() + wait, []
        while time.time() < end:
            try:
                self.buf += os.read(self.fd, 256)
            except BlockingIOError:
                time.sleep(0.01)
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                out.append(line.decode(errors="replace").strip())
        return out

    def close(self):
        os.close(self.fd)


def countdown(seconds):
    for n in range(seconds, 0, -1):
        print(f"  starting in {n}...", flush=True)
        time.sleep(1)
    print("  go", flush=True)


def play(board, path):
    """Runs a simulator script against the board in real time."""
    with open(path) as f:
        lines = [l.strip() for l in f if l.strip() and not l.startswith("#")]
    for line in lines:
        words = line.split()
        cmd = words[0]
        if cmd == "wait":
            time.sleep(int(words[1]) / 1000)
        elif cmd == "tap":
            ms = int(words[2]) if len(words) > 2 else 80
            board.send(line)
            time.sleep(ms / 1000 + 1 / 30)  # as in the simulator, a frame sees it come up again
        elif cmd in ("shot",):
            continue
        elif cmd == "quit":
            break
        else:
            board.send(line)
    board.replies(0.2)


def flash(port, uf2):
    """Restarts the board into its USB drive mode (the SDK's 1200 baud reset)
    and copies the UF2 onto the drive that appears."""
    if port:
        try:
            Board(port, 1200).close()
        except OSError:
            pass
    for _ in range(100):
        drives = [d for d in ("/Volumes/RPI-RP2", "/Volumes/RP2350") if os.path.isdir(d)]
        if drives:
            break
        time.sleep(0.1)
    else:
        sys.exit("the board's USB drive did not appear; hold BOOTSEL while plugging it in")
    print(f"copying {uf2} to {drives[0]}")
    shutil.copy(uf2, drives[0])
    print("done; the board restarts into the new firmware")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", nargs="+", help="ping, attract [app], open <app>, menu, tap A, play <script>, flash <uf2>")
    ap.add_argument("--port")
    ap.add_argument("--countdown", type=int, default=0, help="seconds to count down before starting")
    o = ap.parse_args()
    verb = o.command[0]

    if verb == "flash":
        ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
        flash(o.port or (ports[0] if ports else None), o.command[1])
        return

    board = Board(o.port or find_port())
    board.replies(0.1)  # drop anything left over
    if o.countdown:
        countdown(o.countdown)
    if verb == "play":
        play(board, o.command[1])
    else:
        board.send(" ".join(o.command))
        for line in board.replies(0.5):
            print(line)
    board.close()


if __name__ == "__main__":
    main()

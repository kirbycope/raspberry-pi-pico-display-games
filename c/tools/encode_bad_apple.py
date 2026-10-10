"""Encode the Bad Apple!! video into the 1-bit stream the Pico plays.

    python c/tools/encode_bad_apple.py <video file> [--out c/data/bad_apple.bin]

The video is not in this repository and neither is the output: both stay
on your machine (c/data/ is git-ignored). Any copy of the shadow-art PV
works; the usual one is 480x360 at 30 fps, 6,572 frames. Needs ffmpeg on the
PATH and numpy.

Each frame is scaled to 180x135 (4:3, the panel's full height) and
thresholded to black and white. It is then coded one of four ways, whichever
is smallest for that frame:

    0 direct   the pixels themselves
    1 xor      the pixels XOR the previous frame
    2 row      each row XOR the row above it
    3 xorrow   the previous-frame XOR, then row XOR row above

and the resulting bits are coded as alternating run lengths, starting with
a run of zeros (which may be empty), each run in exponential-Golomb code.
Runs of zeros and runs of ones get their own Golomb order, chosen per frame.

File layout, all integers little-endian:

    "BAD1"  u16 width  u16 height  u16 fps  u16 reserved  u32 frames
    then each frame, starting on a byte boundary:
        2 bits mode, 3 bits order for zero runs, 3 bits order for one runs,
        runs until width*height pixels are covered

Bits are read most significant first within each byte. The decoder is
c/src/apps/bad_apple.c; --verify decodes the output again here and compares.
"""

import argparse
import os
import struct
import subprocess
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def write(self, value, bits):
        for i in range(bits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc = self.n = 0

    def golomb(self, v, k):
        v += 1 << k
        nb = v.bit_length()
        self.write(0, nb - 1 - k)
        self.write(v, nb)

    def align(self):
        if self.n:
            self.write(0, 8 - self.n)


class BitReader:
    def __init__(self, data, pos):
        self.data = data
        self.bit = pos * 8

    def read(self, bits):
        v = 0
        for _ in range(bits):
            byte = self.data[self.bit >> 3]
            v = (v << 1) | ((byte >> (7 - (self.bit & 7))) & 1)
            self.bit += 1
        return v

    def golomb(self, k):
        zeros = 0
        while self.read(1) == 0:
            zeros += 1
        v = (1 << (zeros + k)) | self.read(zeros + k)
        return v - (1 << k)

    def align(self):
        self.bit = (self.bit + 7) & ~7


def golomb_bits(runs, k):
    v = runs + (1 << k)
    return int(np.sum(2 * np.floor(np.log2(v)).astype(np.int64) + 1 - k))


def runs_of(bits):
    flat = bits.reshape(-1).astype(np.int8)
    # Alternating runs starting with zeros: boundaries where the value changes.
    edges = np.flatnonzero(np.diff(flat)) + 1
    starts = np.concatenate(([0], edges))
    ends = np.concatenate((edges, [flat.size]))
    runs = ends - starts
    if flat[0] == 1:
        runs = np.concatenate(([0], runs))
    return runs.astype(np.int64)


def row_delta(b):
    d = b.copy()
    d[1:] ^= b[:-1]
    return d


def undo_row_delta(d):
    b = d.copy()
    for y in range(1, b.shape[0]):
        b[y] ^= b[y - 1]
    return b


def best_order(runs, top):
    if runs.size == 0:
        return 0, 0
    return min((golomb_bits(runs, k), k) for k in range(top))[::-1]


def encode(frames, width, height, fps):
    w = BitWriter()
    w.out += b"BAD1" + struct.pack("<HHHHI", width, height, fps, 0, len(frames))
    prev = np.zeros((height, width), np.uint8)
    modes = [0, 0, 0, 0]
    for cur in frames:
        x = cur ^ prev
        candidates = [cur, x, row_delta(cur), row_delta(x)]
        best = None
        for mode, bits in enumerate(candidates):
            runs = runs_of(bits)
            k0, c0 = best_order(runs[0::2], 8)
            k1, c1 = best_order(runs[1::2], 8)
            cost = c0 + c1
            if best is None or cost < best[0]:
                best = (cost, mode, k0, k1, runs)
        _, mode, k0, k1, runs = best
        modes[mode] += 1
        w.write(mode, 2)
        w.write(k0, 3)
        w.write(k1, 3)
        for i, r in enumerate(runs):
            w.golomb(int(r), k0 if i % 2 == 0 else k1)
        w.align()
        prev = cur
    return bytes(w.out), modes


def decode(data):
    assert data[:4] == b"BAD1"
    width, height, fps, _, count = struct.unpack_from("<HHHHI", data, 4)
    r = BitReader(data, 16)
    prev = np.zeros((height, width), np.uint8)
    for _ in range(count):
        mode, k0, k1 = r.read(2), r.read(3), r.read(3)
        flat = np.zeros(width * height, np.uint8)
        pos, value, i = 0, 0, 0
        while pos < flat.size:
            n = r.golomb(k0 if i % 2 == 0 else k1)
            flat[pos:pos + n] = value
            pos += n
            value ^= 1
            i += 1
        r.align()
        d = flat.reshape(height, width)
        if mode in (2, 3):
            d = undo_row_delta(d)
        cur = d ^ prev if mode in (1, 3) else d
        yield cur
        prev = cur


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("video")
    ap.add_argument("--out", default=os.path.join(ROOT, "data", "bad_apple.bin"))
    ap.add_argument("--width", type=int, default=180)
    ap.add_argument("--height", type=int, default=135)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--threshold", type=int, default=128)
    ap.add_argument("--verify", action="store_true", help="decode the result again and compare")
    args = ap.parse_args()

    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", args.video, "-vf",
         f"fps={args.fps},scale={args.width}:{args.height}:flags=area,format=gray", "-f", "rawvideo", "-"],
        check=True, stdout=subprocess.PIPE).stdout
    size = args.width * args.height
    count = len(raw) // size
    gray = np.frombuffer(raw[:count * size], np.uint8).reshape(count, args.height, args.width)
    frames = list((gray >= args.threshold).astype(np.uint8))

    data, modes = encode(frames, args.width, args.height, args.fps)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(data)
    print(f"{count} frames at {args.width}x{args.height}, {len(data) / 1024:.0f} KB -> {args.out}")
    print(f"modes used: direct {modes[0]}, xor {modes[1]}, row {modes[2]}, xorrow {modes[3]}")

    if args.verify:
        for i, got in enumerate(decode(data)):
            if not np.array_equal(got, frames[i]):
                sys.exit(f"verify failed at frame {i}")
        print("verify: every frame decodes exactly")


if __name__ == "__main__":
    main()

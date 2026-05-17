#!/usr/bin/env python3
#
# Copyright (c) 2026 triaxis s.r.o.
# Licensed under the MIT license. See LICENSE.txt file in the repository root
# for full license information.
#
# tools/test_mono_font_gen.py
#
# Deterministic generator self-test: it round-trips synthetic glyph grids
# through the scheme-U encoder and a reference decoder that mirrors the
# on-target Font::ForEachSpan, and pins the wire format with goldens. No
# fonts, no FreeType - the C++ sanity suite (FontRLE) independently pins
# the decoder against hand-written bytes, so the two meet in the middle.
#
# Run: python3 tools/test_mono_font_gen.py

import importlib.util
import os
import sys

_here = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "mfg", os.path.join(_here, "mono-font-gen.py"))
mfg = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mfg)


def decode_rle(data, width, height):
    """Reference decoder - byte-for-byte the logic of Font::ForEachSpan."""
    nibbles = []
    for b in data:
        nibbles.append(b >> 4)
        nibbles.append(b & 0xF)
    it = iter(nibbles)

    def run():
        n = next(it)
        if n != 0xF:
            return n
        m = next(it)
        if m <= 0xD:
            return 15 + m
        if m == 0xE:
            return 29 + ((next(it) << 4) | next(it))
        return 285 + ((next(it) << 8) | (next(it) << 4) | next(it))

    grid = [[False] * width for _ in range(height)]
    total = width * height
    px = py = pos = 0
    fg = False
    while pos < total:
        rem = run()
        while rem > 0:
            space = width - px
            take = rem if rem < space else space
            if fg:
                for c in range(px, px + take):
                    grid[py][c] = True
            px += take
            pos += take
            rem -= take
            if px == width:
                px = 0
                py += 1
        fg = not fg
    return grid


def decode_raw(data, width, height):
    stride = (width + 7) // 8
    grid = [[False] * width for _ in range(height)]
    for y in range(height):
        for x in range(width):
            byte = data[y * stride + (x >> 3)]
            grid[y][x] = bool(byte & (0x80 >> (x & 7)))
    return grid


def grid_eq(a, b):
    return a == b


CASES = {
    "empty 6x7": (6, 7, lambda x, y: False),
    "full 6x7": (6, 7, lambda x, y: True),
    "single px": (5, 5, lambda x, y: x == 2 and y == 2),
    "leading fg": (4, 3, lambda x, y: (x == 0 and y == 0)),
    "v-bar": (9, 9, lambda x, y: x == 4),
    "checker": (8, 8, lambda x, y: (x ^ y) & 1),
    "ring": (12, 12, lambda x, y: x in (0, 11) or y in (0, 11)),
    "wide blank (8-bit tier)": (40, 2, lambda x, y: False),
    "tall solid (12-bit tier)": (200, 2, lambda x, y: True),
    "huge solid (>4380 split)": (100, 60, lambda x, y: True),
}


def run_roundtrip():
    fails = 0
    for name, (w, h, fn) in CASES.items():
        grid = [[bool(fn(x, y)) for x in range(w)] for y in range(h)]
        for enc, dec, tag in (
                (mfg.encode_rle, decode_rle, "rle"),
                (mfg.encode_raw, decode_raw, "raw")):
            data = enc(w, h, grid)
            back = dec(data, w, h)
            if not grid_eq(grid, back):
                print(f"FAIL {tag}: {name}")
                fails += 1
            else:
                print(f"ok   {tag}: {name} ({len(data)} B)")
    return fails


def run_goldens():
    """Pin the wire format so a silent encoder change is caught."""
    fails = 0
    # 3x1 "# #": continuous runs bg0 fg1 bg1 fg1 -> nibbles 0,1,1,1
    g = [[True, False, True]]
    exp = bytes([0x01, 0x11])
    got = mfg.encode_rle(3, 1, g)
    if got != exp:
        print(f"FAIL golden A: {got.hex()} != {exp.hex()}")
        fails += 1
    else:
        print("ok   golden A (literal tier)")

    # 20x1 all fg -> bg0, fg20 (15..28 tier): nibbles 0, F,(20-15=5)
    g = [[True] * 20]
    exp = bytes([0x0F, 0x50])
    got = mfg.encode_rle(20, 1, g)
    if got != exp:
        print(f"FAIL golden B: {got.hex()} != {exp.hex()}")
        fails += 1
    else:
        print("ok   golden B (15..28 tier)")

    # 40x1 all fg -> bg0, fg40 (29..284): nibbles 0,F,E,(40-29=11)->0,B
    g = [[True] * 40]
    exp = bytes([0x0F, 0xE0, 0xB0])
    got = mfg.encode_rle(40, 1, g)
    if got != exp:
        print(f"FAIL golden C: {got.hex()} != {exp.hex()}")
        fails += 1
    else:
        print("ok   golden C (8-bit tier)")
    return fails


if __name__ == "__main__":
    n = run_roundtrip() + run_goldens()
    print(f"\n{'PASSED' if n == 0 else f'{n} FAILED'}")
    sys.exit(1 if n else 0)

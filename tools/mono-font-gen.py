#!/usr/bin/env python3
#
# Copyright (c) 2026 triaxis s.r.o.
# Licensed under the MIT license. See LICENSE.txt file in the repository root
# for full license information.
#
# tools/mono-font-gen.py
#
# Converts a bitmap font into the flat mono-gfx `Font` layout (widths /
# offsets / glyph data), optionally RLE-compressed.
#
# Input is a BDF font - the classic, dependency-free bitmap font text
# format. (lv_font_conv does not emit BDF directly; a project using it
# can post-process its output, or use any other BDF source.) The tool,
# the on-target decoder and the generated tables are exercised
# hermetically by the sanity suite.
#
# Usage:
#   mono-font-gen.py FONT.bdf --name FontX [--rle] --out DIR
#
# Emits DIR/FontX.cpp and DIR/FontX.h. Regenerate, never hand-edit.

import argparse
import os
import sys


def parse_bdf(path):
    """Returns (ascent, descent, {codepoint: (advance, bbx, rows)}).

    `bbx` is (w, h, xoff, yoff); `rows` is a list of h ints, each the
    glyph row left-aligned with the MSB as the leftmost pixel.
    """
    ascent = descent = None
    fbb = (0, 0, 0, 0)
    glyphs = {}

    with open(path, "r", encoding="latin-1") as f:
        lines = iter(f.read().splitlines())

    for line in lines:
        parts = line.split()
        if not parts:
            continue
        key = parts[0]
        if key == "FONTBOUNDINGBOX":
            fbb = tuple(int(v) for v in parts[1:5])
        elif key == "FONT_ASCENT":
            ascent = int(parts[1])
        elif key == "FONT_DESCENT":
            descent = int(parts[1])
        elif key == "STARTCHAR":
            cp = None
            advance = None
            bbx = None
            rows = []
            for cl in lines:
                cp_parts = cl.split()
                if not cp_parts:
                    continue
                ck = cp_parts[0]
                if ck == "ENCODING":
                    cp = int(cp_parts[1])
                elif ck == "DWIDTH":
                    advance = int(cp_parts[1])
                elif ck == "BBX":
                    bbx = tuple(int(v) for v in cp_parts[1:5])
                elif ck == "BITMAP":
                    for bl in lines:
                        if bl.strip() == "ENDCHAR":
                            break
                        rows.append(int(bl.strip(), 16))
                    break
            if cp is None or advance is None or bbx is None:
                sys.exit(f"malformed glyph near codepoint {cp}")
            glyphs[cp] = (advance, bbx, rows)

    if ascent is None or descent is None:
        # fall back to the font bounding box when properties are absent
        ascent = fbb[1] + fbb[3]
        descent = -fbb[3]
    return ascent, descent, glyphs


def glyph_cell(ascent, descent, advance, bbx, rows):
    """Rasterises a glyph into a (height x advance) boolean grid."""
    height = ascent + descent
    width = advance
    bw, bh, xoff, yoff = bbx
    grid = [[False] * width for _ in range(height)]
    # BDF row 0 is the top of the bounding box; the box top sits at
    # ascent - bh - yoff from the cell top
    top = ascent - bh - yoff
    for ry, bits in enumerate(rows[:bh]):
        y = top + ry
        if not 0 <= y < height:
            continue
        # bitmap rows are byte-padded and MSB-left
        rowbytes = (bw + 7) // 8
        for bx in range(bw):
            x = xoff + bx
            if 0 <= x < width:
                byte = (bits >> (8 * (rowbytes - 1 - bx // 8))) & 0xFF
                if byte & (0x80 >> (bx % 8)):
                    grid[y][x] = True
    return width, height, grid


def encode_raw(width, height, grid):
    """Row-major MSB-first, (width+7)//8 bytes per row."""
    stride = (width + 7) // 8
    out = bytearray()
    for y in range(height):
        for b in range(stride):
            byte = 0
            for bit in range(8):
                x = b * 8 + bit
                if x < width and grid[y][x]:
                    byte |= 0x80 >> bit
            out.append(byte)
    return bytes(out)


RLE_MAX = 4380          # largest run a single code can express


def _emit_run(nib, run):
    """Appends one run length to the nibble list using scheme U."""
    if run <= 14:
        nib.append(run)
    elif run <= 28:
        nib.append(0xF)
        nib.append(run - 15)                    # 0..13
    elif run <= 284:
        v = run - 29                            # 0..255
        nib += (0xF, 0xE, (v >> 4) & 0xF, v & 0xF)
    else:
        v = run - 285                           # 0..4095
        nib += (0xF, 0xF, (v >> 8) & 0xF, (v >> 4) & 0xF, v & 0xF)


def encode_rle(width, height, grid):
    """Continuous (cross-row) alternating runs, scheme U, nibble-packed.

    The glyph is one row-major pixel stream; runs alternate starting on
    background. A run longer than RLE_MAX is split into chunks joined by a
    zero-length opposite-colour run so the decoder needs no special case.
    """
    nib = []
    colour = 0                                  # stream starts on background
    pos = 0
    total = width * height
    while pos < total:
        # measure the maximal continuous run of this colour, row-major
        run = 0
        while pos + run < total:
            xx = (pos + run) % width
            yy = (pos + run) // width
            if grid[yy][xx] != colour:
                break
            run += 1
        rem = run
        while rem > RLE_MAX:
            _emit_run(nib, RLE_MAX)
            nib.append(0)                       # zero-length opposite run
            rem -= RLE_MAX
        _emit_run(nib, rem)
        pos += run
        colour ^= 1
    if len(nib) & 1:
        nib.append(0)                           # pad to a whole byte
    return bytes((nib[i] << 4) | nib[i + 1] for i in range(0, len(nib), 2))


def emit(name, font_var, ascent, descent, glyphs, use_rle):
    cps = sorted(glyphs)
    # the flat Font covers a single contiguous codepoint range
    first, last = cps[0], cps[-1]
    count = last - first + 1

    data = bytearray()
    offsets = []
    widths = []
    height = ascent + descent
    for cp in range(first, last + 1):
        g = glyphs.get(cp)
        if g is None:
            # gap inside the range: empty glyph, zero advance
            widths.append(0)
            offsets.append(len(data))
            if use_rle:
                data += encode_rle(0, height, [[]] * height)
            continue
        w, h, grid = glyph_cell(ascent, descent, *g)
        widths.append(w)
        offsets.append(len(data))
        data += encode_rle(w, h, grid) if use_rle else encode_raw(w, h, grid)

    if len(data) > 0xFFFF:
        sys.exit(f"glyph data is {len(data)} bytes; Font.offsets is "
                 f"uint16 (max 65535). Use --rle or split the range.")

    fmt = "FontFormat::RLE" if use_rle else "FontFormat::Raw"
    missing = widths[ord(' ') - first] if first <= ord(' ') <= last else 0

    def arr(values, per_line, fmt_one):
        s = []
        for i, v in enumerate(values):
            if i % per_line == 0:
                s.append("\n    ")
            s.append(fmt_one(v))
        return "".join(s)

    cmd = "tools/mono-font-gen.py"
    banner = (f"/*\n * GENERATED by {cmd} - do not edit.\n"
              f" * Source: {name} ({'RLE' if use_rle else 'Raw'}, "
              f"{height}px, U+{first:04X}..U+{last:04X})\n */\n")

    cpp = banner + f'\n#include "{name}.h"\n\n'
    cpp += "namespace {\n\n"
    cpp += "const uint8_t fontData[] = {" + arr(
        data, 16, lambda v: f"0x{v:02X}, ") + "\n};\n\n"
    cpp += "const uint8_t fontWidths[] = {" + arr(
        widths, 16, lambda v: f"{v}, ") + "\n};\n\n"
    cpp += "const uint16_t fontOffsets[] = {" + arr(
        offsets, 12, lambda v: f"{v}, ") + "\n};\n\n"
    cpp += "}\n\n"
    cpp += f"const Font {font_var} = {{\n"
    cpp += f"    .height = {height},\n"
    cpp += "    .spacing = 0,\n"
    cpp += "    .fixedWidth = 0,\n"
    cpp += f"    .missingWidth = {missing},\n"
    cpp += f"    .firstChar = 0x{first:X},\n"
    cpp += f"    .charCount = {count},\n"
    cpp += "    .widths = fontWidths,\n"
    cpp += "    .offsets = fontOffsets,\n"
    cpp += "    .data = fontData,\n"
    cpp += f"    .format = {fmt},\n"
    cpp += "};\n"

    h = banner + "\n#pragma once\n\n#include <mono-gfx/Font.h>\n\n"
    h += f"extern const Font {font_var};\n"
    return cpp, h


def main():
    ap = argparse.ArgumentParser(description="BDF -> mono-gfx Font generator")
    ap.add_argument("bdf", help="input BDF font")
    ap.add_argument("--name", required=True,
                    help="output base name, e.g. FontTiny")
    ap.add_argument("--var", help="C++ Font variable name (default: --name)")
    ap.add_argument("--rle", action="store_true",
                    help="emit the RLE compressed format")
    ap.add_argument("--out", default=".", help="output directory")
    args = ap.parse_args()

    ascent, descent, glyphs = parse_bdf(args.bdf)
    if not glyphs:
        sys.exit("no glyphs found")
    cpp, h = emit(args.name, args.var or args.name,
                  ascent, descent, glyphs, args.rle)

    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, args.name + ".cpp"), "w") as f:
        f.write(cpp)
    with open(os.path.join(args.out, args.name + ".h"), "w") as f:
        f.write(h)


if __name__ == "__main__":
    main()

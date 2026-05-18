#!/usr/bin/env python3
#
# Copyright (c) 2026 triaxis s.r.o.
# Licensed under the MIT license. See LICENSE.txt file in the repository root
# for full license information.
#
# tools/mono-font-gen.py
#
# Rasterises TrueType / OpenType fonts into the flat mono-gfx `Font`
# layout (widths / offsets / glyph data), optionally RLE-compressed.
#
# Rasterisation is done with FreeType (the `freetype-py` package) in pure
# monochrome mode with the autohinter, which is what produces crisp 1-bpp
# glyphs for an embedded panel. FreeType is a generator-only dependency:
# it is not needed by the on-target library nor by its test build.
#
# Several source fonts can be merged into one output (a text face plus
# icon faces, like a typical UI font set). Each `--font` starts a source;
# `--range`, `--symbols`, `--coords` and `--autohint` apply to the source
# they follow. `--size` and `--name` are global.
#
# Usage:
#   mono-font-gen.py --name FontX --size 24 [--rle] --out DIR \
#       --font Text-VF.ttf --coords wdth=75,wght=700 --range 0x20-0x7E,0xB0 \
#       --font Icons.otf --range 0xF240-0xF244
#
# Emits DIR/FontX.cpp and DIR/FontX.h. Regenerate, never hand-edit.

import argparse
import os
import sys

import freetype


def parse_codepoints(range_spec=None, symbols=None):
    cps = set()
    if range_spec:
        for part in range_spec.split(","):
            part = part.strip()
            if not part:
                continue
            if "-" in part:
                a, b = part.split("-", 1)
                for cp in range(int(a, 0), int(b, 0) + 1):
                    cps.add(cp)
            else:
                cps.add(int(part, 0))
    if symbols:
        for ch in symbols:
            cps.add(ord(ch))
    return cps


def apply_coords(face, coords):
    """coords: "wght=700,wdth=75" -> set variable-font design axes."""
    if not coords:
        return
    want = {}
    for kv in coords.split(","):
        tag, val = kv.split("=")
        want[tag.strip()] = float(val)
    info = face.get_variation_info()
    design = []
    for axis in info.axes:
        tag = axis.tag
        if isinstance(tag, bytes):
            tag = tag.decode("latin-1")
        design.append(want.get(tag, axis.default))
    face.set_var_design_coords(design)


def load_source(path, size, coords, hint, cps, glyphs):
    """Renders glyphs into `glyphs` using lv_font_conv's exact FreeType
    recipe: FT_Set_Pixel_Sizes(0,size); autohinter at TARGET_NORMAL
    (--autohint-strong) / TARGET_LIGHT (default) / NO_AUTOHINT (off);
    render MONO; advance from the linear (unhinted) advance."""
    face = freetype.Face(path)
    apply_coords(face, coords)
    face.set_pixel_sizes(0, size)

    # exactly lv_font_conv's flag construction (lib/freetype/index.js):
    # base FT_LOAD_RENDER; TARGET_NORMAL for --autohint-strong else
    # TARGET_LIGHT; NO_AUTOHINT for --autohint-off else FORCE_AUTOHINT;
    # then TARGET_MONO OR-ed in for the 1-bpp case
    flags = freetype.FT_LOAD_RENDER
    flags |= 0 if hint == "strong" else freetype.FT_LOAD_TARGET_LIGHT
    flags |= (freetype.FT_LOAD_NO_AUTOHINT if hint == "off"
              else freetype.FT_LOAD_FORCE_AUTOHINT)
    flags |= freetype.FT_LOAD_TARGET_MONO

    for cp in cps:
        if cp in glyphs:
            continue                        # earlier source wins
        if face.get_char_index(cp) == 0:
            continue                        # not in this face
        face.load_char(cp, flags)
        g = face.glyph
        bm = g.bitmap
        bw, bh = bm.width, bm.rows
        pitch = abs(bm.pitch)
        rowbytes = (bw + 7) // 8
        rows = []
        for r in range(bh):
            chunk = bytes(bm.buffer[r * pitch:r * pitch + rowbytes])
            rows.append(int.from_bytes(chunk, "big") if chunk else 0)
        # lv_font_conv uses the linear (unhinted) horizontal advance
        advance = round(g.linearHoriAdvance / 65536.0)
        # bbx = (w, h, xoff, yoff); yoff = bbox bottom vs baseline = top - h
        bbx = (bw, bh, g.bitmap_left, g.bitmap_top - bh)
        glyphs[cp] = (advance, bbx, rows)


def glyph_cell(ascent, descent, advance, bbx, rows):
    """Rasterises a glyph into a (height x advance) boolean grid."""
    height = ascent + descent
    width = max(advance, 0)
    bw, bh, xoff, yoff = bbx
    grid = [[False] * width for _ in range(height)]
    # the glyph box top sits at ascent - bh - yoff from the cell top
    top = ascent - bh - yoff
    for ry in range(bh):
        y = top + ry
        if not 0 <= y < height:
            continue
        bits = rows[ry]
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


def build_ranges(cps):
    """Groups sorted code points into (first, count, glyph_base) runs -
    an LVGL-style multi-range cmap. Glyph indices follow sorted order."""
    cps = sorted(cps)
    ranges = []
    i = 0
    while i < len(cps):
        j = i
        while j + 1 < len(cps) and cps[j + 1] == cps[j] + 1:
            j += 1
        ranges.append((cps[i], j - i + 1, i))
        i = j + 1
    return ranges


def emit(name, font_var, ascent, descent, glyphs, use_rle):
    cps = sorted(glyphs)
    ranges = build_ranges(cps)               # only present code points

    data = bytearray()
    offsets = []
    widths = []
    blobs = []                          # per-glyph (cp, width, encoded bytes)
    height = ascent + descent
    for cp in cps:
        w, h, grid = glyph_cell(ascent, descent, *glyphs[cp])
        enc = encode_rle(w, h, grid) if use_rle else encode_raw(w, h, grid)
        widths.append(w)
        offsets.append(len(data))
        data += enc
        blobs.append((cp, w, bytes(enc)))

    if len(data) > 0xFFFF:
        sys.exit(f"glyph data is {len(data)} bytes; Font.offsets is "
                 f"uint16 (max 65535). Use --rle or a smaller range.")

    fmt = "FontFormat::RLE" if use_rle else "FontFormat::Raw"
    missing = widths[cps.index(0x20)] if 0x20 in glyphs else 0

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
              f"{height}px, U+{cps[0]:04X}..U+{cps[-1]:04X}, "
              f"{len(ranges)} range(s), {len(cps)} glyphs)\n */\n")

    def cp_label(cp):
        if cp == 0x20:
            return f"U+{cp:04X} ' '"
        if 0x21 <= cp <= 0x7E:
            return f"U+{cp:04X} '{chr(cp)}'"
        return f"U+{cp:04X}"

    def glyph_data():
        s, off = [], 0
        for idx, (cp, w, enc) in enumerate(blobs):
            s.append(f"\n    // [{idx}] {cp_label(cp)}  w={w} off={off}")
            for j, b in enumerate(enc):
                if j % 16 == 0:
                    s.append("\n   ")
                s.append(f" 0x{b:02X},")
            if not enc:
                s.append("\n        /* (no data) */")
            off += len(enc)
        return "".join(s)

    cpp = banner + f'\n#include "{name}.h"\n\n'
    cpp += "namespace {\n\n"
    cpp += "const uint8_t fontData[] = {" + glyph_data() + "\n};\n\n"
    cpp += "const uint8_t fontWidths[] = {" + arr(
        widths, 16, lambda v: f"{v}, ") + "\n};\n\n"
    cpp += "const uint16_t fontOffsets[] = {" + arr(
        offsets, 12, lambda v: f"{v}, ") + "\n};\n\n"
    cpp += "const FontRange fontRanges[] = {\n"
    for first, count, base in ranges:
        cpp += f"    {{ 0x{first:X}, {count}, {base} }},\n"
    cpp += "};\n\n}\n\n"
    cpp += f"const Font {font_var} = {{\n"
    cpp += f"    .height = {height},\n"
    cpp += "    .spacing = 0,\n"
    cpp += "    .fixedWidth = 0,\n"
    cpp += f"    .missingWidth = {missing},\n"
    cpp += "    .ranges = fontRanges,\n"
    cpp += f"    .rangeCount = {len(ranges)},\n"
    cpp += "    .widths = fontWidths,\n"
    cpp += "    .offsets = fontOffsets,\n"
    cpp += "    .data = fontData,\n"
    cpp += f"    .format = {fmt},\n"
    cpp += "};\n"

    h = banner + "\n#pragma once\n\n#include <mono-gfx/Font.h>\n\n"
    h += f"extern const Font {font_var};\n"
    return cpp, h


class _Font(argparse.Action):
    """Starts a new source group."""
    def __call__(self, parser, ns, value, opt=None):
        ns.sources.append({"path": value, "coords": None, "range": None,
                            "symbols": None, "hint": "default"})


class _SourceOpt(argparse.Action):
    """An option that binds to the most recent --font."""
    def __call__(self, parser, ns, value, opt=None):
        if not ns.sources:
            parser.error(f"{opt} must follow a --font")
        ns.sources[-1][self.dest] = value


class _Hint(argparse.Action):
    """--autohint-strong / --autohint-off, bound to the current --font."""
    def __init__(self, *a, **k):
        k["nargs"] = 0
        super().__init__(*a, **k)

    def __call__(self, parser, ns, value, opt=None):
        if not ns.sources:
            parser.error(f"{opt} must follow a --font")
        ns.sources[-1]["hint"] = "strong" if opt == "--autohint-strong" \
            else "off"


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="mono-font-gen.py",
        description="Rasterise TTF/OTF fonts into a flat mono-gfx Font "
                    "table. Several --font sources merge into one output; "
                    "--range/--symbols/--coords/--autohint-* bind to the "
                    "--font they follow.")
    p.add_argument("--name", required=True, help="output base name")
    p.add_argument("--var", help="C++ Font variable name (default: --name)")
    p.add_argument("--out", default=".", help="output directory")
    p.add_argument("--size", type=int, required=True,
                   help="pixel size")
    p.add_argument("--rle", action="store_true",
                   help="emit the RLE compressed format")
    p.add_argument("--font", action=_Font, metavar="PATH",
                   help="add a source face (repeatable)")
    p.add_argument("--coords", action=_SourceOpt, metavar="tag=val,...",
                   help="variable-font design axes for the current font")
    p.add_argument("--range", dest="range", action=_SourceOpt,
                   metavar="A-B,C", help="codepoint range(s)")
    p.add_argument("--symbols", action=_SourceOpt, metavar="CHARS",
                   help="literal characters to include")
    p.add_argument("--autohint-strong", action=_Hint,
                   help="FT_LOAD_TARGET_NORMAL hinting for the current font")
    p.add_argument("--autohint-off", action=_Hint,
                   help="disable autohinting for the current font")

    args = p.parse_args(argv, argparse.Namespace(sources=[]))
    if not args.sources:
        p.error("at least one --font is required")
    return args


def main():
    args = parse_args(sys.argv[1:])
    glyphs = {}
    for s in args.sources:
        cps = parse_codepoints(s["range"], s["symbols"])
        if not cps:
            sys.exit(f"source {s['path']} has no --range/--symbols")
        load_source(s["path"], args.size, s["coords"], s["hint"],
                    cps, glyphs)
    if not glyphs:
        sys.exit("no glyphs rendered")
    # ascent/descent are the bbox extremes over the rendered set, exactly
    # as lv_font_conv computes them (not the face metrics)
    tops = [bbx[3] + bbx[1] for _, bbx, _ in glyphs.values()]    # yoff + h
    bots = [bbx[3] for _, bbx, _ in glyphs.values()]             # yoff
    ascent = max(tops)
    descent = -min(bots)
    cpp, h = emit(args.name, args.var or args.name,
                  ascent, descent, glyphs, args.rle)

    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, args.name + ".cpp"), "w") as f:
        f.write(cpp)
    with open(os.path.join(args.out, args.name + ".h"), "w") as f:
        f.write(h)


if __name__ == "__main__":
    main()

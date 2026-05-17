# MinuteOS Monochrome Graphics Library

A small, fast graphics library for 1-bit-per-pixel displays.

The library targets the embedded use case: no allocations, no virtuals on
hot paths, framebuffers are passed by value as four-word handles, and the
inner loops process whole bytes wherever the geometry allows.

The output of `MonoBuffer` is laid out in the canonical horizontal
MSB-first 1-bpp planar format also consumed by the [`lib-display`][1]
drivers, so a fully rendered framebuffer can be handed straight to the
display controller, or transformed into vertical-byte / page-mode formats
via `MonoFormat::ToVerticalBytes` for chips like SSD1306.

[1]: https://github.com/minuteos/lib-display

## Components

```
targets/all/mono-gfx/
    DrawOp.h            pixel and blit ops, plus the one-byte helper used everywhere
    MonoBuffer.{h,cpp}  the 1-bpp framebuffer view and all drawing primitives
    MonoFormat.{h,cpp}  conversions to display-specific output formats
    Font.h              bitmap font definition (Raw + RLE formats)
    fonts/Font5x7.{h,cpp}   built-in 5x7 ASCII font (0x20-0x7E)
    mono-gfx.h          umbrella header
tools/
    mono-font-gen.py        TTF/OTF -> flat Font generator (--rle to compress)
    test_mono_font_gen.py   deterministic encoder round-trip self-test
```

To use the library, add `mono-gfx` to your project's `COMPONENTS` and
include `<mono-gfx/mono-gfx.h>`. The built-in `Font5x7` is in the optional
`mono-gfx/fonts` component.

## Quick start

```cpp
#include <mono-gfx/mono-gfx.h>
#include <mono-gfx/fonts/Font5x7.h>

uint8_t fb[128 * 64 / 8];
MonoBuffer screen(fb, 128, 64);

screen.Clear();
screen.DrawRoundRect(0, 0, 128, 64, 4);
screen.DrawText(6, 8, Font5x7, "Hello, MinuteOS!");
screen.FillCircle(110, 50, 10);

// hand the framebuffer to your display driver, or convert for SSD1306:
uint8_t page[128 * 8];
MonoFormat::ToVerticalBytes(screen, page);
```

## Storage format

`MonoBuffer` is a non-owning view: it stores a data pointer and three
small ints (width, height, stride). Pixel `(x, y)` is at:

```
byte index   = y * stride + (x >> 3)
bit position = 0x80 >> (x & 7)
```

This is the same format used by `lib-display::ST7306` and the format most
e-paper drivers expect, so the framebuffer can be transferred to the
display with no per-pixel work.

## Drawing operations

Every primitive accepts a `DrawOp`:

- `Set` — turn pixels on
- `Clear` — turn pixels off
- `Invert` — toggle pixels
- `Keep` — no-op (skip drawing)

Blits accept a `BlitOp` that distinguishes between "treat the source as a
mask" operators (`Or`, `AndNot`, `Xor`) and "treat the source as a full
plane" operators (`Copy`, `CopyNot`, `And`).

## Fonts

A `Font` is a flat struct holding glyph dimensions and a glyph blob, in
either fixed-width or variable-width form. New fonts can be defined as
plain `Font` constants without touching the library code; the supplied
`Font5x7` is one such definition.

### Compressed glyphs (`FontFormat::RLE`)

`Font::format` selects the glyph storage scheme. `Raw` is the
uncompressed row-major bitmap (whole-byte blit fast path, unchanged).
`RLE` stores the glyph as **one continuous row-major run stream** (not
per row); runs of equal colour alternate starting on background. Each run
length is read from a nibble stream:

| code | run length |
|---|---|
| `0x0..0xE` | `0..14` (one nibble) |
| `0xF` `0x0..0xD` | `15..28` |
| `0xF` `0xE` + 8 bits | `29..284` |
| `0xF` `0xF` + 12 bits | `285..4380` |

`Font::ForEachSpan` decodes it **straight into `DrawHLine` runs**: a
foreground run is emitted as one span per row it covers (a run crossing a
row boundary is split at the edge). The single >4380 run in a typical
font set is split by the generator into chunks joined by a zero-length
opposite-colour run, so the decoder needs no special case.

**Decode budget:** O(runs) per glyph - one nibble read + a 4-way branch
per run (no continuation loop, no bit reader), one `DrawHLine` per
foreground run segment, zero allocation. This stays within the
"near-zero cost at blit time" constraint - entropy coding (~3 KB more)
was rejected for putting a per-symbol decode in the hot path.

**Size** (worked example: a bar glyph + a ring glyph at several sizes,
glyph-data blob; raw = byte-aligned-row `Font` layout):

| glyph size | raw | RLE |
|---|--:|--:|
| 16 px | 64 B | 41 B |
| 32 px | 256 B | 148 B |
| 48 px | 576 B | 272 B |
| 96 px | 2304 B | 605 B |

Because the stream is continuous it also collapses the background
between glyph strokes and across blank rows, so RLE typically lands well
under half of raw for proportional fonts in the ~10-96 px range and wins
on small dense fonts too. Most runs are a single nibble; the 8/12-bit
tiers and the >4380 split are rare. Pick `--rle` per font (it can lose
on a tiny font where raw is already one byte per row).

### Generator (`tools/mono-font-gen.py`)

`tools/mono-font-gen.py` rasterises TrueType / OpenType fonts into a flat
`Font` C++ table (`--rle` to compress). It renders with FreeType (via
`freetype-py`) using the **exact recipe of `lv_font_conv`** — the same
`FT_Set_Pixel_Sizes(0,size)`, the same load flags
(`FT_LOAD_RENDER | TARGET_NORMAL|FORCE_AUTOHINT | TARGET_MONO` for
`--autohint-strong`; `TARGET_LIGHT` / `NO_AUTOHINT` for the other modes),
the linear (unhinted) advance, and ascent/descent taken from the bbox
extremes of the rendered set. Output is metrically identical to
`lv_font_conv`; glyph extents can still differ by ~1–2px because
`lv_font_conv` bundles its own frozen FreeType build and the autohinter
changes between FreeType versions — this is invisible in use and not a
regression. FreeType is a **generator-only** dependency: not needed by
the library nor its test build, and the generated tables are checked in.

Several source faces merge into one output (a text face plus icon
faces). Each `--font` starts a source; `--range`, `--symbols`,
`--coords` (variable-font axes) and `--autohint-strong` /
`--autohint-off` apply to the source they follow; `--size`/`--name` are
global:

```sh
pip install freetype-py
tools/mono-font-gen.py --name FontUI --size 24 --rle \
    --out OUTDIR \
    --font Text-VF.ttf --coords wdth=75,wght=700 --range 0x20-0x7E,0xB0 \
    --autohint-strong \
    --font Icons.otf --range 0xF240-0xF244,0xF072
```

The encoder is independently pinned by `tools/test_mono_font_gen.py`, a
deterministic round-trip + golden self-test (no fonts, no FreeType); the
on-target decoder is pinned by the `FontRLE` C++ sanity suite against
hand-written bytes — the two meet in the middle on the wire format.

## Primitives

`MonoBuffer` covers pixels, h/v/arbitrary lines, rectangles, round-rects,
circles, **arcs** (`DrawArc`, angular sweep), **triangles**
(`DrawTriangle` / `FillTriangle`, solid conservative fill), text, plain
blits and **rotated blits** (`BlitRotated`, nearest-neighbour mask stamp
around a pivot). Arc and rotation use a table-free fixed-point sine
approximation, so there is no `libm` dependency on the target.

## License

Licensed under the [MIT License](./LICENSE.txt).

Copyright (c) 2026 triaxis s.r.o.

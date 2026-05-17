/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * Cmap.cpp - multi-range Font (LVGL-style cmap), real Unicode code points
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>
#include <mono-gfx/fonts/Font5x7.h>

namespace
{

// Five 1-row solid glyphs, widths 1..5 (so a code point is identifiable
// by its glyph width). Continuous RLE of a solid w-wide row = bg0, fg w
// -> nibbles 0,w -> byte 0x0w.
const uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
const uint8_t widths[] = { 1, 2, 3, 4, 5 };
const uint16_t offsets[] = { 0, 1, 2, 3, 4 };
// two disjoint ranges: 'A'..'C' (glyphs 0..2), U+F240..U+F241 (glyphs 3..4)
const FontRange ranges[] = { { 0x41, 3, 0 }, { 0xF240, 2, 3 } };

const Font Cmap = {
    .height = 1, .spacing = 0, .fixedWidth = 0, .missingWidth = 9,
    .ranges = ranges, .rangeCount = 2,
    .widths = widths, .offsets = offsets, .data = data,
    .format = FontFormat::RLE,
};

int Count(const MonoBuffer& b)
{
    int n = 0;
    for (int y = 0; y < b.Height(); y++)
        for (int x = 0; x < b.Width(); x++)
            if (b.GetPixel(x, y)) n++;
    return n;
}

TEST_CASE("01 GlyphIndex / GetGlyph resolve across both ranges")
{
    AssertEqual(Cmap.GlyphIndex(0x41), 0);
    AssertEqual(Cmap.GlyphIndex(0x43), 2);
    AssertEqual(Cmap.GlyphIndex(0xF240), 3);
    AssertEqual(Cmap.GlyphIndex(0xF241), 4);
    AssertEqual((unsigned)Cmap.GetGlyph(0x41).width, 1u);
    AssertEqual((unsigned)Cmap.GetGlyph(0x43).width, 3u);
    AssertEqual((unsigned)Cmap.GetGlyph(0xF240).width, 4u);
    AssertEqual((unsigned)Cmap.GetGlyph(0xF241).width, 5u);
}

TEST_CASE("02 Code points outside the ranges are missing")
{
    for (unsigned cp : { 0x40u, 0x44u, 0xF23Fu, 0xF242u, 0x20u })
    {
        AssertEqual(Cmap.GlyphIndex(cp), -1);
        Glyph g = Cmap.GetGlyph(cp);
        AssertEqual(g.bitmap == nullptr, true);
        AssertEqual((unsigned)g.width, 9u);          // missingWidth
    }
}

TEST_CASE("03 DrawGlyph at a real PUA code point")
{
    uint8_t mem[1 * 2] = {};
    MonoBuffer b(mem, 8, 2);
    int pen = b.DrawGlyph(0, 0, Cmap, 0xF240);       // 4px solid
    AssertEqual(Count(b), 4);
    for (int x = 0; x < 4; x++) AssertEqual(b.GetPixel(x, 0), true);
    AssertEqual(b.GetPixel(4, 0), false);
    AssertEqual(pen, 4);
}

TEST_CASE("04 UTF-8 DrawText reaches the PUA glyph, no remap")
{
    uint8_t m1[2 * 2] = {}, m2[2 * 2] = {};
    MonoBuffer a(m1, 16, 2), b(m2, 16, 2);
    // U+F240 == UTF-8 EF 89 80 ; mixed with ASCII 'A'
    a.DrawText(0, 0, Cmap, "A\xEF\x89\x80");
    int p = b.DrawGlyph(0, 0, Cmap, 0x41);
    p = b.DrawGlyph(p, 0, Cmap, 0xF240);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 16; x++)
            AssertEqual(a.GetPixel(x, y), b.GetPixel(x, y));
    AssertEqual(Count(a), 1 + 4);
    AssertEqual(MonoBuffer::MeasureText(Cmap, "A\xEF\x89\x80"), 1 + 4);
}

TEST_CASE("05 Single-range (ranges == null) font still works")
{
    AssertEqual(Font5x7.ranges == nullptr, true);
    Assert(Font5x7.GlyphIndex('A') >= 0);
    AssertEqual(Font5x7.GlyphIndex(0x19), -1);        // below firstChar
    AssertEqual(Font5x7.GlyphIndex(0x2000), -1);      // above range
    Assert(Cmap.GlyphIndex('Z') == -1);              // sanity vs Cmap
}

}

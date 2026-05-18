/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * Glyph.cpp - MonoBuffer::DrawGlyph (single glyph by code point)
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>
#include <mono-gfx/fonts/Font5x7.h>

namespace
{

// one RLE glyph at U+2080 (> 0xFF, unreachable via a byte string):
// a solid 3x2 block -> continuous bg0 fg6 -> nibbles 0,6 -> byte 0x06
const uint8_t hiData[] = { 0x06 };
const GlyphMetric hiGlyphs[] = { { 0, 3, 3, 2, 0, 0 } };
const Font HiFont = {
    .height = 2, .spacing = 1, .missingWidth = 0,
    .ascent = 2, .descent = 0,
    .firstChar = 0x2080, .charCount = 1,
    .glyphs = hiGlyphs, .data = hiData, .format = FontFormat::RLE,
};

int CountSet(const MonoBuffer& b)
{
    int n = 0;
    for (int y = 0; y < b.Height(); y++)
        for (int x = 0; x < b.Width(); x++)
            if (b.GetPixel(x, y)) n++;
    return n;
}

TEST_CASE("01 DrawGlyph reaches a code point above 0xFF")
{
    uint8_t mem[1 * 4] = {};
    MonoBuffer b(mem, 8, 4);
    int pen = b.DrawGlyph(1, 1, HiFont, 0x2080);
    // solid 3x2 block at (1,1)
    for (int y = 1; y <= 2; y++)
        for (int x = 1; x <= 3; x++)
            AssertEqual(b.GetPixel(x, y), true);
    AssertEqual(CountSet(b), 6);
    AssertEqual(pen, 1 + 3 + 1);            // x + width + spacing
}

TEST_CASE("02 DrawGlyph on a Raw font matches DrawText of one char")
{
    uint8_t m1[8 * 8] = {};
    uint8_t m2[8 * 8] = {};
    MonoBuffer a(m1, 16, 8), c(m2, 16, 8);
    int pa = a.DrawGlyph(2, 0, Font5x7, 'R');
    int pc = c.DrawText(2, 0, Font5x7, "R");
    AssertEqual(pa, pc);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 16; x++)
            AssertEqual(a.GetPixel(x, y), c.GetPixel(x, y));
    Assert(CountSet(a) > 0);
}

TEST_CASE("03 DrawGlyph Keep advances without drawing")
{
    uint8_t mem[1 * 4] = {};
    MonoBuffer b(mem, 8, 4);
    int pen = b.DrawGlyph(0, 1, HiFont, 0x2080, DrawOp::Keep);
    AssertEqual(CountSet(b), 0);
    AssertEqual(pen, 0 + 3 + 1);
}

TEST_CASE("04 Missing code point advances by missingWidth")
{
    uint8_t mem[2 * 8] = {};
    MonoBuffer b(mem, 16, 8);
    int pen = b.DrawGlyph(0, 0, Font5x7, 0x3000);   // out of range
    AssertEqual(CountSet(b), 0);
    AssertEqual(pen, Font5x7.missingWidth + Font5x7.spacing);
}

TEST_CASE("05 DrawText equals chained DrawGlyph")
{
    uint8_t m1[8 * 8] = {};
    uint8_t m2[8 * 8] = {};
    MonoBuffer a(m1, 32, 8), c(m2, 32, 8);
    a.DrawText(0, 0, Font5x7, "Hi!");
    int p = 0;
    for (const char* s = "Hi!"; *s; s++)
        p = c.DrawGlyph(p, 0, Font5x7, (unsigned char)*s);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 32; x++)
            AssertEqual(a.GetPixel(x, y), c.GetPixel(x, y));
}

}

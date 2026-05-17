/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * FontRLE.cpp - the cheap-decode continuous RLE glyph format (scheme U)
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>
#include <mono-gfx/fonts/Font5x7.h>

namespace
{

// Three 3-row glyphs, firstChar 'A'. The stream is one continuous
// row-major run sequence, runs alternate starting on background, nibble
// packed high-first.
//
//   'A' (w=3)   'B' (w=4)   'C' (w=2)
//   # #         ####        ..   (blank)
//   .#.         ####        ..
//   ###         ####        ..
const uint8_t rleData[] = {
    // 'A' continuous runs: bg0 fg1 bg1 fg1 bg1 fg1 bg1 fg3
    //   nibbles 0,1,1,1,1,1,1,3
    0x01, 0x11, 0x11, 0x13,
    // 'B' solid 4x3: bg0 fg12  -> nibbles 0,C
    0x0C,
    // 'C' blank 2x3: bg6 (single run, total = 6) -> nibble 6 (+pad)
    0x60,
};
const uint8_t rleWidths[] = { 3, 4, 2 };
const uint16_t rleOffsets[] = { 0, 4, 5 };

const Font RleFont = {
    .height = 3,
    .spacing = 1,
    .fixedWidth = 0,
    .missingWidth = 2,
    .firstChar = 'A',
    .charCount = 3,
    .widths = rleWidths,
    .offsets = rleOffsets,
    .data = rleData,
    .format = FontFormat::RLE,
};

// 8-bit tier: one glyph 'X', solid 20x2 -> bg0 fg40; 40 in 29..284
//   nibbles 0, F,E,0,B
const uint8_t d8[] = { 0x0F, 0xE0, 0xB0 };
const uint8_t w8[] = { 20 };
const uint16_t o8[] = { 0 };
const Font Font8 = {
    .height = 2, .spacing = 0, .fixedWidth = 0, .missingWidth = 0,
    .firstChar = 'X', .charCount = 1,
    .widths = w8, .offsets = o8, .data = d8, .format = FontFormat::RLE,
};

// 12-bit tier: one glyph 'X', solid 200x2 -> bg0 fg400; 400 in 285..4380
//   nibbles 0, F,F,0,7,3
const uint8_t d12[] = { 0x0F, 0xF0, 0x73 };
const uint8_t w12[] = { 200 };
const uint16_t o12[] = { 0 };
const Font Font12 = {
    .height = 2, .spacing = 0, .fixedWidth = 0, .missingWidth = 0,
    .firstChar = 'X', .charCount = 1,
    .widths = w12, .offsets = o12, .data = d12, .format = FontFormat::RLE,
};

// split tier: 'X' solid 100x50 -> fg run 5000 > 4380, generator-style
//   split: bg0, fg4380, bg0(sep), fg620
//   nibbles: 0 | F F F F F | 0 | F F 1 4 F
const uint8_t dS[] = { 0x0F, 0xFF, 0xFF, 0x0F, 0xF1, 0x4F };
const uint8_t wS[] = { 100 };
const uint16_t oS[] = { 0 };
const Font FontSplit = {
    .height = 50, .spacing = 0, .fixedWidth = 0, .missingWidth = 0,
    .firstChar = 'X', .charCount = 1,
    .widths = wS, .offsets = oS, .data = dS, .format = FontFormat::RLE,
};

TEST_CASE("01 ForEachSpan yields exact foreground runs")
{
    int n = 0;
    int gx[8], gy[8], gl[8];
    RleFont.ForEachSpan('A', [&](int x, int y, int l)
    {
        gx[n] = x; gy[n] = y; gl[n] = l; n++;
    });
    AssertEqual(n, 4);
    AssertEqual(gx[0], 0); AssertEqual(gy[0], 0); AssertEqual(gl[0], 1);
    AssertEqual(gx[1], 2); AssertEqual(gy[1], 0); AssertEqual(gl[1], 1);
    AssertEqual(gx[2], 1); AssertEqual(gy[2], 1); AssertEqual(gl[2], 1);
    AssertEqual(gx[3], 0); AssertEqual(gy[3], 2); AssertEqual(gl[3], 3);
}

TEST_CASE("02 Blank glyph emits no spans")
{
    int n = 0;
    RleFont.ForEachSpan('C', [&](int, int, int) { n++; });
    AssertEqual(n, 0);
}

TEST_CASE("03 Out-of-range and non-RLE fonts yield nothing")
{
    int n = 0;
    RleFont.ForEachSpan('Z', [&](int, int, int) { n++; });   // past charCount
    AssertEqual(n, 0);
    Font5x7.ForEachSpan('A', [&](int, int, int) { n++; });    // Raw font
    AssertEqual(n, 0);
}

TEST_CASE("04 DrawText renders an RLE glyph pixel-exact")
{
    uint8_t mem[1 * 4] = {};
    MonoBuffer b(mem, 8, 4);
    int pen = b.DrawText(0, 0, RleFont, "A");

    AssertEqual(b.GetPixel(0, 0), true);
    AssertEqual(b.GetPixel(1, 0), false);
    AssertEqual(b.GetPixel(2, 0), true);
    AssertEqual(b.GetPixel(0, 1), false);
    AssertEqual(b.GetPixel(1, 1), true);
    AssertEqual(b.GetPixel(2, 1), false);
    AssertEqual(b.GetPixel(0, 2), true);
    AssertEqual(b.GetPixel(1, 2), true);
    AssertEqual(b.GetPixel(2, 2), true);
    AssertEqual(b.GetPixel(0, 3), false);
    AssertEqual(pen, 3 + 1);
}

TEST_CASE("05 Continuous run is split across rows")
{
    // 'B' is a single fg run of 12 that must paint 3 separate rows
    int rows = 0;
    int seen[3] = {};
    RleFont.ForEachSpan('B', [&](int x, int y, int l)
    {
        AssertEqual(x, 0);
        AssertEqual(l, 4);
        seen[y]++; rows++;
    });
    AssertEqual(rows, 3);
    AssertEqual(seen[0], 1);
    AssertEqual(seen[1], 1);
    AssertEqual(seen[2], 1);

    uint8_t mem[2 * 4] = {};
    MonoBuffer b(mem, 16, 4);
    int pen = b.DrawText(0, 0, RleFont, "AB");
    AssertEqual(pen, MonoBuffer::MeasureText(RleFont, "AB"));
    AssertEqual(pen, (3 + 1) + (4 + 1));
    for (int y = 0; y < 3; y++)
        for (int x = 4; x < 8; x++)
            AssertEqual(b.GetPixel(x, y), true);
    AssertEqual(b.GetPixel(8, 0), false);
}

TEST_CASE("06 DrawOp applies to RLE spans")
{
    uint8_t mem[1 * 3] = {};
    MonoBuffer b(mem, 8, 3);
    b.FillRect(0, 0, 8, 3);                  // all foreground
    b.DrawText(0, 0, RleFont, "A", DrawOp::Clear);
    AssertEqual(b.GetPixel(0, 0), false);
    AssertEqual(b.GetPixel(1, 0), true);     // gap in "# #" survives
    AssertEqual(b.GetPixel(0, 2), false);
    AssertEqual(b.GetPixel(3, 0), true);     // outside the glyph untouched
}

TEST_CASE("07 Missing glyph advances by missingWidth and draws nothing")
{
    uint8_t mem[2 * 3] = {};
    MonoBuffer b(mem, 16, 3);
    int pen = b.DrawText(0, 0, RleFont, "Z");    // out of range
    int set = 0;
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 16; x++)
            if (b.GetPixel(x, y)) set++;
    AssertEqual(set, 0);
    AssertEqual(pen, RleFont.missingWidth + RleFont.spacing);
}

TEST_CASE("08 8-bit tier (F E + byte) decodes a 29..284 run")
{
    int total = 0;
    Font8.ForEachSpan('X', [&](int x, int y, int l)
    {
        AssertEqual(x, 0);
        AssertEqual(l, 20);
        Assert(y == 0 || y == 1);
        total += l;
    });
    AssertEqual(total, 40);

    uint8_t mem[3 * 2] = {};
    MonoBuffer b(mem, 20, 2);
    b.DrawText(0, 0, Font8, "X");
    int set = 0;
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 20; x++)
            if (b.GetPixel(x, y)) set++;
    AssertEqual(set, 40);
}

TEST_CASE("09 12-bit tier (F F + 12 bits) decodes a 285..4380 run")
{
    int total = 0, rows = 0;
    Font12.ForEachSpan('X', [&](int x, int y, int l)
    {
        AssertEqual(x, 0);
        AssertEqual(l, 200);
        rows++; total += l;
    });
    AssertEqual(rows, 2);
    AssertEqual(total, 400);

    uint8_t mem[25 * 2] = {};
    MonoBuffer b(mem, 200, 2);
    b.DrawText(0, 0, Font12, "X");
    AssertEqual(b.GetPixel(0, 0), true);
    AssertEqual(b.GetPixel(199, 0), true);
    AssertEqual(b.GetPixel(199, 1), true);
}

TEST_CASE("10 Split run (> 4380) reconstructs one contiguous region")
{
    int total = 0;
    FontSplit.ForEachSpan('X', [&](int, int, int l) { total += l; });
    AssertEqual(total, 5000);                // 4380 + 620, separator is 0

    uint8_t mem[13 * 50] = {};
    MonoBuffer b(mem, 100, 50);
    b.DrawText(0, 0, FontSplit, "X");
    int set = 0;
    for (int y = 0; y < 50; y++)
        for (int x = 0; x < 100; x++)
            if (b.GetPixel(x, y)) set++;
    AssertEqual(set, 5000);                  // fully solid, no seam at 4380
    AssertEqual(b.GetPixel(0, 0), true);
    AssertEqual(b.GetPixel(99, 49), true);
}

}

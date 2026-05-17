/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * FontTiny.cpp - round-trips the generator output through the decoder
 *
 * FontTiny.{h,cpp} are produced by tools/mono-font-gen.py from
 * tools/samples/tiny.bdf (RLE). This test decodes that committed
 * output with the on-target path and checks it against the known glyph
 * shapes, so the generator and the decoder are validated together with
 * no build-time tooling.
 *
 * The sample covers the contiguous range U+0020..U+0023:
 *   ' '  blank          '!'  bar with serifs
 *   '"'  solid block    '#'  ring with a hollow centre
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>
#include <mono-gfx/fonts/FontTiny.h>

namespace
{

TEST_CASE("01 Generated metrics survive the round-trip (no gap glyphs)")
{
    AssertEqual((unsigned)FontTiny.height, 7u);
    AssertEqual(FontTiny.IsRLE(), true);
    AssertEqual((unsigned)FontTiny.firstChar, 0x20u);
    AssertEqual((unsigned)FontTiny.charCount, 4u);     // contiguous, dense
    // every covered glyph is a real 6px cell - no zero-width fillers
    for (unsigned c = 0x20; c <= 0x23; c++)
        AssertEqual((unsigned)FontTiny.GetGlyph(c).width, 6u);
    AssertEqual(MonoBuffer::MeasureText(FontTiny, "!\"#"), 18);
}

TEST_CASE("02 Generated glyphs decode to the source bitmap")
{
    uint8_t mem[3 * 8] = {};
    MonoBuffer b(mem, 24, 8);
    int pen = b.DrawText(0, 0, FontTiny, "!\"#");
    AssertEqual(pen, 18);

    // '!' (cell x 0..5): full top/bottom bars, centred 2px stem
    for (int x = 0; x <= 5; x++)
    {
        AssertEqual(b.GetPixel(x, 0), true);
        AssertEqual(b.GetPixel(x, 6), true);
    }
    for (int y = 1; y <= 5; y++)
    {
        AssertEqual(b.GetPixel(0, y), false);
        AssertEqual(b.GetPixel(2, y), true);
        AssertEqual(b.GetPixel(3, y), true);
        AssertEqual(b.GetPixel(5, y), false);
    }

    // '"' (cell x 6..11): a solid 6x7 block (one fg run spanning all rows)
    for (int y = 0; y <= 6; y++)
        for (int x = 6; x <= 11; x++)
            AssertEqual(b.GetPixel(x, y), true);
    AssertEqual(b.GetPixel(6, 7), false);

    // '#' (cell x 12..17): ring - walls at the edge columns, hollow centre
    AssertEqual(b.GetPixel(12, 0), false);
    AssertEqual(b.GetPixel(13, 0), true);
    AssertEqual(b.GetPixel(16, 0), true);
    AssertEqual(b.GetPixel(12, 3), true);       // left wall
    AssertEqual(b.GetPixel(17, 3), true);       // right wall
    AssertEqual(b.GetPixel(14, 3), false);      // hollow interior
    AssertEqual(b.GetPixel(13, 6), true);
}

TEST_CASE("03 Space is blank and advances; missing glyph uses fallback")
{
    uint8_t mem[3 * 8] = {};
    MonoBuffer b(mem, 24, 8);
    int pen = b.DrawText(0, 0, FontTiny, " !");

    // nothing drawn for the space; '!' sits one cell in (x 6..)
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 6; x++)
            AssertEqual(b.GetPixel(x, y), false);
    AssertEqual(b.GetPixel(0, 0), false);
    AssertEqual(b.GetPixel(6, 0), true);
    AssertEqual(pen, 12);

    // 'A' (0x41) is outside the encoded range -> missingWidth, no pixels
    uint8_t m2[3 * 8] = {};
    MonoBuffer b2(m2, 24, 8);
    int p2 = b2.DrawText(0, 0, FontTiny, "A");
    int set = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 24; x++)
            if (b2.GetPixel(x, y)) set++;
    AssertEqual(set, 0);
    AssertEqual(p2, (int)FontTiny.missingWidth);
}

}

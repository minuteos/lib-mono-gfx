/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * Text.cpp - text rendering and measurement
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>
#include <mono-gfx/fonts/Font5x7.h>

namespace
{

TEST_CASE("01 MeasureText basic")
{
    AssertEqual(MonoBuffer::MeasureText(Font5x7, ""), 0);
    // each glyph is 5px wide with 1px spacing
    AssertEqual(MonoBuffer::MeasureText(Font5x7, "a"),   6);
    AssertEqual(MonoBuffer::MeasureText(Font5x7, "abc"), 18);
    AssertEqual(MonoBuffer::MeasureText(Font5x7, "Hi!"), 18);
}

TEST_CASE("02 MeasureText newline picks longest line")
{
    AssertEqual(MonoBuffer::MeasureText(Font5x7, "Hi\nWorld"), 30);
    AssertEqual(MonoBuffer::MeasureText(Font5x7, "World\nHi"), 30);
    AssertEqual(MonoBuffer::MeasureText(Font5x7, "\n\n\n"),    0);
}

TEST_CASE("03 DrawText returns final pen position")
{
    uint8_t mem[10 * 8] = {};
    MonoBuffer b(mem, 80, 8);
    int x = b.DrawText(2, 0, Font5x7, "Hi");
    AssertEqual(x, 14);
    int x2 = b.DrawText(2, 0, Font5x7, "");
    AssertEqual(x2, 2);
}

TEST_CASE("04 DrawText respects newline")
{
    uint8_t mem[16 * 16] = {};
    MonoBuffer b(mem, 128, 16);
    b.DrawText(0, 0, Font5x7, "A\nB");
    // 'A' should appear in the top half (y < 8)
    Assert(b.GetPixel(2, 0) || b.GetPixel(0, 1) || b.GetPixel(4, 1));
    // 'B' should appear below the first line break (y >= 8)
    bool foundB = false;
    for (int y = 8; y < 16 && !foundB; y++)
        for (int x = 0; x < 8; x++)
            if (b.GetPixel(x, y)) foundB = true;
    Assert(foundB);
}

TEST_CASE("05 DrawText Clear inverts within filled background")
{
    uint8_t mem[16 * 8] = {};
    MonoBuffer b(mem, 128, 8);
    // fill a region, then draw text in Clear mode - the glyph should appear
    // as cleared pixels inside the otherwise solid fill
    b.FillRect(0, 0, 50, 8, DrawOp::Set);
    b.DrawText(2, 0, Font5x7, "X", DrawOp::Clear);

    // there must be some cleared pixels inside the X glyph cell
    bool foundClear = false;
    for (int y = 0; y < 7 && !foundClear; y++)
        for (int x = 2; x < 7; x++)
            if (!b.GetPixel(x, y)) { foundClear = true; break; }
    Assert(foundClear);
    // pixels outside the glyph cell are still set
    AssertEqual(b.GetPixel(20, 0), true);
}

TEST_CASE("06 DrawText Keep does not modify framebuffer")
{
    uint8_t mem[10 * 8] = {};
    MonoBuffer b(mem, 80, 8);
    for (size_t i = 0; i < sizeof(mem); i++) mem[i] = uint8_t(i * 17);
    uint8_t snapshot[sizeof(mem)];
    for (size_t i = 0; i < sizeof(mem); i++) snapshot[i] = mem[i];

    int x = b.DrawText(0, 0, Font5x7, "Hello", DrawOp::Keep);
    AssertEqual(x, 30);
    for (size_t i = 0; i < sizeof(mem); i++)
        AssertEqual((unsigned)mem[i], (unsigned)snapshot[i]);
}

TEST_CASE("07 Missing glyph uses substituted width")
{
    // Font5x7 covers 0x20..0x7E; 0x01 is outside that range
    char outsideRange[2] = { 0x01, 0 };
    int width = MonoBuffer::MeasureText(Font5x7, outsideRange);
    // missingWidth (5) + spacing (1) = 6
    AssertEqual(width, 6);
}

}

/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * Clip.cpp - the write clip rectangle
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>
#include <mono-gfx/fonts/Font5x7.h>

namespace
{

int Count(const MonoBuffer& b)
{
    int n = 0;
    for (int y = 0; y < b.Height(); y++)
        for (int x = 0; x < b.Width(); x++)
            if (b.GetPixel(x, y)) n++;
    return n;
}

TEST_CASE("01 Writes are confined to the clip")
{
    uint8_t mem[4 * 16] = {};
    MonoBuffer b(mem, 32, 16);
    b.SetClip(8, 4, 16, 8);

    b.FillRect(0, 0, 32, 16);           // rect
    b.DrawHLine(0, 2, 32);              // fully outside rows
    b.DrawVLine(2, 0, 16);              // fully outside columns
    b.DrawLine(0, 0, 31, 15);           // per-pixel path
    b.FillCircle(0, 0, 6);              // span path
    b.DrawText(0, 0, Font5x7, "XXXXXX");

    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 32; x++)
            if (b.GetPixel(x, y))
                Assert(x >= 8 && x < 24 && y >= 4 && y < 12);
    // the clipped FillRect filled the whole clip region
    for (int y = 4; y < 12; y++)
        for (int x = 8; x < 24; x++)
            AssertEqual(b.GetPixel(x, y), true);
}

TEST_CASE("02 Clear and FillAll respect the clip")
{
    uint8_t mem[4 * 16];
    MonoBuffer b(mem, 32, 16);
    b.FillAll();
    b.SetClip(8, 4, 16, 8);
    b.Clear();
    AssertEqual(Count(b), 32 * 16 - 16 * 8);
    b.FillAll();
    AssertEqual(Count(b), 32 * 16);

    b.ClearClip();
    b.Clear();
    AssertEqual(Count(b), 0);
}

TEST_CASE("03 Runs straddling the clip edge are trimmed")
{
    uint8_t mem[4 * 8] = {};
    MonoBuffer b(mem, 32, 8);
    b.SetClip(10, 2, 10, 4);            // x 10..19, y 2..5

    b.DrawHLine(5, 3, 20);              // 5..24 -> 10..19
    for (int x = 0; x < 32; x++)
        AssertEqual(b.GetPixel(x, 3), x >= 10 && x < 20);

    b.DrawVLine(12, 0, 8);              // 0..7 -> 2..5
    for (int y = 0; y < 8; y++)
        AssertEqual(b.GetPixel(12, y), y >= 2 && y <= 5 || y == 3 /* hline */);
}

TEST_CASE("04 SetClip clamps to the buffer and can be empty")
{
    uint8_t mem[4 * 8] = {};
    MonoBuffer b(mem, 32, 8);
    b.SetClip(-10, -10, 100, 100);      // oversized -> whole buffer
    AssertEqual(b.HasClip(), false);
    b.FillRect(0, 0, 32, 8);
    AssertEqual(Count(b), 32 * 8);

    b.SetClip(5, 5, 0, 0);              // empty -> nothing drawn
    b.Clear();
    AssertEqual(Count(b), 32 * 8);
}

TEST_CASE("05 Blit clips its destination")
{
    uint8_t src[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    MonoBuffer sb(src, 8, 8);
    uint8_t mem[4 * 8] = {};
    MonoBuffer b(mem, 32, 8);
    b.SetClip(10, 2, 4, 4);
    b.Blit(8, 0, sb, BlitOp::Or);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 32; x++)
            AssertEqual(b.GetPixel(x, y), x >= 10 && x < 14 && y >= 2 && y < 6);
}

TEST_CASE("06 BlitRotated honours the clip")
{
    uint8_t src[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    MonoBuffer sb(src, 8, 8);
    uint8_t mem[4 * 16] = {};
    MonoBuffer b(mem, 32, 16);
    b.SetClip(10, 4, 6, 6);
    b.BlitRotated(10, 4, sb, 0, 0, 0);          // 0deg == straight copy
    bool any = false;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 32; x++)
            if (b.GetPixel(x, y))
            {
                any = true;
                Assert(x >= 10 && x < 16 && y >= 4 && y < 10);
            }
    Assert(any);                                // it actually drew something
}

TEST_CASE("07 A huge extent cannot overflow the clip into an unbounded run")
{
    uint8_t mem[4 * 8] = {};
    MonoBuffer b(mem, 32, 8);
    // x + width overflows int; the clamp must still bound the run
    b.FillRect(4, 0, 0x7FFFFFFF, 2);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 32; x++)
            AssertEqual(b.GetPixel(x, y), y < 2 && x >= 4);
    AssertEqual(b.GetPixel(31, 0), true);       // filled up to the edge
    AssertEqual(b.GetPixel(31, 2), false);      // but not past the height
}

}

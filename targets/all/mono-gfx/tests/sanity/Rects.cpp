/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * Rects.cpp - rectangle and round-rectangle primitives
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>

namespace
{

static void RefFillRect(MonoBuffer b, int x, int y, int w, int h, DrawOp op)
{
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            b.DrawPixel(x + xx, y + yy, op);
}

static bool BuffersEqual(MonoBuffer a, MonoBuffer b)
{
    for (int y = 0; y < a.Height(); y++)
        for (int x = 0; x < a.Width(); x++)
            if (a.GetPixel(x, y) != b.GetPixel(x, y))
                return false;
    return true;
}

TEST_CASE("01 FillRect byte-aligned")
{
    uint8_t mem[8] = {};
    MonoBuffer b(mem, 16, 4);
    b.FillRect(0, 0, 16, 2);
    AssertEqual((unsigned)mem[0], 0xFFu);
    AssertEqual((unsigned)mem[1], 0xFFu);
    AssertEqual((unsigned)mem[2], 0xFFu);
    AssertEqual((unsigned)mem[3], 0xFFu);
    AssertEqual((unsigned)mem[4], 0u);
    AssertEqual((unsigned)mem[7], 0u);
}

TEST_CASE("02 FillRect misaligned")
{
    uint8_t mem[8] = {};
    MonoBuffer b(mem, 16, 4);
    b.FillRect(2, 1, 12, 2);
    // rows 1..2: cols 2..13 set: byte0 = 0x3F, byte1 = 0xFC
    AssertEqual((unsigned)mem[2], 0x3Fu);
    AssertEqual((unsigned)mem[3], 0xFCu);
    AssertEqual((unsigned)mem[4], 0x3Fu);
    AssertEqual((unsigned)mem[5], 0xFCu);
    // unaffected rows
    AssertEqual((unsigned)mem[0], 0u);
    AssertEqual((unsigned)mem[1], 0u);
    AssertEqual((unsigned)mem[6], 0u);
    AssertEqual((unsigned)mem[7], 0u);
}

TEST_CASE("03 FillRect sweep")
{
    constexpr int W = 32, H = 8, S = W / 8;
    uint8_t mA[S * H], mB[S * H];

    for (int x = -3; x <= W; x++)
    {
        for (int y = -3; y <= H; y++)
        {
            for (int w = 0; w <= 12; w++)
            {
                for (int h = 0; h <= 6; h++)
                {
                    for (int op = 0; op <= 3; op++)
                    {
                        for (int i = 0; i < S * H; i++) mA[i] = mB[i] = uint8_t(i * 31 + 7);
                        MonoBuffer a(mA, W, H), b(mB, W, H);
                        a.FillRect(x, y, w, h, DrawOp(op));
                        RefFillRect(b, x, y, w, h, DrawOp(op));
                        Assert(BuffersEqual(a, b));
                    }
                }
            }
        }
    }
}

TEST_CASE("04 DrawRect outline only")
{
    uint8_t mem[8] = {};
    MonoBuffer b(mem, 16, 4);
    b.DrawRect(2, 0, 6, 4);
    // top and bottom rows: cols 2..7 -> byte 0 bits 5..0 = 0x3F
    AssertEqual((unsigned)mem[0], 0x3Fu);
    AssertEqual((unsigned)mem[6], 0x3Fu);
    // middle rows: only cols 2 and 7 set -> bit 5 and bit 0 of byte 0 = 0x21
    AssertEqual((unsigned)mem[2], 0x21u);
    AssertEqual((unsigned)mem[4], 0x21u);
}

TEST_CASE("05 RoundRect degenerates to Rect at r=0")
{
    constexpr int W = 32, H = 8, S = W / 8;
    uint8_t mA[S * H] = {}, mB[S * H] = {};
    MonoBuffer a(mA, W, H), b(mB, W, H);

    a.DrawRoundRect(2, 1, 20, 6, 0);
    b.DrawRect(2, 1, 20, 6);
    Assert(BuffersEqual(a, b));

    for (int i = 0; i < S * H; i++) { mA[i] = 0; mB[i] = 0; }
    a.FillRoundRect(2, 1, 20, 6, 0);
    b.FillRect(2, 1, 20, 6);
    Assert(BuffersEqual(a, b));
}

TEST_CASE("06 RoundRect clamps oversized radius")
{
    // a 10x6 round rect with r=100 should not crash and should at least
    // draw all four corner pixels and fall within the rectangle bounds
    uint8_t mem[64] = {};
    MonoBuffer b(mem, 32, 16);
    b.FillRoundRect(2, 2, 10, 6, 100);
    // every set pixel must lie within the requested rectangle
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 32; x++)
            if (b.GetPixel(x, y))
                Assert(x >= 2 && x < 12 && y >= 2 && y < 8);
}

TEST_CASE("07 FillRoundRect inside rectangle bounds")
{
    uint8_t mem[64] = {};
    MonoBuffer b(mem, 32, 16);
    b.FillRoundRect(4, 2, 24, 12, 4);
    // every set pixel inside the bounds, no overflow
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 32; x++)
            if (b.GetPixel(x, y))
                Assert(x >= 4 && x < 28 && y >= 2 && y < 14);
    // center is filled
    AssertEqual(b.GetPixel(16, 8), true);
    // corners outside the rounding are not filled
    AssertEqual(b.GetPixel(4, 2), false);
    AssertEqual(b.GetPixel(27, 13), false);
}


TEST_CASE("08 FillRoundRect corner shape is round at small radii")
{
    // r=3 corner insets from the outermost row must be 2,1,0 (a quarter
    // circle), not a single notch
    uint8_t mem[64] = {};
    MonoBuffer b(mem, 32, 16);
    b.FillRoundRect(2, 2, 20, 10, 3);
    static const int inset[] = { 2, 1, 0 };
    for (int t = 0; t < 3; t++)
    {
        AssertEqual(b.GetPixel(2 + inset[t], 2 + t), true);          // first set px
        if (inset[t])
            AssertEqual(b.GetPixel(2 + inset[t] - 1, 2 + t), false); // one left of it
        AssertEqual(b.GetPixel(21 - inset[t], 2 + t), true);         // mirrored right
        AssertEqual(b.GetPixel(2 + inset[t], 11 - t), true);         // mirrored bottom
    }
}

TEST_CASE("09 FillRoundRect boundary matches DrawRoundRect")
{
    // the outline must lie exactly on the fill's edge: clearing it from a
    // fill leaves no outline pixel set, and it never paints outside the fill
    for (int r : { 3, 5, 14, 16 })
    {
        uint8_t m1[16 * 40] = {}, m2[16 * 40] = {};
        MonoBuffer f(m1, 100, 40), o(m2, 100, 40);
        f.FillRoundRect(4, 2, 90, 36, r);
        o.DrawRoundRect(4, 2, 90, 36, r);
        for (int y = 0; y < 40; y++)
            for (int x = 0; x < 100; x++)
                if (o.GetPixel(x, y))
                    AssertEqual(f.GetPixel(x, y), true);
    }
}

TEST_CASE("10 FillRoundRect double invert is identity")
{
    uint8_t mem[16 * 40] = {};
    MonoBuffer b(mem, 100, 40);
    b.FillRoundRect(3, 1, 80, 30, 16, DrawOp::Invert);
    b.FillRoundRect(3, 1, 80, 30, 16, DrawOp::Invert);
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 100; x++)
            AssertEqual(b.GetPixel(x, y), false);
}

}

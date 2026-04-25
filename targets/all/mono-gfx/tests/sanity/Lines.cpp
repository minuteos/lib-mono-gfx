/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * Lines.cpp - exhaustive verification of horizontal/vertical line drawing
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>

namespace
{

//! Per-pixel reference implementation for DrawHLine
static void RefHLine(MonoBuffer b, int x, int y, int w, DrawOp op)
{
    for (int i = 0; i < w; i++) b.DrawPixel(x + i, y, op);
}

//! Per-pixel reference implementation for DrawVLine
static void RefVLine(MonoBuffer b, int x, int y, int h, DrawOp op)
{
    for (int i = 0; i < h; i++) b.DrawPixel(x, y + i, op);
}

//! Compares two buffers pixel-by-pixel
static bool BuffersEqual(MonoBuffer a, MonoBuffer b)
{
    for (int y = 0; y < a.Height(); y++)
        for (int x = 0; x < a.Width(); x++)
            if (a.GetPixel(x, y) != b.GetPixel(x, y))
                return false;
    return true;
}

TEST_CASE("01 HLine byte-aligned")
{
    uint8_t mem[12] = {};
    MonoBuffer b(mem, 24, 4);
    b.DrawHLine(0, 0, 24);
    AssertEqual((unsigned)mem[0], 0xFFu);
    AssertEqual((unsigned)mem[1], 0xFFu);
    AssertEqual((unsigned)mem[2], 0xFFu);
    AssertEqual((unsigned)mem[3], 0u);
}

TEST_CASE("02 HLine misaligned")
{
    uint8_t mem[2] = {};
    MonoBuffer b(mem, 16, 1);
    b.DrawHLine(3, 0, 10);
    // bits [3..12] set: byte 0 = 0x1F, byte 1 = 0xF8
    AssertEqual((unsigned)mem[0], 0x1Fu);
    AssertEqual((unsigned)mem[1], 0xF8u);
}

TEST_CASE("03 HLine within single byte")
{
    uint8_t mem[1] = {};
    MonoBuffer b(mem, 8, 1);
    b.DrawHLine(2, 0, 4);
    // bits 5..2 set
    AssertEqual((unsigned)mem[0], 0x3Cu);
}

TEST_CASE("04 HLine sweep")
{
    constexpr int W = 32, H = 4, S = W / 8;
    uint8_t mA[S * H], mB[S * H];

    for (int x = -3; x <= W + 3; x++)
    {
        for (int w = 0; w <= W + 5; w++)
        {
            for (int op = 0; op <= 3; op++)
            {
                for (int i = 0; i < S * H; i++) mA[i] = mB[i] = uint8_t(i * 31 + 5);
                MonoBuffer a(mA, W, H), b(mB, W, H);
                a.DrawHLine(x, 2, w, DrawOp(op));
                RefHLine(b, x, 2, w, DrawOp(op));
                Assert(BuffersEqual(a, b));
            }
        }
    }
}

TEST_CASE("05 VLine sweep")
{
    constexpr int W = 32, H = 8, S = W / 8;
    uint8_t mA[S * H], mB[S * H];

    for (int x = -3; x <= W + 3; x++)
    {
        for (int y = -3; y <= H + 3; y++)
        {
            for (int h = 0; h <= H + 5; h++)
            {
                for (int op = 0; op <= 3; op++)
                {
                    for (int i = 0; i < S * H; i++) mA[i] = mB[i] = uint8_t(i * 17 + 11);
                    MonoBuffer a(mA, W, H), b(mB, W, H);
                    a.DrawVLine(x, y, h, DrawOp(op));
                    RefVLine(b, x, y, h, DrawOp(op));
                    Assert(BuffersEqual(a, b));
                }
            }
        }
    }
}

TEST_CASE("06 DrawLine axis-aligned matches HLine/VLine")
{
    constexpr int W = 24, H = 8, S = W / 8;
    uint8_t mA[S * H] = {}, mB[S * H] = {};
    MonoBuffer a(mA, W, H), b(mB, W, H);
    a.DrawLine(2, 3, 20, 3);
    b.DrawHLine(2, 3, 19);
    Assert(BuffersEqual(a, b));

    for (int i = 0; i < S * H; i++) { mA[i] = 0; mB[i] = 0; }
    a.DrawLine(5, 1, 5, 6);
    b.DrawVLine(5, 1, 6);
    Assert(BuffersEqual(a, b));
}

TEST_CASE("07 DrawLine reversed endpoints")
{
    constexpr int W = 16, H = 8, S = W / 8;
    uint8_t mA[S * H] = {}, mB[S * H] = {};
    MonoBuffer a(mA, W, H), b(mB, W, H);
    // a horizontal line drawn right-to-left should produce the same pixels
    a.DrawLine(12, 4, 2, 4);
    b.DrawLine(2, 4, 12, 4);
    Assert(BuffersEqual(a, b));
}

}

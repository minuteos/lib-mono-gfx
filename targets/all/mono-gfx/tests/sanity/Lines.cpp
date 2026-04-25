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

TEST_CASE("08 Long off-buffer diagonal does not draw or hang")
{
    // Without endpoint clipping, this would iterate ~1000 Bresenham steps
    // and call DrawPixel on every one of them. With clipping the visible
    // segment is the buffer's main diagonal.
    uint8_t mem[3 * 20] = {};
    MonoBuffer b(mem, 20, 20);
    b.DrawLine(0, 0, 1000, 1000);
    // every pixel on the main diagonal must be set
    for (int i = 0; i < 20; i++) AssertEqual(b.GetPixel(i, i), true);
    // off-diagonal pixels must remain clear
    for (int y = 0; y < 20; y++)
        for (int x = 0; x < 20; x++)
            if (x != y) AssertEqual(b.GetPixel(x, y), false);
}

TEST_CASE("09 Line entirely outside is rejected")
{
    uint8_t mem[3 * 20] = {};
    MonoBuffer b(mem, 20, 20);
    // diagonal that enters x>=0 only at y=50, well past the buffer
    b.DrawLine(-50, 0, 950, 1000);
    for (size_t i = 0; i < sizeof(mem); i++)
        AssertEqual((unsigned)mem[i], 0u);

    // line entirely above
    b.DrawLine(-100, -10, 100, -5);
    // line entirely below
    b.DrawLine(0, 25, 50, 30);
    // line entirely left
    b.DrawLine(-10, 0, -5, 19);
    // line entirely right
    b.DrawLine(25, 0, 50, 19);
    for (size_t i = 0; i < sizeof(mem); i++)
        AssertEqual((unsigned)mem[i], 0u);
}

TEST_CASE("10 Fully-inside lines are unaffected by clipping")
{
    constexpr int W = 32, H = 16, S = W / 8;
    // for endpoints fully inside the buffer, the clipper must not change
    // anything; the output must be byte-for-byte identical to the
    // straightforward Bresenham this function falls back to
    auto naiveBresenham = [](MonoBuffer dst, int x0, int y0, int x1, int y1) {
        int dx = x1 - x0, dy = y1 - y0;
        int sx = dx > 0 ? 1 : -1; if (dx < 0) dx = -dx;
        int sy = dy > 0 ? 1 : -1; if (dy < 0) dy = -dy;
        int err = dx - dy;
        for (;;)
        {
            dst.DrawPixel(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            int e2 = err << 1;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx)  { err += dx; y0 += sy; }
        }
    };

    for (int x0 = 0; x0 < W; x0 += 3)
        for (int y0 = 0; y0 < H; y0 += 3)
            for (int x1 = 0; x1 < W; x1 += 3)
                for (int y1 = 0; y1 < H; y1 += 3)
                {
                    if (x0 == x1 && y0 == y1) continue;
                    uint8_t mA[S * H] = {}, mB[S * H] = {};
                    MonoBuffer a(mA, W, H), b(mB, W, H);
                    a.DrawLine(x0, y0, x1, y1);
                    naiveBresenham(b, x0, y0, x1, y1);
                    Assert(BuffersEqual(a, b));
                }
}

TEST_CASE("11 Clipped lines stay within the buffer")
{
    // probe with deliberately huge coordinates to exercise the 64-bit
    // intermediate math; no pixels should ever be drawn outside the
    // buffer (the per-pixel guard would mask such a regression, but we
    // still want to know the clipper computes sensible endpoints)
    constexpr int W = 20, H = 20, S = W / 8 + 1;
    int probes[][4] = {
        { -1000, -1000, 1000, 1000 },
        { -1000000, 0, 1000000, 19 },
        { 10, -500, 10, 500 },
        { -500, 10, 500, 10 },
        { 30, -10, -10, 30 },
        { 19, 0, 0, 19 },
        { 100000, -100000, -100000, 100000 },
    };
    for (auto& p : probes)
    {
        uint8_t mem[S * H] = {};
        MonoBuffer b(mem, W, H);
        b.DrawLine(p[0], p[1], p[2], p[3]);
        // every set pixel must lie within the buffer
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (b.GetPixel(x, y))
                    Assert(x >= 0 && x < W && y >= 0 && y < H);
    }
}

}

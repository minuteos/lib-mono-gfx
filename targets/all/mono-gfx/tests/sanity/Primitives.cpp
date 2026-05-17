/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * Primitives.cpp - arc, triangle and rotated-blit primitive parity
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>

namespace
{

int CountSet(const MonoBuffer& b)
{
    int n = 0;
    for (int y = 0; y < b.Height(); y++)
        for (int x = 0; x < b.Width(); x++)
            if (b.GetPixel(x, y)) n++;
    return n;
}

TEST_CASE("01 DrawArc r=0 paints single pixel")
{
    uint8_t mem[8] = {};
    MonoBuffer b(mem, 8, 8);
    b.DrawArc(3, 3, 0, 0, 270);
    AssertEqual(b.GetPixel(3, 3), true);
    AssertEqual(CountSet(b), 1);
}

TEST_CASE("02 DrawArc pixels lie on the radius band")
{
    uint8_t mem[6 * 48] = {};
    MonoBuffer b(mem, 48, 48);
    int cx = 24, cy = 24, r = 18;
    b.DrawArc(cx, cy, r, 0, 359);
    Assert(CountSet(b) > 0);
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 48; x++)
            if (b.GetPixel(x, y))
            {
                int dx = x - cx, dy = y - cy;
                int d2 = dx * dx + dy * dy;
                Assert(d2 <= (r + 2) * (r + 2));
                Assert(d2 >= (r - 2) * (r - 2));
            }
}

TEST_CASE("03 DrawArc 0..90 stays in the +X/+Y quadrant")
{
    uint8_t mem[5 * 40] = {};
    MonoBuffer b(mem, 40, 40);
    int cx = 20, cy = 20, r = 15;
    b.DrawArc(cx, cy, r, 0, 90);
    Assert(CountSet(b) > 0);
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 40; x++)
            if (b.GetPixel(x, y))
            {
                Assert(x >= cx - 1);
                Assert(y >= cy - 1);
            }
    // endpoints: 0deg -> (cx+r, cy), 90deg -> (cx, cy+r)
    Assert(b.GetPixel(cx + r, cy));
    Assert(b.GetPixel(cx, cy + r));
    // the opposite side is untouched
    AssertEqual(b.GetPixel(cx - r, cy), false);
}

TEST_CASE("04 DrawArc is continuous (no gaps) at a large radius")
{
    uint8_t mem[12 * 96] = {};
    MonoBuffer b(mem, 96, 96);
    int cx = 48, cy = 48, r = 44;
    b.DrawArc(cx, cy, r, 0, 360);
    // every set pixel must have at least one set 8-neighbour
    for (int y = 1; y < 95; y++)
        for (int x = 1; x < 95; x++)
            if (b.GetPixel(x, y))
            {
                int adj = 0;
                for (int j = -1; j <= 1; j++)
                    for (int i = -1; i <= 1; i++)
                        if ((i || j) && b.GetPixel(x + i, y + j)) adj++;
                Assert(adj >= 1);
            }
}

TEST_CASE("05 FillTriangle is solid (every interior point set)")
{
    uint8_t mem[4 * 32] = {};
    MonoBuffer b(mem, 32, 32);
    int ax = 2, ay = 2, bx = 28, by = 8, cx = 10, cy = 30;
    b.FillTriangle(ax, ay, bx, by, cx, cy);

    // vertices and centroid are inside
    Assert(b.GetPixel(ax, ay));
    Assert(b.GetPixel(bx, by));
    Assert(b.GetPixel(cx, cy));
    Assert(b.GetPixel((ax + bx + cx) / 3, (ay + by + cy) / 3));

    // any point strictly inside (positive barycentric weights) must be set
    int d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
        {
            int w0 = ((by - cy) * (x - cx) + (cx - bx) * (y - cy));
            int w1 = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy));
            int w2 = d - w0 - w1;
            bool inside = (w0 > 0 && w1 > 0 && w2 > 0) ||
                          (w0 < 0 && w1 < 0 && w2 < 0);
            if (inside)
                Assert(b.GetPixel(x, y));
        }
}

TEST_CASE("06 FillTriangle right triangle has exact area")
{
    uint8_t mem[2 * 16] = {};
    MonoBuffer b(mem, 16, 16);
    // axis-aligned right triangle: (0,0)-(10,0)-(0,10)
    b.FillTriangle(0, 0, 10, 0, 0, 10);
    // row y has pixels x in [0, 10-y]
    for (int y = 0; y <= 10; y++)
    {
        for (int x = 0; x <= 10 - y; x++)
            AssertEqual(b.GetPixel(x, y), true);
        AssertEqual(b.GetPixel(11 - y, y), false);
    }
    AssertEqual(b.GetPixel(0, 11), false);
}

TEST_CASE("07 FillTriangle degenerate collinear is a line")
{
    uint8_t mem[2 * 8] = {};
    MonoBuffer b(mem, 16, 8);
    b.FillTriangle(2, 3, 8, 3, 12, 3);
    for (int x = 2; x <= 12; x++)
        AssertEqual(b.GetPixel(x, 3), true);
    AssertEqual(b.GetPixel(1, 3), false);
    AssertEqual(b.GetPixel(13, 3), false);
    AssertEqual(b.GetPixel(5, 2), false);
}

TEST_CASE("07b FillTriangle flat-bottom draws its bottom row")
{
    uint8_t mem[2 * 16] = {};
    MonoBuffer b(mem, 16, 16);
    // apex at top, flat bottom edge along y == 10 from x=0 to x=10
    b.FillTriangle(5, 0, 0, 10, 10, 10);
    Assert(b.GetPixel(5, 0));               // apex
    for (int x = 0; x <= 10; x++)
        AssertEqual(b.GetPixel(x, 10), true);   // entire bottom edge present
    Assert(b.GetPixel(5, 5));               // interior solid
    AssertEqual(b.GetPixel(0, 0), false);
}

TEST_CASE("08 FillTriangle stays within its bounding box")
{
    uint8_t mem[4 * 32] = {};
    MonoBuffer b(mem, 32, 32);
    b.FillTriangle(5, 4, 26, 10, 14, 29, DrawOp::Set);
    int minx = 5, maxx = 26, miny = 4, maxy = 29;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            if (b.GetPixel(x, y))
            {
                Assert(x >= minx && x <= maxx);
                Assert(y >= miny && y <= maxy);
            }
}

TEST_CASE("09 BlitRotated by 0 degrees is a plain mask stamp")
{
    uint8_t sbuf[3] = { 0xE0, 0xA0, 0xE0 };     // 3x3 'C'-ish glyph
    MonoBuffer src(sbuf, 3, 3);

    uint8_t dbuf[2 * 16] = {};
    MonoBuffer dst(dbuf, 16, 16);
    dst.BlitRotated(5, 6, src, 0, 0, 0);

    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++)
            AssertEqual(dst.GetPixel(5 + x, 6 + y), src.GetPixel(x, y));
    // nothing leaked outside the 3x3 footprint
    AssertEqual(CountSet(dst), CountSet(src));
}

TEST_CASE("10 BlitRotated 90 degrees maps axes correctly")
{
    // a 4x1 horizontal bar
    uint8_t sbuf[1] = { 0xF0 };
    MonoBuffer src(sbuf, 4, 1);

    uint8_t dbuf[2 * 16] = {};
    MonoBuffer dst(dbuf, 16, 16);
    // rotate 90deg about the bar's left end, placing that pivot at (8,8):
    // +X of the source should map onto +Y of the destination
    dst.BlitRotated(8, 8, src, 0, 0, 90);

    int set = CountSet(dst);
    Assert(set >= 3 && set <= 5);       // ~4 pixels, allow nearest-neighbour slack
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            if (dst.GetPixel(x, y))
            {
                Assert(x >= 7 && x <= 9);   // stayed on the vertical axis
                Assert(y >= 8 && y <= 12);  // swept downward (+Y)
            }
}

TEST_CASE("11 BlitRotated honours the DrawOp and source mask")
{
    uint8_t sbuf[1] = { 0xC0 };         // two set pixels on a 2x1 source
    MonoBuffer src(sbuf, 2, 1);

    uint8_t dbuf[1 * 8] = {};
    MonoBuffer dst(dbuf, 8, 8);
    dst.FillRect(0, 0, 8, 8);           // all foreground

    dst.BlitRotated(2, 2, src, 0, 0, 0, DrawOp::Clear);
    // exactly the two source pixels were cleared, the rest stays set
    AssertEqual(dst.GetPixel(2, 2), false);
    AssertEqual(dst.GetPixel(3, 2), false);
    AssertEqual(CountSet(dst), 8 * 8 - 2);
}

TEST_CASE("12 BlitRotated clips against the destination")
{
    uint8_t sbuf[8] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    MonoBuffer src(sbuf, 8, 8);
    src.FillAll();

    uint8_t dbuf[1 * 8] = {};
    MonoBuffer dst(dbuf, 8, 8);
    // pivot placed off the top-left corner; only part of the rotated block
    // should land on screen and nothing must be written out of bounds
    dst.BlitRotated(-2, -2, src, 0, 0, 30);
    Assert(CountSet(dst) > 0);
    Assert(CountSet(dst) < 64);
}

}

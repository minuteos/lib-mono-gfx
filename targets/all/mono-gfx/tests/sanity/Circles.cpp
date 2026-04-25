/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * Circles.cpp - midpoint circle primitives
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>

namespace
{

TEST_CASE("01 DrawCircle r=0 paints single pixel")
{
    uint8_t mem[8] = {};
    MonoBuffer b(mem, 8, 8);
    b.DrawCircle(3, 3, 0);
    AssertEqual(b.GetPixel(3, 3), true);
    // exactly one pixel set
    int count = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            if (b.GetPixel(x, y)) count++;
    AssertEqual(count, 1);
}

TEST_CASE("02 DrawCircle is symmetric")
{
    uint8_t mem[4 * 32] = {};
    MonoBuffer b(mem, 32, 32);
    b.DrawCircle(16, 16, 8);
    // every set pixel (x, y) should have its three mirror points set too
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            if (b.GetPixel(x, y))
            {
                Assert(b.GetPixel(32 - x, y));
                Assert(b.GetPixel(x, 32 - y));
                Assert(b.GetPixel(32 - x, 32 - y));
            }
}

TEST_CASE("03 DrawCircle stays inside bounding box")
{
    uint8_t mem[8 * 16] = {};
    MonoBuffer b(mem, 64, 16);
    int cx = 32, cy = 8, r = 6;
    b.DrawCircle(cx, cy, r);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 64; x++)
            if (b.GetPixel(x, y))
            {
                int dx = x - cx, dy = y - cy;
                Assert(dx * dx + dy * dy <= (r + 1) * (r + 1));
                Assert(dx * dx + dy * dy >= (r - 1) * (r - 1));
            }
}

TEST_CASE("04 FillCircle covers center and DrawCircle pixels")
{
    uint8_t memA[4 * 32] = {};
    uint8_t memB[4 * 32] = {};
    MonoBuffer outline(memA, 32, 32);
    MonoBuffer filled(memB, 32, 32);
    outline.DrawCircle(16, 16, 7);
    filled.FillCircle(16, 16, 7);

    // every outline pixel must also be set in the filled version
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            if (outline.GetPixel(x, y))
                Assert(filled.GetPixel(x, y));

    // the center is set in the filled version
    AssertEqual(filled.GetPixel(16, 16), true);
    // a far away pixel is not
    AssertEqual(filled.GetPixel(0, 0), false);
}

TEST_CASE("05 Circle clipped at corner")
{
    // a circle centered off-buffer should still clip correctly
    uint8_t mem[2 * 8] = {};
    MonoBuffer b(mem, 16, 8);
    b.DrawCircle(-3, -3, 8);
    // some pixels visible
    int count = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 16; x++)
            if (b.GetPixel(x, y)) count++;
    Assert(count > 0);
}

}

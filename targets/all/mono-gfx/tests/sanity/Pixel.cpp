/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * Pixel.cpp - basic pixel access and clipping
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>

namespace
{

TEST_CASE("01 Construction")
{
    uint8_t mem[16] = {};
    MonoBuffer empty;
    AssertEqual(empty.Width(), 0);
    AssertEqual(empty.Height(), 0);
    AssertEqual(empty.IsEmpty(), true);

    MonoBuffer tight(mem, 32, 4);
    AssertEqual(tight.Width(), 32);
    AssertEqual(tight.Height(), 4);
    AssertEqual(tight.Stride(), 4);
    AssertEqual(tight.Size(), 16u);
    AssertEqual(tight.IsEmpty(), false);

    uint8_t big[64];
    MonoBuffer padded(big, 32, 4, 16);
    AssertEqual(padded.Stride(), 16);
    AssertEqual(padded.Size(), 64u);
}

TEST_CASE("02 Pixel set and get")
{
    uint8_t mem[8] = {};
    MonoBuffer b(mem, 8, 8);
    b.DrawPixel(0, 0);
    b.DrawPixel(7, 0);
    b.DrawPixel(3, 4);
    AssertEqual(b.GetPixel(0, 0), true);
    AssertEqual(b.GetPixel(7, 0), true);
    AssertEqual(b.GetPixel(3, 4), true);
    AssertEqual(b.GetPixel(1, 0), false);
    AssertEqual(b.GetPixel(3, 5), false);

    // Bit positions: column 0 is MSB, column 7 is LSB
    AssertEqual((unsigned)mem[0], 0x81u);
    AssertEqual((unsigned)mem[4], 0x10u);
}

TEST_CASE("03 Pixel out-of-bounds clipping")
{
    uint8_t mem[8] = {};
    MonoBuffer b(mem, 8, 8);

    b.DrawPixel(-1, 0);
    b.DrawPixel(0, -1);
    b.DrawPixel(8, 0);
    b.DrawPixel(0, 8);
    b.DrawPixel(-100, -100);
    b.DrawPixel(1000, 1000);

    for (size_t i = 0; i < sizeof(mem); i++)
        AssertEqual((unsigned)mem[i], 0u);

    AssertEqual(b.GetPixel(-1, 0), false);
    AssertEqual(b.GetPixel(8, 0), false);
    AssertEqual(b.GetPixel(0, 8), false);
}

TEST_CASE("04 DrawOp variants")
{
    uint8_t mem[1] = { 0xAA };
    MonoBuffer b(mem, 8, 1);

    b.DrawPixel(0, 0, DrawOp::Clear);
    AssertEqual((unsigned)mem[0], 0x2Au);

    b.DrawPixel(0, 0, DrawOp::Set);
    AssertEqual((unsigned)mem[0], 0xAAu);

    b.DrawPixel(0, 0, DrawOp::Invert);
    AssertEqual((unsigned)mem[0], 0x2Au);

    b.DrawPixel(0, 0, DrawOp::Keep);
    AssertEqual((unsigned)mem[0], 0x2Au);
}

TEST_CASE("05 Fill helpers")
{
    uint8_t mem[16] = {};
    MonoBuffer b(mem, 32, 4);

    b.FillAll();
    for (size_t i = 0; i < sizeof(mem); i++)
        AssertEqual((unsigned)mem[i], 0xFFu);

    b.Clear();
    for (size_t i = 0; i < sizeof(mem); i++)
        AssertEqual((unsigned)mem[i], 0u);

    mem[0] = 0xA5;
    mem[5] = 0x3C;
    b.InvertAll();
    AssertEqual((unsigned)mem[0], 0x5Au);
    AssertEqual((unsigned)mem[5], 0xC3u);
    AssertEqual((unsigned)mem[15], 0xFFu);
}

}

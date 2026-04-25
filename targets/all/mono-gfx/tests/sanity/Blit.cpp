/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * Blit.cpp - exhaustive byte-aligned and bit-shifted blit verification
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>

namespace
{

//! Per-pixel reference for Blit, mirroring the implementation's clip rules
static void RefBlit(MonoBuffer dst, int dx, int dy, MonoBuffer src,
    int sx, int sy, int w, int h, BlitOp op)
{
    if (sx < 0) { dx -= sx; w += sx; sx = 0; }
    if (sy < 0) { dy -= sy; h += sy; sy = 0; }
    if (sx + w > src.Width())  w = src.Width()  - sx;
    if (sy + h > src.Height()) h = src.Height() - sy;
    if (dx < 0) { sx -= dx; w += dx; dx = 0; }
    if (dy < 0) { sy -= dy; h += dy; dy = 0; }
    if (dx + w > dst.Width())  w = dst.Width()  - dx;
    if (dy + h > dst.Height()) h = dst.Height() - dy;
    if (w <= 0 || h <= 0) return;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            bool s = src.GetPixel(sx + x, sy + y);
            bool d = dst.GetPixel(dx + x, dy + y);
            bool nd;
            switch (op)
            {
                case BlitOp::Copy:    nd = s; break;
                case BlitOp::CopyNot: nd = !s; break;
                case BlitOp::Or:      nd = d || s; break;
                case BlitOp::AndNot:  nd = d && !s; break;
                case BlitOp::Xor:     nd = d != s; break;
                case BlitOp::And:     nd = d && s; break;
                default:              nd = d; break;
            }
            dst.DrawPixel(dx + x, dy + y, nd ? DrawOp::Set : DrawOp::Clear);
        }
    }
}

static bool MemEqual(const uint8_t* a, const uint8_t* b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i]) return false;
    return true;
}

TEST_CASE("01 Aligned Copy")
{
    uint8_t srcMem[3] = { 0xAB, 0xCD, 0xEF };
    uint8_t dstMem[3] = {};
    MonoBuffer src(srcMem, 24, 1);
    MonoBuffer dst(dstMem, 24, 1);
    dst.Blit(0, 0, src);
    AssertEqual((unsigned)dstMem[0], 0xABu);
    AssertEqual((unsigned)dstMem[1], 0xCDu);
    AssertEqual((unsigned)dstMem[2], 0xEFu);
}

TEST_CASE("02 Misaligned Copy across byte boundary")
{
    uint8_t srcMem[1] = { 0xFF };
    uint8_t dstMem[3] = {};
    MonoBuffer src(srcMem, 8, 1);
    MonoBuffer dst(dstMem, 24, 1);
    dst.Blit(3, 0, src);
    // dst columns 3..10 set
    AssertEqual((unsigned)dstMem[0], 0x1Fu);
    AssertEqual((unsigned)dstMem[1], 0xE0u);
    AssertEqual((unsigned)dstMem[2], 0u);
}

TEST_CASE("03 Misaligned source with offset")
{
    uint8_t srcMem[2] = { 0x07, 0x80 };  // src cols 5..8 set
    uint8_t dstMem[2] = {};
    MonoBuffer src(srcMem, 16, 1);
    MonoBuffer dst(dstMem, 16, 1);
    dst.Blit(0, 0, src, 5, 0, 4, 1);
    // dst cols 0..3 set -> top nibble of byte 0
    AssertEqual((unsigned)dstMem[0], 0xF0u);
    AssertEqual((unsigned)dstMem[1], 0u);
}

TEST_CASE("04 Exhaustive blit sweep")
{
    constexpr int W = 32, H = 8, S = W / 8;
    uint8_t srcMem[S * H];
    for (int i = 0; i < S * H; i++) srcMem[i] = uint8_t((i * 73 + 42) ^ 0xA5);
    MonoBuffer src(srcMem, W, H);

    for (int dx = -2; dx <= 10; dx++)
    {
        for (int sx = -2; sx <= 10; sx++)
        {
            for (int w = 1; w <= 18; w++)
            {
                for (int op = 0; op <= int(BlitOp::And); op++)
                {
                    uint8_t dstA[S * H], dstB[S * H];
                    for (int i = 0; i < S * H; i++) { dstA[i] = uint8_t(i * 17); dstB[i] = uint8_t(i * 17); }
                    MonoBuffer da(dstA, W, H), db(dstB, W, H);
                    da.Blit(dx, 0, src, sx, 0, w, 4, BlitOp(op));
                    RefBlit(db, dx, 0, src, sx, 0, w, 4, BlitOp(op));
                    Assert(MemEqual(dstA, dstB, S * H));
                }
            }
        }
    }
}

TEST_CASE("05 Blit fully out of bounds is a no-op")
{
    uint8_t srcMem[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t dstMem[4] = { 0x12, 0x34, 0x56, 0x78 };
    MonoBuffer src(srcMem, 32, 1);
    MonoBuffer dst(dstMem, 32, 1);

    dst.Blit(100, 0, src);
    AssertEqual((unsigned)dstMem[0], 0x12u);
    AssertEqual((unsigned)dstMem[3], 0x78u);

    dst.Blit(-100, 0, src);
    AssertEqual((unsigned)dstMem[0], 0x12u);
    AssertEqual((unsigned)dstMem[3], 0x78u);
}

}

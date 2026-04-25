/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * Format.cpp - format conversion to vertical-byte page mode
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>

namespace
{

//! Per-pixel reference for ToVerticalBytes
static void RefToVerticalBytes(const MonoBuffer& src, uint8_t* dst, int dstStride,
    MonoFormat::VerticalBitOrder order)
{
    int w = src.Width();
    int h = src.Height();
    int pages = (h + 7) / 8;
    for (int i = 0; i < pages * dstStride; i++) dst[i] = 0;
    for (int p = 0; p < pages; p++)
    {
        for (int c = 0; c < w; c++)
        {
            uint8_t v = 0;
            for (int r = 0; r < 8; r++)
            {
                int y = p * 8 + r;
                if (y >= h) break;
                if (src.GetPixel(c, y))
                {
                    if (order == MonoFormat::VerticalBitOrder::TopLSB)
                        v |= 1u << r;
                    else
                        v |= 1u << (7 - r);
                }
            }
            dst[p * dstStride + c] = v;
        }
    }
}

static bool MemEqual(const uint8_t* a, const uint8_t* b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i]) return false;
    return true;
}

TEST_CASE("01 Diagonal converts cleanly")
{
    // main diagonal: row r has only column r set (bit 7-r in byte r)
    uint8_t srcMem[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
    MonoBuffer src(srcMem, 8, 8);
    uint8_t out[8] = {};
    MonoFormat::ToVerticalBytes(src, out, 8, MonoFormat::VerticalBitOrder::TopLSB);
    // column c only has row c set; in TopLSB that's bit c, value (1 << c)
    for (int c = 0; c < 8; c++)
    {
        unsigned expect = 1u << c;
        AssertEqual((unsigned)out[c], expect);
    }
}

TEST_CASE("02 TopMSB is bit-reversed TopLSB")
{
    uint8_t srcMem[8];
    for (int i = 0; i < 8; i++) srcMem[i] = uint8_t(i * 53 + 17);
    MonoBuffer src(srcMem, 8, 8);

    uint8_t lsb[8] = {}, msb[8] = {};
    MonoFormat::ToVerticalBytes(src, lsb, 8, MonoFormat::VerticalBitOrder::TopLSB);
    MonoFormat::ToVerticalBytes(src, msb, 8, MonoFormat::VerticalBitOrder::TopMSB);

    auto reverse = [](uint8_t v) {
        v = uint8_t((v >> 4) | (v << 4));
        v = uint8_t(((v & 0xCC) >> 2) | ((v & 0x33) << 2));
        v = uint8_t(((v & 0xAA) >> 1) | ((v & 0x55) << 1));
        return v;
    };
    for (int c = 0; c < 8; c++)
        AssertEqual((unsigned)msb[c], (unsigned)reverse(lsb[c]));
}

TEST_CASE("03 Partial trailing page zero-fills missing rows")
{
    // 8 columns x 5 rows: only one page, but rows 5..7 do not exist
    uint8_t srcMem[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    MonoBuffer src(srcMem, 8, 5);
    uint8_t out[8] = {};
    MonoFormat::ToVerticalBytes(src, out, 8, MonoFormat::VerticalBitOrder::TopLSB);
    // each column has its bottom 5 bits set, top 3 bits clear -> 0x1F
    for (int c = 0; c < 8; c++)
        AssertEqual((unsigned)out[c], 0x1Fu);
}

TEST_CASE("04 Sweep against reference for irregular sizes")
{
    for (int W = 1; W <= 17; W++)
    {
        for (int H = 1; H <= 17; H++)
        {
            int stride = (W + 7) / 8;
            int srcSz = stride * H;
            uint8_t srcMem[64];
            for (int i = 0; i < srcSz; i++) srcMem[i] = uint8_t(i * 91 + 13);
            MonoBuffer src(srcMem, W, H);

            int pages = (H + 7) / 8;
            int dstSz = pages * W;
            uint8_t dstA[256] = {}, dstB[256] = {};
            for (int order = 0; order <= 1; order++)
            {
                for (int i = 0; i < dstSz; i++) { dstA[i] = 0; dstB[i] = 0; }
                MonoFormat::ToVerticalBytes(src, dstA, W, MonoFormat::VerticalBitOrder(order));
                RefToVerticalBytes(src, dstB, W, MonoFormat::VerticalBitOrder(order));
                Assert(MemEqual(dstA, dstB, dstSz));
            }
        }
    }
}

}

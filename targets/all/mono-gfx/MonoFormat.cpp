/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * mono-gfx/MonoFormat.cpp
 *
 * Format conversion implementations
 */

#include "MonoFormat.h"

//! Transposes an 8x8 bit matrix packed into a 64-bit word
/*!
 * The input is interpreted as 8 bytes laid out as
 * <tt>byte0 byte1 ... byte7</tt> (in memory order), where byte @c r holds
 * 8 horizontally-adjacent pixels of row @c r with bit 7 representing the
 * leftmost pixel. The output, also packed as 8 bytes in memory order, has
 * byte @c c holding the same 8 pixels reorganized so that bit 7 represents
 * the topmost pixel of column @c c.
 *
 * Algorithm follows Hacker's Delight, section 7.3 ("Transposing a Bit
 * Matrix"). Each step swaps two halves of an N-bit interleave pattern.
 */
ALWAYS_INLINE static uint64_t Transpose8x8(uint64_t x)
{
    uint64_t t;
    // swap 1-bit pairs across rows 7 apart
    t = (x ^ (x >> 7)) & 0x00AA00AA00AA00AAULL;
    x = x ^ t ^ (t << 7);
    // swap 2-bit pairs across rows 14 apart
    t = (x ^ (x >> 14)) & 0x0000CCCC0000CCCCULL;
    x = x ^ t ^ (t << 14);
    // swap 4-bit pairs across rows 28 apart
    t = (x ^ (x >> 28)) & 0x00000000F0F0F0F0ULL;
    x = x ^ t ^ (t << 28);
    return x;
}

//! Reads 8 source bytes (one per row) at the same column offset into a 64-bit word
/*!
 * Rows past @p remRows produce zero bytes, so the helper handles partial
 * tail pages without a separate code path. The result is laid out so
 * @c byte0 in memory holds row 0, @c byte7 holds row 7, matching the input
 * convention of @ref Transpose8x8.
 */
ALWAYS_INLINE static uint64_t ReadCellRows(const uint8_t* p, int stride, int remRows)
{
    uint64_t v = 0;
    int n = remRows < 8 ? remRows : 8;
    for (int i = 0; i < n; i++)
        v |= uint64_t(p[i * stride]) << (i * 8);
    return v;
}

//! Reverses the bit order within every byte of a 64-bit word
ALWAYS_INLINE static uint64_t ReverseBitsPerByte(uint64_t x)
{
    x = ((x & 0xAAAAAAAAAAAAAAAAULL) >> 1) | ((x & 0x5555555555555555ULL) << 1);
    x = ((x & 0xCCCCCCCCCCCCCCCCULL) >> 2) | ((x & 0x3333333333333333ULL) << 2);
    x = ((x & 0xF0F0F0F0F0F0F0F0ULL) >> 4) | ((x & 0x0F0F0F0F0F0F0F0FULL) << 4);
    return x;
}

OPTIMIZE_SIZE void MonoFormat::ToVerticalBytes(const MonoBuffer& src, void* dst, int dstStride, VerticalBitOrder order)
{
    int w = src.Width();
    int h = src.Height();
    int srcStride = src.Stride();
    if (w <= 0 || h <= 0) return;

    auto* dp = (uint8_t*)dst;
    auto* sp = src.Data();

    // After Transpose8x8, the high byte of the 64-bit word holds column 0
    // and within every output byte bit 0 holds the topmost row, i.e. the
    // result is already in TopLSB form. For TopMSB we additionally reverse
    // the bit order within each byte.
    int pages = (h + 7) >> 3;
    for (int page = 0; page < pages; page++)
    {
        const uint8_t* srow = sp + page * 8 * srcStride;
        int remRows = h - page * 8;
        uint8_t* dRow = dp + page * dstStride;

        int x = 0;
        // process full 8-pixel-wide cells
        while (x + 8 <= w)
        {
            uint64_t v = Transpose8x8(ReadCellRows(srow + (x >> 3), srcStride, remRows));
            if (order == VerticalBitOrder::TopMSB)
                v = ReverseBitsPerByte(v);
            // column c sits in byte (7-c); emit them in column order
            for (int c = 0; c < 8; c++)
                dRow[x + c] = uint8_t(v >> ((7 - c) * 8));
            x += 8;
        }

        // handle a partial trailing cell (fewer than 8 columns)
        if (x < w)
        {
            uint64_t v = Transpose8x8(ReadCellRows(srow + (x >> 3), srcStride, remRows));
            if (order == VerticalBitOrder::TopMSB)
                v = ReverseBitsPerByte(v);
            int cols = w - x;
            for (int c = 0; c < cols; c++)
                dRow[x + c] = uint8_t(v >> ((7 - c) * 8));
        }
    }
}

/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * mono-gfx/MonoFormat.h
 *
 * Conversion helpers from the canonical horizontal MSB-first 1-bpp format
 * to packings expected by various display chips
 */

#pragma once

#include <base/base.h>

#include <mono-gfx/MonoBuffer.h>

//! Static format conversion helpers
class MonoFormat
{
public:
    //! Bit ordering for the vertical-byte output formats
    enum struct VerticalBitOrder : uint8_t
    {
        //! Bit 0 holds the topmost pixel of each byte (SSD1306, SH1106 page mode)
        TopLSB,
        //! Bit 7 holds the topmost pixel of each byte
        TopMSB,
    };

    //! Converts a @ref MonoBuffer to vertical-byte format
    /*!
     * The output is a sequence of pages, each page being @p dstStride bytes
     * wide. Every byte holds 8 vertically adjacent pixels of one column.
     *
     * The number of pages emitted is @c (src.Height()+7)/8. Pixels below
     * the source height are output as zeroes. The destination must be at
     * least @c pages*dstStride bytes long. When @p dstStride equals the
     * source width the output is tightly packed; a larger stride leaves
     * trailing bytes untouched between rows.
     *
     * @param order Selects how the 8 source rows are packed into each
     * output byte. Use @ref VerticalBitOrder::TopLSB for SSD1306-style
     * displays (the most common case).
     */
    static void ToVerticalBytes(const MonoBuffer& src, void* dst, int dstStride,
        VerticalBitOrder order = VerticalBitOrder::TopLSB);

    //! Converts a @ref MonoBuffer to vertical-byte format
    /*!
     * Convenience overload that uses tight packing (@c dstStride==src.Width()).
     * The output buffer must be at least @c src.Width()*((src.Height()+7)/8)
     * bytes long.
     */
    static ALWAYS_INLINE void ToVerticalBytes(const MonoBuffer& src, void* dst,
        VerticalBitOrder order = VerticalBitOrder::TopLSB)
    {
        ToVerticalBytes(src, dst, src.Width(), order);
    }
};

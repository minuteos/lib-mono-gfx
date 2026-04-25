/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * mono-gfx/Font.h
 *
 * Bitmap font definition consumed by MonoBuffer::DrawText
 */

#pragma once

#include <base/base.h>

//! A single glyph located inside a Font's bitmap data
struct Glyph
{
    //! Pointer to the glyph bitmap, or @c nullptr for missing glyphs
    /*!
     * Glyphs are stored in the same row-major MSB-first format as
     * @ref MonoBuffer, with one row of @c (width+7)/8 bytes per pixel row,
     * @c height rows total.
     */
    const uint8_t* bitmap;
    //! Glyph width in pixels (always 0 when @ref bitmap is null)
    uint8_t width;
};

//! A bitmap font usable with @ref MonoBuffer::DrawText
/*!
 * A font is a packed bitmap, optional offset and width tables and a few
 * scalar fields. Two layouts are supported:
 *
 * - @b Fixed-width: @ref widths and @ref offsets are both null. Every
 *   glyph is @ref fixedWidth pixels wide. Glyph @c n starts at byte
 *   @c n*((fixedWidth+7)/8)*height in @ref data.
 *
 * - @b Variable-width: @ref widths and @ref offsets are non-null.
 *   Glyph @c n is @c widths[n] pixels wide and starts at byte
 *   @c offsets[n] in @ref data.
 *
 * In either case, only the contiguous range
 * <tt>[firstChar, firstChar + charCount)</tt> of code points is covered;
 * code points outside that range are rendered with width
 * @ref missingWidth and no bitmap.
 */
struct Font
{
    //! Glyph height in pixels
    uint8_t height;
    //! Width in pixels added between adjacent glyphs (and used for spaces if not encoded)
    uint8_t spacing;
    //! Width of glyphs when @ref widths is null
    uint8_t fixedWidth;
    //! Width substituted for code points outside the encoded range
    uint8_t missingWidth;
    //! First encoded code point
    uint16_t firstChar;
    //! Number of contiguous encoded code points
    uint16_t charCount;
    //! Per-glyph width in pixels, or @c nullptr for fixed-width fonts
    const uint8_t* widths;
    //! Per-glyph byte offset into @ref data, or @c nullptr for fixed-width fonts
    const uint16_t* offsets;
    //! Glyph bitmap data
    const uint8_t* data;

    //! Returns the @ref Glyph descriptor for the requested code point
    Glyph GetGlyph(unsigned codepoint) const
    {
        unsigned index = codepoint - firstChar;
        if (index >= charCount)
        {
            return Glyph { nullptr, missingWidth };
        }
        if (widths)
        {
            return Glyph { data + offsets[index], widths[index] };
        }
        unsigned stride = (fixedWidth + 7u) >> 3;
        return Glyph { data + index * stride * height, fixedWidth };
    }
};

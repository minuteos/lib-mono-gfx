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

//! Glyph bitmap storage scheme used by a @ref Font
enum struct FontFormat : uint8_t
{
    //! Uncompressed: @ref Font::data holds raw row-major MSB-first bitmaps
    Raw = 0,
    //! Nibble run-length: @ref Font::data is a continuous run stream
    //! decoded straight into horizontal spans at blit time, no buffer
    RLE = 1,
};

//! One contiguous block of the code-point map (LVGL-style cmap subrange)
/*!
 * A @ref Font may cover several disjoint code-point ranges (e.g. ASCII
 * plus scattered icon code points) without a giant sparse table: each
 * range maps @c [first, first+count) onto consecutive glyph indices
 * starting at @ref glyphBase. Real Unicode code points are used directly
 * - no application-side remapping.
 */
struct FontRange
{
    uint16_t first;         //!< first code point in this range
    uint16_t count;         //!< number of contiguous code points
    uint16_t glyphBase;     //!< glyph index of @ref first
};

//! A bitmap font usable with @ref MonoBuffer::DrawText
/*!
 * A font is a packed bitmap, optional offset and width tables and a few
 * scalar fields. Two layouts are supported:
 *
 * - @b Fixed-width: @ref widths and @ref offsets are both null. Every
 *   glyph is @ref fixedWidth pixels wide. Glyph @c n starts at byte
 *   @c n*((fixedWidth+7)/8)*height in @ref data. Only valid for
 *   @ref FontFormat::Raw.
 *
 * - @b Variable-width: @ref widths and @ref offsets are non-null.
 *   Glyph @c n is @c widths[n] pixels wide and starts at byte
 *   @c offsets[n] in @ref data.
 *
 * Code-point coverage is either a single contiguous range
 * <tt>[firstChar, firstChar + charCount)</tt> (when @ref ranges is null)
 * or, for fonts mixing e.g. ASCII with scattered icons, the set of
 * @ref ranges (LVGL-style multi-range cmap, real Unicode code points).
 * Code points outside the covered set are rendered with width
 * @ref missingWidth and no bitmap.
 *
 * When @ref format is @ref FontFormat::RLE the glyph data is a
 * @b continuous run-length stream (not per row) instead of a raw bitmap.
 * The glyph is treated as @c width*height pixels in row-major order; runs
 * of equal colour alternate starting with @b background (a glyph that
 * begins on a set pixel starts with a zero-length background run). Each
 * run length is read from a nibble stream, high nibble of each byte
 * first, glyphs byte-aligned at @ref offsets:
 *
 * - @c 0x0..0xE         -> run length @c 0..14 (one nibble)
 * - @c 0xF, @c 0x0..0xD -> run length @c 15..28
 * - @c 0xF, @c 0xE, hi, lo            -> @c 29 + (8-bit big-endian nibbles)
 * - @c 0xF, @c 0xF, n2, n1, n0        -> @c 285 + (12-bit big-endian nibbles)
 *
 * The maximum encodable run is 4380; the (vanishingly rare) longer run is
 * split by the generator into chunks joined by a zero-length opposite-
 * colour run, so the decoder needs no special case. Decoding is O(runs),
 * allocation-free, every byte read once, and emits one
 * @ref MonoBuffer::DrawHLine per foreground run segment - a run crossing a
 * row boundary is simply split at the edge. RLE always uses the
 * variable-width table form.
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
    //! First encoded code point (single-range form; ignored if @ref ranges)
    uint16_t firstChar;
    //! Number of contiguous encoded code points (single-range form)
    uint16_t charCount;
    //! Optional multi-range cmap; @c nullptr selects the single-range form
    const FontRange* ranges;
    //! Number of entries in @ref ranges
    uint16_t rangeCount;
    //! Per-glyph width in pixels, or @c nullptr for fixed-width fonts
    const uint8_t* widths;
    //! Per-glyph byte offset into @ref data, or @c nullptr for fixed-width fonts
    const uint16_t* offsets;
    //! Glyph bitmap data (raw bitmap or RLE stream per @ref format)
    const uint8_t* data;
    //! Glyph storage scheme; defaults to @ref FontFormat::Raw
    FontFormat format;

    //! @c true if glyph data is run-length encoded
    ALWAYS_INLINE bool IsRLE() const { return format == FontFormat::RLE; }

    //! Resolves a code point to a glyph index, or -1 if not covered
    int GlyphIndex(unsigned codepoint) const
    {
        if (ranges)
        {
            for (unsigned i = 0; i < rangeCount; i++)
            {
                unsigned d = codepoint - ranges[i].first;
                if (d < ranges[i].count)
                    return int(ranges[i].glyphBase + d);
            }
            return -1;
        }
        unsigned d = codepoint - firstChar;
        return d < charCount ? int(d) : -1;
    }

    //! Returns the @ref Glyph descriptor for the requested code point
    Glyph GetGlyph(unsigned codepoint) const
    {
        int idx = GlyphIndex(codepoint);
        if (idx < 0)
        {
            return Glyph { nullptr, missingWidth };
        }
        if (widths)
        {
            return Glyph { data + offsets[idx], widths[idx] };
        }
        unsigned stride = (fixedWidth + 7u) >> 3;
        return Glyph { data + unsigned(idx) * stride * height, fixedWidth };
    }

    //! Walks the foreground horizontal runs of an RLE glyph
    /*!
     * Invokes @p span(int dx, int dy, int len) once per maximal run of
     * set pixels on a row, with coordinates relative to the glyph origin
     * (a foreground run that crosses a row boundary is reported as one
     * span per row it touches). Does nothing for code points outside the
     * encoded range or for non-RLE fonts. The decoder is O(runs),
     * allocates nothing and reads every nibble exactly once.
     */
    template <typename Fn>
    void ForEachSpan(unsigned codepoint, Fn&& span) const
    {
        int idx = GlyphIndex(codepoint);
        if (format != FontFormat::RLE || idx < 0 || !widths || !offsets)
        {
            return;
        }
        unsigned index = unsigned(idx);
        const uint8_t* p = data + offsets[index];
        bool hi = true;                 // high nibble of *p is read first
        auto nib = [&]() -> unsigned
        {
            if (hi) { hi = false; return *p >> 4; }
            hi = true; return *p++ & 0xF;
        };
        auto run = [&]() -> int
        {
            unsigned n = nib();
            if (n != 0xF) return int(n);                 // 0..14
            unsigned m = nib();
            if (m <= 0xD) return int(15 + m);            // 15..28
            if (m == 0xE)                                // 29..284
            {
                unsigned h2 = nib();
                return int(29 + ((h2 << 4) | nib()));
            }
            unsigned a = nib(), b = nib();               // 285..4380
            return int(285 + ((a << 8) | (b << 4) | nib()));
        };

        int total = int(widths[index]) * height;
        int px = 0, py = 0, pos = 0;
        bool fg = false;                // glyph starts on background
        while (pos < total)
        {
            int rem = run();
            while (rem > 0)
            {
                int space = widths[index] - px;
                int take = rem < space ? rem : space;
                if (fg) span(px, py, take);
                px += take; pos += take; rem -= take;
                if (px == widths[index]) { px = 0; py++; }
            }
            fg = !fg;
        }
    }
};

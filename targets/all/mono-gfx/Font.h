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
    //! Pointer to the glyph black-box bitmap, or @c nullptr for missing
    //! (or empty, e.g. space) glyphs
    /*!
     * Glyphs are stored in the same row-major MSB-first format as
     * @ref MonoBuffer, with one row of @c (bw+7)/8 bytes per pixel row,
     * @c bh rows total - only the inked black box, not a full cell.
     */
    const uint8_t* bitmap;
    //! Pen advance in pixels (a.k.a. glyph width; always @ref Font::missingWidth
    //! for missing glyphs)
    uint8_t width;
    //! Black-box dimensions in pixels (both 0 when @ref bitmap is null)
    uint8_t bw, bh;
    //! Black-box bearings: @ref bx left side bearing from the pen,
    //! @ref by the box bottom relative to the baseline (LVGL convention)
    int8_t bx, by;
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

//! Packed per-glyph metrics (parallel to the cmap, one per glyph index)
struct GlyphMetric
{
    uint16_t offset;        //!< byte offset of the black box in @ref Font::data
    uint8_t advance;        //!< pen advance in pixels
    uint8_t bw, bh;         //!< black-box width / height in pixels
    int8_t bx, by;          //!< black-box left bearing / bottom-vs-baseline
};

//! A bitmap font usable with @ref MonoBuffer::DrawText
/*!
 * Each glyph is stored as just its inked black box (no padded cell);
 * @ref glyphs gives its byte @ref GlyphMetric::offset into @ref data,
 * its @ref GlyphMetric::advance and the box size/bearings. The glyph is
 * placed at <tt>pen.x + bx</tt>, <tt>pen.y + @ref ascent - (by + bh)</tt>
 * (baseline-relative, LVGL convention). Code-point coverage is either a
 * single contiguous range <tt>[firstChar, firstChar + charCount)</tt>
 * (when @ref ranges is null) or the set of @ref ranges (LVGL-style
 * multi-range cmap, real Unicode code points). Code points outside the
 * covered set get @ref missingWidth advance and no bitmap.
 *
 * When @ref format is @ref FontFormat::RLE the black box is a continuous
 * run-length stream (not per row): @c bw*bh pixels in row-major order,
 * runs of equal colour alternating from @b background (a box starting on
 * a set pixel begins with a zero-length background run). Each run length
 * is read from a nibble stream, high nibble of each byte first:
 *
 * - @c 0x0..0xE         -> run length @c 0..14 (one nibble)
 * - @c 0xF, @c 0x0..0xD -> run length @c 15..28
 * - @c 0xF, @c 0xE, hi, lo            -> @c 29 + (8-bit big-endian nibbles)
 * - @c 0xF, @c 0xF, n2, n1, n0        -> @c 285 + (12-bit big-endian nibbles)
 *
 * The maximum encodable run is 4380; a longer run is split by the
 * generator into chunks joined by a zero-length opposite-colour run, so
 * the decoder needs no special case. Decoding is O(runs), allocation-free
 * and reads every nibble once. @ref FontFormat::Raw instead stores the
 * box as a row-major MSB-first bitmap, @c (bw+7)/8 bytes per row.
 */
struct Font
{
    //! Cell (line) height in pixels (@c == ascent+descent)
    uint8_t height;
    //! Width in pixels added between adjacent glyphs
    uint8_t spacing;
    //! Advance substituted for code points outside the encoded range
    uint8_t missingWidth;
    //! Distance from the cell top to the baseline
    uint8_t ascent;
    //! Distance from the baseline to the cell bottom
    uint8_t descent;
    //! First encoded code point (single-range form; ignored if @ref ranges)
    uint16_t firstChar;
    //! Number of contiguous encoded code points (single-range form)
    uint16_t charCount;
    //! Optional multi-range cmap; @c nullptr selects the single-range form
    const FontRange* ranges;
    //! Number of entries in @ref ranges
    uint16_t rangeCount;
    //! Per-glyph packed metric table (one entry per glyph index)
    const GlyphMetric* glyphs;
    //! Glyph black-box data (raw bitmap or RLE stream per @ref format)
    const uint8_t* data;
    //! Glyph storage scheme
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
            return Glyph { nullptr, missingWidth, 0, 0, 0, 0 };
        }
        const GlyphMetric& m = glyphs[idx];
        return Glyph { data + m.offset, m.advance, m.bw, m.bh, m.bx, m.by };
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
        if (format != FontFormat::RLE || idx < 0 || !glyphs)
        {
            return;
        }
        const GlyphMetric& m = glyphs[idx];
        int bw = m.bw, bh = m.bh;
        if (bw <= 0 || bh <= 0)
        {
            return;                     // empty glyph (e.g. space)
        }
        const uint8_t* p = data + m.offset;
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

        int total = bw * bh;
        int px = 0, py = 0, pos = 0;
        bool fg = false;                // glyph starts on background
        while (pos < total)
        {
            int rem = run();
            while (rem > 0)
            {
                int space = bw - px;
                int take = rem < space ? rem : space;
                if (fg) span(px, py, take);
                px += take; pos += take; rem -= take;
                if (px == bw) { px = 0; py++; }
            }
            fg = !fg;
        }
    }
};

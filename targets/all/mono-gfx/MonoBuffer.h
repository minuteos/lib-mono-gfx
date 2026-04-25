/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * mono-gfx/MonoBuffer.h
 *
 * 1-bit-per-pixel framebuffer view, MSB-first horizontal byte layout
 */

#pragma once

#include <base/base.h>
#include <base/Span.h>

#include <mono-gfx/DrawOp.h>

class Font;

//! A non-owning view of a 1-bit-per-pixel image
/*!
 * The internal storage format is one bit per pixel, packed MSB-first along
 * the X axis with byte-aligned rows. Bit 7 of byte 0 in any row holds the
 * leftmost pixel of that row, bit 0 holds the eighth pixel, bit 7 of byte 1
 * holds the ninth, and so on. Rows are laid out top-to-bottom and may be
 * padded via the configurable @ref Stride. A pixel value of 1 represents
 * the foreground; the actual color displayed is left for the device driver
 * to decide.
 *
 * This is the standard 1-bpp planar format consumed by the @c lib-display
 * drivers (e.g. ST7306) and by most monochrome display chips after a
 * trivial bit shuffle.
 *
 * The buffer holds nothing more than a pointer and three small integers,
 * so it is intended to be passed around by value.
 */
class MonoBuffer
{
    uint8_t* p = nullptr;
    int16_t w = 0, h = 0;
    int16_t s = 0;

public:
    //! Constructs an empty buffer
    constexpr MonoBuffer() = default;

    //! Constructs a buffer over a contiguous tightly-packed framebuffer
    /*!
     * The row stride is computed as @c (width+7)/8 bytes.
     */
    constexpr MonoBuffer(void* data, int width, int height)
        : p((uint8_t*)data), w(width), h(height), s((width + 7) >> 3) {}

    //! Constructs a buffer with an explicit row stride in bytes
    constexpr MonoBuffer(void* data, int width, int height, int stride)
        : p((uint8_t*)data), w(width), h(height), s(stride) {}

    ALWAYS_INLINE constexpr uint8_t* Data() const { return p; }
    ALWAYS_INLINE constexpr int Width() const { return w; }
    ALWAYS_INLINE constexpr int Height() const { return h; }
    //! Number of bytes between the start of one row and the next
    ALWAYS_INLINE constexpr int Stride() const { return s; }
    //! Total size of the framebuffer in bytes
    ALWAYS_INLINE constexpr size_t Size() const { return size_t(s) * h; }
    //! Returns @c true if the buffer is empty (no pixels)
    ALWAYS_INLINE constexpr bool IsEmpty() const { return w <= 0 || h <= 0; }

    //! Returns the underlying memory as a writable Buffer
    ALWAYS_INLINE Buffer Bytes() const { return Buffer(p, Size()); }
    //! Returns the underlying memory as a read-only Span
    ALWAYS_INLINE Span AsSpan() const { return Span(p, Size()); }

    //! Returns a sub-window of this buffer that shares memory with the original
    /*!
     * The returned window has its own width, height and stride but points
     * into the same backing memory. The new origin is rounded down to a
     * byte boundary along the X axis: a buffer is always byte-aligned at
     * its top-left corner. The returned window is therefore widened by up
     * to 7 pixels on the left compared to the requested rectangle.
     *
     * Useful as a scratch view for incremental rendering pipelines that
     * want to draw into a sub-rectangle of a larger framebuffer without
     * recomputing addresses on every call.
     */
    MonoBuffer ByteWindow(int x, int y, int width, int height) const;

    //! Reads a single pixel; returns @c false for coordinates outside the buffer
    ALWAYS_INLINE bool GetPixel(int x, int y) const
    {
        if (unsigned(x) >= unsigned(w) || unsigned(y) >= unsigned(h)) return false;
        return p[y * s + (x >> 3)] & (0x80 >> (x & 7));
    }

    //! Draws a single pixel; coordinates outside the buffer are silently clipped
    ALWAYS_INLINE void DrawPixel(int x, int y, DrawOp op = DrawOp::Set)
    {
        if (unsigned(x) >= unsigned(w) || unsigned(y) >= unsigned(h)) return;
        ApplyDrawOp(p[y * s + (x >> 3)], 0x80 >> (x & 7), op);
    }

    //! Fills the entire buffer with a single color
    ALWAYS_INLINE void Clear() { memset(p, 0, Size()); }
    //! Fills the entire buffer with @c 0xFF (all foreground)
    ALWAYS_INLINE void FillAll() { memset(p, 0xFF, Size()); }
    //! Inverts every pixel in the buffer
    void InvertAll();

    //! Draws a horizontal line of @p width pixels starting at (@p x, @p y)
    void DrawHLine(int x, int y, int width, DrawOp op = DrawOp::Set);
    //! Draws a vertical line of @p height pixels starting at (@p x, @p y)
    void DrawVLine(int x, int y, int height, DrawOp op = DrawOp::Set);
    //! Draws a line between (@p x0, @p y0) and (@p x1, @p y1) inclusive
    void DrawLine(int x0, int y0, int x1, int y1, DrawOp op = DrawOp::Set);
    //! Draws the outline of a rectangle
    void DrawRect(int x, int y, int width, int height, DrawOp op = DrawOp::Set);
    //! Fills a rectangle
    void FillRect(int x, int y, int width, int height, DrawOp op = DrawOp::Set);
    //! Draws the outline of a circle centered at (@p cx, @p cy) with radius @p r
    void DrawCircle(int cx, int cy, int r, DrawOp op = DrawOp::Set);
    //! Fills a circle centered at (@p cx, @p cy) with radius @p r
    void FillCircle(int cx, int cy, int r, DrawOp op = DrawOp::Set);
    //! Draws the outline of a rectangle with rounded corners of radius @p r
    void DrawRoundRect(int x, int y, int width, int height, int r, DrawOp op = DrawOp::Set);
    //! Fills a rectangle with rounded corners of radius @p r
    void FillRoundRect(int x, int y, int width, int height, int r, DrawOp op = DrawOp::Set);

    //! Copies a source buffer onto this one at (@p x, @p y) using the specified blit op
    void Blit(int x, int y, const MonoBuffer& src, BlitOp op = BlitOp::Copy);
    //! Copies a source rectangle onto this one at (@p x, @p y) using the specified blit op
    void Blit(int x, int y, const MonoBuffer& src, int sx, int sy, int width, int height, BlitOp op = BlitOp::Copy);

    //! Renders a UTF-8 / ASCII string at (@p x, @p y)
    /*!
     * The pen advances pixel-by-pixel, taking each glyph's stored width
     * and the font's spacing into account. Returns the X coordinate at
     * which the next glyph would be drawn, useful for chaining strings.
     */
    int DrawText(int x, int y, const Font& font, Span text, DrawOp op = DrawOp::Set);
    //! Convenience wrapper for null-terminated strings
    ALWAYS_INLINE int DrawText(int x, int y, const Font& font, const char* sz, DrawOp op = DrawOp::Set)
    { return DrawText(x, y, font, Span::FromSZ(sz), op); }

    //! Returns the width in pixels that @ref DrawText would consume
    static int MeasureText(const Font& font, Span text);
    //! Convenience wrapper for null-terminated strings
    ALWAYS_INLINE static int MeasureText(const Font& font, const char* sz)
    { return MeasureText(font, Span::FromSZ(sz)); }

private:
    //! Clips a horizontal run [x, x+width) to the buffer width and the row [0,h)
    ALWAYS_INLINE bool ClipRow(int& x, int y, int& width) const
    {
        if (unsigned(y) >= unsigned(h)) return false;
        if (x < 0) { width += x; x = 0; }
        if (x + width > w) width = w - x;
        return width > 0;
    }

    //! Clips a vertical run [y, y+height) to the buffer height and the column [0,w)
    ALWAYS_INLINE bool ClipColumn(int x, int& y, int& height) const
    {
        if (unsigned(x) >= unsigned(w)) return false;
        if (y < 0) { height += y; y = 0; }
        if (y + height > h) height = h - y;
        return height > 0;
    }

    //! Clips an arbitrary rectangle to the buffer bounds
    bool ClipRect(int& x, int& y, int& width, int& height) const;

    void HRunRaw(int x, int y, int width, DrawOp op);

    friend class MonoFormat;
};

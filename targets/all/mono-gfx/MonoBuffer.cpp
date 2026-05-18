/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * mono-gfx/MonoBuffer.cpp
 *
 * Drawing primitives for 1-bpp framebuffers
 */

#include "MonoBuffer.h"

#include <mono-gfx/Font.h>
#include <mono-gfx/Utf8.h>

//! Applies @p op to a contiguous run of full bytes
ALWAYS_INLINE static void ApplyOpRun(uint8_t* dst, size_t n, DrawOp op)
{
    switch (op)
    {
        case DrawOp::Clear:  memset(dst, 0x00, n); break;
        case DrawOp::Set:    memset(dst, 0xFF, n); break;
        case DrawOp::Invert: while (n--) { *dst++ ^= 0xFF; } break;
        case DrawOp::Keep:   break;
    }
}

bool MonoBuffer::ClipRect(int& x, int& y, int& width, int& height) const
{
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > w) width = w - x;
    if (y + height > h) height = h - y;
    return width > 0 && height > 0;
}

MonoBuffer MonoBuffer::ByteWindow(int x, int y, int width, int height) const
{
    // round x down to a byte boundary; the requested left edge becomes
    // visible inside the returned buffer at column (original x) & 7
    int xb = x & ~7;
    width += x - xb;
    x = xb;
    if (!ClipRect(x, y, width, height))
        return MonoBuffer();
    return MonoBuffer(p + y * this->s + (x >> 3), width, height, this->s);
}

void MonoBuffer::InvertAll()
{
    auto e = p + Size();
    auto q = p;
    while (q + 4 <= e)
    {
        *(uint32_t*)q ^= 0xFFFFFFFF;
        q += 4;
    }
    while (q < e) { *q++ ^= 0xFF; }
}

void MonoBuffer::HRunRaw(int x, int y, int width, DrawOp op)
{
    uint8_t* row = p + y * s + (x >> 3);
    int lead = x & 7;
    // mask of pixels in the first byte that are part of the run
    uint8_t leftMask = uint8_t(0xFF >> lead);
    int leftCount = 8 - lead;

    if (width <= leftCount)
    {
        // entire run fits within a single byte
        leftMask &= 0xFF << (leftCount - width);
        ApplyDrawOp(*row, leftMask, op);
        return;
    }

    // first (partial) byte
    ApplyDrawOp(*row++, leftMask, op);
    width -= leftCount;

    // full bytes
    int fullBytes = width >> 3;
    if (fullBytes)
    {
        ApplyOpRun(row, fullBytes, op);
        row += fullBytes;
        width &= 7;
    }

    // trailing partial byte
    if (width)
    {
        uint8_t rightMask = uint8_t(0xFF << (8 - width));
        ApplyDrawOp(*row, rightMask, op);
    }
}

void MonoBuffer::DrawHLine(int x, int y, int width, DrawOp op)
{
    if (!ClipRow(x, y, width)) return;
    HRunRaw(x, y, width, op);
}

void MonoBuffer::DrawVLine(int x, int y, int height, DrawOp op)
{
    if (!ClipColumn(x, y, height)) return;
    uint8_t mask = 0x80 >> (x & 7);
    uint8_t* col = p + y * s + (x >> 3);
    while (height--)
    {
        ApplyDrawOp(*col, mask, op);
        col += s;
    }
}

void MonoBuffer::DrawLine(int x0, int y0, int x1, int y1, DrawOp op)
{
    // axis-aligned shortcuts
    if (y0 == y1)
    {
        if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
        DrawHLine(x0, y0, x1 - x0 + 1, op);
        return;
    }
    if (x0 == x1)
    {
        if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
        DrawVLine(x0, y0, y1 - y0 + 1, op);
        return;
    }

    // Bresenham's algorithm with clipping done per-pixel; for short lines
    // this is cheaper than running Cohen-Sutherland up front
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx > 0 ? 1 : -1; if (dx < 0) dx = -dx;
    int sy = dy > 0 ? 1 : -1; if (dy < 0) dy = -dy;
    int err = dx - dy;
    for (;;)
    {
        DrawPixel(x0, y0, op);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err << 1;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void MonoBuffer::DrawRect(int x, int y, int width, int height, DrawOp op)
{
    if (width <= 0 || height <= 0) return;
    DrawHLine(x, y, width, op);
    if (height > 1)
    {
        DrawHLine(x, y + height - 1, width, op);
        if (height > 2)
        {
            DrawVLine(x, y + 1, height - 2, op);
            if (width > 1)
                DrawVLine(x + width - 1, y + 1, height - 2, op);
        }
    }
}

void MonoBuffer::FillRect(int x, int y, int width, int height, DrawOp op)
{
    if (!ClipRect(x, y, width, height)) return;
    while (height--)
        HRunRaw(x, y++, width, op);
}

void MonoBuffer::DrawCircle(int cx, int cy, int r, DrawOp op)
{
    if (r < 0) return;
    if (r == 0) { DrawPixel(cx, cy, op); return; }

    // midpoint circle algorithm - 8-fold symmetry
    int x = r, y = 0, err = 1 - r;
    DrawPixel(cx + r, cy, op);
    DrawPixel(cx - r, cy, op);
    DrawPixel(cx, cy + r, op);
    DrawPixel(cx, cy - r, op);
    while (++y < x)
    {
        if (err < 0)
        {
            err += 2 * y + 1;
        }
        else
        {
            --x;
            err += 2 * (y - x) + 1;
        }
        DrawPixel(cx + x, cy + y, op); DrawPixel(cx - x, cy + y, op);
        DrawPixel(cx + x, cy - y, op); DrawPixel(cx - x, cy - y, op);
        if (x != y)
        {
            DrawPixel(cx + y, cy + x, op); DrawPixel(cx - y, cy + x, op);
            DrawPixel(cx + y, cy - x, op); DrawPixel(cx - y, cy - x, op);
        }
    }
}

void MonoBuffer::FillCircle(int cx, int cy, int r, DrawOp op)
{
    if (r < 0) return;
    if (r == 0) { DrawPixel(cx, cy, op); return; }

    int x = r, y = 0, err = 1 - r;
    DrawHLine(cx - r, cy, 2 * r + 1, op);
    while (++y <= x)
    {
        if (err < 0)
        {
            err += 2 * y + 1;
        }
        else
        {
            // x is about to decrement; emit the last span at this y first
            DrawHLine(cx - x, cy + y, 2 * x + 1, op);
            if (y) DrawHLine(cx - x, cy - y, 2 * x + 1, op);
            --x;
            err += 2 * (y - x) + 1;
            // when y has caught up with x we still need to draw the column
            if (y > x) break;
            DrawHLine(cx - y, cy + x + 1, 2 * y + 1, op);
            DrawHLine(cx - y, cy - x - 1, 2 * y + 1, op);
            continue;
        }
        // err >= 0 path emits a horizontal span centered on cy ± y
        DrawHLine(cx - x, cy + y, 2 * x + 1, op);
        DrawHLine(cx - x, cy - y, 2 * x + 1, op);
    }
}

void MonoBuffer::DrawRoundRect(int x, int y, int width, int height, int r, DrawOp op)
{
    if (width <= 0 || height <= 0) return;
    int rmax = (width < height ? width : height) >> 1;
    if (r > rmax) r = rmax;
    if (r <= 0) { DrawRect(x, y, width, height, op); return; }

    // straight edges (excluding the corner regions)
    DrawHLine(x + r, y, width - 2 * r, op);
    DrawHLine(x + r, y + height - 1, width - 2 * r, op);
    DrawVLine(x, y + r, height - 2 * r, op);
    DrawVLine(x + width - 1, y + r, height - 2 * r, op);

    // four corner arcs - midpoint algorithm, restricted to one quadrant each
    int xx = r, yy = 0, err = 1 - r;
    int x0l = x + r, x0r = x + width - 1 - r;
    int y0t = y + r, y0b = y + height - 1 - r;
    DrawPixel(x0r + r, y0t, op); DrawPixel(x0l - r, y0t, op);
    DrawPixel(x0r + r, y0b, op); DrawPixel(x0l - r, y0b, op);
    DrawPixel(x0l, y0t - r, op); DrawPixel(x0l, y0b + r, op);
    DrawPixel(x0r, y0t - r, op); DrawPixel(x0r, y0b + r, op);
    while (++yy < xx)
    {
        if (err < 0) { err += 2 * yy + 1; }
        else { --xx; err += 2 * (yy - xx) + 1; }
        DrawPixel(x0r + xx, y0b + yy, op); DrawPixel(x0l - xx, y0b + yy, op);
        DrawPixel(x0r + xx, y0t - yy, op); DrawPixel(x0l - xx, y0t - yy, op);
        if (xx != yy)
        {
            DrawPixel(x0r + yy, y0b + xx, op); DrawPixel(x0l - yy, y0b + xx, op);
            DrawPixel(x0r + yy, y0t - xx, op); DrawPixel(x0l - yy, y0t - xx, op);
        }
    }
}

void MonoBuffer::FillRoundRect(int x, int y, int width, int height, int r, DrawOp op)
{
    if (width <= 0 || height <= 0) return;
    int rmax = (width < height ? width : height) >> 1;
    if (r > rmax) r = rmax;
    if (r <= 0) { FillRect(x, y, width, height, op); return; }

    // central straight band (full width, between top and bottom corner rows)
    FillRect(x, y + r, width, height - 2 * r, op);

    // top and bottom rows are filled along with the corner sweeps
    int xx = r, yy = 0, err = 1 - r;
    int x0l = x + r;
    int y0t = y + r, y0b = y + height - 1 - r;
    int innerW = width - 2 * r;
    // initial span at y == 0 (the apex of the corner) covers the center span only
    DrawHLine(x0l, y0t - r, innerW, op);
    DrawHLine(x0l, y0b + r, innerW, op);
    while (++yy <= xx)
    {
        if (err < 0)
        {
            err += 2 * yy + 1;
        }
        else
        {
            DrawHLine(x0l - xx, y0t - yy, innerW + 2 * xx, op);
            DrawHLine(x0l - xx, y0b + yy, innerW + 2 * xx, op);
            --xx;
            err += 2 * (yy - xx) + 1;
            if (yy > xx) break;
            DrawHLine(x0l - yy, y0t - xx - 1, innerW + 2 * yy, op);
            DrawHLine(x0l - yy, y0b + xx + 1, innerW + 2 * yy, op);
            continue;
        }
        DrawHLine(x0l - xx, y0t - yy, innerW + 2 * xx, op);
        DrawHLine(x0l - xx, y0b + yy, innerW + 2 * xx, op);
    }
}

// Bhaskara I sine approximation in Q12 fixed point. Accurate to ~0.2% of
// full scale, which is well below one pixel for any radius this library
// targets, and needs no table or libm dependency.
ALWAYS_INLINE static int Sin12(int deg)
{
    deg %= 360;
    if (deg < 0) deg += 360;
    int sign = 1;
    if (deg > 180) { deg -= 180; sign = -1; }
    int t = deg * (180 - deg);          // 0..8100
    return sign * (4 * t * 4096 / (40500 - t));
}

ALWAYS_INLINE static int Cos12(int deg) { return Sin12(deg + 90); }

void MonoBuffer::DrawArc(int cx, int cy, int r, int startDeg, int endDeg, DrawOp op)
{
    if (r < 0 || op == DrawOp::Keep) return;
    if (r == 0) { DrawPixel(cx, cy, op); return; }

    int span = endDeg - startDeg;
    if (span == 0) return;
    if (span < 0) span += ((-span / 360) + 1) * 360;    // normalise to (0,360]
    if (span > 360) span = 360;

    // step fine enough that successive samples are at most ~1px apart
    int steps = (span * r) / 57 + 1;
    int prevX = cx + ((r * Cos12(startDeg) + 2048) >> 12);
    int prevY = cy + ((r * Sin12(startDeg) + 2048) >> 12);
    DrawPixel(prevX, prevY, op);
    for (int i = 1; i <= steps; i++)
    {
        int a = startDeg + span * i / steps;
        int x = cx + ((r * Cos12(a) + 2048) >> 12);
        int y = cy + ((r * Sin12(a) + 2048) >> 12);
        // join consecutive samples so no gaps appear at large radii
        if (x != prevX || y != prevY)
        {
            if ((x - prevX) * (x - prevX) + (y - prevY) * (y - prevY) > 2)
                DrawLine(prevX, prevY, x, y, op);
            else
                DrawPixel(x, y, op);
            prevX = x; prevY = y;
        }
    }
}

void MonoBuffer::DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, DrawOp op)
{
    DrawLine(x0, y0, x1, y1, op);
    DrawLine(x1, y1, x2, y2, op);
    DrawLine(x2, y2, x0, y0, op);
}

void MonoBuffer::FillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, DrawOp op)
{
    if (op == DrawOp::Keep) return;

    // sort vertices by ascending y so we can split into a flat-bottom and a
    // flat-top half and walk both edges with integer interpolation
    if (y0 > y1) { int t; t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y0 > y2) { int t; t = y0; y0 = y2; y2 = t; t = x0; x0 = x2; x2 = t; }
    if (y1 > y2) { int t; t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }

    if (y2 == y0)
    {
        // fully degenerate - a single horizontal extent
        int lo = x0 < x1 ? x0 : x1; if (x2 < lo) lo = x2;
        int hi = x0 > x1 ? x0 : x1; if (x2 > hi) hi = x2;
        DrawHLine(lo, y0, hi - lo + 1, op);
        return;
    }

    // long edge (v0->v2) x as a function of y, in Q16
    int longDx = ((x2 - x0) << 16) / (y2 - y0);
    int longX = x0 << 16;

    // spans are rounded outward (left edge floored, right edge ceiled) so a
    // filled triangle is a conservative superset of its own outline - a
    // border drawn over a fill of the same triangle leaves no 1px seam
    auto Span = [&](int y, int xa, int xb)
    {
        int lo = xa < xb ? xa : xb;
        int hi = xa < xb ? xb : xa;
        lo >>= 16;
        hi = (hi + 0xFFFF) >> 16;
        DrawHLine(lo, y, hi - lo + 1, op);
    };

    // upper sub-triangle v0->v1 (skipped when flat-top)
    if (y1 > y0)
    {
        int dx = ((x1 - x0) << 16) / (y1 - y0);
        int sx = x0 << 16;
        for (int y = y0; y < y1; y++)
        {
            Span(y, longX, sx);
            longX += longDx;
            sx += dx;
        }
    }

    // lower sub-triangle v1->v2
    if (y2 > y1)
    {
        int dx = ((x2 - x1) << 16) / (y2 - y1);
        int sx = x1 << 16;
        for (int y = y1; y <= y2; y++)
        {
            Span(y, longX, sx);
            longX += longDx;
            sx += dx;
        }
    }
    else
    {
        // flat bottom (y1 == y2): the upper loop stopped at y1-1, so the
        // bottom edge between the long edge and v1 still needs its row
        Span(y1, longX, x1 << 16);
    }
}

void MonoBuffer::BlitRotated(int destX, int destY, const MonoBuffer& src,
    int pivotX, int pivotY, int angleDeg, DrawOp op)
{
    if (op == DrawOp::Keep || src.IsEmpty()) return;

    int cosA = Cos12(angleDeg);
    int sinA = Sin12(angleDeg);

    // destination bounding box: rotate the four source corners around the
    // pivot, then translate so the pivot lands on (destX, destY)
    int minX = 0x7FFF, minY = 0x7FFF, maxX = -0x7FFF, maxY = -0x7FFF;
    const int cornX[4] = { 0, src.w, 0, src.w };
    const int cornY[4] = { 0, 0, src.h, src.h };
    for (int i = 0; i < 4; i++)
    {
        int rx = cornX[i] - pivotX, ry = cornY[i] - pivotY;
        int dx = destX + ((rx * cosA - ry * sinA + 2048) >> 12);
        int dy = destY + ((rx * sinA + ry * cosA + 2048) >> 12);
        if (dx < minX) minX = dx;
        if (dx > maxX) maxX = dx;
        if (dy < minY) minY = dy;
        if (dy > maxY) maxY = dy;
    }

    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= w) maxX = w - 1;
    if (maxY >= h) maxY = h - 1;

    // inverse map every destination pixel back into source space (nearest)
    for (int dy = minY; dy <= maxY; dy++)
    {
        for (int dx = minX; dx <= maxX; dx++)
        {
            int rx = dx - destX, ry = dy - destY;
            int sx = pivotX + ((rx * cosA + ry * sinA + 2048) >> 12);
            int sy = pivotY + ((-rx * sinA + ry * cosA + 2048) >> 12);
            if (unsigned(sx) < unsigned(src.w) && unsigned(sy) < unsigned(src.h) &&
                (src.p[sy * src.s + (sx >> 3)] & (0x80 >> (sx & 7))))
            {
                ApplyDrawOp(p[dy * s + (dx >> 3)], 0x80 >> (dx & 7), op);
            }
        }
    }
}

//! Combines a destination byte with a source byte under an arbitrary blit op
ALWAYS_INLINE static uint8_t ApplyBlit(uint8_t dst, uint8_t src, BlitOp op)
{
    switch (op)
    {
        case BlitOp::Copy:    return src;
        case BlitOp::CopyNot: return uint8_t(~src);
        case BlitOp::Or:      return dst | src;
        case BlitOp::AndNot:  return dst & uint8_t(~src);
        case BlitOp::Xor:     return dst ^ src;
        case BlitOp::And:     return dst & src;
    }
    return dst;
}

//! Combines a destination byte with a source byte under a mask
ALWAYS_INLINE static uint8_t ApplyBlitMasked(uint8_t dst, uint8_t src, uint8_t mask, BlitOp op)
{
    uint8_t merged = ApplyBlit(dst, src, op);
    return uint8_t((dst & ~mask) | (merged & mask));
}

void MonoBuffer::Blit(int x, int y, const MonoBuffer& src, BlitOp op)
{
    Blit(x, y, src, 0, 0, src.w, src.h, op);
}

void MonoBuffer::Blit(int x, int y, const MonoBuffer& src, int sx, int sy, int width, int height, BlitOp op)
{
    if (width <= 0 || height <= 0) return;
    // clip the source rect against the source buffer
    if (sx < 0) { x -= sx; width += sx; sx = 0; }
    if (sy < 0) { y -= sy; height += sy; sy = 0; }
    if (sx + width > src.w) width = src.w - sx;
    if (sy + height > src.h) height = src.h - sy;
    // clip the destination rect against this buffer; track the deltas so we
    // can adjust the source origin in lock step
    if (x < 0) { sx -= x; width += x; x = 0; }
    if (y < 0) { sy -= y; height += y; y = 0; }
    if (x + width > w) width = w - x;
    if (y + height > h) height = h - y;
    if (width <= 0 || height <= 0) return;

    int sShift = sx & 7;
    int dShift = x & 7;
    const uint8_t* srow = src.p + sy * src.s + (sx >> 3);
    uint8_t* drow = p + y * s + (x >> 3);

    // common fast path: source and destination are bit-aligned, so we can
    // operate one byte at a time without any shifting
    if (sShift == dShift)
    {
        int firstWidth = (8 - dShift) & 7;
        if (firstWidth > width) firstWidth = width;
        int midPixels = width - firstWidth;
        int midBytes = midPixels >> 3;
        int rightPixels = midPixels & 7;

        uint8_t leftMask = firstWidth ? uint8_t((0xFF >> dShift) & (0xFF << ((8 - dShift) - firstWidth))) : 0;
        uint8_t rightMask = rightPixels ? uint8_t(0xFF << (8 - rightPixels)) : 0;

        for (int row = 0; row < height; row++)
        {
            const uint8_t* sp = srow;
            uint8_t* dp = drow;
            if (leftMask)
            {
                *dp = ApplyBlitMasked(*dp, *sp, leftMask, op);
                dp++; sp++;
            }
            for (int i = 0; i < midBytes; i++)
                dp[i] = ApplyBlit(dp[i], sp[i], op);
            dp += midBytes; sp += midBytes;
            if (rightMask)
                *dp = ApplyBlitMasked(*dp, *sp, rightMask, op);
            srow += src.s;
            drow += s;
        }
        return;
    }

    // misaligned path: each destination byte is assembled from 1-2 source
    // bytes via a 16-bit sliding window. The window is positioned at bit
    // offset `srcBit` from the start of the row; reading 8 consecutive
    // bits from there yields the 8 source pixels that align with the
    // current destination byte.
    int srcBytesPerRow = (sShift + width + 7) >> 3;

    int firstWidth = (8 - dShift) & 7;
    if (firstWidth == 0) firstWidth = 8;
    if (firstWidth > width) firstWidth = width;
    int leadingPixels = firstWidth;
    int midPixels = width - leadingPixels;
    int midBytes = midPixels >> 3;
    int rightPixels = midPixels & 7;

    uint8_t leftMask = uint8_t((0xFF >> dShift) & (0xFF << ((8 - dShift) - leadingPixels)));
    uint8_t rightMask = rightPixels ? uint8_t(0xFF << (8 - rightPixels)) : 0;

    for (int row = 0; row < height; row++)
    {
        const uint8_t* sp = srow;
        uint8_t* dp = drow;

        // sliding-window read: returns 8 source bits starting at row-bit
        // offset `b`. Bytes past the end of the row are read as 0 (caller
        // ensures this never reaches past srcBytesPerRow with non-zero
        // contribution).
        auto Read8 = [sp, srcBytesPerRow](int b) -> uint8_t {
            int byteIdx = b >> 3;
            int bit = b & 7;
            uint16_t w16 = uint16_t(sp[byteIdx]) << 8;
            if (byteIdx + 1 < srcBytesPerRow) w16 |= uint16_t(sp[byteIdx + 1]);
            return uint8_t(w16 >> (8 - bit));
        };

        // first dest byte: 8 source pixels would normally start at sShift,
        // but the dest byte expects them aligned at dShift; the source bits
        // that should land in the dest byte's bit (7 - dShift) downward
        // are sShift..sShift+leadingPixels-1, i.e. the first leadingPixels
        // pixels of the row. We read 8 bits starting at `sShift - dShift`
        // (which may be negative); negative offsets pull leading zero bits.
        int srcBit = sShift - dShift;
        uint8_t srcByte;
        if (srcBit < 0)
            srcByte = uint8_t(sp[0] >> (-srcBit));
        else
            srcByte = Read8(srcBit);
        *dp = ApplyBlitMasked(*dp, srcByte, leftMask, op);
        dp++;
        srcBit += 8;

        for (int i = 0; i < midBytes; i++)
        {
            *dp = ApplyBlit(*dp, Read8(srcBit), op);
            dp++;
            srcBit += 8;
        }

        if (rightMask)
            *dp = ApplyBlitMasked(*dp, Read8(srcBit), rightMask, op);

        srow += src.s;
        drow += s;
    }
}

int MonoBuffer::DrawGlyph(int x, int y, const Font& font, unsigned cp, DrawOp op)
{
    Glyph g = font.GetGlyph(cp);
    if (op != DrawOp::Keep)
    {
        // place the black box: x + left side bearing, baseline-relative
        // vertically (font.ascent is the baseline from the cell top)
        int gx = x + g.bx;
        int gy = y + font.ascent - (g.by + g.bh);
        if (font.IsRLE())
        {
            // RLE glyphs decode straight into horizontal runs - no temp
            // buffer, the op maps directly since spans are foreground only
            font.ForEachSpan(cp, [this, gx, gy, op](int rx, int ry, int len)
            {
                DrawHLine(gx + rx, gy + ry, len, op);
            });
        }
        else if (g.bitmap)
        {
            BlitOp blit = (op == DrawOp::Clear)  ? BlitOp::AndNot :
                          (op == DrawOp::Invert) ? BlitOp::Xor :
                                                   BlitOp::Or;
            MonoBuffer src((void*)g.bitmap, g.bw, g.bh, (g.bw + 7) >> 3);
            Blit(gx, gy, src, 0, 0, g.bw, g.bh, blit);
        }
    }
    return x + g.width + font.spacing;
}

int MonoBuffer::DrawText(int x, int y, const Font& font, Span text, DrawOp op)
{
    if (op == DrawOp::Keep) return x + MeasureText(font, text);

    int pen = x;
    int lineH = font.height + font.spacing;
    auto* sp = text.Pointer();
    auto* end = text.end();
    while (sp < end)
    {
        unsigned c = Utf8Next(sp, end);
        if (c == '\n')
        {
            y += lineH;
            pen = x;
            continue;
        }
        pen = DrawGlyph(pen, y, font, c, op);
    }
    return pen;
}

int MonoBuffer::MeasureText(const Font& font, Span text)
{
    int pen = 0, longest = 0;
    int spacing = font.spacing;
    auto* p = text.Pointer();
    auto* end = text.end();
    while (p < end)
    {
        unsigned c = Utf8Next(p, end);
        if (c == '\n')
        {
            if (pen > longest) longest = pen;
            pen = 0;
            continue;
        }
        pen += font.GetGlyph(c).width + spacing;
    }
    return pen > longest ? pen : longest;
}

/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * mono-gfx/Utf8.h
 *
 * Minimal lenient UTF-8 decoder shared by the text renderer and the UI
 * rasteriser - one source of truth for how a string maps to code points.
 */

#pragma once

#include <base/base.h>

//! Decodes the next code point from [@p p, @p end), advancing @p p
/*!
 * Lenient: a byte that is not a valid (non-overlong, non-surrogate,
 * in-range) UTF-8 sequence start/continuation is returned as-is and the
 * pointer advances exactly one byte, so ASCII is untouched and malformed
 * input never stalls or runs past @p end.
 */
ALWAYS_INLINE static unsigned Utf8Next(const char*& p, const char* end)
{
    unsigned b0 = (unsigned char)p[0];
    if (b0 < 0x80) { p += 1; return b0; }

    int n;
    unsigned cp, minv;
    if ((b0 & 0xE0) == 0xC0)      { n = 1; cp = b0 & 0x1F; minv = 0x80; }
    else if ((b0 & 0xF0) == 0xE0) { n = 2; cp = b0 & 0x0F; minv = 0x800; }
    else if ((b0 & 0xF8) == 0xF0) { n = 3; cp = b0 & 0x07; minv = 0x10000; }
    else { p += 1; return b0; }                  // invalid lead byte

    if (p + 1 + n > end) { p += 1; return b0; }   // truncated
    for (int i = 1; i <= n; i++)
    {
        unsigned c = (unsigned char)p[i];
        if ((c & 0xC0) != 0x80) { p += 1; return b0; }   // bad continuation
        cp = (cp << 6) | (c & 0x3F);
    }
    if (cp < minv || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
    {
        p += 1; return b0;                        // overlong / surrogate / oor
    }
    p += 1 + n;
    return cp;
}

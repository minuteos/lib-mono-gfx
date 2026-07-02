/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * mono-gfx/Ui.h
 *
 * High-level rendering context: the text kit and panel chrome that UI
 * screens draw through. Today it forwards straight to the MonoBuffer;
 * the same call surface later becomes the op recorder for two-pass
 * incremental rendering, so screens written against it stay unchanged.
 */

#pragma once

#include <mono-gfx/MonoBuffer.h>
#include <mono-gfx/Font.h>

class Ui
{
public:
    explicit Ui(MonoBuffer& fb) : fb(&fb) {}

    //! Tight ink box of an ASCII string: x/y are the ink offset from the
    //! pen origin, w/h its extent
    struct Ink { int x, y, w, h; };
    static Ink MeasureInk(const Font& f, const char* s);

    // ---- text
    void Text(int x, int y, const Font& f, const char* s, DrawOp op = DrawOp::Set)
        { fb->DrawText(x, y, f, s, op); }
    void Glyph(int x, int y, const Font& f, unsigned cp, DrawOp op = DrawOp::Set)
        { fb->DrawGlyph(x, y, f, cp, op); }

    //! Draws @p s centred in the box, auto-picking the first ladder font
    //! whose ink fits with a 2px margin per side (falls back to the last),
    //! then centring the ink box
    void Fit(int x, int y, int w, int h, const Font* const* ladder, int ladderCount,
             const char* s, int maxFontSize = 0);

    //! Draws @p s word-wrapped to @p maxW, the block centred on (cx, cy)
    void Wrapped(int cx, int cy, int maxW, const Font& f, const char* s);

    //! Solid bar with centred knocked-out text (widget label style);
    //! returns the bar height
    int LabelBar(int x, int y, int w, const Font& f, const char* s);

    // ---- panel chrome
    //! Opaque rounded box: 2px border as the difference of two fills, so
    //! the border band and the interior share the same corner pixels
    void Panel(int x, int y, int w, int h, int r);

    //! Panel with a title bar whose rounded top matches the interior;
    //! (cx, cy) receive the centre for content of height @p contentH
    void Toast(int x, int y, int w, int h, int r, const Font& titleFont,
               const char* title, int contentH, int& cx, int& cy);

    // ---- shapes
    void Fill(int x, int y, int w, int h, DrawOp op = DrawOp::Set)
        { fb->FillRect(x, y, w, h, op); }
    void FillRound(int x, int y, int w, int h, int r, DrawOp op = DrawOp::Set)
        { fb->FillRoundRect(x, y, w, h, r, op); }
    void Round(int x, int y, int w, int h, int r, DrawOp op = DrawOp::Set)
        { fb->DrawRoundRect(x, y, w, h, r, op); }

    //! Custom painter escape hatch. @p contentHash must identify the
    //! painted content (inputs of the painter); it is what will let the
    //! incremental renderer skip an unchanged region
    template<typename F> void Custom(int x, int y, int w, int h,
                                     uint32_t contentHash, F&& paint)
    {
        (void)x; (void)y; (void)w; (void)h; (void)contentHash;
        paint(*fb);
    }

    //! Direct buffer access for code not yet migrated to the op surface
    MonoBuffer& Raw() { return *fb; }

private:
    MonoBuffer* fb;
};

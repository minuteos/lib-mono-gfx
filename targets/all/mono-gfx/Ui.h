/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * mono-gfx/Ui.h
 *
 * High-level rendering context: the text kit and panel chrome that UI
 * screens draw through. Besides direct drawing it supports two-pass
 * incremental rendering: a collect pass hashes every op into a slot
 * table diffed against the previous frame (yielding dirty regions), and
 * a draw pass repaints only ops intersecting them. Screens are unaware
 * of the mode - they just render every frame.
 */

#pragma once

#include <mono-gfx/MonoBuffer.h>
#include <mono-gfx/Font.h>

//! A dirty region: up to four disjoint rectangles, merged by least growth
struct UiDirty
{
    static constexpr int MaxRects = 4;

    struct R { int16_t x0, y0, x1, y1; };      // exclusive x1/y1
    R rects[MaxRects];
    int8_t count = 0;

    void Clear() { count = 0; }
    bool IsEmpty() const { return count == 0; }
    void Add(int x, int y, int w, int h);
    bool Intersects(int x, int y, int w, int h) const;
    void SetAll(int w, int h)
    {
        count = 1;
        rects[0] = { 0, 0, int16_t(w), int16_t(h) };
    }
};

//! One recorded op: bounding rectangle + content hash
struct UiSlot
{
    int16_t x, y, w, h;
    uint32_t hash;
};

//! Frame-to-frame op diff over a single slot array (compare-and-overwrite)
class UiDiff
{
public:
    void SetStorage(UiSlot* storage, int capacity)
    {
        slots = storage;
        cap = capacity;
        prevCount = 0;
        forceFull = true;
    }

    //! Forces the next frame to be treated as fully dirty
    void Invalidate() { forceFull = true; }

    void Begin() { count = 0; dirty.Clear(); overflow = false; }
    void Note(int x, int y, int w, int h, uint32_t hash, bool volatileOp = false);
    //! Finalizes the frame; returns the dirty region
    const UiDirty& End(int screenW, int screenH);

private:
    UiSlot* slots = nullptr;
    int cap = 0;
    int count = 0, prevCount = 0;
    UiDirty dirty;
    bool overflow = false, forceFull = true;
};

class Ui
{
public:
    enum class Mode : uint8_t { Direct, Collect, Draw };

    //! Direct-drawing context (no diffing)
    explicit Ui(MonoBuffer& fb) : fb(&fb) {}
    //! Collect-pass context: ops are hashed into @p diff, nothing is drawn
    Ui(MonoBuffer& fb, UiDiff& diff) : fb(&fb), diff(&diff), mode(Mode::Collect) {}
    //! Draw-pass context: only ops intersecting @p dirty are painted
    Ui(MonoBuffer& fb, const UiDirty& dirty) : fb(&fb), dirty(&dirty), mode(Mode::Draw) {}

    //! Logical screen dimensions - screens should lay out against these
    //! rather than compile-time constants so rotation just works
    int Width() const { return fb->Width(); }
    int Height() const { return fb->Height(); }

    //! Tight ink box of an ASCII string: x/y are the ink offset from the
    //! pen origin, w/h its extent
    struct Ink { int x, y, w, h; };
    static Ink MeasureInk(const Font& f, const char* s);

    // ---- text
    void Text(int x, int y, const Font& f, const char* s, DrawOp op = DrawOp::Set);
    void Glyph(int x, int y, const Font& f, unsigned cp, DrawOp op = DrawOp::Set);

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
    void Fill(int x, int y, int w, int h, DrawOp op = DrawOp::Set);
    void FillRound(int x, int y, int w, int h, int r, DrawOp op = DrawOp::Set);
    void Round(int x, int y, int w, int h, int r, DrawOp op = DrawOp::Set);

    //! Custom painter escape hatch. @p contentHash must identify the
    //! painted content (the painter's inputs); 0 means unknown, treating
    //! the region as changed every frame
    template<typename F> void Custom(int x, int y, int w, int h,
                                     uint32_t contentHash, F&& paint)
    {
        if (Note(1, x, y, w, h, contentHash, contentHash == 0))
            paint(*fb);
    }

    //! Direct buffer access for code not yet migrated to the op surface;
    //! only meaningful on a direct context
    MonoBuffer& Raw() { return *fb; }

private:
    MonoBuffer* fb;
    UiDiff* diff = nullptr;
    const UiDirty* dirty = nullptr;
    Mode mode = Mode::Direct;

    //! Records/tests an op; @returns true if it should be painted
    bool Note(uint32_t tag, int x, int y, int w, int h, uint32_t hash,
              bool volatileOp = false);
};

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

//! Frame-to-frame op diff over a single slot array (compare-and-overwrite).
//! Two assumptions bound its correctness: (1) each op's painted pixels stay
//! within the bounding box it reports (text ink must not overhang the pen
//! advance by more than the 2px pad), else stale pixels can linger on a
//! changed frame; (2) an op's content hash is 32-bit, so a hash collision on
//! an op whose bounding box is unchanged treats a real change as unchanged
//! (astronomically rare, but not impossible).
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
    explicit Ui(MonoBuffer& fb) : fb(&fb) { InitArea(); }
    //! Collect-pass context: ops are hashed into @p diff, nothing is drawn
    Ui(MonoBuffer& fb, UiDiff& diff) : fb(&fb), diff(&diff), mode(Mode::Collect) { InitArea(); }
    //! Draw-pass context: only ops intersecting @p dirty are painted
    Ui(MonoBuffer& fb, const UiDirty& dirty) : fb(&fb), dirty(&dirty), mode(Mode::Draw) { InitArea(); }

    //! Rectangle in the current area's local coordinates
    struct Rect { int x, y, w, h; };

    // ---- layout areas
    //! Dimensions of the current layout area (the whole buffer at the top
    //! level) - lay out against these, not compile-time constants
    int Width() const { return aw; }
    int Height() const { return ah; }

    //! Pushes a child area (coordinates relative to the current one). All
    //! subsequent ops address its local space and are clipped to it, until
    //! the matching PopArea; children need no absolute coordinates
    void PushArea(int x, int y, int w, int h);
    void PushArea(const Rect& r) { PushArea(r.x, r.y, r.w, r.h); }
    void PopArea();

    //! RAII area: `auto a = ui.Area(...)` pops when it leaves scope
    class Scope
    {
        Ui* u;
    public:
        explicit Scope(Ui* u) : u(u) {}
        Scope(Scope&& o) : u(o.u) { o.u = nullptr; }
        Scope(const Scope&) = delete;
        ~Scope() { if (u) u->PopArea(); }
    };
    [[nodiscard]] Scope Area(int x, int y, int w, int h) { PushArea(x, y, w, h); return Scope(this); }
    [[nodiscard]] Scope Area(const Rect& r) { PushArea(r); return Scope(this); }

    //! Alignment within an area (H in the low bits, V in the next two)
    enum class Align : uint8_t {
        Left = 0, HCenter = 1, Right = 2,
        Top = 0, VCenter = 4, Bottom = 8,
        Center = HCenter | VCenter,
    };

    //! Tight ink box of a UTF-8 string: x/y are the ink offset from the
    //! pen origin, w/h its extent. Requires an RLE-format font (it scans
    //! glyph spans); a raw-bitmap font yields an empty box, so Fit - which
    //! relies on it - only auto-sizes correctly with RLE fonts.
    struct Ink { int x, y, w, h; };
    static Ink MeasureInk(const Font& f, const char* s);

    // ---- text
    //! Draws @p s at (@p x, @p y) relative to the current area (not the
    //! buffer); unlike MonoBuffer::DrawText it returns nothing - measure
    //! with MeasureText/MeasureInk if you need the pen advance
    void Text(int x, int y, const Font& f, const char* s, DrawOp op = DrawOp::Set);
    //! Draws one glyph at (@p x, @p y) relative to the current area
    void Glyph(int x, int y, const Font& f, unsigned cp, DrawOp op = DrawOp::Set);

    //! Draws @p s within the current area, aligned - no coordinates needed
    void Label(const Font& f, const char* s, Align a = Align::Left, DrawOp op = DrawOp::Set);

    //! Draws @p s centred in the box, auto-picking the first ladder font
    //! whose ink fits with a 2px margin per side (falls back to the last);
    //! @p ladder must hold at least one font (@p ladderCount > 0)
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
    //! @returns the interior content area below the bar (in the current
    //! area's local coordinates), ready to PushArea into
    Rect Toast(int x, int y, int w, int h, int r, const Font& titleFont,
               const char* title);

    // ---- shapes (coordinates relative to the current area)
    //! Filled rectangle
    void Fill(int x, int y, int w, int h, DrawOp op = DrawOp::Set);
    //! Filled rounded rectangle
    void FillRound(int x, int y, int w, int h, int r, DrawOp op = DrawOp::Set);
    //! Rounded-rectangle outline (stroke only - the Fill* variants fill)
    void Round(int x, int y, int w, int h, int r, DrawOp op = DrawOp::Set);

    //! Custom painter escape hatch; the painter receives the buffer and the
    //! op's absolute rect. @p contentHash must identify the painted content
    //! (the painter's inputs); 0 means unknown, repainting every frame
    template<typename F> void Custom(int x, int y, int w, int h,
                                     uint32_t contentHash, F&& paint)
    {
        x += aox; y += aoy;
        if (Note(1, x, y, w, h, contentHash, contentHash == 0))
            paint(*fb, x, y, w, h);
    }

    //! Direct buffer access for code not yet migrated to the op surface;
    //! only meaningful on a direct context
    MonoBuffer& Raw() { return *fb; }

private:
    MonoBuffer* fb;
    UiDiff* diff = nullptr;
    const UiDirty* dirty = nullptr;
    Mode mode = Mode::Direct;

    // current layout area: local (0,0) maps to absolute (aox, aoy), with
    // logical size aw x ah; a clip mirror tracks the buffer clip so nested
    // areas intersect rather than replace
    int aox = 0, aoy = 0, aw = 0, ah = 0;
    int16_t clx0 = 0, cly0 = 0, clx1 = 0, cly1 = 0;
    struct SavedArea { int ox, oy, w, h; int16_t x0, y0, x1, y1; };
    static constexpr int MaxAreas = 8;
    SavedArea areaStack[MaxAreas];
    int areaDepth = 0;
    void InitArea();

    //! Records/tests an op; @returns true if it should be painted
    bool Note(uint32_t tag, int x, int y, int w, int h, uint32_t hash,
              bool volatileOp = false);

    //! Text with pre-measured extent (w/h), so Label doesn't re-measure
    void TextAt(int x, int y, int w, int h, const Font& f, const char* s, DrawOp op);
};

constexpr Ui::Align operator|(Ui::Align a, Ui::Align b)
{ return Ui::Align(uint8_t(a) | uint8_t(b)); }

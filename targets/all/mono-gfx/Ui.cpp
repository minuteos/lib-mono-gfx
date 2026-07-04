/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * mono-gfx/Ui.cpp
 *
 * Text kit, panel chrome and the op diff for incremental rendering.
 */

#include "Ui.h"

// ---- UiDirty

void UiDirty::Add(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    int16_t x0 = x, y0 = y, x1 = x + w, y1 = y + h;

    // grow an overlapping/touching rect if one exists
    for (int i = 0; i < count; i++)
    {
        R& r = rects[i];
        if (x0 <= r.x1 && x1 >= r.x0 && y0 <= r.y1 && y1 >= r.y0)
        {
            if (x0 < r.x0) r.x0 = x0;
            if (y0 < r.y0) r.y0 = y0;
            if (x1 > r.x1) r.x1 = x1;
            if (y1 > r.y1) r.y1 = y1;
            return;
        }
    }

    if (count < MaxRects)
    {
        rects[count++] = { x0, y0, x1, y1 };
        return;
    }

    // full: merge into the rect that grows the least
    int best = 0;
    int32_t bestGrowth = INT32_MAX;
    for (int i = 0; i < count; i++)
    {
        const R& r = rects[i];
        int32_t ux0 = x0 < r.x0 ? x0 : r.x0, uy0 = y0 < r.y0 ? y0 : r.y0;
        int32_t ux1 = x1 > r.x1 ? x1 : r.x1, uy1 = y1 > r.y1 ? y1 : r.y1;
        int32_t growth = (ux1 - ux0) * (uy1 - uy0) - (r.x1 - r.x0) * (r.y1 - r.y0);
        if (growth < bestGrowth) { bestGrowth = growth; best = i; }
    }
    R& r = rects[best];
    if (x0 < r.x0) r.x0 = x0;
    if (y0 < r.y0) r.y0 = y0;
    if (x1 > r.x1) r.x1 = x1;
    if (y1 > r.y1) r.y1 = y1;
}

bool UiDirty::Intersects(int x, int y, int w, int h) const
{
    int16_t x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    for (int i = 0; i < count; i++)
    {
        const R& r = rects[i];
        if (x0 < r.x1 && x1 > r.x0 && y0 < r.y1 && y1 > r.y0)
            return true;
    }
    return false;
}

// ---- UiDiff

void UiDiff::Note(int x, int y, int w, int h, uint32_t hash, bool volatileOp)
{
    if (!slots || count >= cap)
    {
        overflow = true;
        return;
    }
    if (!volatileOp && count < prevCount)
    {
        const UiSlot& p = slots[count];
        if (p.x == x && p.y == y && p.w == w && p.h == h && p.hash == hash)
        {
            count++;
            return;
        }
        dirty.Add(p.x, p.y, p.w, p.h);      // where the old content was
    }
    dirty.Add(x, y, w, h);
    slots[count++] = { int16_t(x), int16_t(y), int16_t(w), int16_t(h), hash };
}

const UiDirty& UiDiff::End(int screenW, int screenH)
{
    // ops that disappeared leave their old pixels dirty
    for (int i = count; i < prevCount; i++)
        dirty.Add(slots[i].x, slots[i].y, slots[i].w, slots[i].h);

    if (overflow || forceFull)
    {
        dirty.SetAll(screenW, screenH);
        forceFull = false;
        // an overflowing frame leaves the table incomplete; resync fully
        prevCount = overflow ? 0 : count;
        return dirty;
    }

    prevCount = count;

    // op rectangles may extend off screen, the repaintable region does not
    int n = 0;
    for (int i = 0; i < dirty.count; i++)
    {
        auto r = dirty.rects[i];
        if (r.x0 < 0) r.x0 = 0;
        if (r.y0 < 0) r.y0 = 0;
        if (r.x1 > screenW) r.x1 = screenW;
        if (r.y1 > screenH) r.y1 = screenH;
        if (r.x0 < r.x1 && r.y0 < r.y1)
            dirty.rects[n++] = r;
    }
    dirty.count = n;
    return dirty;
}

// ---- op hashing

namespace
{

struct OpHash
{
    uint32_t v = 2166136261u;
    OpHash& M(uint32_t x)
    {
        for (int i = 0; i < 4; i++, x >>= 8)
            v = (v ^ (x & 0xFF)) * 16777619u;
        return *this;
    }
    OpHash& P(const void* p) { return M(uint32_t(uintptr_t(p))); }
    OpHash& S(const char* s)
    {
        while (*s) v = (v ^ uint8_t(*s++)) * 16777619u;
        return *this;
    }
};

}

// ---- layout areas

void Ui::InitArea()
{
    aox = aoy = 0;
    aw = fb->Width();
    ah = fb->Height();
    clx0 = fb->ClipLeft();  cly0 = fb->ClipTop();
    clx1 = fb->ClipRight(); cly1 = fb->ClipBottom();
}

void Ui::PushArea(int x, int y, int w, int h)
{
    ASSERT(areaDepth < MaxAreas);
    areaStack[areaDepth] = { aox, aoy, aw, ah, clx0, cly0, clx1, cly1 };
    areaDepth++;

    aox += x; aoy += y; aw = w; ah = h;

    // intersect the area with the clip already in effect (never widen it)
    int nx0 = aox > clx0 ? aox : clx0;
    int ny0 = aoy > cly0 ? aoy : cly0;
    int nx1 = aox + w < clx1 ? aox + w : clx1;
    int ny1 = aoy + h < cly1 ? aoy + h : cly1;
    if (nx1 < nx0) nx1 = nx0;
    if (ny1 < ny0) ny1 = ny0;
    clx0 = nx0; cly0 = ny0; clx1 = nx1; cly1 = ny1;
    fb->SetClip(clx0, cly0, clx1 - clx0, cly1 - cly0);
}

void Ui::PopArea()
{
    if (areaDepth <= 0) return;
    areaDepth--;
    if (areaDepth < MaxAreas)
    {
        const SavedArea& s = areaStack[areaDepth];
        aox = s.ox; aoy = s.oy; aw = s.w; ah = s.h;
        clx0 = s.x0; cly0 = s.y0; clx1 = s.x1; cly1 = s.y1;
        fb->SetClip(clx0, cly0, clx1 - clx0, cly1 - cly0);
    }
}

bool Ui::Note(uint32_t tag, int x, int y, int w, int h, uint32_t hash, bool volatileOp)
{
    switch (mode)
    {
        case Mode::Direct:
            return true;
        case Mode::Collect:
            diff->Note(x, y, w, h, OpHash().M(tag).M(hash).v, volatileOp);
            return false;
        case Mode::Draw:
            // volatile ops added their rect to the dirty region during
            // collect, so a plain intersection test covers them too
            return dirty->Intersects(x, y, w, h);
    }
    return true;
}

// ---- text kit

Ui::Ink Ui::MeasureInk(const Font& f, const char* s)
{
    int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN, pen = 0;
    for (; *s; s++)
    {
        unsigned cp = (unsigned char)*s;
        ::Glyph g = f.GetGlyph(cp);
        // measure in the exact space DrawText paints: pen + left bearing
        // horizontally, baseline-relative vertically
        int gx = pen + g.bx;
        int gy = f.ascent - (g.by + g.bh);
        f.ForEachSpan(cp, [&](int dx, int dy, int len) {
            int x = gx + dx, y = gy + dy;
            if (x < x0) x0 = x;
            if (x + len > x1) x1 = x + len;
            if (y < y0) y0 = y;
            if (y + 1 > y1) y1 = y + 1;
        });
        pen += g.width + f.spacing;
    }
    if (x1 < x0) return { 0, 0, 0, 0 };
    return { x0, y0, x1 - x0, y1 - y0 };
}

void Ui::TextAt(int x, int y, int w, int h, const Font& f, const char* s, DrawOp op)
{
    x += aox; y += aoy;
    // pad the box horizontally for glyph side bearings
    if (Note(2, x - 2, y, w + 4, h,
             OpHash().M(x).M(y).P(&f).M(unsigned(op)).S(s).v))
        fb->DrawText(x, y, f, s, op);
}

void Ui::Text(int x, int y, const Font& f, const char* s, DrawOp op)
{
    int h, w = MonoBuffer::MeasureText(f, s, &h);
    TextAt(x, y, w, h, f, s, op);
}

void Ui::Glyph(int x, int y, const Font& f, unsigned cp, DrawOp op)
{
    x += aox; y += aoy;
    ::Glyph g = f.GetGlyph(cp);
    if (Note(3, x - 2, y, g.width + 4, f.height,
             OpHash().M(x).M(y).P(&f).M(cp).M(unsigned(op)).v))
        fb->DrawGlyph(x, y, f, cp, op);
}

void Ui::Label(const Font& f, const char* s, Align a, DrawOp op)
{
    int th, tw = MonoBuffer::MeasureText(f, s, &th);
    int hp = unsigned(a) & 3, vp = (unsigned(a) >> 2) & 3;
    int x = hp == 1 ? (aw - tw) / 2 : hp == 2 ? aw - tw : 0;
    int y = vp == 1 ? (ah - th) / 2 : vp == 2 ? ah - th : 0;
    TextAt(x, y, tw, th, f, s, op);     // reuse the measurement
}

void Ui::Fit(int x, int y, int w, int h, const Font* const* ladder, int ladderCount,
             const char* s, int maxFontSize)
{
    x += aox; y += aoy;
    if (!Note(4, x, y, w, h,
              OpHash().M(x).M(y).M(w).M(h).P(ladder).M(ladderCount).M(maxFontSize).S(s).v))
        return;

    // walk the ladder largest-first, keeping the last measured font as the
    // fallback; no separate pre-measure to throw away when one fits
    const Font* font = nullptr;
    Ink ink {};
    for (int i = 0; i < ladderCount; i++)
    {
        const Font* f = ladder[i];
        if (maxFontSize > 0 && f->height > maxFontSize) continue;
        font = f; ink = MeasureInk(*f, s);
        if (ink.w <= w - 4 && ink.h <= h - 4) break;
    }
    if (!font) { font = ladder[ladderCount - 1]; ink = MeasureInk(*font, s); }

    int tx = x + ((w - ink.w) >> 1) - ink.x;
    int ty = y + ((h - ink.h) >> 1) - ink.y;
    fb->DrawText(tx, ty, *font, s);
}

void Ui::Wrapped(int cx, int cy, int maxW, const Font& font, const char* text)
{
    if (!text || !*text) return;
    cx += aox; cy += aoy;

    const char* lines[8];
    int lens[8], n = 0;
    const char* p = text;
    while (*p && n < 8)
    {
        const char* start = p;
        const char* fit = nullptr;
        const char* q = p;
        while (*q && *q != '\n')
        {
            const char* we = q;
            while (*we && *we != ' ' && *we != '\n') we++;
            if (fit && MonoBuffer::MeasureText(font, Span(start, we)) > maxW)
                break;
            fit = we;
            q = *we == ' ' ? we + 1 : we;
        }
        const char* end = fit ? fit : q;
        lines[n] = start; lens[n] = end - start; n++;
        p = end;
        while (*p == ' ') p++;
        if (*p == '\n') p++;
    }

    int lineH = font.height + font.spacing;
    int top = cy - n * lineH / 2;
    if (!Note(5, cx - maxW / 2 - 2, top, maxW + 4, n * lineH,
              OpHash().M(cx).M(cy).M(maxW).P(&font).S(text).v))
        return;

    int y = top;
    for (int i = 0; i < n; i++)
    {
        Span s(lines[i], lens[i]);
        int w = MonoBuffer::MeasureText(font, s);
        fb->DrawText(cx - w / 2, y, font, s);
        y += lineH;
    }
}

int Ui::LabelBar(int x, int y, int w, const Font& f, const char* s)
{
    x += aox; y += aoy;
    int h = f.height;
    if (Note(6, x, y, w, h, OpHash().M(x).M(y).M(w).P(&f).S(s).v))
    {
        fb->FillRect(x, y, w, h, DrawOp::Set);
        int tw = MonoBuffer::MeasureText(f, s);
        fb->DrawText(x + ((w - tw) >> 1), y, f, s, DrawOp::Clear);
    }
    return h;
}

// ---- panel chrome

void Ui::Panel(int x, int y, int w, int h, int r)
{
    x += aox; y += aoy;
    if (!Note(7, x, y, w, h, OpHash().M(x).M(y).M(w).M(h).M(r).v))
        return;
    fb->FillRoundRect(x, y, w, h, r, DrawOp::Set);
    fb->FillRoundRect(x + 2, y + 2, w - 4, h - 4, r - 2, DrawOp::Clear);
}

Ui::Rect Ui::Toast(int x, int y, int w, int h, int r, const Font& titleFont,
                   const char* title)
{
    int barH = titleFont.height + 2;
    // interior below the title bar, in local coordinates (for the caller)
    Rect content = { x + 2, y + 2 + barH, w - 4, h - 4 - barH };

    int ax = x + aox, ay = y + aoy;
    if (!Note(8, ax, ay, w, h,
              OpHash().M(ax).M(ay).M(w).M(h).M(r).P(&titleFont).S(title).v))
        return content;

    fb->FillRoundRect(ax, ay, w, h, r, DrawOp::Set);
    fb->FillRoundRect(ax + 2, ay + 2, w - 4, h - 4, r - 2, DrawOp::Clear);

    // title bar with the interior's rounded top: a rounded fill extending
    // r-2 below the bar, whose overhang is then cleared - the overhang
    // rows lie in the straight-wall zone (barH >= r-2), so the border
    // band's corner arcs are never touched
    fb->FillRoundRect(ax + 2, ay + 2, w - 4, barH + (r - 2), r - 2, DrawOp::Set);
    fb->FillRect(ax + 2, ay + 2 + barH, w - 4, r - 2, DrawOp::Clear);

    int tw = MonoBuffer::MeasureText(titleFont, title);
    fb->DrawText(ax + (w - tw) / 2, ay + 3, titleFont, title, DrawOp::Clear);
    return content;
}

// ---- shapes

void Ui::Fill(int x, int y, int w, int h, DrawOp op)
{
    x += aox; y += aoy;
    if (Note(9, x, y, w, h, OpHash().M(x).M(y).M(w).M(h).M(unsigned(op)).v))
        fb->FillRect(x, y, w, h, op);
}

void Ui::FillRound(int x, int y, int w, int h, int r, DrawOp op)
{
    x += aox; y += aoy;
    if (Note(10, x, y, w, h, OpHash().M(x).M(y).M(w).M(h).M(r).M(unsigned(op)).v))
        fb->FillRoundRect(x, y, w, h, r, op);
}

void Ui::Round(int x, int y, int w, int h, int r, DrawOp op)
{
    x += aox; y += aoy;
    if (Note(11, x, y, w, h, OpHash().M(x).M(y).M(w).M(h).M(r).M(unsigned(op)).v))
        fb->DrawRoundRect(x, y, w, h, r, op);
}

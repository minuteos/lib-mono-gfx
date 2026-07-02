/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * mono-gfx/Ui.cpp
 *
 * Text kit and panel chrome implementations.
 */

#include "Ui.h"

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

void Ui::Fit(int x, int y, int w, int h, const Font* const* ladder, int ladderCount,
             const char* s, int maxFontSize)
{
    const Font* font = ladder[ladderCount - 1];
    Ink ink = MeasureInk(*font, s);
    for (int i = 0; i < ladderCount; i++)
    {
        const Font* f = ladder[i];
        if (maxFontSize > 0 && f->height > maxFontSize) continue;
        Ink k = MeasureInk(*f, s);
        if (k.w <= w - 4 && k.h <= h - 4) { font = f; ink = k; break; }
    }
    int tx = x + ((w - ink.w) >> 1) - ink.x;
    int ty = y + ((h - ink.h) >> 1) - ink.y;
    fb->DrawText(tx, ty, *font, s);
}

void Ui::Wrapped(int cx, int cy, int maxW, const Font& font, const char* text)
{
    if (!text || !*text) return;

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
    int y = cy - n * lineH / 2;
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
    int h = f.height;
    fb->FillRect(x, y, w, h, DrawOp::Set);
    int tw = MonoBuffer::MeasureText(f, s);
    fb->DrawText(x + ((w - tw) >> 1), y, f, s, DrawOp::Clear);
    return h;
}

void Ui::Panel(int x, int y, int w, int h, int r)
{
    fb->FillRoundRect(x, y, w, h, r, DrawOp::Set);
    fb->FillRoundRect(x + 2, y + 2, w - 4, h - 4, r - 2, DrawOp::Clear);
}

void Ui::Toast(int x, int y, int w, int h, int r, const Font& titleFont,
               const char* title, int contentH, int& cx, int& cy)
{
    Panel(x, y, w, h, r);

    // title bar with the interior's rounded top: a rounded fill extending
    // r-2 below the bar, whose overhang is then cleared - the overhang
    // rows lie in the straight-wall zone (barH >= r-2), so the border
    // band's corner arcs are never touched
    int barH = titleFont.height + 2;
    fb->FillRoundRect(x + 2, y + 2, w - 4, barH + (r - 2), r - 2, DrawOp::Set);
    fb->FillRect(x + 2, y + 2 + barH, w - 4, r - 2, DrawOp::Clear);

    int tw = MonoBuffer::MeasureText(titleFont, title);
    fb->DrawText(x + (w - tw) / 2, y + 3, titleFont, title, DrawOp::Clear);
    cx = x + w / 2;
    cy = y + 2 + barH + (h - 4 - barH - contentH) / 2;
}

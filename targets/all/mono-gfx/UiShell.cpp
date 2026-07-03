/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * mono-gfx/UiShell.cpp
 */

#include "UiShell.h"

void UiShell::RenderAll(Ui& ui)
{
    if (base) base->Render(ui);
    for (int i = 0; i < layerCount; i++)
        layers[i].s->Render(ui);
}

void UiShell::RenderDirect(MonoBuffer& fb)
{
    fb.Clear();
    Ui ui(fb);
    RenderAll(ui);
}

const UiDirty& UiShell::Tick(MonoBuffer& fb)
{
    while (keyQueue)
    {
        int k = __builtin_ctz(keyQueue);
        RESBIT(keyQueue, k);

        // layers get first refusal, top-down; a handler may mutate the
        // stack (SetBase/Push/Pop), so re-check bounds each step
        bool handled = false;
        for (int i = layerCount - 1; i >= 0 && !handled; i--)
        {
            if (i < layerCount)
                handled = layers[i].s->OnKey(UiKey(k));
        }
        if (!handled && base)
            base->OnKey(UiKey(k));
    }

    for (int i = 0; i < layerCount; )
    {
        if (!layers[i].until.Pending())
        {
            for (int j = i + 1; j < layerCount; j++) layers[j - 1] = layers[j];
            layerCount--;
        }
        else
        {
            i++;
        }
    }

    if (!diffEnabled)
    {
        RenderDirect(fb);
        lastDirty.SetAll(fb.Width(), fb.Height());
        return lastDirty;
    }

    diff.Begin();
    {
        Ui collect(fb, diff);
        RenderAll(collect);
    }
    lastDirty = diff.End(fb.Width(), fb.Height());

    // repaint one dirty rect at a time: the clip both bounds the ops and
    // lets Clear() wipe just the rect, so pixels outside stay valid from
    // the previous frame
    for (int i = 0; i < lastDirty.count; i++)
    {
        auto& r = lastDirty.rects[i];
        fb.SetClip(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
        fb.Clear();
        UiDirty one;
        one.count = 1;
        one.rects[0] = r;
        Ui draw(fb, one);
        RenderAll(draw);
    }
    fb.ClearClip();
    return lastDirty;
}

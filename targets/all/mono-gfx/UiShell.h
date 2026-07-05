/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * mono-gfx/UiShell.h
 *
 * Layer stack and input pump for immediate-mode UIs: one base screen
 * plus transient (timeout) layers. Keys are offered to layers top-down,
 * then to the base; rendering runs bottom-up. The client owns all
 * UiScreen storage - the shell only keeps pointers.
 *
 * With diffing enabled (EnableDiff) a frame renders in two passes: a
 * collect pass hashes every op against the previous frame to find dirty
 * regions, then only those regions are cleared and repainted. Tick
 * returns the dirty region so the client can flush just that part of
 * the framebuffer - or nothing at all when the frame is unchanged.
 */

#pragma once

#include <kernel/kernel.h>

#include <mono-gfx/Ui.h>

//! Keys a UI shell routes; how buttons map to them is the client's business
enum class UiKey : uint8_t { Enter, Next, Up, Down, Left, Right };

//! A layer: renders in stack order, gets first refusal on keys in reverse
class UiScreen
{
public:
    virtual void Render(Ui& ui) = 0;
    //! Handles a key; @returns true if consumed. May mutate the shell
    //! (SetBase/Push/Pop own layer). Popping a layer *below* this one while
    //! returning false can misroute the key, so don't - pop self or consume.
    virtual bool OnKey(UiKey k) { return false; }
};

class UiShell
{
public:
    //! Replaces the base screen; transient layers are dismissed
    //! (switching content dismisses overlays)
    void SetBase(UiScreen& s)
    {
        base = &s;
        layerCount = 0;
    }

    UiScreen* Base() const { return base; }

    //! Shows a transient layer (up to MaxLayers; a push past that is
    //! dropped). Pushing an already shown layer just
    //! extends its timeout
    void Push(UiScreen& s, Timeout timeout)
    {
        for (int i = 0; i < layerCount; i++)
        {
            if (layers[i].s == &s)
            {
                layers[i].until = timeout.MakeAbsolute();
                return;
            }
        }
        if (layerCount < MaxLayers)
        {
            layers[layerCount].s = &s;
            layers[layerCount].until = timeout.MakeAbsolute();
            layerCount++;
        }
    }

    void Pop(UiScreen& s)
    {
        for (int i = 0; i < layerCount; i++)
        {
            if (layers[i].s == &s)
            {
                for (int j = i + 1; j < layerCount; j++) layers[j - 1] = layers[j];
                layerCount--;
                return;
            }
        }
    }

    //! Queues a key (safe to call from event handlers)
    void Key(UiKey k) { SETBIT(keyQueue, unsigned(k)); }

    //! Enables incremental rendering over caller-provided slot storage
    void EnableDiff(UiSlot* slots, int capacity) { diff.SetStorage(slots, capacity); diffEnabled = true; }
    //! Forces the next Tick to repaint (and report) the full screen
    void Invalidate() { diff.Invalidate(); }

    //! Routes queued keys, expires layers and renders one frame;
    //! @returns the region that changed (the full screen without diffing)
    const UiDirty& Tick(MonoBuffer& fb);

    //! Clears and renders the full frame without touching the diff state
    //! (reference render for validating the incremental path)
    void RenderDirect(MonoBuffer& fb);

private:
    static constexpr int MaxLayers = 4;

    UiScreen* base = nullptr;
    struct { UiScreen* s; Timeout until; } layers[MaxLayers];
    int layerCount = 0;
    unsigned keyQueue = 0;
    UiDiff diff;
    UiDirty lastDirty;
    bool diffEnabled = false;

    void RenderAll(Ui& ui);
};

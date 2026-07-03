/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * UiDiff.cpp - two-pass incremental rendering (op diff + dirty regions)
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>
#include <mono-gfx/fonts/Font5x7.h>

namespace
{

constexpr int W = 64, H = 48;

struct Frame
{
    uint8_t mem[(W / 8) * H] = {};
    MonoBuffer fb{mem, W, H};
};

//! Renders one frame incrementally the way a shell tick does: collect,
//! then clear + draw each dirty rect under its clip
template<typename R> const UiDirty& Incremental(UiDiff& diff, MonoBuffer& fb, R&& render)
{
    static UiDirty dirty;
    diff.Begin();
    {
        Ui ui(fb, diff);
        render(ui);
    }
    dirty = diff.End(fb.Width(), fb.Height());
    for (int i = 0; i < dirty.count; i++)
    {
        auto& r = dirty.rects[i];
        fb.SetClip(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
        fb.Clear();
        UiDirty one;
        one.count = 1;
        one.rects[0] = r;
        Ui ui(fb, one);
        render(ui);
    }
    fb.ClearClip();
    return dirty;
}

template<typename R> void AssertMatchesDirect(MonoBuffer& fb, R&& render)
{
    Frame ref;
    Ui ui(ref.fb);
    render(ui);
    AssertEqual(memcmp(fb.Data(), ref.fb.Data(), fb.Size()), 0);
}

TEST_CASE("01 First frame is fully dirty, an identical frame is clean")
{
    Frame f;
    UiSlot slots[16];
    UiDiff diff;
    diff.SetStorage(slots, 16);

    auto render = [](Ui& ui) {
        ui.Fill(4, 4, 20, 10);
        ui.Text(8, 20, Font5x7, "AB");
    };

    auto& d1 = Incremental(diff, f.fb, render);
    AssertEqual(d1.count, 1);
    AssertEqual(int(d1.rects[0].x0), 0);
    AssertEqual(int(d1.rects[0].y1), H);
    AssertMatchesDirect(f.fb, render);

    auto& d2 = Incremental(diff, f.fb, render);
    AssertEqual(d2.IsEmpty(), true);
}

TEST_CASE("02 A changed op dirties it, unchanged pixels survive")
{
    Frame f;
    UiSlot slots[16];
    UiDiff diff;
    diff.SetStorage(slots, 16);

    int v = 1;
    char buf[4];
    auto render = [&](Ui& ui) {
        ui.Fill(2, 2, 10, 6);
        buf[0] = char('0' + v); buf[1] = 0;
        ui.Text(4, 30, Font5x7, buf);
    };

    Incremental(diff, f.fb, render);
    v = 2;
    auto& d = Incremental(diff, f.fb, render);
    AssertEqual(d.IsEmpty(), false);
    // the static fill is not part of the dirty region
    Assert(!d.Intersects(2, 2, 10, 6));
    AssertMatchesDirect(f.fb, render);
}

TEST_CASE("03 A disappearing op leaves its old rect dirty")
{
    Frame f;
    UiSlot slots[16];
    UiDiff diff;
    diff.SetStorage(slots, 16);

    bool overlay = true;
    auto render = [&](Ui& ui) {
        ui.Fill(0, 0, W, 8);
        if (overlay) ui.FillRound(20, 20, 24, 16, 3);
    };

    Incremental(diff, f.fb, render);
    overlay = false;
    auto& d = Incremental(diff, f.fb, render);
    Assert(d.Intersects(20, 20, 24, 16));
    AssertMatchesDirect(f.fb, render);
}

TEST_CASE("04 A moved op dirties both the old and the new place")
{
    Frame f;
    UiSlot slots[16];
    UiDiff diff;
    diff.SetStorage(slots, 16);

    int x = 4;
    auto render = [&](Ui& ui) { ui.Fill(x, 40, 8, 6); };

    Incremental(diff, f.fb, render);
    x = 40;
    auto& d = Incremental(diff, f.fb, render);
    Assert(d.Intersects(4, 40, 8, 6));
    Assert(d.Intersects(40, 40, 8, 6));
    AssertMatchesDirect(f.fb, render);
}

TEST_CASE("05 Slot overflow degrades to a full repaint and recovers")
{
    Frame f;
    UiSlot slots[4];
    UiDiff diff;
    diff.SetStorage(slots, 4);

    int n = 6;
    auto render = [&](Ui& ui) {
        for (int i = 0; i < n; i++)
            ui.Fill(i * 10, 10, 8, 8);
    };

    auto& d1 = Incremental(diff, f.fb, render);
    AssertEqual(int(d1.rects[0].x1), W);        // overflow -> full screen
    AssertMatchesDirect(f.fb, render);

    n = 3;                                      // fits now, but the table
    auto& d2 = Incremental(diff, f.fb, render); // was incomplete -> full
    AssertEqual(d2.IsEmpty(), false);
    AssertMatchesDirect(f.fb, render);

    auto& d3 = Incremental(diff, f.fb, render);
    AssertEqual(d3.IsEmpty(), true);
}

TEST_CASE("06 Volatile ops repaint every frame, hashed ops do not")
{
    Frame f;
    UiSlot slots[16];
    UiDiff diff;
    diff.SetStorage(slots, 16);

    int paints = 0;
    auto render = [&](Ui& ui) {
        ui.Custom(4, 4, 16, 16, 0, [&](MonoBuffer& fb) {
            paints++;
            fb.FillCircle(12, 12, 6);
        });
        ui.Custom(30, 4, 16, 16, 123, [&](MonoBuffer& fb) {
            fb.FillRect(32, 6, 8, 8);
        });
    };

    Incremental(diff, f.fb, render);
    paints = 0;
    auto& d = Incremental(diff, f.fb, render);
    AssertEqual(paints, 1);                     // draw pass only
    Assert(d.Intersects(4, 4, 16, 16));
    Assert(!d.Intersects(30, 4, 16, 16));
    AssertMatchesDirect(f.fb, render);
}

TEST_CASE("07 Off-screen op rects are clamped out of the dirty region")
{
    Frame f;
    UiSlot slots[16];
    UiDiff diff;
    diff.SetStorage(slots, 16);

    int y = -4;
    auto render = [&](Ui& ui) { ui.Fill(-8, y, 12, 8); };

    Incremental(diff, f.fb, render);
    y = -3;
    auto& d = Incremental(diff, f.fb, render);
    for (int i = 0; i < d.count; i++)
    {
        Assert(d.rects[i].x0 >= 0 && d.rects[i].y0 >= 0);
        Assert(d.rects[i].x1 <= W && d.rects[i].y1 <= H);
    }
    AssertMatchesDirect(f.fb, render);
}

TEST_CASE("08 UiDirty merges overlapping rects and bounds the count")
{
    UiDirty d;
    d.Add(0, 0, 10, 10);
    d.Add(5, 5, 10, 10);                        // overlaps -> merged
    AssertEqual(int(d.count), 1);
    AssertEqual(int(d.rects[0].x1), 15);

    d.Clear();
    for (int i = 0; i < UiDirty::MaxRects + 2; i++)
        d.Add(i * 20, 0, 10, 5);                // disjoint -> least growth
    Assert(d.count <= UiDirty::MaxRects);
    for (int i = 0; i < UiDirty::MaxRects + 2; i++)
        Assert(d.Intersects(i * 20, 0, 10, 5));
}

}

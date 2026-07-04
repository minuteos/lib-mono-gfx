/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * Area.cpp - Ui layout area stack and alignment
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>
#include <mono-gfx/fonts/Font5x7.h>

namespace
{

int Count(const MonoBuffer& b)
{
    int n = 0;
    for (int y = 0; y < b.Height(); y++)
        for (int x = 0; x < b.Width(); x++)
            if (b.GetPixel(x, y)) n++;
    return n;
}

TEST_CASE("01 Area translates child coordinates to absolute")
{
    uint8_t mem[8 * 32] = {};
    MonoBuffer b(mem, 64, 32);
    Ui ui(b);

    ui.PushArea(10, 8, 20, 16);
    AssertEqual(ui.Width(), 20);
    AssertEqual(ui.Height(), 16);
    ui.Fill(0, 0, 4, 4);            // local (0,0) -> absolute (10,8)
    ui.PopArea();

    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 64; x++)
            AssertEqual(b.GetPixel(x, y), x >= 10 && x < 14 && y >= 8 && y < 12);
}

TEST_CASE("02 Area clips children to its bounds")
{
    uint8_t mem[8 * 32] = {};
    MonoBuffer b(mem, 64, 32);
    Ui ui(b);

    ui.PushArea(10, 8, 20, 16);
    ui.Fill(-100, -100, 400, 400);     // vastly oversized
    ui.PopArea();

    AssertEqual(Count(b), 20 * 16);    // exactly the area, nothing outside
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 64; x++)
            if (b.GetPixel(x, y))
                Assert(x >= 10 && x < 30 && y >= 8 && y < 24);
}

TEST_CASE("03 Nested areas intersect, popping restores the parent")
{
    uint8_t mem[8 * 32] = {};
    MonoBuffer b(mem, 64, 32);
    Ui ui(b);

    ui.PushArea(10, 8, 20, 16);
    ui.PushArea(5, 4, 100, 100);       // child extends past the parent
    ui.Fill(0, 0, 100, 100);
    ui.PopArea();
    // back in the parent: full-area fill must still be bounded by it
    ui.Fill(0, 0, 100, 100);
    ui.PopArea();

    // parent fill covers the whole 20x16 area; the child only added pixels
    // inside it, so the union is just the parent area
    AssertEqual(Count(b), 20 * 16);
}

TEST_CASE("04 Label aligns within the current area")
{
    uint8_t mem[16 * 16] = {};
    MonoBuffer b(mem, 128, 16);

    auto inkLeft = [&]()
    {
        for (int x = 0; x < 128; x++)
            for (int y = 0; y < 16; y++)
                if (b.GetPixel(x, y)) return x;
        return -1;
    };

    // "Hi" advances 12px in Font5x7 (5+1 per glyph); the leftmost ink of
    // the block lands at the aligned origin
    { Ui ui(b); auto a = ui.Area(0, 0, 128, 16); ui.Label(Font5x7, "Hi", Ui::Align::Left); }
    AssertEqual(inkLeft(), 0);                  // origin

    memset(mem, 0, sizeof(mem));
    { Ui ui(b); auto a = ui.Area(0, 0, 128, 16); ui.Label(Font5x7, "Hi", Ui::Align::Right); }
    AssertEqual(inkLeft(), 128 - 12);           // flush right

    memset(mem, 0, sizeof(mem));
    { Ui ui(b); auto a = ui.Area(0, 0, 128, 16); ui.Label(Font5x7, "Hi", Ui::Align::HCenter); }
    AssertEqual(inkLeft(), (128 - 12) / 2);     // centred

    // vertical centring: a 7px-tall line in a 16px area starts at row 4
    memset(mem, 0, sizeof(mem));
    int firstRow = -1;
    { Ui ui(b); auto a = ui.Area(0, 0, 128, 16); ui.Label(Font5x7, "Hi", Ui::Align::VCenter); }
    for (int y = 0; y < 16 && firstRow < 0; y++)
        for (int x = 0; x < 128; x++)
            if (b.GetPixel(x, y)) { firstRow = y; break; }
    AssertEqual(firstRow, (16 - 7) / 2);
}

TEST_CASE("05 Areas compose with the diff (moved area re-dirties)")
{
    uint8_t mem[8 * 32] = {};
    MonoBuffer b(mem, 64, 32);
    UiSlot slots[8];
    UiDiff diff;
    diff.SetStorage(slots, 8);

    int ox = 4;
    auto render = [&](Ui& ui) { ui.PushArea(ox, 4, 10, 8); ui.Fill(0, 0, 6, 4); ui.PopArea(); };

    // frame 1: full paint
    diff.Begin(); { Ui u(b, diff); render(u); } diff.End(64, 32);

    // frame 2: same -> clean
    diff.Begin(); { Ui u(b, diff); render(u); }
    AssertEqual(diff.End(64, 32).IsEmpty(), true);

    // frame 3: shift the area -> both old and new positions dirty
    ox = 20;
    diff.Begin(); { Ui u(b, diff); render(u); }
    auto& d = diff.End(64, 32);
    Assert(d.Intersects(4, 4, 6, 4));       // where it was
    Assert(d.Intersects(20, 4, 6, 4));      // where it is now
}

}

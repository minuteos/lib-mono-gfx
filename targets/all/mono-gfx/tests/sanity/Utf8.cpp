/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * Utf8.cpp - DrawText / MeasureText UTF-8 decoding
 */

#include <testrunner/TestCase.h>

#include <mono-gfx/mono-gfx.h>
#include <mono-gfx/fonts/Font5x7.h>

namespace
{

// solid 3x2 block: continuous bg0 fg6 -> nibbles 0,6 -> byte 0x06
const uint8_t blk[] = { 0x06 };
const uint8_t w1[] = { 3 };
const uint16_t o1[] = { 0 };

// U+00B0 (2-byte UTF-8) and U+2080 (3-byte UTF-8) one-glyph fonts
const Font DegFont = {
    .height = 2, .spacing = 1, .fixedWidth = 0, .missingWidth = 0,
    .firstChar = 0x00B0, .charCount = 1,
    .widths = w1, .offsets = o1, .data = blk, .format = FontFormat::RLE,
};
const Font SubFont = {
    .height = 2, .spacing = 1, .fixedWidth = 0, .missingWidth = 0,
    .firstChar = 0x2080, .charCount = 1,
    .widths = w1, .offsets = o1, .data = blk, .format = FontFormat::RLE,
};

bool Same(const MonoBuffer& a, const MonoBuffer& b)
{
    for (int y = 0; y < a.Height(); y++)
        for (int x = 0; x < a.Width(); x++)
            if (a.GetPixel(x, y) != b.GetPixel(x, y)) return false;
    return true;
}
int Count(const MonoBuffer& b)
{
    int n = 0;
    for (int y = 0; y < b.Height(); y++)
        for (int x = 0; x < b.Width(); x++)
            if (b.GetPixel(x, y)) n++;
    return n;
}

TEST_CASE("01 2-byte UTF-8 decodes to its code point")
{
    uint8_t m1[2 * 4] = {}, m2[2 * 4] = {};
    MonoBuffer a(m1, 16, 4), b(m2, 16, 4);
    a.DrawText(0, 1, DegFont, "\xC2\xB0");          // U+00B0
    b.DrawGlyph(0, 1, DegFont, 0x00B0);
    Assert(Count(a) == 6);
    Assert(Same(a, b));
    AssertEqual(MonoBuffer::MeasureText(DegFont, "\xC2\xB0"), 3 + 1);
}

TEST_CASE("02 3-byte UTF-8 decodes to its code point")
{
    uint8_t m1[2 * 4] = {}, m2[2 * 4] = {};
    MonoBuffer a(m1, 16, 4), b(m2, 16, 4);
    a.DrawText(0, 1, SubFont, "\xE2\x82\x80");      // U+2080
    b.DrawGlyph(0, 1, SubFont, 0x2080);
    Assert(Count(a) == 6);
    Assert(Same(a, b));
}

TEST_CASE("03 ASCII is unaffected by decoding")
{
    uint8_t m1[8 * 8] = {}, m2[8 * 8] = {};
    MonoBuffer a(m1, 40, 8), b(m2, 40, 8);
    int pa = a.DrawText(0, 0, Font5x7, "Hi! 42");
    int pc = 0;
    for (const char* s = "Hi! 42"; *s; s++)
        pc = b.DrawGlyph(pc, 0, Font5x7, (unsigned char)*s);
    AssertEqual(pa, pc);
    Assert(Same(a, b));
    AssertEqual(MonoBuffer::MeasureText(Font5x7, "Hi! 42"), pa);
}

TEST_CASE("04 malformed bytes pass through, no overrun")
{
    uint8_t m1[2 * 4] = {}, m2[2 * 4] = {};
    MonoBuffer a(m1, 16, 4), b(m2, 16, 4);
    // lone continuation/lead byte 0xB0 is not valid UTF-8 -> passes
    // through as code point 0xB0, so it still maps to DegFont's glyph
    a.DrawText(0, 1, DegFont, "\xB0");
    b.DrawGlyph(0, 1, DegFont, 0xB0);
    Assert(Same(a, b));
    Assert(Count(a) == 6);

    // truncated 2-byte sequence (lead with no continuation before NUL)
    uint8_t m3[2 * 4] = {};
    MonoBuffer c(m3, 16, 4);
    int pen = c.DrawText(0, 1, DegFont, "\xC2");     // must not overrun
    AssertEqual(pen, DegFont.missingWidth + DegFont.spacing);  // 0xC2 absent
}

TEST_CASE("05 overlong encoding is rejected")
{
    // 0xC0 0xAF is an overlong '/'; it must NOT decode to U+002F.
    // Both bytes are invalid leads -> pass through as 0xC0, 0xAF, which
    // Font5x7 (0x20-0x7E) lacks, so nothing is drawn; '/' alone draws.
    uint8_t m1[8 * 8] = {}, m2[8 * 8] = {};
    MonoBuffer fb(m1, 40, 8), rf(m2, 40, 8);
    fb.DrawText(0, 0, Font5x7, "\xC0\xAF");
    rf.DrawText(0, 0, Font5x7, "/");
    AssertEqual(Count(fb), 0);
    Assert(Count(rf) > 0);
}

}

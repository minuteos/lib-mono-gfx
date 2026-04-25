# MinuteOS Monochrome Graphics Library

A small, fast graphics library for 1-bit-per-pixel displays.

The library targets the embedded use case: no allocations, no virtuals on
hot paths, framebuffers are passed by value as four-word handles, and the
inner loops process whole bytes wherever the geometry allows.

The output of `MonoBuffer` is laid out in the canonical horizontal
MSB-first 1-bpp planar format also consumed by the [`lib-display`][1]
drivers, so a fully rendered framebuffer can be handed straight to the
display controller, or transformed into vertical-byte / page-mode formats
via `MonoFormat::ToVerticalBytes` for chips like SSD1306.

[1]: https://github.com/minuteos/lib-display

## Components

```
targets/all/mono-gfx/
    DrawOp.h            pixel and blit ops, plus the one-byte helper used everywhere
    MonoBuffer.{h,cpp}  the 1-bpp framebuffer view and all drawing primitives
    MonoFormat.{h,cpp}  conversions to display-specific output formats
    Font.h              bitmap font definition
    fonts/Font5x7.{h,cpp}   built-in 5x7 ASCII font (0x20-0x7E)
    mono-gfx.h          umbrella header
```

To use the library, add `mono-gfx` to your project's `COMPONENTS` and
include `<mono-gfx/mono-gfx.h>`.

## Quick start

```cpp
#include <mono-gfx/mono-gfx.h>
#include <mono-gfx/fonts/Font5x7.h>

uint8_t fb[128 * 64 / 8];
MonoBuffer screen(fb, 128, 64);

screen.Clear();
screen.DrawRoundRect(0, 0, 128, 64, 4);
screen.DrawText(6, 8, Font5x7, "Hello, MinuteOS!");
screen.FillCircle(110, 50, 10);

// hand the framebuffer to your display driver, or convert for SSD1306:
uint8_t page[128 * 8];
MonoFormat::ToVerticalBytes(screen, page);
```

## Storage format

`MonoBuffer` is a non-owning view: it stores a data pointer and three
small ints (width, height, stride). Pixel `(x, y)` is at:

```
byte index   = y * stride + (x >> 3)
bit position = 0x80 >> (x & 7)
```

This is the same format used by `lib-display::ST7306` and the format most
e-paper drivers expect, so the framebuffer can be transferred to the
display with no per-pixel work.

## Drawing operations

Every primitive accepts a `DrawOp`:

- `Set` — turn pixels on
- `Clear` — turn pixels off
- `Invert` — toggle pixels
- `Keep` — no-op (skip drawing)

Blits accept a `BlitOp` that distinguishes between "treat the source as a
mask" operators (`Or`, `AndNot`, `Xor`) and "treat the source as a full
plane" operators (`Copy`, `CopyNot`, `And`).

## Fonts

A `Font` is a flat struct holding glyph dimensions and a bitmap blob, in
either fixed-width or variable-width form. New fonts can be defined as
plain `Font` constants without touching the library code; the supplied
`Font5x7` is one such definition.

## License

Licensed under the [MIT License](./LICENSE.txt).

Copyright (c) 2026 triaxis s.r.o.

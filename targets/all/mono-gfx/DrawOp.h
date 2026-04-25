/*
 * Copyright (c) 2026 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * mono-gfx/DrawOp.h
 *
 * Pixel and blit operations for the monochrome graphics component
 */

#pragma once

#include <base/base.h>

//! Operation applied to destination pixels by primitive drawing functions
/*!
 * The two least significant bits of every value encode a 2-bit truth table
 * keyed by the destination pixel: bit 0 selects the new value when the
 * destination is currently 0, bit 1 selects it when the destination is 1.
 *
 * This makes it possible to evaluate any operation in a single bitwise
 * expression: @code dst = (op & 1) | (dst & ((op >> 1) ^ (op & 1))) @endcode
 * but in practice the four cases are dispatched to specialized loops.
 */
enum struct DrawOp : uint8_t
{
    Clear = 0b00,    //!< force pixels to 0
    Set = 0b11,      //!< force pixels to 1
    Invert = 0b10,   //!< toggle pixels (XOR with 1)
    Keep = 0b01,     //!< leave destination unchanged (no-op)
};

//! Applies a @ref DrawOp to a single byte under a precomputed mask
ALWAYS_INLINE static void ApplyDrawOp(uint8_t& byte, uint8_t mask, DrawOp op)
{
    switch (op)
    {
        case DrawOp::Clear:  byte &= ~mask; break;
        case DrawOp::Set:    byte |= mask; break;
        case DrawOp::Invert: byte ^= mask; break;
        case DrawOp::Keep:   break;
    }
}

//! Operation applied when blitting one buffer onto another
/*!
 * @c Or, @c AndNot and @c Xor treat the source as a foreground mask: only
 * destination pixels under set source pixels are modified, which is what
 * you want when stamping glyphs or sprites. @c Copy, @c And and @c CopyNot
 * affect every destination pixel inside the blit rectangle and are useful
 * when transferring full plane images.
 */
enum struct BlitOp : uint8_t
{
    //! Destination pixels are overwritten with source pixels (full plane)
    Copy,
    //! Destination pixels are overwritten with the inverse of the source (full plane)
    CopyNot,
    //! Set destination pixels under set source pixels: dst |= src
    Or,
    //! Clear destination pixels under set source pixels: dst &= ~src
    AndNot,
    //! Toggle destination pixels under set source pixels: dst ^= src
    Xor,
    //! AND with source: dst &= src (zeroes in source clear destination)
    And,
};

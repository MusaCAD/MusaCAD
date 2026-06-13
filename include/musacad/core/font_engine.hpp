// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "musacad/core/math/vec2.hpp"

namespace musacad::core {

/// The seam behind which TrueType/OpenType glyph geometry is produced, keeping the
/// core Qt-free (mirrors IGeometryKernel). The concrete implementation (QtFontEngine,
/// in the UI layer) uses Qt's font machinery to enumerate faces and extract glyph
/// outlines, triangulated to filled triangles for the Phase-14 fill pipeline. The
/// snapshot/bounds/pick consult this interface; a null engine (tests / headless) means
/// "stroke font only" -- every text falls back to the built-in stroke font.
///
/// Text geometry is GENERATED here at snapshot time from (name, string, height), never
/// baked into the store -- the same derived-not-baked rule as dimensions and blocks.
class IFontEngine {
public:
    virtual ~IFontEngine() = default;

    IFontEngine() = default;
    IFontEngine(const IFontEngine&) = default;
    IFontEngine(IFontEngine&&) = default;
    IFontEngine& operator=(const IFontEngine&) = default;
    IFontEngine& operator=(IFontEngine&&) = default;

    /// True if `name` resolves to a renderable outline (TTF/OTF) face. False (incl. the
    /// empty/"Standard" name) means the caller renders with the built-in stroke font.
    [[nodiscard]] virtual bool is_outline_font(std::string_view name) const = 0;

    /// Advance width of `text` at cap `height` in the named face (world units). Used for
    /// justification offsets, MTEXT wrapping, and text bounds -- the SAME metric the
    /// rendered glyphs use, so layout and geometry never diverge.
    [[nodiscard]] virtual double advance(std::string_view name, std::string_view text,
                                         double height) const = 0;

    /// Appends world-space FILLED triangles (3 Vec2 each) for `text` laid out from
    /// baseline `origin`, at cap `height`, rotated `rotation` radians CCW, left-origin
    /// (the caller applies any justification/attachment offset to `origin`).
    virtual void glyph_fills(std::string_view name, std::string_view text, Vec2 origin,
                             double height, double rotation, std::vector<Vec2>& tris) const = 0;

    /// The selectable font names for the Properties palette (the built-in "Standard"
    /// stroke font plus the available system outline faces).
    [[nodiscard]] virtual std::vector<std::string> available() const = 0;

    /// Resolve a requested font reference (an SHX file name like "romans.shx", or a TTF
    /// family name) to a concrete available face, via the substitution table -- or "" if
    /// nothing matches (the caller then uses the stroke font). Used on import.
    [[nodiscard]] virtual std::string substitute(std::string_view requested) const = 0;
};

} // namespace musacad::core

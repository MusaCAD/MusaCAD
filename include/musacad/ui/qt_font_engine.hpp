// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <QFont>

#include "musacad/core/font_engine.hpp"
#include "musacad/core/math/vec2.hpp"

namespace musacad::ui {

/// Qt-backed implementation of the core IFontEngine seam. Enumerates the system's
/// installed TrueType/OpenType faces, extracts glyph outlines via QRawFont/QPainterPath,
/// and triangulates them to filled triangles for the Phase-14 fill pipeline. Glyph
/// triangulations are cached per (face, codepoint) at unit em (cap height = 1), so the
/// hot path scales/rotates cached triangles -- the snapshot stays cheap and the only Qt
/// work is a one-time outline extraction per glyph.
///
/// Threading: font ENUMERATION happens once, on the UI thread, in the constructor.
/// Glyph OUTLINE extraction happens lazily on a cache miss (driven by the geometry
/// thread during snapshot build); QRawFont/QPainterPath read immutable font data and are
/// safe off the GUI thread. All cache access is mutex-guarded.
class QtFontEngine final : public core::IFontEngine {
public:
    QtFontEngine();

    [[nodiscard]] bool is_outline_font(std::string_view name) const override;
    [[nodiscard]] double advance(std::string_view name, std::string_view text,
                                 double height) const override;
    void glyph_fills(std::string_view name, std::string_view text, core::Vec2 origin, double height,
                     double rotation, std::vector<core::Vec2>& tris) const override;
    [[nodiscard]] std::vector<std::string> available() const override;
    [[nodiscard]] std::string substitute(std::string_view requested) const override;

private:
    // A glyph at unit em: triangles (3 Vec2 each, y-up, baseline at y=0) + advance, all
    // divided by the face's cap height so a draw at cap `height` is a plain scale.
    struct Glyph {
        std::vector<core::Vec2> tris;
        double advance = 1.0;
    };
    struct Face {
        QFont font;
        double cap = 1.0; // cap height in the QFont's pixel size
        std::map<char32_t, Glyph> cache;
    };

    Face* face_for(std::string_view name) const; // resolve a registered name to a Face
    const Glyph& glyph_for(Face& f, char32_t cp) const;

    mutable std::mutex mu_;
    mutable std::map<std::string, Face, std::less<>> faces_; // lazily populated, by family
    std::vector<std::string> available_;                    // "Standard" + system families
    std::map<std::string, std::string, std::less<>> subst_; // requested name -> family (lowercased keys)
};

} // namespace musacad::ui

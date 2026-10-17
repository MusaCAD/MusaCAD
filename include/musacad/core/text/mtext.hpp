// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "musacad/core/math/math.hpp"
#include "musacad/core/mtext_block.hpp"
#include "musacad/core/properties.hpp"

namespace musacad::core {
class IFontEngine;
}

namespace musacad::core::text {

/// The computed layout of a paragraph (MTEXT / QLEADER label): the world-space glyph
/// geometry and the block's axis-aligned bounding box (for pick, bounds, and the width
/// grip). The stroke font fills `segments` (line pairs); an outline (TTF) font fills
/// `fills` (triangles). Computed entirely from the MTextBlock + content -- nothing baked.
/// One laid-out line: its text and where its baseline starts (world), left-justified
/// at the block's rotation. What EXPLODE turns into a TEXT entity per line.
struct MTextLine {
    std::string text;
    Vec2 origin{};
};

/// A run of the text in a colour of its own (an MTEXT format code, \C or \c): the range
/// of `segments` and of `fills` its glyphs (and its underline) take.
struct MTextColorSpan {
    std::size_t seg_begin = 0;
    std::size_t seg_end = 0;
    std::size_t fill_begin = 0;
    std::size_t fill_end = 0;
    Rgb color{};
};

struct MTextLayout {
    std::vector<Vec2> segments; ///< 2 Vec2 per glyph stroke (stroke font)
    std::vector<Vec2> fills;    ///< 3 Vec2 per glyph triangle (outline font)
    Vec2 min{};                 ///< block AABB (world)
    Vec2 max{};
    int line_count = 0;
    std::vector<MTextLine> lines; ///< per line, in reading order
    std::vector<MTextColorSpan> colors; ///< in order; what no span covers takes the text's colour
};

/// Whether `content` carries AutoCAD's MTEXT format codes (\H, \C, \f, \L, \S, \P ...);
/// one that does not is laid out as plain text, its braces and all.
[[nodiscard]] bool has_mtext_codes(std::string_view content);

/// The text `content` shows: its format codes taken out, paragraphs as '\n', a stack as
/// "top/bottom". The %% codes and \U+ escapes stay as they are (they expand when drawn).
/// Content has_mtext_codes does not take for formatted comes back as it is, unless
/// `all_codes` (then \~, \\, \{ \}, { } and lone toggles are read as codes too).
[[nodiscard]] std::string mtext_plain(std::string_view content, bool all_codes = false);

/// Lays out `content` within `block`: splits on '\n', word-wraps each paragraph to
/// `block.width` (0 = no wrap), stacks lines by `line_spacing`, anchors by `attach`
/// (TL..BR), applies `rotation`. The ONE layout path (snapshot, bounds, placement
/// preview, grips). When `fonts` resolves `font_name` to an outline face, wrapping/metrics
/// use the face's advances and glyphs go to `fills`; otherwise the stroke font is used and
/// glyphs go to `segments`. A null engine / unknown name => stroke font.
[[nodiscard]] MTextLayout layout_mtext(const MTextBlock& block, std::string_view content,
                                       const IFontEngine* fonts = nullptr,
                                       std::string_view font_name = {});

/// World advance width of single-line `text` at cap `height` in the named font: the
/// outline face's advance when `fonts` resolves `font_name`, else the stroke-font width.
/// The ONE width metric shared by single-line bounds and pick (no fork with rendering).
[[nodiscard]] double text_advance(const IFontEngine* fonts, std::string_view font_name,
                                  std::string_view text, double height);

} // namespace musacad::core::text

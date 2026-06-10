#pragma once

#include <string_view>
#include <vector>

#include "musacad/core/math/math.hpp"
#include "musacad/core/mtext_block.hpp"

namespace musacad::core {
class IFontEngine;
}

namespace musacad::core::text {

/// The computed layout of a paragraph (MTEXT / QLEADER label): the world-space glyph
/// geometry and the block's axis-aligned bounding box (for pick, bounds, and the width
/// grip). The stroke font fills `segments` (line pairs); an outline (TTF) font fills
/// `fills` (triangles). Computed entirely from the MTextBlock + content -- nothing baked.
struct MTextLayout {
    std::vector<Vec2> segments; ///< 2 Vec2 per glyph stroke (stroke font)
    std::vector<Vec2> fills;    ///< 3 Vec2 per glyph triangle (outline font)
    Vec2 min{};                 ///< block AABB (world)
    Vec2 max{};
    int line_count = 0;
};

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

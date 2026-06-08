#pragma once

#include <string_view>
#include <vector>

#include "musacad/core/math/math.hpp"
#include "musacad/core/mtext_block.hpp"

namespace musacad::core::text {

/// The computed layout of a paragraph (MTEXT / QLEADER label): the world-space
/// line segments for all glyphs, and the block's axis-aligned bounding box (for
/// pick, bounds, and the width grip). Computed entirely from the MTextBlock fields
/// + content -- nothing here is stored on the entity.
struct MTextLayout {
    std::vector<Vec2> segments; ///< 2 Vec2 per glyph stroke
    Vec2 min{};                 ///< block AABB (world)
    Vec2 max{};
    int line_count = 0;
};

/// Lays out `content` within `block`: splits on '\n', word-wraps each paragraph to
/// `block.width` (0 = no wrap) using the stroke font metrics scaled by
/// `width_factor`, stacks lines by `line_spacing`, anchors by `attach`
/// (TL..BR), applies `rotation`. The single layout path used by the snapshot,
/// the bounds, the placement preview, and the grips.
[[nodiscard]] MTextLayout layout_mtext(const MTextBlock& block, std::string_view content);

} // namespace musacad::core::text

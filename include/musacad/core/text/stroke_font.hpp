#pragma once

#include <string_view>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::core::text {

/// Horizontal justification of a text run relative to its insertion point.
enum class Justify { Left, Center, Right };

/// A single-stroke (vector) font. Chosen over a glyph atlas/SDF because text in a
/// vector CAD viewport must stay crisp at every zoom and batches naturally with
/// the existing line-segment pipeline -- no texture backend. Covers ASCII
/// 0x20-0x7E plus the CAD symbols U+00B0 (degree), U+00B1 (plus-minus), U+2300
/// (diameter). Lowercase a-z render as small capitals (a documented CAD-font
/// simplification).
///
/// `append_text_segments` emits world-space line segments (two Vec2 per segment)
/// for `text`: glyphs sit on a baseline through `origin`, `height` tall, rotated
/// `rotation` radians CCW about `origin`, shifted for `justify`. Screen-space text
/// (UI labels) is just rotation 0 with a y-down caller convention.
void append_text_segments(std::string_view text, Vec2 origin, double height, double rotation,
                          Justify justify, std::vector<Vec2>& out);

/// Total advance width of `text` at `height` (world units). Used for justification
/// and pick bounds.
[[nodiscard]] double text_width(std::string_view text, double height);

} // namespace musacad::core::text

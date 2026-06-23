// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "musacad/core/hatch.hpp" // Segment
#include "musacad/core/math/math.hpp"

namespace musacad::core::hatch {

/// One line family of a hatch pattern, in AutoCAD .PAT terms:
///   angle, x-origin, y-origin, delta-x, delta-y [, dash, gap, ...]
/// `delta.x` shifts the dash origin ALONG the line for each successive parallel line (a
/// stagger); `delta.y` is the perpendicular spacing between the parallel lines. `dashes`
/// is the dash specification (positive = pen-down, negative = pen-up/gap, 0 = dot); empty
/// means a solid (continuous) line family. All lengths are in pattern space (scaled by the
/// hatch's pattern_scale at render time).
struct PatternLine {
    double angle = 0.0; // degrees, in pattern space
    Vec2 origin{};
    Vec2 delta{};
    std::vector<double> dashes;
};

/// A named hatch pattern = one or more line families. SOLID is NOT represented here (it is
/// the special pattern name handled by the fill pipeline); every other pattern renders as
/// clipped line families.
struct Pattern {
    std::string name;
    std::vector<PatternLine> lines;
};

/// Parse AutoCAD .PAT text (one or more `*NAME, description` definitions, each followed by
/// its family lines). Blank lines and `;`-comments are ignored. Tolerant of extra spaces.
/// Used both for the built-in library and for user-loaded .PAT files.
[[nodiscard]] std::vector<Pattern> parse_pat(std::string_view text);

/// Look up a built-in stock pattern by name (case-insensitive). Returns nullptr for an
/// unknown name (and for "SOLID", which is not a line pattern). The library is authored
/// from the public .PAT format -- it does NOT copy Autodesk's acad.pat; load that file with
/// parse_pat() for the full vendor set.
[[nodiscard]] const Pattern* builtin_pattern(std::string_view name);

/// Names of all built-in patterns (for listing / UI), excluding SOLID.
[[nodiscard]] const std::vector<std::string>& builtin_pattern_names();

/// The full set of selectable pattern names for the Pattern dropdown: "SOLID" first, then
/// every built-in line pattern (sorted). Source of truth for the PR Pattern combo.
[[nodiscard]] const std::vector<std::string>& pattern_choice_list();

/// Generate the pattern's line segments clipped to the filled region of `loops` (loop 0 =
/// outer, the rest islands/holes; even-odd, so islands carve out), with the given
/// pattern_scale, pattern_angle (RADIANS, added to each family angle) and pattern_origin.
/// Derived-not-baked: appended to `out` at render/plot time, never stored.
void generate_pattern_segments(const std::vector<std::vector<Vec2>>& loops, const Pattern& pat,
                               double scale, double angle, Vec2 origin, std::vector<Segment>& out);

} // namespace musacad::core::hatch

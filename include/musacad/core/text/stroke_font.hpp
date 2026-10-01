// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cmath>
#include <string_view>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::core::text {

/// Horizontal justification of a text run relative to its insertion point.
enum class Justify { Left, Center, Right };

/// A single-stroke (vector) font. Chosen over a glyph atlas/SDF because text in a
/// vector CAD viewport must stay crisp at every zoom and batches naturally with
/// the existing line-segment pipeline -- no texture backend. Letterforms are
/// simplex/Hershey-class (engineering single-stroke); lowercase a-z are real glyphs
/// with true ascenders, x-height and descenders (small capitals remain only as a
/// defensive fallback for a missing glyph). The font is monospace: every glyph
/// shares one advance, including every symbol below -- so adding glyphs can never
/// change text_width, layout, bounds or pick.
///
/// Coverage:
/// * ASCII 0x20-0x7E.
/// * Engineering symbols: U+00B0 degree, U+00B1 plus-minus, U+2300 diameter.
/// * Typographic and units: U+2014 em dash, U+00D7 multiplication sign, U+03A9 omega
///   (U+2126 ohm sign, the same glyph), U+00B5 micro sign (U+03BC mu, the same glyph).
/// * Hole callouts: U+2334 counterbore/spotface, U+21A7 depth, U+2335 countersink.
/// * GD&T characteristics: U+23E4 straightness, U+23E5 flatness, U+25CB circularity,
///   U+232D cylindricity, U+2312 profile of a line, U+2313 profile of a surface,
///   U+2220 angularity, U+27C2 perpendicularity, U+2225 parallelism, U+2316 position,
///   U+25CE concentricity, U+232F symmetry, U+2197 circular runout, U+2330 total runout.
/// * Material condition / modifiers: U+24C2 MMC, U+24C1 LMC, U+24C8 RFS, U+24C5
///   projected zone, U+24BB free state, U+24C9 tangent plane, U+24CA unequally disposed.
/// * Feature form: U+25A1 square, U+2332 conical taper, U+2333 slope.
///
/// Reachable from any text entity through the `%%` codes and the `\U+XXXX` escape
/// (see text_codes.hpp), so they work uniformly in TEXT, MTEXT, LEADER and dimension
/// text. An unmapped code point renders blank (advance only) -- never a wrong glyph.
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

/// True when the font draws `cp` (a space, a tab or a line break counts: there is nothing
/// to draw). False for a code point it has no glyph for, which it draws blank -- what
/// `musacad_app --check` reports as a missing character.
[[nodiscard]] bool has_glyph(char32_t cp);

/// Applies a text STYLE's width factor and obliquing angle to already laid-out world
/// points: in the text's own frame (un-rotated about `anchor`, the justification
/// point) x is scaled by `width_factor` and sheared by y * tan(oblique), then the
/// frame is re-rotated. Scaling about the anchor keeps every justification put.
/// Shared by drawing, picking and bounds so they can never disagree.
inline void apply_text_style(std::vector<Vec2>& pts, Vec2 anchor, double rotation,
                             double width_factor, double oblique) {
    if (width_factor == 1.0 && oblique == 0.0) {
        return;
    }
    const double cs = std::cos(rotation);
    const double sn = std::sin(rotation);
    const double shear = std::tan(oblique);
    for (Vec2& p : pts) {
        const Vec2 d = p - anchor;
        const double lx = d.x * cs + d.y * sn;
        const double ly = -d.x * sn + d.y * cs;
        const double nx = lx * width_factor + ly * shear;
        p = {anchor.x + nx * cs - ly * sn, anchor.y + nx * sn + ly * cs};
    }
}

/// The four corners of a text's box in its own frame (baseline-left at the origin),
/// after the style's width factor and shear: (0,0), (w*wf,0), (w*wf + h*tan, h), (h*tan, h).
inline void text_box_corners(double advance, double height, double width_factor, double oblique,
                             Vec2 (&out)[4]) {
    const double w = advance * width_factor;
    const double t = std::tan(oblique) * height;
    out[0] = {0.0, 0.0};
    out[1] = {w, 0.0};
    out[2] = {w + t, height};
    out[3] = {t, height};
}

} // namespace musacad::core::text

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <array>
#include <span>
#include <vector>

#include "musacad/core/math/math.hpp"
#include "musacad/core/properties.hpp"

// Linetype dash-pattern rendering. Dashing is DERIVED at snapshot time from the
// stored linetype + the global LTSCALE -- nothing is baked into storage (same
// parametric rule as tessellation). A single walker dashes any polyline by
// arc-length, so a straight line (2 points) and a tessellated curve (N points)
// share one path and the pattern phase carries across vertices (no restart /
// bunching at tessellation joins).

namespace musacad::core {

/// The base dash pattern for a linetype, in drawing units: even indices are "on"
/// (dash) lengths, odd indices are "off" (gap) lengths. Empty span => Continuous
/// (no dashing). Multiply by LTSCALE before use. Proportions follow AutoCAD's
/// acad.lin (x10 so they read at LTSCALE 1 in a tens-of-units drawing):
///   Dashed  = [5, 2.5]                 (acad .5,-.25)
///   Hidden  = [2.5, 1.25]              (acad .25,-.125)
///   Center  = [12.5, 2.5, 2.5, 2.5]    (acad 1.25,-.25,.25,-.25: long,gap,dot,gap)
[[nodiscard]] std::span<const double> dash_pattern(Linetype lt) noexcept;

/// Append dash sub-segment endpoint pairs (a0,b0, a1,b1, ...) to `out`, walking the
/// polyline `pts` by arc-length with `lt`'s pattern scaled by `ltscale`. Continuous
/// (or ltscale <= 0, or a <2-point input) copies every segment unchanged. The phase
/// starts pen-down at the first point and carries continuously across all segments.
void dash_polyline(std::span<const Vec2> pts, Linetype lt, double ltscale,
                   std::vector<Vec2>& out);

/// The same for any linetype: a built-in one as above, a loaded one (`lt` from
/// kFirstLoadedLinetype on) by its definition in `loaded`, the drawing's table -- its
/// .lin pattern walked as written (dashes, gaps, and dots drawn as short dashes). A loaded
/// value with no definition draws solid.
void dash_polyline(std::span<const Vec2> pts, Linetype lt, std::span<const LinetypeDef> loaded, double ltscale,
                   std::vector<Vec2>& out);

/// The built-in linetypes' names as the Linetype Manager shows them (Continuous, Dashed,
/// Center, Hidden), and as DXF writes them (Continuous, DASHED, CENTER, HIDDEN).
[[nodiscard]] const char* builtin_linetype_name(Linetype lt, bool dxf = false) noexcept;

/// The linetypes of a .lin file: each `*NAME,description` with its `A,` pattern line. Shape
/// and text elements ([...]) are left out, the gaps around them kept.
[[nodiscard]] std::vector<LinetypeDef> parse_lin(std::string_view text);

/// The built-in Dashed, Center and Hidden as a drawing defines them, in .lin form (a dash
/// positive, a gap negative): [0] Dashed, [1] Center, [2] Hidden. A drawing keeps its own, as
/// AutoCAD keeps a loaded linetype's definition in the drawing.
using BuiltinLinetypes = std::array<std::vector<double>, 3>;

/// The built-in three as Musa CAD defined them before a drawing kept its own (acad.lin x10,
/// the sizes dash_pattern() gives) -- what a drawing saved then still draws with.
[[nodiscard]] const BuiltinLinetypes& legacy_builtin_linetypes();

/// The built-in three as AutoCAD loads them: from acad.lin (inches, MEASUREMENT 0) or, with
/// `metric`, from acadiso.lin (millimetres, MEASUREMENT 1: the same 25.4 times as long).
[[nodiscard]] const BuiltinLinetypes& standard_builtin_linetypes(bool metric);

/// dash_polyline with the drawing's own built-in definitions: Dashed, Center and Hidden by
/// `builtin`, a loaded linetype by `loaded`.
void dash_polyline(std::span<const Vec2> pts, Linetype lt, const BuiltinLinetypes& builtin,
                   std::span<const LinetypeDef> loaded, double ltscale, std::vector<Vec2>& out);

/// The linetypes -LINETYPE Load and the Linetype Manager offer when no .lin file is given, by
/// AutoCAD's names and patterns: BORDER, CENTER, DASHDOT, DASHED, DIVIDE, DOT, HIDDEN and
/// PHANTOM, each with its 2 and X2 sizes, as acad.lin has them (inches) or, `metric`, as
/// acadiso.lin has them (millimetres, 25.4 times as long), then the ISO linetypes
/// ACAD_ISO02W100 to ACAD_ISO15W100 (millimetres in both).
[[nodiscard]] const std::vector<LinetypeDef>& linetype_library(bool metric);

} // namespace musacad::core

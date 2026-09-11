// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <array>
#include <span>
#include <vector>
#include <string>
#include <string_view>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/math/math.hpp"

// Raster IMAGE placement geometry. Like every other entity, an image stores only its
// parameters and its drawable quad is DERIVED here -- one function, feeding the
// snapshot, entity bounds, the pick path, grips and the plot route, so the displayed,
// picked and bounded geometry cannot diverge.

namespace musacad::core {

/// The four world-space corners of a placed image, counter-clockwise from the
/// insertion point: bottom-left, bottom-right, top-right, top-left.
using ImageQuad = std::array<Vec2, 4>;

/// THE geometry definition of a placed image: the clipped quad in world space,
/// honouring position, size and rotation. When the entity is clipped the quad covers
/// only the visible sub-rectangle, so pick, bounds and the plot all agree that the
/// clipped-away part is not there.
[[nodiscard]] ImageQuad resolve_image_quad(const ImageData& img);

/// The image-space UV rectangle the quad maps to, as (u0, v0, u1, v1) fractions with v
/// measured DOWN from the top-left (the orientation decoders hand us). Unclipped it is
/// the whole image. Returned alongside the quad so a renderer/plotter samples exactly
/// the region the quad covers.
[[nodiscard]] std::array<double, 4> resolve_image_uv(const ImageData& img);

/// True when `p` is inside the placed (clipped) quad -- the pick test.
[[nodiscard]] bool point_in_image(const ImageData& img, Vec2 p);
/// A polygon in image fractions (u right, v down from the top-left) placed in the world
/// through the image's transform.
[[nodiscard]] std::vector<Vec2> image_uv_to_world(const ImageData& img, std::span<const Vec2> uv);
/// Point-in-polygon (even-odd) for a world polygon.
[[nodiscard]] bool point_in_polygon(std::span<const Vec2> poly, Vec2 p);

/// Resolve an image definition's external `source` against the directory of the drawing
/// that referenced it, refusing anything that escapes that directory.
///
/// This is a SECURITY boundary, not a convenience: a `.musa` is a document that can be
/// mailed around, and a relative source of `../../../../etc/passwd` must not be read
/// just because a drawing asked for it. Returns false (and leaves `out` untouched) for
/// any path that traverses outside `drawing_dir`, and for absolute paths.
[[nodiscard]] bool resolve_image_path(std::string_view drawing_dir, std::string_view source,
                                      std::string& out);

} // namespace musacad::core

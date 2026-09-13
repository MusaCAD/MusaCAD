// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "musacad/core/math/vec2.hpp"

namespace musacad::core {

/// A tile of the model-space window (VPORTS, "tiled viewports"): its rectangle in window
/// fractions (0..1, x to the right, y UP -- the DXF VPORT table's convention) and the
/// view it shows. `height` is the visible height in drawing units (resolution-free, as
/// DXF code 40); 0 means "frame the drawing's extents" when the tile first appears.
struct TiledViewport {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 1.0;
    double y1 = 1.0;
    Vec2 center{};
    double height = 0.0;
    friend bool operator==(const TiledViewport&, const TiledViewport&) = default;
};

/// A named viewport configuration (VPORTS Save / Restore).
struct VportConfig {
    std::string name;
    std::vector<TiledViewport> tiles;
    friend bool operator==(const VportConfig&, const VportConfig&) = default;
};

/// The standard configurations, as AutoCAD names them. `kind` is one of: "single",
/// "2v" / "2h" (two side by side / stacked), "3r" / "3l" / "3a" / "3b" (a large viewport
/// on the right / left / above / below two small ones), "3v" / "3h" (three columns /
/// rows), "4" (four equal). Every tile shows `within`'s view. Unknown kinds give `within`
/// back unchanged.
[[nodiscard]] std::vector<TiledViewport> split_vport(const TiledViewport& within,
                                                     std::string_view kind);

/// VPORTS Join: merge `other` into `dominant` when they share a full edge (the union is
/// a rectangle); the result keeps the dominant's view. False when they cannot be joined.
[[nodiscard]] bool join_vports(std::vector<TiledViewport>& tiles, std::size_t dominant,
                               std::size_t other);

} // namespace musacad::core

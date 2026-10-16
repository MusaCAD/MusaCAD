// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
#pragma once

#include <cstdint>

namespace musacad::core {

/// GRID's settings: the spacing (GRIDUNIT; 0 = the grid follows the zoom in powers of
/// ten, Musa CAD's own default), a major line every `major` lines (GRIDMAJOR), and
/// GRIDDISPLAY's adaptive behaviour (fewer lines zoomed out; more, between them, zoomed
/// in when `subdivide`) and whether the grid shows beyond the drawing limits.
struct GridSettings {
    double x = 0.0;
    double y = 0.0;
    int major = 5;
    bool adaptive = true;
    bool subdivide = true;
    bool beyond_limits = true;
    /// Isometric drafting (SNAP Style Isometric, ISODRAFT; SNAPSTYL 1): the snap, the grid,
    /// ORTHO and the crosshair follow the isometric axes of `isoplane` -- 0 Left (90 and
    /// 150 degrees), 1 Top (30 and 150), 2 Right (90 and 30); SNAPISOPAIR.
    bool isometric = false;
    std::uint8_t isoplane = 0;
    friend bool operator==(const GridSettings&, const GridSettings&) = default;
};

} // namespace musacad::core

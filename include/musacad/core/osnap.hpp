// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "musacad/core/snap.hpp"

namespace musacad::core {

class GeometryStore;
class IGeometryKernel;
class SpatialGrid;

/// An end of a line or an arc the cursor passed over ("acquired", as AutoCAD says): the
/// Extension snap finds points along the line carried on past it, or round the arc's
/// circle beyond it.
struct ExtensionPath {
    Vec2 from{};         ///< the acquired end
    bool arc = false;
    Vec2 dir{};          ///< a line: the unit direction pointing away from the object
    Vec2 center{};       ///< an arc: its circle ...
    double radius = 0.0;
    double start = 0.0;  ///< ... and the range it is drawn over (start angle, CCW sweep)
    double sweep = 0.0;
};

/// The end of a line, an arc or an open polyline's end segment within `radius_world` of
/// the cursor, as the path its extension runs along.
std::optional<ExtensionPath> extension_path_at(const GeometryStore& store, const SpatialGrid& grid,
                                               Vec2 cursor, double radius_world);

/// Computes the best object-snap near `cursor` within `radius_world`, using the
/// spatial index to narrow candidates and the kernel for exact geometry. Runs on
/// the geometry thread. `enabled_types` is a mask of snap_bit(SnapType). Higher-
/// priority categories (lower SnapType value) win within the aperture.
/// `from_point`, when present, is the active command's previous input point; it
/// enables the deferred snaps (Perpendicular, Tangent). `paths` are the acquired ends
/// the Extension snap may run along.
SnapResult compute_snap(const GeometryStore& store, const IGeometryKernel& kernel,
                        const SpatialGrid& grid, Vec2 cursor, double radius_world,
                        std::uint32_t enabled_types = kAllSnaps,
                        std::optional<Vec2> from_point = std::nullopt,
                        std::span<const ExtensionPath> paths = {});

} // namespace musacad::core

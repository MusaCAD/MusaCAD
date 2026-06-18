// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <optional>

#include "musacad/core/snap.hpp"

namespace musacad::core {

class GeometryStore;
class IGeometryKernel;
class SpatialGrid;

/// Computes the best object-snap near `cursor` within `radius_world`, using the
/// spatial index to narrow candidates and the kernel for exact geometry. Runs on
/// the geometry thread. `enabled_types` is a mask of snap_bit(SnapType). Higher-
/// priority categories (lower SnapType value) win within the aperture.
/// `from_point`, when present, is the active command's previous input point; it
/// enables the deferred snaps (Perpendicular, Tangent).
SnapResult compute_snap(const GeometryStore& store, const IGeometryKernel& kernel,
                        const SpatialGrid& grid, Vec2 cursor, double radius_world,
                        std::uint32_t enabled_types = kAllSnaps,
                        std::optional<Vec2> from_point = std::nullopt);

} // namespace musacad::core

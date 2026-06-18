// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include "musacad/core/entity_handle.hpp"
#include "musacad/core/math/math.hpp"

namespace musacad::core {

class GeometryStore;

/// Computes the world-space AABB of a live entity. Returns false for an invalid
/// handle. Used by the spatial index and ZOOM extents.
bool entity_aabb(const GeometryStore& store, EntityHandle handle, Vec2& out_min, Vec2& out_max);

} // namespace musacad::core

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <vector>

#include "musacad/core/command.hpp"
#include "musacad/core/entity_handle.hpp"
#include "musacad/core/math/math.hpp"

namespace musacad::core {

class GeometryStore;

/// Narrow geometry-operations interface. Every input and output uses Musa CAD's
/// own types only (Vec2/Vec3, EntityHandle, GeometryStore) -- no external
/// library type ever appears here. This is the seam behind which a future
/// NativeKernel3D or optional B-rep backend could be added without touching the
/// command/render/UI layers.
///
/// The surface is deliberately minimal: it contains only operations with a
/// concrete caller in the current roadmap (tessellation for the renderer and
/// command echo; closest-point and intersection for OSNAP). No speculative
/// methods (offset, booleans, parametric eval) are reserved here.
class IGeometryKernel {
public:
    virtual ~IGeometryKernel() = default;

    IGeometryKernel() = default;
    IGeometryKernel(const IGeometryKernel&) = default;
    IGeometryKernel(IGeometryKernel&&) = default;
    IGeometryKernel& operator=(const IGeometryKernel&) = default;
    IGeometryKernel& operator=(IGeometryKernel&&) = default;

    /// Approximates `entity` as a world-space polyline within `tolerance`
    /// (max chord deviation). Clears and fills `out`. Closed primitives
    /// (circle, closed polyline) repeat the first point as the last so the
    /// result forms a closed loop. Used by the renderer (Phase 3) and the
    /// command-line echo (Phase 4).
    virtual void tessellate(const GeometryStore& store, EntityHandle entity, double tolerance,
                            std::vector<Vec2>& out) const = 0;

    /// Finds the closest point on `entity` to `query`. Returns false (and
    /// leaves `out_point` untouched) if the handle is invalid. Used by the
    /// OSNAP "nearest" mode (Phase 5).
    virtual bool closest_point(const GeometryStore& store, EntityHandle entity, Vec2 query,
                               Vec2& out_point) const = 0;

    /// Appends intersection points between entities `a` and `b` to `out`
    /// (existing contents are preserved). Used by the OSNAP "intersection"
    /// mode (Phase 5).
    virtual void intersect(const GeometryStore& store, EntityHandle a, EntityHandle b,
                           std::vector<Vec2>& out) const = 0;

    /// Produces an entity offset from `entity` by `distance`, on the side of the
    /// `side` point. Writes an Add* command describing the new entity into `out`
    /// and returns true on success (false if the entity can't be offset, e.g. a
    /// circle whose radius would go non-positive). The OFFSET command (Phase 7)
    /// is the first and only caller -- added per the minimal-interface rule.
    virtual bool offset(const GeometryStore& store, EntityHandle entity, double distance, Vec2 side,
                        Command& out) const = 0;
};

} // namespace musacad::core

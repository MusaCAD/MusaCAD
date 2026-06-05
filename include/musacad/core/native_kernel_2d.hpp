#pragma once

#include <vector>

#include "musacad/core/geometry_kernel.hpp"

namespace musacad::core {

/// The default, fully-functional 2D geometry kernel. Operates directly on the
/// cache-friendly GeometryStore and covers every 2D primitive (point, line,
/// polyline, circle, arc, spline).
class NativeKernel2D final : public IGeometryKernel {
public:
    void tessellate(const GeometryStore& store, EntityHandle entity, double tolerance,
                    std::vector<Vec2>& out) const override;

    bool closest_point(const GeometryStore& store, EntityHandle entity, Vec2 query,
                       Vec2& out_point) const override;

    void intersect(const GeometryStore& store, EntityHandle a, EntityHandle b,
                   std::vector<Vec2>& out) const override;

    bool offset(const GeometryStore& store, EntityHandle entity, double distance, Vec2 side,
                Command& out) const override;
};

} // namespace musacad::core

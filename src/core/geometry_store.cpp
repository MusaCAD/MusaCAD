#include "musacad/core/geometry_store.hpp"

namespace musacad::core {

EntityHandle GeometryStore::add_point(Vec2 p) {
    const auto slot = points_.insert(PointData{p});
    return EntityHandle{slot.index, slot.generation, EntityKind::Point};
}

EntityHandle GeometryStore::add_line(Vec2 a, Vec2 b) {
    const auto slot = lines_.insert(LineData{a, b});
    return EntityHandle{slot.index, slot.generation, EntityKind::Line};
}

EntityHandle GeometryStore::add_circle(Vec2 center, double radius) {
    const auto slot = circles_.insert(CircleData{center, radius});
    return EntityHandle{slot.index, slot.generation, EntityKind::Circle};
}

EntityHandle GeometryStore::add_arc(Vec2 center, double radius, double start_angle,
                                    double end_angle) {
    const auto slot = arcs_.insert(ArcData{center, radius, start_angle, end_angle});
    return EntityHandle{slot.index, slot.generation, EntityKind::Arc};
}

EntityHandle GeometryStore::add_polyline(std::span<const Vec2> vertices, bool closed) {
    const auto offset = static_cast<std::uint32_t>(polyline_pool_.size());
    polyline_pool_.insert(polyline_pool_.end(), vertices.begin(), vertices.end());
    const auto slot = polylines_.insert(
        PolylineData{offset, static_cast<std::uint32_t>(vertices.size()), closed});
    return EntityHandle{slot.index, slot.generation, EntityKind::Polyline};
}

EntityHandle GeometryStore::add_spline(std::span<const Vec2> control_points,
                                       std::uint32_t degree) {
    const auto offset = static_cast<std::uint32_t>(spline_pool_.size());
    spline_pool_.insert(spline_pool_.end(), control_points.begin(), control_points.end());
    const auto slot = splines_.insert(
        SplineData{offset, static_cast<std::uint32_t>(control_points.size()), degree});
    return EntityHandle{slot.index, slot.generation, EntityKind::Spline};
}

bool GeometryStore::remove(EntityHandle h) noexcept {
    switch (h.kind) {
    case EntityKind::Point:
        return points_.erase(h.index, h.generation);
    case EntityKind::Line:
        return lines_.erase(h.index, h.generation);
    case EntityKind::Polyline:
        return polylines_.erase(h.index, h.generation);
    case EntityKind::Circle:
        return circles_.erase(h.index, h.generation);
    case EntityKind::Arc:
        return arcs_.erase(h.index, h.generation);
    case EntityKind::Spline:
        return splines_.erase(h.index, h.generation);
    }
    return false;
}

bool GeometryStore::is_valid(EntityHandle h) const noexcept {
    switch (h.kind) {
    case EntityKind::Point:
        return points_.is_valid(h.index, h.generation);
    case EntityKind::Line:
        return lines_.is_valid(h.index, h.generation);
    case EntityKind::Polyline:
        return polylines_.is_valid(h.index, h.generation);
    case EntityKind::Circle:
        return circles_.is_valid(h.index, h.generation);
    case EntityKind::Arc:
        return arcs_.is_valid(h.index, h.generation);
    case EntityKind::Spline:
        return splines_.is_valid(h.index, h.generation);
    }
    return false;
}

std::size_t GeometryStore::live_count() const noexcept {
    return points_.live_count() + lines_.live_count() + polylines_.live_count() +
           circles_.live_count() + arcs_.live_count() + splines_.live_count();
}

void GeometryStore::clear() noexcept {
    points_.clear();
    lines_.clear();
    circles_.clear();
    arcs_.clear();
    polylines_.clear();
    splines_.clear();
    polyline_pool_.clear();
    spline_pool_.clear();
}

const PointData* GeometryStore::point(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Point ? points_.get(h.index, h.generation) : nullptr;
}
const LineData* GeometryStore::line(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Line ? lines_.get(h.index, h.generation) : nullptr;
}
const CircleData* GeometryStore::circle(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Circle ? circles_.get(h.index, h.generation) : nullptr;
}
const ArcData* GeometryStore::arc(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Arc ? arcs_.get(h.index, h.generation) : nullptr;
}
const PolylineData* GeometryStore::polyline(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Polyline ? polylines_.get(h.index, h.generation) : nullptr;
}
const SplineData* GeometryStore::spline(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Spline ? splines_.get(h.index, h.generation) : nullptr;
}

std::span<const Vec2> GeometryStore::vertices_of(const PolylineData& pl) const noexcept {
    return std::span<const Vec2>(polyline_pool_).subspan(pl.offset, pl.count);
}
std::span<const Vec2> GeometryStore::control_points_of(const SplineData& sp) const noexcept {
    return std::span<const Vec2>(spline_pool_).subspan(sp.offset, sp.count);
}

} // namespace musacad::core

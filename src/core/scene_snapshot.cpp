#include "musacad/core/scene_snapshot.hpp"

#include <algorithm>
#include <vector>

namespace musacad::core {

namespace {

template <class Arena, class Fn>
void for_each_live(const Arena& arena, EntityKind kind, Fn&& fn) {
    for (std::uint32_t i = 0; i < arena.slot_count(); ++i) {
        if (arena.alive(i)) {
            fn(EntityHandle{i, arena.generations()[i], kind});
        }
    }
}

} // namespace

void build_render_snapshot(const GeometryStore& store, const IGeometryKernel& kernel,
                           RenderSnapshot& out, double tolerance) {
    out.points.clear();
    out.line_vertices.clear();

    // Standalone points.
    for_each_live(store.points(), EntityKind::Point,
                  [&](EntityHandle h) { out.points.push_back(store.point(h)->p); });

    // Native lines -> segment endpoints (one pair per segment).
    for_each_live(store.lines(), EntityKind::Line, [&](EntityHandle h) {
        const LineData* l = store.line(h);
        out.line_vertices.push_back(l->a);
        out.line_vertices.push_back(l->b);
    });

    // Curved primitives -> tessellated segment endpoints.
    std::vector<Vec2> tess;
    const auto emit = [&](EntityHandle h) {
        kernel.tessellate(store, h, tolerance, tess);
        for (std::size_t s = 1; s < tess.size(); ++s) {
            out.line_vertices.push_back(tess[s - 1]);
            out.line_vertices.push_back(tess[s]);
        }
    };
    for_each_live(store.circles(), EntityKind::Circle, emit);
    for_each_live(store.arcs(), EntityKind::Arc, emit);
    for_each_live(store.polylines(), EntityKind::Polyline, emit);
    for_each_live(store.splines(), EntityKind::Spline, emit);

    // World-space bounds of live geometry (for ZOOM extents).
    out.has_bounds = false;
    const auto extend = [&](const Vec2& p) {
        if (!out.has_bounds) {
            out.bounds_min = p;
            out.bounds_max = p;
            out.has_bounds = true;
        } else {
            out.bounds_min = {std::min(out.bounds_min.x, p.x), std::min(out.bounds_min.y, p.y)};
            out.bounds_max = {std::max(out.bounds_max.x, p.x), std::max(out.bounds_max.y, p.y)};
        }
    };
    for (const Vec2& p : out.points) {
        extend(p);
    }
    for (const Vec2& p : out.line_vertices) {
        extend(p);
    }
}

} // namespace musacad::core

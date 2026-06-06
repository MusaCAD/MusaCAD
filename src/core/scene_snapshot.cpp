#include "musacad/core/scene_snapshot.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
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

std::uint32_t pack_rgb(Rgb c) {
    return (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) | c.b;
}

/// Off/frozen layers contribute no geometry, so the renderer never sees them.
bool visible(const GeometryStore& store, const EntityProps& p) {
    const Layer* l = store.layer(p.layer);
    return l != nullptr && l->on && !l->frozen;
}
Rgb entity_color(const GeometryStore& store, const EntityProps& p) {
    const Layer* l = store.layer(p.layer);
    return l != nullptr ? resolve(p, *l).color : Rgb{};
}

} // namespace

void build_render_snapshot(const GeometryStore& store, const IGeometryKernel& kernel,
                           RenderSnapshot& out, double tolerance) {
    out.points.clear();
    out.line_vertices.clear();
    out.line_batches.clear();
    out.point_batches.clear();
    out.layers.assign(store.layers().begin(), store.layers().end());
    out.current_layer = store.current_layer();

    // Group visible geometry by resolved colour; std::map keeps a stable order.
    std::map<std::uint32_t, std::vector<Vec2>> line_groups;
    std::map<std::uint32_t, std::vector<Vec2>> point_groups;
    std::map<std::uint32_t, Rgb> color_of;
    const auto note = [&](Rgb c) { color_of[pack_rgb(c)] = c; };

    for_each_live(store.points(), EntityKind::Point, [&](EntityHandle h) {
        const PointData* d = store.point(h);
        if (!visible(store, d->props)) {
            return;
        }
        const Rgb c = entity_color(store, d->props);
        note(c);
        point_groups[pack_rgb(c)].push_back(d->p);
    });

    const auto line_seg = [&](Rgb c, Vec2 a, Vec2 b) {
        note(c);
        auto& v = line_groups[pack_rgb(c)];
        v.push_back(a);
        v.push_back(b);
    };
    for_each_live(store.lines(), EntityKind::Line, [&](EntityHandle h) {
        const LineData* l = store.line(h);
        if (!visible(store, l->props)) {
            return;
        }
        line_seg(entity_color(store, l->props), l->a, l->b);
    });

    std::vector<Vec2> tess;
    const auto emit_curve = [&](EntityHandle h, const EntityProps& p) {
        if (!visible(store, p)) {
            return;
        }
        const Rgb c = entity_color(store, p);
        kernel.tessellate(store, h, tolerance, tess);
        for (std::size_t s = 1; s < tess.size(); ++s) {
            line_seg(c, tess[s - 1], tess[s]);
        }
    };
    for_each_live(store.circles(), EntityKind::Circle,
                  [&](EntityHandle h) { emit_curve(h, store.circle(h)->props); });
    for_each_live(store.arcs(), EntityKind::Arc,
                  [&](EntityHandle h) { emit_curve(h, store.arc(h)->props); });
    for_each_live(store.polylines(), EntityKind::Polyline,
                  [&](EntityHandle h) { emit_curve(h, store.polyline(h)->props); });
    for_each_live(store.splines(), EntityKind::Spline,
                  [&](EntityHandle h) { emit_curve(h, store.spline(h)->props); });

    // Flatten colour groups into contiguous batches over the payload arrays.
    for (auto& [key, verts] : line_groups) {
        out.line_batches.push_back(
            ColorBatch{color_of[key], static_cast<std::uint32_t>(out.line_vertices.size() / 2),
                       static_cast<std::uint32_t>(verts.size() / 2)});
        out.line_vertices.insert(out.line_vertices.end(), verts.begin(), verts.end());
    }
    for (auto& [key, pts] : point_groups) {
        out.point_batches.push_back(
            ColorBatch{color_of[key], static_cast<std::uint32_t>(out.points.size()),
                       static_cast<std::uint32_t>(pts.size())});
        out.points.insert(out.points.end(), pts.begin(), pts.end());
    }

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

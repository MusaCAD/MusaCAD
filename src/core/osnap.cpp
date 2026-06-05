#include "musacad/core/osnap.hpp"

#include <cmath>
#include <vector>

#include "musacad/core/geometry_kernel.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/spatial_grid.hpp"

namespace musacad::core {

namespace {

Vec2 on_circle(Vec2 c, double r, double a) {
    return {c.x + r * std::cos(a), c.y + r * std::sin(a)};
}

double sweep_of(double start, double end) {
    double s = end - start;
    while (s < 0.0) {
        s += kTwoPi;
    }
    return s <= 0.0 ? kTwoPi : s;
}

} // namespace

SnapResult compute_snap(const GeometryStore& store, const IGeometryKernel& kernel,
                        const SpatialGrid& grid, Vec2 cursor, double radius_world,
                        std::uint32_t enabled_types) {
    SnapResult result;
    if (radius_world <= 0.0 || enabled_types == 0) {
        return result;
    }
    const double r2 = radius_world * radius_world;

    std::vector<EntityHandle> candidates;
    grid.query({cursor.x - radius_world, cursor.y - radius_world},
               {cursor.x + radius_world, cursor.y + radius_world}, candidates);

    // Track the best by (priority, distance): lower SnapType value wins.
    std::uint8_t best_type = 0xFF;
    double best_d2 = r2;
    const auto consider = [&](SnapType t, Vec2 p) {
        if ((enabled_types & snap_bit(t)) == 0) {
            return;
        }
        const double d2 = length_squared(p - cursor);
        if (d2 > r2) {
            return;
        }
        const auto tv = static_cast<std::uint8_t>(t);
        if (tv < best_type || (tv == best_type && d2 < best_d2)) {
            best_type = tv;
            best_d2 = d2;
            result.found = true;
            result.type = t;
            result.point = p;
        }
    };

    for (const EntityHandle h : candidates) {
        switch (h.kind) {
        case EntityKind::Point:
            consider(SnapType::Endpoint, store.point(h)->p);
            break;
        case EntityKind::Line: {
            const LineData* l = store.line(h);
            consider(SnapType::Endpoint, l->a);
            consider(SnapType::Endpoint, l->b);
            consider(SnapType::Midpoint, (l->a + l->b) * 0.5);
            break;
        }
        case EntityKind::Circle: {
            const CircleData* c = store.circle(h);
            consider(SnapType::Center, c->center);
            break;
        }
        case EntityKind::Arc: {
            const ArcData* a = store.arc(h);
            const double sw = sweep_of(a->start_angle, a->end_angle);
            consider(SnapType::Center, a->center);
            consider(SnapType::Endpoint, on_circle(a->center, a->radius, a->start_angle));
            consider(SnapType::Endpoint, on_circle(a->center, a->radius, a->start_angle + sw));
            consider(SnapType::Midpoint, on_circle(a->center, a->radius, a->start_angle + sw * 0.5));
            break;
        }
        case EntityKind::Polyline: {
            const PolylineData* p = store.polyline(h);
            const auto v = store.vertices_of(*p);
            for (std::size_t i = 0; i < v.size(); ++i) {
                consider(SnapType::Endpoint, v[i]);
                if (i + 1 < v.size()) {
                    consider(SnapType::Midpoint, (v[i] + v[i + 1]) * 0.5);
                }
            }
            if (p->closed && v.size() >= 2) {
                consider(SnapType::Midpoint, (v.back() + v.front()) * 0.5);
            }
            break;
        }
        case EntityKind::Spline:
            break; // nearest handled below
        }

        // Nearest applies to any curve-like entity.
        if ((enabled_types & snap_bit(SnapType::Nearest)) != 0) {
            Vec2 np;
            if (kernel.closest_point(store, h, cursor, np)) {
                consider(SnapType::Nearest, np);
            }
        }
    }

    // Intersection: pairs of candidates (the candidate set is local/small).
    if ((enabled_types & snap_bit(SnapType::Intersection)) != 0) {
        std::vector<Vec2> hits;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            for (std::size_t j = i + 1; j < candidates.size(); ++j) {
                hits.clear();
                kernel.intersect(store, candidates[i], candidates[j], hits);
                for (const Vec2& p : hits) {
                    consider(SnapType::Intersection, p);
                }
            }
        }
    }

    return result;
}

} // namespace musacad::core

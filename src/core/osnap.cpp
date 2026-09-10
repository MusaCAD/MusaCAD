// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/osnap.hpp"

#include "musacad/core/ellipse.hpp"
#include "musacad/core/native_kernel_2d.hpp"

#include <array>
#include <cmath>
#include <optional>
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

/// True if `angle` lies within the CCW sweep [start, start+sweep].
bool angle_in_sweep(double angle, double start, double sweep) {
    double rel = angle - start;
    while (rel < 0.0) {
        rel += kTwoPi;
    }
    return rel <= sweep + 1e-9;
}

/// Area-weighted centroid of a closed polygon (falls back to the vertex average
/// for a degenerate / zero-area outline).
Vec2 polygon_centroid(std::span<const Vec2> v) {
    double area = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const Vec2& p0 = v[i];
        const Vec2& p1 = v[(i + 1) % v.size()];
        const double cross_z = p0.x * p1.y - p1.x * p0.y;
        area += cross_z;
        cx += (p0.x + p1.x) * cross_z;
        cy += (p0.y + p1.y) * cross_z;
    }
    if (std::abs(area) < 1e-12) {
        Vec2 avg{0.0, 0.0};
        for (const Vec2& p : v) {
            avg += p;
        }
        return v.empty() ? avg : avg / static_cast<double>(v.size());
    }
    return {cx / (3.0 * area), cy / (3.0 * area)};
}

} // namespace

SnapResult compute_snap(const GeometryStore& store, const IGeometryKernel& kernel,
                        const SpatialGrid& grid, Vec2 cursor, double radius_world,
                        std::uint32_t enabled_types, std::optional<Vec2> from_point) {
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
        // Circle/arc helpers for quadrant, perpendicular and tangent snaps.
        const auto circle_quadrants = [&](Vec2 c, double r, bool arc, double a0, double sw) {
            for (int q = 0; q < 4; ++q) {
                const double ang = static_cast<double>(q) * kHalfPi;
                if (!arc || angle_in_sweep(ang, a0, sw)) {
                    consider(SnapType::Quadrant, on_circle(c, r, ang));
                }
            }
        };
        const auto circle_perp = [&](Vec2 c, double r, bool arc, double a0, double sw) {
            if (!from_point) {
                return;
            }
            const Vec2 d = *from_point - c;
            const double len = length(d);
            if (len < 1e-9) {
                return;
            }
            const Vec2 u = d / len;
            for (const Vec2 p : {c + u * r, c - u * r}) {
                const double ang = std::atan2(p.y - c.y, p.x - c.x);
                if (!arc || angle_in_sweep(ang, a0, sw)) {
                    consider(SnapType::Perpendicular, p);
                }
            }
        };
        const auto circle_tangent = [&](Vec2 c, double r, bool arc, double a0, double sw) {
            if (!from_point) {
                return;
            }
            const Vec2 d = *from_point - c;
            const double dd = length(d);
            if (dd <= r) {
                return; // inside or on the circle: no tangent
            }
            const double base = std::atan2(d.y, d.x);
            const double phi = std::acos(r / dd);
            for (const double a : {base + phi, base - phi}) {
                const Vec2 p = on_circle(c, r, a);
                if (!arc || angle_in_sweep(a, a0, sw)) {
                    consider(SnapType::Tangent, p);
                }
            }
        };

        switch (h.kind) {
        case EntityKind::Point:
            consider(SnapType::Node, store.point(h)->p);
            break;
        case EntityKind::Line: {
            const LineData* l = store.line(h);
            consider(SnapType::Endpoint, l->a);
            consider(SnapType::Endpoint, l->b);
            consider(SnapType::Midpoint, (l->a + l->b) * 0.5);
            if (from_point) {
                // Parallel: the line through the previous point parallel to this one;
                // the snap is the cursor's foot on it (AutoCAD's PAR tracking point).
                const Vec2 pd = l->b - l->a;
                const double plen2 = length_squared(pd);
                if (plen2 > 0.0) {
                    const Vec2 u = pd / std::sqrt(plen2);
                    consider(SnapType::Parallel, *from_point + u * dot(cursor - *from_point, u));
                }
            }
            if (from_point) {
                // Foot of perpendicular from the previous point onto the line.
                const Vec2 ab = l->b - l->a;
                const double len2 = length_squared(ab);
                if (len2 > 0.0) {
                    const double t = dot(*from_point - l->a, ab) / len2;
                    consider(SnapType::Perpendicular, l->a + ab * t);
                }
            }
            break;
        }
        case EntityKind::Circle: {
            const CircleData* c = store.circle(h);
            consider(SnapType::Center, c->center);
            circle_quadrants(c->center, c->radius, false, 0.0, 0.0);
            circle_perp(c->center, c->radius, false, 0.0, 0.0);
            circle_tangent(c->center, c->radius, false, 0.0, 0.0);
            break;
        }
        case EntityKind::Arc: {
            const ArcData* a = store.arc(h);
            const double sw = sweep_of(a->start_angle, a->end_angle);
            consider(SnapType::Center, a->center);
            consider(SnapType::Endpoint, on_circle(a->center, a->radius, a->start_angle));
            consider(SnapType::Endpoint, on_circle(a->center, a->radius, a->start_angle + sw));
            consider(SnapType::Midpoint, on_circle(a->center, a->radius, a->start_angle + sw * 0.5));
            circle_quadrants(a->center, a->radius, true, a->start_angle, sw);
            circle_perp(a->center, a->radius, true, a->start_angle, sw);
            circle_tangent(a->center, a->radius, true, a->start_angle, sw);
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
                consider(SnapType::Centroid, polygon_centroid(v)); // Musa extension
            }
            break;
        }
        case EntityKind::Insert:
            consider(SnapType::Insertion, store.insert(h)->pos); // block insertion point
            break;
        case EntityKind::Text:
            consider(SnapType::Insertion, store.text(h)->pos);
            break;
        case EntityKind::AttDef:
            consider(SnapType::Insertion, store.attdef(h)->text.pos);
            break;
        case EntityKind::MText:
            consider(SnapType::Insertion, store.mtext(h)->text.pos);
            break;
        case EntityKind::Spline:
        case EntityKind::Dimension:
        case EntityKind::Leader:
        case EntityKind::MLeader:
        case EntityKind::Hatch:
        case EntityKind::Fcf:
        case EntityKind::Datum:
        case EntityKind::Image:
            break; // no object-snap points (nearest, if any, handled below)
        case EntityKind::Viewport: {
            const ViewportData* v = store.viewport(h);
            const double hw = v->width * 0.5;
            const double hh = v->height * 0.5;
            const Vec2 c[4] = {{v->center.x - hw, v->center.y - hh}, {v->center.x + hw, v->center.y - hh},
                               {v->center.x + hw, v->center.y + hh}, {v->center.x - hw, v->center.y + hh}};
            for (int i = 0; i < 4; ++i) {
                consider(SnapType::Endpoint, c[i]);
                const Vec2& n = c[(i + 1) % 4];
                consider(SnapType::Midpoint, {(c[i].x + n.x) * 0.5, (c[i].y + n.y) * 0.5});
            }
            break;
        }
        case EntityKind::Ellipse: {
            const EllipseData* e = store.ellipse(h);
            consider(SnapType::Center, e->center);
            // Quadrants are the axis endpoints (parameters 0, 90, 180, 270 degrees) that
            // lie on the drawn portion; an arc also has its ends and midpoint.
            for (const double t : {0.0, kHalfPi, kPi, kPi + kHalfPi}) {
                if (ellipse::param_in_range(*e, t)) {
                    consider(SnapType::Quadrant, ellipse::point_at(*e, t));
                }
            }
            if (!ellipse::is_full(*e)) {
                const double sw = ellipse::sweep_of(*e);
                consider(SnapType::Endpoint, ellipse::point_at(*e, e->start));
                consider(SnapType::Endpoint, ellipse::point_at(*e, e->start + sw));
                consider(SnapType::Midpoint, ellipse::point_at(*e, e->start + sw * 0.5));
            }
            break;
        }
        case EntityKind::Xline:
            if (from_point) {
                const XlineData* x = store.xline(h);
                consider(SnapType::Parallel, *from_point + x->dir * dot(cursor - *from_point, x->dir));
            }
            break;
        case EntityKind::Table:
            break; // no object-snap points (nearest, if any, handled below)
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

    // Apparent intersection: where the objects WOULD cross if extended -- lines and
    // construction lines as infinite lines, arcs as their full circles. A real crossing
    // is already an Intersection (which out-ranks this), so only the extensions matter.
    if ((enabled_types & snap_bit(SnapType::ApparentIntersection)) != 0) {
        struct Ext {
            bool is_line = false;
            Vec2 a{};
            Vec2 b{};
            Vec2 c{};
            double r = 0.0;
        };
        const auto ext_of = [&](EntityHandle h, Ext& e) {
            switch (h.kind) {
            case EntityKind::Line:
                e = {true, store.line(h)->a, store.line(h)->b, {}, 0.0};
                return true;
            case EntityKind::Xline:
                e = {true, store.xline(h)->base, store.xline(h)->base + store.xline(h)->dir, {}, 0.0};
                return true;
            case EntityKind::Arc:
                e = {false, {}, {}, store.arc(h)->center, store.arc(h)->radius};
                return true;
            case EntityKind::Circle:
                e = {false, {}, {}, store.circle(h)->center, store.circle(h)->radius};
                return true;
            default:
                return false;
            }
        };
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            Ext ei;
            if (!ext_of(candidates[i], ei)) {
                continue;
            }
            for (std::size_t j = i + 1; j < candidates.size(); ++j) {
                Ext ej;
                if (!ext_of(candidates[j], ej)) {
                    continue;
                }
                Vec2 p0{};
                Vec2 p1{};
                if (ei.is_line && ej.is_line) {
                    if (NativeKernel2D::line_line_intersection(ei.a, ei.b, ej.a, ej.b, p0)) {
                        consider(SnapType::ApparentIntersection, p0);
                    }
                } else if (ei.is_line != ej.is_line) {
                    const Ext& ln = ei.is_line ? ei : ej;
                    const Ext& ci = ei.is_line ? ej : ei;
                    const int n = NativeKernel2D::line_circle_intersection(ln.a, ln.b, ci.c, ci.r, p0, p1);
                    if (n >= 1) {
                        consider(SnapType::ApparentIntersection, p0);
                    }
                    if (n == 2) {
                        consider(SnapType::ApparentIntersection, p1);
                    }
                }
            }
        }
    }

    return result;
}

} // namespace musacad::core

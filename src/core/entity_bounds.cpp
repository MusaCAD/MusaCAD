#include "musacad/core/entity_bounds.hpp"

#include <algorithm>
#include <cmath>

#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/text/stroke_font.hpp"

namespace musacad::core {

bool entity_aabb(const GeometryStore& store, EntityHandle h, Vec2& out_min, Vec2& out_max) {
    if (!store.is_valid(h)) {
        return false;
    }
    const auto box2 = [&](Vec2 a, Vec2 b) {
        out_min = {std::min(a.x, b.x), std::min(a.y, b.y)};
        out_max = {std::max(a.x, b.x), std::max(a.y, b.y)};
    };
    switch (h.kind) {
    case EntityKind::Point: {
        out_min = out_max = store.point(h)->p;
        return true;
    }
    case EntityKind::Line: {
        const LineData* l = store.line(h);
        box2(l->a, l->b);
        return true;
    }
    case EntityKind::Circle: {
        const CircleData* c = store.circle(h);
        out_min = {c->center.x - c->radius, c->center.y - c->radius};
        out_max = {c->center.x + c->radius, c->center.y + c->radius};
        return true;
    }
    case EntityKind::Arc: {
        // Conservative: the full circle's box (cheap, correct as a superset).
        const ArcData* a = store.arc(h);
        out_min = {a->center.x - a->radius, a->center.y - a->radius};
        out_max = {a->center.x + a->radius, a->center.y + a->radius};
        return true;
    }
    case EntityKind::Polyline: {
        const PolylineData* p = store.polyline(h);
        const auto verts = store.vertices_of(*p);
        if (verts.empty()) {
            return false;
        }
        out_min = out_max = verts.front();
        for (const Vec2& v : verts) {
            out_min = {std::min(out_min.x, v.x), std::min(out_min.y, v.y)};
            out_max = {std::max(out_max.x, v.x), std::max(out_max.y, v.y)};
        }
        return true;
    }
    case EntityKind::Spline: {
        const SplineData* s = store.spline(h);
        const auto cps = store.control_points_of(*s);
        if (cps.empty()) {
            return false;
        }
        out_min = out_max = cps.front();
        for (const Vec2& v : cps) {
            out_min = {std::min(out_min.x, v.x), std::min(out_min.y, v.y)};
            out_max = {std::max(out_max.x, v.x), std::max(out_max.y, v.y)};
        }
        return true;
    }
    case EntityKind::Text: {
        const TextData* t = store.text(h);
        const double w = text::text_width(store.string_of(*t), t->height);
        const double cs = std::cos(t->rotation);
        const double sn = std::sin(t->rotation);
        const Vec2 corners[4] = {{0, 0}, {w, 0}, {w, t->height}, {0, t->height}};
        bool first = true;
        for (const Vec2& c : corners) {
            const Vec2 p{t->pos.x + c.x * cs - c.y * sn, t->pos.y + c.x * sn + c.y * cs};
            if (first) {
                out_min = out_max = p;
                first = false;
            } else {
                out_min = {std::min(out_min.x, p.x), std::min(out_min.y, p.y)};
                out_max = {std::max(out_max.x, p.x), std::max(out_max.y, p.y)};
            }
        }
        return true;
    }
    case EntityKind::Dimension: {
        const DimData* d = store.dimension(h);
        const DimStyle* s = store.dimstyle(d->style);
        const DimGeometry g = compute_dim_geometry(*d, s != nullptr ? *s : DimStyle{});
        bool first = true;
        const auto extend = [&](Vec2 p) {
            if (first) {
                out_min = out_max = p;
                first = false;
            } else {
                out_min = {std::min(out_min.x, p.x), std::min(out_min.y, p.y)};
                out_max = {std::max(out_max.x, p.x), std::max(out_max.y, p.y)};
            }
        };
        for (const Vec2& p : g.lines) {
            extend(p);
        }
        for (const Vec2& p : g.arrows) {
            extend(p);
        }
        extend(d->a);
        extend(d->b);
        return !first;
    }
    }
    return false;
}

} // namespace musacad::core

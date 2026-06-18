// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/entity_bounds.hpp"

#include <algorithm>
#include <cmath>

#include "musacad/core/block_resolve.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/text/mtext.hpp"
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
        const auto bulges = store.bulges_of(*p);
        if (verts.empty()) {
            return false;
        }
        out_min = out_max = verts.front();
        const auto extend = [&](Vec2 v) {
            out_min = {std::min(out_min.x, v.x), std::min(out_min.y, v.y)};
            out_max = {std::max(out_max.x, v.x), std::max(out_max.y, v.y)};
        };
        for (const Vec2& v : verts) {
            extend(v);
        }
        // Arc (bulged) segments can bow past their chord; sample them so the AABB
        // encloses the arc (loose is fine; too-tight would clip the bulge).
        if (!bulges.empty()) {
            const std::size_t n = verts.size();
            const std::size_t segs = (p->closed && n >= 2) ? n : n - 1;
            for (std::size_t i = 0; i < segs; ++i) {
                if (bulges[i] == 0.0) {
                    continue;
                }
                const BulgeArc a = arc_from_bulge(verts[i], verts[(i + 1) % n], bulges[i]);
                constexpr int kS = 16;
                for (int k = 1; k < kS; ++k) {
                    const double ang = a.a0 + a.sweep * (static_cast<double>(k) / kS);
                    extend({a.center.x + a.radius * std::cos(ang),
                            a.center.y + a.radius * std::sin(ang)});
                }
            }
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
        const double w = text::text_advance(store.font_engine(), store.font_name(t->font),
                                            store.string_of(*t), t->height);
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
        const DimGeometry g = compute_dim_geometry(*d, s != nullptr ? *s : DimStyle{}, Rgb{});
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
        for (const auto* list : {&g.ext_lines, &g.dim_lines, &g.arrow_lines, &g.arrow_fills}) {
            for (const Vec2& p : *list) {
                extend(p);
            }
        }
        extend(d->a);
        extend(d->b);
        return !first;
    }
    case EntityKind::Leader: {
        const LeaderData* l = store.leader(h);
        const double w = text::text_advance(store.font_engine(), store.font_name(l->font),
                                            store.string_of(*l), l->text_height);
        box2(l->tip, l->knee);
        out_min = {std::min(out_min.x, l->knee.x), std::min(out_min.y, l->knee.y)};
        out_max = {std::max(out_max.x, l->knee.x + w), std::max(out_max.y, l->knee.y + l->text_height)};
        return true;
    }
    case EntityKind::MText: {
        const MTextData* m = store.mtext(h);
        const text::MTextLayout lay = text::layout_mtext(m->text, store.string_of(m->text),
                                                         store.font_engine(),
                                                         store.font_name(m->text.font));
        out_min = lay.min;
        out_max = lay.max;
        return true;
    }
    case EntityKind::MLeader: {
        const MLeaderData* m = store.mleader(h);
        const text::MTextLayout lay = text::layout_mtext(m->text, store.string_of(m->text),
                                                         store.font_engine(),
                                                         store.font_name(m->text.font));
        out_min = lay.min;
        out_max = lay.max;
        for (const Vec2& v : store.vertices_of(*m)) {
            out_min = {std::min(out_min.x, v.x), std::min(out_min.y, v.y)};
            out_max = {std::max(out_max.x, v.x), std::max(out_max.y, v.y)};
        }
        return true;
    }
    case EntityKind::Insert: {
        // AABB over the instance's resolved world geometry (the same path the renderer
        // and pick use), so window-select / zoom-extents agree with what is drawn.
        const InsertData* in = store.insert(h);
        std::vector<InsertSeg> segs;
        resolve_insert(store, *in, kDefaultTessTolerance, segs);
        if (segs.empty()) {
            out_min = out_max = in->pos; // empty/dangling block: a point at the insertion
            return true;
        }
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
        for (const InsertSeg& s : segs) {
            extend(s.a);
            extend(s.b);
        }
        return true;
    }
    }
    return false;
}

} // namespace musacad::core

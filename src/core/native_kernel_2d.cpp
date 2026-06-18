// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#include "musacad/core/native_kernel_2d.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "musacad/core/block_resolve.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/text/mtext.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/text/stroke_font.hpp"

namespace musacad::core {

namespace {

constexpr double kIntersectEps = 1e-9;

/// Number of chord segments to approximate an arc of the given sweep within a
/// chordal tolerance.
// Upper bound on chord segments per curve. Keeps tessellation work bounded at
// extreme zoom-in (a screen-filling circle needs only ~128 segments at 0.3px
// chord error, so this cap is generous headroom and rarely reached).
constexpr std::size_t kMaxArcSegments = 8192;

std::size_t arc_segments(double radius, double sweep_abs, double tolerance) {
    sweep_abs = std::abs(sweep_abs);
    if (radius <= 0.0 || sweep_abs <= 0.0) {
        return 1;
    }
    double max_step = kHalfPi;
    if (tolerance > 0.0 && tolerance < radius) {
        max_step = 2.0 * std::acos(1.0 - tolerance / radius);
    }
    if (!(max_step > 0.0)) {
        max_step = kHalfPi;
    }
    const auto n = static_cast<std::size_t>(std::ceil(sweep_abs / max_step));
    return std::clamp(n, std::size_t{1}, kMaxArcSegments);
}

Vec2 point_on_circle(Vec2 center, double radius, double angle) {
    return {center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)};
}

/// Normalised CCW sweep from start_angle to end_angle in (0, 2pi].
double normalized_sweep(double start_angle, double end_angle) {
    double s = end_angle - start_angle;
    while (s < 0.0) {
        s += kTwoPi;
    }
    if (s <= 0.0) {
        s = kTwoPi;
    }
    return s;
}

Vec2 closest_on_segment(Vec2 a, Vec2 b, Vec2 q) {
    const Vec2 ab = b - a;
    const double len2 = length_squared(ab);
    if (len2 <= 0.0) {
        return a;
    }
    const double t = std::clamp(dot(q - a, ab) / len2, 0.0, 1.0);
    return a + ab * t;
}

// --- B-spline (clamped, uniform interior knots) evaluation -----------------

std::vector<double> make_clamped_knots(int control_count, int degree) {
    const int knot_count = control_count + degree + 1;
    std::vector<double> u(static_cast<std::size_t>(knot_count), 0.0);
    for (int i = 0; i <= degree; ++i) {
        u[static_cast<std::size_t>(i)] = 0.0;
    }
    const int interior = control_count - degree - 1; // may be <= 0
    for (int j = 1; j <= interior; ++j) {
        u[static_cast<std::size_t>(degree + j)] =
            static_cast<double>(j) / static_cast<double>(control_count - degree);
    }
    for (int i = control_count; i < knot_count; ++i) {
        u[static_cast<std::size_t>(i)] = 1.0;
    }
    return u;
}

int find_span(int n, int degree, double t, const std::vector<double>& u) {
    if (t >= u[static_cast<std::size_t>(n + 1)]) {
        return n;
    }
    if (t <= u[static_cast<std::size_t>(degree)]) {
        return degree;
    }
    int low = degree;
    int high = n + 1;
    int mid = (low + high) / 2;
    while (t < u[static_cast<std::size_t>(mid)] || t >= u[static_cast<std::size_t>(mid + 1)]) {
        if (t < u[static_cast<std::size_t>(mid)]) {
            high = mid;
        } else {
            low = mid;
        }
        mid = (low + high) / 2;
    }
    return mid;
}

Vec2 de_boor(int span, double t, int degree, const std::vector<double>& u,
             std::span<const Vec2> ctrl) {
    std::vector<Vec2> d(static_cast<std::size_t>(degree + 1));
    for (int j = 0; j <= degree; ++j) {
        d[static_cast<std::size_t>(j)] = ctrl[static_cast<std::size_t>(span - degree + j)];
    }
    for (int r = 1; r <= degree; ++r) {
        for (int j = degree; j >= r; --j) {
            const double left = u[static_cast<std::size_t>(span - degree + j)];
            const double right = u[static_cast<std::size_t>(span + 1 + j - r)];
            const double denom = right - left;
            const double alpha = denom > 0.0 ? (t - left) / denom : 0.0;
            d[static_cast<std::size_t>(j)] =
                d[static_cast<std::size_t>(j - 1)] * (1.0 - alpha) +
                d[static_cast<std::size_t>(j)] * alpha;
        }
    }
    return d[static_cast<std::size_t>(degree)];
}

void tessellate_spline(std::span<const Vec2> ctrl, std::uint32_t degree_in, std::vector<Vec2>& out) {
    const int count = static_cast<int>(ctrl.size());
    if (count <= 0) {
        return;
    }
    if (count == 1) {
        out.push_back(ctrl[0]);
        return;
    }
    int degree = static_cast<int>(degree_in);
    if (degree < 1) {
        degree = 1;
    }
    if (degree > count - 1) {
        degree = count - 1;
    }
    const std::vector<double> knots = make_clamped_knots(count, degree);
    const int n = count - 1;
    const auto samples = std::clamp<std::size_t>(static_cast<std::size_t>(count - 1) * 16,
                                                 std::size_t{2}, std::size_t{8192});
    out.reserve(out.size() + samples + 1);
    for (std::size_t i = 0; i <= samples; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(samples);
        t = std::clamp(t, 0.0, 1.0);
        const int span = find_span(n, degree, t, knots);
        out.push_back(de_boor(span, t, degree, knots, ctrl));
    }
}

/// Closest point on the angular arc; falls back to the nearer endpoint when the
/// query projects outside the sweep.
Vec2 closest_on_arc(const ArcData& arc, Vec2 q) {
    const Vec2 d = q - arc.center;
    const double sweep = normalized_sweep(arc.start_angle, arc.end_angle);
    double ang = std::atan2(d.y, d.x);
    double rel = ang - arc.start_angle;
    while (rel < 0.0) {
        rel += kTwoPi;
    }
    if (rel <= sweep) {
        return point_on_circle(arc.center, arc.radius, arc.start_angle + rel);
    }
    const Vec2 p0 = point_on_circle(arc.center, arc.radius, arc.start_angle);
    const Vec2 p1 = point_on_circle(arc.center, arc.radius, arc.start_angle + sweep);
    return length_squared(q - p0) <= length_squared(q - p1) ? p0 : p1;
}

bool segment_intersection(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, Vec2& out) {
    const Vec2 r = p2 - p1;
    const Vec2 s = p4 - p3;
    const double rxs = cross(r, s);
    if (std::abs(rxs) < 1e-12) {
        return false; // parallel or collinear -- not reported
    }
    const Vec2 qp = p3 - p1;
    const double t = cross(qp, s) / rxs;
    const double u = cross(qp, r) / rxs;
    if (t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0) {
        out = p1 + r * t;
        return true;
    }
    return false;
}

void push_unique(std::vector<Vec2>& out, std::size_t start, Vec2 p) {
    for (std::size_t i = start; i < out.size(); ++i) {
        if (length_squared(out[i] - p) < kIntersectEps * kIntersectEps) {
            return;
        }
    }
    out.push_back(p);
}

bool angle_on_arc(const ArcData& arc, Vec2 p) {
    const double sweep = normalized_sweep(arc.start_angle, arc.end_angle);
    double rel = std::atan2(p.y - arc.center.y, p.x - arc.center.x) - arc.start_angle;
    while (rel < 0.0) {
        rel += kTwoPi;
    }
    return rel <= sweep + 1e-9;
}

/// Intersections of the segment p0-p1 with a circle (centre c, radius r), kept
/// only within the segment. When `arc` is non-null, points are further filtered
/// to the arc's sweep. Exact (analytic).
void seg_circle_hits(Vec2 p0, Vec2 p1, Vec2 c, double r, std::vector<Vec2>& out, std::size_t base,
                     const ArcData* arc) {
    const Vec2 d = p1 - p0;
    const double A = dot(d, d);
    if (A < kIntersectEps) {
        return;
    }
    const Vec2 f = p0 - c;
    const double B = 2.0 * dot(f, d);
    const double C = dot(f, f) - r * r;
    double disc = B * B - 4.0 * A * C;
    if (disc < 0.0) {
        return;
    }
    disc = std::sqrt(disc);
    for (const double t : {(-B - disc) / (2.0 * A), (-B + disc) / (2.0 * A)}) {
        if (t < -1e-9 || t > 1.0 + 1e-9) {
            continue;
        }
        const Vec2 p = p0 + d * t;
        if (arc == nullptr || angle_on_arc(*arc, p)) {
            push_unique(out, base, p);
        }
    }
}

} // namespace

void NativeKernel2D::tessellate(const GeometryStore& store, EntityHandle entity, double tolerance,
                                std::vector<Vec2>& out) const {
    out.clear();
    if (!store.is_valid(entity)) {
        return;
    }
    switch (entity.kind) {
    case EntityKind::Point: {
        out.push_back(store.point(entity)->p);
        break;
    }
    case EntityKind::Line: {
        const LineData* l = store.line(entity);
        out.push_back(l->a);
        out.push_back(l->b);
        break;
    }
    case EntityKind::Circle: {
        const CircleData* c = store.circle(entity);
        const std::size_t n = arc_segments(c->radius, kTwoPi, tolerance);
        out.reserve(n + 1);
        for (std::size_t i = 0; i < n; ++i) {
            const double a = static_cast<double>(i) / static_cast<double>(n) * kTwoPi;
            out.push_back(point_on_circle(c->center, c->radius, a));
        }
        out.push_back(out.front()); // close the loop exactly (no float drift)
        break;
    }
    case EntityKind::Arc: {
        const ArcData* arc = store.arc(entity);
        const double sweep = normalized_sweep(arc->start_angle, arc->end_angle);
        const std::size_t n = arc_segments(arc->radius, sweep, tolerance);
        out.reserve(n + 1);
        for (std::size_t i = 0; i <= n; ++i) {
            const double a = arc->start_angle + sweep * (static_cast<double>(i) /
                                                         static_cast<double>(n));
            out.push_back(point_on_circle(arc->center, arc->radius, a));
        }
        break;
    }
    case EntityKind::Polyline: {
        const PolylineData* pl = store.polyline(entity);
        const std::span<const Vec2> verts = store.vertices_of(*pl);
        const std::span<const double> bulges = store.bulges_of(*pl);
        out.clear();
        if (verts.empty()) {
            break;
        }
        const std::size_t n = verts.size();
        const std::size_t segs = (pl->closed && n >= 2) ? n : n - 1;
        out.push_back(verts[0]);
        for (std::size_t i = 0; i < segs; ++i) {
            const Vec2 p0 = verts[i];
            const Vec2 p1 = verts[(i + 1) % n];
            const double b = bulges.empty() ? 0.0 : bulges[i];
            if (b == 0.0) {
                out.push_back(p1); // straight segment
                continue;
            }
            const BulgeArc a = arc_from_bulge(p0, p1, b); // zoom-adaptive arc samples
            const std::size_t m = arc_segments(a.radius, a.sweep, tolerance);
            for (std::size_t k = 1; k <= m; ++k) {
                const double ang = a.a0 + a.sweep * (static_cast<double>(k) / static_cast<double>(m));
                out.push_back({a.center.x + a.radius * std::cos(ang),
                               a.center.y + a.radius * std::sin(ang)});
            }
            out.back() = p1; // land exactly on the vertex
        }
        break;
    }
    case EntityKind::Spline: {
        const SplineData* sp = store.spline(entity);
        tessellate_spline(store.control_points_of(*sp), sp->degree, out);
        break;
    }
    case EntityKind::Text: {
        // Pick/window-select against the (rotated) bounding box outline.
        const TextData* t = store.text(entity);
        const double w = text::text_advance(store.font_engine(), store.font_name(t->font),
                                            store.string_of(*t), t->height);
        const double cs = std::cos(t->rotation);
        const double sn = std::sin(t->rotation);
        const Vec2 local[5] = {{0, 0}, {w, 0}, {w, t->height}, {0, t->height}, {0, 0}};
        for (const Vec2& c : local) {
            out.push_back({t->pos.x + c.x * cs - c.y * sn, t->pos.y + c.x * sn + c.y * cs});
        }
        break;
    }
    case EntityKind::Dimension: {
        // The dimension line(s) are the selectable span.
        const DimData* d = store.dimension(entity);
        const DimStyle* s = store.dimstyle(d->style);
        const DimGeometry g = compute_dim_geometry(*d, s != nullptr ? *s : DimStyle{}, Rgb{});
        out.assign(g.dim_lines.begin(), g.dim_lines.end());
        break;
    }
    case EntityKind::Leader: {
        const LeaderData* l = store.leader(entity);
        out.push_back(l->tip);
        out.push_back(l->knee);
        break;
    }
    case EntityKind::MText: {
        const MTextData* m = store.mtext(entity);
        const text::MTextLayout lay = text::layout_mtext(m->text, store.string_of(m->text),
                                                         store.font_engine(),
                                                         store.font_name(m->text.font));
        const Vec2 c[5] = {lay.min, {lay.max.x, lay.min.y}, lay.max, {lay.min.x, lay.max.y}, lay.min};
        for (const Vec2& p : c) {
            out.push_back(p);
        }
        break;
    }
    case EntityKind::MLeader: {
        const MLeaderData* m = store.mleader(entity);
        for (const Vec2& v : store.vertices_of(*m)) {
            out.push_back(v); // the leader polyline (selectable span)
        }
        break;
    }
    case EntityKind::Insert: {
        // The whole instance is one selectable object: resolve the definition to world
        // segments and emit them as pairs (disjoint primitives, no phantom connectors).
        const InsertData* in = store.insert(entity);
        std::vector<InsertSeg> segs;
        resolve_insert(store, *in, tolerance, segs);
        out.reserve(segs.size() * 2);
        for (const InsertSeg& s : segs) {
            out.push_back(s.a);
            out.push_back(s.b);
        }
        break;
    }
    }
}

bool NativeKernel2D::closest_point(const GeometryStore& store, EntityHandle entity, Vec2 query,
                                   Vec2& out_point) const {
    if (!store.is_valid(entity)) {
        return false;
    }
    switch (entity.kind) {
    case EntityKind::Point: {
        out_point = store.point(entity)->p;
        return true;
    }
    case EntityKind::Line: {
        const LineData* l = store.line(entity);
        out_point = closest_on_segment(l->a, l->b, query);
        return true;
    }
    case EntityKind::Circle: {
        const CircleData* c = store.circle(entity);
        const Vec2 dir = query - c->center;
        const double d = length(dir);
        out_point = d > 0.0 ? c->center + dir * (c->radius / d)
                            : c->center + Vec2{c->radius, 0.0};
        return true;
    }
    case EntityKind::Arc: {
        out_point = closest_on_arc(*store.arc(entity), query);
        return true;
    }
    case EntityKind::Polyline:
    case EntityKind::Spline: {
        std::vector<Vec2> pts;
        tessellate(store, entity, kDefaultTessTolerance, pts);
        if (pts.empty()) {
            return false;
        }
        Vec2 best = pts.front();
        double best_d2 = length_squared(query - best);
        for (std::size_t i = 1; i < pts.size(); ++i) {
            const Vec2 cp = closest_on_segment(pts[i - 1], pts[i], query);
            const double d2 = length_squared(query - cp);
            if (d2 < best_d2) {
                best_d2 = d2;
                best = cp;
            }
        }
        out_point = best;
        return true;
    }
    case EntityKind::Text: {
        // Closest point on the text bbox; the query inside the box reads distance 0.
        const TextData* t = store.text(entity);
        const double w = text::text_advance(store.font_engine(), store.font_name(t->font),
                                            store.string_of(*t), t->height);
        const double cs = std::cos(t->rotation);
        const double sn = std::sin(t->rotation);
        // Transform the query into text-local space (un-rotate about pos).
        const Vec2 q = query - t->pos;
        const double lx = q.x * cs + q.y * sn;
        const double ly = -q.x * sn + q.y * cs;
        const double cx = std::clamp(lx, 0.0, w);
        const double cy = std::clamp(ly, 0.0, t->height);
        out_point = {t->pos.x + cx * cs - cy * sn, t->pos.y + cx * sn + cy * cs};
        return true;
    }
    case EntityKind::Dimension: {
        const DimData* d = store.dimension(entity);
        const DimStyle* s = store.dimstyle(d->style);
        const DimGeometry g = compute_dim_geometry(*d, s != nullptr ? *s : DimStyle{}, Rgb{});
        bool found = false;
        double best_d2 = 0.0;
        const auto consider_list = [&](const std::vector<Vec2>& v) {
            for (std::size_t i = 0; i + 1 < v.size(); i += 2) {
                const Vec2 cp = closest_on_segment(v[i], v[i + 1], query);
                const double d2 = length_squared(query - cp);
                if (!found || d2 < best_d2) {
                    found = true;
                    best_d2 = d2;
                    out_point = cp;
                }
            }
        };
        consider_list(g.ext_lines);
        consider_list(g.dim_lines);
        consider_list(g.arrow_lines);
        return found;
    }
    case EntityKind::Leader: {
        const LeaderData* l = store.leader(entity);
        out_point = closest_on_segment(l->tip, l->knee, query);
        return true;
    }
    case EntityKind::MText:
    case EntityKind::MLeader: {
        // Nearest point on the entity's pick outline (box / leader polyline).
        std::vector<Vec2> chain;
        tessellate(store, entity, kDefaultTessTolerance, chain);
        bool found = false;
        double best = 0.0;
        for (std::size_t i = 1; i < chain.size(); ++i) {
            const Vec2 cp = closest_on_segment(chain[i - 1], chain[i], query);
            const double d2 = length_squared(query - cp);
            if (!found || d2 < best) {
                found = true;
                best = d2;
                out_point = cp;
            }
        }
        return found;
    }
    case EntityKind::Insert: {
        // Nearest point on the instance's real (transformed) geometry -- segment pairs,
        // stepped by 2 so empty gaps between primitives are never falsely hit.
        const InsertData* in = store.insert(entity);
        std::vector<InsertSeg> segs;
        resolve_insert(store, *in, kDefaultTessTolerance, segs);
        bool found = false;
        double best = 0.0;
        for (const InsertSeg& s : segs) {
            const Vec2 cp = closest_on_segment(s.a, s.b, query);
            const double d2 = length_squared(query - cp);
            if (!found || d2 < best) {
                found = true;
                best = d2;
                out_point = cp;
            }
        }
        return found;
    }
    }
    return false;
}

// Proper polyline offset: offset each segment (straight -> parallel line; arc/bulge ->
// concentric arc) by `distance` to the side the pick lies on, then re-MITER every corner
// as the intersection of the two adjacent offset curves -- so edges stay at distance d
// and corners meet cleanly (no trapezoid). Reuses the Ph10 intersection primitives
// (line/line, line/circle). Bulges (arc segments) are preserved. Returns false if the
// offset collapses/folds (too large for the polyline), leaving the store unchanged.
static bool offset_polyline(const GeometryStore& store, const PolylineData& p, double distance,
                            Vec2 side, Command& out) {
    const std::span<const Vec2> v = store.vertices_of(p);
    const std::span<const double> bl = store.bulges_of(p);
    const std::size_t n = v.size();
    if (n < 2) {
        return false;
    }
    const bool closed = p.closed;
    const std::size_t nseg = closed ? n : n - 1;
    if (nseg < 1) {
        return false;
    }
    const auto bulge_at = [&](std::size_t i) { return bl.empty() ? 0.0 : bl[i]; };

    // Offset side: +1 = left of travel, -1 = right. Take it from the segment whose chord
    // is nearest the pick (robust for open/closed polylines and any first segment).
    double sign = 1.0;
    {
        double best = std::numeric_limits<double>::max();
        for (std::size_t i = 0; i < nseg; ++i) {
            const Vec2 a = v[i];
            const Vec2 b = v[(i + 1) % n];
            const Vec2 cp = closest_on_segment(a, b, side);
            const double dd = length(side - cp);
            if (dd < best) {
                best = dd;
                const Vec2 dir = normalized(b - a);
                const Vec2 nrm{-dir.y, dir.x}; // left normal of travel
                sign = dot(side - cp, nrm) >= 0.0 ? 1.0 : -1.0;
            }
        }
    }

    // One offset curve per segment: a parallel line (straight) or a concentric circle (arc).
    struct OffSeg {
        bool arc = false;
        Vec2 s{}, e{};      // naive offset endpoints (parallel / radially-scaled)
        Vec2 center{};      // arc: circle centre
        double radius = 0.0; // arc: offset radius
        double bulge = 0.0; // output segment bulge (0 = straight)
        Vec2 in_dir{};      // input chord direction (fold check)
    };
    std::vector<OffSeg> seg(nseg);
    for (std::size_t i = 0; i < nseg; ++i) {
        const Vec2 a = v[i];
        const Vec2 b = v[(i + 1) % n];
        const double bg = bulge_at(i);
        OffSeg os;
        os.in_dir = normalized(b - a);
        if (bg == 0.0) {
            const Vec2 dir = normalized(b - a);
            const Vec2 nrm{-dir.y, dir.x};
            const Vec2 d = nrm * (sign * distance);
            os.s = a + d;
            os.e = b + d;
        } else {
            const BulgeArc ba = arc_from_bulge(a, b, bg);
            const double sweep_sign = ba.sweep >= 0.0 ? 1.0 : -1.0;
            // Centre is left of travel for a CCW arc, right for CW; offsetting toward the
            // centre shrinks the radius, away grows it.
            const double r2 = ba.radius - sign * sweep_sign * distance;
            if (r2 <= kIntersectEps) {
                return false; // arc collapses -> offset too large
            }
            const double k = r2 / ba.radius;
            os.arc = true;
            os.center = ba.center;
            os.radius = r2;
            os.s = ba.center + (a - ba.center) * k;
            os.e = ba.center + (b - ba.center) * k;
            os.bulge = bg; // concentric arc: same sweep -> same bulge
        }
        seg[i] = os;
    }

    // Re-miter each corner: vertex j is the intersection of seg[j-1] and seg[j].
    const std::size_t nv = closed ? nseg : nseg + 1;
    std::vector<Vec2> pts(nv);
    std::vector<double> bulges(nv, 0.0);

    const auto corner = [](const OffSeg& A, const OffSeg& B, Vec2 naive, Vec2& res) -> bool {
        if (!A.arc && !B.arc) {
            Vec2 hit{};
            if (NativeKernel2D::line_line_intersection(A.s, A.e, B.s, B.e, hit)) {
                res = hit;
            } else {
                res = naive; // parallel (collinear continuation): the shared point
            }
            return true;
        }
        if (A.arc != B.arc) {
            const OffSeg& ln = A.arc ? B : A;
            const OffSeg& ac = A.arc ? A : B;
            Vec2 p0{};
            Vec2 p1{};
            const int k = NativeKernel2D::line_circle_intersection(ln.s, ln.e, ac.center, ac.radius,
                                                                   p0, p1);
            if (k == 0) {
                return false; // offset line misses the offset arc -> too large
            }
            res = (k == 1 || length(p0 - naive) <= length(p1 - naive)) ? p0 : p1;
            return true;
        }
        // arc/arc: intersect the two offset circles; pick the hit nearest the ideal
        // corner. (Reachable via JOIN of two arcs or a DXF polyline with consecutive
        // bulges.) No intersection -> the offset folded this corner: fail gracefully.
        Vec2 q0{};
        Vec2 q1{};
        const int k = NativeKernel2D::circle_circle_intersection(A.center, A.radius, B.center,
                                                                 B.radius, q0, q1);
        if (k == 0) {
            return false;
        }
        res = (k == 1 || length(q0 - naive) <= length(q1 - naive)) ? q0 : q1;
        return true;
    };

    if (closed) {
        for (std::size_t j = 0; j < nseg; ++j) {
            const OffSeg& A = seg[(j + nseg - 1) % nseg];
            const OffSeg& B = seg[j];
            Vec2 c{};
            if (!corner(A, B, (A.e + B.s) * 0.5, c)) {
                return false;
            }
            pts[j] = c;
            bulges[j] = B.bulge;
        }
    } else {
        pts[0] = seg[0].s;
        for (std::size_t j = 1; j < nseg; ++j) {
            Vec2 c{};
            if (!corner(seg[j - 1], seg[j], (seg[j - 1].e + seg[j].s) * 0.5, c)) {
                return false;
            }
            pts[j] = c;
        }
        pts[nseg] = seg[nseg - 1].e;
        for (std::size_t i = 0; i < nseg; ++i) {
            bulges[i] = seg[i].bulge;
        }
    }

    // Fold detection: a valid offset keeps each segment's direction. A reversed or
    // zero-length output segment means the offset is too large (it self-intersects).
    for (std::size_t i = 0; i < nseg; ++i) {
        const Vec2 a = pts[i];
        const Vec2 b = pts[(i + 1) % nv];
        const Vec2 d = b - a;
        if (length(d) < kIntersectEps || dot(normalized(d), seg[i].in_dir) <= 0.0) {
            return false;
        }
    }

    AddPolylineCommand cmd;
    cmd.points = std::move(pts);
    cmd.closed = closed;
    cmd.group = 0;
    cmd.props = p.props;
    if (std::any_of(bulges.begin(), bulges.end(), [](double b) { return b != 0.0; })) {
        cmd.bulges = std::move(bulges);
    }
    out = cmd;
    return true;
}

bool NativeKernel2D::offset(const GeometryStore& store, EntityHandle entity, double distance,
                            Vec2 side, Command& out) const {
    if (!store.is_valid(entity) || distance <= 0.0) {
        return false;
    }
    switch (entity.kind) {
    case EntityKind::Line: {
        const LineData* l = store.line(entity);
        const Vec2 dir = normalized(l->b - l->a);
        const Vec2 normal{-dir.y, dir.x};
        const double sign = dot(side - l->a, normal) >= 0.0 ? 1.0 : -1.0;
        const Vec2 d = normal * (sign * distance);
        out = AddLineCommand{l->a + d, l->b + d, 0};
        return true;
    }
    case EntityKind::Circle: {
        const CircleData* c = store.circle(entity);
        const bool outward = length(side - c->center) > c->radius;
        const double r = outward ? c->radius + distance : c->radius - distance;
        if (r <= 0.0) {
            return false;
        }
        out = AddCircleCommand{c->center, r, 0};
        return true;
    }
    case EntityKind::Arc: {
        const ArcData* arc = store.arc(entity);
        const bool outward = length(side - arc->center) > arc->radius;
        const double r = outward ? arc->radius + distance : arc->radius - distance;
        if (r <= 0.0) {
            return false;
        }
        out = AddArcCommand{arc->center, r, arc->start_angle, arc->end_angle, 0};
        return true;
    }
    case EntityKind::Polyline: {
        const PolylineData* p = store.polyline(entity);
        return offset_polyline(store, *p, distance, side, out);
    }
    case EntityKind::Point:
    case EntityKind::Spline:
    case EntityKind::Text:
    case EntityKind::Dimension:
    case EntityKind::Leader:
    case EntityKind::MText:
    case EntityKind::MLeader:
    case EntityKind::Insert: // offsetting a block reference is not defined
        break;
    }
    return false;
}

void NativeKernel2D::intersect(const GeometryStore& store, EntityHandle a, EntityHandle b,
                               std::vector<Vec2>& out) const {
    if (!store.is_valid(a) || !store.is_valid(b)) {
        return;
    }
    const std::size_t base = out.size();

    // Robust analytic paths for the common pairs (segment-bounded). Order a,b so
    // a line, when present, is `a`.
    EntityHandle e0 = a;
    EntityHandle e1 = b;
    if (e1.kind == EntityKind::Line && e0.kind != EntityKind::Line) {
        std::swap(e0, e1);
    }
    if (e0.kind == EntityKind::Line) {
        const LineData* l = store.line(e0);
        if (e1.kind == EntityKind::Line) { // line x line (exact)
            const LineData* m = store.line(e1);
            Vec2 hit{};
            if (segment_intersection(l->a, l->b, m->a, m->b, hit)) {
                push_unique(out, base, hit);
            }
            return;
        }
        if (e1.kind == EntityKind::Circle) { // line x circle (exact)
            const CircleData* c = store.circle(e1);
            seg_circle_hits(l->a, l->b, c->center, c->radius, out, base, nullptr);
            return;
        }
        if (e1.kind == EntityKind::Arc) { // line x arc (exact, sweep-filtered)
            const ArcData* arc = store.arc(e1);
            seg_circle_hits(l->a, l->b, arc->center, arc->radius, out, base, arc);
            return;
        }
    }

    // Fallback: tessellate both and cross every segment pair (arc x arc,
    // polyline/spline pairs -- approximate to the tessellation tolerance).
    std::vector<Vec2> pa;
    std::vector<Vec2> pb;
    tessellate(store, a, kDefaultTessTolerance, pa);
    tessellate(store, b, kDefaultTessTolerance, pb);
    if (pa.size() < 2 || pb.size() < 2) {
        return; // point-like entities produce no intersection segments
    }
    Vec2 hit{};
    for (std::size_t i = 1; i < pa.size(); ++i) {
        for (std::size_t j = 1; j < pb.size(); ++j) {
            if (segment_intersection(pa[i - 1], pa[i], pb[j - 1], pb[j], hit)) {
                push_unique(out, base, hit);
            }
        }
    }
}

bool NativeKernel2D::line_line_intersection(Vec2 a0, Vec2 a1, Vec2 b0, Vec2 b1, Vec2& out) {
    const Vec2 r = a1 - a0;
    const Vec2 s = b1 - b0;
    const double rxs = cross(r, s);
    if (std::abs(rxs) < 1e-12) {
        return false; // parallel
    }
    const double t = cross(b0 - a0, s) / rxs;
    out = a0 + r * t;
    return true;
}

int NativeKernel2D::line_circle_intersection(Vec2 a, Vec2 b, Vec2 center, double radius, Vec2& p0,
                                             Vec2& p1) {
    const Vec2 d = b - a;
    const double A = dot(d, d);
    if (A < 1e-18) {
        return 0;
    }
    const Vec2 f = a - center;
    const double B = 2.0 * dot(f, d);
    const double C = dot(f, f) - radius * radius;
    double disc = B * B - 4.0 * A * C;
    if (disc < 0.0) {
        return 0;
    }
    if (disc < 1e-18) {
        p0 = a + d * (-B / (2.0 * A));
        return 1;
    }
    disc = std::sqrt(disc);
    p0 = a + d * ((-B - disc) / (2.0 * A));
    p1 = a + d * ((-B + disc) / (2.0 * A));
    return 2;
}

int NativeKernel2D::circle_circle_intersection(Vec2 c0, double r0, Vec2 c1, double r1, Vec2& p0,
                                               Vec2& p1) {
    const Vec2 d = c1 - c0;
    const double dist2 = dot(d, d);
    const double dist = std::sqrt(dist2);
    if (dist < 1e-12) {
        return 0; // concentric (or coincident) -- no isolated intersection points
    }
    if (dist > r0 + r1 + 1e-9 || dist < std::abs(r0 - r1) - 1e-9) {
        return 0; // circles are separate, or one is wholly inside the other
    }
    // Distance from c0 to the radical line, along d; h = half-chord perpendicular to d.
    const double a = (dist2 + r0 * r0 - r1 * r1) / (2.0 * dist);
    const double h2 = r0 * r0 - a * a;
    const Vec2 mid = c0 + d * (a / dist);
    if (h2 <= 1e-18) {
        p0 = mid;
        return 1; // tangent
    }
    const double h = std::sqrt(h2);
    const Vec2 perp{-d.y / dist * h, d.x / dist * h};
    p0 = mid + perp;
    p1 = mid - perp;
    return 2;
}

} // namespace musacad::core

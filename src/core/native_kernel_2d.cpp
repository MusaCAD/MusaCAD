#include "musacad/core/native_kernel_2d.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "musacad/core/geometry_store.hpp"

namespace musacad::core {

namespace {

constexpr double kIntersectEps = 1e-9;

/// Number of chord segments to approximate an arc of the given sweep within a
/// chordal tolerance.
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
    return n < 1 ? std::size_t{1} : n;
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
        out.assign(verts.begin(), verts.end());
        if (pl->closed && verts.size() >= 2) {
            out.push_back(verts.front());
        }
        break;
    }
    case EntityKind::Spline: {
        const SplineData* sp = store.spline(entity);
        tessellate_spline(store.control_points_of(*sp), sp->degree, out);
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
    }
    return false;
}

void NativeKernel2D::intersect(const GeometryStore& store, EntityHandle a, EntityHandle b,
                               std::vector<Vec2>& out) const {
    if (!store.is_valid(a) || !store.is_valid(b)) {
        return;
    }
    std::vector<Vec2> pa;
    std::vector<Vec2> pb;
    tessellate(store, a, kDefaultTessTolerance, pa);
    tessellate(store, b, kDefaultTessTolerance, pb);
    if (pa.size() < 2 || pb.size() < 2) {
        return; // point-like entities produce no intersection segments
    }
    const std::size_t base = out.size();
    Vec2 hit{};
    for (std::size_t i = 1; i < pa.size(); ++i) {
        for (std::size_t j = 1; j < pb.size(); ++j) {
            if (segment_intersection(pa[i - 1], pa[i], pb[j - 1], pb[j], hit)) {
                push_unique(out, base, hit);
            }
        }
    }
}

} // namespace musacad::core

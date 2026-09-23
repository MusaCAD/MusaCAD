// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Circles from constraints, as CIRCLE's construction methods need them: through three
// points (3P), tangent to two objects with a radius (Ttr), and tangent to three objects
// (Tan, Tan, Tan -- Apollonius' problem). Objects are lines or circles (an arc counts as
// its circle, as AutoCAD treats it). Where several circles satisfy the constraints, the
// one whose tangent points lie nearest the pick points is chosen, which is how AutoCAD
// resolves the branch from where you clicked.
#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::core {

/// A line (through `a` and `b`) or a circle, as a tangency constraint.
struct TangentObject {
    bool is_line = true;
    Vec2 a{};
    Vec2 b{1.0, 0.0};
    Vec2 center{};
    double radius = 0.0;

    [[nodiscard]] static TangentObject line(Vec2 a, Vec2 b) noexcept {
        TangentObject o;
        o.is_line = true;
        o.a = a;
        o.b = b;
        return o;
    }
    [[nodiscard]] static TangentObject circle(Vec2 c, double r) noexcept {
        TangentObject o;
        o.is_line = false;
        o.center = c;
        o.radius = r;
        return o;
    }
    /// The point of this object nearest `p` (the tangent point for a circle centred at p).
    [[nodiscard]] Vec2 closest(Vec2 p) const noexcept {
        if (is_line) {
            const Vec2 d = b - a;
            const double l2 = length_squared(d);
            if (l2 < 1e-24) {
                return a;
            }
            return a + d * (dot(p - a, d) / l2);
        }
        const Vec2 d = p - center;
        const double l = length(d);
        return l < 1e-12 ? center + Vec2{radius, 0.0} : center + d * (radius / l);
    }
};

struct TangentCircle {
    Vec2 center{};
    double radius = 0.0;
};

/// The circle through three points; false when they are collinear.
inline bool circumcircle(Vec2 a, Vec2 b, Vec2 c, Vec2& center, double& radius) noexcept {
    const double d = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    if (std::abs(d) < 1e-12) {
        return false;
    }
    const double a2 = length_squared(a);
    const double b2 = length_squared(b);
    const double c2 = length_squared(c);
    center = {(a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d,
              (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d};
    radius = distance(center, a);
    return radius > 0.0;
}

namespace tangent_detail {

inline int line_line(Vec2 a0, Vec2 a1, Vec2 b0, Vec2 b1, Vec2* out) noexcept {
    const Vec2 r = a1 - a0;
    const Vec2 s = b1 - b0;
    const double den = r.x * s.y - r.y * s.x;
    if (std::abs(den) < 1e-12 * (length(r) * length(s) + 1e-300)) {
        return 0;
    }
    const Vec2 qp = b0 - a0;
    const double t = (qp.x * s.y - qp.y * s.x) / den;
    out[0] = a0 + r * t;
    return 1;
}

inline int line_circle(Vec2 a, Vec2 b, Vec2 c, double r, Vec2* out) noexcept {
    const Vec2 d = b - a;
    const double l2 = length_squared(d);
    if (l2 < 1e-24) {
        return 0;
    }
    const Vec2 f = a - c;
    const double bq = 2.0 * dot(f, d);
    const double cq = length_squared(f) - r * r;
    double disc = bq * bq - 4.0 * l2 * cq;
    if (disc < -1e-9 * l2 * r * r) {
        return 0;
    }
    disc = std::sqrt(std::max(disc, 0.0));
    const double t0 = (-bq - disc) / (2.0 * l2);
    const double t1 = (-bq + disc) / (2.0 * l2);
    out[0] = a + d * t0;
    if (disc < 1e-12) {
        return 1;
    }
    out[1] = a + d * t1;
    return 2;
}

inline int circle_circle(Vec2 c0, double r0, Vec2 c1, double r1, Vec2* out) noexcept {
    const double d = distance(c0, c1);
    if (d < 1e-12 || d > r0 + r1 + 1e-9 || d < std::abs(r0 - r1) - 1e-9) {
        return 0;
    }
    const double a = (r0 * r0 - r1 * r1 + d * d) / (2.0 * d);
    const double h2 = r0 * r0 - a * a;
    const double h = std::sqrt(std::max(h2, 0.0));
    const Vec2 dir = (c1 - c0) * (1.0 / d);
    const Vec2 m = c0 + dir * a;
    const Vec2 n{-dir.y, dir.x};
    out[0] = m + n * h;
    if (h < 1e-12) {
        return 1;
    }
    out[1] = m - n * h;
    return 2;
}

/// The loci of centres of circles of radius r tangent to `o`: two parallel lines, or one
/// or two concentric circles.
inline std::vector<TangentObject> centre_loci(const TangentObject& o, double r) {
    std::vector<TangentObject> out;
    if (o.is_line) {
        const Vec2 d = o.b - o.a;
        const double l = length(d);
        if (l < 1e-12) {
            return out;
        }
        const Vec2 n{-d.y / l, d.x / l};
        out.push_back(TangentObject::line(o.a + n * r, o.b + n * r));
        out.push_back(TangentObject::line(o.a - n * r, o.b - n * r));
        return out;
    }
    out.push_back(TangentObject::circle(o.center, o.radius + r));
    if (std::abs(o.radius - r) > 1e-9) {
        out.push_back(TangentObject::circle(o.center, std::abs(o.radius - r)));
    }
    return out;
}

inline int intersect(const TangentObject& p, const TangentObject& q, Vec2* out) noexcept {
    if (p.is_line && q.is_line) {
        return line_line(p.a, p.b, q.a, q.b, out);
    }
    if (p.is_line) {
        return line_circle(p.a, p.b, q.center, q.radius, out);
    }
    if (q.is_line) {
        return line_circle(q.a, q.b, p.center, p.radius, out);
    }
    return circle_circle(p.center, p.radius, q.center, q.radius, out);
}

} // namespace tangent_detail

/// Ttr: the circle of radius `r` tangent to `o1` and `o2`, on the branch whose tangent
/// points lie nearest `pick1` / `pick2`. Empty when no such circle exists (AutoCAD:
/// "Circle does not exist.").
inline std::optional<TangentCircle> circle_tan_tan_radius(const TangentObject& o1, Vec2 pick1,
                                                          const TangentObject& o2, Vec2 pick2,
                                                          double r) {
    if (!(r > 0.0)) {
        return std::nullopt;
    }
    std::optional<TangentCircle> best;
    double best_score = 0.0;
    for (const TangentObject& l1 : tangent_detail::centre_loci(o1, r)) {
        for (const TangentObject& l2 : tangent_detail::centre_loci(o2, r)) {
            Vec2 pts[2];
            int n = tangent_detail::intersect(l1, l2, pts);
            if (n == 0 && l1.is_line && l2.is_line &&
                distance(l1.closest(l2.a), l2.a) < 1e-9 * (1.0 + r)) {
                // The two loci coincide (parallel objects 2r apart): any centre on that
                // line works; the one between the picks is the one meant.
                pts[0] = l1.closest((pick1 + pick2) * 0.5);
                n = 1;
            }
            for (int i = 0; i < n; ++i) {
                const Vec2 c = pts[i];
                const double score = distance(o1.closest(c), pick1) + distance(o2.closest(c), pick2);
                if (!best || score < best_score) {
                    best = TangentCircle{c, r};
                    best_score = score;
                }
            }
        }
    }
    return best;
}

/// Tan, Tan, Tan: the circle tangent to three objects, on the branch whose tangent points
/// lie nearest the picks. Each object admits two tangency sides; every combination is
/// solved (a Newton iteration from the picks' centroid -- exact in one step for three
/// lines) and the converged solutions are ranked by the picks.
inline std::optional<TangentCircle> circle_tan_tan_tan(const TangentObject& o1, Vec2 pick1,
                                                       const TangentObject& o2, Vec2 pick2,
                                                       const TangentObject& o3, Vec2 pick3) {
    const std::array<const TangentObject*, 3> objs{&o1, &o2, &o3};
    const std::array<Vec2, 3> picks{pick1, pick2, pick3};
    const Vec2 c0 = (pick1 + pick2 + pick3) * (1.0 / 3.0);
    double r0 = 0.0;
    double scale = 0.0;
    for (int i = 0; i < 3; ++i) {
        r0 += distance(objs[static_cast<std::size_t>(i)]->closest(c0), c0) / 3.0;
        scale = std::max(scale, distance(picks[static_cast<std::size_t>(i)], c0));
    }
    if (!(r0 > 0.0)) {
        r0 = std::max(scale, 1.0);
    }
    scale = std::max(scale, r0);
    std::optional<TangentCircle> best;
    double best_score = 0.0;
    for (int combo = 0; combo < 8; ++combo) {
        double cx = c0.x;
        double cy = c0.y;
        double r = r0;
        bool ok = false;
        for (int it = 0; it < 60; ++it) {
            double f[3];
            double j[3][3];
            for (int i = 0; i < 3; ++i) {
                const TangentObject& o = *objs[static_cast<std::size_t>(i)];
                const double s = ((combo >> i) & 1) != 0 ? -1.0 : 1.0;
                if (o.is_line) {
                    const Vec2 d = o.b - o.a;
                    const double l = length(d);
                    const Vec2 n = l > 1e-12 ? Vec2{-d.y / l, d.x / l} : Vec2{0.0, 1.0};
                    f[i] = n.x * (cx - o.a.x) + n.y * (cy - o.a.y) - s * r;
                    j[i][0] = n.x;
                    j[i][1] = n.y;
                    j[i][2] = -s;
                } else {
                    const double dx = cx - o.center.x;
                    const double dy = cy - o.center.y;
                    const double rr = o.radius + s * r;
                    f[i] = dx * dx + dy * dy - rr * rr;
                    j[i][0] = 2.0 * dx;
                    j[i][1] = 2.0 * dy;
                    j[i][2] = -2.0 * s * rr;
                }
            }
            const double det = j[0][0] * (j[1][1] * j[2][2] - j[1][2] * j[2][1]) -
                               j[0][1] * (j[1][0] * j[2][2] - j[1][2] * j[2][0]) +
                               j[0][2] * (j[1][0] * j[2][1] - j[1][1] * j[2][0]);
            if (std::abs(det) < 1e-18) {
                break;
            }
            const auto solve = [&](int col) {
                double m[3][3];
                for (int a = 0; a < 3; ++a) {
                    for (int b = 0; b < 3; ++b) {
                        m[a][b] = (b == col) ? -f[a] : j[a][b];
                    }
                }
                return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                        m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                        m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])) /
                       det;
            };
            const double dx = solve(0);
            const double dy = solve(1);
            const double dr = solve(2);
            cx += dx;
            cy += dy;
            r += dr;
            if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(r) ||
                std::abs(cx) > 1e6 * scale + 1e6) {
                break;
            }
            if (std::abs(dx) + std::abs(dy) + std::abs(dr) < 1e-11 * scale) {
                ok = true;
                break;
            }
        }
        if (!ok || !(r > 1e-9 * scale)) {
            continue;
        }
        const Vec2 c{cx, cy};
        bool tangent = true;
        double score = 0.0;
        for (int i = 0; i < 3; ++i) {
            const TangentObject& o = *objs[static_cast<std::size_t>(i)];
            const double d = distance(o.closest(c), c);
            if (std::abs(d - r) > 1e-6 * scale) {
                tangent = false; // a spurious root (a circle "tangent" from the wrong side)
            }
            score += distance(o.closest(c), picks[static_cast<std::size_t>(i)]);
        }
        if (!tangent) {
            continue;
        }
        if (!best || score < best_score) {
            best = TangentCircle{c, r};
            best_score = score;
        }
    }
    return best;
}

} // namespace musacad::core

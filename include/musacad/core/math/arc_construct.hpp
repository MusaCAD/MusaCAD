// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Arcs from the constraints ARC's construction methods take: three points; start,
// centre and an end / included angle / chord length; start, end and a centre / included
// angle / tangent direction / radius; and the tangent continuation of the previous
// segment. Every result is the stored form -- a centre, a radius and the counter-
// clockwise sweep from `start` to `end` -- so a clockwise construction is the same arc
// swept from its other end. `end_point` and `end_tangent` describe the arc as it was
// drawn (where it finishes and which way it is heading there), which is what LINE and
// ARC continue from.
#pragma once

#include <cmath>
#include <optional>

#include "musacad/core/math/math.hpp"
#include "musacad/core/math/tangent_circle.hpp"

namespace musacad::core {

struct ConstructedArc {
    Vec2 center{};
    double radius = 0.0;
    double start = 0.0; ///< radians; the stored arc runs counter-clockwise from here
    double end = 0.0;   ///< ... to here
    Vec2 end_point{};   ///< where the drawn arc finishes
    double end_tangent = 0.0; ///< the heading at end_point, in the drawn direction
    double sweep = 0.0; ///< the signed sweep as drawn (+ counter-clockwise)
    /// The polyline bulge of this arc as a segment from the drawn start to end_point.
    [[nodiscard]] double bulge() const noexcept { return std::tan(sweep * 0.25); }
};

namespace arc_detail {
inline double bearing(Vec2 from, Vec2 to) noexcept {
    return std::atan2(to.y - from.y, to.x - from.x);
}
inline double wrap_positive(double a) noexcept {
    a = std::fmod(a, kTwoPi);
    return a < 0.0 ? a + kTwoPi : a;
}
/// The arc from angle `a` sweeping `sweep` (signed: + counter-clockwise) about `c`.
inline ConstructedArc from_sweep(Vec2 c, double r, double a, double sweep) noexcept {
    ConstructedArc out;
    out.center = c;
    out.radius = r;
    const double b = a + sweep;
    if (sweep >= 0.0) {
        out.start = a;
        out.end = b;
    } else {
        out.start = b;
        out.end = a;
    }
    out.end_point = {c.x + r * std::cos(b), c.y + r * std::sin(b)};
    out.end_tangent = sweep >= 0.0 ? b + kHalfPi : b - kHalfPi;
    out.sweep = sweep;
    return out;
}
} // namespace arc_detail

/// Three points on the arc, in the order picked (the sweep passes through the second).
inline std::optional<ConstructedArc> arc_three_points(Vec2 s, Vec2 m, Vec2 e) {
    Vec2 c;
    double r = 0.0;
    if (!circumcircle(s, m, e, c, r)) {
        return std::nullopt;
    }
    const double as = arc_detail::bearing(c, s);
    const double am = arc_detail::wrap_positive(arc_detail::bearing(c, m) - as);
    const double ae = arc_detail::wrap_positive(arc_detail::bearing(c, e) - as);
    // Counter-clockwise from s when the middle point comes before the end that way,
    // else clockwise (the same points, the sweep the other way round).
    const double sweep = am < ae ? ae : ae - kTwoPi;
    return arc_detail::from_sweep(c, r, as, sweep);
}

/// Start, centre, end: from the start to the ray from the centre through `e`,
/// counter-clockwise unless `clockwise` (Ctrl).
inline std::optional<ConstructedArc> arc_start_center_end(Vec2 s, Vec2 c, Vec2 e, bool clockwise) {
    const double r = distance(s, c);
    if (!(r > 0.0) || distance(e, c) <= 0.0) {
        return std::nullopt;
    }
    const double as = arc_detail::bearing(c, s);
    double sweep = arc_detail::wrap_positive(arc_detail::bearing(c, e) - as);
    if (clockwise) {
        sweep -= kTwoPi;
    }
    if (std::abs(sweep) < 1e-12) {
        return std::nullopt;
    }
    return arc_detail::from_sweep(c, r, as, sweep);
}

/// Start, centre, included angle (radians; negative = clockwise; Ctrl flips it).
inline std::optional<ConstructedArc> arc_start_center_angle(Vec2 s, Vec2 c, double angle, bool clockwise) {
    const double r = distance(s, c);
    if (!(r > 0.0) || std::abs(angle) < 1e-12 || std::abs(angle) > kTwoPi + 1e-12) {
        return std::nullopt;
    }
    return arc_detail::from_sweep(c, r, arc_detail::bearing(c, s), clockwise ? -angle : angle);
}

/// Start, centre, chord length: the minor arc for a positive length, the major arc for a
/// negative one; counter-clockwise unless `clockwise`.
inline std::optional<ConstructedArc> arc_start_center_length(Vec2 s, Vec2 c, double chord, bool clockwise) {
    const double r = distance(s, c);
    const double l = std::abs(chord);
    if (!(r > 0.0) || !(l > 0.0) || l > 2.0 * r + 1e-9) {
        return std::nullopt;
    }
    double sweep = 2.0 * std::asin(std::min(1.0, l / (2.0 * r)));
    if (chord < 0.0) {
        sweep = kTwoPi - sweep;
    }
    return arc_detail::from_sweep(c, r, arc_detail::bearing(c, s), clockwise ? -sweep : sweep);
}

/// Start, end, included angle (radians; negative = clockwise; Ctrl flips it).
inline std::optional<ConstructedArc> arc_start_end_angle(Vec2 s, Vec2 e, double angle, bool clockwise) {
    const double chord = distance(s, e);
    double sweep = clockwise ? -angle : angle;
    if (!(chord > 0.0) || std::abs(sweep) < 1e-12 || std::abs(sweep) >= kTwoPi - 1e-12) {
        return std::nullopt;
    }
    const double half = std::abs(sweep) * 0.5;
    const double r = chord / (2.0 * std::sin(half));
    // The centre sits on the chord's bisector: to the left of s->e for a counter-clockwise
    // sweep (and on the far side when the arc is major), mirrored for clockwise.
    const Vec2 d = (e - s) * (1.0 / chord);
    const Vec2 n{-d.y, d.x};
    const double h = r * std::cos(half) * (sweep > 0.0 ? 1.0 : -1.0);
    const Vec2 m = (s + e) * 0.5;
    const Vec2 c = m + n * h;
    return arc_detail::from_sweep(c, r, arc_detail::bearing(c, s), sweep);
}

/// Start, end, the tangent direction at the start (radians). The arc leaves `s` heading
/// `direction` and reaches `e`; there is none when `e` lies on that tangent line.
inline std::optional<ConstructedArc> arc_start_end_direction(Vec2 s, Vec2 e, double direction) {
    const Vec2 t{std::cos(direction), std::sin(direction)};
    const Vec2 n{-t.y, t.x};
    const Vec2 v = e - s;
    const double l2 = length_squared(v);
    const double side = dot(n, v);
    if (!(l2 > 0.0) || std::abs(side) < 1e-12 * std::sqrt(l2)) {
        return std::nullopt;
    }
    const double r = l2 / (2.0 * side); // signed: the centre to the left of the tangent when > 0
    const Vec2 c = s + n * r;
    const double as = arc_detail::bearing(c, s);
    const double ae = arc_detail::bearing(c, e);
    double sweep = arc_detail::wrap_positive(ae - as);
    if (r < 0.0) {
        sweep -= kTwoPi; // clockwise: the centre is on the right
    }
    return arc_detail::from_sweep(c, std::abs(r), as, sweep);
}

/// Start, end, radius: the minor arc counter-clockwise from `s` to `e` for a positive
/// radius, the major arc for a negative one; `clockwise` (Ctrl) mirrors it.
inline std::optional<ConstructedArc> arc_start_end_radius(Vec2 s, Vec2 e, double radius, bool clockwise) {
    const double chord = distance(s, e);
    const double r = std::abs(radius);
    if (!(chord > 0.0) || !(r > 0.0) || r < chord * 0.5 - 1e-9) {
        return std::nullopt;
    }
    const double h = std::sqrt(std::max(0.0, r * r - chord * chord * 0.25));
    const Vec2 d = (e - s) * (1.0 / chord);
    const Vec2 n{-d.y, d.x};
    const double dir = clockwise ? -1.0 : 1.0;
    const double side = (radius >= 0.0 ? 1.0 : -1.0) * dir; // the minor arc's centre lies left of s->e
    const Vec2 c = (s + e) * 0.5 + n * (h * side);
    const double as = arc_detail::bearing(c, s);
    double sweep = arc_detail::wrap_positive(arc_detail::bearing(c, e) - as);
    if (clockwise) {
        sweep -= kTwoPi;
    }
    return arc_detail::from_sweep(c, r, as, sweep);
}

} // namespace musacad::core

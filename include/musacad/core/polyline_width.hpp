// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::core::pline {

// Polyline widths (AutoCAD's PLINE Width / Halfwidth, DXF 40 / 41 / 43). They are kept
// two per vertex: widths[2 * i] is the width the segment LEAVING vertex i starts with and
// widths[2 * i + 1] the width it ends with at the next vertex; an empty span means every
// segment is a plain line. A wide segment is drawn as a filled band centred on the
// segment (a straight one as a trapezoid, an arc as a ring sector whose width runs
// linearly along the sweep); FILLMODE 0 draws the band's outline instead.

[[nodiscard]] inline bool has_width(std::span<const double> widths) noexcept {
    return std::any_of(widths.begin(), widths.end(), [](double w) { return w > 0.0; });
}

/// The one width every segment has, when that is so (0 for a plain polyline).
[[nodiscard]] inline std::optional<double> uniform_width(std::span<const double> widths) noexcept {
    if (widths.empty()) {
        return 0.0;
    }
    const double w = widths.front();
    for (const double v : widths) {
        if (std::abs(v - w) > 1e-12) {
            return std::nullopt;
        }
    }
    return w;
}

[[nodiscard]] inline double max_width(std::span<const double> widths) noexcept {
    double m = 0.0;
    for (const double v : widths) {
        m = std::max(m, v);
    }
    return m;
}

/// Bring `widths` to two per vertex: a pair (or any uniform list) spreads over all the
/// vertices; a list that cannot be matched to the vertices is dropped. Every edit that
/// rebuilds a polyline runs its widths through this, so none can leave them misaligned.
inline void fit_widths(std::vector<double>& widths, std::size_t vertex_count) {
    if (widths.empty() || widths.size() == 2 * vertex_count) {
        if (!has_width(widths)) {
            widths.clear();
        }
        return;
    }
    const std::optional<double> u = uniform_width(widths);
    if (u && *u > 0.0) {
        widths.assign(2 * vertex_count, *u);
    } else {
        widths.clear();
    }
}

namespace detail {
struct ArcOf {
    Vec2 center{};
    double radius = 0.0;
    double start = 0.0; ///< angle of the segment's first vertex
    double sweep = 0.0; ///< signed: + counter-clockwise
};
/// The arc a bulge describes between p0 and p1 (bulge = tan(sweep / 4)).
[[nodiscard]] inline ArcOf arc_of(Vec2 p0, Vec2 p1, double bulge) noexcept {
    ArcOf a;
    const Vec2 chord = p1 - p0;
    const double d = length(chord);
    a.sweep = 4.0 * std::atan(bulge);
    const Vec2 mid{(p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5};
    const Vec2 left{-chord.y / d, chord.x / d};
    const double h = (d * 0.5) * (1.0 - bulge * bulge) / (2.0 * bulge);
    a.center = {mid.x + left.x * h, mid.y + left.y * h};
    a.radius = d * (1.0 + bulge * bulge) / (4.0 * std::abs(bulge));
    a.start = std::atan2(p0.y - a.center.y, p0.x - a.center.x);
    return a;
}
inline void quad(std::vector<Vec2>& out, Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    out.push_back(a);
    out.push_back(b);
    out.push_back(c);
    out.push_back(a);
    out.push_back(c);
    out.push_back(d);
}
/// The two sides of one wide segment as point rows (left row, right row), start to end.
inline void segment_rows(Vec2 p0, Vec2 p1, double bulge, double w0, double w1, double tolerance,
                         std::vector<Vec2>& left, std::vector<Vec2>& right) {
    left.clear();
    right.clear();
    if (std::abs(bulge) <= 1e-12) {
        const Vec2 d = p1 - p0;
        const double len = length(d);
        if (len < 1e-12) {
            return;
        }
        const Vec2 n{-d.y / len, d.x / len};
        left = {p0 + n * (w0 * 0.5), p1 + n * (w1 * 0.5)};
        right = {p0 - n * (w0 * 0.5), p1 - n * (w1 * 0.5)};
        return;
    }
    const ArcOf a = arc_of(p0, p1, bulge);
    if (!(a.radius > 0.0) || !std::isfinite(a.radius)) {
        return;
    }
    const double outer = a.radius + std::max(w0, w1) * 0.5;
    const double tol = std::max(tolerance, 1e-6);
    double step = 2.0 * std::acos(std::clamp(1.0 - tol / std::max(outer, tol), -1.0, 1.0));
    if (!(step > 1e-3)) {
        step = 1e-3;
    }
    const int n = std::clamp(static_cast<int>(std::ceil(std::abs(a.sweep) / step)), 4, 256);
    const double side = a.sweep > 0.0 ? -1.0 : 1.0; // the left of travel is towards the centre when CCW
    for (int i = 0; i <= n; ++i) {
        const double t = static_cast<double>(i) / n;
        const double ang = a.start + a.sweep * t;
        const double half = (w0 + (w1 - w0) * t) * 0.5;
        const Vec2 dir{std::cos(ang), std::sin(ang)};
        left.push_back(a.center + dir * (a.radius + side * half));
        right.push_back(a.center + dir * (a.radius - side * half));
    }
}
} // namespace detail

/// The filled bands of a polyline's wide segments, as triangles (three points each).
/// Two straight neighbours of one width meet in a mitre unless the corner is too sharp;
/// every other joint is butted, as AutoCAD leaves them.
inline void band_triangles(std::span<const Vec2> verts, std::span<const double> bulges,
                           std::span<const double> widths, bool closed, double tolerance,
                           std::vector<Vec2>& out) {
    const std::size_t n = verts.size();
    if (n < 2 || widths.size() != 2 * n) {
        return;
    }
    const std::size_t nseg = closed ? n : n - 1;
    const auto bulge_at = [&](std::size_t i) { return i < bulges.size() ? bulges[i] : 0.0; };
    std::vector<Vec2> left;
    std::vector<Vec2> right;
    for (std::size_t i = 0; i < nseg; ++i) {
        const double w0 = widths[2 * i];
        const double w1 = widths[2 * i + 1];
        if (!(w0 > 0.0) && !(w1 > 0.0)) {
            continue;
        }
        const std::size_t j = (i + 1) % n;
        detail::segment_rows(verts[i], verts[j], bulge_at(i), w0, w1, tolerance, left, right);
        if (left.size() < 2) {
            continue;
        }
        // Mitres with straight neighbours of the same width.
        if (std::abs(bulge_at(i)) <= 1e-12) {
            const auto mitre = [&](std::size_t at, std::size_t other_seg, bool at_start) {
                const std::size_t oj = (other_seg + 1) % n;
                if (std::abs(bulge_at(other_seg)) > 1e-12) {
                    return;
                }
                const double ow = at_start ? widths[2 * other_seg + 1] : widths[2 * other_seg];
                const double w = at_start ? w0 : w1;
                if (std::abs(ow - w) > 1e-12 || !(w > 0.0)) {
                    return;
                }
                const Vec2 d1 = normalized(verts[oj] - verts[other_seg]);
                const Vec2 d2 = normalized(verts[j] - verts[i]);
                const Vec2 in = at_start ? d1 : d2;   // the segment arriving at the joint
                const Vec2 outd = at_start ? d2 : d1; // the one leaving it
                const double cross = in.x * outd.y - in.y * outd.x;
                const double dotp = in.x * outd.x + in.y * outd.y;
                if (std::abs(cross) < 1e-9 || dotp < -0.94) {
                    return; // straight on, or sharper than about 20 degrees: butt
                }
                // The mitre point lies along the bisector of the two normals.
                const Vec2 n1{-in.y, in.x};
                const Vec2 n2{-outd.y, outd.x};
                Vec2 m{n1.x + n2.x, n1.y + n2.y};
                const double ml = length(m);
                if (ml < 1e-9) {
                    return;
                }
                m = m * (1.0 / ml);
                const double reach = (w * 0.5) / std::max(m.x * n2.x + m.y * n2.y, 0.2);
                const Vec2 v = verts[at];
                const std::size_t k = at_start ? 0 : left.size() - 1;
                left[k] = v + m * reach;
                right[k] = v - m * reach;
            };
            if (closed || i > 0) {
                mitre(i, (i + nseg - 1) % nseg, true);
            }
            if (closed || i + 1 < nseg) {
                mitre(j, (i + 1) % nseg, false);
            }
        }
        for (std::size_t k = 1; k < left.size(); ++k) {
            detail::quad(out, left[k - 1], left[k], right[k], right[k - 1]);
        }
    }
}

/// The bands' outlines as line segments (two points each): what FILLMODE 0 draws.
inline void band_outline(std::span<const Vec2> verts, std::span<const double> bulges,
                         std::span<const double> widths, bool closed, double tolerance,
                         std::vector<Vec2>& out) {
    const std::size_t n = verts.size();
    if (n < 2 || widths.size() != 2 * n) {
        return;
    }
    const std::size_t nseg = closed ? n : n - 1;
    std::vector<Vec2> left;
    std::vector<Vec2> right;
    for (std::size_t i = 0; i < nseg; ++i) {
        const double w0 = widths[2 * i];
        const double w1 = widths[2 * i + 1];
        if (!(w0 > 0.0) && !(w1 > 0.0)) {
            continue;
        }
        detail::segment_rows(verts[i], verts[(i + 1) % n], i < bulges.size() ? bulges[i] : 0.0, w0, w1,
                             tolerance, left, right);
        if (left.size() < 2) {
            continue;
        }
        for (std::size_t k = 1; k < left.size(); ++k) {
            out.push_back(left[k - 1]);
            out.push_back(left[k]);
            out.push_back(right[k - 1]);
            out.push_back(right[k]);
        }
        out.push_back(left.front());
        out.push_back(right.front());
        out.push_back(left.back());
        out.push_back(right.back());
    }
}

/// The segments of no width, as line pieces (two points each; arcs flattened): what a
/// polyline that is wide only in places still draws as ordinary line work.
inline void plain_segments(std::span<const Vec2> verts, std::span<const double> bulges,
                           std::span<const double> widths, bool closed, double tolerance,
                           std::vector<Vec2>& out) {
    const std::size_t n = verts.size();
    if (n < 2 || widths.size() != 2 * n) {
        return;
    }
    const std::size_t nseg = closed ? n : n - 1;
    std::vector<Vec2> left;
    std::vector<Vec2> right;
    for (std::size_t i = 0; i < nseg; ++i) {
        if (widths[2 * i] > 0.0 || widths[2 * i + 1] > 0.0) {
            continue;
        }
        // A band of no width: its two sides coincide on the centre line.
        detail::segment_rows(verts[i], verts[(i + 1) % n], i < bulges.size() ? bulges[i] : 0.0, 0.0,
                             0.0, tolerance, left, right);
        for (std::size_t k = 1; k < left.size(); ++k) {
            out.push_back(left[k - 1]);
            out.push_back(left[k]);
        }
    }
}

namespace detail {
/// Where `p` falls on one segment: the fraction along it (0..1) and how far from it.
inline void locate_on_segment(Vec2 p0, Vec2 p1, double bulge, Vec2 p, double& t,
                              double& dist) noexcept {
    const Vec2 d = p1 - p0;
    const double len2 = d.x * d.x + d.y * d.y;
    if (std::abs(bulge) <= 1e-12 || len2 < 1e-24) {
        t = len2 > 1e-24
                ? std::clamp(((p.x - p0.x) * d.x + (p.y - p0.y) * d.y) / len2, 0.0, 1.0)
                : 0.0;
        dist = length(p - Vec2{p0.x + d.x * t, p0.y + d.y * t});
        return;
    }
    const ArcOf a = arc_of(p0, p1, bulge);
    constexpr double kTurn = 6.283185307179586476925286766559;
    double ang = std::atan2(p.y - a.center.y, p.x - a.center.x) - a.start;
    if (a.sweep > 0.0) {
        while (ang < 0.0) {
            ang += kTurn;
        }
        while (ang >= kTurn) {
            ang -= kTurn;
        }
    } else {
        while (ang > 0.0) {
            ang -= kTurn;
        }
        while (ang <= -kTurn) {
            ang += kTurn;
        }
    }
    const double f = ang / a.sweep;
    if (f <= 1.0) {
        t = f;
        dist = std::abs(length(p - a.center) - a.radius);
        return;
    }
    const double d0 = length(p - p0);
    const double d1 = length(p - p1);
    t = d0 <= d1 ? 0.0 : 1.0;
    dist = std::min(d0, d1);
}
/// The point half way along a segment (on the arc when it is one).
[[nodiscard]] inline Vec2 segment_midpoint(Vec2 p0, Vec2 p1, double bulge) noexcept {
    Vec2 mid{(p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5};
    const Vec2 d = p1 - p0;
    const double len = length(d);
    if (std::abs(bulge) > 1e-12 && len > 1e-12) {
        mid = mid + Vec2{d.y / len, -d.x / len} * (bulge * len * 0.5);
    }
    return mid;
}
} // namespace detail

/// The widths a polyline built FROM another one takes (a trimmed or broken piece, the
/// result of a fillet, an offset copy): each new segment is matched to the source
/// segment it lies on (or nearest to) and takes that segment's width where its two ends
/// fall, so a uniform width carries over exactly and a taper keeps its slope. Segments
/// farther than `max_dist` from the source are left as `out` has them. `out` comes back
/// two per new vertex, or empty when nothing is wide.
inline void inherit_widths(std::span<const Vec2> src_verts, std::span<const double> src_bulges,
                           std::span<const double> src_widths, bool src_closed,
                           std::span<const Vec2> verts, std::span<const double> bulges, bool closed,
                           std::vector<double>& out,
                           double max_dist = std::numeric_limits<double>::infinity()) {
    const std::size_t n = src_verts.size();
    const std::size_t m = verts.size();
    if (out.size() != 2 * m) {
        out.assign(2 * m, 0.0);
    }
    if (n >= 2 && m >= 2 && src_widths.size() == 2 * n && has_width(src_widths)) {
        const std::size_t src_nseg = src_closed ? n : n - 1;
        const std::size_t nseg = closed ? m : m - 1;
        const auto src_bulge = [&](std::size_t i) { return i < src_bulges.size() ? src_bulges[i] : 0.0; };
        for (std::size_t k = 0; k < nseg; ++k) {
            const Vec2 a = verts[k];
            const Vec2 b = verts[(k + 1) % m];
            const Vec2 mid = detail::segment_midpoint(a, b, k < bulges.size() ? bulges[k] : 0.0);
            std::size_t best = 0;
            double best_d = std::numeric_limits<double>::infinity();
            for (std::size_t s = 0; s < src_nseg; ++s) {
                double t = 0.0;
                double d = 0.0;
                detail::locate_on_segment(src_verts[s], src_verts[(s + 1) % n], src_bulge(s), mid, t, d);
                if (d < best_d) {
                    best_d = d;
                    best = s;
                }
            }
            if (best_d > max_dist) {
                continue;
            }
            const double w0 = src_widths[2 * best];
            const double w1 = src_widths[2 * best + 1];
            double ta = 0.0;
            double tb = 0.0;
            double unused = 0.0;
            detail::locate_on_segment(src_verts[best], src_verts[(best + 1) % n], src_bulge(best), a, ta,
                                      unused);
            detail::locate_on_segment(src_verts[best], src_verts[(best + 1) % n], src_bulge(best), b, tb,
                                      unused);
            out[2 * k] = w0 + (w1 - w0) * ta;
            out[2 * k + 1] = w0 + (w1 - w0) * tb;
        }
    }
    if (!has_width(out)) {
        out.clear();
    }
}

} // namespace musacad::core::pline

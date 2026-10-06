// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include "musacad/core/math/math.hpp"

// Corner and lobe operations on polylines, shared by every command that rounds,
// chamfers or clouds a vertex chain: the FILLET/CHAMFER commands (on a picked corner),
// RECTANGLE's Fillet/Chamfer options (on all four corners as it is drawn) and REVCLOUD.
// One implementation, so a filleted rectangle's corner is exactly what FILLET would
// have produced afterwards, and bulge signs cannot drift between the two.

namespace musacad::core::polyline_ops {

inline bool fillet_corner(std::vector<Vec2>& pts, std::vector<double>& bulges, bool closed,
                          int sv, double r);
inline bool chamfer_corner(std::vector<Vec2>& pts, bool closed, int sv, double d_prev,
                           double d_next);

/// RECTANG's outline: the four corners from `first` to `other`, turned by `rotation`
/// about `first`, every corner rounded by `fillet_r` or cut by the chamfer distances --
/// through the same routines FILLET and CHAMFER use on a picked corner, so the two can
/// never disagree. Returns false (and the plain rectangle) when the treatment does not
/// fit, which is what AutoCAD does with an oversized radius.
inline bool rectangle_outline(Vec2 first, Vec2 other, double rotation, double fillet_r,
                              double chamfer_d1, double chamfer_d2, std::vector<Vec2>& pts,
                              std::vector<double>& bulges) {
    const auto corners = [&] {
        std::vector<Vec2> c{{first.x, first.y}, {other.x, first.y}, {other.x, other.y}, {first.x, other.y}};
        if (rotation != 0.0) {
            const double cs = std::cos(rotation);
            const double sn = std::sin(rotation);
            for (Vec2& q : c) {
                const double dx = q.x - first.x;
                const double dy = q.y - first.y;
                q = {first.x + dx * cs - dy * sn, first.y + dx * sn + dy * cs};
            }
        }
        return c;
    };
    pts = corners();
    bulges.clear();
    bool shaped = true;
    if (fillet_r > 0.0) {
        bulges.assign(4, 0.0);
        for (int i = 3; i >= 0 && shaped; --i) { // descending: inserts never shift the rest
            shaped = fillet_corner(pts, bulges, true, i, fillet_r);
        }
    } else if (chamfer_d1 > 0.0 || chamfer_d2 > 0.0) {
        for (int i = 3; i >= 0 && shaped; --i) {
            shaped = chamfer_corner(pts, true, i, chamfer_d1, chamfer_d2);
        }
    }
    if (!shaped) {
        pts = corners();
        bulges.clear();
    }
    return shaped;
}

/// Replace vertex `sv` with a tangent arc of radius r, approximated by vertices.
// Rounds corner `sv` with a true arc by replacing the corner vertex with its two
// tangent points and recording the arc as a BULGE on the first -- the geometry
// stays a parametric polyline (no baked facets), so it can be dimensioned and
// re-tessellated at any zoom. `bulges` is grown to match `pts` (zeros = straight).
inline bool fillet_corner(std::vector<Vec2>& pts, std::vector<double>& bulges, bool closed,
                          int sv, double r) {
    const std::size_t n = pts.size();
    const std::size_t s = static_cast<std::size_t>(sv);
    if (n < 3 || (!closed && (sv <= 0 || s >= n - 1)) || r <= 0.0) {
        return false;
    }
    if (bulges.size() != n) {
        bulges.assign(n, 0.0);
    }
    const std::size_t prev = (s + n - 1) % n;
    const std::size_t next = (s + 1) % n;
    const Vec2 V = pts[s];
    const Vec2 uP = normalized(pts[prev] - V);
    const Vec2 uN = normalized(pts[next] - V);
    const double alpha = std::acos(std::clamp(dot(uP, uN), -1.0, 1.0));
    if (alpha < 1e-4 || alpha > kPi - 1e-4) {
        return false;
    }
    const double td = r / std::tan(alpha / 2.0);
    if (td > distance(V, pts[prev]) + 1e-9 || td > distance(V, pts[next]) + 1e-9) {
        return false;
    }
    const Vec2 Tp = V + uP * td; // tangent point on the incoming edge
    const Vec2 Tn = V + uN * td; // tangent point on the outgoing edge
    const Vec2 C = V + normalized(uP + uN) * (r / std::sin(alpha / 2.0));
    double a0 = std::atan2(Tp.y - C.y, Tp.x - C.x);
    const double a1 = std::atan2(Tn.y - C.y, Tn.x - C.x);
    double sweep = a1 - a0;
    while (sweep <= -kPi) {
        sweep += kTwoPi;
    }
    while (sweep > kPi) {
        sweep -= kTwoPi;
    }
    const double bulge = std::tan(sweep / 4.0); // arc Tp->Tn as an AutoCAD bulge
    // Replace V (index s) with Tp, Tn; the prev->Tp edge keeps its bulge, Tp->Tn is
    // the fillet arc, Tn->next keeps what V->next had.
    pts[s] = Tp;
    pts.insert(pts.begin() + static_cast<std::ptrdiff_t>(s) + 1, Tn);
    const double out_bulge = bulges[s]; // old V->next segment bulge
    bulges[s] = bulge;
    bulges.insert(bulges.begin() + static_cast<std::ptrdiff_t>(s) + 1, out_bulge);
    return true;
}

/// Replace vertex `sv` with a bevel: a point d_prev along the edge toward the
/// previous vertex and d_next toward the next. Returns false if it can't fit.
inline bool chamfer_corner(std::vector<Vec2>& pts, bool closed, int sv, double d_prev,
                           double d_next) {
    const std::size_t n = pts.size();
    const std::size_t s = static_cast<std::size_t>(sv);
    if (n < 3 || (!closed && (sv <= 0 || s >= n - 1))) {
        return false;
    }
    const std::size_t prev = (s + n - 1) % n;
    const std::size_t next = (s + 1) % n;
    const Vec2 V = pts[s];
    if (d_prev > distance(V, pts[prev]) + 1e-9 || d_next > distance(V, pts[next]) + 1e-9) {
        return false;
    }
    const Vec2 A = V + normalized(pts[prev] - V) * d_prev;
    const Vec2 B = V + normalized(pts[next] - V) * d_next;
    std::vector<Vec2> out;
    out.reserve(n + 1);
    for (std::size_t i = 0; i < n; ++i) {
        if (i == s) {
            out.push_back(A);
            out.push_back(B);
        } else {
            out.push_back(pts[i]);
        }
    }
    pts = std::move(out);
    return true;
}

/// Revision cloud (AutoCAD REVCLOUD): re-express `path` as a polyline of arcs whose
/// chords are as close as possible to `arc_len`, every lobe bulging OUTWARD (to the
/// right of travel on a CCW-oriented path); `reverse` flips them inward, AutoCAD's
/// "Reverse direction". Each lobe is a 120-degree arc, the look of AutoCAD's Normal
/// style.
///
/// The path is first split into RUNS at its real corners (a turn of more than 15
/// degrees), and each run is then divided into whole lobes of near-equal length laid
/// along the run by arc length. That keeps a rectangle's corners as lobe boundaries, as
/// AutoCAD does, while a densely tessellated circle (many tiny near-collinear edges)
/// becomes one run and gets lobes of the asked size rather than one per tessellation
/// edge. A degenerate path yields nothing.
inline void revcloud_from_path(std::span<const Vec2> path, bool closed, double arc_len, bool reverse,
                               std::vector<Vec2>& verts, std::vector<double>& bulges) {
    verts.clear();
    bulges.clear();
    if (path.size() < 2 || !(arc_len > 1e-9)) {
        return;
    }
    const std::size_t n = path.size();
    // Orientation of the path: signed area for a closed loop, else treat as CCW.
    double area2 = 0.0;
    if (closed) {
        for (std::size_t i = 0; i < n; ++i) {
            const Vec2& a = path[i];
            const Vec2& b = path[(i + 1) % n];
            area2 += a.x * b.y - b.x * a.y;
        }
    }
    const bool ccw = closed ? area2 >= 0.0 : true;
    // Travelling CCW round a loop the interior is on the LEFT, so an outward lobe bulges
    // to the RIGHT of each chord -- which is what a positive (CCW) bulge does.
    const double mag = std::tan(kPi / 6.0);
    double b = ccw ? mag : -mag;
    if (reverse) {
        b = -b;
    }

    // Corner test at vertex i (turn between the edges into and out of it).
    const auto is_corner = [&](std::size_t i) {
        const Vec2 prev = path[(i + n - 1) % n];
        const Vec2 cur = path[i];
        const Vec2 next = path[(i + 1) % n];
        const Vec2 d0 = cur - prev;
        const Vec2 d1 = next - cur;
        const double l0 = length(d0);
        const double l1 = length(d1);
        if (l0 < 1e-12 || l1 < 1e-12) {
            return true;
        }
        const double c = std::clamp(dot(d0, d1) / (l0 * l1), -1.0, 1.0);
        return std::acos(c) > (15.0 * kPi / 180.0);
    };
    // Run starts: every corner; an open path always starts at vertex 0. A closed path
    // with no corners at all (a circle) is one run from vertex 0.
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < n; ++i) {
        const bool corner = closed ? is_corner(i) : (i == 0 || i == n - 1 || is_corner(i));
        if (corner && (closed || i < n - 1)) {
            starts.push_back(i);
        }
    }
    if (starts.empty()) {
        starts.push_back(0);
    }
    const std::size_t n_runs = starts.size();
    for (std::size_t r = 0; r < n_runs; ++r) {
        const std::size_t s0 = starts[r];
        // The run's vertices: from s0 up to the next start (wrapping when closed) or the
        // path's end.
        std::vector<Vec2> run{path[s0]};
        if (closed) {
            const std::size_t s1 = starts[(r + 1) % n_runs];
            for (std::size_t k = (s0 + 1) % n; k != s1; k = (k + 1) % n) {
                run.push_back(path[k]);
            }
            run.push_back(path[s1]);
        } else {
            const std::size_t s1 = (r + 1 < n_runs) ? starts[r + 1] : n - 1;
            for (std::size_t k = s0 + 1; k <= s1; ++k) {
                run.push_back(path[k]);
            }
        }
        // Cumulative length along the run.
        std::vector<double> cum(run.size(), 0.0);
        if (cum.empty()) {
            continue; // run always holds >= 1 vertex; this reassures the optimizer's
                      // null-dereference analysis (cum.back() below) at no runtime cost
        }
        for (std::size_t k = 1; k < run.size(); ++k) {
            cum[k] = cum[k - 1] + length(run[k] - run[k - 1]);
        }
        const double total = cum.back();
        if (total < 1e-12) {
            continue;
        }
        const int lobes = std::max(1, static_cast<int>(std::lround(total / arc_len)));
        std::size_t seg = 1;
        for (int k = 0; k < lobes; ++k) {
            const double st = total * static_cast<double>(k) / static_cast<double>(lobes);
            while (seg + 1 < run.size() && cum[seg] < st) {
                ++seg;
            }
            const double sl = cum[seg] - cum[seg - 1];
            const double t = sl > 1e-12 ? (st - cum[seg - 1]) / sl : 0.0;
            verts.push_back(run[seg - 1] + (run[seg] - run[seg - 1]) * t);
            bulges.push_back(b);
        }
    }
    if (!closed) {
        verts.push_back(path.back()); // the open end is a plain vertex
        bulges.push_back(0.0);
    }
}

/// REVCLOUD's Calligraphy style: each lobe drawn as with a broad pen, from nothing at its
/// start to a fifth of its chord at its end -- two widths per vertex, as polylines keep them.
inline std::vector<double> calligraphy_widths(const std::vector<Vec2>& verts, bool closed) {
    const std::size_t n = verts.size();
    std::vector<double> w(2 * n, 0.0);
    for (std::size_t i = 0; i < n && (closed || i + 1 < n); ++i) {
        const Vec2 d = verts[(i + 1) % n] - verts[i];
        w[2 * i + 1] = 0.2 * std::sqrt(dot(d, d));
    }
    return w;
}

/// Two arcs from `p0`, leaving along `t0`, to `p1`, arriving along `t1` (unit tangents),
/// tangent to each other at `joint`: the pair PEDIT Fit puts on every segment and
/// PELLIPSE builds an ellipse from. The bulges are the two arcs' (positive CCW).
struct Biarc {
    Vec2 joint;
    double bulge0 = 0.0;
    double bulge1 = 0.0;
};
inline Biarc biarc(Vec2 p0, Vec2 t0, Vec2 p1, Vec2 t1) {
    const Vec2 d = p1 - p0;
    const Vec2 tt = t0 + t1;
    const double denom = 2.0 * (1.0 - dot(t0, t1));
    Biarc out;
    if (denom < 1e-12) {
        out.joint = (p0 + p1) * 0.5; // parallel tangents: the two arcs meet half way
    } else {
        // Both arcs' tangent lengths equal: |p1 - p0 - h (t0 + t1)|^2 = (2h)^2.
        const double dt = dot(d, tt);
        const double h = (-dt + std::sqrt(dt * dt + denom * dot(d, d))) / denom;
        out.joint = (p0 + t0 * h + p1 - t1 * h) * 0.5;
    }
    const Vec2 c0 = out.joint - p0;
    const Vec2 c1 = p1 - out.joint;
    // An arc's bulge is tan(sweep / 4), and its chord turns half the sweep from the tangent.
    out.bulge0 = std::tan(std::atan2(cross(t0, c0), dot(t0, c0)) * 0.5);
    out.bulge1 = std::tan(std::atan2(cross(c1, t1), dot(c1, t1)) * 0.5);
    return out;
}

/// PEDIT Fit: every segment of the chain becomes a biarc through the vertices, tangent at
/// each vertex along the bisector of its two segments (an open end's tangent mirrors its
/// neighbour's across the end segment), so the curve passes every vertex smoothly. A joint
/// vertex goes in on each segment; `widths` (two per vertex) taper half way at it. False
/// for a chain too short to fit (an open one needs three vertices).
inline bool fit_arcs(std::vector<Vec2>& pts, std::vector<double>& bulges, std::vector<double>& widths, bool closed) {
    const std::size_t n = pts.size();
    if (n < 3) {
        return false;
    }
    widths.resize(2 * n, 0.0);
    const auto unit = [](Vec2 v) {
        const double l = std::sqrt(dot(v, v));
        return l > 1e-12 ? v * (1.0 / l) : Vec2{1.0, 0.0};
    };
    std::vector<Vec2> tan(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (!closed && (i == 0 || i + 1 == n)) {
            continue;
        }
        const Vec2 in = unit(pts[i] - pts[(i + n - 1) % n]);
        const Vec2 out = unit(pts[(i + 1) % n] - pts[i]);
        const Vec2 t = in + out;
        tan[i] = dot(t, t) > 1e-18 ? unit(t) : out;
    }
    if (!closed) {
        const Vec2 c0 = unit(pts[1] - pts[0]);
        tan[0] = c0 * (2.0 * dot(tan[1], c0)) - tan[1];
        const Vec2 c1 = unit(pts[n - 1] - pts[n - 2]);
        tan[n - 1] = c1 * (2.0 * dot(tan[n - 2], c1)) - tan[n - 2];
    }
    std::vector<Vec2> nv;
    std::vector<double> nb;
    std::vector<double> nw;
    const std::size_t segs = closed ? n : n - 1;
    for (std::size_t s = 0; s < segs; ++s) {
        const std::size_t e = (s + 1) % n;
        const double w0 = widths[2 * s];
        const double w1 = widths[2 * s + 1];
        nv.push_back(pts[s]);
        if (dot(pts[e] - pts[s], pts[e] - pts[s]) < 1e-24) {
            nb.push_back(0.0); // a zero-length segment stays as it is
            nw.insert(nw.end(), {w0, w1});
            continue;
        }
        const Biarc a = biarc(pts[s], tan[s], pts[e], tan[e]);
        const double wm = (w0 + w1) * 0.5;
        nb.push_back(a.bulge0);
        nw.insert(nw.end(), {w0, wm});
        nv.push_back(a.joint);
        nb.push_back(a.bulge1);
        nw.insert(nw.end(), {wm, w1});
    }
    if (!closed) {
        nv.push_back(pts[n - 1]);
        nb.push_back(0.0);
        nw.insert(nw.end(), {widths[2 * (n - 1)], widths[2 * (n - 1) + 1]});
    }
    pts = std::move(nv);
    bulges = std::move(nb);
    widths = std::move(nw);
    return true;
}

} // namespace musacad::core::polyline_ops

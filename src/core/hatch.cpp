// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/hatch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace musacad::core::hatch {

namespace {

constexpr double kTwoPi = 6.283185307179586;

// Signed polygon area (CCW positive). Used to pick the smallest enclosing loop.
double signed_area(const std::vector<Vec2>& poly) {
    double a = 0.0;
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 p = poly[i];
        const Vec2 q = poly[(i + 1) % n];
        a += p.x * q.y - q.x * p.y;
    }
    return 0.5 * a;
}

// A planar graph of boundary segments: nodes (snapped endpoints) + undirected edges.
struct Graph {
    std::vector<Vec2> pos;
    std::vector<std::vector<std::size_t>> adj; // neighbour node ids per node
};

Graph build_graph(const std::vector<Segment>& segs, double tol) {
    Graph g;
    const double inv = 1.0 / (tol > 0.0 ? tol : 1e-9);
    std::map<std::pair<std::int64_t, std::int64_t>, std::size_t> nodes;
    const auto node = [&](Vec2 p) -> std::size_t {
        const std::pair<std::int64_t, std::int64_t> key{
            static_cast<std::int64_t>(std::llround(p.x * inv)),
            static_cast<std::int64_t>(std::llround(p.y * inv))};
        auto it = nodes.find(key);
        if (it != nodes.end()) {
            return it->second;
        }
        const std::size_t id = g.pos.size();
        nodes.emplace(key, id);
        g.pos.push_back(p);
        g.adj.emplace_back();
        return id;
    };
    const auto connect = [&](std::size_t u, std::size_t v) {
        if (u == v) {
            return;
        }
        if (std::find(g.adj[u].begin(), g.adj[u].end(), v) == g.adj[u].end()) {
            g.adj[u].push_back(v);
        }
        if (std::find(g.adj[v].begin(), g.adj[v].end(), u) == g.adj[v].end()) {
            g.adj[v].push_back(u);
        }
    };
    for (const Segment& s : segs) {
        connect(node(s.a), node(s.b));
    }
    return g;
}

// Walk a face boundary from the directed edge (from -> to). At each node, take the next edge
// turning as far clockwise (sign +1) or counter-clockwise (sign -1) as possible from the
// reverse-incoming direction. Returns the loop polygon, or empty on a dead end / no closure.
std::vector<Vec2> walk_face(const Graph& g, std::size_t start_from, std::size_t start_to,
                            double sign) {
    std::vector<Vec2> loop;
    std::size_t from = start_from;
    std::size_t to = start_to;
    const std::size_t cap = g.pos.size() * 4 + 16;
    loop.push_back(g.pos[to]);
    for (std::size_t step = 0; step < cap; ++step) {
        const Vec2 pt = g.pos[to];
        const double rev = std::atan2(g.pos[from].y - pt.y, g.pos[from].x - pt.x);
        std::size_t best = 0;
        bool have = false;
        double best_key = 1e300;
        for (const std::size_t w : g.adj[to]) {
            if (w == from && g.adj[to].size() > 1) {
                continue; // don't immediately backtrack unless it's a dead end
            }
            const double ang = std::atan2(g.pos[w].y - pt.y, g.pos[w].x - pt.x);
            double d = sign * (rev - ang);
            while (d <= 1e-9) {
                d += kTwoPi;
            }
            while (d > kTwoPi) {
                d -= kTwoPi;
            }
            if (d < best_key) {
                best_key = d;
                best = w;
                have = true;
            }
        }
        if (!have) {
            return {}; // dead end -> open boundary
        }
        from = to;
        to = best;
        if (from == start_from && to == start_to) {
            return loop; // returned to the starting directed edge -> closed
        }
        loop.push_back(g.pos[to]);
    }
    return {}; // failed to close within the step cap
}

// Parametric intersection of two segments' infinite lines. Sets t (along a->b) and u
// (along c->d); returns false when the lines are parallel/collinear.
bool intersect_params(Vec2 a, Vec2 b, Vec2 c, Vec2 d, double& t, double& u) {
    const Vec2 r{b.x - a.x, b.y - a.y};
    const Vec2 s{d.x - c.x, d.y - c.y};
    const double denom = r.x * s.y - r.y * s.x;
    if (std::abs(denom) < 1e-12) {
        return false;
    }
    const Vec2 ac{c.x - a.x, c.y - a.y};
    t = (ac.x * s.y - ac.y * s.x) / denom;
    u = (ac.x * r.y - ac.y * r.x) / denom;
    return true;
}

// Parameter of point p projected onto the line a->b; returns true only when p actually
// lies on that line within `tol` (used to split collinear overlaps at shared endpoints).
bool project_param(Vec2 a, Vec2 b, Vec2 p, double tol, double& t) {
    const Vec2 r{b.x - a.x, b.y - a.y};
    const double l2 = r.x * r.x + r.y * r.y;
    if (l2 < 1e-18) {
        return false;
    }
    t = ((p.x - a.x) * r.x + (p.y - a.y) * r.y) / l2;
    const Vec2 proj{a.x + r.x * t, a.y + r.y * t};
    return std::hypot(p.x - proj.x, p.y - proj.y) <= tol;
}

// Split every segment at all points where another segment crosses it or touches its
// interior (a T-junction). Without this, a partitioning chord whose endpoints land in the
// middle of an enclosing edge stays a DANGLING edge -- its endpoints are degree-1 nodes
// not wired into that edge -- so walk_face() can only trace the whole enclosing loop and a
// pick in one partition fills the entire region. Splitting turns the input into a true
// planar arrangement: every crossing becomes a shared node, so the smaller sub-region is
// traceable. O(n^2) in segment count, but this runs once per pick (a user action), not per
// frame; spatial culling is a future optimisation for very dense boundaries.
std::vector<Segment> split_at_intersections(const std::vector<Segment>& segs, double tol) {
    const std::size_t n = segs.size();
    std::vector<std::vector<double>> cut(n); // split parameters per segment (0 and 1 seeded)
    std::vector<double> len(n);
    for (std::size_t i = 0; i < n; ++i) {
        cut[i].push_back(0.0);
        cut[i].push_back(1.0);
        len[i] = std::hypot(segs[i].b.x - segs[i].a.x, segs[i].b.y - segs[i].a.y);
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (len[i] < tol) {
            continue;
        }
        for (std::size_t j = i + 1; j < n; ++j) {
            if (len[j] < tol) {
                continue;
            }
            double t = 0.0;
            double u = 0.0;
            if (intersect_params(segs[i].a, segs[i].b, segs[j].a, segs[j].b, t, u)) {
                // Admit the crossing / T-junction when it lands on BOTH segments, with a
                // tol-sized slack near the ends so a chord that just touches an edge counts.
                const double pi = tol / len[i];
                const double pj = tol / len[j];
                if (t > -pi && t < 1.0 + pi && u > -pj && u < 1.0 + pj) {
                    cut[i].push_back(std::clamp(t, 0.0, 1.0));
                    cut[j].push_back(std::clamp(u, 0.0, 1.0));
                }
            } else {
                // Parallel: split collinear overlaps at each other's interior endpoints.
                if (project_param(segs[i].a, segs[i].b, segs[j].a, tol, t) && t > 0.0 && t < 1.0) {
                    cut[i].push_back(t);
                }
                if (project_param(segs[i].a, segs[i].b, segs[j].b, tol, t) && t > 0.0 && t < 1.0) {
                    cut[i].push_back(t);
                }
                if (project_param(segs[j].a, segs[j].b, segs[i].a, tol, t) && t > 0.0 && t < 1.0) {
                    cut[j].push_back(t);
                }
                if (project_param(segs[j].a, segs[j].b, segs[i].b, tol, t) && t > 0.0 && t < 1.0) {
                    cut[j].push_back(t);
                }
            }
        }
    }
    std::vector<Segment> out;
    out.reserve(segs.size());
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = segs[i].a;
        const Vec2 b = segs[i].b;
        if (len[i] < tol) {
            out.push_back(segs[i]);
            continue;
        }
        std::sort(cut[i].begin(), cut[i].end());
        std::vector<double> keep; // dedup split points within tol along the segment
        for (const double t : cut[i]) {
            if (keep.empty() || (t - keep.back()) * len[i] > tol) {
                keep.push_back(t);
            }
        }
        if (keep.size() < 2) {
            out.push_back(segs[i]);
            continue;
        }
        for (std::size_t k = 0; k + 1 < keep.size(); ++k) {
            out.push_back({{a.x + (b.x - a.x) * keep[k], a.y + (b.y - a.y) * keep[k]},
                           {a.x + (b.x - a.x) * keep[k + 1], a.y + (b.y - a.y) * keep[k + 1]}});
        }
    }
    return out;
}

} // namespace

void triangulate_filled(const std::vector<std::vector<Vec2>>& loops, std::vector<Vec2>& out) {
    // Distinct vertex y-values define horizontal slabs. Within a slab (between consecutive
    // distinct y's) no edge has an interior vertex, so every crossing edge spans the whole
    // slab and the cross-section is a set of trapezoids -- exact, no stepping.
    std::vector<double> ys;
    for (const std::vector<Vec2>& loop : loops) {
        for (const Vec2& p : loop) {
            ys.push_back(p.y);
        }
    }
    if (ys.size() < 3) {
        return;
    }
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end(),
                         [](double a, double b) { return std::abs(a - b) < 1e-9; }),
             ys.end());

    struct Cross {
        double xb; // x where the edge meets the slab bottom
        double xt; // x where the edge meets the slab top
        double xm; // x at the slab mid-y (for left-to-right ordering / parity)
    };
    std::vector<Cross> cs;
    for (std::size_t s = 0; s + 1 < ys.size(); ++s) {
        const double yb = ys[s];
        const double yt = ys[s + 1];
        if (yt - yb < 1e-12) {
            continue;
        }
        const double ym = 0.5 * (yb + yt);
        cs.clear();
        for (const std::vector<Vec2>& loop : loops) {
            const std::size_t m = loop.size();
            for (std::size_t k = 0; k < m; ++k) {
                const Vec2 a = loop[k];
                const Vec2 b = loop[(k + 1) % m];
                // Half-open: counts an edge once even when it shares a vertex y.
                if ((a.y <= ym) != (b.y <= ym)) {
                    const double inv = 1.0 / (b.y - a.y);
                    cs.push_back({a.x + (yb - a.y) * inv * (b.x - a.x),
                                  a.x + (yt - a.y) * inv * (b.x - a.x),
                                  a.x + (ym - a.y) * inv * (b.x - a.x)});
                }
            }
        }
        std::sort(cs.begin(), cs.end(), [](const Cross& l, const Cross& r) { return l.xm < r.xm; });
        for (std::size_t k = 0; k + 1 < cs.size(); k += 2) {
            const Cross& l = cs[k];
            const Cross& r = cs[k + 1];
            if (r.xm - l.xm < 1e-12) {
                continue;
            }
            // Trapezoid (l.xb,yb)-(r.xb,yb)-(r.xt,yt)-(l.xt,yt) as two triangles.
            out.push_back({l.xb, yb});
            out.push_back({r.xb, yb});
            out.push_back({r.xt, yt});
            out.push_back({l.xb, yb});
            out.push_back({r.xt, yt});
            out.push_back({l.xt, yt});
        }
    }
}

bool point_in_loops(const std::vector<std::vector<Vec2>>& loops, Vec2 p) {
    bool inside = false;
    for (const std::vector<Vec2>& loop : loops) {
        const std::size_t m = loop.size();
        for (std::size_t k = 0; k < m; ++k) {
            const Vec2 a = loop[k];
            const Vec2 b = loop[(k + 1) % m];
            if ((a.y <= p.y) != (b.y <= p.y)) {
                const double x = a.x + (p.y - a.y) / (b.y - a.y) * (b.x - a.x);
                if (x > p.x) {
                    inside = !inside;
                }
            }
        }
    }
    return inside;
}

std::optional<std::vector<Vec2>> trace_boundary(const std::vector<Segment>& segs, Vec2 p,
                                                double tol) {
    // Build a planar arrangement first: split every segment at crossings/T-junctions so a
    // partitioning chord wires into the edges it touches (else it dangles and the whole
    // enclosing loop is traced -- the "fills the total polyline" bug).
    const std::vector<Segment> refined = split_at_intersections(segs, tol);
    const Graph g = build_graph(refined, tol);
    if (g.pos.size() < 3) {
        return std::nullopt;
    }
    // Ray-cast +X from p: find the nearest edge to the right that crosses the horizontal
    // through p -- that edge is on the boundary of the enclosing face.
    std::size_t hit_u = 0;
    std::size_t hit_v = 0;
    bool hit = false;
    double best_x = 1e300;
    for (std::size_t u = 0; u < g.pos.size(); ++u) {
        for (const std::size_t v : g.adj[u]) {
            if (v <= u) {
                continue; // each undirected edge once
            }
            const Vec2 a = g.pos[u];
            const Vec2 b = g.pos[v];
            if ((a.y <= p.y) != (b.y <= p.y)) {
                const double x = a.x + (p.y - a.y) / (b.y - a.y) * (b.x - a.x);
                if (x > p.x && x < best_x) {
                    best_x = x;
                    hit_u = u;
                    hit_v = v;
                    hit = true;
                }
            }
        }
    }
    if (!hit) {
        return std::nullopt; // p is not enclosed by any edge to its right
    }
    // Trace from both orientations of the hit edge and both turn senses; keep the smallest
    // closed loop that actually contains p (robust against handedness).
    std::optional<std::vector<Vec2>> best;
    double best_area = 1e300;
    const std::size_t starts[2][2] = {{hit_u, hit_v}, {hit_v, hit_u}};
    for (const auto& st : starts) {
        for (const double sign : {1.0, -1.0}) {
            std::vector<Vec2> loop = walk_face(g, st[0], st[1], sign);
            if (loop.size() < 3) {
                continue;
            }
            if (!point_in_loops({loop}, p)) {
                continue;
            }
            const double area = std::abs(signed_area(loop));
            if (area > 1e-9 && area < best_area) {
                best_area = area;
                best = std::move(loop);
            }
        }
    }
    return best;
}

} // namespace musacad::core::hatch

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/mline.hpp"

#include <algorithm>
#include <cmath>

namespace musacad::core::mline {

namespace {
Vec2 unit(Vec2 v) {
    const double l = length(v);
    return l > 1e-12 ? v * (1.0 / l) : Vec2{1.0, 0.0};
}
Vec2 left_normal(Vec2 u) {
    return {-u.y, u.x};
}
Vec2 turned(Vec2 u, double radians) {
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return {u.x * c - u.y * s, u.x * s + u.y * c};
}
// The multiple of `m` that lies `1` off the line along normal `n`, kept from running away
// at a hairpin.
Vec2 miter_for(Vec2 m, Vec2 n) {
    const double dot_mn = dot(m, n);
    if (std::abs(dot_mn) < 1e-3) {
        return n;
    }
    return m * (1.0 / dot_mn);
}
// A semicircle from `a` to `b` bulging towards `out`, as an arc and as segments.
void half_circle(Vec2 a, Vec2 b, Vec2 out, Geometry& g) {
    const Vec2 c = (a + b) * 0.5;
    const double r = length(b - a) * 0.5;
    if (r < 1e-12) {
        return;
    }
    const double sa = std::atan2(a.y - c.y, a.x - c.x);
    // Counter-clockwise from a, the arc's middle is a quarter turn on: that side or the other.
    const Vec2 mid_ccw = c + Vec2{std::cos(sa + kHalfPi), std::sin(sa + kHalfPi)} * r;
    const bool ccw = dot(mid_ccw - c, out) >= 0.0;
    const double start = ccw ? sa : sa - kPi;
    g.arcs.push_back({c, r, start, start + kPi}); // counter-clockwise: from a, or from b
    constexpr int kSteps = 16;
    for (int k = 0; k < kSteps; ++k) {
        const double t0 = start + kPi * static_cast<double>(k) / kSteps;
        const double t1 = start + kPi * static_cast<double>(k + 1) / kSteps;
        g.caps.push_back(c + Vec2{std::cos(t0), std::sin(t0)} * r);
        g.caps.push_back(c + Vec2{std::cos(t1), std::sin(t1)} * r);
    }
}
void straight(Vec2 a, Vec2 b, Geometry& g) {
    g.caps.push_back(a);
    g.caps.push_back(b);
    g.lines.push_back(a);
    g.lines.push_back(b);
}
} // namespace

void trim_breaks(std::vector<std::vector<std::vector<double>>>& breaks) {
    for (std::vector<std::vector<double>>& seg : breaks) {
        while (!seg.empty() && seg.back().empty()) {
            seg.pop_back();
        }
    }
    while (!breaks.empty() && breaks.back().empty()) {
        breaks.pop_back();
    }
}

std::size_t segment_count(const MLineData& m) noexcept {
    const std::size_t n = m.vertices.size();
    return n < 2 ? 0 : (m.closed ? n : n - 1);
}

std::vector<double> offsets(const MLineStyle& style, double scale, std::uint8_t justification) {
    std::vector<double> out;
    if (style.elements.empty()) {
        return out;
    }
    double hi = style.elements.front().offset;
    double lo = hi;
    for (const MLineElement& e : style.elements) {
        hi = std::max(hi, e.offset);
        lo = std::min(lo, e.offset);
    }
    const double shift = justification == 0 ? -hi : justification == 2 ? -lo : 0.0;
    for (const MLineElement& e : style.elements) {
        out.push_back((e.offset + shift) * scale);
    }
    return out;
}

std::vector<Vec2> miters(const std::vector<Vec2>& v, bool closed, double start_angle_deg, double end_angle_deg) {
    const std::size_t n = v.size();
    std::vector<Vec2> out(n, Vec2{0.0, 1.0});
    if (n < 2) {
        return out;
    }
    const std::size_t segs = closed ? n : n - 1;
    std::vector<Vec2> u(segs);
    for (std::size_t i = 0; i < segs; ++i) {
        u[i] = unit(v[(i + 1) % n] - v[i]);
    }
    for (std::size_t i = 0; i < n; ++i) {
        const bool has_in = closed || i > 0;
        const bool has_out = closed || i + 1 < n;
        if (has_in && has_out) {
            const Vec2 n_in = left_normal(u[(i + segs - 1) % segs]);
            const Vec2 n_out = left_normal(u[i % segs]);
            const Vec2 s = n_in + n_out;
            out[i] = length(s) < 1e-9 ? n_out : miter_for(unit(s), n_out);
        } else if (has_out) {
            const Vec2 m = turned(u[0], start_angle_deg * kPi / 180.0);
            out[i] = miter_for(m, left_normal(u[0]));
        } else {
            const Vec2 m = turned(u[segs - 1], end_angle_deg * kPi / 180.0);
            out[i] = miter_for(m, left_normal(u[segs - 1]));
        }
    }
    return out;
}

Vec2 element_point(const MLineData& m, const MLineStyle& style, std::size_t i, std::size_t e) {
    const std::vector<Vec2> w = miters(m.vertices, m.closed, style.start_angle, style.end_angle);
    const std::vector<double> d = offsets(style, m.scale, m.justification);
    if (i >= m.vertices.size() || e >= d.size()) {
        return i < m.vertices.size() ? m.vertices[i] : Vec2{};
    }
    return m.vertices[i] + w[i] * d[e];
}

Geometry geometry(const MLineData& m, const MLineStyle& style) {
    Geometry g;
    const std::size_t n = m.vertices.size();
    const std::size_t segs = segment_count(m);
    const std::vector<double> d = offsets(style, m.scale, m.justification);
    if (segs == 0 || d.empty()) {
        return g;
    }
    const std::vector<Vec2> w = miters(m.vertices, m.closed, style.start_angle, style.end_angle);
    const auto at = [&](std::size_t i, std::size_t e) { return m.vertices[i % n] + w[i % n] * d[e]; };
    // The outer lines (largest and smallest offset) and the pairs inside them, outside in.
    std::vector<std::size_t> order(d.size());
    for (std::size_t e = 0; e < d.size(); ++e) {
        order[e] = e;
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return d[a] > d[b]; });
    const std::size_t top = order.front();
    const std::size_t bottom = order.back();

    for (std::size_t i = 0; i < segs; ++i) {
        for (std::size_t e = 0; e < d.size(); ++e) {
            const Vec2 a = at(i, e);
            const Vec2 b = at(i + 1, e);
            const double len = length(b - a);
            const Vec2 dir = len > 1e-12 ? (b - a) * (1.0 / len) : Vec2{};
            // The gaps, sorted and kept within the piece.
            std::vector<std::pair<double, double>> gaps;
            if (i < m.breaks.size() && e < m.breaks[i].size()) {
                const std::vector<double>& br = m.breaks[i][e];
                for (std::size_t k = 0; k + 1 < br.size(); k += 2) {
                    const double s0 = std::clamp(std::min(br[k], br[k + 1]), 0.0, len);
                    const double s1 = std::clamp(std::max(br[k], br[k + 1]), 0.0, len);
                    if (s1 > s0) {
                        gaps.emplace_back(s0, s1);
                    }
                }
            }
            std::sort(gaps.begin(), gaps.end());
            double from = 0.0;
            for (const auto& [s0, s1] : gaps) {
                if (s0 > from + 1e-12) {
                    g.pieces.push_back({e, a + dir * from, a + dir * s0});
                }
                from = std::max(from, s1);
            }
            if (len - from > 1e-12 || (len <= 1e-12 && gaps.empty())) {
                g.pieces.push_back({e, a + dir * from, b});
            }
        }
        if ((style.flags & MLineStyle::kFill) != 0 && top != bottom) {
            const Vec2 p0 = at(i, top);
            const Vec2 p1 = at(i + 1, top);
            const Vec2 p2 = at(i + 1, bottom);
            const Vec2 p3 = at(i, bottom);
            g.fill.insert(g.fill.end(), {p0, p1, p2, p0, p2, p3});
        }
    }
    // The joints: a line across at each corner.
    if ((style.flags & MLineStyle::kJoints) != 0 && top != bottom) {
        for (std::size_t i = m.closed ? 0 : 1; i < (m.closed ? n : n - 1); ++i) {
            straight(at(i, top), at(i, bottom), g);
        }
    }
    if (m.closed || top == bottom) {
        return g;
    }
    // The caps, at the start and at the end.
    const Vec2 u0 = (m.vertices[1] - m.vertices[0]);
    const Vec2 u1 = (m.vertices[n - 1] - m.vertices[n - 2]);
    const auto cap = [&](std::size_t i, bool line, bool outer_arc, bool inner_arcs, Vec2 out) {
        if (line) {
            straight(at(i, top), at(i, bottom), g);
        }
        if (outer_arc) {
            half_circle(at(i, top), at(i, bottom), out, g);
        }
        if (inner_arcs) {
            for (std::size_t k = 1; k < order.size() / 2; ++k) {
                half_circle(at(i, order[k]), at(i, order[order.size() - 1 - k]), out, g);
            }
        }
    };
    if (!m.no_start_caps) {
        cap(0, (style.flags & MLineStyle::kStartLine) != 0, (style.flags & MLineStyle::kStartOuterArc) != 0,
            (style.flags & MLineStyle::kStartInnerArcs) != 0, u0 * -1.0);
    }
    if (!m.no_end_caps) {
        cap(n - 1, (style.flags & MLineStyle::kEndLine) != 0, (style.flags & MLineStyle::kEndOuterArc) != 0,
            (style.flags & MLineStyle::kEndInnerArcs) != 0, u1);
    }
    return g;
}

} // namespace musacad::core::mline

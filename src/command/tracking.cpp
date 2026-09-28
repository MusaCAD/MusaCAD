// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/command/tracking.hpp"

#include <algorithm>
#include <cmath>

namespace musacad::command {

namespace {
double wrapped(double a) {
    a = std::fmod(a, core::kTwoPi);
    if (a < 0.0) {
        a += core::kTwoPi;
    }
    // 2 pi less a rounding error is 0.
    return a > core::kTwoPi - 1e-9 ? 0.0 : a;
}

struct Candidate {
    TrackHit hit;
    core::Vec2 dir{};
    core::Vec2 foot{};
    double off = 0.0; ///< how far the cursor is from the path
};

// The cursor's foot on the ray from `from` along `angle`, when it is ahead of `from` and
// within reach.
bool on_ray(core::Vec2 from, double angle, core::Vec2 cursor, double aperture, Candidate& out) {
    const core::Vec2 u{std::cos(angle), std::sin(angle)};
    const core::Vec2 d = cursor - from;
    const double t = d.x * u.x + d.y * u.y;
    const double off = std::abs(u.x * d.y - u.y * d.x);
    if (t <= aperture || off > aperture) {
        return false; // behind or at the point itself, or out of reach
    }
    out.dir = u;
    out.foot = from + u * t;
    out.off = off;
    out.hit.from = from;
    out.hit.angle = wrapped(angle);
    out.hit.length = t;
    return true;
}
} // namespace

std::vector<double> polar_angles(const TrackingSettings& s, double base) {
    std::vector<double> out;
    const double origin = s.relative ? base : 0.0;
    const double inc = s.polar_increment;
    if (inc > 1e-6) {
        const int n = std::max(1, static_cast<int>(std::lround(core::kTwoPi / inc)));
        // An increment that does not divide the circle still starts at the origin and
        // stops short of coming round.
        for (int k = 0; k < n && static_cast<double>(k) * inc < core::kTwoPi - 1e-9; ++k) {
            out.push_back(wrapped(origin + static_cast<double>(k) * inc));
        }
    }
    if (s.use_additional) {
        for (const double a : s.additional) {
            out.push_back(wrapped(origin + a));
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end(),
                          [](double a, double b) { return std::abs(a - b) < 1e-9; }),
              out.end());
    return out;
}

TrackResult track(const TrackingSettings& s, bool polar_on, bool otrack_on, core::Vec2 cursor,
                  std::optional<core::Vec2> last, double base, std::span<const TrackPoint> acquired,
                  double aperture) {
    TrackResult r;
    r.point = cursor;
    if (!(aperture > 0.0)) {
        return r;
    }
    std::vector<Candidate> found;
    if (polar_on && last) {
        for (const double a : polar_angles(s, base)) {
            Candidate c;
            if (on_ray(*last, a, cursor, aperture, c)) {
                c.hit.polar = true;
                c.hit.relative = s.relative;
                found.push_back(std::move(c));
            }
        }
    }
    if (otrack_on) {
        TrackingSettings absolute = s;
        absolute.relative = false;
        const std::vector<double> every = polar_angles(absolute, 0.0);
        const std::vector<double> square{0.0, core::kHalfPi, core::kPi, core::kPi + core::kHalfPi};
        for (const TrackPoint& p : acquired) {
            for (const double a : (s.otrack_polar ? every : square)) {
                Candidate c;
                if (on_ray(p.at, a, cursor, aperture, c)) {
                    c.hit.label = p.label;
                    found.push_back(std::move(c));
                }
            }
        }
    }
    if (found.empty()) {
        return r;
    }
    std::sort(found.begin(), found.end(),
              [](const Candidate& a, const Candidate& b) { return a.off < b.off; });
    // Two paths in reach that cross within it: the crossing.
    for (std::size_t i = 0; i < found.size(); ++i) {
        for (std::size_t j = i + 1; j < found.size(); ++j) {
            const Candidate& a = found[i];
            const Candidate& b = found[j];
            const double den = a.dir.x * b.dir.y - a.dir.y * b.dir.x;
            if (std::abs(den) < 1e-9) {
                continue; // parallel (or the same path from two points)
            }
            const core::Vec2 w = b.hit.from - a.hit.from;
            const double ta = (w.x * b.dir.y - w.y * b.dir.x) / den;
            const double tb = (w.x * a.dir.y - w.y * a.dir.x) / den;
            const core::Vec2 x = a.hit.from + a.dir * ta;
            if (ta <= 0.0 || tb <= 0.0 || core::length(x - cursor) > aperture * 1.5) {
                continue;
            }
            r.point = x;
            r.hits = {a.hit, b.hit};
            r.hits[0].length = ta;
            r.hits[1].length = tb;
            return r;
        }
    }
    Candidate best = found.front();
    if (best.hit.polar && s.polar_snap && s.polar_distance > 1e-12) {
        // PolarSnap: along the path in steps of POLARDIST.
        best.hit.length = std::max(1.0, std::round(best.hit.length / s.polar_distance)) * s.polar_distance;
        best.foot = best.hit.from + best.dir * best.hit.length;
    }
    r.point = best.foot;
    r.hits = {best.hit};
    return r;
}

} // namespace musacad::command

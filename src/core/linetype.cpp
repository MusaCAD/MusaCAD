// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#include "musacad/core/linetype.hpp"

#include <array>

namespace musacad::core {

namespace {
// Base patterns in drawing units (see header for the AutoCAD-proportion rationale).
constexpr std::array<double, 2> kDashed = {5.0, 2.5};
constexpr std::array<double, 2> kHidden = {2.5, 1.25};
constexpr std::array<double, 4> kCenter = {12.5, 2.5, 2.5, 2.5};
} // namespace

std::span<const double> dash_pattern(Linetype lt) noexcept {
    switch (lt) {
    case Linetype::Dashed:
        return kDashed;
    case Linetype::Hidden:
        return kHidden;
    case Linetype::Center:
        return kCenter;
    case Linetype::Continuous:
        break;
    }
    return {};
}

void dash_polyline(std::span<const Vec2> pts, Linetype lt, double ltscale,
                   std::vector<Vec2>& out) {
    const std::span<const double> pattern = dash_pattern(lt);
    // Continuous, degenerate scale, or too few points -> copy every segment as-is.
    if (pattern.empty() || ltscale <= 0.0 || pts.size() < 2) {
        for (std::size_t s = 1; s < pts.size(); ++s) {
            out.push_back(pts[s - 1]);
            out.push_back(pts[s]);
        }
        return;
    }

    // Walk arc-length, maintaining the current pattern element, how much of it is
    // left, and whether the pen is down (dash) or up (gap). The phase is continuous
    // across polyline vertices, so curves dash by true arc-length.
    std::size_t pi = 0;
    double remaining = pattern[0] * ltscale;
    bool pen_down = true; // even indices are "on"
    for (std::size_t s = 1; s < pts.size(); ++s) {
        const Vec2 a = pts[s - 1];
        const Vec2 b = pts[s];
        const Vec2 d = b - a;
        const double seg_len = length(d);
        if (seg_len < 1e-12) {
            continue;
        }
        const Vec2 dir = d / seg_len;
        double pos = 0.0;
        while (pos < seg_len - 1e-12) {
            const double step = std::min(remaining, seg_len - pos);
            if (pen_down) {
                out.push_back(a + dir * pos);
                out.push_back(a + dir * (pos + step));
            }
            pos += step;
            remaining -= step;
            if (remaining <= 1e-9) {
                pi = (pi + 1) % pattern.size();
                remaining = pattern[pi] * ltscale;
                pen_down = !pen_down;
            }
        }
    }
}

} // namespace musacad::core

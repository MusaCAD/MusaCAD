// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/render/grid.hpp"

#include <algorithm>
#include <cmath>

namespace musacad::render {

double choose_minor_spacing(double scale, double target_minor_px) noexcept {
    if (scale <= 0.0 || target_minor_px <= 0.0) {
        return 1.0;
    }
    // World units that map to target_minor_px on screen.
    const double raw = target_minor_px / scale;
    // Round up to the next power of ten so a cell is >= target on screen.
    const double exponent = std::ceil(std::log10(raw));
    return std::pow(10.0, exponent);
}

GridResult build_grid(const Camera2D& camera, double target_minor_px, int max_lines_per_axis) {
    GridResult result;
    const double minor = choose_minor_spacing(camera.scale(), target_minor_px);
    const double major = minor * 10.0;
    result.minor_spacing = minor;
    result.major_spacing = major;

    const Vec2 lo = camera.visible_min();
    const Vec2 hi = camera.visible_max();

    const auto first_index = [](double v, double step) {
        return static_cast<long long>(std::floor(v / step));
    };
    const auto last_index = [](double v, double step) {
        return static_cast<long long>(std::ceil(v / step));
    };

    // Vertical lines (constant x).
    const long long x0 = first_index(lo.x, minor);
    const long long x1 = last_index(hi.x, minor);
    if (x1 - x0 <= max_lines_per_axis) {
        for (long long i = x0; i <= x1; ++i) {
            const double x = static_cast<double>(i) * minor;
            std::vector<Vec2>& dst = (i % 10 == 0) ? result.major : result.minor;
            dst.push_back({x, lo.y});
            dst.push_back({x, hi.y});
        }
    }

    // Horizontal lines (constant y).
    const long long y0 = first_index(lo.y, minor);
    const long long y1 = last_index(hi.y, minor);
    if (y1 - y0 <= max_lines_per_axis) {
        for (long long i = y0; i <= y1; ++i) {
            const double y = static_cast<double>(i) * minor;
            std::vector<Vec2>& dst = (i % 10 == 0) ? result.major : result.minor;
            dst.push_back({lo.x, y});
            dst.push_back({hi.x, y});
        }
    }

    return result;
}

namespace {
/// The part of the line {p : dot(p, n) = c} (n unit) inside [lo, hi], appended to `out`.
void clip_line(Vec2 n, double c, Vec2 lo, Vec2 hi, std::vector<Vec2>& out) {
    const Vec2 p0{n.x * c, n.y * c};
    const Vec2 d{-n.y, n.x};
    double t0 = -1e300;
    double t1 = 1e300;
    const double p[2] = {p0.x, p0.y};
    const double dd[2] = {d.x, d.y};
    const double mn[2] = {lo.x, lo.y};
    const double mx[2] = {hi.x, hi.y};
    for (int k = 0; k < 2; ++k) {
        if (std::abs(dd[k]) < 1e-15) {
            if (p[k] < mn[k] || p[k] > mx[k]) {
                return;
            }
            continue;
        }
        double a = (mn[k] - p[k]) / dd[k];
        double b = (mx[k] - p[k]) / dd[k];
        if (a > b) {
            std::swap(a, b);
        }
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
    }
    if (t0 < t1) {
        out.push_back({p0.x + d.x * t0, p0.y + d.y * t0});
        out.push_back({p0.x + d.x * t1, p0.y + d.y * t1});
    }
}

/// The isometric grid: lines at 30 and 150 degrees and vertical ones, all through the
/// isometric snap lattice of vertical spacing `s` (points i*s*cos30, j*s/2, i + j even).
void iso_grid(double s, int major, Vec2 lo, Vec2 hi, int max_lines, GridResult& result) {
    const double c30 = std::sqrt(3.0) * 0.5;
    const double step = s * c30; // between parallel lines of each family
    const Vec2 normals[3] = {{1.0, 0.0}, {-0.5, c30}, {0.5, c30}}; // vertical, 30 deg, 150 deg
    const Vec2 corners[4] = {lo, {hi.x, lo.y}, hi, {lo.x, hi.y}};
    for (const Vec2& n : normals) {
        double dmin = 1e300;
        double dmax = -1e300;
        for (const Vec2& q : corners) {
            const double v = q.x * n.x + q.y * n.y;
            dmin = std::min(dmin, v);
            dmax = std::max(dmax, v);
        }
        const auto k0 = static_cast<long long>(std::ceil(dmin / step - 1e-9));
        const auto k1 = static_cast<long long>(std::floor(dmax / step + 1e-9));
        if (k1 - k0 > max_lines) {
            continue;
        }
        for (long long k = k0; k <= k1; ++k) {
            clip_line(n, static_cast<double>(k) * step, lo, hi,
                      major > 1 && k % major == 0 ? result.major : result.minor);
        }
    }
}
} // namespace

GridResult build_grid(const Camera2D& camera, const core::GridSettings& settings, Vec2 lim_lo, Vec2 lim_hi,
                      int max_lines_per_axis) {
    if (settings.isometric) {
        GridResult result;
        const double scale = camera.scale();
        double s = settings.y > 0.0 ? settings.y : choose_minor_spacing(scale, 14.0);
        const int major = settings.major > 0 ? settings.major : 1;
        for (int k = 0; k < 64 && scale > 0.0 && s * scale < 6.0; ++k) {
            s *= major > 1 ? major : 10; // zoomed out: fewer lines
        }
        result.minor_spacing = s;
        result.major_spacing = s * major;
        Vec2 lo = camera.visible_min();
        Vec2 hi = camera.visible_max();
        if (!settings.beyond_limits) {
            lo = {std::max(lo.x, lim_lo.x), std::max(lo.y, lim_lo.y)};
            hi = {std::min(hi.x, lim_hi.x), std::min(hi.y, lim_hi.y)};
        }
        if (scale > 0.0 && lo.x < hi.x && lo.y < hi.y) {
            iso_grid(s, major, lo, hi, max_lines_per_axis, result);
        }
        return result;
    }
    if (settings.x <= 0.0 || settings.y <= 0.0) {
        return build_grid(camera, 14.0, max_lines_per_axis);
    }
    GridResult result;
    const double scale = camera.scale();
    const int major = settings.major > 0 ? settings.major : 1;
    const auto effective = [&](double s) {
        if (scale <= 0.0) {
            return 0.0;
        }
        if (settings.adaptive) {
            for (int k = 0; k < 64 && s * scale < 6.0 && major > 1; ++k) {
                s *= major; // zoomed out: fewer lines
            }
            for (int k = 0; k < 64 && settings.subdivide && s * scale > 14.0 * major && major > 1; ++k) {
                s /= major; // zoomed in: lines between them
            }
        }
        return s * scale < 2.0 ? 0.0 : s; // too fine to show
    };
    const double sx = effective(settings.x);
    const double sy = effective(settings.y);
    result.minor_spacing = sx;
    result.major_spacing = sx * major;
    if (sx <= 0.0 || sy <= 0.0) {
        return result;
    }
    Vec2 lo = camera.visible_min();
    Vec2 hi = camera.visible_max();
    if (!settings.beyond_limits) {
        lo = {std::max(lo.x, lim_lo.x), std::max(lo.y, lim_lo.y)};
        hi = {std::min(hi.x, lim_hi.x), std::min(hi.y, lim_hi.y)};
        if (lo.x > hi.x || lo.y > hi.y) {
            return result;
        }
    }
    const auto is_major = [major](long long i) { return major > 1 && i % major == 0; };
    // Lines on the grid through the origin; within the limits, inside them only.
    const long long x0 = static_cast<long long>(std::ceil(lo.x / sx - 1e-9));
    const long long x1 = static_cast<long long>(std::floor(hi.x / sx + 1e-9));
    if (x1 - x0 <= max_lines_per_axis) {
        for (long long i = x0; i <= x1; ++i) {
            const double x = static_cast<double>(i) * sx;
            std::vector<Vec2>& dst = is_major(i) ? result.major : result.minor;
            dst.push_back({x, lo.y});
            dst.push_back({x, hi.y});
        }
    }
    const long long y0 = static_cast<long long>(std::ceil(lo.y / sy - 1e-9));
    const long long y1 = static_cast<long long>(std::floor(hi.y / sy + 1e-9));
    if (y1 - y0 <= max_lines_per_axis) {
        for (long long i = y0; i <= y1; ++i) {
            const double y = static_cast<double>(i) * sy;
            std::vector<Vec2>& dst = is_major(i) ? result.major : result.minor;
            dst.push_back({lo.x, y});
            dst.push_back({hi.x, y});
        }
    }
    return result;
}

} // namespace musacad::render

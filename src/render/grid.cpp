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

GridResult build_grid(const Camera2D& camera, const core::GridSettings& settings, Vec2 lim_lo, Vec2 lim_hi,
                      int max_lines_per_axis) {
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

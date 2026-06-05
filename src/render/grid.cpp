#include "musacad/render/grid.hpp"

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

} // namespace musacad::render

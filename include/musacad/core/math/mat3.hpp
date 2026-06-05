#pragma once

#include <array>
#include <cmath>

#include "musacad/core/math/vec2.hpp"

namespace musacad::core {

/// 3x3 matrix, column-major, used for 2D affine/homogeneous transforms.
/// Element (row r, col c) is at m[c * 3 + r].
struct Mat3 {
    std::array<double, 9> m{};

    [[nodiscard]] static constexpr Mat3 identity() noexcept {
        Mat3 r{};
        r.m[0] = 1.0;
        r.m[4] = 1.0;
        r.m[8] = 1.0;
        return r;
    }

    [[nodiscard]] constexpr double at(int row, int col) const noexcept {
        return m[static_cast<std::size_t>(col * 3 + row)];
    }

    [[nodiscard]] static constexpr Mat3 translation(const Vec2& t) noexcept {
        Mat3 r = identity();
        r.m[6] = t.x;
        r.m[7] = t.y;
        return r;
    }

    [[nodiscard]] static constexpr Mat3 scale(const Vec2& s) noexcept {
        Mat3 r{};
        r.m[0] = s.x;
        r.m[4] = s.y;
        r.m[8] = 1.0;
        return r;
    }

    [[nodiscard]] static Mat3 rotation(double radians) noexcept {
        const double c = std::cos(radians);
        const double s = std::sin(radians);
        Mat3 r = identity();
        r.m[0] = c;
        r.m[1] = s;
        r.m[3] = -s;
        r.m[4] = c;
        return r;
    }

    /// Transform a point (implicit w = 1).
    [[nodiscard]] constexpr Vec2 transform_point(const Vec2& p) const noexcept {
        return {m[0] * p.x + m[3] * p.y + m[6], m[1] * p.x + m[4] * p.y + m[7]};
    }

    /// Transform a direction (implicit w = 0; ignores translation).
    [[nodiscard]] constexpr Vec2 transform_vector(const Vec2& v) const noexcept {
        return {m[0] * v.x + m[3] * v.y, m[1] * v.x + m[4] * v.y};
    }
};

[[nodiscard]] constexpr Mat3 operator*(const Mat3& a, const Mat3& b) noexcept {
    Mat3 r{};
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) {
                sum += a.at(row, k) * b.at(k, col);
            }
            r.m[static_cast<std::size_t>(col * 3 + row)] = sum;
        }
    }
    return r;
}

} // namespace musacad::core

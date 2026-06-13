// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <array>

#include "musacad/core/math/vec3.hpp"

namespace musacad::core {

/// 4x4 matrix, column-major. Element (row r, col c) is at m[c * 4 + r].
/// Part of the math foundation; 3D transforms become load-bearing in a future
/// module. The 2D viewport projection is added in Phase 3 when the renderer
/// needs it.
struct Mat4 {
    std::array<double, 16> m{};

    [[nodiscard]] static constexpr Mat4 identity() noexcept {
        Mat4 r{};
        r.m[0] = 1.0;
        r.m[5] = 1.0;
        r.m[10] = 1.0;
        r.m[15] = 1.0;
        return r;
    }

    [[nodiscard]] constexpr double at(int row, int col) const noexcept {
        return m[static_cast<std::size_t>(col * 4 + row)];
    }

    [[nodiscard]] static constexpr Mat4 translation(const Vec3& t) noexcept {
        Mat4 r = identity();
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }

    [[nodiscard]] static constexpr Mat4 scale(const Vec3& s) noexcept {
        Mat4 r{};
        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        r.m[15] = 1.0;
        return r;
    }

    /// Transform a point (implicit w = 1, no perspective divide).
    [[nodiscard]] constexpr Vec3 transform_point(const Vec3& p) const noexcept {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
                m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }
};

[[nodiscard]] constexpr Mat4 operator*(const Mat4& a, const Mat4& b) noexcept {
    Mat4 r{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) {
                sum += a.at(row, k) * b.at(k, col);
            }
            r.m[static_cast<std::size_t>(col * 4 + row)] = sum;
        }
    }
    return r;
}

} // namespace musacad::core

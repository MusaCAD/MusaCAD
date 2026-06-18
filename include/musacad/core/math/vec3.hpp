// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cmath>

namespace musacad::core {

/// 3D vector / point. Scalar type is double. Present in the math foundation so
/// the kernel interface can speak 3D at its type boundary; 3D geometry itself
/// is a future module.
struct Vec3 {
    double x{};
    double y{};
    double z{};

    constexpr Vec3() noexcept = default;
    constexpr Vec3(double x_, double y_, double z_) noexcept : x{x_}, y{y_}, z{z_} {}

    friend constexpr bool operator==(const Vec3&, const Vec3&) noexcept = default;

    constexpr Vec3& operator+=(const Vec3& o) noexcept { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr Vec3& operator-=(const Vec3& o) noexcept { x -= o.x; y -= o.y; z -= o.z; return *this; }
    constexpr Vec3& operator*=(double s) noexcept { x *= s; y *= s; z *= s; return *this; }
    constexpr Vec3& operator/=(double s) noexcept { x /= s; y /= s; z /= s; return *this; }
};

[[nodiscard]] constexpr Vec3 operator+(const Vec3& a, const Vec3& b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
[[nodiscard]] constexpr Vec3 operator-(const Vec3& a, const Vec3& b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
[[nodiscard]] constexpr Vec3 operator-(const Vec3& a) noexcept { return {-a.x, -a.y, -a.z}; }
[[nodiscard]] constexpr Vec3 operator*(const Vec3& a, double s) noexcept {
    return {a.x * s, a.y * s, a.z * s};
}
[[nodiscard]] constexpr Vec3 operator*(double s, const Vec3& a) noexcept { return a * s; }
[[nodiscard]] constexpr Vec3 operator/(const Vec3& a, double s) noexcept {
    return {a.x / s, a.y / s, a.z / s};
}

[[nodiscard]] constexpr double dot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
[[nodiscard]] constexpr Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] constexpr double length_squared(const Vec3& a) noexcept { return dot(a, a); }
[[nodiscard]] inline double length(const Vec3& a) noexcept { return std::sqrt(dot(a, a)); }

[[nodiscard]] inline Vec3 normalized(const Vec3& a) noexcept {
    const double len = length(a);
    return len > 0.0 ? a / len : a;
}

} // namespace musacad::core

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <cmath>

namespace musacad::core {

/// 2D vector / point. Scalar type is double (CAD precision).
struct Vec2 {
    double x{};
    double y{};

    constexpr Vec2() noexcept = default;
    constexpr Vec2(double x_, double y_) noexcept : x{x_}, y{y_} {}

    friend constexpr bool operator==(const Vec2&, const Vec2&) noexcept = default;

    constexpr Vec2& operator+=(const Vec2& o) noexcept { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(const Vec2& o) noexcept { x -= o.x; y -= o.y; return *this; }
    constexpr Vec2& operator*=(double s) noexcept { x *= s; y *= s; return *this; }
    constexpr Vec2& operator/=(double s) noexcept { x /= s; y /= s; return *this; }
};

[[nodiscard]] constexpr Vec2 operator+(const Vec2& a, const Vec2& b) noexcept {
    return {a.x + b.x, a.y + b.y};
}
[[nodiscard]] constexpr Vec2 operator-(const Vec2& a, const Vec2& b) noexcept {
    return {a.x - b.x, a.y - b.y};
}
[[nodiscard]] constexpr Vec2 operator-(const Vec2& a) noexcept { return {-a.x, -a.y}; }
[[nodiscard]] constexpr Vec2 operator*(const Vec2& a, double s) noexcept {
    return {a.x * s, a.y * s};
}
[[nodiscard]] constexpr Vec2 operator*(double s, const Vec2& a) noexcept { return a * s; }
[[nodiscard]] constexpr Vec2 operator/(const Vec2& a, double s) noexcept {
    return {a.x / s, a.y / s};
}

[[nodiscard]] constexpr double dot(const Vec2& a, const Vec2& b) noexcept {
    return a.x * b.x + a.y * b.y;
}
/// 2D scalar cross product (z component of the 3D cross).
[[nodiscard]] constexpr double cross(const Vec2& a, const Vec2& b) noexcept {
    return a.x * b.y - a.y * b.x;
}
[[nodiscard]] constexpr double length_squared(const Vec2& a) noexcept { return dot(a, a); }
[[nodiscard]] inline double length(const Vec2& a) noexcept { return std::sqrt(dot(a, a)); }

[[nodiscard]] inline double distance(const Vec2& a, const Vec2& b) noexcept {
    return length(b - a);
}

/// Returns a unit-length copy. If the vector is (near) zero, returns it unchanged.
[[nodiscard]] inline Vec2 normalized(const Vec2& a) noexcept {
    const double len = length(a);
    return len > 0.0 ? a / len : a;
}

/// Perpendicular (90 degrees CCW).
[[nodiscard]] constexpr Vec2 perpendicular(const Vec2& a) noexcept { return {-a.y, a.x}; }

/// Linear interpolation, t in [0,1].
[[nodiscard]] constexpr Vec2 lerp(const Vec2& a, const Vec2& b, double t) noexcept {
    return a + (b - a) * t;
}

} // namespace musacad::core

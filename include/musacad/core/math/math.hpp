#pragma once

// Aggregate math header for the Musa CAD core. Own types only -- no external
// math library. Geometry uses double-precision scalars.

#include <cmath>

#include "musacad/core/math/mat3.hpp"
#include "musacad/core/math/mat4.hpp"
#include "musacad/core/math/vec2.hpp"
#include "musacad/core/math/vec3.hpp"

namespace musacad::core {

/// Geometry scalar type.
using Scalar = double;

inline constexpr double kPi = 3.141592653589793238462643383279502884;
inline constexpr double kTwoPi = 2.0 * kPi;
inline constexpr double kHalfPi = 0.5 * kPi;

/// Default chordal tolerance (world units) for tessellating curves.
inline constexpr double kDefaultTessTolerance = 0.01;

[[nodiscard]] constexpr double to_radians(double degrees) noexcept {
    return degrees * (kPi / 180.0);
}
[[nodiscard]] constexpr double to_degrees(double radians) noexcept {
    return radians * (180.0 / kPi);
}

/// An arc segment of a polyline, recovered from a vertex bulge.
struct BulgeArc {
    Vec2 center;
    double radius = 0.0; ///< always >= 0
    double a0 = 0.0;     ///< start angle (of p0), radians
    double sweep = 0.0;  ///< signed included angle (CCW positive); |sweep| = arc angle
};

/// Resolves the arc through `p0`->`p1` whose AutoCAD bulge is `bulge`
/// (bulge = tan(theta/4); 0 = straight). Returns the circle centre, radius, the
/// start angle at `p0`, and the signed sweep. For `bulge == 0` the radius is 0 and
/// the caller should treat the segment as a straight chord.
[[nodiscard]] inline BulgeArc arc_from_bulge(Vec2 p0, Vec2 p1, double bulge) noexcept {
    BulgeArc out;
    const Vec2 chord = p1 - p0;
    const double len = length(chord);
    if (bulge == 0.0 || len < 1e-12) {
        return out; // straight (radius 0)
    }
    const double theta = 4.0 * std::atan(bulge); // signed included angle
    const double half = 0.5 * theta;
    const double r = len / (2.0 * std::sin(half)); // signed
    const Vec2 dir = chord / len;
    const Vec2 perp{-dir.y, dir.x};
    const Vec2 mid = (p0 + p1) * 0.5;
    const Vec2 center = mid + perp * (r * std::cos(half));
    out.center = center;
    out.radius = std::abs(r);
    out.a0 = std::atan2(p0.y - center.y, p0.x - center.x);
    out.sweep = theta;
    return out;
}

} // namespace musacad::core

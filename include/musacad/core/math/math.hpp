#pragma once

// Aggregate math header for the Musa CAD core. Own types only -- no external
// math library. Geometry uses double-precision scalars.

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

} // namespace musacad::core

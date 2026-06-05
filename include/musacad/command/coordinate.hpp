#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "musacad/core/math/math.hpp"

namespace musacad::command {

/// Result of parsing a coordinate token.
struct CoordParse {
    bool ok = false;
    core::Vec2 point{};
    std::string interpretation; ///< human-readable echo of how it was understood
    std::string error;          ///< set when ok == false
};

/// Parses an AutoCAD-style coordinate:
///   * absolute        `x,y`
///   * relative        `@dx,dy`        (offset from `last`)
///   * polar relative  `@dist<angle`   (angle in degrees, CCW from +X)
/// Relative forms require `last`; otherwise an error is returned.
[[nodiscard]] CoordParse parse_coordinate(std::string_view text, std::optional<core::Vec2> last);

/// Parses a bare number (for radius, zoom factor, ...). Returns false if the
/// trimmed text is not a single valid number.
[[nodiscard]] bool parse_number(std::string_view text, double& out);

} // namespace musacad::command

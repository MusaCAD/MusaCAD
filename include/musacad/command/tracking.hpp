// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::command {

// Polar tracking and object snap tracking (AutoCAD's AutoTrack). Both work the same way:
// alignment paths -- rays at the polar angles from the last point, lines through the
// points acquired with an object snap -- and a cursor that locks onto a path once it
// comes within the aperture of it, onto the crossing of two when both are in reach.
// Everything here is plain geometry on values, so the command processor, the viewport's
// rubber band and the tests share one rule.

/// The settings, as AutoCAD's system variables keep them.
struct TrackingSettings {
    double polar_increment = core::kHalfPi; ///< POLARANG, radians (90 degrees to begin with)
    std::vector<double> additional;         ///< POLARADDANG, radians
    bool use_additional = false;            ///< POLARMODE bit 4
    bool relative = false;                  ///< POLARMODE bit 1: measured from the last segment
    bool otrack_polar = false;              ///< POLARMODE bit 2: tracking along every polar angle
    double polar_distance = 0.0;            ///< POLARDIST: the PolarSnap distance
    bool polar_snap = false;                ///< SNAPTYPE 1 (PolarSnap) with SNAP on

    [[nodiscard]] int polarmode() const noexcept {
        return (relative ? 1 : 0) | (otrack_polar ? 2 : 0) | (use_additional ? 4 : 0);
    }
    void set_polarmode(int v) noexcept {
        relative = (v & 1) != 0;
        otrack_polar = (v & 2) != 0;
        use_additional = (v & 4) != 0;
    }
};

/// A point acquired for object snap tracking: where, and what it was ("Endpoint").
struct TrackPoint {
    core::Vec2 at{};
    std::string label;
};

/// One alignment path the cursor has locked onto.
struct TrackHit {
    core::Vec2 from{};   ///< the last point (polar) or the acquired point
    double angle = 0.0;  ///< the path's direction from `from`, radians in [0, 2 pi)
    double length = 0.0; ///< how far along it the point is
    bool polar = false;
    bool relative = false; ///< a polar angle measured from the last segment
    std::string label;     ///< the acquired point's label
};

struct TrackResult {
    core::Vec2 point{};         ///< the cursor, or where tracking put it
    std::vector<TrackHit> hits; ///< none (free), one path, or the two that cross
};

/// The directions polar tracking stops at, in [0, 2 pi), ascending: the multiples of the
/// increment and the additional angles, from `base` (the last segment's direction) when
/// the settings say relative.
[[nodiscard]] std::vector<double> polar_angles(const TrackingSettings& s, double base);

/// Where the cursor goes. `last` is the point polar tracking runs from and `base` the
/// direction of the segment that reached it; `acquired` are the object snap tracking
/// points; `aperture` is the reach of a path, in world units.
[[nodiscard]] TrackResult track(const TrackingSettings& s, bool polar_on, bool otrack_on,
                                core::Vec2 cursor, std::optional<core::Vec2> last, double base,
                                std::span<const TrackPoint> acquired, double aperture);

} // namespace musacad::command

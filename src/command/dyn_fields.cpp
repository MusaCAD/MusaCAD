// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#include "musacad/command/dyn_fields.hpp"

#include <cmath>

namespace musacad::command {

namespace {
constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

double sign_of(double v) {
    return v < 0.0 ? -1.0 : 1.0;
}
std::string num(double v) {
    return std::to_string(v);
}
} // namespace

std::vector<DynField> dyn_fields(const PreviewSpec& pv, core::Vec2 cursor) {
    std::vector<DynField> out;
    if (pv.points.empty()) {
        return out;
    }
    const core::Vec2 a = pv.points[0];

    switch (pv.kind) {
    case PreviewKind::Segment: {
        const double len = core::distance(a, cursor);
        const double ang = std::atan2(cursor.y - a.y, cursor.x - a.x) * kRadToDeg;
        out.push_back({"Length", {(a.x + cursor.x) * 0.5, (a.y + cursor.y) * 0.5}, len, false, 0});
        out.push_back({"Angle", cursor, ang, true, 1});
        break;
    }
    case PreviewKind::Circle: {
        const double r = core::distance(a, cursor);
        out.push_back({"Radius", {(a.x + cursor.x) * 0.5, (a.y + cursor.y) * 0.5}, r, false, 0});
        break;
    }
    case PreviewKind::Rectangle: {
        // When the command's Dimensions/Area option has fixed the size, mirror that
        // (quadrant-flipped) corner; otherwise the cursor is the opposite corner.
        core::Vec2 b = cursor;
        if (pv.fixed_w > 0.0 && pv.fixed_h > 0.0) {
            b = {a.x + sign_of(cursor.x - a.x) * pv.fixed_w,
                 a.y + sign_of(cursor.y - a.y) * pv.fixed_h};
        }
        const double w = std::abs(b.x - a.x);
        const double h = std::abs(b.y - a.y);
        out.push_back({"Length", {(a.x + b.x) * 0.5, a.y}, w, false, 0}); // along the first edge
        out.push_back({"Width", {b.x, (a.y + b.y) * 0.5}, h, false, 1});  // along the side edge
        break;
    }
    default:
        break;
    }
    return out;
}

std::string compose_dyn_submit(const PreviewSpec& pv, core::Vec2 cursor,
                               std::optional<double> primary, std::optional<double> secondary) {
    const core::Vec2 a = pv.points.empty() ? core::Vec2{0, 0} : pv.points[0];

    switch (pv.kind) {
    case PreviewKind::Segment: {
        if (!primary && !secondary) {
            return {}; // need at least one typed value to override the cursor
        }
        const double live_len = core::distance(a, cursor);
        const double live_ang = std::atan2(cursor.y - a.y, cursor.x - a.x) * kRadToDeg;
        const double len = primary ? *primary : live_len;
        const double ang = secondary ? *secondary : live_ang;
        return "@" + num(len) + "<" + num(ang);
    }
    case PreviewKind::Circle: {
        if (!primary) {
            return {};
        }
        double ang = std::atan2(cursor.y - a.y, cursor.x - a.x) * kRadToDeg;
        if (core::distance(a, cursor) < 1e-9) {
            ang = 0.0;
        }
        return "@" + num(*primary) + "<" + num(ang);
    }
    case PreviewKind::Rectangle: {
        if (!primary && !secondary) {
            return {};
        }
        const double live_w = cursor.x - a.x;
        const double live_h = cursor.y - a.y;
        // A typed dimension is a magnitude; its sign follows the cursor quadrant so
        // the rectangle still extends the way the user is dragging (NE/NW/SE/SW).
        const double w = primary ? sign_of(live_w) * std::abs(*primary) : live_w;
        const double h = secondary ? sign_of(live_h) * std::abs(*secondary) : live_h;
        return "@" + num(w) + "," + num(h);
    }
    default:
        return {};
    }
}

core::Vec2 apply_dyn_lock(const PreviewSpec& pv, core::Vec2 cursor, std::optional<double> primary,
                          std::optional<double> secondary) {
    if (!primary && !secondary) {
        return cursor;
    }
    const core::Vec2 a = pv.points.empty() ? core::Vec2{0, 0} : pv.points[0];

    switch (pv.kind) {
    case PreviewKind::Segment: {
        const double live_len = core::distance(a, cursor);
        const double live_ang = std::atan2(cursor.y - a.y, cursor.x - a.x) * kRadToDeg;
        const double len = primary ? *primary : live_len;
        const double ang = (secondary ? *secondary : live_ang) * kDegToRad;
        return {a.x + len * std::cos(ang), a.y + len * std::sin(ang)};
    }
    case PreviewKind::Circle: {
        const double live_r = core::distance(a, cursor);
        const double r = primary ? *primary : live_r;
        core::Vec2 dir = cursor - a;
        if (core::length(dir) < 1e-9) {
            dir = {1.0, 0.0};
        }
        return a + core::normalized(dir) * r;
    }
    case PreviewKind::Rectangle: {
        const double live_w = cursor.x - a.x;
        const double live_h = cursor.y - a.y;
        const double w = primary ? sign_of(live_w) * std::abs(*primary) : live_w;
        const double h = secondary ? sign_of(live_h) * std::abs(*secondary) : live_h;
        return {a.x + w, a.y + h};
    }
    default:
        return cursor;
    }
}

} // namespace musacad::command

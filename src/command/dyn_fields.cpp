// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/command/dyn_fields.hpp"

#include <array>
#include <cctype>

#include <cmath>

#include "musacad/core/grips.hpp"

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
// The point a drag is measured from: the first anchor, or the last vertex of a polyline
// being drawn.
core::Vec2 anchor_of(const PreviewSpec& pv) {
    if (pv.points.empty()) {
        return {0.0, 0.0};
    }
    return pv.kind == PreviewKind::Polyline ? pv.points.back() : pv.points[0];
}
} // namespace

bool dyn_segment_like(const PreviewSpec& pv) noexcept {
    return pv.kind == PreviewKind::Segment ||
           (pv.kind == PreviewKind::Polyline && pv.pline_dyn && !pv.points.empty());
}

namespace {
// A hot grip's fields, in the order shown: 0 the length change, 1 the resulting length (or
// radius), 2 the absolute angle, 3 the angle change -- as many as DYNDIVIS shows of those
// DYNDIGRIP asks for.
std::vector<int> grip_roles(const PreviewSpec& pv) {
    const int bits = dyndigrip();
    std::vector<int> roles;
    if ((bits & 2) != 0) {
        roles.push_back(0);
    }
    if ((bits & (pv.grip_radius ? 1 | 16 : 1)) != 0) {
        roles.push_back(1);
    }
    if (!pv.grip_radius) {
        if ((bits & 4) != 0) {
            roles.push_back(2);
        }
        if ((bits & 8) != 0) {
            roles.push_back(3);
        }
    }
    const int divis = dyndivis();
    const std::size_t shown = divis <= 0 ? 1 : divis == 1 ? 2 : roles.size();
    if (roles.size() > shown) {
        roles.resize(shown);
    }
    return roles;
}

double turn_deg(double d) {
    while (d > 180.0) {
        d -= 360.0;
    }
    while (d <= -180.0) {
        d += 360.0;
    }
    return d;
}

void grip_fields(const PreviewSpec& pv, core::Vec2 cursor, std::vector<DynField>& out) {
    const core::Vec2 a = *pv.grip_anchor;
    const core::Vec2 g = pv.points[0];
    const double len = core::distance(a, cursor);
    const double len0 = core::distance(a, g);
    const double ang0 = std::atan2(g.y - a.y, g.x - a.x) * kRadToDeg;
    const double ang = len > 1e-12 ? std::atan2(cursor.y - a.y, cursor.x - a.x) * kRadToDeg : ang0;
    const core::Vec2 u = len > 1e-12 ? (cursor - a) * (1.0 / len) : core::Vec2{1.0, 0.0};
    const core::Vec2 was = a + u * len0; // the old length along the new direction
    const std::vector<int> roles = grip_roles(pv);
    for (std::size_t i = 0; i < roles.size(); ++i) {
        const int slot = static_cast<int>(i);
        switch (roles[i]) {
        case 0:
            out.push_back({pv.grip_radius ? "Radius change" : "Length change", (was + cursor) * 0.5, len - len0,
                           false, slot});
            break;
        case 1:
            out.push_back({pv.grip_radius ? "Radius" : "Length", (a + cursor) * 0.5, len, false, slot});
            break;
        case 2:
            out.push_back({"Angle", cursor, ang, true, slot});
            break;
        default:
            out.push_back({"Angle change", a + (cursor - a) * 0.25, turn_deg(ang - ang0), true, slot});
            break;
        }
    }
}
} // namespace

bool dyn_grip_measured(const PreviewSpec& pv) noexcept {
    return pv.grip_hot && pv.grip_anchor.has_value() && !pv.points.empty();
}

core::Vec2 grip_dyn_point(const PreviewSpec& pv, core::Vec2 cursor, std::span<const std::optional<double>> typed) {
    if (!dyn_grip_measured(pv)) {
        return cursor;
    }
    const core::Vec2 a = *pv.grip_anchor;
    const core::Vec2 g = pv.points[0];
    const double len0 = core::distance(a, g);
    const double ang0 = std::atan2(g.y - a.y, g.x - a.x);
    double len = core::distance(a, cursor);
    double ang = len > 1e-12 ? std::atan2(cursor.y - a.y, cursor.x - a.x) : ang0;
    const std::vector<int> roles = grip_roles(pv);
    bool any = false;
    std::array<std::optional<double>, 4> v{};
    for (std::size_t i = 0; i < roles.size() && i < typed.size(); ++i) {
        if (typed[i]) {
            v[static_cast<std::size_t>(roles[i])] = typed[i];
            any = true;
        }
    }
    if (!any) {
        return cursor;
    }
    if (v[1]) {
        len = *v[1];
    } else if (v[0]) {
        len = len0 + *v[0];
    }
    if (v[2]) {
        ang = *v[2] * kDegToRad;
    } else if (v[3]) {
        ang = ang0 + *v[3] * kDegToRad;
    }
    return {a.x + len * std::cos(ang), a.y + len * std::sin(ang)};
}

std::string compose_grip_submit(const PreviewSpec& pv, core::Vec2 cursor, std::span<const std::optional<double>> typed) {
    bool any = false;
    for (const std::optional<double>& v : typed) {
        any = any || v.has_value();
    }
    if (!any || !dyn_grip_measured(pv)) {
        return {};
    }
    const core::Vec2 p = grip_dyn_point(pv, cursor, typed);
    return "#" + num(p.x) + "," + num(p.y);
}

std::optional<core::Vec2> grip_dyn_anchor(std::span<const core::GripInfo> grips, core::EntityHandle h,
                                          std::uint32_t index, bool& radius) {
    radius = false;
    const auto at = [&](std::uint32_t i) -> std::optional<core::Vec2> {
        for (const core::GripInfo& g : grips) {
            if (g.handle == h && g.index == i) {
                return g.pos;
            }
        }
        return std::nullopt;
    };
    switch (h.kind) {
    case core::EntityKind::Line:
        return index <= 1 ? at(1 - index) : std::nullopt;
    case core::EntityKind::Circle:
        radius = index >= 1 && index <= 4;
        return radius ? at(0) : std::nullopt;
    case core::EntityKind::Arc:
        radius = index == 3;
        return radius ? at(0) : std::nullopt;
    case core::EntityKind::Polyline: {
        if (index >= core::kSegmentGripBase) {
            return std::nullopt;
        }
        std::uint32_t verts = 0;
        std::uint32_t segs = 0;
        for (const core::GripInfo& g : grips) {
            if (g.handle == h) {
                (g.index >= core::kSegmentGripBase ? segs : verts) += 1;
            }
        }
        if (verts < 2) {
            return std::nullopt;
        }
        if (index > 0) {
            return at(index - 1);
        }
        return segs == verts ? at(verts - 1) : at(1); // closed: the last; open: the next
    }
    default:
        return std::nullopt;
    }
}

std::vector<DynField> dyn_fields(const PreviewSpec& pv, core::Vec2 cursor) {
    std::vector<DynField> out;
    if (pv.points.empty()) {
        return out;
    }
    if (dyn_grip_measured(pv)) {
        grip_fields(pv, cursor, out);
        return out;
    }
    const core::Vec2 a = anchor_of(pv);

    switch (dyn_segment_like(pv) ? PreviewKind::Segment : pv.kind) {
    case PreviewKind::Segment: {
        const double len = core::distance(a, cursor);
        const double ang = std::atan2(cursor.y - a.y, cursor.x - a.x) * kRadToDeg;
        out.push_back({"Length", {(a.x + cursor.x) * 0.5, (a.y + cursor.y) * 0.5}, len, false, 0});
        out.push_back({"Angle", cursor, ang, true, 1});
        break;
    }
    case PreviewKind::Circle: {
        // Centre + radius: the radius; centre + diameter and 2P: the diameter (the drag
        // distance); 3P: no field (three points define it).
        if (pv.circle_mode == 3) {
            break;
        }
        const double d = core::distance(a, cursor);
        out.push_back({pv.circle_mode == 0 ? "Radius" : "Diameter",
                       {(a.x + cursor.x) * 0.5, (a.y + cursor.y) * 0.5}, d, false, 0});
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
    const core::Vec2 a = anchor_of(pv);

    switch (dyn_segment_like(pv) ? PreviewKind::Segment : pv.kind) {
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
        if (!primary || pv.circle_mode == 3) {
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
    const core::Vec2 a = anchor_of(pv);

    switch (dyn_segment_like(pv) ? PreviewKind::Segment : pv.kind) {
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

int& dynpicoords() noexcept {
    static int value = 0;
    return value;
}

int& dyndigrip() noexcept {
    static int value = 31;
    return value;
}

int& dyndivis() noexcept {
    static int value = 1;
    return value;
}

std::optional<std::string> dyn_pointer_input(std::string_view typed, bool next) {
    while (!typed.empty() && (typed.front() == ' ' || typed.front() == '\t')) {
        typed.remove_prefix(1);
    }
    while (!typed.empty() && (typed.back() == ' ' || typed.back() == '\t')) {
        typed.remove_suffix(1);
    }
    if (typed.empty()) {
        return std::nullopt;
    }
    const char lead = typed.front();
    const std::string_view body = lead == '#' || lead == '@' ? typed.substr(1) : typed;
    if (body.find(',') == std::string_view::npos && body.find('<') == std::string_view::npos) {
        return std::nullopt; // a distance, a value or an option: not a coordinate
    }
    if (lead == '#') {
        return std::string(body); // absolute
    }
    if (lead == '@' || !next || dynpicoords() == 1) {
        return std::string(typed);
    }
    return "@" + std::string(typed); // relative to the point before, AutoCAD's default
}

bool prompt_asks_point(std::string_view prompt) {
    std::string p;
    for (const char c : prompt.substr(0, prompt.find('['))) {
        p += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (p.rfind("specify", 0) != 0 && p.rfind("pick", 0) != 0) {
        return false;
    }
    for (const char* word : {"point", "corner", "center", "centre", "location", "position", "vertex"}) {
        if (p.find(word) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace musacad::command

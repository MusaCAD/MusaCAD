// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/command/commands.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <optional>
#include <string>

#include "musacad/command/coordinate.hpp"
#include "musacad/core/hatch_pattern.hpp"
#include "musacad/core/ellipse.hpp"
#include "musacad/core/polygon.hpp"
#include "musacad/core/spline_eval.hpp"
#include "musacad/core/units.hpp"
#include "musacad/core/polyline_ops.hpp"

namespace musacad::command {

namespace {

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string trimmed(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

/// Parses a point, echoing the interpretation on success or the error on
/// failure. Returns nullopt (after echoing the error) when invalid -- the caller
/// re-prompts rather than aborting.
std::optional<core::Vec2> read_point(CommandContext& ctx, const std::string& text) {
    const CoordParse p = parse_coordinate(text, ctx.last_point());
    if (!p.ok) {
        ctx.echo(p.error);
        return std::nullopt;
    }
    ctx.echo("  = " + p.interpretation);
    return p.point;
}

/// Circumcircle of three points. Returns false if (near) collinear.
bool circumcircle(core::Vec2 a, core::Vec2 b, core::Vec2 c, core::Vec2& center, double& radius) {
    const double d = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    if (std::abs(d) < 1e-12) {
        return false;
    }
    const double a2 = core::length_squared(a);
    const double b2 = core::length_squared(b);
    const double c2 = core::length_squared(c);
    center.x = (a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d;
    center.y = (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d;
    radius = core::distance(center, a);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// LINE
// ---------------------------------------------------------------------------
void LineCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Specify first point: ");
}

void LineCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (points_.empty()) {
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            points_.push_back(*p);
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Segment, {points_.back()}});
            ctx.set_prompt("Specify next point or [Undo]: ");
        }
        return;
    }
    if (t.empty()) {
        done_ = true; // Enter ends LINE
        return;
    }
    if (upper(t) == "U") {
        if (points_.size() >= 2) {
            points_.pop_back();
            ctx.submit(core::UndoLastOpCommand{});
            ctx.set_last_point(points_.back());
            ctx.set_preview({PreviewKind::Segment, {points_.back()}});
            ctx.echo("Undo last segment");
        } else {
            points_.clear();
            ctx.clear_last_point();
            ctx.clear_preview();
            ctx.set_prompt("Specify first point: ");
        }
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        ctx.submit(core::AddLineCommand{points_.back(), *p, ctx.group_id()});
        points_.push_back(*p);
        ctx.set_last_point(*p);
        ctx.set_preview({PreviewKind::Segment, {points_.back()}});
    }
}

void LineCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// CIRCLE
// ---------------------------------------------------------------------------
void CircleCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Specify center point: ");
}

void CircleCommand::input(CommandContext& ctx, const std::string& text) {
    if (state_ == State::Center) {
        if (const auto p = read_point(ctx, text)) {
            center_ = *p;
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Circle, {center_}});
            state_ = State::Radius;
            ctx.set_prompt("Specify radius or [Diameter]: ");
        }
        return;
    }
    // The [Diameter] option keyword -- switches the value step to diameter. Works
    // identically from the command line and Dynamic Input (both feed input()).
    if (state_ == State::Radius) {
        const std::string up = upper(trimmed(text));
        if (up == "D" || up == "DIAMETER") {
            state_ = State::Diameter;
            ctx.set_prompt("Specify diameter: ");
            return;
        }
    }
    const bool by_diameter = state_ == State::Diameter;
    double value = 0.0;
    if (parse_number(text, value)) {
        // explicit radius/diameter
    } else if (const auto p = read_point(ctx, text)) {
        value = core::distance(center_, *p);
        if (by_diameter) {
            value *= 2.0; // a picked point gives the radius distance -> diameter
        }
    } else {
        return; // read_point already echoed the error
    }
    const double radius = by_diameter ? value * 0.5 : value;
    if (radius <= 0.0) {
        ctx.echo("Value must be positive.");
        return;
    }
    ctx.submit(core::AddCircleCommand{center_, radius, ctx.group_id()});
    ctx.echo("Circle: radius " + std::to_string(radius));
    done_ = true;
}

void CircleCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// PLINE
// ---------------------------------------------------------------------------
void PolylineCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Specify start point: ");
}

void PolylineCommand::prompt_next(CommandContext& ctx) {
    ctx.set_prompt("Specify next point or [Close/Undo]: ");
}

void PolylineCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (points_.empty()) {
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            points_.push_back(*p);
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Polyline, points_});
            prompt_next(ctx);
        }
        return;
    }
    if (t.empty()) {
        if (points_.size() >= 2) {
            ctx.submit(core::AddPolylineCommand{points_, false, ctx.group_id()});
            ctx.echo("Polyline created (" + std::to_string(points_.size()) + " vertices).");
        }
        done_ = true;
        return;
    }
    if (upper(t) == "C") {
        if (points_.size() >= 3) {
            ctx.submit(core::AddPolylineCommand{points_, true, ctx.group_id()});
            ctx.echo("Closed polyline created.");
            done_ = true;
        } else {
            ctx.echo("Need at least 3 points to close.");
        }
        return;
    }
    if (upper(t) == "U") {
        points_.pop_back();
        if (points_.empty()) {
            ctx.clear_last_point();
            ctx.clear_preview();
            ctx.set_prompt("Specify start point: ");
        } else {
            ctx.set_last_point(points_.back());
            ctx.set_preview({PreviewKind::Polyline, points_});
            prompt_next(ctx);
        }
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        points_.push_back(*p);
        ctx.set_last_point(*p);
        ctx.set_preview({PreviewKind::Polyline, points_});
    }
}

void PolylineCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// ARC (three-point)
// ---------------------------------------------------------------------------
void ArcCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Specify start point of arc: ");
}

void ArcCommand::input(CommandContext& ctx, const std::string& text) {
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    points_.push_back(*p);
    ctx.set_last_point(*p);
    if (points_.size() == 1) {
        ctx.set_preview({PreviewKind::Segment, {points_[0]}});
        ctx.set_prompt("Specify second point of arc: ");
        return;
    }
    if (points_.size() == 2) {
        ctx.set_preview({PreviewKind::Arc, {points_[0], points_[1]}});
        ctx.set_prompt("Specify end point of arc: ");
        return;
    }
    // Three points: build the arc.
    core::Vec2 center{};
    double radius = 0.0;
    if (!circumcircle(points_[0], points_[1], points_[2], center, radius)) {
        ctx.echo("Points are collinear; specify a different end point.");
        points_.pop_back();
        ctx.set_last_point(points_.back());
        return;
    }
    const auto ang = [&](core::Vec2 q) { return std::atan2(q.y - center.y, q.x - center.x); };
    const double a1 = ang(points_[0]);
    const double a2 = ang(points_[1]);
    const double a3 = ang(points_[2]);
    const auto rel = [](double x, double base) {
        double r = x - base;
        while (r < 0.0) {
            r += core::kTwoPi;
        }
        return r;
    };
    // Choose start/end so the CCW sweep from start passes through the second point.
    double start_angle = a1;
    double end_angle = a3;
    if (rel(a2, a1) > rel(a3, a1)) {
        start_angle = a3;
        end_angle = a1;
    }
    ctx.submit(core::AddArcCommand{center, radius, start_angle, end_angle, ctx.group_id()});
    ctx.echo("Arc: radius " + std::to_string(radius));
    done_ = true;
}

void ArcCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// RECTANGLE
// ---------------------------------------------------------------------------
void RectangleCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    // Elevation/Thickness are 3D and Width needs polyline width, which this model does
    // not have, so only the two corner treatments are offered -- an option that cannot
    // work is worse than a shorter prompt.
    ctx.set_prompt("Specify first corner point or [Chamfer/Fillet]: ");
}

void RectangleCommand::input(CommandContext& ctx, const std::string& text) {
    constexpr const char* kCornerPrompt = "Specify other corner or [Area/Dimensions/Rotation]: ";
    const std::string up = upper(trimmed(text));

    // Push the cursor preview for the current state: corner-to-corner by default, or a
    // FIXED-SIZE quadrant-flip rectangle once dimensions/area are chosen. Carries rotation.
    const auto refresh_preview = [&] {
        PreviewSpec pv{PreviewKind::Rectangle, {first_}};
        if (has_dims_) {
            pv.fixed_w = length_;
            pv.fixed_h = width_;
        }
        pv.rect_rotation = rotation_;
        // Every state except the corner pick is a single scalar/keyword sub-prompt:
        // with DYN on it shows the at-cursor cell, not the two-field corner drag.
        pv.scalar_prompt = state_ != State::First && state_ != State::AwaitCorner;
        ctx.set_preview(pv);
    };
    // The four corners from first_ to `other`, rotated about first_.
    const auto corners = [&](core::Vec2 other) {
        std::vector<core::Vec2> c{{first_.x, first_.y},
                                  {other.x, first_.y},
                                  {other.x, other.y},
                                  {first_.x, other.y}};
        if (rotation_ != 0.0) {
            const double cs = std::cos(rotation_);
            const double sn = std::sin(rotation_);
            for (core::Vec2& q : c) {
                const double dx = q.x - first_.x;
                const double dy = q.y - first_.y;
                q = {first_.x + dx * cs - dy * sn, first_.y + dx * sn + dy * cs};
            }
        }
        return c;
    };
    // Commit the closed polyline, with every corner rounded or chamfered by the SAME
    // routine the FILLET / CHAMFER commands use on a picked corner, so the two can never
    // disagree. A treatment that does not fit falls back to square corners and says so,
    // which is what AutoCAD does with an oversized radius.
    const auto commit = [&](core::Vec2 other) {
        std::vector<core::Vec2> c = corners(other);
        std::vector<double> bulges;
        bool shaped = true;
        if (fillet_r_ > 0.0) {
            bulges.assign(4, 0.0);
            for (int i = 3; i >= 0 && shaped; --i) { // descending: inserts never shift the rest
                shaped = core::polyline_ops::fillet_corner(c, bulges, true, i, fillet_r_);
            }
            if (!shaped) {
                ctx.echo("Fillet radius too large for this rectangle: drawn with square corners.");
            }
        } else if (chamfer_d1_ > 0.0 || chamfer_d2_ > 0.0) {
            for (int i = 3; i >= 0 && shaped; --i) {
                shaped = core::polyline_ops::chamfer_corner(c, true, i, chamfer_d1_, chamfer_d2_);
            }
            if (!shaped) {
                ctx.echo("Chamfer distances too large for this rectangle: drawn with square corners.");
            }
        }
        if (!shaped) {
            c = corners(other);
            bulges.clear();
        }
        core::AddPolylineCommand poly;
        poly.points = std::move(c);
        poly.closed = true;
        poly.group = ctx.group_id();
        poly.bulges = std::move(bulges);
        ctx.submit(std::move(poly));
        ctx.echo("Rectangle created.");
        done_ = true;
    };
    const auto fmt4 = [](double v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4f", v);
        return std::string(buf);
    };
    // A non-numeric entry at a value prompt must not trap the user: drop back to the
    // other-corner pick (AutoCAD-style), preserving any dims/rotation already chosen.
    const auto revert_to_corner = [&] {
        state_ = State::AwaitCorner;
        refresh_preview();
        ctx.set_prompt(kCornerPrompt);
    };

    switch (state_) {
    case State::First:
        if (up == "C" || up == "CHAMFER") {
            state_ = State::ChamferD1;
            ctx.set_prompt("Specify first chamfer distance for rectangles <" + fmt4(chamfer_d1_) +
                           ">: ");
            return;
        }
        if (up == "F" || up == "FILLET") {
            state_ = State::FilletR;
            ctx.set_prompt("Specify fillet radius for rectangles <" + fmt4(fillet_r_) + ">: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            first_ = *p;
            ctx.set_last_point(*p);
            state_ = State::AwaitCorner;
            refresh_preview();
            ctx.set_prompt(kCornerPrompt);
        }
        return;

    case State::ChamferD1: {
        double v = chamfer_d1_; // Enter keeps the current default
        if (!up.empty() && (!parse_number(text, v) || v < 0.0)) {
            ctx.echo("Enter a distance of zero or more.");
            return;
        }
        chamfer_d1_ = v;
        state_ = State::ChamferD2;
        // AutoCAD defaults the second distance to the first just entered.
        ctx.set_prompt("Specify second chamfer distance for rectangles <" + fmt4(chamfer_d1_) +
                       ">: ");
        return;
    }
    case State::ChamferD2: {
        double v = chamfer_d1_;
        if (!up.empty() && (!parse_number(text, v) || v < 0.0)) {
            ctx.echo("Enter a distance of zero or more.");
            return;
        }
        chamfer_d2_ = v;
        fillet_r_ = 0.0; // the treatment set last wins
        s_chamfer_d1_ = chamfer_d1_;
        s_chamfer_d2_ = chamfer_d2_;
        s_fillet_r_ = 0.0;
        state_ = State::First;
        ctx.set_prompt("Specify first corner point or [Chamfer/Fillet]: ");
        return;
    }
    case State::FilletR: {
        double v = fillet_r_;
        if (!up.empty() && (!parse_number(text, v) || v < 0.0)) {
            ctx.echo("Enter a radius of zero or more.");
            return;
        }
        fillet_r_ = v;
        chamfer_d1_ = chamfer_d2_ = 0.0;
        s_fillet_r_ = fillet_r_;
        s_chamfer_d1_ = s_chamfer_d2_ = 0.0;
        state_ = State::First;
        ctx.set_prompt("Specify first corner point or [Chamfer/Fillet]: ");
        return;
    }

    case State::AwaitCorner: {
        if (up == "D" || up == "DIMENSIONS") {
            state_ = State::DimLen;
            refresh_preview();
            ctx.set_prompt("Specify length for rectangles: ");
            return;
        }
        if (up == "A" || up == "AREA") {
            state_ = State::AreaVal;
            refresh_preview();
            ctx.set_prompt("Enter area of rectangle in current units: ");
            return;
        }
        if (up == "R" || up == "ROTATION") {
            state_ = State::RotVal;
            refresh_preview();
            ctx.set_prompt("Specify rotation angle: ");
            return;
        }
        const auto p = read_point(ctx, text);
        if (!p) {
            return; // read_point echoed the error; stay put
        }
        core::Vec2 other = *p;
        if (has_dims_) {
            // Fixed size; the pick's quadrant relative to first_ flips the direction.
            const double sx = (p->x >= first_.x) ? 1.0 : -1.0;
            const double sy = (p->y >= first_.y) ? 1.0 : -1.0;
            other = {first_.x + sx * length_, first_.y + sy * width_};
        }
        commit(other);
        return;
    }

    case State::DimLen: {
        double v = 0.0;
        if (!parse_number(text, v) || v <= 0.0) {
            revert_to_corner();
            return;
        }
        length_ = v;
        state_ = State::DimWid;
        refresh_preview();
        ctx.set_prompt("Specify width for rectangles: ");
        return;
    }
    case State::DimWid: {
        double v = 0.0;
        if (!parse_number(text, v) || v <= 0.0) {
            revert_to_corner();
            return;
        }
        width_ = v;
        has_dims_ = true;
        revert_to_corner(); // back to the corner pick, now with a fixed-size preview
        return;
    }

    case State::AreaVal: {
        double v = 0.0;
        if (!parse_number(text, v) || v <= 0.0) {
            revert_to_corner();
            return;
        }
        area_ = v;
        state_ = State::AreaSide;
        refresh_preview();
        ctx.set_prompt("Calculate rectangle dimensions based on [Length/Width] <Length>: ");
        return;
    }
    case State::AreaSide:
        area_by_length_ = !(up == "W" || up == "WIDTH"); // default + L/Length -> length
        state_ = State::AreaSideVal;
        refresh_preview();
        ctx.set_prompt(area_by_length_ ? "Enter rectangle length: " : "Enter rectangle width: ");
        return;
    case State::AreaSideVal: {
        double v = 0.0;
        if (!parse_number(text, v) || v <= 0.0) {
            revert_to_corner();
            return;
        }
        if (area_by_length_) {
            length_ = v;
            width_ = area_ / v; // other side computed from the area
        } else {
            width_ = v;
            length_ = area_ / v;
        }
        has_dims_ = true;
        revert_to_corner();
        return;
    }

    case State::RotVal: {
        double deg = 0.0;
        if (parse_number(text, deg)) {
            rotation_ = deg * (3.14159265358979323846 / 180.0);
        }
        revert_to_corner(); // a non-number simply leaves rotation unchanged
        return;
    }
    }
}

void RectangleCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// ERASE
// ---------------------------------------------------------------------------
void EraseCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Select objects [Last/All]: ");
}

void EraseCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    if (u.empty()) {
        done_ = true;
        return;
    }
    if (u == "L" || u == "LAST") {
        ctx.submit(core::EraseCommand{core::EraseScope::Last, ctx.group_id()});
        ctx.echo("Erased last object.");
        done_ = true;
    } else if (u == "ALL" || u == "A") {
        ctx.submit(core::EraseCommand{core::EraseScope::All, ctx.group_id()});
        ctx.echo("Erased all objects.");
        done_ = true;
    } else {
        ctx.echo("Enter L (last) or ALL.");
    }
}

void EraseCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// U (undo)
// ---------------------------------------------------------------------------
void UndoCommand::start(CommandContext& ctx) {
    ctx.submit(core::UndoLastGroupCommand{});
    ctx.echo("Undo");
    done_ = true;
}

void UndoCommand::input(CommandContext&, const std::string&) {}

void UndoCommand::cancel(CommandContext&) { done_ = true; }

// ---------------------------------------------------------------------------
// ZOOM
// ---------------------------------------------------------------------------
void ZoomCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Specify scale factor or [All/Extents]: ");
}

void ZoomCommand::input(CommandContext& ctx, const std::string& text) {
    std::string u = upper(trimmed(text));
    if (u.empty()) {
        done_ = true;
        return;
    }
    if (u == "E" || u == "EXTENTS" || u == "A" || u == "ALL") {
        if (ViewControl* v = ctx.view()) {
            v->zoom_extents();
        }
        ctx.echo("Zoom extents.");
        done_ = true;
        return;
    }
    if (!u.empty() && (u.back() == 'X')) {
        u.pop_back(); // accept "2X" style
    }
    double factor = 0.0;
    if (parse_number(u, factor) && factor > 0.0) {
        if (ViewControl* v = ctx.view()) {
            v->zoom_scale(factor);
        }
        ctx.echo("Zoom " + std::to_string(factor) + "x.");
        done_ = true;
    } else {
        ctx.echo("Enter a positive scale factor, or A/E for extents.");
    }
}

void ZoomCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// MOVE
// ---------------------------------------------------------------------------
void MoveCommand::start(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        ctx.echo("No selection. Select objects first, then run MOVE.");
        done_ = true;
        return;
    }
    ctx.clear_last_point();
    ctx.set_prompt("Specify base point: ");
}

void MoveCommand::input(CommandContext& ctx, const std::string& text) {
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    if (!base_) {
        base_ = *p;
        ctx.set_last_point(*p);
        ctx.set_preview({PreviewKind::Move, {*p}});
        ctx.set_prompt("Specify second point: ");
        return;
    }
    ctx.submit(core::MoveSelectionCommand{*p - *base_, ctx.group_id()});
    ctx.echo("Moved.");
    done_ = true;
}

void MoveCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// COPY (leaves originals; repeats until Enter/Esc)
// ---------------------------------------------------------------------------
void CopyCommand::start(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        ctx.echo("No selection. Select objects first, then run COPY.");
        done_ = true;
        return;
    }
    ctx.clear_last_point();
    ctx.set_prompt("Specify base point: ");
}

void CopyCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (!base_) {
        if (const auto p = read_point(ctx, text)) {
            base_ = *p;
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Move, {*p}});
            ctx.set_prompt("Specify second point or [Exit]: ");
        }
        return;
    }
    if (t.empty()) {
        done_ = true; // Enter ends COPY
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        ctx.submit(core::CopySelectionCommand{*p - *base_, ctx.group_id()});
        ctx.echo("Copy placed.");
    }
}

void CopyCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// MIRROR
// ---------------------------------------------------------------------------
void MirrorCommand::start(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        ctx.echo("No selection. Select objects first, then run MIRROR.");
        done_ = true;
        return;
    }
    ctx.clear_last_point();
    ctx.set_prompt("Specify first point of mirror line: ");
}

void MirrorCommand::input(CommandContext& ctx, const std::string& text) {
    if (state_ == State::Ask) {
        const std::string u = upper(trimmed(text));
        const bool erase = (u == "Y" || u == "YES");
        ctx.submit(core::MirrorSelectionCommand{p1_, p2_, erase, ctx.group_id()});
        ctx.echo(erase ? "Mirrored (source erased)." : "Mirrored.");
        done_ = true;
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    if (state_ == State::First) {
        p1_ = *p;
        ctx.set_last_point(*p);
        ctx.set_preview({PreviewKind::Mirror, {*p}});
        state_ = State::Second;
        ctx.set_prompt("Specify second point of mirror line: ");
    } else {
        p2_ = *p;
        ctx.clear_preview();
        state_ = State::Ask;
        ctx.set_prompt("Erase source objects? [Yes/No] <No>: ");
    }
}

void MirrorCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// OFFSET (distance -> pick object -> pick side; repeats)
// ---------------------------------------------------------------------------
void OffsetCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Specify offset distance: ");
}

void OffsetCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (state_ == State::Distance) {
        double d = 0.0;
        if (!parse_number(t, d) || d <= 0.0) {
            ctx.echo("Enter a positive offset distance.");
            return;
        }
        distance_ = d;
        state_ = State::Object;
        ctx.set_prompt("Select object to offset: ");
        return;
    }
    if (t.empty()) {
        done_ = true;
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    if (state_ == State::Object) {
        object_pick_ = *p;
        state_ = State::Side;
        ctx.set_prompt("Specify point on side to offset: ");
    } else {
        ctx.submit(core::OffsetPickCommand{object_pick_, ctx.pick_radius(), distance_, *p,
                                           ctx.group_id()});
        // Result is echoed by the engine (honest status), not assumed here.
        state_ = State::Object;
        ctx.set_prompt("Select object to offset: ");
    }
}

void OffsetCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// JOIN (pick a source, then targets that share endpoints -> one polyline)
// ---------------------------------------------------------------------------
void JoinCommand::start(CommandContext& ctx) {
    // Noun-verb (the usual workflow): if objects are already selected, join all of them
    // that share endpoints -- each connected chain becomes one polyline -- in one step.
    if (ctx.has_selection()) {
        ctx.submit(core::JoinSelectionCommand{ctx.pick_radius(), ctx.group_id()});
        done_ = true;
        return;
    }
    ctx.set_prompt("Select source object: ");
}

void JoinCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (state_ == State::Source) {
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        picks_.push_back(*p);
        state_ = State::Targets;
        ctx.set_prompt("Select objects to join to source: ");
        return;
    }
    // Targets: pick more objects; Enter commits the join (the engine resolves entities,
    // walks the connected chain, and reports how many joined / were skipped).
    if (t.empty()) {
        if (picks_.size() >= 2) {
            ctx.submit(core::JoinPickCommand{picks_, ctx.pick_radius(), ctx.group_id()});
        } else {
            ctx.echo("JOIN: select at least one object to join to the source.");
        }
        done_ = true;
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        picks_.push_back(*p);
    }
}

void JoinCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// HATCH / H (Part A: SOLID fill from the selected closed polyline boundaries)
// ---------------------------------------------------------------------------
void HatchCommand::start(CommandContext& ctx) {
    // Noun-verb: with closed boundaries already selected, fill them immediately ("Select
    // objects" mode). Otherwise the default is AutoCAD's "Pick internal point".
    if (ctx.has_selection()) {
        ctx.submit(core::HatchFromSelectionCommand{pattern_, scale_, angle_, ctx.group_id(), color2_});
        done_ = true;
        return;
    }
    ctx.set_prompt("Pick internal point or [Pattern/Scale/Angle/Gradient]: ");
}

void HatchCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    // Sub-prompts: a typed value for the pattern name / scale / angle, then back to picking.
    if (mode_ == Mode::Pattern) {
        if (!t.empty()) {
            const std::string up = upper(t);
            if (up == "SOLID" || core::hatch::builtin_pattern(up) != nullptr) {
                pattern_ = up;
                ctx.echo("Pattern: " + pattern_);
            } else {
                ctx.echo("Unknown pattern '" + t + "'. Keeping " + pattern_ + ".");
            }
        }
        mode_ = Mode::PickPoint;
        ctx.set_prompt("Pick internal point or [Pattern/Scale/Angle/Gradient]: ");
        return;
    }
    if (mode_ == Mode::Scale) {
        try {
            const double v = std::stod(t);
            if (v > 1e-9) {
                scale_ = v;
            }
        } catch (...) {
        }
        mode_ = Mode::PickPoint;
        ctx.set_prompt("Pick internal point or [Pattern/Scale/Angle/Gradient]: ");
        return;
    }
    if (mode_ == Mode::Angle) {
        try {
            angle_ = std::stod(t) * 3.14159265358979323846 / 180.0; // degrees -> radians
        } catch (...) {
        }
        mode_ = Mode::PickPoint;
        ctx.set_prompt("Pick internal point or [Pattern/Scale/Angle/Gradient]: ");
        return;
    }
    if (mode_ == Mode::GradientColor) {
        // The second colour as r,g,b; Enter keeps the current one. The first colour is
        // the entity colour (ByLayer resolves through the layer), as AutoCAD's
        // one-colour gradient works.
        if (!t.empty()) {
            int r = 0;
            int g = 0;
            int b = 0;
            if (std::sscanf(t.c_str(), "%d,%d,%d", &r, &g, &b) != 3 || r < 0 || g < 0 || b < 0 ||
                r > 255 || g > 255 || b > 255) {
                ctx.echo("Enter the colour as r,g,b (0-255 each).");
                return;
            }
            color2_ = core::Rgb{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                                static_cast<std::uint8_t>(b)};
        }
        mode_ = Mode::GradientAngle;
        ctx.set_prompt("Specify gradient angle <" +
                       std::to_string(std::lround(core::to_degrees(angle_))) + ">: ");
        return;
    }
    if (mode_ == Mode::GradientAngle) {
        if (!t.empty()) {
            try {
                angle_ = core::to_radians(std::stod(t));
            } catch (...) {
                ctx.echo("Enter an angle in degrees.");
                return;
            }
        }
        pattern_ = "GRADIENT";
        ctx.echo("Pattern: GRADIENT");
        mode_ = Mode::PickPoint;
        ctx.set_prompt("Pick internal point or [Pattern/Scale/Angle/Gradient]: ");
        return;
    }

    if (t.empty()) {
        done_ = true; // Enter finishes the command
        return;
    }
    const std::string up = upper(t);
    if (up == "P" || up == "PATTERN") {
        mode_ = Mode::Pattern;
        ctx.set_prompt("Pattern name (SOLID, ANSI31, ...) <" + pattern_ + ">: ");
        return;
    }
    if (up == "G" || up == "GRADIENT") {
        mode_ = Mode::GradientColor;
        ctx.set_prompt("Specify second colour as r,g,b <" + std::to_string(color2_.r) + "," +
                       std::to_string(color2_.g) + "," + std::to_string(color2_.b) + ">: ");
        return;
    }
    if (up == "S" || up == "SCALE") {
        mode_ = Mode::Scale;
        ctx.set_prompt("Pattern scale: ");
        return;
    }
    if (up == "A" || up == "ANGLE") {
        mode_ = Mode::Angle;
        ctx.set_prompt("Pattern angle (degrees): ");
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        // Click inside a closed region -> trace its boundary (+ islands) and hatch it. Each
        // pick is its own undo group; the command stays active for more picks.
        ctx.submit(core::HatchPickPointCommand{*p, pattern_, scale_, angle_, ctx.new_group(), color2_});
        ctx.set_prompt("Pick internal point or [Pattern/Scale/Angle/Gradient] or Enter to finish: ");
    }
}

void HatchCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// MATCHPROP / MA (source -> N targets; paintbrush cursor; per-target undo)
// ---------------------------------------------------------------------------
void MatchPropCommand::start(CommandContext& ctx) {
    // Noun-verb: if objects are already selected, the first becomes the source and we go
    // straight to picking destinations (same convenience as JOIN).
    if (ctx.has_selection()) {
        ctx.submit(core::MatchPropSourceFromSelectionCommand{});
        state_ = State::Targets;
        if (ctx.view() != nullptr) {
            ctx.view()->set_match_cursor(true);
        }
        ctx.set_prompt("Select destination object(s) or [Settings]: ");
        return;
    }
    ctx.set_prompt("Select source object: ");
}

void MatchPropCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (state_ == State::Source) {
        const auto p = read_point(ctx, text);
        if (!p) {
            return; // read_point already echoed the error / awaits a real pick
        }
        // Capture the source on the geometry thread (the UI never reads the store).
        ctx.submit(core::MatchPropPickSourceCommand{*p, ctx.pick_radius()});
        state_ = State::Targets;
        if (ctx.view() != nullptr) {
            ctx.view()->set_match_cursor(true); // paintbrush while matching
        }
        ctx.set_prompt("Select destination object(s) or [Settings]: ");
        return;
    }
    // Targets: Enter finishes; "S"/"Settings" opens the category dialog; else apply.
    if (t.empty()) {
        if (ctx.view() != nullptr) {
            ctx.view()->set_match_cursor(false);
        }
        done_ = true;
        return;
    }
    if (upper(t) == "S" || upper(t) == "SETTINGS") {
        if (ctx.view() != nullptr) {
            ctx.view()->match_settings_dialog(); // modal; persists the filter
        }
        ctx.set_prompt("Select destination object(s) or [Settings]: ");
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        const core::MatchPropFilter filter =
            ctx.view() != nullptr ? ctx.view()->match_filter() : core::MatchPropFilter{};
        // Each matched target is its OWN undo group, so Ctrl+Z undoes them in reverse.
        ctx.submit(core::MatchPropApplyCommand{*p, ctx.pick_radius(), filter, ctx.new_group()});
    }
}

void MatchPropCommand::cancel(CommandContext& ctx) {
    if (ctx.view() != nullptr) {
        ctx.view()->set_match_cursor(false);
    }
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// TRIM (line subset; repeats)
// ---------------------------------------------------------------------------
void TrimCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Select line to trim: ");
}

void TrimCommand::input(CommandContext& ctx, const std::string& text) {
    if (trimmed(text).empty()) {
        done_ = true;
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    ctx.submit(core::TrimPickCommand{*p, ctx.pick_radius(), ctx.group_id()});
    // Result is echoed by the engine (honest status), not assumed here.
}

void TrimCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// ROTATE
// ---------------------------------------------------------------------------
void RotateCommand::start(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        ctx.echo("No selection. Select objects first, then run ROTATE.");
        done_ = true;
        return;
    }
    ctx.clear_last_point();
    ctx.set_prompt("Specify base point: ");
}

void RotateCommand::input(CommandContext& ctx, const std::string& text) {
    if (!base_) {
        if (const auto p = read_point(ctx, text)) {
            base_ = *p;
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Rotate, {*p}});
            ctx.set_prompt("Specify rotation angle or [Copy/Reference] <0>: ");
        }
        return;
    }
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    if (u == "C" || u == "COPY") {
        copy_ = true;
        ctx.echo("Rotating a copy of the selected objects.");
        ctx.set_prompt("Specify rotation angle or [Copy/Reference] <0>: ");
        return;
    }
    if (u == "R" || u == "REFERENCE") {
        reference_ = true;
        have_ref_ = false;
        ctx.set_prompt("Specify the reference angle <0>: ");
        return;
    }
    double deg = 0.0;
    double angle = 0.0;
    if (t.empty() && !reference_) {
        angle = 0.0;
    } else if (parse_number(t, deg)) {
        angle = core::to_radians(deg); // typed number = degrees
    } else if (const auto p = read_point(ctx, text)) {
        angle = std::atan2(p->y - base_->y, p->x - base_->x); // picked = angle to point
    } else if (t.empty() && reference_ && !have_ref_) {
        angle = 0.0; // the default reference angle
    } else {
        return;
    }
    if (reference_ && !have_ref_) {
        ref_angle_ = angle;
        have_ref_ = true;
        ctx.set_prompt("Specify the new angle: ");
        return;
    }
    if (reference_) {
        angle -= ref_angle_; // rotate so the reference direction lands on the new one
    }
    ctx.submit(core::RotateSelectionCommand{*base_, angle, ctx.group_id(), copy_});
    ctx.echo(copy_ ? "Rotated a copy." : "Rotated.");
    done_ = true;
}

void RotateCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// SCALE
// ---------------------------------------------------------------------------
void ScaleCommand::start(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        ctx.echo("No selection. Select objects first, then run SCALE.");
        done_ = true;
        return;
    }
    ctx.clear_last_point();
    ctx.set_prompt("Specify base point: ");
}

void ScaleCommand::input(CommandContext& ctx, const std::string& text) {
    if (!base_) {
        if (const auto p = read_point(ctx, text)) {
            base_ = *p;
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Scale, {*p}});
            ctx.set_prompt("Specify scale factor: ");
        }
        return;
    }
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    if (u == "C" || u == "COPY") {
        copy_ = true;
        ctx.echo("Scaling a copy of the selected objects.");
        ctx.set_prompt("Specify scale factor or [Copy/Reference]: ");
        return;
    }
    if (u == "R" || u == "REFERENCE") {
        reference_ = true;
        have_ref_ = false;
        ctx.set_prompt("Specify reference length <1>: ");
        return;
    }
    double factor = 0.0;
    if (parse_number(t, factor)) {
        // typed factor (or a length, in Reference mode)
    } else if (const auto p = read_point(ctx, text)) {
        factor = core::distance(*base_, *p); // picked = distance (reference length 1)
    } else if (t.empty() && reference_ && !have_ref_) {
        factor = 1.0; // the default reference length
    } else {
        return;
    }
    if (!(factor > 0.0)) {
        ctx.echo("Value must be positive.");
        return;
    }
    if (reference_ && !have_ref_) {
        ref_len_ = factor;
        have_ref_ = true;
        ctx.set_prompt("Specify new length: ");
        return;
    }
    if (reference_) {
        factor = factor / ref_len_; // the reference length becomes the new length
    }
    ctx.submit(core::ScaleSelectionCommand{*base_, factor, ctx.group_id(), copy_});
    ctx.echo(copy_ ? "Scaled a copy." : "Scaled.");
    done_ = true;
}

void ScaleCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// ARRAY (command-line driven: rectangular or polar)
// ---------------------------------------------------------------------------
namespace {
int parse_int(const std::string& t, int fallback) {
    double d = 0.0;
    return parse_number(t, d) ? static_cast<int>(std::lround(d)) : fallback;
}
} // namespace

// ---------------------------------------------------------------------------
// PURGE: drop unused symbol-table entries
// ---------------------------------------------------------------------------
void PurgeCommand::start(CommandContext& ctx) {
    ctx.set_prompt(
        "Enter type of unused objects to purge [Blocks/Dimstyles/Groups/LAyers/Tablestyles/Images/textSTyles/All] <All>: ");
}

void PurgeCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    std::uint8_t what = 0;
    if (u.empty() || u == "A" || u == "ALL") {
        what = 0;
    } else if (u == "B" || u == "BLOCKS") {
        what = 1;
    } else if (u == "D" || u == "DIMSTYLES") {
        what = 2;
    } else if (u == "G" || u == "GROUPS") {
        what = 3;
    } else if (u == "LA" || u == "LAYERS") {
        what = 4;
    } else if (u == "T" || u == "TABLESTYLES") {
        what = 5;
    } else if (u == "I" || u == "IMAGES") {
        what = 6;
    } else if (u == "ST" || u == "TEXTSTYLES") {
        what = 7;
    } else {
        ctx.echo("Enter Blocks, Dimstyles, Groups, LAyers, Tablestyles, Images, textSTyles or All.");
        return;
    }
    ctx.submit(core::PurgeCommand{ctx.group_id(), what}); // the engine reports what went
    done_ = true;
}

void PurgeCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// ALIGN: two source/destination pairs, optional uniform scale
// ---------------------------------------------------------------------------
void AlignCommand::start(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        ctx.echo("No selection. Select objects first, then run ALIGN.");
        done_ = true;
        return;
    }
    ctx.clear_last_point();
    state_ = State::Src1;
    ctx.set_prompt("Specify first source point: ");
}

void AlignCommand::input(CommandContext& ctx, const std::string& text) {
    switch (state_) {
    case State::Src1:
        if (const auto p = read_point(ctx, text)) {
            src1_ = *p;
            state_ = State::Dst1;
            ctx.set_prompt("Specify first destination point: ");
        }
        return;
    case State::Dst1:
        if (const auto p = read_point(ctx, text)) {
            dst1_ = *p;
            state_ = State::Src2;
            ctx.set_prompt("Specify second source point: ");
        }
        return;
    case State::Src2:
        if (const auto p = read_point(ctx, text)) {
            src2_ = *p;
            state_ = State::Dst2;
            ctx.set_prompt("Specify second destination point: ");
        }
        return;
    case State::Dst2:
        if (const auto p = read_point(ctx, text)) {
            dst2_ = *p;
            state_ = State::Scale;
            ctx.set_prompt("Scale objects based on alignment points? [Yes/No] <N>: ");
        }
        return;
    case State::Scale: {
        const std::string u = upper(trimmed(text));
        core::AlignSelectionCommand cmd;
        cmd.src1 = src1_;
        cmd.dst1 = dst1_;
        cmd.src2 = src2_;
        cmd.dst2 = dst2_;
        cmd.scale = (u == "Y" || u == "YES");
        cmd.group = ctx.group_id();
        ctx.submit(cmd);
        done_ = true;
        return;
    }
    }
}

void AlignCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// LENGTHEN: mode, amount, then the end to move
// ---------------------------------------------------------------------------
void LengthenCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Mode;
    ctx.set_prompt("Enter an option [DElta/Percent/Total] <Total>: ");
}

void LengthenCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    switch (state_) {
    case State::Mode: {
        const std::string u = upper(t);
        if (u == "DE" || u == "DELTA") {
            mode_ = core::LengthenCommand::Mode::Delta;
            ctx.set_prompt("Enter delta length: ");
        } else if (u == "P" || u == "PERCENT") {
            mode_ = core::LengthenCommand::Mode::Percent;
            ctx.set_prompt("Enter percentage length: ");
        } else {
            mode_ = core::LengthenCommand::Mode::Total;
            ctx.set_prompt("Specify total length: ");
        }
        state_ = State::Amount;
        return;
    }
    case State::Amount: {
        if (!parse_number(t, value_)) {
            ctx.echo("Enter a number.");
            return;
        }
        if (mode_ != core::LengthenCommand::Mode::Delta && value_ <= 0.0) {
            ctx.echo("Enter a value greater than zero.");
            return;
        }
        state_ = State::Pick;
        // The pick does double duty: it chooses the object AND, by which end it is
        // nearer, which end moves. That is AutoCAD's behaviour and worth saying.
        ctx.set_prompt("Select an object to change (pick near the end to move): ");
        return;
    }
    case State::Pick:
        if (const auto p = read_point(ctx, text)) {
            core::LengthenCommand cmd;
            cmd.pick = *p;
            cmd.pick_radius = ctx.pick_radius();
            cmd.mode = mode_;
            cmd.value = value_;
            cmd.group = ctx.group_id();
            ctx.submit(cmd);
            done_ = true;
        }
        return;
    }
}

void LengthenCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// BREAK / BREAKATPOINT: cut a piece out of a curve, or just split it
// ---------------------------------------------------------------------------
void BreakCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Select;
    ctx.set_prompt("Select object: ");
}

void BreakCommand::input(CommandContext& ctx, const std::string& text) {
    const auto fire = [&](core::Vec2 a, core::Vec2 b) {
        core::BreakCommand cmd;
        cmd.pick = pick_;
        cmd.pick_radius = ctx.pick_radius();
        cmd.p1 = a;
        cmd.p2 = b;
        cmd.group = ctx.group_id();
        ctx.submit(cmd);
        // The engine reports what it actually did (Ph10.1).
        done_ = true;
    };
    switch (state_) {
    case State::Select:
        if (const auto p = read_point(ctx, text)) {
            // AutoCAD: the selecting click doubles as the first break point.
            pick_ = *p;
            p1_ = *p;
            if (at_point_) {
                fire(p1_, p1_); // BREAKATPOINT needs nothing more
                return;
            }
            state_ = State::Second;
            ctx.set_prompt("Specify second break point or [First point]: ");
        }
        return;
    case State::Second: {
        if (upper(trimmed(text)) == "F" || upper(trimmed(text)) == "FIRST") {
            state_ = State::FirstAgain;
            ctx.set_prompt("Specify first break point: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            fire(p1_, *p);
        }
        return;
    }
    case State::FirstAgain:
        if (const auto p = read_point(ctx, text)) {
            p1_ = *p;
            state_ = State::Second;
            ctx.set_prompt("Specify second break point: ");
        }
        return;
    }
}

void BreakCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// ELLIPSE
// ---------------------------------------------------------------------------
core::EllipseData EllipseCommand::shape() const {
    core::EllipseData e;
    e.center = center_;
    e.major = major_;
    e.ratio = ratio_;
    e.start = start_;
    e.end = end_;
    return e;
}

void EllipseCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Start;
    arc_ = false;
    ctx.set_prompt("Specify axis endpoint of ellipse or [Arc/Center]: ");
}

void EllipseCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.set_preview({});
    done_ = true;
}

// The first axis is known (centre, unit direction, half-length); the other half-axis
// is `other_half`. AutoCAD stores the LONGER axis as the major one, so if the second
// axis is longer the two swap and the ratio is inverted -- arc angles are still taken
// from the first axis the user gave (see param_from_input).
void EllipseCommand::define_axes(CommandContext& ctx, double other_half) {
    if (other_half <= 1e-9 || half_ <= 1e-9) {
        ctx.echo("Invalid axis length.");
        return;
    }
    if (other_half <= half_) {
        major_ = first_dir_ * half_;
        ratio_ = other_half / half_;
        swapped_ = false;
    } else {
        major_ = core::Vec2{-first_dir_.y, first_dir_.x} * other_half;
        ratio_ = half_ / other_half;
        swapped_ = true;
    }
    after_axes(ctx);
}

void EllipseCommand::after_axes(CommandContext& ctx) {
    if (!arc_) {
        start_ = 0.0;
        end_ = core::kTwoPi;
        commit(ctx);
        return;
    }
    state_ = State::ArcStart;
    start_param_mode_ = false;
    PreviewSpec pv{PreviewKind::Ellipse, {center_}};
    pv.major = major_;
    pv.ratio = ratio_;
    pv.ellipse_stage = 1;
    ctx.set_preview(std::move(pv));
    ctx.set_prompt("Specify start angle or [Parameter]: ");
}

// An arc angle/parameter from typed input: a number (degrees, from the first axis --
// or a parameter in Parameter mode) or a point (the parameter on the centre->point ray).
double EllipseCommand::param_from_input(const std::string& text, bool parameter_mode, bool* ok,
                                        CommandContext& ctx) const {
    *ok = true;
    double v = 0.0;
    if (parse_number(text, v)) {
        const double a = core::to_radians(v);
        if (parameter_mode) {
            return a;
        }
        // Angles are measured from the FIRST axis; if the axes swapped, the major axis
        // sits 90 degrees from it.
        const double rel = swapped_ ? a - core::kHalfPi : a;
        return core::ellipse::angle_to_param(rel, ratio_);
    }
    if (const auto p = read_point(ctx, text)) {
        return core::ellipse::param_of(shape(), *p);
    }
    *ok = false;
    return 0.0;
}

void EllipseCommand::commit(CommandContext& ctx) {
    ctx.submit(core::AddEllipseCommand{center_, major_, ratio_, start_, end_, ctx.group_id(), {}});
    ctx.set_preview({});
    done_ = true;
}

void EllipseCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Start:
        if (u == "A" || u == "ARC") {
            arc_ = true;
            ctx.set_prompt("Specify axis endpoint of elliptical arc or [Center]: ");
            return;
        }
        if (u == "C" || u == "CENTER") {
            state_ = State::Center;
            ctx.set_prompt(arc_ ? "Specify center of elliptical arc: "
                                : "Specify center of ellipse: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            axis_a_ = *p;
            ctx.set_last_point(*p);
            state_ = State::OtherEnd;
            ctx.set_preview({PreviewKind::Segment, {*p}});
            ctx.set_prompt("Specify other endpoint of axis: ");
        }
        return;
    case State::Center:
        if (const auto p = read_point(ctx, text)) {
            center_ = *p;
            ctx.set_last_point(*p);
            state_ = State::AxisEnd;
            ctx.set_preview({PreviewKind::Segment, {*p}});
            ctx.set_prompt("Specify endpoint of axis: ");
        }
        return;
    case State::AxisEnd:
        if (const auto p = read_point(ctx, text)) {
            const core::Vec2 v = *p - center_;
            half_ = core::length(v);
            if (half_ <= 1e-9) {
                ctx.echo("Invalid axis length.");
                return;
            }
            first_dir_ = v * (1.0 / half_);
            state_ = State::OtherDist;
            PreviewSpec pv{PreviewKind::Ellipse, {center_}};
            pv.major = first_dir_ * half_;
            pv.ellipse_stage = 0;
            ctx.set_preview(std::move(pv));
            ctx.set_prompt("Specify distance to other axis or [Rotation]: ");
        }
        return;
    case State::OtherEnd:
        if (const auto p = read_point(ctx, text)) {
            const core::Vec2 v = *p - axis_a_;
            const double len = core::length(v);
            if (len <= 1e-9) {
                ctx.echo("Invalid axis length.");
                return;
            }
            center_ = (axis_a_ + *p) * 0.5;
            half_ = len * 0.5;
            first_dir_ = v * (1.0 / len);
            ctx.set_last_point(*p);
            state_ = State::OtherDist;
            PreviewSpec pv{PreviewKind::Ellipse, {center_}};
            pv.major = first_dir_ * half_;
            pv.ellipse_stage = 0;
            ctx.set_preview(std::move(pv));
            ctx.set_prompt("Specify distance to other axis or [Rotation]: ");
        }
        return;
    case State::OtherDist: {
        if (u == "R" || u == "ROTATION") {
            state_ = State::Rotation;
            ctx.set_prompt("Specify rotation around major axis: ");
            return;
        }
        double d = 0.0;
        if (parse_number(t, d)) {
            define_axes(ctx, d);
        } else if (const auto p = read_point(ctx, text)) {
            // AutoCAD: the distance from the midpoint of the first axis to the point.
            define_axes(ctx, core::distance(center_, *p));
        }
        return;
    }
    case State::Rotation: {
        // Rotation about the major axis: the ellipse is the first-axis circle seen at
        // that angle, so ratio = cos(angle). AutoCAD accepts 0 <= angle < 89.4 degrees.
        double deg = 0.0;
        if (!parse_number(t, deg)) {
            if (const auto p = read_point(ctx, text)) {
                const core::Vec2 v = *p - center_;
                deg = core::to_degrees(std::atan2(v.y, v.x) -
                                       std::atan2(first_dir_.y, first_dir_.x));
            } else {
                return;
            }
        }
        deg = std::abs(std::fmod(deg, 180.0));
        if (deg > 90.0) {
            deg = 180.0 - deg;
        }
        if (deg >= 89.4) {
            ctx.echo("Rotation must be less than 89.4 degrees.");
            return;
        }
        major_ = first_dir_ * half_;
        ratio_ = std::max(std::cos(core::to_radians(deg)), 1e-6);
        swapped_ = false;
        after_axes(ctx);
        return;
    }
    case State::ArcStart: {
        if (u == "P" || u == "PARAMETER") {
            start_param_mode_ = !start_param_mode_;
            ctx.set_prompt(start_param_mode_ ? "Specify start parameter or [Angle]: "
                                             : "Specify start angle or [Parameter]: ");
            return;
        }
        if (u == "A" || u == "ANGLE") {
            start_param_mode_ = false;
            ctx.set_prompt("Specify start angle or [Parameter]: ");
            return;
        }
        bool ok = false;
        const double v = param_from_input(text, start_param_mode_, &ok, ctx);
        if (!ok) {
            return;
        }
        start_ = v;
        state_ = State::ArcEnd;
        end_param_mode_ = start_param_mode_;
        PreviewSpec pv{PreviewKind::Ellipse, {center_}};
        pv.major = major_;
        pv.ratio = ratio_;
        pv.ellipse_stage = 2;
        pv.ellipse_start = start_;
        ctx.set_preview(std::move(pv));
        ctx.set_prompt(end_param_mode_ ? "Specify end parameter or [Angle/Included angle]: "
                                       : "Specify end angle or [Parameter/Included angle]: ");
        return;
    }
    case State::ArcEnd: {
        if (u == "P" || u == "PARAMETER") {
            end_param_mode_ = true;
            ctx.set_prompt("Specify end parameter or [Angle/Included angle]: ");
            return;
        }
        if (u == "A" || u == "ANGLE") {
            end_param_mode_ = false;
            ctx.set_prompt("Specify end angle or [Parameter/Included angle]: ");
            return;
        }
        if (u == "I" || u == "INCLUDED") {
            state_ = State::ArcIncluded;
            ctx.set_prompt("Specify included angle for arc <180>: ");
            return;
        }
        bool ok = false;
        const double v = param_from_input(text, end_param_mode_, &ok, ctx);
        if (!ok) {
            return;
        }
        end_ = v;
        commit(ctx);
        return;
    }
    case State::ArcIncluded: {
        double deg = 180.0;
        if (!t.empty() && !parse_number(t, deg)) {
            ctx.echo("Enter an included angle in degrees.");
            return;
        }
        if (std::abs(deg) < 1e-9) {
            ctx.echo("Included angle must be non-zero.");
            return;
        }
        // Included ANGLE (from the first axis) -> the end angle -> its parameter.
        const double start_angle_rel = std::atan2(ratio_ * std::sin(start_), std::cos(start_));
        const double end_rel = start_angle_rel + core::to_radians(deg);
        end_ = core::ellipse::angle_to_param(end_rel, ratio_);
        commit(ctx);
        return;
    }
    }
}

// ---------------------------------------------------------------------------
// SPLINE
// ---------------------------------------------------------------------------
namespace {
const char* knots_name(int k) {
    return k == 1 ? "Square root" : (k == 2 ? "Uniform" : "Chord");
}
} // namespace

void SplineCommand::prompt_first(CommandContext& ctx) const {
    ctx.set_prompt(fit_ ? "Specify first point or [Method/Knots/Object]: "
                        : "Specify first point or [Method/Degree/Object]: ");
}

void SplineCommand::prompt_next(CommandContext& ctx) const {
    const std::size_t n = pts_.size();
    if (fit_) {
        if (n < 2) {
            ctx.set_prompt("Enter next point or [start Tangency/toLerance]: ");
        } else if (n < 3) {
            ctx.set_prompt("Enter next point or [end Tangency/toLerance/Undo]: ");
        } else {
            ctx.set_prompt("Enter next point or [end Tangency/toLerance/Undo/Close]: ");
        }
    } else {
        if (n < 2) {
            ctx.set_prompt("Enter next point: ");
        } else if (n < 3) {
            ctx.set_prompt("Enter next point or [Undo]: ");
        } else {
            ctx.set_prompt("Enter next point or [Close/Undo]: ");
        }
    }
}

void SplineCommand::refresh_preview(CommandContext& ctx) const {
    PreviewSpec pv{PreviewKind::Spline, pts_};
    pv.spline_fit = fit_;
    pv.spline_degree = degree_;
    pv.spline_knots = knots_;
    ctx.set_preview(std::move(pv));
}

void SplineCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    fit_ = s_fit_;
    degree_ = s_degree_;
    knots_ = s_knots_;
    pts_.clear();
    state_ = State::First;
    if (fit_) {
        ctx.echo(std::string("Current settings: Method=Fit   Knots=") + knots_name(knots_));
    } else {
        ctx.echo("Current settings: Method=CV   Degree=" + std::to_string(degree_));
    }
    prompt_first(ctx);
}

void SplineCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.set_preview({});
    done_ = true;
}

void SplineCommand::finish(CommandContext& ctx, bool close) {
    if (pts_.size() < 2) {
        ctx.echo("A spline needs at least two points.");
        ctx.set_preview({});
        done_ = true;
        return;
    }
    std::vector<core::Vec2> through = pts_;
    if (close) {
        through.push_back(pts_.front()); // back to the start (C0 at the seam)
    }
    std::vector<core::Vec2> ctrl;
    if (fit_) {
        ctrl = core::spline::fit_or_fallback(through, degree_,
                                             static_cast<core::spline::FitParam>(knots_));
    } else {
        ctrl = std::move(through);
    }
    ctx.submit(core::AddSplineCommand{std::move(ctrl), static_cast<std::uint32_t>(degree_),
                                      ctx.group_id(), {}});
    ctx.set_preview({});
    done_ = true;
}

void SplineCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::First:
        if (u == "M" || u == "METHOD") {
            state_ = State::MethodPick;
            ctx.set_prompt(std::string("Enter spline creation method [Fit/CV] <") +
                           (fit_ ? "Fit" : "CV") + ">: ");
            return;
        }
        if (fit_ && (u == "K" || u == "KNOTS")) {
            state_ = State::KnotsPick;
            ctx.set_prompt(std::string("Enter knot parameterization [Chord/Square root/Uniform] <") +
                           knots_name(knots_) + ">: ");
            return;
        }
        if (!fit_ && (u == "D" || u == "DEGREE")) {
            state_ = State::DegreePick;
            ctx.set_prompt("Enter degree <" + std::to_string(degree_) + ">: ");
            return;
        }
        if (u == "O" || u == "OBJECT") {
            ctx.echo("Object: converting spline-fit polylines is not supported yet.");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            pts_.push_back(*p);
            ctx.set_last_point(*p);
            state_ = State::Next;
            refresh_preview(ctx);
            prompt_next(ctx);
        }
        return;
    case State::MethodPick:
        if (u == "F" || u == "FIT" || (t.empty() && fit_)) {
            fit_ = true;
        } else if (u == "CV" || u == "C" || (t.empty() && !fit_)) {
            fit_ = false;
        } else {
            ctx.echo("Enter Fit or CV.");
            return;
        }
        s_fit_ = fit_;
        state_ = State::First;
        prompt_first(ctx);
        return;
    case State::KnotsPick:
        if (u == "C" || u == "CHORD") {
            knots_ = 0;
        } else if (u == "S" || u == "SQUARE ROOT" || u == "SQRT") {
            knots_ = 1;
        } else if (u == "U" || u == "UNIFORM") {
            knots_ = 2;
        } else if (!t.empty()) {
            ctx.echo("Enter Chord, Square root or Uniform.");
            return;
        }
        s_knots_ = knots_;
        state_ = State::First;
        prompt_first(ctx);
        return;
    case State::DegreePick: {
        double v = static_cast<double>(degree_);
        if (!t.empty() && (!parse_number(t, v) || v < 1.0 || v > 10.0)) {
            ctx.echo("Degree must be between 1 and 10.");
            return;
        }
        degree_ = static_cast<int>(v);
        s_degree_ = degree_;
        state_ = State::First;
        prompt_first(ctx);
        return;
    }
    case State::Next:
        if (t.empty()) {
            finish(ctx, false);
            return;
        }
        if (u == "U" || u == "UNDO") {
            if (!pts_.empty()) {
                pts_.pop_back();
            }
            if (pts_.empty()) {
                state_ = State::First;
                ctx.set_preview({});
                prompt_first(ctx);
            } else {
                ctx.set_last_point(pts_.back());
                refresh_preview(ctx);
                prompt_next(ctx);
            }
            return;
        }
        if (u == "C" || u == "CLOSE") {
            if (pts_.size() < 3) {
                ctx.echo("Close needs at least three points.");
                return;
            }
            finish(ctx, true);
            return;
        }
        if (fit_ && (u == "T" || u == "TANGENCY")) {
            ctx.echo("Tangency is not supported yet; the end is left free.");
            return;
        }
        if (fit_ && (u == "L" || u == "TOLERANCE")) {
            ctx.echo("Fit tolerance other than 0 is not supported yet.");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            pts_.push_back(*p);
            ctx.set_last_point(*p);
            refresh_preview(ctx);
            prompt_next(ctx);
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// DONUT
// ---------------------------------------------------------------------------
namespace {
std::string fmt4(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    return buf;
}
} // namespace

void DonutCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    inner_ = s_inner_;
    outer_ = s_outer_;
    state_ = State::Inner;
    ctx.set_prompt("Specify inside diameter of donut <" + fmt4(inner_) + ">: ");
}

void DonutCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void DonutCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    switch (state_) {
    case State::Inner: {
        double v = inner_;
        if (!t.empty() && (!parse_number(t, v) || v < 0.0)) {
            ctx.echo("Enter a diameter of 0 or more.");
            return;
        }
        inner_ = v;
        state_ = State::Outer;
        ctx.set_prompt("Specify outside diameter of donut <" + fmt4(std::max(outer_, inner_)) + ">: ");
        return;
    }
    case State::Outer: {
        double v = std::max(outer_, inner_);
        if (!t.empty() && !parse_number(t, v)) {
            ctx.echo("Enter a diameter.");
            return;
        }
        if (v <= inner_) {
            ctx.echo("Value must be greater than the inside diameter.");
            return;
        }
        outer_ = v;
        s_inner_ = inner_;
        s_outer_ = outer_;
        state_ = State::Center;
        ctx.set_prompt("Specify center of donut or <exit>: ");
        return;
    }
    case State::Center: {
        if (t.empty()) {
            done_ = true;
            return;
        }
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        // A filled annulus: SOLID hatch with an outer loop and (when the hole has a size)
        // an inner loop -- even-odd, so the hole drops out.
        const auto ring = [&](double radius) {
            std::vector<core::Vec2> pts;
            constexpr int kSegs = 96;
            pts.reserve(kSegs);
            for (int i = 0; i < kSegs; ++i) {
                const double a = core::kTwoPi * static_cast<double>(i) / kSegs;
                pts.push_back({p->x + radius * std::cos(a), p->y + radius * std::sin(a)});
            }
            return pts;
        };
        core::AddHatchCommand h;
        h.loops.push_back(ring(outer_ * 0.5));
        if (inner_ > 0.0) {
            h.loops.push_back(ring(inner_ * 0.5));
        }
        h.pattern_name = "SOLID";
        h.group = ctx.group_id();
        ctx.submit(std::move(h));
        ctx.set_last_point(*p);
        ctx.set_prompt("Specify center of donut or <exit>: ");
        return;
    }
    }
}

// ---------------------------------------------------------------------------
// VIEW
// ---------------------------------------------------------------------------
void ViewCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Option;
    ctx.set_prompt("Enter an option [?/Delete/Orthographic/Restore/Save/Ucs/Window]: ");
}

void ViewCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void ViewCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Option:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (u == "?") {
            const std::vector<core::NamedView> views = ctx.named_views();
            if (views.empty()) {
                ctx.echo("No saved views.");
            } else {
                std::string line = "Saved views:";
                for (const core::NamedView& v : views) {
                    line += " \"" + v.name + "\"";
                }
                ctx.echo(line);
            }
            ctx.set_prompt("Enter an option [?/Delete/Orthographic/Restore/Save/Ucs/Window]: ");
            return;
        }
        if (u == "S" || u == "SAVE") {
            state_ = State::SaveName;
            ctx.set_prompt("Enter view name to save: ");
            return;
        }
        if (u == "R" || u == "RESTORE") {
            state_ = State::RestoreName;
            ctx.set_prompt("Enter view name to restore: ");
            return;
        }
        if (u == "D" || u == "DELETE") {
            state_ = State::DeleteName;
            ctx.set_prompt("Enter view name to delete: ");
            return;
        }
        if (u == "W" || u == "WINDOW") {
            state_ = State::WinFirst;
            ctx.set_prompt("Specify first corner: ");
            return;
        }
        if (u == "O" || u == "ORTHOGRAPHIC" || u == "U" || u == "UCS") {
            ctx.echo("Orthographic and UCS views do not apply to a 2D drawing.");
            return;
        }
        ctx.echo("Enter ?, Delete, Restore, Save or Window.");
        return;
    case State::SaveName: {
        if (t.empty()) {
            ctx.echo("A view name is required.");
            return;
        }
        core::Vec2 center{};
        double scale = 0.0;
        if (ctx.view() == nullptr || !ctx.view()->current_view(center, scale)) {
            ctx.echo("The current view is not available here.");
            done_ = true;
            return;
        }
        ctx.submit(core::SaveNamedViewCommand{core::NamedView{t, center, scale}});
        done_ = true;
        return;
    }
    case State::RestoreName: {
        for (const core::NamedView& v : ctx.named_views()) {
            if (v.name == t) {
                if (ctx.view() != nullptr) {
                    ctx.view()->set_view(v.center, v.scale);
                }
                ctx.echo("View \"" + t + "\" restored.");
                done_ = true;
                return;
            }
        }
        ctx.echo("View \"" + t + "\" not found.");
        done_ = true;
        return;
    }
    case State::DeleteName:
        if (t.empty()) {
            done_ = true;
            return;
        }
        ctx.submit(core::DeleteNamedViewCommand{t});
        done_ = true;
        return;
    case State::WinFirst:
        if (const auto p = read_point(ctx, text)) {
            w0_ = *p;
            ctx.set_last_point(*p);
            state_ = State::WinSecond;
            ctx.set_preview({PreviewKind::Rectangle, {*p}});
            ctx.set_prompt("Specify opposite corner: ");
        }
        return;
    case State::WinSecond:
        if (const auto p = read_point(ctx, text)) {
            w1_ = *p;
            ctx.set_preview({});
            state_ = State::WinName;
            ctx.set_prompt("Enter view name to save: ");
        }
        return;
    case State::WinName: {
        if (t.empty()) {
            ctx.echo("A view name is required.");
            return;
        }
        const core::Vec2 center = (w0_ + w1_) * 0.5;
        const double wx = std::abs(w1_.x - w0_.x);
        const double wy = std::abs(w1_.y - w0_.y);
        double scale = 1.0;
        int pw = 0;
        int ph = 0;
        core::Vec2 cur_c{};
        double cur_s = 0.0;
        if (ctx.view() != nullptr && ctx.view()->viewport_size(pw, ph) && wx > 1e-9 && wy > 1e-9) {
            scale = std::min(static_cast<double>(pw) / wx, static_cast<double>(ph) / wy);
        } else if (ctx.view() != nullptr && ctx.view()->current_view(cur_c, cur_s)) {
            scale = cur_s;
        }
        ctx.submit(core::SaveNamedViewCommand{core::NamedView{t, center, scale}});
        done_ = true;
        return;
    }
    }
}

// ---------------------------------------------------------------------------
// GROUP / UNGROUP / PICKSTYLE
// ---------------------------------------------------------------------------
void GroupCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Select;
    ctx.set_prompt("Select objects or [Name/Description]: ");
}

void GroupCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void GroupCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Select:
        if (t.empty()) {
            if (ctx.selection_count() == 0) {
                ctx.echo("Nothing selected.");
                done_ = true;
                return;
            }
            ctx.submit(core::CreateGroupCommand{name_, description_});
            done_ = true;
            return;
        }
        if (u == "N" || u == "NAME") {
            state_ = State::Name;
            ctx.set_prompt("Enter a group name or [?]: ");
            return;
        }
        if (u == "D" || u == "DESCRIPTION") {
            state_ = State::Description;
            ctx.set_prompt("Enter a group description: ");
            return;
        }
        if (u == "ALL") {
            ctx.submit(core::SelectAllCommand{});
            ctx.set_prompt("Select objects or [Name/Description]: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            ctx.submit(core::SelectPickCommand{*p, ctx.pick_radius(), true, true});
            ctx.set_prompt("Select objects or [Name/Description]: ");
        }
        return;
    case State::Name:
        if (u == "?") {
            ctx.echo("Group names are listed by the engine when created; use UNGROUP by name to remove one.");
            return;
        }
        name_ = t;
        state_ = State::Select;
        ctx.set_prompt("Select objects or [Name/Description]: ");
        return;
    case State::Description:
        description_ = t;
        state_ = State::Select;
        ctx.set_prompt("Select objects or [Name/Description]: ");
        return;
    }
}

void UngroupCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    by_name_ = false;
    ctx.set_prompt("Select group or [Name]: ");
}

void UngroupCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void UngroupCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    if (by_name_) {
        if (t.empty()) {
            done_ = true;
            return;
        }
        ctx.submit(core::UngroupCommand{t, {}, 0.0, true});
        done_ = true;
        return;
    }
    if (u == "N" || u == "NAME") {
        by_name_ = true;
        ctx.set_prompt("Enter group name: ");
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        ctx.submit(core::UngroupCommand{{}, *p, ctx.pick_radius(), false});
        done_ = true;
    }
}

void PickStyleCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Enter new value for PICKSTYLE <1>: ");
}

void PickStyleCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void PickStyleCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    double v = 1.0;
    if (!t.empty() && (!parse_number(t, v) || (v != 0.0 && v != 1.0))) {
        ctx.echo("Enter 0 or 1.");
        return;
    }
    ctx.submit(core::SetPickStyleCommand{v != 0.0});
    done_ = true;
}

// ---------------------------------------------------------------------------
// OSNAP / -OSNAP: running object snaps
// ---------------------------------------------------------------------------
namespace {
struct SnapCode {
    const char* code;
    const char* name;
    core::SnapType type;
};
constexpr SnapCode kSnapCodes[] = {
    {"END", "Endpoint", core::SnapType::Endpoint},
    {"MID", "Midpoint", core::SnapType::Midpoint},
    {"CEN", "Center", core::SnapType::Center},
    {"NOD", "Node", core::SnapType::Node},
    {"QUA", "Quadrant", core::SnapType::Quadrant},
    {"INT", "Intersection", core::SnapType::Intersection},
    {"PER", "Perpendicular", core::SnapType::Perpendicular},
    {"TAN", "Tangent", core::SnapType::Tangent},
    {"NEA", "Nearest", core::SnapType::Nearest},
    {"INS", "Insertion", core::SnapType::Insertion},
    {"APP", "Apparent intersection", core::SnapType::ApparentIntersection},
    {"PAR", "Parallel", core::SnapType::Parallel},
    {"CENTROID", "Centroid", core::SnapType::Centroid},
};
std::string snap_list(std::uint32_t mask) {
    std::string out;
    for (const SnapCode& c : kSnapCodes) {
        if ((mask & core::snap_bit(c.type)) != 0) {
            out += (out.empty() ? "" : ", ") + std::string(c.name);
        }
    }
    return out.empty() ? "none" : out;
}
} // namespace

void OsnapCommand::start(CommandContext& ctx) {
    if (ctx.view() != nullptr) {
        ctx.view()->osnap_settings_dialog();
    } else {
        ctx.echo("Object snap settings are not available here; use -OSNAP.");
    }
    done_ = true;
}

void OsnapCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void OsnapModesCommand::start(CommandContext& ctx) {
    const std::uint32_t cur = ctx.view() != nullptr ? ctx.view()->snap_mask() : 0;
    ctx.echo("Current object snap modes: " + snap_list(cur));
    ctx.set_prompt("Enter list of object snap modes: ");
}

void OsnapModesCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void OsnapModesCommand::input(CommandContext& ctx, const std::string& text) {
    std::string u = upper(trimmed(text));
    for (char& ch : u) {
        if (ch == ',' || ch == ';') {
            ch = ' ';
        }
    }
    std::uint32_t mask = 0;
    std::string tok;
    std::stringstream ss(u);
    bool any = false;
    while (ss >> tok) {
        any = true;
        if (tok == "NONE" || tok == "OFF") {
            mask = 0;
            continue;
        }
        if (tok == "ALL") {
            for (const SnapCode& c : kSnapCodes) {
                mask |= core::snap_bit(c.type);
            }
            continue;
        }
        bool known = false;
        for (const SnapCode& c : kSnapCodes) {
            const std::string code(c.code);
            std::string name = upper(c.name);
            if (tok == code || tok == name || (tok.size() >= 3 && name.rfind(tok, 0) == 0)) {
                mask |= core::snap_bit(c.type);
                known = true;
                break;
            }
        }
        if (!known) {
            ctx.echo("Unknown object snap mode \"" + tok + "\". Use END, MID, CEN, NOD, QUA, INT, PER, TAN, NEA, INS, APP, PAR, NONE or ALL.");
            return;
        }
    }
    if (!any) {
        done_ = true;
        return;
    }
    if (ctx.view() != nullptr) {
        ctx.view()->set_snap_mask(mask);
    }
    ctx.echo("Object snap modes: " + snap_list(mask));
    done_ = true;
}

// ---------------------------------------------------------------------------
// WIPEOUT / FIELD
// ---------------------------------------------------------------------------
void WipeoutCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::First;
    pts_.clear();
    ctx.set_prompt("Specify first point or [Frames/Polyline] <Polyline>: ");
}

void WipeoutCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.set_preview({});
    done_ = true;
}

void WipeoutCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::First:
        if (u == "F" || u == "FRAMES") {
            state_ = State::Frames;
            ctx.set_prompt("Enter mode [ON/OFF] <ON>: ");
            return;
        }
        if (t.empty() || u == "P" || u == "POLYLINE") {
            state_ = State::PolyPick;
            ctx.set_prompt("Select a closed polyline: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            pts_ = {*p};
            ctx.set_last_point(*p);
            state_ = State::Next;
            ctx.set_preview({PreviewKind::Polyline, pts_});
            ctx.set_prompt("Specify next point: ");
        }
        return;
    case State::Next:
        if (t.empty() || u == "C" || u == "CLOSE") {
            if (pts_.size() < 3) {
                ctx.echo("A wipeout needs at least three points.");
                return;
            }
            core::AddHatchCommand w;
            w.loops = {pts_};
            w.pattern_name = "WIPEOUT";
            w.group = ctx.group_id();
            ctx.submit(std::move(w));
            ctx.set_preview({});
            ctx.echo("Wipeout created.");
            done_ = true;
            return;
        }
        if (u == "U" || u == "UNDO") {
            if (!pts_.empty()) {
                pts_.pop_back();
            }
            if (pts_.empty()) {
                state_ = State::First;
                ctx.set_preview({});
                ctx.set_prompt("Specify first point or [Frames/Polyline] <Polyline>: ");
            } else {
                ctx.set_last_point(pts_.back());
                ctx.set_preview({PreviewKind::Polyline, pts_});
            }
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            pts_.push_back(*p);
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Polyline, pts_});
            ctx.set_prompt(pts_.size() >= 3 ? "Specify next point or [Undo/Close] <Close>: "
                                            : "Specify next point or [Undo]: ");
        }
        return;
    case State::Frames:
        if (u == "OFF") {
            ctx.submit(core::SetWipeoutFramesCommand{false});
        } else if (t.empty() || u == "ON") {
            ctx.submit(core::SetWipeoutFramesCommand{true});
        } else {
            ctx.echo("Enter ON or OFF.");
            return;
        }
        done_ = true;
        return;
    case State::PolyPick:
        if (const auto p = read_point(ctx, text)) {
            poly_pick_ = *p;
            state_ = State::PolyErase;
            ctx.set_prompt("Erase polyline? [Yes/No] <No>: ");
        }
        return;
    case State::PolyErase: {
        bool erase = false;
        if (u == "Y" || u == "YES") {
            erase = true;
        } else if (!t.empty() && u != "N" && u != "NO") {
            ctx.echo("Enter Yes or No.");
            return;
        }
        ctx.submit(core::WipeoutFromPolylineCommand{poly_pick_, ctx.pick_radius(), erase, ctx.group_id()});
        done_ = true;
        return;
    }
    }
}

void FieldCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Name;
    ctx.set_prompt("Enter field name [Date/Time/Filename/Login]: ");
}

void FieldCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void FieldCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Name:
        if (u == "D" || u == "DATE") {
            code_ = "%<Date>%";
        } else if (u == "T" || u == "TIME") {
            code_ = "%<Time>%";
        } else if (u == "F" || u == "FILENAME") {
            code_ = "%<Filename>%";
        } else if (u == "L" || u == "LOGIN") {
            code_ = "%<Login>%";
        } else {
            ctx.echo("Enter Date, Time, Filename or Login.");
            return;
        }
        state_ = State::Point;
        ctx.set_prompt("Specify start point: ");
        return;
    case State::Point:
        if (const auto p = read_point(ctx, text)) {
            pos_ = *p;
            ctx.set_last_point(*p);
            state_ = State::Height;
            ctx.set_prompt("Specify text height <" + fmt4(height_) + ">: ");
        }
        return;
    case State::Height:
        if (!t.empty()) {
            double h = 0.0;
            try {
                h = std::stod(t);
            } catch (...) {
                h = 0.0;
            }
            if (h <= 0.0) {
                ctx.echo("Enter a height greater than 0.");
                return;
            }
            height_ = h;
        }
        state_ = State::Rotation;
        ctx.set_prompt("Specify rotation angle <0>: ");
        return;
    case State::Rotation: {
        double deg = 0.0;
        if (!t.empty()) {
            try {
                deg = std::stod(t);
            } catch (...) {
                ctx.echo("Enter an angle in degrees.");
                return;
            }
        }
        core::AddTextCommand tc;
        tc.pos = pos_;
        tc.height = height_;
        tc.rotation = core::to_radians(deg);
        tc.content = code_;
        tc.group = ctx.group_id();
        ctx.submit(std::move(tc));
        ctx.echo("Field placed; it updates on the next regen.");
        done_ = true;
        return;
    }
    }
}

// ---------------------------------------------------------------------------
// PEDIT
// ---------------------------------------------------------------------------
void PeditCommand::prompt_option(CommandContext& ctx) const {
    ctx.set_prompt(
        "Enter an option [Close/Open/Join/Width/Edit vertex/Fit/Spline/Decurve/Ltype gen/Reverse/Undo]: ");
}

void PeditCommand::prompt_vertex(CommandContext& ctx) const {
    ctx.set_prompt("Enter a vertex editing option [Insert/Delete/Move/eXit] <X>: ");
}

void PeditCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Select;
    ctx.set_prompt("Select polyline or [Multiple]: ");
}

void PeditCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void PeditCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    const auto op = [&](std::uint8_t code, core::Vec2 p1 = {}, core::Vec2 p2 = {}) {
        ctx.submit(core::PeditCommand{pick_, ctx.pick_radius(), code, p1, p2, ctx.new_group()});
    };
    switch (state_) {
    case State::Select:
        if (u == "M" || u == "MULTIPLE") {
            ctx.echo("Multiple is not supported yet; select one polyline.");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            pick_ = *p;
            ctx.set_last_point(*p);
            state_ = State::Option;
            prompt_option(ctx);
        }
        return;
    case State::Option:
        if (t.empty() || u == "X" || u == "EXIT") {
            done_ = true;
            return;
        }
        if (u == "C" || u == "CLOSE") {
            op(0);
        } else if (u == "O" || u == "OPEN") {
            op(1);
        } else if (u == "J" || u == "JOIN") {
            join_picks_ = {pick_};
            state_ = State::JoinTargets;
            ctx.set_prompt("Select objects to join: ");
            return;
        } else if (u == "W" || u == "WIDTH") {
            ctx.echo("Width is not supported: polylines have no width here yet.");
        } else if (u == "E" || u == "EDIT VERTEX" || u == "EDIT") {
            state_ = State::Vertex;
            prompt_vertex(ctx);
            return;
        } else if (u == "F" || u == "FIT") {
            ctx.echo("Fit is not supported; Spline makes a fit spline through the vertices.");
        } else if (u == "S" || u == "SPLINE") {
            op(4);
            done_ = true; // the polyline is a spline now
            return;
        } else if (u == "D" || u == "DECURVE") {
            op(3);
        } else if (u == "L" || u == "LTYPE GEN" || u == "LTYPE") {
            ctx.echo("Ltype gen is not supported yet.");
        } else if (u == "R" || u == "REVERSE") {
            op(2);
        } else if (u == "U" || u == "UNDO") {
            ctx.submit(core::UndoLastGroupCommand{});
        } else {
            ctx.echo("Enter Close, Open, Join, Edit vertex, Spline, Decurve, Reverse, Undo or Enter to finish.");
        }
        prompt_option(ctx);
        return;
    case State::JoinTargets:
        if (t.empty()) {
            if (join_picks_.size() > 1) {
                ctx.submit(core::JoinPickCommand{join_picks_, ctx.pick_radius(), ctx.new_group()});
            }
            state_ = State::Option;
            prompt_option(ctx);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            join_picks_.push_back(*p);
            ctx.set_prompt("Select objects to join: ");
        }
        return;
    case State::Vertex:
        if (t.empty() || u == "X" || u == "EXIT") {
            state_ = State::Option;
            prompt_option(ctx);
            return;
        }
        if (u == "I" || u == "INSERT") {
            state_ = State::VInsert;
            ctx.set_prompt("Specify location of new vertex: ");
        } else if (u == "D" || u == "DELETE") {
            state_ = State::VDelete;
            ctx.set_prompt("Specify vertex to delete: ");
        } else if (u == "M" || u == "MOVE") {
            state_ = State::VMoveFrom;
            ctx.set_prompt("Specify vertex to move: ");
        } else {
            ctx.echo("Enter Insert, Delete, Move or eXit.");
            prompt_vertex(ctx);
        }
        return;
    case State::VInsert:
        if (const auto p = read_point(ctx, text)) {
            op(5, *p);
            state_ = State::Vertex;
            prompt_vertex(ctx);
        }
        return;
    case State::VDelete:
        if (const auto p = read_point(ctx, text)) {
            op(6, *p);
            state_ = State::Vertex;
            prompt_vertex(ctx);
        }
        return;
    case State::VMoveFrom:
        if (const auto p = read_point(ctx, text)) {
            vfrom_ = *p;
            ctx.set_last_point(*p);
            state_ = State::VMoveTo;
            ctx.set_preview({PreviewKind::Segment, {*p}});
            ctx.set_prompt("Specify new location: ");
        }
        return;
    case State::VMoveTo:
        if (const auto p = read_point(ctx, text)) {
            op(7, vfrom_, *p);
            pick_ = *p; // the moved vertex is on the polyline: keep picking there
            ctx.set_preview({});
            state_ = State::Vertex;
            prompt_vertex(ctx);
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// BLOCK / INSERT / WBLOCK / REGEN
// ---------------------------------------------------------------------------
namespace {
void echo_block_names(CommandContext& ctx) {
    const std::vector<std::string> names = ctx.block_names();
    if (names.empty()) {
        ctx.echo("No blocks defined.");
        return;
    }
    std::string line = "Blocks:";
    for (const std::string& n : names) {
        line += " \"" + n + "\"";
    }
    ctx.echo(line);
}
} // namespace

void BlockCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Name;
    ctx.set_prompt("Enter block name or [?]: ");
}

void BlockCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void BlockCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Name:
        if (u == "?") {
            echo_block_names(ctx);
            return;
        }
        if (t.empty()) {
            ctx.echo("A block name is required.");
            return;
        }
        name_ = t;
        state_ = State::Base;
        ctx.set_prompt("Specify insertion base point: ");
        return;
    case State::Base:
        if (const auto p = read_point(ctx, text)) {
            base_ = *p;
            ctx.set_last_point(*p);
            state_ = State::Select;
            ctx.set_prompt("Select objects: ");
        }
        return;
    case State::Select:
        if (t.empty()) {
            if (ctx.selection_count() == 0) {
                ctx.echo("Nothing selected.");
                done_ = true;
                return;
            }
            ctx.submit(core::DefineBlockCommand{name_, base_, ctx.group_id()});
            done_ = true;
            return;
        }
        if (u == "ALL") {
            ctx.submit(core::SelectAllCommand{});
            ctx.set_prompt("Select objects: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            ctx.submit(core::SelectPickCommand{*p, ctx.pick_radius(), true, true});
            ctx.set_prompt("Select objects: ");
        }
        return;
    }
}

void InsertCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Name;
    ctx.set_prompt("Enter block name or [?]" + (s_last_.empty() ? std::string() : " <" + s_last_ + ">") + ": ");
}

void InsertCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.set_preview({});
    done_ = true;
}

void InsertCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    double v = 0.0;
    switch (state_) {
    case State::Name:
        if (u == "?") {
            echo_block_names(ctx);
            return;
        }
        if (t.empty() && s_last_.empty()) {
            ctx.echo("A block name is required.");
            return;
        }
        name_ = t.empty() ? s_last_ : t;
        {
            bool known = false;
            for (const std::string& n : ctx.block_names()) {
                known = known || n == name_;
            }
            if (!known) {
                ctx.echo("Block \"" + name_ + "\" not found.");
                return;
            }
        }
        state_ = State::Point;
        ctx.set_prompt("Specify insertion point or [Scale/Rotate]: ");
        return;
    case State::Point:
        if (u == "S" || u == "SCALE") {
            state_ = State::ScaleX;
            ctx.set_prompt("Enter scale factor for XY axes <1>: ");
            return;
        }
        if (u == "R" || u == "ROTATE") {
            state_ = State::Rotation;
            ctx.set_prompt("Specify rotation angle <0>: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            pos_ = *p;
            ctx.set_last_point(*p);
            state_ = State::ScaleX;
            ctx.set_prompt("Enter X scale factor or [Corner/XYZ] <1>: ");
        }
        return;
    case State::ScaleX:
        if (!t.empty()) {
            if (!parse_number(t, v) || v == 0.0) {
                ctx.echo("Enter a non-zero scale factor.");
                return;
            }
            sx_ = v;
            sy_ = v;
        }
        state_ = State::ScaleY;
        ctx.set_prompt("Enter Y scale factor <use X scale factor>: ");
        return;
    case State::ScaleY:
        if (!t.empty()) {
            if (!parse_number(t, v) || v == 0.0) {
                ctx.echo("Enter a non-zero scale factor.");
                return;
            }
            sy_ = v;
        }
        state_ = State::Rotation;
        ctx.set_prompt("Specify rotation angle <0>: ");
        return;
    case State::Rotation: {
        double deg = 0.0;
        if (!t.empty() && !parse_number(t, deg)) {
            ctx.echo("Enter an angle in degrees.");
            return;
        }
        rot_ = core::to_radians(deg);
        // Attributes: every value starts at its default; the ones that are neither
        // Constant nor Preset are asked for, as AutoCAD does with ATTDIA off.
        attdefs_ = ctx.block_attdefs(name_);
        values_.clear();
        for (const core::BlockAttDefInfo& a : attdefs_) {
            values_.push_back(a.def);
        }
        attrib_index_ = 0;
        if (!attdefs_.empty()) {
            ctx.echo("Enter attribute values");
        }
        next_attrib_or_finish(ctx);
        return;
    }
    case State::Attrib:
        if (!t.empty()) {
            values_[attrib_index_] = t;
        }
        ++attrib_index_;
        next_attrib_or_finish(ctx);
        return;
    }
}

void InsertCommand::next_attrib_or_finish(CommandContext& ctx) {
    while (attrib_index_ < attdefs_.size()) {
        const core::BlockAttDefInfo& a = attdefs_[attrib_index_];
        if ((a.flags & (core::kAttConstant | core::kAttPreset)) == 0) {
            state_ = State::Attrib;
            const std::string label = a.prompt.empty() ? a.tag : a.prompt;
            ctx.set_prompt(label + (a.def.empty() ? ": " : " <" + a.def + ">: "));
            return;
        }
        ++attrib_index_;
    }
    s_last_ = name_;
    ctx.submit(core::InsertBlockCommand{name_, pos_, sx_, sy_, rot_, ctx.group_id(), values_});
    done_ = true;
}

// ---------------------------------------------------------------------------
// ATTDEF / ATTDISP / ATTEDIT
// ---------------------------------------------------------------------------
void AttdefCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Modes;
    flags_ = 0;
    height_ = s_height_;
    prompt_modes(ctx);
}

void AttdefCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void AttdefCommand::prompt_modes(CommandContext& ctx) {
    const auto yn = [this](std::uint8_t bit) { return (flags_ & bit) != 0 ? "Y" : "N"; };
    ctx.echo(std::string("Current attribute modes: Invisible=") + yn(core::kAttInvisible) +
             " Constant=" + yn(core::kAttConstant) + " Verify=" + yn(core::kAttVerify) +
             " Preset=" + yn(core::kAttPreset));
    ctx.set_prompt("Enter an option to change [Invisible/Constant/Verify/Preset] <done>: ");
}

void AttdefCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Modes:
        if (u == "I" || u == "INVISIBLE") {
            flags_ ^= core::kAttInvisible;
        } else if (u == "C" || u == "CONSTANT") {
            flags_ ^= core::kAttConstant;
        } else if (u == "V" || u == "VERIFY") {
            flags_ ^= core::kAttVerify;
        } else if (u == "P" || u == "PRESET") {
            flags_ ^= core::kAttPreset;
        } else if (t.empty()) {
            state_ = State::Tag;
            ctx.set_prompt("Enter attribute tag name: ");
            return;
        } else {
            ctx.echo("Enter Invisible, Constant, Verify, Preset, or press Enter.");
            return;
        }
        prompt_modes(ctx);
        return;
    case State::Tag:
        if (t.empty() || t.find(' ') != std::string::npos) {
            ctx.echo("A tag is required and cannot contain spaces.");
            return;
        }
        tag_ = u; // tags are upper-case, as in AutoCAD
        state_ = State::Prompt;
        ctx.set_prompt("Enter attribute prompt: ");
        return;
    case State::Prompt:
        prompt_ = t;
        state_ = State::Default;
        ctx.set_prompt((flags_ & core::kAttConstant) != 0 ? "Enter attribute value: "
                                                          : "Enter default attribute value: ");
        return;
    case State::Default:
        default_ = t;
        state_ = State::Point;
        ctx.set_prompt("Specify start point: ");
        return;
    case State::Point:
        if (const auto p = read_point(ctx, text)) {
            pos_ = *p;
            ctx.set_last_point(*p);
            state_ = State::Height;
            ctx.set_prompt("Specify height <" + fmt4(height_) + ">: ");
        }
        return;
    case State::Height: {
        if (!t.empty()) {
            double h = 0.0;
            if (!parse_number(t, h) || h <= 0.0) {
                ctx.echo("Enter a height greater than 0.");
                return;
            }
            height_ = h;
        }
        state_ = State::Rotation;
        ctx.set_prompt("Specify rotation angle of text <0>: ");
        return;
    }
    case State::Rotation: {
        double deg = 0.0;
        if (!t.empty() && !parse_number(t, deg)) {
            ctx.echo("Enter an angle in degrees.");
            return;
        }
        s_height_ = height_;
        std::string style_name;
        const std::vector<core::TextStyle> styles = ctx.text_styles();
        const std::uint16_t cur = ctx.current_text_style();
        if (cur > 0 && cur < styles.size()) {
            style_name = styles[cur].name;
        }
        core::AddAttDefCommand c;
        c.text.pos = pos_;
        c.text.height = height_;
        c.text.rotation = core::to_radians(deg);
        c.text.content = tag_;
        c.text.style = style_name;
        c.prompt = prompt_;
        c.def = default_;
        c.flags = flags_;
        c.group = ctx.group_id();
        ctx.submit(std::move(c));
        done_ = true;
        return;
    }
    }
}

void RefeditCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select reference: ");
}

void RefeditCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void RefeditCommand::input(CommandContext& ctx, const std::string& text) {
    if (const auto p = read_point(ctx, text)) {
        ctx.submit(core::RefEditCommand{*p, ctx.pick_radius(), ctx.group_id()});
        done_ = true;
    }
}

void RefsetCommand::start(CommandContext& ctx) {
    state_ = State::Option;
    ctx.set_prompt("Enter an option [Add/Remove] <Add>: ");
}

void RefsetCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void RefsetCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Option:
        if (u == "R" || u == "REMOVE") {
            add_ = false;
        } else if (!(t.empty() || u == "A" || u == "ADD")) {
            ctx.echo("Enter Add or Remove.");
            return;
        }
        state_ = State::Select;
        ctx.set_prompt("Select objects: ");
        return;
    case State::Select:
        if (t.empty()) {
            if (ctx.selection_count() == 0) {
                ctx.echo("Nothing selected.");
                done_ = true;
                return;
            }
            ctx.submit(core::RefSetCommand{add_, ctx.group_id()});
            done_ = true;
            return;
        }
        if (u == "ALL") {
            ctx.submit(core::SelectAllCommand{});
            ctx.set_prompt("Select objects: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            ctx.submit(core::SelectPickCommand{*p, ctx.pick_radius(), true, true});
            ctx.set_prompt("Select objects: ");
        }
        return;
    }
}

void RefcloseCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Enter option [Save/Discard reference changes] <Save>: ");
}

void RefcloseCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void RefcloseCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    bool save = true;
    if (u == "D" || u == "DISCARD") {
        save = false;
    } else if (!(u.empty() || u == "S" || u == "SAVE")) {
        ctx.echo("Enter Save or Discard.");
        return;
    }
    ctx.submit(core::RefCloseCommand{save, ctx.group_id()});
    done_ = true;
}

void ImageAttachCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::File;
    ctx.set_prompt("Enter image file name (~ for the file dialog): ");
}

void ImageAttachCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void ImageAttachCommand::ask_embed(CommandContext& ctx) {
    state_ = State::Embed;
    ctx.set_prompt("Keep a copy of the image inside the drawing? [Yes/No] <No>: ");
}

void ImageAttachCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::File:
        if (t == "~" || t.empty()) {
            path_ = ctx.view() != nullptr ? ctx.view()->image_file_dialog() : std::string();
            if (path_.empty()) {
                if (t.empty()) {
                    ctx.echo("A file name is required (~ opens the file dialog).");
                    return;
                }
                ctx.echo("*Cancel*");
                done_ = true;
                return;
            }
        } else {
            path_ = t;
        }
        ask_embed(ctx);
        return;
    case State::Embed:
        if (u == "Y" || u == "YES") {
            embed_ = true;
        } else if (!(t.empty() || u == "N" || u == "NO")) {
            ctx.echo("Enter Yes or No.");
            return;
        }
        state_ = State::Point;
        ctx.set_prompt("Specify insertion point <0,0>: ");
        return;
    case State::Point:
        if (t.empty()) {
            pos_ = {0.0, 0.0};
        } else if (const auto p = read_point(ctx, text)) {
            pos_ = *p;
        } else {
            return;
        }
        ctx.set_last_point(pos_);
        state_ = State::Scale;
        ctx.set_prompt("Specify scale factor <1>: ");
        return;
    case State::Scale: {
        if (!t.empty()) {
            double v = 0.0;
            if (!parse_number(t, v) || v <= 0.0) {
                ctx.echo("Enter a scale factor greater than 0.");
                return;
            }
            scale_ = v;
        }
        state_ = State::Rotation;
        ctx.set_prompt("Specify rotation <0>: ");
        return;
    }
    case State::Rotation: {
        double deg = 0.0;
        if (!t.empty() && !parse_number(t, deg)) {
            ctx.echo("Enter an angle in degrees.");
            return;
        }
        ctx.submit(core::AttachImageCommand{path_, embed_, pos_, scale_, core::to_radians(deg),
                                            ctx.group_id()});
        done_ = true;
        return;
    }
    }
}

void ImageClipCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Pick;
    ctx.set_prompt("Select image to clip: ");
}

void ImageClipCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void ImageClipCommand::input(CommandContext& ctx, const std::string& text) {
    using Mode = core::SetImageClipCommand::Mode;
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Pick:
        if (const auto p = read_point(ctx, text)) {
            pick_ = *p;
            state_ = State::Option;
            ctx.set_prompt("Enter image clipping option [ON/OFF/Delete/New boundary] <New>: ");
        }
        return;
    case State::Option: {
        std::optional<Mode> mode;
        if (u == "ON") {
            mode = Mode::On;
        } else if (u == "OFF") {
            mode = Mode::Off;
        } else if (u == "D" || u == "DELETE") {
            mode = Mode::Delete;
        } else if (t.empty() || u == "N" || u == "NEW" || u == "NEW BOUNDARY") {
            state_ = State::Shape;
            ctx.set_prompt("Enter clipping type [Polygonal/Rectangular] <Rectangular>: ");
            return;
        } else {
            ctx.echo("Enter ON, OFF, Delete or New.");
            return;
        }
        ctx.submit(core::SetImageClipCommand{pick_, ctx.pick_radius(), *mode, {}, {}, ctx.group_id()});
        done_ = true;
        return;
    }
    case State::Shape:
        if (u == "P" || u == "POLYGONAL") {
            ctx.echo("Polygonal boundaries are not available yet; a rectangular one is.");
            return;
        }
        if (!(t.empty() || u == "R" || u == "RECTANGULAR")) {
            ctx.echo("Enter Polygonal or Rectangular.");
            return;
        }
        state_ = State::First;
        ctx.set_prompt("Specify first corner point: ");
        return;
    case State::First:
        if (const auto p = read_point(ctx, text)) {
            first_ = *p;
            ctx.set_last_point(*p);
            state_ = State::Second;
            ctx.set_preview({PreviewKind::Rectangle, {first_}});
            ctx.set_prompt("Specify opposite corner point: ");
        }
        return;
    case State::Second:
        if (const auto p = read_point(ctx, text)) {
            ctx.set_preview({});
            ctx.submit(core::SetImageClipCommand{pick_, ctx.pick_radius(), Mode::NewRect, first_, *p,
                                                 ctx.group_id()});
            done_ = true;
        }
        return;
    }
}

void ImageFrameCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Enter new value for IMAGEFRAME [0 hidden/1 shown and plotted/2 shown only] <1>: ");
}

void ImageFrameCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void ImageFrameCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    std::uint8_t mode = 1;
    if (!t.empty()) {
        if (t != "0" && t != "1" && t != "2") {
            ctx.echo("Enter 0, 1 or 2.");
            return;
        }
        mode = static_cast<std::uint8_t>(t[0] - '0');
    }
    ctx.submit(core::SetImageFrameCommand{mode});
    done_ = true;
}

namespace {
std::string layout_list(CommandContext& ctx) {
    std::string out = "Model";
    for (const std::string& n : ctx.layout_names()) {
        out += ", " + n;
    }
    return out;
}
} // namespace

void LayoutCommand::start(CommandContext& ctx) {
    state_ = State::Option;
    ctx.set_prompt("Enter layout option [Copy/Delete/New/Rename/Set/?] <Set>: ");
}

void LayoutCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void LayoutCommand::input(CommandContext& ctx, const std::string& text) {
    using Op = core::LayoutCommand::Op;
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Option:
        set_ = false;
        if (u == "?") {
            ctx.echo("Layouts: " + layout_list(ctx));
            ctx.set_prompt("Enter layout option [Copy/Delete/New/Rename/Set/?] <Set>: ");
            return;
        }
        if (u == "C" || u == "COPY") {
            op_ = Op::Copy;
            ctx.set_prompt("Enter name of layout to copy: ");
        } else if (u == "D" || u == "DELETE") {
            op_ = Op::Delete;
            ctx.set_prompt("Enter name of layout to delete: ");
        } else if (u == "N" || u == "NEW") {
            op_ = Op::New;
            ctx.set_prompt("Enter new layout name: ");
        } else if (u == "R" || u == "RENAME") {
            op_ = Op::Rename;
            ctx.set_prompt("Enter name of layout to rename: ");
        } else if (t.empty() || u == "S" || u == "SET") {
            set_ = true;
            ctx.set_prompt("Enter layout to make current (or Model) <" +
                           (ctx.layout_names().empty() ? std::string("Model") : ctx.layout_names().front()) +
                           ">: ");
        } else {
            ctx.echo("Enter Copy, Delete, New, Rename, Set or ?.");
            return;
        }
        state_ = State::Name;
        return;
    case State::Name:
        if (set_) {
            std::string want = t.empty() ? (ctx.layout_names().empty() ? std::string("Model")
                                                                       : ctx.layout_names().front())
                                         : t;
            if (upper(want) == "MODEL") {
                ctx.submit(core::SetActiveSpaceCommand{0});
                done_ = true;
                return;
            }
            ctx.submit(core::SetActiveSpaceCommand{0, want}); // the engine resolves the name
            done_ = true;
            return;
        }
        if (t.empty()) {
            ctx.echo("A layout name is required.");
            return;
        }
        name_ = t;
        if (op_ == Op::New) {
            ctx.submit(core::LayoutCommand{Op::New, name_, std::string(), ctx.group_id()});
            done_ = true;
            return;
        }
        if (op_ == Op::Delete) {
            ctx.submit(core::LayoutCommand{Op::Delete, name_, std::string(), ctx.group_id()});
            done_ = true;
            return;
        }
        state_ = State::NewName;
        ctx.set_prompt(op_ == Op::Copy ? "Enter layout name for copy: " : "Enter new layout name: ");
        return;
    case State::NewName:
        if (t.empty()) {
            ctx.echo("A name is required.");
            return;
        }
        ctx.submit(core::LayoutCommand{op_, name_, t, ctx.group_id()});
        done_ = true;
        return;
    }
}

void ModelSpaceCommand::start(CommandContext& ctx) {
    if (!to_paper_) {
        ctx.submit(core::SetActiveSpaceCommand{0});
    } else if (ctx.active_space() != 0) {
        ctx.echo("Already in paper space.");
    } else {
        ctx.submit(core::SetActiveSpaceCommand{0xFF}); // the engine picks the first layout
    }
    done_ = true;
}

void MviewCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::First;
    if (ctx.active_space() == 0) {
        ctx.echo("MVIEW works on a layout: pick a layout tab (or LAYOUT Set) first.");
        done_ = true;
        return;
    }
    ctx.set_prompt("Specify corner of viewport or [ON/OFF/Fit/Scale/Center] <Fit>: ");
}

void MviewCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.set_preview({});
    done_ = true;
}

void MviewCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::First:
        if (t.empty() || u == "F" || u == "FIT") {
            ctx.submit(core::CreateViewportCommand{{}, {}, true, ctx.group_id()});
            done_ = true;
            return;
        }
        if (u == "ON" || u == "OFF") {
            on_ = u == "ON" ? 1 : 0;
            state_ = State::PickOnOff;
            ctx.set_prompt("Select viewport: ");
            return;
        }
        if (u == "S" || u == "SCALE") {
            state_ = State::PickScale;
            ctx.set_prompt("Select viewport: ");
            return;
        }
        if (u == "C" || u == "CENTER") {
            state_ = State::PickCenter;
            ctx.set_prompt("Select viewport: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            first_ = *p;
            ctx.set_last_point(*p);
            state_ = State::Second;
            ctx.set_preview({PreviewKind::Rectangle, {first_}});
            ctx.set_prompt("Specify opposite corner: ");
        }
        return;
    case State::Second:
        if (const auto p = read_point(ctx, text)) {
            ctx.set_preview({});
            ctx.submit(core::CreateViewportCommand{first_, *p, false, ctx.group_id()});
            done_ = true;
        }
        return;
    case State::PickOnOff:
        if (const auto p = read_point(ctx, text)) {
            ctx.submit(core::SetViewportViewCommand{*p, ctx.pick_radius(), on_, 0.0, std::nullopt, ctx.group_id()});
            done_ = true;
        }
        return;
    case State::PickScale:
        if (const auto p = read_point(ctx, text)) {
            pick_ = *p;
            state_ = State::Scale;
            ctx.set_prompt("Enter viewport scale (paper mm per model unit, e.g. 0.5 for 1:2): ");
        }
        return;
    case State::Scale: {
        double v = 0.0;
        if (!parse_number(t, v) || v <= 0.0) {
            ctx.echo("Enter a scale greater than 0.");
            return;
        }
        ctx.submit(core::SetViewportViewCommand{pick_, ctx.pick_radius(), -1, v, std::nullopt, ctx.group_id()});
        done_ = true;
        return;
    }
    case State::PickCenter:
        if (const auto p = read_point(ctx, text)) {
            pick_ = *p;
            state_ = State::Center;
            ctx.set_prompt("Specify the model point to show at the viewport's centre: ");
        }
        return;
    case State::Center:
        if (const auto p = read_point(ctx, text)) {
            ctx.submit(core::SetViewportViewCommand{pick_, ctx.pick_radius(), -1, 0.0, *p, ctx.group_id()});
            done_ = true;
        }
        return;
    }
}

void AttdispCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Enter attribute visibility setting [Normal/ON/OFF] <Normal>: ");
}

void AttdispCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void AttdispCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    std::uint8_t mode = 0;
    if (u == "ON") {
        mode = 1;
    } else if (u == "OFF") {
        mode = 2;
    } else if (!(u.empty() || u == "N" || u == "NORMAL")) {
        ctx.echo("Enter Normal, ON or OFF.");
        return;
    }
    ctx.submit(core::SetAttDispCommand{mode});
    done_ = true;
}

void AtteditCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Pick;
    ctx.set_prompt("Select block reference: ");
}

void AtteditCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void AtteditCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    switch (state_) {
    case State::Pick:
        if (const auto p = read_point(ctx, text)) {
            pick_ = *p;
            state_ = State::Tag;
            ctx.set_prompt("Enter attribute tag to change <all>: ");
        }
        return;
    case State::Tag:
        tag_ = upper(t);
        state_ = State::Value;
        ctx.set_prompt("Enter new attribute value: ");
        return;
    case State::Value:
        ctx.submit(core::SetInsertAttribCommand{pick_, ctx.pick_radius(), tag_, t, ctx.group_id()});
        done_ = true;
        return;
    }
}

void WblockCommand::start(CommandContext& ctx) {
    state_ = State::Name;
    ctx.set_prompt("Enter name of existing block or [?/* (whole drawing)] <*>: ");
}

void WblockCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void WblockCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (state_ == State::Name) {
        if (t == "?") {
            echo_block_names(ctx);
            return;
        }
        name_ = (t.empty() || t == "*") ? std::string() : t;
        state_ = State::Path;
        ctx.set_prompt("Specify output file (.musa): ");
        return;
    }
    if (t.empty()) {
        ctx.echo("A file name is required.");
        return;
    }
    ctx.submit(core::WriteBlockCommand{name_, t});
    done_ = true;
}

void RegenCommand::start(CommandContext& ctx) {
    ctx.submit(core::RegenCommand{});
    done_ = true;
}

void RegenCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// STYLE (-STYLE): the command-line flow, prompt by prompt
// ---------------------------------------------------------------------------
void StyleCommand::start(CommandContext& ctx) {
    state_ = State::Name;
    const std::vector<core::TextStyle> styles = ctx.text_styles();
    const std::uint16_t cur = ctx.current_text_style();
    const std::string cur_name = cur < styles.size() ? styles[cur].name : "Standard";
    ctx.echo("Current text style: \"" + cur_name + "\"");
    ctx.set_prompt("Enter name of text style or [?] <" + cur_name + ">: ");
}

void StyleCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void StyleCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    double v = 0.0;
    switch (state_) {
    case State::Name: {
        const std::vector<core::TextStyle> styles = ctx.text_styles();
        if (u == "?") {
            std::string line = "Text styles:";
            for (const core::TextStyle& s : styles) {
                line += " \"" + s.name + "\"";
            }
            ctx.echo(styles.empty() ? "Text styles: \"Standard\"" : line);
            return;
        }
        const std::uint16_t cur = ctx.current_text_style();
        std::string name = t.empty() ? (cur < styles.size() ? styles[cur].name : "Standard") : t;
        bool found = false;
        for (const core::TextStyle& s : styles) {
            if (s.name == name) {
                ts_ = s;
                found = true;
            }
        }
        if (!found) {
            ts_ = core::TextStyle{};
            ts_.name = name;
            ctx.echo("New style.");
        }
        state_ = State::Font;
        ctx.set_prompt("Specify full font name or font filename (TTF or SHX) <" +
                       (ts_.font.empty() ? std::string("txt") : ts_.font) + ">: ");
        return;
    }
    case State::Font:
        if (!t.empty()) {
            ts_.font = (u == "TXT" || u == "TXT.SHX") ? std::string{} : t;
        }
        state_ = State::Height;
        ctx.set_prompt("Specify height of text <" + fmt4(ts_.height) + ">: ");
        return;
    case State::Height:
        if (!t.empty()) {
            if (!parse_number(t, v) || v < 0.0) {
                ctx.echo("Enter a height of 0 (not fixed) or more.");
                return;
            }
            ts_.height = v;
        }
        state_ = State::Width;
        ctx.set_prompt("Specify width factor <" + fmt4(ts_.width_factor) + ">: ");
        return;
    case State::Width:
        if (!t.empty()) {
            if (!parse_number(t, v) || v <= 0.0) {
                ctx.echo("Enter a width factor greater than 0.");
                return;
            }
            ts_.width_factor = v;
        }
        state_ = State::Oblique;
        ctx.set_prompt("Specify obliquing angle <" + fmt4(core::to_degrees(ts_.oblique)) + ">: ");
        return;
    case State::Oblique:
        if (!t.empty()) {
            if (!parse_number(t, v) || v <= -85.0 || v >= 85.0) {
                ctx.echo("Enter an angle between -85 and 85 degrees.");
                return;
            }
            ts_.oblique = core::to_radians(v);
        }
        state_ = State::Backwards;
        ctx.set_prompt("Display text backwards? [Yes/No] <N>: ");
        return;
    case State::Backwards:
    case State::Upside:
    case State::Vertical:
        if (u == "Y" || u == "YES") {
            ctx.echo("That option is not supported yet; the style keeps normal orientation.");
        } else if (!t.empty() && u != "N" && u != "NO") {
            ctx.echo("Enter Yes or No.");
            return;
        }
        if (state_ == State::Backwards) {
            state_ = State::Upside;
            ctx.set_prompt("Display text upside-down? [Yes/No] <N>: ");
            return;
        }
        if (state_ == State::Upside) {
            state_ = State::Vertical;
            ctx.set_prompt("Vertical? [Yes/No] <N>: ");
            return;
        }
        ctx.submit(core::SetTextStyleCommand{ts_, true});
        done_ = true;
        return;
    }
}

// ---------------------------------------------------------------------------
// UNITS (-UNITS): the command-line flow, prompt by prompt
// ---------------------------------------------------------------------------
void UnitsCommand::start(CommandContext& ctx) {
    u_ = ctx.units();
    state_ = State::Linear;
    ctx.echo(std::string("Current units: ") + core::units::linear_name(u_.linear) + ", precision " +
             std::to_string(u_.linear_precision) + "; angles " + core::units::angular_name(u_.angular) +
             ", precision " + std::to_string(u_.angular_precision) + "; base angle " +
             fmt4(core::to_degrees(u_.base_angle)) + (u_.clockwise ? "; clockwise." : "; counter-clockwise."));
    ctx.set_prompt(std::string("Enter units type [Scientific/Decimal/Engineering/Architectural/Fractional] <") +
                   core::units::linear_name(u_.linear) + ">: ");
}

void UnitsCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void UnitsCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    double v = 0.0;
    switch (state_) {
    case State::Linear:
        if (u == "S" || u == "SCIENTIFIC" || u == "1") {
            u_.linear = core::LinearFormat::Scientific;
        } else if (u == "D" || u == "DECIMAL" || u == "2") {
            u_.linear = core::LinearFormat::Decimal;
        } else if (u == "E" || u == "ENGINEERING" || u == "3") {
            u_.linear = core::LinearFormat::Engineering;
        } else if (u == "A" || u == "ARCHITECTURAL" || u == "4") {
            u_.linear = core::LinearFormat::Architectural;
        } else if (u == "F" || u == "FRACTIONAL" || u == "5") {
            u_.linear = core::LinearFormat::Fractional;
        } else if (!t.empty()) {
            ctx.echo("Enter Scientific, Decimal, Engineering, Architectural or Fractional.");
            return;
        }
        state_ = State::LinearPrecision;
        ctx.set_prompt("Enter number of digits to right of decimal point (0 to 8) <" +
                       std::to_string(u_.linear_precision) + ">: ");
        return;
    case State::LinearPrecision:
        if (!t.empty()) {
            if (!parse_number(t, v) || v < 0.0 || v > 8.0) {
                ctx.echo("Enter a value from 0 to 8.");
                return;
            }
            u_.linear_precision = static_cast<std::uint8_t>(v);
        }
        state_ = State::Angular;
        ctx.set_prompt(std::string("Enter angle format [Decimal degrees/Deg-Min-Sec/Grads/Radians/Surveyor] <") +
                       core::units::angular_name(u_.angular) + ">: ");
        return;
    case State::Angular:
        if (u == "D" || u == "DECIMAL DEGREES" || u == "1") {
            u_.angular = core::AngleFormat::DecimalDegrees;
        } else if (u == "DMS" || u == "DEG-MIN-SEC" || u == "M" || u == "2") {
            u_.angular = core::AngleFormat::DegMinSec;
        } else if (u == "G" || u == "GRADS" || u == "3") {
            u_.angular = core::AngleFormat::Grads;
        } else if (u == "R" || u == "RADIANS" || u == "4") {
            u_.angular = core::AngleFormat::Radians;
        } else if (u == "S" || u == "SURVEYOR" || u == "5") {
            u_.angular = core::AngleFormat::Surveyor;
        } else if (!t.empty()) {
            ctx.echo("Enter Decimal degrees, Deg-Min-Sec, Grads, Radians or Surveyor.");
            return;
        }
        state_ = State::AngularPrecision;
        ctx.set_prompt("Enter number of fractional places for display of angles (0 to 8) <" +
                       std::to_string(u_.angular_precision) + ">: ");
        return;
    case State::AngularPrecision:
        if (!t.empty()) {
            if (!parse_number(t, v) || v < 0.0 || v > 8.0) {
                ctx.echo("Enter a value from 0 to 8.");
                return;
            }
            u_.angular_precision = static_cast<std::uint8_t>(v);
        }
        state_ = State::Base;
        ctx.echo("Direction for angle 0: East 3 o'clock = 0, North 12 o'clock = 90, West 9 o'clock = 180, South 6 o'clock = 270");
        ctx.set_prompt("Enter direction for angle 0 <" + fmt4(core::to_degrees(u_.base_angle)) + ">: ");
        return;
    case State::Base:
        if (!t.empty()) {
            if (!parse_number(t, v)) {
                ctx.echo("Enter an angle in degrees.");
                return;
            }
            u_.base_angle = core::to_radians(v);
        }
        state_ = State::Clockwise;
        ctx.set_prompt(std::string("Measure angles clockwise? [Yes/No] <") + (u_.clockwise ? "Y" : "N") + ">: ");
        return;
    case State::Clockwise:
        if (u == "Y" || u == "YES") {
            u_.clockwise = true;
        } else if (u == "N" || u == "NO") {
            u_.clockwise = false;
        } else if (!t.empty()) {
            ctx.echo("Enter Yes or No.");
            return;
        }
        ctx.submit(core::SetUnitsCommand{u_});
        done_ = true;
        return;
    }
}

// ---------------------------------------------------------------------------
// AUDIT
// ---------------------------------------------------------------------------
void AuditDrawingCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Fix any errors detected? [Yes/No] <N>: ");
}

void AuditDrawingCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void AuditDrawingCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    bool fix = false;
    if (u == "Y" || u == "YES") {
        fix = true;
    } else if (!u.empty() && u != "N" && u != "NO") {
        ctx.echo("Enter Yes or No.");
        return;
    }
    ctx.submit(core::AuditCommand{fix});
    done_ = true;
}

// ---------------------------------------------------------------------------
// XLINE / RAY: construction lines
// ---------------------------------------------------------------------------
void XlineCommand::emit(CommandContext& ctx, core::Vec2 base, core::Vec2 dir) {
    if (core::length(dir) < 1e-9) {
        return;
    }
    ctx.submit(core::AddXlineCommand{base, core::normalized(dir), ray_, ctx.group_id(), {}});
}

void XlineCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::First;
    mode_ = 0;
    if (ray_) {
        ctx.set_prompt("Specify start point: ");
    } else {
        ctx.set_prompt("Specify a point or [Hor/Ver/Ang/Bisect]: ");
    }
}

void XlineCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::First: {
        if (!ray_) {
            if (u == "H" || u == "HOR") {
                mode_ = 1;
                state_ = State::Through;
                ctx.set_prompt("Specify through point: ");
                return;
            }
            if (u == "V" || u == "VER") {
                mode_ = 2;
                state_ = State::Through;
                ctx.set_prompt("Specify through point: ");
                return;
            }
            if (u == "A" || u == "ANG") {
                state_ = State::Angle;
                ctx.set_prompt("Enter angle of xline (0): ");
                return;
            }
            if (u == "B" || u == "BISECT") {
                state_ = State::BisectVertex;
                ctx.set_prompt("Specify angle vertex point: ");
                return;
            }
            if (u == "O" || u == "OFFSET") {
                ctx.echo("XLINE Offset is not supported yet; use the OFFSET command on a line.");
                return;
            }
        }
        if (const auto p = read_point(ctx, text)) {
            root_ = *p;
            ctx.set_last_point(*p);
            state_ = State::Through;
            ctx.set_prompt("Specify through point: ");
        }
        return;
    }
    case State::Angle: {
        double deg = 0.0;
        if (!t.empty() && !parse_number(t, deg)) {
            ctx.echo("Enter an angle in degrees.");
            return;
        }
        angle_ = core::to_radians(deg);
        mode_ = 3;
        state_ = State::Through;
        ctx.set_prompt("Specify through point: ");
        return;
    }
    case State::Through: {
        if (t.empty()) {
            done_ = true; // Enter ends the repeating family, as in AutoCAD
            return;
        }
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        core::Vec2 base = *p;
        core::Vec2 dir;
        if (mode_ == 1) {
            dir = {1.0, 0.0};
        } else if (mode_ == 2) {
            dir = {0.0, 1.0};
        } else if (mode_ == 3) {
            dir = {std::cos(angle_), std::sin(angle_)};
        } else {
            // Two-point / RAY: the line runs through the root toward this point.
            base = root_;
            dir = *p - root_;
        }
        emit(ctx, base, dir);
        // XLINE repeats through the SAME root; the Hor/Ver/Ang families repeat at new
        // points; RAY keeps its start. Enter (empty) ends it.
        if (ray_ || mode_ == 0) {
            ctx.set_last_point(root_);
        }
        ctx.set_prompt("Specify through point: ");
        return;
    }
    case State::BisectVertex:
        if (const auto p = read_point(ctx, text)) {
            bvertex_ = *p;
            ctx.set_last_point(*p);
            state_ = State::BisectStart;
            ctx.set_prompt("Specify angle start point: ");
        }
        return;
    case State::BisectStart:
        if (const auto p = read_point(ctx, text)) {
            bstart_ = *p;
            state_ = State::BisectEnd;
            ctx.set_prompt("Specify angle end point: ");
        }
        return;
    case State::BisectEnd:
        if (const auto p = read_point(ctx, text)) {
            const core::Vec2 d0 = core::normalized(bstart_ - bvertex_);
            const core::Vec2 d1 = core::normalized(*p - bvertex_);
            const core::Vec2 bis = d0 + d1; // the angle bisector direction
            emit(ctx, bvertex_, bis);
            done_ = true;
        }
        return;
    }
}

void XlineCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// REVCLOUD
// ---------------------------------------------------------------------------
void RevcloudCommand::main_prompt(CommandContext& ctx) {
    state_ = State::Main;
    path_.clear();
    ctx.clear_preview();
    ctx.set_prompt("Specify first point or [Arc length/Object/Rectangular/Polygonal/Freehand/Style] "
                   "<Object>: ");
}

void RevcloudCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "Minimum arc length: %.4f   Maximum arc length: %.4f   Style: Normal", min_arc_,
                  max_arc_);
    ctx.echo(buf);
    main_prompt(ctx);
}

void RevcloudCommand::emit_cloud(CommandContext& ctx, const std::vector<core::Vec2>& path,
                                 bool closed) {
    std::vector<core::Vec2> verts;
    std::vector<double> bulges;
    core::polyline_ops::revcloud_from_path(path, closed, arc_len(), false, verts, bulges);
    if (verts.size() < 2) {
        ctx.echo("Revision cloud: the arc length is too large for that shape.");
        done_ = true;
        return;
    }
    core::AddPolylineCommand pc;
    pc.points = std::move(verts);
    pc.bulges = std::move(bulges);
    pc.closed = closed;
    pc.group = ctx.group_id();
    ctx.clear_preview();
    ctx.submit(std::move(pc));
    ctx.echo("Revision cloud created.");
    done_ = true;
}

void RevcloudCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Main: {
        if (t.empty() || u == "O" || u == "OBJECT") {
            state_ = State::ObjectPick;
            ctx.set_prompt("Select object: ");
            return;
        }
        if (u == "A" || u == "ARC" || u == "ARC LENGTH") {
            state_ = State::ArcMin;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Specify minimum length of arc <%.4f>: ", min_arc_);
            ctx.set_prompt(buf);
            return;
        }
        if (u == "R" || u == "RECTANGULAR") {
            state_ = State::RectFirst;
            ctx.set_prompt("Specify first corner point: ");
            return;
        }
        if (u == "P" || u == "POLYGONAL" || u == "F" || u == "FREEHAND") {
            state_ = State::PathNext;
            path_.clear();
            ctx.set_prompt(u.front() == 'F' ? "Guide the path point by point (Enter closes): "
                                            : "Specify start point: ");
            return;
        }
        if (u == "S" || u == "STYLE") {
            state_ = State::Style;
            ctx.set_prompt("Select arc style [Normal/Calligraphy] <Normal>: ");
            return;
        }
        if (u == "M" || u == "MODIFY") {
            ctx.echo("Modify is not supported yet; draw a new cloud.");
            return;
        }
        // A point at the main prompt starts a path (AutoCAD's default Freehand type).
        if (const auto p = read_point(ctx, text)) {
            state_ = State::PathNext;
            path_ = {*p};
            ctx.set_last_point(*p);
            ctx.set_preview(PreviewSpec{PreviewKind::Polyline, path_});
            ctx.set_prompt("Specify next point (Enter closes): ");
        }
        return;
    }
    case State::ArcMin: {
        double v = min_arc_;
        if (!t.empty() && (!parse_number(t, v) || v <= 0.0)) {
            ctx.echo("Enter a length greater than zero.");
            return;
        }
        min_arc_ = v;
        state_ = State::ArcMax;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Specify maximum length of arc <%.4f>: ",
                      std::max(max_arc_, min_arc_));
        ctx.set_prompt(buf);
        return;
    }
    case State::ArcMax: {
        double v = std::max(max_arc_, min_arc_);
        if (!t.empty() && (!parse_number(t, v) || v < min_arc_)) {
            ctx.echo("The maximum must be at least the minimum length.");
            return;
        }
        max_arc_ = v;
        s_min_arc_ = min_arc_;
        s_max_arc_ = max_arc_;
        main_prompt(ctx);
        return;
    }
    case State::RectFirst:
        if (const auto p = read_point(ctx, text)) {
            first_ = *p;
            ctx.set_last_point(*p);
            state_ = State::RectSecond;
            ctx.set_preview(PreviewSpec{PreviewKind::Rectangle, {first_}});
            ctx.set_prompt("Specify opposite corner: ");
        }
        return;
    case State::RectSecond:
        if (const auto p = read_point(ctx, text)) {
            // Counter-clockwise corners, whatever quadrant the second pick is in.
            const core::Vec2 mn{std::min(first_.x, p->x), std::min(first_.y, p->y)};
            const core::Vec2 mx{std::max(first_.x, p->x), std::max(first_.y, p->y)};
            emit_cloud(ctx, {{mn.x, mn.y}, {mx.x, mn.y}, {mx.x, mx.y}, {mn.x, mx.y}}, true);
        }
        return;
    case State::PathNext: {
        if (t.empty()) {
            if (path_.size() < 3) {
                ctx.echo("A cloud needs at least three points.");
                return;
            }
            emit_cloud(ctx, path_, true);
            return;
        }
        if (u == "U" || u == "UNDO") {
            if (!path_.empty()) {
                path_.pop_back();
            }
            ctx.set_preview(PreviewSpec{PreviewKind::Polyline, path_});
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            path_.push_back(*p);
            ctx.set_last_point(*p);
            ctx.set_preview(PreviewSpec{PreviewKind::Polyline, path_});
            ctx.set_prompt("Specify next point or [Undo] (Enter closes): ");
        }
        return;
    }
    case State::ObjectPick:
        if (const auto p = read_point(ctx, text)) {
            core::RevcloudObjectCommand cmd;
            cmd.pick = *p;
            cmd.pick_radius = ctx.pick_radius();
            cmd.arc_len = arc_len();
            cmd.group = ctx.group_id();
            ctx.submit(cmd);
            state_ = State::ObjectReverse;
            ctx.set_prompt("Reverse direction [Yes/No] <No>: ");
        }
        return;
    case State::ObjectReverse:
        if (u == "Y" || u == "YES") {
            ctx.submit(core::RevcloudReverseCommand{ctx.group_id()});
        }
        done_ = true;
        return;
    case State::Style:
        if (u == "C" || u == "CALLIGRAPHY") {
            ctx.echo("Calligraphy style is not supported; Normal is used.");
        }
        main_prompt(ctx);
        return;
    }
}

void RevcloudCommand::cancel(CommandContext& ctx) {
    ctx.clear_preview();
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// EXPLODE
// ---------------------------------------------------------------------------
void ExplodeCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (ctx.has_selection()) {
        ctx.submit(core::ExplodeSelectionCommand{ctx.group_id()});
        done_ = true; // the engine reports what it broke and what it could not (Ph10.1)
        return;
    }
    ctx.set_prompt("Select objects: ");
}

void ExplodeCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (t.empty()) {
        if (!ctx.has_selection()) {
            ctx.echo("Nothing selected.");
            done_ = true;
            return;
        }
        ctx.submit(core::ExplodeSelectionCommand{ctx.group_id()});
        done_ = true;
        return;
    }
    if (upper(t) == "ALL") {
        ctx.submit(core::SelectAllCommand{});
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        ctx.submit(core::SelectPickCommand{*p, ctx.pick_radius(), true, true});
    }
}

void ExplodeCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// POLYGON: n sides, sized about a centre (inscribed/circumscribed) or by one edge
// ---------------------------------------------------------------------------
void PolygonCommand::refresh_preview(CommandContext& ctx) {
    PreviewSpec pv{PreviewKind::Polygon, {center_}};
    pv.sides = sides_;
    pv.inscribed = inscribed_;
    ctx.set_preview(pv);
}

void PolygonCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Sides;
    ctx.set_prompt("Enter number of sides <4>: ");
}

void PolygonCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    switch (state_) {
    case State::Sides: {
        if (!t.empty()) {
            const int n = parse_int(t, 0);
            if (n < 3) {
                ctx.echo("A polygon needs at least 3 sides.");
                return;
            }
            sides_ = n;
        }
        state_ = State::Center;
        ctx.set_prompt("Specify center of polygon or [Edge]: ");
        return;
    }
    case State::Center: {
        if (upper(t) == "E" || upper(t) == "EDGE") {
            state_ = State::Edge1;
            ctx.set_prompt("Specify first endpoint of edge: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            center_ = *p;
            ctx.set_last_point(*p);
            state_ = State::Fit;
            ctx.set_prompt("Enter an option [Inscribed in circle/Circumscribed about circle] <I>: ");
        }
        return;
    }
    case State::Fit: {
        const std::string u = upper(t);
        inscribed_ = !(u == "C" || u == "CIRCUMSCRIBED");
        state_ = State::Radius;
        refresh_preview(ctx);
        ctx.set_prompt("Specify radius of circle: ");
        return;
    }
    case State::Radius: {
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        // A typed bare number is a radius along +X; a picked point also fixes the
        // orientation, which is why the reference ANGLE comes from the pick.
        const core::Vec2 r = *p - center_;
        const double dist = core::length(r);
        if (dist <= 1e-12) {
            ctx.echo("The radius must be greater than zero.");
            return;
        }
        const std::vector<core::Vec2> v = core::polygon_vertices(
            center_, dist, sides_, inscribed_, std::atan2(r.y, r.x));
        ctx.clear_preview();
        core::AddPolylineCommand poly;
        poly.points = v;
        poly.closed = true;
        poly.group = ctx.group_id();
        ctx.submit(poly);
        ctx.echo("Polygon created.");
        done_ = true;
        return;
    }
    case State::Edge1:
        if (const auto p = read_point(ctx, text)) {
            edge1_ = *p;
            ctx.set_last_point(*p);
            state_ = State::Edge2;
            ctx.set_prompt("Specify second endpoint of edge: ");
        }
        return;
    case State::Edge2: {
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        const core::Vec2 e = *p - edge1_;
        const double side = core::length(e);
        if (side <= 1e-12) {
            ctx.echo("The two edge endpoints must differ.");
            return;
        }
        // Edge mode: the two picks ARE one side. The centre sits on the edge's
        // perpendicular bisector, an apothem away, on the left of edge1->edge2 -- the
        // side AutoCAD builds towards.
        const double n = static_cast<double>(sides_);
        const double apothem = side / (2.0 * std::tan(core::kPi / n));
        const core::Vec2 mid{(edge1_.x + p->x) * 0.5, (edge1_.y + p->y) * 0.5};
        const core::Vec2 dir{e.x / side, e.y / side};
        const core::Vec2 left{-dir.y, dir.x};
        const core::Vec2 c{mid.x + left.x * apothem, mid.y + left.y * apothem};
        // Generated by the same rule as centre mode: edge1 is a VERTEX, so this is the
        // inscribed case with the angle pointing at it.
        const core::Vec2 rad = edge1_ - c;
        const std::vector<core::Vec2> v = core::polygon_vertices(
            c, core::length(rad), sides_, true, std::atan2(rad.y, rad.x));
        ctx.clear_preview();
        core::AddPolylineCommand poly;
        poly.points = v;
        poly.closed = true;
        poly.group = ctx.group_id();
        ctx.submit(poly);
        ctx.echo("Polygon created.");
        done_ = true;
        return;
    }
    }
}

void PolygonCommand::cancel(CommandContext& ctx) {
    ctx.clear_preview();
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// POINT: place a point at each pick until Esc
// ---------------------------------------------------------------------------
void PointCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Specify a point: ");
}

void PointCommand::input(CommandContext& ctx, const std::string& text) {
    if (const auto p = read_point(ctx, text)) {
        ctx.submit(core::AddPointCommand{*p, ctx.group_id(), {}});
        ctx.set_last_point(*p);
        // Stay open for the next one, like AutoCAD: points come in groups.
        ctx.set_prompt("Specify a point: ");
    }
}

void PointCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// DIVIDE / MEASURE: mark a curve with points
// ---------------------------------------------------------------------------
void DivideCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Pick;
    ctx.set_prompt(measure_ ? "Select object to measure: " : "Select object to divide: ");
}

void DivideCommand::input(CommandContext& ctx, const std::string& text) {
    switch (state_) {
    case State::Pick:
        if (const auto p = read_point(ctx, text)) {
            pick_ = *p;
            state_ = State::Amount;
            ctx.set_prompt(measure_ ? "Specify length of segment: "
                                    : "Enter the number of segments: ");
        }
        return;
    case State::Amount: {
        const std::string t = trimmed(text);
        core::DividePathCommand cmd;
        cmd.pick = pick_;
        cmd.pick_radius = ctx.pick_radius();
        cmd.group = ctx.group_id();
        if (measure_) {
            double d = 0.0;
            if (!parse_number(t, d) || d <= 0.0) {
                ctx.echo("Enter a positive segment length.");
                return;
            }
            cmd.distance = d;
        } else {
            const int n = parse_int(t, 0);
            if (n < 2) {
                ctx.echo("Enter a number of segments of 2 or more.");
                return;
            }
            cmd.segments = n;
        }
        ctx.submit(cmd);
        // The engine reports how many marks it actually placed (Ph10.1).
        done_ = true;
        return;
    }
    }
}

void DivideCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void ArrayCommand::begin_rect(CommandContext& ctx) {
    state_ = State::Rows;
    ctx.set_prompt("Enter number of rows <1>: ");
}

void ArrayCommand::begin_polar(CommandContext& ctx) {
    state_ = State::Center;
    ctx.set_prompt("Specify center point of array: ");
}

void ArrayCommand::begin_path(CommandContext& ctx) {
    state_ = State::PathPick;
    ctx.set_prompt("Select path curve: ");
}

void ArrayCommand::start(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        ctx.echo("No selection. Select objects first, then run " + name() + ".");
        done_ = true;
        return;
    }
    ctx.clear_last_point();
    switch (type_) {
    case Type::Rect:
        begin_rect(ctx);
        return;
    case Type::Polar:
        begin_polar(ctx);
        return;
    case Type::Path:
        begin_path(ctx);
        return;
    case Type::Ask:
        break;
    }
    ctx.set_prompt("Enter array type [Rectangular/PAth/POlar] <R>: ");
}

void ArrayCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    switch (state_) {
    case State::Type: {
        // AutoCAD's own capitalisation picks the keywords apart: PA=path, PO=polar,
        // and a bare P is ambiguous, so it is rejected rather than guessed at.
        const std::string u = upper(t);
        if (u == "PA" || u == "PATH") {
            begin_path(ctx);
        } else if (u == "PO" || u == "POLAR") {
            begin_polar(ctx);
        } else if (u == "P") {
            ctx.echo("Ambiguous: enter PA for path or PO for polar.");
        } else {
            begin_rect(ctx);
        }
        return;
    }

    // --- Rectangular ------------------------------------------------------
    case State::Rows:
        rows_ = std::max(1, parse_int(t, 1));
        state_ = State::Cols;
        ctx.set_prompt("Enter number of columns <1>: ");
        return;
    case State::Cols:
        cols_ = std::max(1, parse_int(t, 1));
        state_ = State::RowSpace;
        ctx.set_prompt("Enter row spacing (Y): ");
        return;
    case State::RowSpace:
        if (!parse_number(t, row_space_)) {
            ctx.echo("Enter a number for row spacing.");
            return;
        }
        state_ = State::ColSpace;
        ctx.set_prompt("Enter column spacing (X): ");
        return;
    case State::ColSpace: {
        if (!parse_number(t, col_space_)) {
            ctx.echo("Enter a number for column spacing.");
            return;
        }
        // AutoCAD's legacy -ARRAY flow ends here; only the modern ARRAYRECT offers an
        // axis angle. Keeping the classic four prompts exactly as they were means
        // existing muscle memory and scripts are untouched.
        if (type_ != Type::Rect) {
            core::ArrayRectCommand cmd;
            cmd.rows = rows_;
            cmd.cols = cols_;
            cmd.dx = col_space_;
            cmd.dy = row_space_;
            cmd.group = ctx.group_id();
            ctx.submit(cmd);
            done_ = true;
            return;
        }
        state_ = State::Angle;
        ctx.set_prompt("Angle of array axes <0>: ");
        return;
    }
    case State::Angle: {
        double deg = 0.0;
        if (!t.empty() && !parse_number(t, deg)) {
            ctx.echo("Enter an angle in degrees.");
            return;
        }
        core::ArrayRectCommand cmd;
        cmd.rows = rows_;
        cmd.cols = cols_;
        cmd.dx = col_space_;
        cmd.dy = row_space_;
        cmd.angle = core::to_radians(deg);
        cmd.group = ctx.group_id();
        ctx.submit(cmd);
        // The engine reports how many copies it actually made (Ph10.1).
        done_ = true;
        return;
    }

    // --- Polar ------------------------------------------------------------
    case State::Center:
        if (const auto p = read_point(ctx, text)) {
            center_ = *p;
            state_ = State::Count;
            ctx.set_prompt("Enter number of items: ");
        }
        return;
    case State::Count:
        count_ = std::max(1, parse_int(t, 1));
        state_ = State::Fill;
        ctx.set_prompt("Specify angle to fill in degrees <360>: ");
        return;
    case State::Fill: {
        double deg = 360.0;
        if (!t.empty() && !parse_number(t, deg)) {
            deg = 360.0;
        }
        fill_ = core::to_radians(deg);
        state_ = State::RotateItems;
        ctx.set_prompt("Rotate items as copied? [Yes/No] <Yes>: ");
        return;
    }
    case State::RotateItems: {
        const std::string u = upper(t);
        const bool rotate = !(u == "N" || u == "NO");
        ctx.submit(core::ArrayPolarCommand{center_, count_, fill_, rotate, ctx.group_id()});
        done_ = true;
        return;
    }

    // --- Path -------------------------------------------------------------
    case State::PathPick:
        if (const auto p = read_point(ctx, text)) {
            path_pick_ = *p;
            state_ = State::PathMethod;
            ctx.set_prompt("Method [Divide/Measure] <D>: ");
        }
        return;
    case State::PathMethod: {
        const std::string u = upper(t);
        if (u == "M" || u == "MEASURE") {
            state_ = State::PathSpacing;
            ctx.set_prompt("Distance between items: ");
        } else {
            state_ = State::PathCount;
            ctx.set_prompt("Number of items to distribute along the path: ");
        }
        return;
    }
    case State::PathCount:
        count_ = std::max(2, parse_int(t, 2));
        path_spacing_ = 0.0; // Divide
        state_ = State::PathAlign;
        ctx.set_prompt("Align items with the path? [Yes/No] <Yes>: ");
        return;
    case State::PathSpacing:
        if (!parse_number(t, path_spacing_) || path_spacing_ <= 0.0) {
            ctx.echo("Enter a positive distance between items.");
            return;
        }
        count_ = 0; // Measure: as many as fit
        state_ = State::PathAlign;
        ctx.set_prompt("Align items with the path? [Yes/No] <Yes>: ");
        return;
    case State::PathAlign: {
        const std::string u = upper(t);
        core::ArrayPathCommand cmd;
        cmd.pick = path_pick_;
        cmd.pick_radius = ctx.pick_radius();
        cmd.count = count_;
        cmd.spacing = path_spacing_;
        cmd.align = !(u == "N" || u == "NO");
        cmd.group = ctx.group_id();
        ctx.submit(cmd);
        done_ = true;
        return;
    }
    }
}

void ArrayCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}


// ---------------------------------------------------------------------------
// EXTEND (pick the object to extend; repeats)
// ---------------------------------------------------------------------------
void ExtendCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Select object to extend: ");
}

void ExtendCommand::input(CommandContext& ctx, const std::string& text) {
    if (trimmed(text).empty()) {
        done_ = true;
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        ctx.submit(core::ExtendPickCommand{*p, ctx.pick_radius(), ctx.group_id()});
        // Result is echoed by the engine (honest status), not assumed here.
    }
}

void ExtendCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// FILLET (radius, then two lines)
// ---------------------------------------------------------------------------
void FilletCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Specify fillet radius <0>: ");
}

void FilletCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (state_ == State::Radius) {
        double r = 0.0;
        if (!t.empty() && parse_number(t, r)) {
            radius_ = std::max(0.0, r);
        }
        state_ = State::First;
        ctx.set_prompt("Select first line: ");
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    if (state_ == State::First) {
        pick1_ = *p;
        state_ = State::Second;
        ctx.set_prompt("Select second line: ");
    } else {
        ctx.submit(core::FilletPickCommand{pick1_, *p, radius_, ctx.pick_radius(), ctx.group_id()});
        // Result is echoed by the engine (honest status), not assumed here.
        done_ = true;
    }
}

void FilletCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// CHAMFER (Distance method, or Angle method defaulting to 45 degrees)
// ---------------------------------------------------------------------------
void ChamferCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Specify first chamfer distance or [Angle] <0>: ");
}

void ChamferCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    switch (state_) {
    case State::Dist1:
        if (upper(t) == "A" || upper(t) == "ANGLE") {
            state_ = State::AngleLen;
            ctx.set_prompt("Specify chamfer length on the first line <0>: ");
            return;
        }
        {
            double d = 0.0;
            if (!t.empty() && parse_number(t, d)) {
                dist1_ = std::max(0.0, d);
            }
        }
        state_ = State::Dist2;
        ctx.set_prompt("Specify second chamfer distance <" + std::to_string(dist1_) + ">: ");
        return;
    case State::Dist2: {
        double d = dist1_;
        if (!t.empty() && !parse_number(t, d)) {
            d = dist1_;
        }
        dist2_ = std::max(0.0, d);
        state_ = State::First;
        ctx.set_prompt("Select first line: ");
        return;
    }
    case State::AngleLen:
        if (!t.empty() && !parse_number(t, length_)) {
            length_ = 0.0;
        }
        length_ = std::max(0.0, length_);
        state_ = State::AngleVal;
        ctx.set_prompt("Specify chamfer angle from the first line <45>: ");
        return;
    case State::AngleVal: {
        double deg = 45.0;
        if (!t.empty() && !parse_number(t, deg)) {
            deg = 45.0;
        }
        // Distance on line 1 is the length; on line 2 it is length * tan(angle).
        dist1_ = length_;
        dist2_ = length_ * std::tan(core::to_radians(deg));
        state_ = State::First;
        ctx.set_prompt("Select first line: ");
        return;
    }
    case State::First:
        if (const auto p = read_point(ctx, text)) {
            pick1_ = *p;
            state_ = State::Second;
            ctx.set_prompt("Select second line: ");
        }
        return;
    case State::Second:
        if (const auto p = read_point(ctx, text)) {
            ctx.submit(core::ChamferPickCommand{pick1_, *p, dist1_, dist2_, ctx.pick_radius(),
                                                ctx.group_id()});
            // Result is echoed by the engine (honest status), not assumed here.
            done_ = true;
        }
        return;
    }
}

void ChamferCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// TEXT (single-line): point -> height -> rotation -> content
// ---------------------------------------------------------------------------
void TextCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Specify start point: ");
}

void TextCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    switch (state_) {
    case State::Point:
        if (const auto p = read_point(ctx, text)) {
            pos_ = *p;
            ctx.set_last_point(*p);
            // The current text STYLE: a fixed height skips the height prompt (AutoCAD).
            const std::vector<core::TextStyle> styles = ctx.text_styles();
            const std::uint16_t cur = ctx.current_text_style();
            style_.clear();
            fixed_height_ = false;
            if (cur > 0 && cur < styles.size()) {
                style_ = styles[cur].name;
                if (styles[cur].height > 0.0) {
                    height_ = styles[cur].height;
                    fixed_height_ = true;
                }
            }
            if (fixed_height_) {
                state_ = State::Rotation;
                ctx.set_prompt("Specify rotation angle <0>: ");
            } else {
                state_ = State::Height;
                ctx.set_prompt("Specify text height <2.5>: ");
            }
        }
        return;
    case State::Height:
        if (!t.empty()) {
            double h = 2.5;
            if (parse_number(t, h) && h > 0.0) {
                height_ = h;
            }
        }
        state_ = State::Rotation;
        ctx.set_prompt("Specify rotation angle <0>: ");
        return;
    case State::Rotation: {
        double deg = 0.0;
        if (!t.empty() && parse_number(t, deg)) {
            rotation_ = core::to_radians(deg);
        }
        state_ = State::Content;
        ctx.set_prompt("Enter text: ");
        return;
    }
    case State::Content: {
        core::AddTextCommand cmd{pos_, height_, rotation_, 0, text, ctx.group_id()};
        cmd.style = style_; // the current text style (its font applies)
        ctx.submit(std::move(cmd));
        ctx.echo("Text placed.");
        done_ = true;
        return;
    }
    }
}

void TextCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

namespace {
bool is_object_keyword(const std::string& text) {
    std::string u;
    for (const char c : text) {
        u += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return u == "O" || u == "OBJECT";
}
const char* dim_type_word(core::DimType t) {
    switch (t) {
    case core::DimType::Radius:
        return "Radius";
    case core::DimType::Diameter:
        return "Diameter";
    case core::DimType::Aligned:
        return "Aligned";
    case core::DimType::Angular:
        return "Angular";
    case core::DimType::Ordinate:
        return "Ordinate";
    case core::DimType::Jogged:
        return "Jogged";
    case core::DimType::ArcLength:
        return "Arc length";
    case core::DimType::Linear:
        break;
    }
    return "Linear";
}
} // namespace

namespace {
// Rubber-band the full dimension at the cursor (Phase 16 Part C). Two-point dims
// pass their def points (a, b); object dims pass none and the UI uses the snapshot's
// resolved pending_dim_* (set by ResolveDimObjectCommand at the object pick).
void preview_two_point_dim(CommandContext& ctx, core::DimType t, core::Vec2 a, core::Vec2 b) {
    PreviewSpec s;
    s.kind = PreviewKind::Dimension;
    s.dim_type = static_cast<int>(t);
    s.points = {a, b};
    ctx.set_preview(std::move(s));
}
void preview_object_dim(CommandContext& ctx, core::DimType t) {
    PreviewSpec s;
    s.kind = PreviewKind::Dimension;
    s.dim_type = static_cast<int>(t);
    ctx.set_preview(std::move(s)); // def points come from the snapshot pending_dim
}
} // namespace

// ---------------------------------------------------------------------------
// DIMLINEAR / DIMALIGNED: two-point flow, or [Object] -> select a line/segment.
// ---------------------------------------------------------------------------
void LinearDimensionCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Specify first extension line origin or [Object]: ");
}

void LinearDimensionCommand::input(CommandContext& ctx, const std::string& text) {
    // Object mode is entered from the first prompt via the [Object] keyword.
    if (state_ == State::First && is_object_keyword(text)) {
        state_ = State::SelectObj;
        ctx.set_prompt("Select line or polyline segment: ");
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    switch (state_) {
    case State::First:
        a_ = *p;
        ctx.set_last_point(*p);
        state_ = State::Second;
        ctx.set_prompt("Specify second extension line origin: ");
        return;
    case State::Second:
        b_ = *p;
        ctx.set_last_point(*p);
        state_ = State::Place;
        preview_two_point_dim(ctx, type_, a_, b_); // rubber-band to the cursor
        ctx.set_prompt("Specify dimension line location: ");
        return;
    case State::Place:
        ctx.submit(core::AddDimensionCommand{.type = static_cast<std::uint8_t>(type_),
                                             .a = a_,
                                             .b = b_,
                                             .line_pt = *p,
                                             .style = 0,
                                             .group = ctx.group_id()});
        ctx.echo("Dimension placed.");
        done_ = true;
        return;
    case State::SelectObj:
        obj_pick_ = *p;
        ctx.set_last_point(*p);
        state_ = State::ObjPlace;
        // Resolve the selected segment's def points once for the placement preview.
        ctx.submit(core::ResolveDimObjectCommand{static_cast<std::uint8_t>(type_), obj_pick_,
                                                 obj_pick_, ctx.pick_radius()});
        preview_object_dim(ctx, type_);
        ctx.set_prompt("Specify dimension line location: ");
        return;
    case State::ObjPlace:
        ctx.submit(core::AddObjectDimensionCommand{static_cast<std::uint8_t>(type_), obj_pick_, *p,
                                                   ctx.pick_radius(), 0, ctx.group_id()});
        ctx.echo("Dimension placed from object.");
        done_ = true;
        return;
    }
}

void LinearDimensionCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// DIMRADIUS / DIMDIAMETER: select the circle/arc -> place. The value comes from
// the entity's own geometry (resolved on the geometry thread).
// ---------------------------------------------------------------------------
void RadialDimensionCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select circle or arc: ");
}

void RadialDimensionCommand::input(CommandContext& ctx, const std::string& text) {
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    if (state_ == State::Select) {
        obj_pick_ = *p;
        ctx.set_last_point(*p);
        state_ = State::Place;
        // Resolve centre+radius once so the preview can rubber-band to the cursor.
        ctx.submit(core::ResolveDimObjectCommand{static_cast<std::uint8_t>(type_), obj_pick_,
                                                 obj_pick_, ctx.pick_radius()});
        preview_object_dim(ctx, type_);
        ctx.set_prompt("Specify dimension line location: ");
        return;
    }
    ctx.submit(core::AddObjectDimensionCommand{static_cast<std::uint8_t>(type_), obj_pick_, *p,
                                               ctx.pick_radius(), 0, ctx.group_id()});
    ctx.echo("Dimension placed.");
    done_ = true;
}

void RadialDimensionCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// DIMORDINATE
// ---------------------------------------------------------------------------
void OrdinateDimensionCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Feature;
    forced_ = -1;
    ctx.set_prompt("Specify feature location: ");
}

void OrdinateDimensionCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    if (state_ == State::Feature) {
        if (const auto p = read_point(ctx, text)) {
            feature_ = *p;
            ctx.set_last_point(*p);
            state_ = State::End;
            ctx.set_preview({PreviewKind::Segment, {*p}});
            ctx.set_prompt("Specify leader endpoint or [Xdatum/Ydatum/Mtext/Text/Angle]: ");
        }
        return;
    }
    if (u == "X" || u == "XDATUM") {
        forced_ = 0;
        ctx.set_prompt("Specify leader endpoint or [Xdatum/Ydatum/Mtext/Text/Angle]: ");
        return;
    }
    if (u == "Y" || u == "YDATUM") {
        forced_ = 1;
        ctx.set_prompt("Specify leader endpoint or [Xdatum/Ydatum/Mtext/Text/Angle]: ");
        return;
    }
    if (u == "M" || u == "MTEXT" || u == "T" || u == "TEXT" || u == "A" || u == "ANGLE") {
        ctx.echo("That option is not supported yet; the measured value is used.");
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        // AutoCAD's automatic choice: a mostly vertical leader measures X.
        const core::Vec2 d = *p - feature_;
        const double aux = forced_ >= 0 ? static_cast<double>(forced_)
                                        : (std::abs(d.y) >= std::abs(d.x) ? 0.0 : 1.0);
        core::AddDimensionCommand dim;
        dim.type = static_cast<std::uint8_t>(core::DimType::Ordinate);
        dim.a = feature_;
        dim.b = *p;
        dim.line_pt = *p;
        dim.group = ctx.group_id();
        dim.aux = aux;
        ctx.submit(std::move(dim));
        ctx.set_preview({});
        ctx.echo("Dimension placed.");
        done_ = true;
    }
}

void OrdinateDimensionCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.set_preview({});
    done_ = true;
}

// ---------------------------------------------------------------------------
// DIMJOGGED
// ---------------------------------------------------------------------------
void JoggedDimensionCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Select;
    ctx.set_prompt("Select arc or circle: ");
}

void JoggedDimensionCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    if (state_ == State::Place && (u == "M" || u == "MTEXT" || u == "T" || u == "TEXT" ||
                                   u == "A" || u == "ANGLE")) {
        ctx.echo("That option is not supported yet; the measured value is used.");
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    switch (state_) {
    case State::Select:
        obj_pick_ = *p;
        ctx.set_last_point(*p);
        state_ = State::Override;
        ctx.submit(core::ResolveDimObjectCommand{static_cast<std::uint8_t>(core::DimType::Jogged),
                                                 obj_pick_, obj_pick_, ctx.pick_radius()});
        ctx.set_prompt("Specify center location override: ");
        return;
    case State::Override:
        override_ = *p;
        ctx.set_last_point(*p);
        state_ = State::Place;
        ctx.set_preview({PreviewKind::Segment, {*p}});
        ctx.set_prompt("Specify dimension line location or [Mtext/Text/Angle]: ");
        return;
    case State::Place:
        place_ = *p;
        state_ = State::Jog;
        ctx.set_prompt("Specify jog location: ");
        return;
    case State::Jog:
        ctx.submit(core::AddObjectDimensionCommand{static_cast<std::uint8_t>(core::DimType::Jogged),
                                                   obj_pick_, place_, ctx.pick_radius(), 0,
                                                   ctx.group_id(), override_, *p});
        ctx.set_preview({});
        ctx.echo("Dimension placed.");
        done_ = true;
        return;
    }
}

void JoggedDimensionCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.set_preview({});
    done_ = true;
}

// ---------------------------------------------------------------------------
// DIMARC
// ---------------------------------------------------------------------------
void ArcLengthDimensionCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Select;
    ctx.set_prompt("Select arc or polyline arc segment: ");
}

void ArcLengthDimensionCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    if (state_ == State::Place && (u == "M" || u == "MTEXT" || u == "T" || u == "TEXT" ||
                                   u == "A" || u == "ANGLE" || u == "P" || u == "PARTIAL" ||
                                   u == "L" || u == "LEADER")) {
        ctx.echo("That option is not supported yet; the whole arc's length is dimensioned.");
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    if (state_ == State::Select) {
        obj_pick_ = *p;
        ctx.set_last_point(*p);
        state_ = State::Place;
        ctx.submit(core::ResolveDimObjectCommand{static_cast<std::uint8_t>(core::DimType::ArcLength),
                                                 obj_pick_, obj_pick_, ctx.pick_radius()});
        preview_object_dim(ctx, core::DimType::ArcLength);
        ctx.set_prompt("Specify arc length dimension location, or [Mtext/Text/Angle/Partial/Leader]: ");
        return;
    }
    ctx.submit(core::AddObjectDimensionCommand{static_cast<std::uint8_t>(core::DimType::ArcLength),
                                               obj_pick_, *p, ctx.pick_radius(), 0, ctx.group_id()});
    ctx.set_preview({});
    ctx.echo("Dimension placed.");
    done_ = true;
}

void ArcLengthDimensionCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.set_preview({});
    done_ = true;
}

// ---------------------------------------------------------------------------
// DIMANGULAR: select two lines/edges; the angle is read from their directions.
// ---------------------------------------------------------------------------
void AngularDimensionCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select first line: ");
}

void AngularDimensionCommand::input(CommandContext& ctx, const std::string& text) {
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    switch (state_) {
    case State::Line1:
        pick1_ = *p;
        ctx.set_last_point(*p);
        state_ = State::Line2;
        ctx.set_prompt("Select second line: ");
        return;
    case State::Line2:
        pick2_ = *p;
        state_ = State::Place;
        // The angle is fully determined by the two lines; resolve it so the preview
        // shows the full dimension. (The arc position has no free placement DOF, so
        // the preview is shown for confirmation rather than cursor-tracking.)
        ctx.submit(core::ResolveDimObjectCommand{static_cast<std::uint8_t>(core::DimType::Angular),
                                                 pick1_, pick2_, ctx.pick_radius()});
        preview_object_dim(ctx, core::DimType::Angular);
        ctx.set_prompt("Specify dimension arc location (or click to place): ");
        return;
    case State::Place:
        ctx.submit(core::AddObjectDimensionCommand{
            static_cast<std::uint8_t>(core::DimType::Angular), pick1_, pick2_, ctx.pick_radius(), 0,
            ctx.group_id()});
        ctx.echo("Angular dimension placed.");
        done_ = true;
        return;
    }
}

void AngularDimensionCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// DIM: smart all-in-one. Hover previews the type; on pick it reads the hovered
// entity kind and dispatches to the shared object-aware machinery.
// ---------------------------------------------------------------------------
namespace {
core::DimType dim_type_for(core::EntityKind k) {
    switch (k) {
    case core::EntityKind::Circle:
        return core::DimType::Diameter;
    case core::EntityKind::Arc:
        return core::DimType::Radius;
    default:
        return core::DimType::Linear; // Line / Polyline
    }
}
bool dimensionable(core::EntityKind k) {
    return k == core::EntityKind::Line || k == core::EntityKind::Polyline ||
           k == core::EntityKind::Circle || k == core::EntityKind::Arc;
}
} // namespace

void DimCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select objects to dimension: ");
}

void DimCommand::hover(CommandContext& ctx, std::optional<core::EntityKind> kind) {
    if (state_ != State::Select) {
        return;
    }
    if (kind && dimensionable(*kind)) {
        ctx.set_prompt(std::string("Select objects to dimension: -> ") +
                       dim_type_word(dim_type_for(*kind)));
    } else {
        ctx.set_prompt("Select objects to dimension: ");
    }
}

void DimCommand::input(CommandContext& ctx, const std::string& text) {
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    if (state_ == State::Select) {
        const auto kind = ctx.hovered_kind();
        if (!kind || !dimensionable(*kind)) {
            ctx.echo("No dimensionable object under the cursor -- hover a line, circle, or arc.");
            return; // stay in Select; let the user try again
        }
        type_ = dim_type_for(*kind);
        obj_pick_ = *p;
        ctx.set_last_point(*p);
        state_ = State::Place;
        // Resolve def points once so the chosen dimension rubber-bands to the cursor.
        ctx.submit(core::ResolveDimObjectCommand{static_cast<std::uint8_t>(type_), obj_pick_,
                                                 obj_pick_, ctx.pick_radius()});
        preview_object_dim(ctx, type_);
        ctx.set_prompt(std::string("Specify dimension line location (") + dim_type_word(type_) +
                       "): ");
        return;
    }
    ctx.submit(core::AddObjectDimensionCommand{static_cast<std::uint8_t>(type_), obj_pick_, *p,
                                               ctx.pick_radius(), 0, ctx.group_id()});
    ctx.echo(std::string(dim_type_word(type_)) + " dimension placed.");
    done_ = true;
}

void DimCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// DIMCONTINUE / DIMBASELINE (issue #28)
// ---------------------------------------------------------------------------
void ChainDimCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt(baseline_ ? "Specify second extension line origin (baseline), or Enter: "
                             : "Specify second extension line origin (continue), or Enter: ");
}

void ChainDimCommand::input(CommandContext& ctx, const std::string& text) {
    if (trimmed(text).empty()) {
        done_ = true; // Enter ends the chain, as in AutoCAD
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        // A FRESH undo group per pick, so each chained dimension undoes on its own --
        // the MATCHPROP target-loop convention.
        ctx.submit(core::ChainDimensionCommand{*p, baseline_, ctx.new_group()});
        ctx.set_last_point(*p);
        // The engine reports success or the honest reason it could not, so the command
        // does not echo a guess. Keep prompting for the next one.
        ctx.set_prompt(baseline_ ? "Specify second extension line origin (baseline), or Enter: "
                                 : "Specify second extension line origin (continue), or Enter: ");
    }
}

void ChainDimCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// Inquiry: DIST / ID / AREA / LIST (issue #30)
// ---------------------------------------------------------------------------
namespace {
/// Compact number formatting shared by the inquiry echoes.
std::string inum(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4g", v);
    return std::string(buf);
}
} // namespace

void DistCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    have_first_ = false;
    ctx.set_prompt("Specify first point: ");
}

void DistCommand::input(CommandContext& ctx, const std::string& text) {
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    if (!have_first_) {
        first_ = *p;
        have_first_ = true;
        ctx.set_last_point(*p);
        ctx.set_prompt("Specify second point: ");
        return;
    }
    // Answered from the picked points alone -- no store access, so no round trip.
    // In the drawing's display units (UNITS), as AutoCAD reports them.
    const core::Vec2 d = *p - first_;
    const core::DrawingUnits u = ctx.units();
    ctx.echo("Distance = " + core::units::format_length(core::length(d), u) +
             ",  Angle in XY Plane = " + core::units::format_angle(std::atan2(d.y, d.x), u) +
             ",  Delta X = " + core::units::format_length(d.x, u) +
             ",  Delta Y = " + core::units::format_length(d.y, u));
    done_ = true;
}

void DistCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void IdCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Specify point: ");
}

void IdCommand::input(CommandContext& ctx, const std::string& text) {
    if (const auto p = read_point(ctx, text)) {
        const core::DrawingUnits u = ctx.units();
        ctx.echo("X = " + core::units::format_length(p->x, u) + ",  Y = " +
                 core::units::format_length(p->y, u));
        done_ = true;
    }
}

void IdCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void AreaCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select an object to measure: ");
}

void AreaCommand::input(CommandContext& ctx, const std::string& text) {
    if (const auto p = read_point(ctx, text)) {
        // The engine resolves the entity and reports -- the UI never reads the store.
        ctx.submit(core::AreaQueryCommand{*p, ctx.pick_radius()});
        done_ = true;
    }
}

void AreaCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void ListCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select an object to list: ");
}

void ListCommand::input(CommandContext& ctx, const std::string& text) {
    if (const auto p = read_point(ctx, text)) {
        ctx.submit(core::ListQueryCommand{*p, ctx.pick_radius()});
        done_ = true;
    }
}

void ListCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// STRETCH: crossing window -> base point -> displacement
// ---------------------------------------------------------------------------
void StretchCommand::begin_base(CommandContext& ctx) {
    mode_ = Mode::Base;
    ctx.set_prompt("Specify base point or [Displacement] <Displacement>: ");
}

void StretchCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (ctx.has_selection()) {
        // Noun-verb (AutoCAD PICKFIRST): the selection, and the crossing window that
        // made it, are already on record in the engine.
        begin_base(ctx);
        return;
    }
    mode_ = Mode::Select;
    ctx.echo("Select objects to stretch by crossing-window or crossing-polygon...");
    ctx.set_prompt("Select objects: ");
}

void StretchCommand::finish(CommandContext& ctx, core::Vec2 delta) {
    ctx.clear_preview();
    ctx.submit(core::StretchSelectionCommand{delta, ctx.group_id()});
    // The engine reports what it actually did -- how many objects, or why none (Ph10.1).
    done_ = true;
}

void StretchCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    switch (mode_) {
    case Mode::Select: {
        // The viewport does the selecting (drags and picks accumulate, announced as
        // "N found"); this prompt only has to know when the user is done.
        if (t.empty()) {
            if (!ctx.has_selection()) {
                ctx.echo("Nothing selected.");
                done_ = true;
                return;
            }
            begin_base(ctx);
            return;
        }
        if (upper(t) == "ALL") {
            ctx.submit(core::SelectAllCommand{});
            return; // still selecting, as in AutoCAD: Enter finishes
        }
        if (const auto p = read_point(ctx, text)) {
            // A typed coordinate at "Select objects:" is a pick at that point.
            ctx.submit(core::SelectPickCommand{*p, ctx.pick_radius(), true, true});
            return;
        }
        ctx.echo("Pick objects, drag a crossing window, type ALL, or press Enter to finish.");
        return;
    }
    case Mode::Base: {
        const std::string u = upper(t);
        if (t.empty() || u == "D" || u == "DISPLACEMENT") {
            mode_ = Mode::Displacement;
            ctx.set_prompt("Specify displacement <0.0000, 0.0000>: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            base_ = *p;
            ctx.set_last_point(*p); // ortho/polar for the second point run from here
            mode_ = Mode::Second;
            // A rubber line from the base point, and the selection stretched live under
            // the cursor (the engine previews it on a scratch store).
            PreviewSpec pv{PreviewKind::Segment, {base_}};
            pv.live_stretch = true;
            ctx.set_preview(pv);
            ctx.set_prompt("Specify second point or <use first point as displacement>: ");
        }
        return;
    }
    case Mode::Displacement: {
        if (t.empty()) {
            ctx.echo("Zero displacement: nothing to stretch.");
            done_ = true;
            return;
        }
        // The value IS the displacement vector, typed as x,y (or a picked point taken
        // as a vector from the origin), as in AutoCAD.
        if (const auto p = read_point(ctx, text)) {
            finish(ctx, *p);
        }
        return;
    }
    case Mode::Second: {
        if (t.empty()) {
            // AutoCAD: the first point's coordinates are the displacement.
            finish(ctx, base_);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            finish(ctx, *p - base_);
        }
        return;
    }
    }
}

void StretchCommand::cancel(CommandContext& ctx) {
    ctx.clear_preview();
    ctx.submit(core::StretchPreviewCommand{{}, false}); // drop the rubber band
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// TABLE: rows -> columns -> column width -> row height -> placement
// ---------------------------------------------------------------------------
void TableCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    mode_ = Mode::Rows;
    ctx.set_prompt("Number of rows <4>: ");
}

void TableCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const auto number = [&](double& into, double min_value) {
        if (t.empty()) {
            return; // Enter keeps the default
        }
        try {
            const double v = std::stod(t);
            if (v >= min_value) {
                into = v;
            }
        } catch (...) {
        }
    };
    switch (mode_) {
    case Mode::Rows: {
        double v = rows_;
        number(v, 1.0);
        rows_ = static_cast<int>(v);
        mode_ = Mode::Cols;
        ctx.set_prompt("Number of columns <3>: ");
        return;
    }
    case Mode::Cols: {
        double v = cols_;
        number(v, 1.0);
        cols_ = static_cast<int>(v);
        mode_ = Mode::ColWidth;
        ctx.set_prompt("Column width <40>: ");
        return;
    }
    case Mode::ColWidth:
        number(col_w_, 1e-6);
        mode_ = Mode::RowHeight;
        ctx.set_prompt("Row height <8>: ");
        return;
    case Mode::RowHeight:
        number(row_h_, 1e-6);
        mode_ = Mode::Place;
        ctx.set_prompt("Specify insertion point (top-left corner): ");
        return;
    case Mode::Place:
        if (const auto p = read_point(ctx, text)) {
            core::AddTableCommand cmd;
            cmd.rows = static_cast<std::uint16_t>(rows_);
            cmd.cols = static_cast<std::uint16_t>(cols_);
            cmd.col_widths.assign(static_cast<std::size_t>(cols_), col_w_);
            cmd.row_heights.assign(static_cast<std::size_t>(rows_), row_h_);
            const std::size_t n = static_cast<std::size_t>(rows_) * cols_;
            cmd.cells.assign(n, core::TableCell{});
            cmd.texts.assign(n, std::string{});
            cmd.pos = *p;
            cmd.group = ctx.group_id();
            ctx.submit(std::move(cmd));
            ctx.echo("Table placed. Double-click a cell to edit it.");
            done_ = true;
        }
        return;
    }
}

void TableCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// TOLERANCE / TOL: cell list -> placement (GD&T feature control frame)
// ---------------------------------------------------------------------------
void ToleranceCommand::prompt_cell(CommandContext& ctx) {
    if (cells_.empty()) {
        // The characteristic. The symbols live in the stroke font (issue #9), so the
        // author types the escape and gets the glyph -- no GD&T-specific input mode.
        ctx.set_prompt("Characteristic (e.g. \\U+2316 position, \\U+27C2 perpendicularity): ");
    } else if (cells_.size() == 1) {
        ctx.set_prompt("Tolerance (e.g. %%c0.05 \\U+24C2), or Enter to finish: ");
    } else {
        ctx.set_prompt("Datum reference (e.g. A), or Enter to finish: ");
    }
}

void ToleranceCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    cells_.clear();
    mode_ = Mode::Cells;
    prompt_cell(ctx);
}

void ToleranceCommand::input(CommandContext& ctx, const std::string& text) {
    if (mode_ == Mode::Cells) {
        const std::string t = trimmed(text);
        if (t.empty()) {
            if (cells_.empty()) {
                ctx.echo("A feature control frame needs at least a characteristic symbol.");
                prompt_cell(ctx);
                return;
            }
            mode_ = Mode::Place;
            ctx.set_prompt("Specify frame location: ");
            return;
        }
        if (cells_.size() >= 5) { // characteristic + tolerance + up to three datums
            ctx.echo("A feature control frame carries at most five cells.");
            mode_ = Mode::Place;
            ctx.set_prompt("Specify frame location: ");
            return;
        }
        cells_.push_back(t); // stored RAW; codes expand at layout time
        prompt_cell(ctx);
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        core::AddFcfCommand cmd;
        cmd.cells = cells_;
        cmd.pos = *p;
        cmd.group = ctx.group_id();
        ctx.submit(std::move(cmd));
        ctx.echo("Feature control frame placed.");
        done_ = true;
    }
}

void ToleranceCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// DATUM: letter -> point on the feature -> box placement
// ---------------------------------------------------------------------------
void DatumCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    mode_ = Mode::Letter;
    ctx.set_prompt("Datum identifier <A>: ");
}

void DatumCommand::input(CommandContext& ctx, const std::string& text) {
    switch (mode_) {
    case Mode::Letter: {
        const std::string t = trimmed(text);
        if (!t.empty()) {
            letter_ = t;
        }
        mode_ = Mode::Tip;
        ctx.set_prompt("Specify point on the feature: ");
        return;
    }
    case Mode::Tip:
        if (const auto p = read_point(ctx, text)) {
            tip_ = *p;
            ctx.set_last_point(*p);
            mode_ = Mode::Place;
            ctx.set_prompt("Specify datum symbol location: ");
        }
        return;
    case Mode::Place:
        if (const auto p = read_point(ctx, text)) {
            core::AddDatumCommand cmd;
            cmd.letter = letter_;
            cmd.tip = tip_;
            cmd.pos = *p;
            cmd.group = ctx.group_id();
            ctx.submit(std::move(cmd));
            ctx.echo("Datum feature symbol placed.");
            done_ = true;
        }
        return;
    }
}

void DatumCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// LEADER: arrow tip -> landing point -> text
// ---------------------------------------------------------------------------
void LeaderCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Specify leader arrow point: ");
}

void LeaderCommand::input(CommandContext& ctx, const std::string& text) {
    switch (state_) {
    case State::Tip:
        if (const auto p = read_point(ctx, text)) {
            tip_ = *p;
            ctx.set_last_point(*p);
            state_ = State::Knee;
            ctx.set_prompt("Specify landing point: ");
        }
        return;
    case State::Knee:
        if (const auto p = read_point(ctx, text)) {
            knee_ = *p;
            ctx.set_last_point(*p);
            state_ = State::Content;
            ctx.set_prompt("Enter leader text: ");
        }
        return;
    case State::Content:
        ctx.submit(core::AddLeaderCommand{tip_, knee_, 2.5, 0, text, ctx.group_id()});
        ctx.echo("Leader placed.");
        done_ = true;
        return;
    }
}

void LeaderCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// MTEXT: two corners (insertion + wrap width) -> paragraph text.
// ---------------------------------------------------------------------------
void MTextCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Specify first corner: ");
}

void MTextCommand::input(CommandContext& ctx, const std::string& text) {
    if (state_ == State::Content) {
        core::MTextBlock b;
        b.pos = pos_;
        b.width = width_;
        b.height = 2.5;
        b.attach = 0; // top-left
        ctx.submit(core::AddMTextCommand{b, text, ctx.group_id()});
        ctx.echo("MText placed.");
        ctx.clear_preview();
        done_ = true;
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    if (state_ == State::First) {
        c1_ = *p;
        ctx.set_last_point(*p);
        state_ = State::Second;
        ctx.set_preview(PreviewSpec{PreviewKind::Rectangle, {c1_}});
        ctx.set_prompt("Specify opposite corner: ");
        return;
    }
    // Second corner: top-left insertion + wrap width from the box.
    pos_ = {std::min(c1_.x, p->x), std::max(c1_.y, p->y)};
    width_ = std::abs(p->x - c1_.x);
    state_ = State::Content;
    ctx.clear_preview();
    ctx.set_prompt("Enter text: ");
}

void MTextCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.clear_preview();
    done_ = true;
}

// ---------------------------------------------------------------------------
// QLEADER: arrow point -> leader vertices (Enter to finish) -> annotation text.
// ---------------------------------------------------------------------------
void QLeaderCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Specify leader arrow point: ");
}

void QLeaderCommand::input(CommandContext& ctx, const std::string& text) {
    if (state_ == State::Content) {
        core::MTextBlock b;
        b.pos = verts_.empty() ? core::Vec2{} : verts_.back(); // landing = text anchor
        b.height = 2.5;
        b.attach = 0;
        ctx.submit(core::AddMLeaderCommand{verts_, 0, b, text, ctx.group_id()});
        ctx.echo("Leader placed.");
        ctx.clear_preview();
        done_ = true;
        return;
    }
    // Empty input finishes the vertex chain (needs an arrow + at least one vertex).
    if (state_ == State::Vertices && text.empty()) {
        if (verts_.size() >= 2) {
            state_ = State::Content;
            ctx.clear_preview();
            ctx.set_prompt("Enter annotation text: ");
        }
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    verts_.push_back(*p);
    ctx.set_last_point(*p);
    state_ = State::Vertices;
    ctx.set_preview(PreviewSpec{PreviewKind::Polyline, verts_});
    ctx.set_prompt("Specify next leader point (Enter to finish): ");
}

void QLeaderCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.clear_preview();
    done_ = true;
}

// ---------------------------------------------------------------------------
// TEXTEDIT / DDEDIT: pick a text entity, then type its new content.
// ---------------------------------------------------------------------------
void TextEditCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select text/MText/leader-label to edit: ");
}

void TextEditCommand::input(CommandContext& ctx, const std::string& text) {
    if (state_ == State::Content) {
        ctx.submit(core::EditTextContentCommand{at_, radius_, text, ctx.group_id()});
        ctx.echo("Text edited.");
        done_ = true;
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    at_ = *p;
    radius_ = ctx.pick_radius();
    state_ = State::Content;
    ctx.set_prompt("Enter new text: ");
}

void TextEditCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// PROPERTIES (PR): toggle the Properties palette via the view, then finish.
// ---------------------------------------------------------------------------
void PropertiesCommand::start(CommandContext& ctx) {
    if (ViewControl* v = ctx.view(); v != nullptr) {
        v->open_properties();
    }
    ctx.echo("Properties palette toggled.");
    done_ = true;
}

void DwgInCommand::start(CommandContext& ctx) {
    if (ViewControl* v = ctx.view(); v != nullptr) {
        v->import_dwg();
    }
    done_ = true;
}

void DwgOutCommand::start(CommandContext& ctx) {
    if (ViewControl* v = ctx.view(); v != nullptr) {
        v->export_dwg();
    }
    done_ = true;
}

void PlotCommand::start(CommandContext& ctx) {
    if (ViewControl* v = ctx.view(); v != nullptr) {
        v->plot_dialog();
    }
    done_ = true;
}

// ---------------------------------------------------------------------------
// LTSCALE: prompt for the global linetype scale factor, then apply it.
// ---------------------------------------------------------------------------
void LtscaleCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Enter new linetype scale factor <1.0>: ");
}

void LtscaleCommand::input(CommandContext& ctx, const std::string& text) {
    if (text.empty()) {
        done_ = true; // Enter with no value -> keep current
        return;
    }
    try {
        const double scale = std::stod(text);
        if (scale > 0.0) {
            ctx.submit(core::SetLtscaleCommand{scale});
            ctx.echo("LTSCALE = " + text);
        } else {
            ctx.echo("Value must be positive.");
        }
    } catch (const std::exception&) {
        ctx.echo("Requires a numeric scale.");
    }
    done_ = true;
}

void LtscaleCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

} // namespace musacad::command

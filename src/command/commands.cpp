// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/command/commands.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>

#include "musacad/command/coordinate.hpp"

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
    ctx.set_prompt("Specify first corner point: ");
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
    // Commit the closed 4-corner polyline from first_ to `other`, rotated about first_.
    const auto commit = [&](core::Vec2 other) {
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
        ctx.submit(core::AddPolylineCommand{std::move(c), true, ctx.group_id()});
        ctx.echo("Rectangle created.");
        done_ = true;
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
        if (const auto p = read_point(ctx, text)) {
            first_ = *p;
            ctx.set_last_point(*p);
            state_ = State::AwaitCorner;
            refresh_preview();
            ctx.set_prompt(kCornerPrompt);
        }
        return;

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
        ctx.submit(core::HatchFromSelectionCommand{"SOLID", 1.0, 0.0, ctx.group_id()});
        done_ = true;
        return;
    }
    ctx.set_prompt("Pick internal point: ");
}

void HatchCommand::input(CommandContext& ctx, const std::string& text) {
    if (trimmed(text).empty()) {
        done_ = true; // Enter finishes the command
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        // Click inside a closed region -> trace its boundary (+ islands) and hatch it. Each
        // pick is its own undo group; the command stays active for more picks.
        ctx.submit(core::HatchPickPointCommand{*p, "SOLID", 1.0, 0.0, ctx.new_group()});
        ctx.set_prompt("Pick internal point or Enter to finish: ");
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
            ctx.set_prompt("Specify rotation angle: ");
        }
        return;
    }
    const std::string t = trimmed(text);
    double deg = 0.0;
    double angle = 0.0;
    if (parse_number(t, deg)) {
        angle = core::to_radians(deg); // typed number = degrees
    } else if (const auto p = read_point(ctx, text)) {
        angle = std::atan2(p->y - base_->y, p->x - base_->x); // picked = angle to point
    } else {
        return;
    }
    ctx.submit(core::RotateSelectionCommand{*base_, angle, ctx.group_id()});
    ctx.echo("Rotated.");
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
    double factor = 0.0;
    if (parse_number(t, factor)) {
        // typed factor
    } else if (const auto p = read_point(ctx, text)) {
        factor = core::distance(*base_, *p); // picked = distance (reference length 1)
    } else {
        return;
    }
    if (!(factor > 0.0)) {
        ctx.echo("Scale factor must be positive.");
        return;
    }
    ctx.submit(core::ScaleSelectionCommand{*base_, factor, ctx.group_id()});
    ctx.echo("Scaled.");
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

void ArrayCommand::start(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        ctx.echo("No selection. Select objects first, then run ARRAY.");
        done_ = true;
        return;
    }
    ctx.clear_last_point();
    ctx.set_prompt("Enter array type [Rectangular/Polar] <R>: ");
}

void ArrayCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    switch (state_) {
    case State::Type: {
        const std::string u = upper(t);
        if (u == "P" || u == "POLAR") {
            state_ = State::Center;
            ctx.set_prompt("Specify center point of array: ");
        } else {
            state_ = State::Rows;
            ctx.set_prompt("Enter number of rows <1>: ");
        }
        return;
    }
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
        double col_space = 0.0;
        if (!parse_number(t, col_space)) {
            ctx.echo("Enter a number for column spacing.");
            return;
        }
        ctx.submit(core::ArrayRectCommand{rows_, cols_, col_space, row_space_, ctx.group_id()});
        ctx.echo("Array created.");
        done_ = true;
        return;
    }
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
        const double deg = t.empty() ? 360.0 : [&] {
            double d = 360.0;
            if (!parse_number(t, d)) {
                d = 360.0;
            }
            return d;
        }();
        fill_ = core::to_radians(deg);
        state_ = State::RotateItems;
        ctx.set_prompt("Rotate items as copied? [Yes/No] <Yes>: ");
        return;
    }
    case State::RotateItems: {
        const std::string u = upper(t);
        const bool rotate = !(u == "N" || u == "NO");
        ctx.submit(core::ArrayPolarCommand{center_, count_, fill_, rotate, ctx.group_id()});
        ctx.echo("Polar array created.");
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
            state_ = State::Height;
            ctx.set_prompt("Specify text height <2.5>: ");
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
    case State::Content:
        ctx.submit(core::AddTextCommand{pos_, height_, rotation_, 0, text, ctx.group_id()});
        ctx.echo("Text placed.");
        done_ = true;
        return;
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
        ctx.submit(core::AddDimensionCommand{static_cast<std::uint8_t>(type_), a_, b_, *p, 0,
                                             ctx.group_id()});
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

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
            ctx.set_prompt("Specify radius: ");
        }
        return;
    }
    double radius = 0.0;
    if (parse_number(text, radius)) {
        // explicit radius
    } else if (const auto p = read_point(ctx, text)) {
        radius = core::distance(center_, *p);
    } else {
        return; // read_point already echoed the error
    }
    if (radius <= 0.0) {
        ctx.echo("Radius must be positive.");
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
    if (state_ == State::First) {
        if (const auto p = read_point(ctx, text)) {
            first_ = *p;
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Rectangle, {first_}});
            state_ = State::Second;
            ctx.set_prompt("Specify other corner point: ");
        }
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    const core::Vec2 o = *p;
    std::vector<core::Vec2> corners{{first_.x, first_.y},
                                    {o.x, first_.y},
                                    {o.x, o.y},
                                    {first_.x, o.y}};
    ctx.submit(core::AddPolylineCommand{std::move(corners), true, ctx.group_id()});
    ctx.echo("Rectangle created.");
    done_ = true;
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
        ctx.echo("Offset created.");
        state_ = State::Object;
        ctx.set_prompt("Select object to offset: ");
    }
}

void OffsetCommand::cancel(CommandContext& ctx) {
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
    ctx.echo("Trimmed.");
}

void TrimCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

} // namespace musacad::command

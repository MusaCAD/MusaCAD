// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/command/commands.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <optional>
#include <string>

#include "musacad/command/calc.hpp"
#include "musacad/command/coordinate.hpp"
#include "musacad/command/snap_keywords.hpp"
#include "musacad/core/hatch_pattern.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/ellipse.hpp"
#include "musacad/core/polygon.hpp"
#include "musacad/core/spline_eval.hpp"
#include "musacad/core/math/tangent_circle.hpp"
#include "musacad/core/text/justify.hpp"
#include "musacad/core/units.hpp"
#include "musacad/core/polyline_ops.hpp"
#include "musacad/core/io/dxf.hpp"

namespace musacad::command {

namespace {

std::string fmt4(double v); // "%.4f", defined with DONUT below

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
    // Enter at a point prompt with no default: AutoCAD asks again, saying nothing.
    if (trimmed(text).empty()) {
        return std::nullopt;
    }
    // An object snap for this one pick, or FROM / M2P / TT / TK: the point follows.
    if (ctx.point_modifier(text)) {
        return std::nullopt;
    }
    // Direct distance entry: a bare number goes along the cursor's bearing from the last
    // point (the cursor with ortho / polar applied, as AutoCAD does it).
    std::optional<double> bearing;
    const auto last = ctx.last_point();
    if (const auto cur = ctx.cursor_world(); last && cur && core::distance(*last, *cur) > 1e-12) {
        bearing = std::atan2(cur->y - last->y, cur->x - last->x);
    }
    const CoordParse p = parse_coordinate(text, last, bearing);
    if (!p.ok) {
        ctx.echo(p.error);
        return std::nullopt;
    }
    // LIMITS on: a point outside the drawing's limits is refused, as AutoCAD refuses it.
    if (const CommandContext::Limits lim = ctx.limits();
        lim.on && (p.point.x < lim.min.x || p.point.y < lim.min.y || p.point.x > lim.max.x || p.point.y > lim.max.y)) {
        ctx.echo("**Outside limits");
        return std::nullopt;
    }
    ctx.echo("  = " + p.interpretation);
    return p.point;
}

/// Circumcircle of three points. Returns false if (near) collinear.
} // namespace

// ---------------------------------------------------------------------------
// LINE
// ---------------------------------------------------------------------------
void LineCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Specify first point: ");
}

void LineCommand::prompt_next(CommandContext& ctx) {
    ctx.set_preview({PreviewKind::Segment, {points_.back()}});
    if (fixed_dir_) {
        ctx.set_prompt("Specify length of line: ");
    } else {
        ctx.set_prompt(points_.size() >= 3 ? "Specify next point or [Close/Undo]: "
                                           : "Specify next point or [Undo]: ");
    }
}

void LineCommand::add_segment(CommandContext& ctx, core::Vec2 to) {
    const core::Vec2 from = points_.back();
    ctx.submit(core::AddLineCommand{from, to, ctx.group_id()});
    points_.push_back(to);
    ctx.set_last_point(to);
    ctx.set_last_segment({to, std::atan2(to.y - from.y, to.x - from.x), false});
}

void LineCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    if (points_.empty()) {
        if (t.empty()) {
            // Continue: from the end of the last line or arc; tangent to an arc, with
            // only the length asked.
            const auto seg = ctx.last_segment();
            if (!seg) {
                done_ = true;
                return;
            }
            points_.push_back(seg->end);
            ctx.set_last_point(seg->end);
            if (seg->arc) {
                fixed_dir_ = seg->tangent;
            }
            prompt_next(ctx);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            points_.push_back(*p);
            ctx.set_last_point(*p);
            prompt_next(ctx);
        }
        return;
    }
    if (t.empty()) {
        done_ = true; // Enter ends LINE
        return;
    }
    if (u == "U" || u == "UNDO") {
        if (points_.size() >= 2) {
            points_.pop_back();
            ctx.submit(core::UndoLastOpCommand{});
            ctx.set_last_point(points_.back());
            ctx.echo("Undo last segment");
            prompt_next(ctx);
        } else {
            points_.clear();
            fixed_dir_.reset();
            ctx.clear_last_point();
            ctx.clear_preview();
            ctx.set_prompt("Specify first point: ");
        }
        return;
    }
    if ((u == "C" || u == "CLOSE") && points_.size() >= 3) {
        add_segment(ctx, points_.front());
        done_ = true;
        return;
    }
    if (fixed_dir_) {
        double len = 0.0;
        if (parse_number(t, len)) {
            // typed
        } else if (const auto p = read_point(ctx, text)) {
            len = core::distance(points_.back(), *p);
        } else {
            return;
        }
        if (!(len > 0.0)) {
            ctx.echo("Value must be positive and nonzero.");
            return;
        }
        const core::Vec2 to{points_.back().x + len * std::cos(*fixed_dir_),
                            points_.back().y + len * std::sin(*fixed_dir_)};
        fixed_dir_.reset();
        add_segment(ctx, to);
        prompt_next(ctx);
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        add_segment(ctx, *p);
        prompt_next(ctx);
    }
}

void LineCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// CIRCLE
// ---------------------------------------------------------------------------
namespace {
double g_circle_rad = 0.0; // CIRCLERAD: the last radius, the next default
}

void CircleCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Specify center point for circle or [3P/2P/Ttr (tan tan radius)]: ");
}

void CircleCommand::prompt_radius(CommandContext& ctx) {
    const std::string dflt =
        g_circle_rad > 0.0
            ? " <" + core::units::format_length(state_ == State::Diameter ? 2.0 * g_circle_rad : g_circle_rad,
                                                ctx.units()) + ">"
            : "";
    PreviewSpec pv;
    pv.kind = PreviewKind::Circle;
    pv.points = {center_};
    pv.circle_mode = state_ == State::Diameter ? 1 : 0;
    ctx.set_preview(pv);
    ctx.set_prompt(state_ == State::Diameter ? "Specify diameter of circle" + dflt + ": "
                                             : "Specify radius of circle or [Diameter]" + dflt + ": ");
}

void CircleCommand::finish(CommandContext& ctx, core::Vec2 center, double radius) {
    if (!(radius > 0.0)) {
        ctx.echo("Value must be positive and nonzero.");
        return;
    }
    g_circle_rad = radius;
    ctx.submit(core::AddCircleCommand{center, radius, ctx.group_id()});
    ctx.echo("Circle: radius " + core::units::format_length(radius, ctx.units()));
    done_ = true;
}

void CircleCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    double value = 0.0;
    switch (state_) {
    case State::Center:
        if (u == "3P") {
            state_ = State::ThreeFirst;
            ctx.set_prompt("Specify first point on circle: ");
            return;
        }
        if (u == "2P") {
            state_ = State::TwoFirst;
            ctx.set_prompt("Specify first end point of circle's diameter: ");
            return;
        }
        if (u == "T" || u == "TTR") {
            state_ = State::TanFirst;
            ctx.set_prompt("Specify point on object for first tangent of circle: ");
            return;
        }
        if (u == "TTT") { // the ribbon's Tan, Tan, Tan
            state_ = State::TttFirst;
            ctx.set_prompt("Specify point on object for first tangent of circle: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            center_ = *p;
            ctx.set_last_point(*p);
            state_ = State::Radius;
            prompt_radius(ctx);
        }
        return;
    case State::Radius:
    case State::Diameter: {
        if (state_ == State::Radius && (u == "D" || u == "DIAMETER")) {
            state_ = State::Diameter;
            prompt_radius(ctx);
            return;
        }
        const bool by_diameter = state_ == State::Diameter;
        if (t.empty()) {
            if (!(g_circle_rad > 0.0)) {
                ctx.echo("Requires numeric distance or second point.");
                return;
            }
            finish(ctx, center_, g_circle_rad);
            return;
        }
        if (parse_number(t, value)) {
            finish(ctx, center_, by_diameter ? value * 0.5 : value);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            // A pick is the distance from the centre: the radius, or the diameter itself.
            const double d = core::distance(center_, *p);
            finish(ctx, center_, by_diameter ? d * 0.5 : d);
            return;
        }
        ctx.echo("Requires numeric distance or second point.");
        return;
    }
    case State::TwoFirst:
        if (const auto p = read_point(ctx, text)) {
            pts_ = {*p};
            ctx.set_last_point(*p);
            PreviewSpec pv;
            pv.kind = PreviewKind::Circle;
            pv.points = pts_;
            pv.circle_mode = 2;
            ctx.set_preview(pv);
            state_ = State::TwoSecond;
            ctx.set_prompt("Specify second end point of circle's diameter: ");
        }
        return;
    case State::TwoSecond:
        if (const auto p = read_point(ctx, text)) {
            const core::Vec2 c = (pts_[0] + *p) * 0.5;
            const double r = core::distance(pts_[0], *p) * 0.5;
            if (!(r > 0.0)) {
                ctx.echo("Circle does not exist.");
                return;
            }
            finish(ctx, c, r);
        }
        return;
    case State::ThreeFirst:
        if (const auto p = read_point(ctx, text)) {
            pts_ = {*p};
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Segment, {*p}});
            state_ = State::ThreeSecond;
            ctx.set_prompt("Specify second point on circle: ");
        }
        return;
    case State::ThreeSecond:
        if (const auto p = read_point(ctx, text)) {
            pts_.push_back(*p);
            ctx.set_last_point(*p);
            PreviewSpec pv;
            pv.kind = PreviewKind::Circle;
            pv.points = pts_;
            pv.circle_mode = 3;
            ctx.set_preview(pv);
            state_ = State::ThreeThird;
            ctx.set_prompt("Specify third point on circle: ");
        }
        return;
    case State::ThreeThird:
        if (const auto p = read_point(ctx, text)) {
            core::Vec2 c;
            double r = 0.0;
            if (!core::circumcircle(pts_[0], pts_[1], *p, c, r)) {
                ctx.echo("Circle does not exist.");
                return;
            }
            finish(ctx, c, r);
        }
        return;
    case State::TanFirst:
    case State::TttFirst:
        if (const auto p = read_point(ctx, text)) {
            pts_ = {*p};
            state_ = state_ == State::TanFirst ? State::TanSecond : State::TttSecond;
            ctx.set_prompt("Specify point on object for second tangent of circle: ");
        }
        return;
    case State::TanSecond:
        if (const auto p = read_point(ctx, text)) {
            pts_.push_back(*p);
            state_ = State::TanRadius;
            ctx.set_prompt("Specify radius of circle" +
                           (g_circle_rad > 0.0 ? " <" + core::units::format_length(g_circle_rad, ctx.units()) + ">"
                                               : std::string()) +
                           ": ");
        }
        return;
    case State::TanRadius: {
        double r = 0.0;
        if (t.empty()) {
            r = g_circle_rad;
        } else if (parse_number(t, r)) {
            // typed
        } else if (const auto p = read_point(ctx, text)) {
            r = ctx.last_point() ? core::distance(*ctx.last_point(), *p) : 0.0;
        }
        if (!(r > 0.0)) {
            ctx.echo("Requires numeric distance or second point.");
            return;
        }
        g_circle_rad = r;
        ctx.submit(core::AddCircleTangentCommand{pts_, r, ctx.pick_radius(), ctx.group_id()});
        done_ = true;
        return;
    }
    case State::TttSecond:
        if (const auto p = read_point(ctx, text)) {
            pts_.push_back(*p);
            state_ = State::TttThird;
            ctx.set_prompt("Specify point on object for third tangent of circle: ");
        }
        return;
    case State::TttThird:
        if (const auto p = read_point(ctx, text)) {
            pts_.push_back(*p);
            ctx.submit(core::AddCircleTangentCommand{pts_, 0.0, ctx.pick_radius(), ctx.group_id()});
            done_ = true;
        }
        return;
    }
}

void CircleCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// PLINE
// ---------------------------------------------------------------------------
namespace {
constexpr const char* kPlineArcPrompt =
    "Specify endpoint of arc (hold Ctrl to switch direction) or "
    "[Angle/CEnter/CLose/Direction/Halfwidth/Line/Radius/Second pt/Undo/Width]: ";
}

void PolylineCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    start_w_ = s_width_;
    end_w_ = s_width_;
    ctx.echo("Current line-width is " + core::units::format_length(s_width_, ctx.units()));
    ctx.set_prompt("Specify start point: ");
}

void PolylineCommand::begin_width(CommandContext& ctx, bool half) {
    resume_ = state_;
    half_ = half;
    state_ = State::WidthStart;
    refresh_preview(ctx, 0);
    ctx.set_prompt(std::string("Specify starting ") + (half ? "half-width" : "width") + " <" +
                   core::units::format_length(half ? end_w_ * 0.5 : end_w_, ctx.units()) + ">: ");
}

double PolylineCommand::start_tangent(CommandContext& ctx) const {
    // The heading an arc leaves the last vertex with: the previous segment's end
    // tangent; for a first segment, the last line or arc drawn; else east.
    if (!tangents_.empty()) {
        return tangents_.back();
    }
    if (const auto seg = ctx.last_segment()) {
        return seg->tangent;
    }
    return 0.0;
}

void PolylineCommand::refresh_preview(CommandContext& ctx, int arc_mode) {
    PreviewSpec pv;
    pv.kind = PreviewKind::Polyline;
    pv.points = points_;
    pv.bulges = bulges_;
    pv.widths = widths_;
    pv.pline_w0 = start_w_;
    pv.pline_w1 = end_w_;
    pv.pline_dyn = arc_mode == 0 && state_ == State::Next;
    pv.pline_arc_mode = arc_mode;
    pv.pline_tangent = arc_mode == 4 ? direction_ : start_tangent(ctx);
    pv.pline_angle = angle_;
    pv.pline_radius = radius_;
    pv.pline_center = center_;
    pv.pline_second = second_;
    ctx.set_preview(pv);
}

void PolylineCommand::prompt_next(CommandContext& ctx) {
    if (state_ == State::ArcNext) {
        refresh_preview(ctx, 1);
        ctx.set_prompt(kPlineArcPrompt);
        return;
    }
    state_ = State::Next;
    refresh_preview(ctx, 0);
    ctx.set_prompt(points_.size() >= 2 ? "Specify next point or [Arc/Close/Halfwidth/Length/Undo/Width]: "
                                       : "Specify next point or [Arc/Halfwidth/Length/Undo/Width]: ");
}

void PolylineCommand::add_segment(CommandContext& ctx, core::Vec2 end, double bulge, double end_tangent) {
    bulges_.push_back(bulge);
    tangents_.push_back(end_tangent);
    widths_.push_back(start_w_);
    widths_.push_back(end_w_);
    start_w_ = end_w_; // the ending width stays in force
    points_.push_back(end);
    ctx.set_last_point(end);
}

void PolylineCommand::add_arc(CommandContext& ctx, const std::optional<core::ConstructedArc>& arc) {
    if (!arc) {
        ctx.echo("*Invalid*");
        state_ = State::ArcNext;
        prompt_next(ctx);
        return;
    }
    add_segment(ctx, arc->end_point, arc->bulge(), arc->end_tangent);
    state_ = State::ArcNext;
    prompt_next(ctx);
}

void PolylineCommand::undo_segment(CommandContext& ctx) {
    if (points_.size() >= 2) {
        points_.pop_back();
        bulges_.pop_back();
        tangents_.pop_back();
        if (widths_.size() >= 2) {
            widths_.resize(widths_.size() - 2);
        }
        ctx.set_last_point(points_.back());
        prompt_next(ctx);
        return;
    }
    points_.clear();
    bulges_.clear();
    tangents_.clear();
    widths_.clear();
    state_ = State::Start;
    ctx.clear_last_point();
    ctx.clear_preview();
    ctx.set_prompt("Specify start point: ");
}

void PolylineCommand::finish(CommandContext& ctx, bool closed) {
    if (points_.size() < 2) {
        done_ = true;
        return;
    }
    core::AddPolylineCommand poly;
    poly.points = points_;
    poly.closed = closed;
    poly.group = ctx.group_id();
    bool any_arc = false;
    for (const double b : bulges_) {
        any_arc = any_arc || std::abs(b) > 1e-12;
    }
    if (any_arc) {
        poly.bulges = bulges_;
        poly.bulges.resize(points_.size(), 0.0);
    }
    // Two widths per vertex: the closing segment (and the vertex an open polyline ends
    // on) takes the width in force.
    std::vector<double> widths = widths_;
    while (widths.size() < 2 * points_.size()) {
        widths.push_back(start_w_);
        widths.push_back(end_w_);
    }
    widths.resize(2 * points_.size());
    if (std::any_of(widths.begin(), widths.end(), [](double w) { return w > 0.0; })) {
        poly.widths = std::move(widths);
    }
    ctx.submit(std::move(poly));
    ctx.echo(closed ? "Closed polyline created."
                    : "Polyline created (" + std::to_string(points_.size()) + " vertices).");
    if (closed && bulges_.size() == points_.size()) {
        ctx.set_last_segment({points_.front(), tangents_.back(), std::abs(bulges_.back()) > 1e-12});
    } else if (closed) {
        const core::Vec2 a = points_.back();
        const core::Vec2 b = points_.front();
        ctx.set_last_segment({b, std::atan2(b.y - a.y, b.x - a.x), false});
    } else {
        ctx.set_last_segment({points_.back(), tangents_.back(), std::abs(bulges_.back()) > 1e-12});
    }
    done_ = true;
}

void PolylineCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    const bool ctrl = ctx.ctrl_held();
    const core::Vec2 last = points_.empty() ? core::Vec2{} : points_.back();
    double v = 0.0;
    switch (state_) {
    case State::Start:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            points_.push_back(*p);
            ctx.set_last_point(*p);
            prompt_next(ctx);
        }
        return;

    case State::Next:
        if (t.empty()) {
            finish(ctx, false);
            return;
        }
        if (u == "A" || u == "ARC") {
            state_ = State::ArcNext;
            prompt_next(ctx);
            return;
        }
        if ((u == "C" || u == "CLOSE") && points_.size() >= 3) {
            finish(ctx, true);
            return;
        }
        if (u == "C" || u == "CLOSE") {
            ctx.echo("Need at least 3 points to close.");
            return;
        }
        if (u == "L" || u == "LENGTH") {
            state_ = State::Length;
            refresh_preview(ctx, 0);
            ctx.set_prompt("Specify length of line: ");
            return;
        }
        if (u == "U" || u == "UNDO") {
            undo_segment(ctx);
            return;
        }
        if (u == "W" || u == "WIDTH" || u == "H" || u == "HALFWIDTH") {
            begin_width(ctx, u.front() == 'H');
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            add_segment(ctx, *p, 0.0, std::atan2(p->y - last.y, p->x - last.x));
            prompt_next(ctx);
        }
        return;

    case State::WidthStart:
    case State::WidthEnd: {
        // A number, or a point whose distance from the last vertex is the value; Enter
        // keeps the default (the width in force, then the starting width just given).
        const bool ending = state_ == State::WidthEnd;
        double w = ending ? start_w_ : end_w_;
        if (!t.empty()) {
            double given = 0.0;
            if (parse_number(t, given)) {
                // typed
            } else if (const auto p = read_point(ctx, text)) {
                given = core::distance(last, *p);
            } else {
                return;
            }
            if (given < 0.0) {
                ctx.echo("Value must be positive or zero.");
                return;
            }
            w = half_ ? given * 2.0 : given;
        }
        if (!ending) {
            start_w_ = w;
            state_ = State::WidthEnd;
            ctx.set_prompt(std::string("Specify ending ") + (half_ ? "half-width" : "width") + " <" +
                           core::units::format_length(half_ ? w * 0.5 : w, ctx.units()) + ">: ");
            return;
        }
        end_w_ = w;
        s_width_ = w;
        state_ = resume_;
        prompt_next(ctx);
        return;
    }

    case State::Length: {
        // Along the previous segment (tangent to an arc), the length typed or picked.
        double len = 0.0;
        if (parse_number(t, len)) {
            // typed
        } else if (const auto p = read_point(ctx, text)) {
            len = core::distance(last, *p);
        } else {
            return;
        }
        if (!(len > 0.0)) {
            ctx.echo("Value must be positive and nonzero.");
            return;
        }
        const double dir = start_tangent(ctx);
        add_segment(ctx, {last.x + len * std::cos(dir), last.y + len * std::sin(dir)}, 0.0, dir);
        state_ = State::Next;
        prompt_next(ctx);
        return;
    }

    case State::ArcNext:
        if (t.empty()) {
            finish(ctx, false);
            return;
        }
        if (u == "A" || u == "ANGLE") {
            state_ = State::ArcAngle;
            ctx.set_prompt("Specify included angle: ");
            return;
        }
        if (u == "CE" || u == "CENTER") {
            state_ = State::ArcCenter;
            ctx.set_prompt("Specify center point of arc: ");
            return;
        }
        if (u == "CL" || u == "CLOSE") {
            if (points_.size() < 2) {
                ctx.echo("Need at least 2 points to close.");
                return;
            }
            // The closing arc: tangent to the last segment, back to the start.
            const auto arc = core::arc_start_end_direction(last, points_.front(), start_tangent(ctx));
            if (!arc) {
                ctx.echo("*Invalid*");
                return;
            }
            bulges_.push_back(arc->bulge());
            tangents_.push_back(arc->end_tangent);
            finish(ctx, true);
            return;
        }
        if (u == "D" || u == "DIRECTION") {
            state_ = State::ArcDirection;
            ctx.set_prompt("Specify the tangent direction for the start point of arc: ");
            return;
        }
        if (u == "L" || u == "LINE") {
            state_ = State::Next;
            prompt_next(ctx);
            return;
        }
        if (u == "R" || u == "RADIUS") {
            state_ = State::ArcRadius;
            ctx.set_prompt("Specify radius of arc: ");
            return;
        }
        if (u == "S" || u == "SECOND" || u == "SECOND PT") {
            state_ = State::ArcSecond;
            ctx.set_prompt("Specify second point on arc: ");
            return;
        }
        if (u == "U" || u == "UNDO") {
            undo_segment(ctx);
            return;
        }
        if (u == "W" || u == "WIDTH" || u == "H" || u == "HALFWIDTH") {
            begin_width(ctx, u.front() == 'H');
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            // Tangent to the previous segment; Ctrl bends it the other way (the arc
            // leaves in the opposite direction, as AutoCAD's Ctrl does here).
            const double dir = start_tangent(ctx) + (ctrl ? core::kPi : 0.0);
            add_arc(ctx, core::arc_start_end_direction(last, *p, dir));
        }
        return;

    case State::ArcAngle:
        if (parse_number(t, v)) {
            angle_ = core::to_radians(v);
            state_ = State::ArcAngleEnd;
            refresh_preview(ctx, 2);
            ctx.set_prompt("Specify endpoint of arc (hold Ctrl to switch direction) or [CEnter/Radius]: ");
        } else {
            ctx.echo("Requires valid numeric angle.");
        }
        return;
    case State::ArcAngleEnd:
        if (u == "CE" || u == "CENTER") {
            state_ = State::ArcCenterAngle;
            ctx.set_prompt("Specify center point of arc: ");
            return;
        }
        if (u == "R" || u == "RADIUS") {
            state_ = State::ArcAngleRadius;
            ctx.set_prompt("Specify radius of arc: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            add_arc(ctx, core::arc_start_end_angle(last, *p, angle_, ctrl));
        }
        return;
    case State::ArcAngleRadius:
        if (parse_number(t, v) && v > 0.0) {
            radius_ = v;
            state_ = State::ArcAngleChordDir;
            ctx.set_prompt("Specify direction of chord for arc <" +
                           core::units::format_angle(start_tangent(ctx), ctx.units()) + ">: ");
        } else {
            ctx.echo("Value must be positive and nonzero.");
        }
        return;
    case State::ArcAngleChordDir:
    case State::ArcRadiusChordDir: {
        // The chord of an arc of radius r and included angle a is 2 r sin(a / 2), laid
        // along the given direction from the last vertex.
        double dir = start_tangent(ctx);
        if (t.empty()) {
            // the default direction
        } else if (parse_number(t, v)) {
            dir = core::to_radians(v);
        } else if (const auto p = read_point(ctx, text)) {
            dir = std::atan2(p->y - last.y, p->x - last.x);
        } else {
            return;
        }
        const double chord = 2.0 * radius_ * std::sin(std::abs(angle_) * 0.5);
        const core::Vec2 e{last.x + chord * std::cos(dir), last.y + chord * std::sin(dir)};
        add_arc(ctx, core::arc_start_end_angle(last, e, angle_, ctrl));
        return;
    }

    case State::ArcCenter:
        if (const auto p = read_point(ctx, text)) {
            center_ = *p;
            state_ = State::ArcCenterEnd;
            refresh_preview(ctx, 3);
            ctx.set_prompt("Specify endpoint of arc (hold Ctrl to switch direction) or [Angle/Length]: ");
        }
        return;
    case State::ArcCenterEnd:
        if (u == "A" || u == "ANGLE") {
            state_ = State::ArcCenterAngle;
            ctx.set_prompt("Specify included angle: ");
            return;
        }
        if (u == "L" || u == "LENGTH") {
            state_ = State::ArcCenterLength;
            ctx.set_prompt("Specify length of chord: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            add_arc(ctx, core::arc_start_center_end(last, center_, *p, ctrl));
        }
        return;
    case State::ArcCenterAngle:
        if (parse_number(t, v)) {
            add_arc(ctx, core::arc_start_center_angle(last, center_, core::to_radians(v), ctrl));
        } else if (const auto p = read_point(ctx, text)) {
            // From [Angle] > [CEnter]: the point is the centre, the angle already given.
            add_arc(ctx, core::arc_start_center_angle(last, *p, angle_, ctrl));
        }
        return;
    case State::ArcCenterLength:
        if (parse_number(t, v)) {
            add_arc(ctx, core::arc_start_center_length(last, center_, v, ctrl));
        } else if (const auto p = read_point(ctx, text)) {
            add_arc(ctx, core::arc_start_center_length(last, center_, core::distance(last, *p), ctrl));
        }
        return;

    case State::ArcDirection:
        if (parse_number(t, v)) {
            direction_ = core::to_radians(v);
        } else if (const auto p = read_point(ctx, text)) {
            direction_ = std::atan2(p->y - last.y, p->x - last.x);
        } else {
            return;
        }
        state_ = State::ArcDirectionEnd;
        refresh_preview(ctx, 4);
        ctx.set_prompt("Specify endpoint of arc: ");
        return;
    case State::ArcDirectionEnd:
        if (const auto p = read_point(ctx, text)) {
            add_arc(ctx, core::arc_start_end_direction(last, *p, direction_));
        }
        return;

    case State::ArcRadius:
        if (parse_number(t, v) && v > 0.0) {
            radius_ = v;
            state_ = State::ArcRadiusEnd;
            refresh_preview(ctx, 5);
            ctx.set_prompt("Specify endpoint of arc (hold Ctrl to switch direction) or [Angle]: ");
        } else {
            ctx.echo("Value must be positive and nonzero.");
        }
        return;
    case State::ArcRadiusEnd:
        if (u == "A" || u == "ANGLE") {
            state_ = State::ArcRadiusAngle;
            ctx.set_prompt("Specify included angle: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            add_arc(ctx, core::arc_start_end_radius(last, *p, radius_, ctrl));
        }
        return;
    case State::ArcRadiusAngle:
        if (parse_number(t, v)) {
            angle_ = core::to_radians(v);
            state_ = State::ArcRadiusChordDir;
            ctx.set_prompt("Specify direction of chord for arc <" +
                           core::units::format_angle(start_tangent(ctx), ctx.units()) + ">: ");
        } else {
            ctx.echo("Requires valid numeric angle.");
        }
        return;

    case State::ArcSecond:
        if (const auto p = read_point(ctx, text)) {
            second_ = *p;
            state_ = State::ArcSecondEnd;
            refresh_preview(ctx, 6);
            ctx.set_prompt("Specify end point of arc: ");
        }
        return;
    case State::ArcSecondEnd:
        if (const auto p = read_point(ctx, text)) {
            add_arc(ctx, core::arc_three_points(last, second_, *p));
        }
        return;
    }
}

void PolylineCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// ARC (three-point)
// ---------------------------------------------------------------------------
namespace {
constexpr const char* kArcCenterEndPrompt =
    "Specify end point of arc (hold Ctrl to switch direction) or [Angle/chord Length]: ";
constexpr const char* kArcEndCenterPrompt =
    "Specify center point of arc (hold Ctrl to switch direction) or [Angle/Direction/Radius]: ";
double bearing_of(core::Vec2 from, core::Vec2 to) {
    return std::atan2(to.y - from.y, to.x - from.x);
}
double positive_turn(double a) {
    a = std::fmod(a, core::kTwoPi);
    return a < 0.0 ? a + core::kTwoPi : a;
}
} // namespace

void ArcCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Specify start point of arc or [Center]: ");
}

void ArcCommand::preview(CommandContext& ctx, int mode) {
    PreviewSpec pv;
    pv.kind = PreviewKind::Arc;
    pv.arc_mode = mode;
    switch (mode) {
    case 1:
        pv.points = {s_, m_};
        break;
    case 2:
    case 3:
    case 4:
        pv.points = {s_, c_};
        break;
    case 5:
    case 6:
    case 7:
    case 8:
        pv.points = {s_, e_};
        break;
    default:
        pv.points = {s_};
        pv.arc_tangent = tangent_;
        break;
    }
    ctx.set_preview(pv);
}

void ArcCommand::commit(CommandContext& ctx, const std::optional<core::ConstructedArc>& arc) {
    if (!arc) {
        ctx.echo("*Invalid*");
        return;
    }
    ctx.submit(core::AddArcCommand{arc->center, arc->radius, arc->start, arc->end, ctx.group_id()});
    ctx.set_last_point(arc->end_point);
    ctx.set_last_segment({arc->end_point, arc->end_tangent, true});
    done_ = true;
}

void ArcCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    const bool ctrl = ctx.ctrl_held();
    double v = 0.0;
    switch (state_) {
    case State::Start:
        if (t.empty()) {
            // Continue: tangent from the end of the last line or arc.
            const auto seg = ctx.last_segment();
            if (!seg) {
                return; // nothing to continue from: the prompt stands
            }
            s_ = seg->end;
            tangent_ = seg->tangent;
            ctx.set_last_point(s_);
            state_ = State::ContinueEnd;
            preview(ctx, 9);
            ctx.set_prompt("Specify end point of arc (hold Ctrl to switch direction): ");
            return;
        }
        if (u == "C" || u == "CENTER") {
            state_ = State::CenterFirst;
            ctx.set_prompt("Specify center point of arc: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            s_ = *p;
            ctx.set_last_point(s_);
            state_ = State::Second;
            ctx.set_preview({PreviewKind::Segment, {s_}});
            ctx.set_prompt("Specify second point of arc or [Center/End]: ");
        }
        return;
    case State::Second:
        if (u == "C" || u == "CENTER") {
            state_ = State::AwaitCenter;
            ctx.set_prompt("Specify center point of arc: ");
            return;
        }
        if (u == "E" || u == "END") {
            state_ = State::AwaitEnd;
            ctx.set_prompt("Specify end point of arc: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            m_ = *p;
            ctx.set_last_point(m_);
            state_ = State::ThreeEnd;
            preview(ctx, 1);
            ctx.set_prompt("Specify end point of arc: ");
        }
        return;
    case State::ThreeEnd:
        if (const auto p = read_point(ctx, text)) {
            const auto arc = core::arc_three_points(s_, m_, *p);
            if (!arc) {
                ctx.echo("*Invalid*"); // collinear: pick a different end point
                return;
            }
            commit(ctx, arc);
        }
        return;
    case State::AwaitCenter:
        if (const auto p = read_point(ctx, text)) {
            c_ = *p;
            ctx.set_last_point(c_);
            state_ = State::CenterEnd;
            preview(ctx, 2);
            ctx.set_prompt(kArcCenterEndPrompt);
        }
        return;
    case State::CenterEnd:
        if (u == "A" || u == "ANGLE") {
            state_ = State::CenterAngle;
            preview(ctx, 3);
            ctx.set_prompt("Specify included angle (hold Ctrl to switch direction): ");
            return;
        }
        if (u == "L" || u == "LENGTH") {
            state_ = State::CenterLength;
            preview(ctx, 4);
            ctx.set_prompt("Specify length of chord (hold Ctrl to switch direction): ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            commit(ctx, core::arc_start_center_end(s_, c_, *p, ctrl));
        }
        return;
    case State::CenterAngle:
        if (parse_number(t, v)) {
            commit(ctx, core::arc_start_center_angle(s_, c_, core::to_radians(v), ctrl));
        } else if (const auto p = read_point(ctx, text)) {
            // A point gives the angle it makes at the centre with the start point.
            const double ang = positive_turn(bearing_of(c_, *p) - bearing_of(c_, s_));
            commit(ctx, core::arc_start_center_angle(s_, c_, ang, ctrl));
        }
        return;
    case State::CenterLength:
        if (parse_number(t, v)) {
            commit(ctx, core::arc_start_center_length(s_, c_, v, ctrl));
        } else if (const auto p = read_point(ctx, text)) {
            commit(ctx, core::arc_start_center_length(s_, c_, core::distance(s_, *p), ctrl));
        }
        return;
    case State::AwaitEnd:
        if (const auto p = read_point(ctx, text)) {
            e_ = *p;
            ctx.set_last_point(e_);
            state_ = State::EndCenter;
            preview(ctx, 5);
            ctx.set_prompt(kArcEndCenterPrompt);
        }
        return;
    case State::EndCenter:
        if (u == "A" || u == "ANGLE") {
            state_ = State::EndAngle;
            preview(ctx, 6);
            ctx.set_prompt("Specify included angle (hold Ctrl to switch direction): ");
            return;
        }
        if (u == "D" || u == "DIRECTION") {
            state_ = State::EndDirection;
            preview(ctx, 7);
            ctx.set_prompt(
                "Specify tangent direction for the start point of arc (hold Ctrl to switch direction): ");
            return;
        }
        if (u == "R" || u == "RADIUS") {
            state_ = State::EndRadius;
            preview(ctx, 8);
            ctx.set_prompt("Specify radius of arc (hold Ctrl to switch direction): ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            commit(ctx, core::arc_start_center_end(s_, *p, e_, ctrl));
        }
        return;
    case State::EndAngle:
        if (parse_number(t, v)) {
            commit(ctx, core::arc_start_end_angle(s_, e_, core::to_radians(v), ctrl));
        } else if (const auto p = read_point(ctx, text)) {
            commit(ctx, core::arc_start_end_angle(s_, e_, positive_turn(bearing_of(s_, *p)), ctrl));
        }
        return;
    case State::EndDirection:
        if (parse_number(t, v)) {
            commit(ctx, core::arc_start_end_direction(s_, e_, core::to_radians(v)));
        } else if (const auto p = read_point(ctx, text)) {
            commit(ctx, core::arc_start_end_direction(s_, e_, bearing_of(s_, *p)));
        }
        return;
    case State::EndRadius:
        if (parse_number(t, v)) {
            commit(ctx, core::arc_start_end_radius(s_, e_, v, ctrl));
        } else if (const auto p = read_point(ctx, text)) {
            commit(ctx, core::arc_start_end_radius(s_, e_, core::distance(e_, *p), ctrl));
        }
        return;
    case State::CenterFirst:
        if (const auto p = read_point(ctx, text)) {
            c_ = *p;
            ctx.set_last_point(c_);
            state_ = State::CenterStart;
            ctx.set_preview({PreviewKind::Segment, {c_}});
            ctx.set_prompt("Specify start point of arc: ");
        }
        return;
    case State::CenterStart:
        if (const auto p = read_point(ctx, text)) {
            s_ = *p;
            ctx.set_last_point(s_);
            state_ = State::CenterEnd;
            preview(ctx, 2);
            ctx.set_prompt(kArcCenterEndPrompt);
        }
        return;
    case State::ContinueEnd:
        if (const auto p = read_point(ctx, text)) {
            commit(ctx, core::arc_start_end_direction(s_, *p, tangent_));
        }
        return;
    }
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
    // The modes in force, in AutoCAD's order; nothing is said when there are none.
    const auto fmt = [&](double v) { return core::units::format_length(v, ctx.units()); };
    std::string modes;
    if (chamfer_d1_ > 0.0 || chamfer_d2_ > 0.0) {
        modes += "  Chamfer=" + fmt(chamfer_d1_) + " x " + fmt(chamfer_d2_);
    }
    if (elevation_ != 0.0) {
        modes += "  Elevation=" + fmt(elevation_);
    }
    if (fillet_r_ > 0.0) {
        modes += "  Fillet=" + fmt(fillet_r_);
    }
    if (thickness_ != 0.0) {
        modes += "  Thickness=" + fmt(thickness_);
    }
    if (line_width_ > 0.0) {
        modes += "  Width=" + fmt(line_width_);
    }
    if (!modes.empty()) {
        ctx.echo("Current rectangle modes:" + modes.substr(1));
    }
    ctx.set_prompt(first_prompt());
}

void RectangleCommand::input(CommandContext& ctx, const std::string& text) {
    constexpr const char* kCornerPrompt = "Specify other corner point or [Area/Dimensions/Rotation]: ";
    const std::string up = upper(trimmed(text));
    const auto fmt = [&](double v) { return core::units::format_length(v, ctx.units()); };

    // Push the cursor preview for the current state: corner-to-corner by default, or a
    // FIXED-SIZE quadrant-flip rectangle once dimensions/area are chosen. Carries rotation.
    const auto refresh_preview = [&] {
        PreviewSpec pv{PreviewKind::Rectangle, {first_}};
        if (has_dims_) {
            pv.fixed_w = length_;
            pv.fixed_h = width_;
        }
        pv.rect_rotation = rotation_;
        pv.rect_fillet = fillet_r_;
        pv.rect_chamfer_d1 = chamfer_d1_;
        pv.rect_chamfer_d2 = chamfer_d2_;
        // Every state except the corner pick is a single scalar/keyword sub-prompt:
        // with DYN on it shows the at-cursor cell, not the two-field corner drag.
        pv.scalar_prompt = state_ != State::First && state_ != State::AwaitCorner;
        ctx.set_preview(pv);
    };
    // Commit the closed polyline through the outline the rubber band draws (the corners
    // rounded or chamfered by the routines FILLET / CHAMFER use, so they cannot disagree).
    // A treatment that does not fit falls back to square corners and says so, which is
    // what AutoCAD does with an oversized radius.
    const auto commit = [&](core::Vec2 other) {
        std::vector<core::Vec2> c;
        std::vector<double> bulges;
        if (!core::polyline_ops::rectangle_outline(first_, other, rotation_, fillet_r_, chamfer_d1_,
                                                   chamfer_d2_, c, bulges)) {
            ctx.echo(fillet_r_ > 0.0
                         ? "Fillet radius too large for this rectangle: drawn with square corners."
                         : "Chamfer distances too large for this rectangle: drawn with square corners.");
        }
        core::AddPolylineCommand poly;
        if (line_width_ > 0.0) {
            poly.widths.assign(2 * c.size(), line_width_);
        }
        poly.elevation = elevation_;
        poly.thickness = thickness_;
        poly.points = std::move(c);
        poly.closed = true;
        poly.group = ctx.group_id();
        poly.bulges = std::move(bulges);
        ctx.submit(std::move(poly));
        ctx.echo("Rectangle created.");
        s_rotation_ = rotation_;
        done_ = true;
    };
    // The area the corner treatment removes: four quarter-round cut-outs, or four
    // chamfer triangles -- AutoCAD's Area option means the area of the finished shape.
    const auto corner_loss = [&] {
        if (fillet_r_ > 0.0) {
            return (4.0 - core::kPi) * fillet_r_ * fillet_r_;
        }
        return 2.0 * chamfer_d1_ * chamfer_d2_;
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
        if (up == "W" || up == "WIDTH") {
            state_ = State::LineWidth;
            ctx.set_prompt("Specify line width for rectangles <" + fmt4(line_width_) + ">: ");
            return;
        }
        if (up == "E" || up == "ELEVATION") {
            state_ = State::Elevation;
            ctx.set_prompt("Specify the elevation for rectangles <" + fmt4(elevation_) + ">: ");
            return;
        }
        if (up == "T" || up == "THICKNESS") {
            state_ = State::Thickness;
            ctx.set_prompt("Specify thickness for rectangles <" + fmt4(thickness_) + ">: ");
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

    case State::LineWidth: {
        double v = line_width_;
        if (!up.empty() && (!parse_number(text, v) || v < 0.0)) {
            ctx.echo("Enter a width of zero or more.");
            return;
        }
        line_width_ = v;
        s_line_width_ = v;
        state_ = State::First;
        ctx.set_prompt(first_prompt());
        return;
    }
    case State::Elevation: {
        double v = elevation_;
        if (!up.empty() && !parse_number(text, v)) {
            ctx.echo("Requires numeric distance.");
            return;
        }
        elevation_ = v;
        s_elevation_ = v;
        state_ = State::First;
        ctx.set_prompt(first_prompt());
        return;
    }
    case State::Thickness: {
        double v = thickness_;
        if (!up.empty() && !parse_number(text, v)) {
            ctx.echo("Requires numeric distance.");
            return;
        }
        thickness_ = v;
        s_thickness_ = v;
        state_ = State::First;
        ctx.set_prompt(first_prompt());
        return;
    }
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
        ctx.set_prompt(first_prompt());
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
        ctx.set_prompt(first_prompt());
        return;
    }

    case State::AwaitCorner: {
        if (up == "D" || up == "DIMENSIONS") {
            state_ = State::DimLen;
            refresh_preview();
            ctx.set_prompt("Specify length for rectangles <" + fmt(s_length_) + ">: ");
            return;
        }
        if (up == "A" || up == "AREA") {
            state_ = State::AreaVal;
            refresh_preview();
            ctx.set_prompt("Enter area of rectangle in current units <" + fmt(s_area_) + ">: ");
            return;
        }
        if (up == "R" || up == "ROTATION") {
            state_ = State::RotVal;
            refresh_preview();
            ctx.set_prompt("Specify rotation angle or [Pick points] <" +
                           core::units::format_angle(rotation_, ctx.units()) + ">: ");
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
        double v = s_length_; // Enter keeps the remembered default
        if (!up.empty() && (!parse_number(text, v) || v <= 0.0)) {
            revert_to_corner();
            return;
        }
        length_ = v;
        s_length_ = v;
        state_ = State::DimWid;
        refresh_preview();
        ctx.set_prompt("Specify width for rectangles <" + fmt(s_width_) + ">: ");
        return;
    }
    case State::DimWid: {
        double v = s_width_;
        if (!up.empty() && (!parse_number(text, v) || v <= 0.0)) {
            revert_to_corner();
            return;
        }
        width_ = v;
        s_width_ = v;
        has_dims_ = true;
        revert_to_corner(); // back to the corner pick, now with a fixed-size preview
        return;
    }

    case State::AreaVal: {
        double v = s_area_;
        if (!up.empty() && (!parse_number(text, v) || v <= 0.0)) {
            revert_to_corner();
            return;
        }
        area_ = v;
        s_area_ = v;
        state_ = State::AreaSide;
        refresh_preview();
        ctx.set_prompt("Calculate rectangle dimensions based on [Length/Width] <Length>: ");
        return;
    }
    case State::AreaSide:
        area_by_length_ = !(up == "W" || up == "WIDTH"); // default + L/Length -> length
        state_ = State::AreaSideVal;
        refresh_preview();
        ctx.set_prompt(area_by_length_ ? "Enter rectangle length <" + fmt(s_length_) + ">: "
                                       : "Enter rectangle width <" + fmt(s_width_) + ">: ");
        return;
    case State::AreaSideVal: {
        double v = area_by_length_ ? s_length_ : s_width_;
        if (!up.empty() && (!parse_number(text, v) || v <= 0.0)) {
            revert_to_corner();
            return;
        }
        // The other side from the area of the finished shape: the corner treatment's
        // cut-outs are added back before dividing.
        const double gross = area_ + corner_loss();
        if (area_by_length_) {
            length_ = v;
            width_ = gross / v;
        } else {
            width_ = v;
            length_ = gross / v;
        }
        if (!(length_ > 0.0) || !(width_ > 0.0)) {
            ctx.echo("The area is too small for that side with the current corner treatment.");
            revert_to_corner();
            return;
        }
        s_length_ = length_;
        s_width_ = width_;
        has_dims_ = true;
        revert_to_corner();
        return;
    }

    case State::RotVal: {
        if (up == "P" || up == "PICK" || up == "PICK POINTS") {
            state_ = State::RotPick1;
            ctx.set_prompt("Specify first point: ");
            return;
        }
        double deg = 0.0;
        if (up.empty()) {
            // Enter keeps the rotation in force
        } else if (parse_number(text, deg)) {
            rotation_ = core::to_radians(deg);
        } else if (const auto p = read_point(ctx, text)) {
            rotation_ = std::atan2(p->y - first_.y, p->x - first_.x); // the bearing from the corner
        }
        s_rotation_ = rotation_;
        revert_to_corner(); // anything else leaves the rotation unchanged
        return;
    }
    case State::RotPick1:
        if (const auto p = read_point(ctx, text)) {
            rot_p1_ = *p;
            ctx.set_last_point(*p);
            state_ = State::RotPick2;
            ctx.set_preview({PreviewKind::Segment, {rot_p1_}});
            ctx.set_prompt("Specify second point: ");
        }
        return;
    case State::RotPick2:
        if (const auto p = read_point(ctx, text)) {
            rotation_ = std::atan2(p->y - rot_p1_.y, p->x - rot_p1_.x);
            s_rotation_ = rotation_;
            ctx.set_last_point(first_);
            revert_to_corner();
        }
        return;
    }
}

void RectangleCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// ERASE
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// "Select objects:" (issue #46) -- the step every edit command starts with
// ---------------------------------------------------------------------------
void SelectObjectsPhase::begin(CommandContext& ctx, std::string prompt) {
    prompt_ = std::move(prompt);
    sub_ = Sub::Objects;
    active_ = true;
    remove_ = false;
    single_ = false;
    box_ = false;
    crossing_ = false;
    pts_.clear();
    reprompt(ctx);
}

void SelectObjectsPhase::reprompt(CommandContext& ctx) {
    ctx.set_prompt(remove_ ? "Remove objects: " : prompt_);
}

SelectObjectsPhase::Result SelectObjectsPhase::gesture(CommandContext& ctx) {
    if (single_) {
        active_ = false;
        ctx.clear_preview();
        return Result::Done;
    }
    return Result::Continue;
}

SelectObjectsPhase::Result SelectObjectsPhase::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (sub_) {
    case Sub::Corner1:
        if (const auto p = read_point(ctx, text)) {
            c1_ = *p;
            sub_ = Sub::Corner2;
            ctx.set_preview({PreviewKind::Rectangle, {c1_}});
            ctx.set_prompt("Specify opposite corner: ");
        }
        return Result::Continue;
    case Sub::Corner2:
        if (const auto p = read_point(ctx, text)) {
            const core::Vec2 mn{std::min(c1_.x, p->x), std::min(c1_.y, p->y)};
            const core::Vec2 mx{std::max(c1_.x, p->x), std::max(c1_.y, p->y)};
            const bool crossing = box_ ? p->x < c1_.x : crossing_; // BOX: by direction
            ctx.submit(core::SelectWindowCommand{mn, mx, crossing, true, true, remove_});
            ctx.clear_preview();
            sub_ = Sub::Objects;
            reprompt(ctx);
            return gesture(ctx);
        }
        return Result::Continue;
    case Sub::Fence:
    case Sub::Polygon: {
        const bool fence = sub_ == Sub::Fence;
        if (t.empty()) {
            if (fence && pts_.size() >= 2) {
                ctx.submit(core::SelectFenceCommand{pts_, remove_, true});
            } else if (!fence && pts_.size() >= 3) {
                ctx.submit(core::SelectPolygonCommand{pts_, crossing_, remove_, true});
            }
            pts_.clear();
            ctx.clear_preview();
            sub_ = Sub::Objects;
            reprompt(ctx);
            return gesture(ctx);
        }
        if (u == "U" || u == "UNDO") {
            if (!pts_.empty()) {
                pts_.pop_back();
            }
        } else if (const auto p = read_point(ctx, text)) {
            pts_.push_back(*p);
            ctx.set_last_point(*p);
        } else {
            return Result::Continue;
        }
        if (pts_.empty()) {
            ctx.clear_preview();
            ctx.set_prompt(fence ? "Specify first fence point or pick/drag cursor: "
                                 : "First polygon point or pick/drag cursor: ");
        } else {
            PreviewSpec pv;
            pv.kind = PreviewKind::Polyline;
            pv.points = pts_;
            ctx.set_preview(pv);
            ctx.set_prompt(fence ? "Specify next fence point or [Undo]: " : "Specify endpoint of line or [Undo]: ");
        }
        return Result::Continue;
    }
    case Sub::GroupName:
        if (!t.empty()) {
            ctx.submit(core::SelectGroupCommand{t, remove_, true});
        }
        sub_ = Sub::Objects;
        reprompt(ctx);
        return Result::Continue;
    case Sub::Objects:
        break;
    }
    if (t.empty()) {
        active_ = false;
        ctx.clear_preview();
        return Result::Done;
    }
    if (u == "W" || u == "WINDOW" || u == "C" || u == "CROSSING" || u == "BOX") {
        crossing_ = u == "C" || u == "CROSSING";
        box_ = u == "BOX";
        sub_ = Sub::Corner1;
        ctx.set_prompt("Specify first corner: ");
    } else if (u == "ALL") {
        ctx.submit(core::SelectAllCommand{true});
        return gesture(ctx);
    } else if (u == "F" || u == "FENCE") {
        sub_ = Sub::Fence;
        pts_.clear();
        ctx.set_prompt("Specify first fence point or pick/drag cursor: ");
    } else if (u == "WP" || u == "WPOLYGON" || u == "CP" || u == "CPOLYGON") {
        crossing_ = u == "CP" || u == "CPOLYGON";
        sub_ = Sub::Polygon;
        pts_.clear();
        ctx.set_prompt("First polygon point or pick/drag cursor: ");
    } else if (u == "G" || u == "GROUP") {
        sub_ = Sub::GroupName;
        ctx.set_prompt("Enter group name: ");
    } else if (u == "L" || u == "LAST") {
        ctx.submit(core::SelectLastCommand{remove_, true});
        return gesture(ctx);
    } else if (u == "P" || u == "PREVIOUS") {
        ctx.submit(core::SelectPreviousCommand{true});
        return gesture(ctx);
    } else if (u == "A" || u == "ADD") {
        remove_ = false;
        reprompt(ctx);
    } else if (u == "R" || u == "REMOVE") {
        remove_ = true;
        reprompt(ctx);
    } else if (u == "M" || u == "MULTIPLE" || u == "AU" || u == "AUTO") {
        reprompt(ctx); // the default way of working here
    } else if (u == "U" || u == "UNDO") {
        ctx.submit(core::SelectUndoCommand{});
        reprompt(ctx);
    } else if (u == "SI" || u == "SINGLE") {
        single_ = true;
        reprompt(ctx);
    } else if (const auto p = read_point(ctx, text)) {
        ctx.submit(core::SelectPickCommand{*p, ctx.pick_radius(), true, true, remove_});
        reprompt(ctx);
        return gesture(ctx);
    } else {
        ctx.echo("Expects a point or Window/Last/Crossing/BOX/ALL/Fence/WPolygon/CPolygon/Group/"
                 "Add/Remove/Multiple/Previous/Undo/AUto/SIngle");
        reprompt(ctx);
    }
    return Result::Continue;
}

// ---------------------------------------------------------------------------
// ERASE / OOPS
// ---------------------------------------------------------------------------
void EraseCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (ctx.has_selection()) {
        ctx.submit(core::EraseSelectionCommand{ctx.group_id()});
        done_ = true;
        return;
    }
    select_.begin(ctx);
}

void EraseCommand::finish(CommandContext& ctx) {
    if (ctx.has_selection()) {
        ctx.submit(core::EraseSelectionCommand{ctx.group_id()});
    }
    done_ = true;
}

void EraseCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void EraseCommand::input(CommandContext& ctx, const std::string& text) {
    if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void OopsCommand::start(CommandContext& ctx) {
    ctx.submit(core::OopsCommand{ctx.group_id()});
    done_ = true;
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
    ctx.set_prompt("Specify corner of window, enter a scale factor (nX or nXP), or "
                   "[All/Center/Dynamic/Extents/Previous/Scale/Window/Object] <real time>: ");
}

void ZoomCommand::remember_view(CommandContext& ctx) {
    core::Vec2 c;
    double s = 0.0;
    if (ctx.view() != nullptr && ctx.view()->current_view(c, s) && s > 0.0) {
        s_previous_.emplace_back(c, s);
        if (s_previous_.size() > 10) {
            s_previous_.erase(s_previous_.begin());
        }
    }
}

bool ZoomCommand::apply_scale(CommandContext& ctx, const std::string& u0) {
    // nX: relative to the view now; nXP: the same here (paper space is one view); n: relative
    // to the drawing limits filling the view.
    std::string u = u0;
    bool relative = false;
    if (u.size() > 2 && u.compare(u.size() - 2, 2, "XP") == 0) {
        u.resize(u.size() - 2);
        relative = true;
    } else if (!u.empty() && u.back() == 'X') {
        u.pop_back();
        relative = true;
    }
    double factor = 0.0;
    if (!parse_number(u, factor) || factor <= 0.0) {
        return false;
    }
    ViewControl* v = ctx.view();
    remember_view(ctx);
    if (relative) {
        if (v != nullptr) {
            v->zoom_scale(factor);
        }
    } else if (v != nullptr) {
        int w = 0;
        int h = 0;
        core::Vec2 c;
        double s = 0.0;
        const CommandContext::Limits lim = ctx.limits();
        const core::Vec2 size = lim.max - lim.min;
        if (v->viewport_size(w, h) && v->current_view(c, s) && size.x > 0.0 && size.y > 0.0) {
            const double fit = std::min(w / size.x, h / size.y);
            v->set_view((lim.min + lim.max) * 0.5, fit * factor);
        }
    }
    ctx.echo("Zoom " + fmt4(factor) + (relative ? "x." : "."));
    done_ = true;
    return true;
}

void ZoomCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    ViewControl* v = ctx.view();
    switch (step_) {
    case Step::Main:
        if (u.empty()) {
            done_ = true; // real time: the wheel and a drag do it
            return;
        }
        if (u == "E" || u == "EXTENTS" || u == "A" || u == "ALL") {
            remember_view(ctx);
            if (v != nullptr) {
                v->zoom_extents();
            }
            ctx.echo("Zoom extents.");
            done_ = true;
            return;
        }
        if (u == "P" || u == "PREVIOUS") {
            if (s_previous_.empty()) {
                ctx.echo("No previous view saved.");
            } else {
                const auto [c, s] = s_previous_.back();
                s_previous_.pop_back();
                if (v != nullptr) {
                    v->set_view(c, s);
                }
            }
            done_ = true;
            return;
        }
        if (u == "W" || u == "WINDOW" || u == "D" || u == "DYNAMIC" || u == "O" || u == "OBJECT") {
            if (u[0] == 'D') {
                ctx.echo("Dynamic zoom is not available; specify a window.");
            } else if (u[0] == 'O') {
                ctx.echo("Zoom Object is not available yet; specify a window.");
            }
            step_ = Step::WindowFirst;
            ctx.set_prompt("Specify first corner: ");
            return;
        }
        if (u == "C" || u == "CENTER") {
            step_ = Step::Center;
            ctx.set_prompt("Specify center point: ");
            return;
        }
        if (u == "S" || u == "SCALE") {
            step_ = Step::Scale;
            ctx.set_prompt("Enter a scale factor (nX or nXP): ");
            return;
        }
        if (t.find(',') != std::string::npos || ctx.input_is_pick()) {
            if (const auto p = read_point(ctx, text)) {
                corner_ = *p; // a window's first corner, given at once
                step_ = Step::WindowSecond;
                ctx.set_preview(PreviewSpec{PreviewKind::Rectangle, {corner_}});
                ctx.set_prompt("Specify opposite corner: ");
            }
            return;
        }
        if (!apply_scale(ctx, u)) {
            ctx.echo("Requires a point, a scale factor (nX or nXP), or an option keyword.");
        }
        return;
    case Step::WindowFirst:
        if (const auto p = read_point(ctx, text)) {
            corner_ = *p;
            step_ = Step::WindowSecond;
            ctx.set_preview(PreviewSpec{PreviewKind::Rectangle, {corner_}});
            ctx.set_prompt("Specify opposite corner: ");
        }
        return;
    case Step::WindowSecond:
        if (const auto p = read_point(ctx, text)) {
            ctx.clear_preview();
            const core::Vec2 d{std::abs(p->x - corner_.x), std::abs(p->y - corner_.y)};
            int w = 0;
            int h = 0;
            if (v != nullptr && v->viewport_size(w, h) && d.x > 1e-12 && d.y > 1e-12) {
                remember_view(ctx);
                v->set_view((corner_ + *p) * 0.5, std::min(w / d.x, h / d.y));
            }
            done_ = true;
        }
        return;
    case Step::Center:
        if (const auto p = read_point(ctx, text)) {
            corner_ = *p;
            core::Vec2 c;
            double s = 0.0;
            int w = 0;
            int h = 0;
            const bool have = v != nullptr && v->current_view(c, s) && v->viewport_size(w, h) && s > 0.0;
            step_ = Step::CenterSize;
            ctx.set_prompt("Enter magnification or height <" + fmt4(have ? h / s : 0.0) + ">: ");
        }
        return;
    case Step::CenterSize: {
        core::Vec2 c;
        double s = 0.0;
        int w = 0;
        int h = 0;
        if (v == nullptr || !v->current_view(c, s) || !v->viewport_size(w, h) || s <= 0.0) {
            done_ = true;
            return;
        }
        double scale = s;
        if (!t.empty()) {
            std::string n = u;
            const bool times = !n.empty() && n.back() == 'X';
            if (times) {
                n.pop_back();
            }
            double val = 0.0;
            if (!parse_number(n, val) || val <= 0.0) {
                ctx.echo("Requires a positive magnification (nX) or height.");
                return;
            }
            scale = times ? s * val : h / val;
        }
        remember_view(ctx);
        v->set_view(corner_, scale);
        done_ = true;
        return;
    }
    case Step::Scale:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (!apply_scale(ctx, u)) {
            ctx.echo("Requires a scale factor (nX or nXP).");
        }
        return;
    }
}

void PanCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (!typed_) {
        ctx.echo("Press Esc or Enter to exit. Drag with the middle button, or hold the wheel, to pan.");
        ctx.set_prompt("Pan: ");
        return;
    }
    ctx.set_prompt("Specify base point or displacement: ");
}

void PanCommand::input(CommandContext& ctx, const std::string& text) {
    if (!typed_ || trimmed(text).empty()) {
        done_ = true;
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    if (!base_) {
        base_ = *p;
        ctx.set_last_point(*p);
        ctx.set_preview(PreviewSpec{PreviewKind::Segment, {*p}});
        ctx.set_prompt("Specify second point: ");
        return;
    }
    ctx.clear_preview();
    core::Vec2 c;
    double s = 0.0;
    if (ctx.view() != nullptr && ctx.view()->current_view(c, s)) {
        ZoomCommand::remember_view(ctx);
        ctx.view()->set_view(c - (*p - *base_), s); // the drawing moves by the displacement
    }
    done_ = true;
}

void PanCommand::cancel(CommandContext& ctx) {
    ctx.clear_preview();
    done_ = true;
}

void ZoomCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// MOVE
// ---------------------------------------------------------------------------
namespace {
std::string displacement_default(CommandContext& ctx, core::Vec2 d) {
    return "<" + core::units::format_length(d.x, ctx.units()) + ", " +
           core::units::format_length(d.y, ctx.units()) + ", " + core::units::format_length(0.0, ctx.units()) + ">";
}
} // namespace

void MoveCommand::begin_base(CommandContext& ctx) {
    state_ = State::Base;
    ctx.set_prompt("Specify base point or [Displacement] <Displacement>: ");
}

void MoveCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (!ctx.has_selection()) {
        state_ = State::Select; // verb-noun: gather the set first
        select_.begin(ctx);
        return;
    }
    begin_base(ctx);
}

void MoveCommand::selection_gesture(CommandContext& ctx) {
    if (state_ == State::Select && select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        input(ctx, "");
    }
}

void MoveCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Select:
        if (select_.active() && select_.input(ctx, text) == SelectObjectsPhase::Result::Continue) {
            return;
        }
        if (!ctx.has_selection()) {
            done_ = true; // nothing chosen: the command ends, as AutoCAD's does
            return;
        }
        begin_base(ctx);
        return;
    case State::Base:
        if (t.empty() || u == "D" || u == "DISPLACEMENT") {
            state_ = State::Displacement;
            ctx.set_prompt("Specify displacement " + displacement_default(ctx, s_displacement_) + ": ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            base_ = *p;
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Move, {*p}});
            state_ = State::Second;
            ctx.set_prompt("Specify second point or <use first point as displacement>: ");
        }
        return;
    case State::Displacement: {
        // A coordinate here is the vector itself (x,y), not a point in the drawing.
        core::Vec2 d = s_displacement_;
        if (!t.empty()) {
            const auto p = read_point(ctx, text);
            if (!p) {
                return;
            }
            d = *p;
        }
        s_displacement_ = d;
        ctx.submit(core::MoveSelectionCommand{d, ctx.group_id()});
        ctx.echo("Moved.");
        done_ = true;
        return;
    }
    case State::Second: {
        core::Vec2 d;
        if (t.empty()) {
            d = *base_; // the first point, taken as a displacement from the origin
        } else if (const auto p = read_point(ctx, text)) {
            d = *p - *base_;
        } else {
            return;
        }
        s_displacement_ = d;
        ctx.submit(core::MoveSelectionCommand{d, ctx.group_id()});
        ctx.echo("Moved.");
        done_ = true;
        return;
    }
    }
}

void MoveCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// COPY (leaves originals; Multiple repeats until Enter/Esc, Single places one)
// ---------------------------------------------------------------------------
void CopyCommand::begin_base(CommandContext& ctx) {
    state_ = State::Base;
    ctx.echo(std::string("Current settings: Copy mode = ") + (s_single_ ? "Single" : "Multiple"));
    ctx.set_prompt("Specify base point or [Displacement/mOde] <Displacement>: ");
}

void CopyCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (!ctx.has_selection()) {
        state_ = State::Select;
        select_.begin(ctx);
        return;
    }
    begin_base(ctx);
}

void CopyCommand::selection_gesture(CommandContext& ctx) {
    if (state_ == State::Select && select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        input(ctx, "");
    }
}

void CopyCommand::prompt_second(CommandContext& ctx) {
    state_ = State::Second;
    ctx.set_preview({PreviewKind::Move, {*base_}});
    ctx.set_prompt(placed_ == 0 ? "Specify second point or [Array] <use first point as displacement>: "
                                : "Specify second point or [Array/Exit/Undo] <Exit>: ");
}

void CopyCommand::place(CommandContext& ctx, core::Vec2 delta) {
    if (placed_ > 0) {
        (void)ctx.new_group(); // every placement its own undo step
    }
    ctx.submit(core::CopySelectionCommand{delta, ctx.group_id()});
    ++placed_;
    s_displacement_ = delta;
}

void CopyCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    double v = 0.0;
    switch (state_) {
    case State::Select:
        if (select_.active() && select_.input(ctx, text) == SelectObjectsPhase::Result::Continue) {
            return;
        }
        if (!ctx.has_selection()) {
            done_ = true;
            return;
        }
        begin_base(ctx);
        return;
    case State::Base:
        if (u == "O" || u == "MODE") {
            state_ = State::Mode;
            ctx.set_prompt(std::string("Enter a copy mode option [Single/Multiple] <") +
                           (s_single_ ? "Single" : "Multiple") + ">: ");
            return;
        }
        if (t.empty() || u == "D" || u == "DISPLACEMENT") {
            state_ = State::Displacement;
            ctx.set_prompt("Specify displacement " + displacement_default(ctx, s_displacement_) + ": ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            base_ = *p;
            ctx.set_last_point(*p);
            prompt_second(ctx);
        }
        return;
    case State::Mode:
        if (t.empty()) {
            // keeps the mode
        } else if (u == "S" || u == "SINGLE") {
            s_single_ = true;
        } else if (u == "M" || u == "MULTIPLE") {
            s_single_ = false;
        } else {
            ctx.echo("Enter Single or Multiple.");
            return;
        }
        state_ = State::Base;
        ctx.set_prompt("Specify base point or [Displacement/mOde] <Displacement>: ");
        return;
    case State::Displacement: {
        core::Vec2 d = s_displacement_;
        if (!t.empty()) {
            const auto p = read_point(ctx, text);
            if (!p) {
                return;
            }
            d = *p;
        }
        place(ctx, d);
        ctx.echo("Copy placed.");
        done_ = true;
        return;
    }
    case State::Second:
        if (t.empty() || u == "E" || u == "EXIT") {
            if (placed_ == 0 && t.empty()) {
                place(ctx, *base_); // the first point as a displacement from the origin
                ctx.echo("Copy placed.");
            }
            done_ = true;
            return;
        }
        if (u == "A" || u == "ARRAY") {
            state_ = State::ArrayCount;
            ctx.set_prompt("Enter number of items to array: ");
            return;
        }
        if ((u == "U" || u == "UNDO") && placed_ > 0) {
            ctx.submit(core::UndoLastGroupCommand{ctx.command_group()});
            --placed_;
            prompt_second(ctx);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            place(ctx, *p - *base_);
            ctx.echo("Copy placed.");
            if (s_single_) {
                done_ = true;
                return;
            }
            prompt_second(ctx);
        }
        return;
    case State::ArrayCount:
        if (!parse_number(t, v) || v < 2.0) {
            ctx.echo("Requires an integer value of 2 or more.");
            return;
        }
        array_count_ = static_cast<int>(v);
        state_ = State::ArrayEnd;
        ctx.set_prompt("Specify second point or [Fit]: ");
        return;
    case State::ArrayEnd:
    case State::ArrayFit: {
        if (state_ == State::ArrayEnd && (u == "F" || u == "FIT")) {
            state_ = State::ArrayFit;
            ctx.set_prompt("Specify second point or [Array]: ");
            return;
        }
        if (state_ == State::ArrayFit && (u == "A" || u == "ARRAY")) {
            state_ = State::ArrayEnd;
            ctx.set_prompt("Specify second point or [Fit]: ");
            return;
        }
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        // Array: `count` items counting the original, the vector between neighbours
        // being the pick (or, with Fit, the pick is where the last item lands).
        const core::Vec2 step = state_ == State::ArrayFit ? (*p - *base_) * (1.0 / (array_count_ - 1))
                                                          : (*p - *base_);
        for (int k = 1; k < array_count_; ++k) {
            place(ctx, step * static_cast<double>(k));
        }
        ctx.echo(std::to_string(array_count_ - 1) + " copies placed.");
        if (s_single_) {
            done_ = true;
            return;
        }
        prompt_second(ctx);
        return;
    }
    }
}

void CopyCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// MIRROR
// ---------------------------------------------------------------------------
void MirrtextCommand::start(CommandContext& ctx) {
    ctx.set_prompt(std::string("Enter new value for MIRRTEXT <") + (s_value_ ? "1" : "0") + ">: ");
}

void MirrtextCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (!t.empty()) {
        if (t != "0" && t != "1") {
            ctx.echo("Requires 0 or 1.");
            return;
        }
        s_value_ = t == "1";
        ctx.submit(core::SetMirrtextCommand{s_value_});
    }
    done_ = true;
}

void MirrtextCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void MirrorCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (!ctx.has_selection()) {
        state_ = State::Select;
        select_.begin(ctx);
        return;
    }
    state_ = State::First;
    ctx.set_prompt("Specify first point of mirror line: ");
}

void MirrorCommand::selection_gesture(CommandContext& ctx) {
    if (state_ == State::Select && select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        input(ctx, "");
    }
}

void MirrorCommand::input(CommandContext& ctx, const std::string& text) {
    if (state_ == State::Select) {
        if (select_.active() && select_.input(ctx, text) == SelectObjectsPhase::Result::Continue) {
            return;
        }
        if (!ctx.has_selection()) {
            done_ = true;
            return;
        }
        state_ = State::First;
        ctx.set_prompt("Specify first point of mirror line: ");
        return;
    }
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
void OffsetCommand::prompt_distance(CommandContext& ctx) {
    state_ = State::Distance;
    ctx.set_prompt("Specify offset distance or [Through/Erase/Layer] <" +
                   (s_distance_ > 0.0 ? core::units::format_length(s_distance_, ctx.units()) : std::string("Through")) +
                   ">: ");
}

void OffsetCommand::show_preview(CommandContext& ctx, bool from_last) {
    core::OffsetPreviewCommand p;
    p.pick = object_pick_;
    p.radius = ctx.pick_radius();
    p.distance = s_distance_;
    p.through = s_distance_ <= 0.0;
    p.from_last = from_last;
    p.gap_type = static_cast<std::uint8_t>(s_gap_type_);
    p.active = true;
    ctx.submit(p);
    previewing_ = true;
}

void OffsetCommand::end_preview(CommandContext& ctx) {
    if (previewing_) {
        core::OffsetPreviewCommand p;
        p.active = false;
        ctx.submit(p);
        previewing_ = false;
    }
}

void OffsetCommand::prompt_object(CommandContext& ctx) {
    state_ = State::Object;
    ctx.clear_preview();
    end_preview(ctx);
    ctx.set_prompt("Select object to offset or [Exit/Undo] <Exit>: ");
}

void OffsetCommand::prompt_side(CommandContext& ctx) {
    state_ = State::Side;
    ctx.set_prompt(s_distance_ > 0.0 ? "Specify point on side to offset or [Exit/Multiple/Undo] <Exit>: "
                                     : "Specify through point or [Exit/Multiple/Undo] <Exit>: ");
    show_preview(ctx, false);
}

void OffsetCommand::start(CommandContext& ctx) {
    ctx.echo(std::string("Current settings: Erase source=") + (s_erase_ ? "Yes" : "No") +
             "  Layer=" + (s_layer_current_ ? "Current" : "Source") +
             "  OFFSETGAPTYPE=" + std::to_string(s_gap_type_));
    prompt_distance(ctx);
}

void OffsetGapTypeCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Enter new value for OFFSETGAPTYPE <" + std::to_string(OffsetCommand::s_gap_type_) +
                   ">: ");
}

void OffsetGapTypeCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (!t.empty()) {
        if (t != "0" && t != "1" && t != "2") {
            ctx.echo("Requires an integer between 0 and 2.");
            return;
        }
        OffsetCommand::s_gap_type_ = t[0] - '0';
    }
    done_ = true;
}

void OffsetGapTypeCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void OffsetCommand::place(CommandContext& ctx, core::Vec2 side, bool from_last) {
    if (placed_ > 0) {
        (void)ctx.new_group(); // every offset its own undo step
    }
    core::OffsetPickCommand c{object_pick_, ctx.pick_radius(), s_distance_, side, ctx.group_id()};
    c.through = s_distance_ <= 0.0;
    c.erase_source = s_erase_;
    c.to_current_layer = s_layer_current_;
    c.from_last = from_last;
    c.gap_type = static_cast<std::uint8_t>(s_gap_type_);
    ctx.submit(c);
    ++placed_;
}

void OffsetCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    double v = 0.0;
    switch (state_) {
    case State::Distance:
        if (t.empty()) {
            prompt_object(ctx); // the remembered distance (or Through)
            return;
        }
        if (u == "T" || u == "THROUGH") {
            s_distance_ = 0.0;
            prompt_object(ctx);
            return;
        }
        if (u == "E" || u == "ERASE") {
            state_ = State::Erase;
            ctx.set_prompt(std::string("Erase source object after offsetting? [Yes/No] <") + (s_erase_ ? "Yes" : "No") + ">: ");
            return;
        }
        if (u == "L" || u == "LAYER") {
            state_ = State::Layer;
            ctx.set_prompt(std::string("Enter layer option for offset objects [Current/Source] <") +
                           (s_layer_current_ ? "Current" : "Source") + ">: ");
            return;
        }
        if (parse_number(t, v)) {
            if (!(v > 0.0)) {
                ctx.echo("Value must be positive and nonzero.");
                return;
            }
            s_distance_ = v;
            prompt_object(ctx);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            first_ = *p; // two points give the distance
            ctx.set_last_point(*p);
            state_ = State::Second;
            ctx.set_preview({PreviewKind::Segment, {*p}});
            ctx.set_prompt("Specify second point: ");
        }
        return;
    case State::Second:
        if (const auto p = read_point(ctx, text)) {
            const double d = core::distance(first_, *p);
            if (!(d > 0.0)) {
                ctx.echo("Value must be positive and nonzero.");
                return;
            }
            s_distance_ = d;
            prompt_object(ctx);
        }
        return;
    case State::Erase:
        if (t.empty()) {
            // keeps the setting
        } else if (u == "Y" || u == "YES") {
            s_erase_ = true;
        } else if (u == "N" || u == "NO") {
            s_erase_ = false;
        } else {
            ctx.echo("Enter Yes or No.");
            return;
        }
        prompt_distance(ctx);
        return;
    case State::Layer:
        if (t.empty()) {
            // keeps the setting
        } else if (u == "C" || u == "CURRENT") {
            s_layer_current_ = true;
        } else if (u == "S" || u == "SOURCE") {
            s_layer_current_ = false;
        } else {
            ctx.echo("Enter Current or Source.");
            return;
        }
        prompt_distance(ctx);
        return;
    case State::Object:
        if (t.empty() || u == "E" || u == "EXIT") {
            done_ = true;
            return;
        }
        if (u == "U" || u == "UNDO") {
            if (placed_ == 0) {
                ctx.echo("Nothing to undo.");
                return;
            }
            ctx.submit(core::UndoLastGroupCommand{ctx.command_group()});
            --placed_;
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            object_pick_ = *p;
            from_last_multiple_ = false;
            prompt_side(ctx);
        }
        return;
    case State::Side:
    case State::Multiple:
        if (t.empty() || u == "E" || u == "EXIT") {
            if (state_ == State::Multiple) {
                prompt_object(ctx); // Multiple ends back at the object prompt
                return;
            }
            end_preview(ctx);
            done_ = true;
            return;
        }
        if (u == "U" || u == "UNDO") {
            if (placed_ == 0) {
                ctx.echo("Nothing to undo.");
                return;
            }
            ctx.submit(core::UndoLastGroupCommand{ctx.command_group()});
            --placed_;
            return;
        }
        if ((u == "M" || u == "MULTIPLE") && state_ == State::Side) {
            state_ = State::Multiple;
            ctx.set_prompt(s_distance_ > 0.0 ? "Specify point on side to offset or [Exit/Undo] <Exit>: "
                                             : "Specify through point or [Exit/Undo] <Exit>: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            if (state_ == State::Multiple) {
                // Each further offset steps out from the one just made.
                place(ctx, *p, placed_ > 0 && from_last_multiple_);
                from_last_multiple_ = true;
                show_preview(ctx, true); // the next one steps out from this one
                ctx.set_prompt(s_distance_ > 0.0 ? "Specify point on side to offset or [Exit/Undo] <Exit>: "
                                                 : "Specify through point or [Exit/Undo] <Exit>: ");
                return;
            }
            place(ctx, *p, false);
            prompt_object(ctx);
        }
        return;
    }
}

void OffsetCommand::cancel(CommandContext& ctx) {
    end_preview(ctx);
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
    ctx.set_prompt("Select source object or multiple objects to join at once: ");
}

void JoinCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        ctx.submit(core::JoinSelectionCommand{ctx.pick_radius(), ctx.group_id()});
        done_ = true;
    }
}

void JoinCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    if (state_ == State::Source) {
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        source_ = *p;
        const std::optional<core::EntityKind> k = ctx.input_is_pick() ? ctx.hovered_kind() : std::nullopt;
        closable_ = k == core::EntityKind::Arc || k == core::EntityKind::Ellipse;
        state_ = State::Targets;
        // The source first in the selection, then whatever is picked or windowed.
        ctx.submit(core::ClearSelectionCommand{});
        ctx.submit(core::SelectPickCommand{*p, ctx.pick_radius(), true, false});
        select_.begin(ctx, closable_ ? (k == core::EntityKind::Arc ? "Select arcs to join to source or [cLose]: "
                                                                   : "Select elliptical arcs to join to source or [cLose]: ")
                                     : "Select objects to join: ");
        return;
    }
    if (closable_ && (u == "L" || u == "CLOSE")) {
        core::JoinPickCommand c{{source_}, ctx.pick_radius(), ctx.group_id()};
        c.close = true;
        ctx.submit(std::move(c));
        done_ = true;
        return;
    }
    // Enter commits the join (the engine joins what is selected and reports).
    if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
        ctx.submit(core::JoinSelectionCommand{ctx.pick_radius(), ctx.group_id()});
        done_ = true;
    }
}

void JoinCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// HATCH / H (Part A: SOLID fill from the selected closed polyline boundaries)
// ---------------------------------------------------------------------------
void HatchCommand::main_prompt(CommandContext& ctx) {
    mode_ = Mode::PickPoint;
    ctx.set_prompt(typed_ ? "Specify internal point or [Properties/Select objects/draW boundary/remove Boundaries/Advanced/"
                            "DRaw order/Origin/ANnotative/hatch COlor/LAyer/Transparency]: "
                          : "Pick internal point or [Select objects/Undo/seTtings]: ");
}

void HatchCommand::start(CommandContext& ctx) {
    if (gradient_ && pattern_ != "GRADIENT") {
        pattern_ = "GRADIENT"; // GRADIENT: HATCH with the gradient fill, its colours and angle as last set
        ctx.echo("Pattern: GRADIENT");
    }
    // Noun-verb: with closed boundaries already selected, fill them immediately ("Select
    // objects" mode). Otherwise the default is AutoCAD's "Pick internal point".
    if (ctx.has_selection()) {
        ctx.submit(core::HatchFromSelectionCommand{pattern_, scale_, angle_, ctx.group_id(), color2_});
        done_ = true;
        return;
    }
    main_prompt(ctx);
}

void HatchCommand::fill_selection(CommandContext& ctx) {
    if (ctx.has_selection()) {
        ctx.submit(core::HatchFromSelectionCommand{pattern_, scale_, angle_, ctx.new_group(), color2_});
        ++picks_;
    }
    main_prompt(ctx);
}

void HatchCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        fill_selection(ctx);
    }
}

void HatchCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string up = upper(t);
    switch (mode_) {
    case Mode::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            fill_selection(ctx);
        }
        return;
    case Mode::Pattern: {
        if (t == "?") {
            std::string line = "Patterns: SOLID";
            for (const std::string& n : core::hatch::builtin_pattern_names()) {
                line += " " + n;
            }
            ctx.echo(line);
            return;
        }
        if (up == "U" || up == "USER DEFINED" || up == "USER") {
            mode_ = Mode::UserAngle;
            ctx.set_prompt("Specify angle for crosshatch lines <" + std::to_string(std::lround(core::to_degrees(angle_))) +
                           ">: ");
            return;
        }
        if (up == "G" || up == "GRADIENT") {
            mode_ = Mode::GradientColor;
            ctx.set_prompt("Specify second colour as r,g,b <" + std::to_string(color2_.r) + "," +
                           std::to_string(color2_.g) + "," + std::to_string(color2_.b) + ">: ");
            return;
        }
        if (up == "S" || up == "SOLID") {
            pattern_ = "SOLID";
            main_prompt(ctx);
            return;
        }
        if (!t.empty()) {
            if (core::hatch::builtin_pattern(up) == nullptr) {
                ctx.echo("Unknown pattern \"" + t + "\"; ? lists them.");
                return;
            }
            pattern_ = up;
        }
        if (pattern_ == "SOLID" || pattern_ == "GRADIENT") {
            main_prompt(ctx);
            return;
        }
        mode_ = Mode::Scale;
        ctx.set_prompt("Specify a scale for the pattern <" + fmt4(scale_) + ">: ");
        return;
    }
    case Mode::Scale:
    case Mode::Angle: {
        if (!t.empty()) {
            double v = 0.0;
            if (!parse_number(t, v) || (mode_ == Mode::Scale && v <= 1e-9)) {
                ctx.echo(mode_ == Mode::Scale ? "Requires a positive scale." : "Requires an angle in degrees.");
                return;
            }
            if (mode_ == Mode::Scale) {
                scale_ = v;
            } else {
                angle_ = core::to_radians(v);
            }
        }
        if (mode_ == Mode::Scale) {
            mode_ = Mode::Angle;
            ctx.set_prompt("Specify an angle for the pattern <" + std::to_string(std::lround(core::to_degrees(angle_))) +
                           ">: ");
            return;
        }
        main_prompt(ctx);
        return;
    }
    case Mode::UserAngle:
    case Mode::UserSpacing: {
        // User defined: parallel lines at an angle, a spacing apart (the pattern's scale),
        // crossed at right angles when doubled.
        if (!t.empty()) {
            double v = 0.0;
            if (!parse_number(t, v) || (mode_ == Mode::UserSpacing && v <= 1e-9)) {
                ctx.echo(mode_ == Mode::UserSpacing ? "Requires a positive spacing." : "Requires an angle in degrees.");
                return;
            }
            if (mode_ == Mode::UserAngle) {
                angle_ = core::to_radians(v);
            } else {
                scale_ = v;
            }
        }
        if (mode_ == Mode::UserAngle) {
            mode_ = Mode::UserSpacing;
            ctx.set_prompt("Specify spacing between the lines <" + fmt4(scale_) + ">: ");
            return;
        }
        mode_ = Mode::UserDouble;
        ctx.set_prompt(std::string("Double hatch area? [Yes/No] <") + (pattern_ == "_USERDOUBLE" ? "Y" : "N") + ">: ");
        return;
    }
    case Mode::UserDouble:
        if (up == "Y" || up == "YES") {
            pattern_ = "_USERDOUBLE";
        } else if (up == "N" || up == "NO" || (t.empty() && pattern_ != "_USERDOUBLE")) {
            pattern_ = "_USER";
        } else if (!t.empty()) {
            ctx.echo("Enter Yes or No.");
            return;
        }
        main_prompt(ctx);
        return;
    case Mode::GradientColor:
        // The second colour as r,g,b; Enter keeps the current one. The first colour is
        // the entity colour (ByLayer resolves through the layer), as AutoCAD's
        // one-colour gradient works.
        if (!t.empty()) {
            int r = 0;
            int g = 0;
            int b = 0;
            if (std::sscanf(t.c_str(), "%d,%d,%d", &r, &g, &b) != 3 || r < 0 || g < 0 || b < 0 || r > 255 || g > 255 ||
                b > 255) {
                ctx.echo("Enter the colour as r,g,b (0-255 each).");
                return;
            }
            color2_ = core::Rgb{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b)};
        }
        mode_ = Mode::GradientAngle;
        ctx.set_prompt("Specify gradient angle <" + std::to_string(std::lround(core::to_degrees(angle_))) + ">: ");
        return;
    case Mode::GradientAngle:
        if (!t.empty()) {
            double v = 0.0;
            if (!parse_number(t, v)) {
                ctx.echo("Enter an angle in degrees.");
                return;
            }
            angle_ = core::to_radians(v);
        }
        pattern_ = "GRADIENT";
        ctx.echo("Pattern: GRADIENT");
        main_prompt(ctx);
        return;
    case Mode::PickPoint:
        break;
    }

    if (t.empty()) {
        done_ = true; // Enter finishes the command
        return;
    }
    const auto settings = [&] {
        mode_ = Mode::Pattern;
        ctx.set_prompt("Enter a pattern name or [?/Solid/User defined/Gradient] <" + pattern_ + ">: ");
    };
    if (up == "T" || up == "SETTINGS" || up == "P" || up == "PROPERTIES" || up == "PATTERN") {
        settings();
        return;
    }
    if (up == "S" || up == "SELECT" || up == "SELECT OBJECTS") {
        mode_ = Mode::Select;
        select_.begin(ctx);
        return;
    }
    if (up == "U" || up == "UNDO") {
        if (picks_ == 0) {
            ctx.echo("Nothing to undo.");
        } else {
            ctx.submit(core::UndoLastGroupCommand{ctx.command_group()});
            --picks_;
        }
        main_prompt(ctx);
        return;
    }
    if (up == "G" || up == "GRADIENT") {
        mode_ = Mode::GradientColor;
        ctx.set_prompt("Specify second colour as r,g,b <" + std::to_string(color2_.r) + "," + std::to_string(color2_.g) +
                       "," + std::to_string(color2_.b) + ">: ");
        return;
    }
    if (typed_ && (up == "W" || up == "B" || up == "A" || up == "DR" || up == "O" || up == "AN" || up == "CO" ||
                   up == "LA" || up == "TR" || up == "DRAW" || up == "ADVANCED" || up == "ORIGIN" || up == "LAYER")) {
        ctx.echo("That option is not available yet.");
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        // Click inside a closed region -> trace its boundary (+ islands) and hatch it. Each
        // pick is its own undo group; the command stays active for more picks.
        ctx.submit(core::HatchPickPointCommand{*p, pattern_, scale_, angle_, ctx.new_group(), color2_});
        ++picks_;
        main_prompt(ctx);
    }
}

void HatchCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// MATCHPROP / MA (source -> N targets; paintbrush cursor; per-target undo)
// ---------------------------------------------------------------------------
namespace {
std::string match_settings_line(const core::MatchPropFilter& f) {
    // AutoCAD's "Current active settings:" line, in its order.
    std::string out = "Current active settings:";
    const auto add = [&](bool on, const char* name) {
        if (on) {
            out += std::string(" ") + name;
        }
    };
    add(f.color, "Color");
    add(f.layer, "Layer");
    add(f.linetype, "Ltype");
    add(f.celtscale, "Ltscale");
    add(f.lineweight, "Lineweight");
    add(f.transparency, "Transparency");
    add(f.thickness, "Thickness");
    add(f.plotstyle, "PlotStyle");
    add(f.dimension, "Dim");
    add(f.text, "Text");
    add(f.hatch, "Hatch");
    add(f.polyline, "Polyline");
    add(f.viewport, "Viewport");
    add(f.table, "Table");
    add(f.material, "Material");
    add(f.shadow, "Shadow display");
    add(f.multileader, "Multileader");
    add(f.center_object, "Center object");
    return out;
}
} // namespace

void MatchPropCommand::begin_targets(CommandContext& ctx) {
    state_ = State::Targets;
    if (ctx.view() != nullptr) {
        ctx.view()->set_match_cursor(true); // paintbrush while matching
        ctx.echo(match_settings_line(ctx.view()->match_filter()));
    }
    ctx.set_prompt("Select destination object(s) or [Settings]: ");
}

void MatchPropCommand::start(CommandContext& ctx) {
    // Noun-verb: if objects are already selected, the first becomes the source and we go
    // straight to picking destinations (same convenience as JOIN).
    if (ctx.has_selection()) {
        ctx.submit(core::MatchPropSourceFromSelectionCommand{});
        begin_targets(ctx);
        return;
    }
    ctx.set_prompt("Select source object: ");
}

void MatchPropCommand::selection_gesture(CommandContext& ctx) {
    // The viewport's pick / window / crossing chose the destinations: apply to them.
    const core::MatchPropFilter filter = ctx.view() != nullptr ? ctx.view()->match_filter() : core::MatchPropFilter{};
    ctx.submit(core::MatchPropApplySelectionCommand{filter, ctx.new_group()});
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
        begin_targets(ctx);
        return;
    }
    // Targets: Enter finishes; "S"/"Settings" opens the category dialog; a typed point
    // is one destination.
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
            ctx.echo(match_settings_line(ctx.view()->match_filter()));
        }
        ctx.set_prompt("Select destination object(s) or [Settings]: ");
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        ctx.submit(core::SelectPickCommand{*p, ctx.pick_radius(), true, true});
        selection_gesture(ctx);
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
// TRIM / EXTEND
// ---------------------------------------------------------------------------
void TrimCommand::start(CommandContext& ctx) {
    picks_ = 0;
    fence_.clear();
    fence_open_ = false;
    corner_.reset();
    static constexpr const char* kProj[] = {"None", "UCS", "View"};
    ctx.echo(std::string("Current settings: Projection=") + kProj[std::clamp(s_projmode_, 0, 2)] +
             ", Edge=" + ((s_edgemode_ == 1) ? "Extend" : "None") + ", Mode=" + ((s_mode_ == 1) ? "Quick" : "Standard"));
    if (s_mode_ == 1) {
        ctx.submit(core::SetTrimEdgesCommand{core::SetTrimEdgesCommand::Op::All, false});
        state_ = State::Objects;
        prompt(ctx);
    } else if (ctx.has_selection()) {
        // Standard mode: objects selected before the command are the cutting edges.
        ctx.submit(core::SetTrimEdgesCommand{core::SetTrimEdgesCommand::Op::FromSelection, (s_edgemode_ == 1)});
        state_ = State::Objects;
        prompt(ctx);
    } else {
        begin_edges(ctx);
    }
}

void TrimCommand::begin_edges(CommandContext& ctx) {
    ctx.set_preview({});
    ctx.submit(core::SetTrimEdgesCommand{core::SetTrimEdgesCommand::Op::All, false});
    ctx.submit(core::ClearSelectionCommand{});
    state_ = State::Edges;
    ctx.echo(extend_ ? "Select boundary edges ..." : "Select cutting edges ...");
    select_.begin(ctx, "Select objects or <select all>: ");
}

void TrimCommand::end_edges(CommandContext& ctx) {
    // Nothing selected is every object (the engine decides on what it holds).
    ctx.submit(core::SetTrimEdgesCommand{core::SetTrimEdgesCommand::Op::FromSelection,
                                         s_mode_ == 0 && s_edgemode_ == 1});
    state_ = State::Objects;
    prompt(ctx);
}

void TrimCommand::end_erase(CommandContext& ctx) {
    if (ctx.has_selection()) {
        step(ctx);
        ctx.submit(core::EraseSelectionCommand{ctx.group_id()});
        ++picks_;
    }
    state_ = State::Objects;
    prompt(ctx);
}

void TrimCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        if (state_ == State::Edges) {
            end_edges(ctx);
        } else if (state_ == State::Erase) {
            end_erase(ctx);
        }
    }
}

void TrimCommand::prompt(CommandContext& ctx) {
    switch (state_) {
    case State::Fence:
        if (fence_open_) {
            ctx.set_prompt(fence_.empty() ? "Specify first fence point: " : "Specify next fence point or [Undo]: ");
        } else {
            ctx.set_prompt("Specify second fence point: ");
        }
        return;
    case State::Corner:
        ctx.set_prompt(corner_ ? "Specify opposite corner: " : "Specify first corner: ");
        return;
    case State::Mode:
        ctx.set_prompt(std::string(extend_ ? "Enter an extend mode option" : "Enter a trim mode option") +
                       " [Quick/Standard] <" + ((s_mode_ == 1) ? "Quick" : "Standard") + ">: ");
        return;
    case State::Project:
        ctx.set_prompt(std::string("Enter a projection option [None/Ucs/View] <") +
                       (s_projmode_ == 0 ? "None" : s_projmode_ == 2 ? "View" : "Ucs") + ">: ");
        return;
    case State::Edge:
        ctx.set_prompt(std::string("Enter an implied edge extension mode [Extend/No extend] <") +
                       ((s_edgemode_ == 1) ? "Extend" : "No extend") + ">: ");
        return;
    case State::Edges:
    case State::Erase:
        return; // the selection step prompts
    case State::Objects:
        break;
    }
    // The part a pick would trim away (or add) shows under the cursor.
    PreviewSpec band;
    band.trim_hover = true;
    band.trim_extend = extend_;
    band.trim_quick = s_mode_ == 1;
    ctx.set_preview(std::move(band));
    std::string opts = extend_ ? ((s_mode_ == 1) ? "Boundary edges/Crossing/mOde/Project"
                                           : "Boundary edges/Fence/Crossing/mOde/Project/Edge")
                               : ((s_mode_ == 1) ? "cuTting edges/Crossing/mOde/Project/eRase"
                                           : "cuTting edges/Fence/Crossing/mOde/Project/Edge/eRase");
    if (picks_ > 0) {
        opts += "/Undo";
    }
    ctx.set_prompt(std::string(extend_ ? "Select object to extend or shift-select to trim or ["
                                       : "Select object to trim or shift-select to extend or [") +
                   opts + "]: ");
}

void TrimCommand::step(CommandContext& ctx) {
    if (picks_ > 0) {
        (void)ctx.new_group(); // every pick, path or erase its own undo step
    }
}

void TrimCommand::path(CommandContext& ctx, std::vector<core::Vec2> points, bool extend, bool window) {
    if (points.size() < 2) {
        return;
    }
    step(ctx);
    core::TrimPathCommand c{std::move(points), extend, ctx.pick_radius(), ctx.group_id()};
    c.window = window;
    c.quick = (s_mode_ == 1);
    ctx.submit(std::move(c));
    ++picks_;
}

void TrimCommand::freehand(CommandContext& ctx, const std::vector<core::Vec2>& points) {
    fence_.clear();
    fence_open_ = false;
    ctx.set_preview({});
    path(ctx, points, extend_ != ctx.shift_held());
    state_ = State::Objects;
    prompt(ctx);
}

void TrimCommand::finish(CommandContext& ctx) {
    ctx.set_preview({});
    ctx.submit(core::SetTrimEdgesCommand{core::SetTrimEdgesCommand::Op::All, false}); // the edges let go
    done_ = true;
}

void TrimCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    switch (state_) {
    case State::Edges:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            end_edges(ctx);
        }
        return;
    case State::Erase:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            end_erase(ctx);
        }
        return;
    case State::Mode:
        if (u == "Q" || u == "QUICK") {
            s_mode_ = 1;
            ctx.submit(core::SetTrimEdgesCommand{core::SetTrimEdgesCommand::Op::All, false});
        } else if (u == "S" || u == "STANDARD") {
            s_mode_ = 0;
            begin_edges(ctx);
            return;
        } else if (!u.empty()) {
            ctx.echo("Invalid option keyword.");
            prompt(ctx);
            return;
        }
        state_ = State::Objects;
        prompt(ctx);
        return;
    case State::Project:
        if (u == "N" || u == "NONE") {
            s_projmode_ = 0;
        } else if (u == "U" || u == "UCS") {
            s_projmode_ = 1;
        } else if (u == "V" || u == "VIEW") {
            s_projmode_ = 2;
        } else if (!u.empty()) {
            ctx.echo("Invalid option keyword.");
            prompt(ctx);
            return;
        }
        state_ = State::Objects;
        prompt(ctx);
        return;
    case State::Edge:
        if (u == "E" || u == "EXTEND") {
            s_edgemode_ = 1;
        } else if (u == "N" || u == "NO" || u == "NO EXTEND" || u == "NOEXTEND") {
            s_edgemode_ = 0;
        } else if (!u.empty()) {
            ctx.echo("Invalid option keyword.");
            prompt(ctx);
            return;
        }
        ctx.submit(core::SetTrimEdgesCommand{core::SetTrimEdgesCommand::Op::EdgeMode, s_mode_ == 0 && s_edgemode_ == 1});
        state_ = State::Objects;
        prompt(ctx);
        return;
    case State::Corner: {
        if (u.empty()) {
            corner_.reset();
            ctx.set_preview({});
            state_ = State::Objects;
            prompt(ctx);
            return;
        }
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        if (!corner_) {
            corner_ = *p;
            ctx.set_preview({PreviewKind::Rectangle, {*p}});
            prompt(ctx);
            return;
        }
        const core::Vec2 first = *corner_;
        corner_.reset();
        ctx.set_preview({});
        path(ctx, {first, *p}, extend_ != ctx.shift_held(), true);
        state_ = State::Objects;
        prompt(ctx);
        return;
    }
    case State::Fence: {
        if (u.empty()) {
            if (fence_open_ && fence_.size() >= 2) {
                path(ctx, fence_, extend_);
            }
            fence_.clear();
            fence_open_ = false;
            ctx.set_preview({});
            state_ = State::Objects;
            prompt(ctx);
            return;
        }
        if (fence_open_ && (u == "U" || u == "UNDO")) {
            if (!fence_.empty()) {
                fence_.pop_back();
            }
            ctx.set_preview(fence_.empty() ? PreviewSpec{} : PreviewSpec{PreviewKind::Polyline, fence_});
            prompt(ctx);
            return;
        }
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        if (fence_open_) {
            fence_.push_back(*p);
            ctx.set_preview({PreviewKind::Polyline, fence_});
            prompt(ctx);
            return;
        }
        // The second of two picks on empty space.
        std::vector<core::Vec2> f{fence_.front(), *p};
        fence_.clear();
        ctx.set_preview({});
        path(ctx, std::move(f), extend_ != ctx.shift_held());
        state_ = State::Objects;
        prompt(ctx);
        return;
    }
    case State::Objects:
        break;
    }

    if (u.empty()) {
        finish(ctx);
        return;
    }
    if (u == "U" || u == "UNDO") {
        if (picks_ == 0) {
            ctx.echo("Nothing to undo.");
        } else {
            ctx.submit(core::UndoLastGroupCommand{ctx.command_group()}); // the last pick is its own step
            --picks_;
        }
        prompt(ctx);
        return;
    }
    if ((!extend_ && (u == "T" || u == "CUTTING" || u == "CUTTINGEDGES")) ||
        (extend_ && (u == "B" || u == "BOUNDARY" || u == "BOUNDARYEDGES"))) {
        begin_edges(ctx);
        return;
    }
    if (u == "F" || u == "FENCE") {
        ctx.set_preview({}); // the hover band goes while the fence is drawn
        fence_.clear();
        fence_open_ = true;
        state_ = State::Fence;
        prompt(ctx);
        return;
    }
    if (u == "C" || u == "CROSSING") {
        ctx.set_preview({});
        corner_.reset();
        state_ = State::Corner;
        prompt(ctx);
        return;
    }
    if (u == "O" || u == "MODE") {
        ctx.set_preview({});
        state_ = State::Mode;
        prompt(ctx);
        return;
    }
    if (u == "P" || u == "PROJECT") {
        ctx.set_preview({});
        state_ = State::Project;
        prompt(ctx);
        return;
    }
    if (u == "E" || u == "EDGE") {
        ctx.set_preview({});
        state_ = State::Edge;
        prompt(ctx);
        return;
    }
    if (!extend_ && (u == "R" || u == "ERASE")) {
        ctx.set_preview({});
        ctx.submit(core::ClearSelectionCommand{});
        state_ = State::Erase;
        select_.begin(ctx, "Select objects to erase or <exit>: ");
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    if (ctx.input_is_pick() && !ctx.hovered_kind().has_value()) {
        // A click on empty space: in Quick mode the first point of a fence, in Standard
        // mode the first corner of a crossing window -- as AutoCAD has them.
        if (s_mode_ == 1) {
            fence_ = {*p};
            fence_open_ = false;
            state_ = State::Fence;
            ctx.set_preview({PreviewKind::Segment, fence_});
        } else {
            corner_ = *p;
            state_ = State::Corner;
            ctx.set_preview({PreviewKind::Rectangle, {*p}});
        }
        prompt(ctx);
        return;
    }
    step(ctx);
    if (extend_ != ctx.shift_held()) {
        ctx.submit(core::ExtendPickCommand{*p, ctx.pick_radius(), ctx.group_id()});
    } else {
        ctx.submit(core::TrimPickCommand{*p, ctx.pick_radius(), ctx.group_id(), (s_mode_ == 1)});
    }
    ++picks_;
    prompt(ctx); // Undo appears after the first
    // The result is echoed by the engine (honest status), not assumed here.
}

void TrimCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    finish(ctx);
}

// ---------------------------------------------------------------------------
// The layer tools: LAYOFF, LAYFRZ, LAYLCK, LAYULK, LAYMCUR, LAYCUR, LAYISO, LAYUNISO,
// LAYON, LAYTHW
// ---------------------------------------------------------------------------
namespace {
core::LayerToolCommand::Op lay_op(LayToolCommand::Kind k) {
    using K = LayToolCommand::Kind;
    using Op = core::LayerToolCommand::Op;
    switch (k) {
    case K::Off:
        return Op::Off;
    case K::Freeze:
        return Op::Freeze;
    case K::Lock:
        return Op::Lock;
    case K::Unlock:
        return Op::Unlock;
    case K::MakeCurrent:
        return Op::MakeCurrent;
    case K::ToCurrent:
        return Op::ToCurrent;
    case K::Isolate:
        return Op::Isolate;
    case K::Unisolate:
        return Op::Unisolate;
    case K::AllOn:
        return Op::AllOn;
    case K::AllThaw:
        return Op::AllThaw;
    }
    return Op::Off;
}
} // namespace

std::string LayToolCommand::name() const {
    switch (kind_) {
    case Kind::Off:
        return "LAYOFF";
    case Kind::Freeze:
        return "LAYFRZ";
    case Kind::Lock:
        return "LAYLCK";
    case Kind::Unlock:
        return "LAYULK";
    case Kind::MakeCurrent:
        return "LAYMCUR";
    case Kind::ToCurrent:
        return "LAYCUR";
    case Kind::Isolate:
        return "LAYISO";
    case Kind::Unisolate:
        return "LAYUNISO";
    case Kind::AllOn:
        return "LAYON";
    case Kind::AllThaw:
        return "LAYTHW";
    }
    return "LAYOFF";
}

void LayToolCommand::prompt(CommandContext& ctx) {
    switch (kind_) {
    case Kind::Off:
        ctx.set_prompt(changed_ > 0 ? "Select an object on the layer to be turned off or [Undo]: "
                                    : "Select an object on the layer to be turned off: ");
        return;
    case Kind::Freeze:
        ctx.set_prompt(changed_ > 0 ? "Select an object on the layer to be frozen or [Undo]: "
                                    : "Select an object on the layer to be frozen: ");
        return;
    case Kind::Lock:
        ctx.set_prompt("Select an object on the layer to be locked: ");
        return;
    case Kind::Unlock:
        ctx.set_prompt("Select an object on the layer to be unlocked: ");
        return;
    case Kind::MakeCurrent:
        ctx.set_prompt("Select object whose layer will become current: ");
        return;
    default:
        return;
    }
}

void LayToolCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    switch (kind_) {
    case Kind::Unisolate:
    case Kind::AllOn:
    case Kind::AllThaw:
        ctx.submit(core::LayerToolCommand{lay_op(kind_), {}, 0.0, ctx.group_id()});
        done_ = true;
        return;
    case Kind::ToCurrent:
    case Kind::Isolate:
        if (ctx.has_selection()) {
            finish_selection(ctx);
            return;
        }
        select_.begin(ctx, kind_ == Kind::ToCurrent ? "Select objects to be changed to the current layer: "
                                                    : "Select objects on the layer(s) to be isolated: ");
        return;
    default:
        prompt(ctx);
        return;
    }
}

void LayToolCommand::finish_selection(CommandContext& ctx) {
    if (ctx.has_selection()) {
        ctx.submit(core::LayerToolCommand{lay_op(kind_), {}, 0.0, ctx.group_id()});
    }
    done_ = true;
}

void LayToolCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        finish_selection(ctx);
    }
}

void LayToolCommand::input(CommandContext& ctx, const std::string& text) {
    if (select_.active()) {
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            finish_selection(ctx);
        }
        return;
    }
    const std::string u = upper(trimmed(text));
    if (u.empty()) {
        done_ = true;
        return;
    }
    const bool repeats = kind_ == Kind::Off || kind_ == Kind::Freeze;
    if (repeats && (u == "U" || u == "UNDO")) {
        if (changed_ == 0) {
            ctx.echo("Nothing to undo.");
        } else {
            ctx.submit(core::LayerToolCommand{core::LayerToolCommand::Op::UndoLast, {}, 0.0, ctx.group_id()});
            --changed_;
        }
        prompt(ctx);
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    ctx.submit(core::LayerToolCommand{lay_op(kind_), *p, ctx.pick_radius(), ctx.group_id()});
    if (!repeats) {
        done_ = true;
        return;
    }
    ++changed_;
    prompt(ctx);
}

void LayToolCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void IntVarCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Enter new value for " + var_ + " <" + std::to_string(*value_) + ">: ");
}

void IntVarCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (!t.empty()) {
        double v = 0.0;
        if (!parse_number(t, v) || v != std::floor(v) || v < lo_ || v > hi_) {
            ctx.echo("Requires an integer between " + std::to_string(lo_) + " and " + std::to_string(hi_) + ".");
            return;
        }
        *value_ = static_cast<int>(v);
    }
    done_ = true;
}

void IntVarCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// COPYCLIP / CUTCLIP / COPYBASE / CUTBASE, PASTECLIP / PASTEORIG
// ---------------------------------------------------------------------------
void ClipCopyCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (with_base_) {
        ctx.set_prompt("Specify base point: ");
        return;
    }
    if (ctx.has_selection()) {
        finish(ctx);
        return;
    }
    select_.begin(ctx);
}

void ClipCopyCommand::finish(CommandContext& ctx) {
    if (ctx.has_selection()) {
        if (cut_) {
            ctx.submit(core::CutClipboardCommand{ctx.group_id(), base_});
        } else {
            ctx.submit(core::CopyClipboardCommand{base_});
        }
    }
    done_ = true;
}

void ClipCopyCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void ClipCopyCommand::input(CommandContext& ctx, const std::string& text) {
    if (with_base_ && !base_) {
        if (const auto p = read_point(ctx, text)) {
            base_ = *p;
            if (ctx.has_selection()) {
                finish(ctx);
            } else {
                select_.begin(ctx);
            }
        }
        return;
    }
    if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void ClipCopyCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void PasteClipCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    kind_ = ctx.view() != nullptr ? ctx.view()->prepare_paste() : std::string();
    if (as_block_ && kind_ != "objects" && !kind_.empty()) {
        ctx.echo("PASTEBLOCK pastes objects copied from a drawing; the clipboard holds something else.");
        done_ = true;
        return;
    }
    if (original_) {
        if (kind_ != "objects" && !kind_.empty()) {
            ctx.echo("PASTEORIG pastes objects copied from a drawing; the clipboard holds something else.");
        } else if (ctx.view() == nullptr || !ctx.view()->paste_at({}, true)) {
            ctx.submit(core::PasteClipboardCommand{{}, ctx.group_id(), false});
        }
        done_ = true;
        return;
    }
    if (kind_ == "objects" || kind_.empty()) {
        PreviewSpec pv;
        pv.paste_band = true; // the objects follow the cursor
        ctx.set_preview(std::move(pv));
    }
    ctx.set_prompt("Specify insertion point: ");
}

void PasteClipCommand::input(CommandContext& ctx, const std::string& text) {
    if (trimmed(text).empty()) {
        ctx.set_preview({});
        done_ = true; // Enter: nothing pasted
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    ctx.set_preview({});
    if (as_block_) {
        // The objects pasted are the selection; BLOCK's own path makes them one insert at
        // the point, all in one undo step.
        static std::uint32_t serial = 0;
        const auto stamp = static_cast<std::uint32_t>(
            std::chrono::system_clock::now().time_since_epoch().count() / 1000 * 2654435761ULL);
        char name[16];
        std::snprintf(name, sizeof(name), "A$C%08X", stamp + (serial++ * 0x9E3779B9U));
        ctx.submit(core::PasteClipboardCommand{*p, ctx.group_id(), true});
        ctx.submit(core::DefineBlockCommand{name, *p, ctx.group_id()});
        done_ = true;
        return;
    }
    if (ctx.view() == nullptr || !ctx.view()->paste_at(*p, false)) {
        ctx.submit(core::PasteClipboardCommand{*p, ctx.group_id(), true});
    }
    done_ = true;
}

void PasteClipCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.set_preview({});
    done_ = true;
}

// ---------------------------------------------------------------------------
// ROTATE
// ---------------------------------------------------------------------------
namespace {
// The [Points] option of ROTATE / SCALE Reference: a rubber line from the first point.
void rubber_from(CommandContext& ctx, core::Vec2 a) {
    PreviewSpec pv;
    pv.kind = PreviewKind::Segment;
    pv.points = {a};
    ctx.set_preview(pv);
}
} // namespace

void RotateCommand::begin_base(CommandContext& ctx) {
    const core::DrawingUnits u = ctx.units();
    char base[32];
    std::snprintf(base, sizeof(base), "%g", core::to_degrees(u.base_angle));
    ctx.echo(std::string("Current positive angle in UCS:  ANGDIR=") +
             (u.clockwise ? "clockwise" : "counterclockwise") + "  ANGBASE=" + base);
    state_ = State::Base;
    ctx.set_prompt("Specify base point: ");
}

void RotateCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (!ctx.has_selection()) {
        state_ = State::Select;
        select_.begin(ctx);
        return;
    }
    begin_base(ctx);
}

void RotateCommand::selection_gesture(CommandContext& ctx) {
    if (state_ == State::Select && select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        input(ctx, "");
    }
}

void RotateCommand::prompt_angle(CommandContext& ctx) {
    // The live band: the selection turned by the cursor's bearing from the base point
    // (minus the reference angle once one is set), the angle shown at the cursor.
    PreviewSpec pv;
    pv.kind = PreviewKind::Rotate;
    pv.points = {*base_};
    pv.ref_angle = state_ == State::NewAngle ? ref_angle_ : 0.0;
    ctx.set_preview(pv);
    ctx.set_prompt(state_ == State::NewAngle ? "Specify the new angle or [Points] <0>: "
                                             : "Specify rotation angle or [Copy/Reference] <0>: ");
}

void RotateCommand::commit(CommandContext& ctx, double angle) {
    ctx.submit(core::RotateSelectionCommand{*base_, angle, ctx.group_id(), copy_});
    ctx.echo(copy_ ? "Rotated a copy." : "Rotated.");
    done_ = true;
}

void RotateCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    double deg = 0.0;
    switch (state_) {
    case State::Select:
        if (select_.active() && select_.input(ctx, text) == SelectObjectsPhase::Result::Continue) {
            return;
        }
        if (!ctx.has_selection()) {
            done_ = true;
            return;
        }
        begin_base(ctx);
        return;
    case State::Base:
        if (const auto p = read_point(ctx, text)) {
            base_ = *p;
            ctx.set_last_point(*p);
            state_ = State::Angle;
            prompt_angle(ctx);
        }
        return;
    case State::Angle:
        if (u == "C" || u == "COPY") {
            copy_ = true;
            ctx.echo("Rotating a copy of the selected objects.");
            prompt_angle(ctx);
            return;
        }
        if (u == "R" || u == "REFERENCE") {
            state_ = State::RefAngle;
            ctx.clear_preview();
            ctx.set_prompt("Specify the reference angle <0>: ");
            return;
        }
        if (t.empty()) {
            commit(ctx, 0.0); // the default
            return;
        }
        if (parse_number(t, deg)) {
            commit(ctx, core::to_radians(deg)); // a typed number is degrees
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            commit(ctx, std::atan2(p->y - base_->y, p->x - base_->x)); // the bearing to the point
            return;
        }
        ctx.echo("Requires valid numeric angle or second point.");
        return;
    case State::RefAngle:
        if (t.empty()) {
            ref_angle_ = 0.0;
        } else if (parse_number(t, deg)) {
            ref_angle_ = core::to_radians(deg);
        } else if (const auto p = read_point(ctx, text)) {
            first_ = *p; // two points define the reference angle
            state_ = State::RefSecond;
            rubber_from(ctx, first_);
            ctx.set_prompt("Specify second point: ");
            return;
        } else {
            ctx.echo("Requires valid numeric angle or two points.");
            return;
        }
        state_ = State::NewAngle;
        prompt_angle(ctx);
        return;
    case State::RefSecond:
        if (const auto p = read_point(ctx, text)) {
            ref_angle_ = std::atan2(p->y - first_.y, p->x - first_.x);
            state_ = State::NewAngle;
            prompt_angle(ctx);
        }
        return;
    case State::NewAngle:
        if (u == "P" || u == "POINTS") {
            state_ = State::NewFirst;
            ctx.clear_preview();
            ctx.set_prompt("Specify first point: ");
            return;
        }
        if (t.empty()) {
            commit(ctx, -ref_angle_); // the default new angle, 0
            return;
        }
        if (parse_number(t, deg)) {
            commit(ctx, core::to_radians(deg) - ref_angle_);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            commit(ctx, std::atan2(p->y - base_->y, p->x - base_->x) - ref_angle_);
            return;
        }
        ctx.echo("Requires valid numeric angle or second point.");
        return;
    case State::NewFirst:
        if (const auto p = read_point(ctx, text)) {
            first_ = *p;
            state_ = State::NewSecond;
            rubber_from(ctx, first_);
            ctx.set_prompt("Specify second point: ");
        }
        return;
    case State::NewSecond:
        if (const auto p = read_point(ctx, text)) {
            commit(ctx, std::atan2(p->y - first_.y, p->x - first_.x) - ref_angle_);
        }
        return;
    }
}

void RotateCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.submit(core::TransformPreviewCommand{core::TransformPreviewCommand::Kind::Rotate, {}, {}, 0.0, false});
    done_ = true;
}

// ---------------------------------------------------------------------------
// SCALE
// ---------------------------------------------------------------------------
void ScaleCommand::begin_base(CommandContext& ctx) {
    state_ = State::Base;
    ctx.set_prompt("Specify base point: ");
}

void ScaleCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (!ctx.has_selection()) {
        state_ = State::Select;
        select_.begin(ctx);
        return;
    }
    begin_base(ctx);
}

void ScaleCommand::selection_gesture(CommandContext& ctx) {
    if (state_ == State::Select && select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        input(ctx, "");
    }
}

void ScaleCommand::prompt_factor(CommandContext& ctx) {
    // The live band: the selection scaled by the cursor's distance from the base point
    // in drawing units (divided by the reference length once one is set), the factor
    // shown at the cursor. AutoCAD's rule: one unit away is a factor of 1.
    PreviewSpec pv;
    pv.kind = PreviewKind::Scale;
    pv.points = {*base_};
    pv.ref_length = state_ == State::NewLength ? ref_len_ : 1.0;
    ctx.set_preview(pv);
    if (state_ == State::NewLength) {
        ctx.set_prompt("Specify new length or [Points] <" +
                       core::units::format_length(1.0, ctx.units()) + ">: ");
    } else {
        ctx.set_prompt("Specify scale factor or [Copy/Reference]: ");
    }
}

void ScaleCommand::commit(CommandContext& ctx, double factor) {
    if (!(factor > 0.0)) {
        ctx.echo("Value must be positive and nonzero.");
        return;
    }
    ctx.submit(core::ScaleSelectionCommand{*base_, factor, ctx.group_id(), copy_});
    ctx.echo(copy_ ? "Scaled a copy." : "Scaled.");
    done_ = true;
}

void ScaleCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    double v = 0.0;
    switch (state_) {
    case State::Select:
        if (select_.active() && select_.input(ctx, text) == SelectObjectsPhase::Result::Continue) {
            return;
        }
        if (!ctx.has_selection()) {
            done_ = true;
            return;
        }
        begin_base(ctx);
        return;
    case State::Base:
        if (const auto p = read_point(ctx, text)) {
            base_ = *p;
            ctx.set_last_point(*p);
            state_ = State::Factor;
            prompt_factor(ctx);
        }
        return;
    case State::Factor:
        if (u == "C" || u == "COPY") {
            copy_ = true;
            ctx.echo("Scaling a copy of the selected objects.");
            prompt_factor(ctx);
            return;
        }
        if (u == "R" || u == "REFERENCE") {
            state_ = State::RefLength;
            ctx.clear_preview();
            ctx.set_prompt("Specify reference length <" +
                           core::units::format_length(1.0, ctx.units()) + ">: ");
            return;
        }
        if (parse_number(t, v)) {
            commit(ctx, v);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            commit(ctx, core::distance(*base_, *p)); // the distance from the base point
            return;
        }
        ctx.echo("Requires numeric distance or second point.");
        return;
    case State::RefLength:
        if (t.empty()) {
            ref_len_ = 1.0;
        } else if (parse_number(t, v)) {
            if (!(v > 0.0)) {
                ctx.echo("Value must be positive and nonzero.");
                return;
            }
            ref_len_ = v;
        } else if (const auto p = read_point(ctx, text)) {
            first_ = *p; // two points define the reference length
            state_ = State::RefSecond;
            rubber_from(ctx, first_);
            ctx.set_prompt("Specify second point: ");
            return;
        } else {
            ctx.echo("Requires numeric distance or two points.");
            return;
        }
        state_ = State::NewLength;
        prompt_factor(ctx);
        return;
    case State::RefSecond:
        if (const auto p = read_point(ctx, text)) {
            const double len = core::distance(first_, *p);
            if (!(len > 0.0)) {
                ctx.echo("Value must be positive and nonzero.");
                return;
            }
            ref_len_ = len;
            state_ = State::NewLength;
            prompt_factor(ctx);
        }
        return;
    case State::NewLength:
        if (u == "P" || u == "POINTS") {
            state_ = State::NewFirst;
            ctx.clear_preview();
            ctx.set_prompt("Specify first point: ");
            return;
        }
        if (t.empty()) {
            commit(ctx, 1.0 / ref_len_); // the default new length, 1
            return;
        }
        if (parse_number(t, v)) {
            commit(ctx, v / ref_len_);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            commit(ctx, core::distance(*base_, *p) / ref_len_);
            return;
        }
        ctx.echo("Requires numeric distance or second point.");
        return;
    case State::NewFirst:
        if (const auto p = read_point(ctx, text)) {
            first_ = *p;
            state_ = State::NewSecond;
            rubber_from(ctx, first_);
            ctx.set_prompt("Specify second point: ");
        }
        return;
    case State::NewSecond:
        if (const auto p = read_point(ctx, text)) {
            commit(ctx, core::distance(first_, *p) / ref_len_);
        }
        return;
    }
}

void ScaleCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.submit(core::TransformPreviewCommand{core::TransformPreviewCommand::Kind::Scale, {}, {}, 0.0, false});
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
    if (dialog_ && ctx.view() != nullptr && ctx.view()->purge_dialog()) {
        done_ = true; // the dialog does the rest
        return;
    }
    state_ = State::Type;
    ctx.set_prompt("Enter type of unused objects to purge [Blocks/Dimstyles/Groups/LAyers/Tablestyles/"
                   "Images/textSTyles/Zero-length geometry/Empty text objects/All] <All>: ");
}

void PurgeCommand::ask_next(CommandContext& ctx) {
    static constexpr const char* kTypeNames[] = {"", "block", "dimension style", "group", "layer",
                                                 "table style", "image", "text style"};
    if (queue_.empty()) {
        done_ = true;
        return;
    }
    const auto& [what, name] = queue_.front();
    ctx.set_prompt(std::string("Purge ") + kTypeNames[what] + " \"" + name + "\"? [Yes/No/All] <N>: ");
}

void PurgeCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Type:
        if (u.empty() || u == "A" || u == "ALL") {
            what_ = 0;
        } else if (u == "B" || u == "BLOCKS") {
            what_ = 1;
        } else if (u == "D" || u == "DIMSTYLES") {
            what_ = 2;
        } else if (u == "G" || u == "GROUPS") {
            what_ = 3;
        } else if (u == "LA" || u == "LAYERS") {
            what_ = 4;
        } else if (u == "T" || u == "TABLESTYLES") {
            what_ = 5;
        } else if (u == "I" || u == "IMAGES") {
            what_ = 6;
        } else if (u == "ST" || u == "TEXTSTYLES") {
            what_ = 7;
        } else if (u == "Z" || u == "ZERO-LENGTH GEOMETRY" || u == "ZERO") {
            what_ = 8;
        } else if (u == "E" || u == "EMPTY TEXT OBJECTS" || u == "EMPTY") {
            what_ = 9;
        } else {
            ctx.echo("Enter Blocks, Dimstyles, Groups, LAyers, Tablestyles, Images, textSTyles, "
                     "Zero-length geometry, Empty text objects or All.");
            return;
        }
        if (what_ == 8 || what_ == 9) {
            // Objects, not names: no pattern to ask for.
            ctx.submit(core::PurgeCommand{ctx.group_id(), what_}); // the engine reports what went
            done_ = true;
            return;
        }
        state_ = State::Names;
        ctx.set_prompt("Enter name(s) to purge <*>: ");
        return;
    case State::Names:
        pattern_ = t.empty() ? "*" : t;
        state_ = State::Verify;
        ctx.set_prompt("Verify each name to be purged? [Yes/No] <Y>: ");
        return;
    case State::Verify: {
        if (u == "N" || u == "NO") {
            ctx.submit(core::PurgeCommand{ctx.group_id(), what_, pattern_});
            done_ = true;
            return;
        }
        if (!u.empty() && u != "Y" && u != "YES") {
            ctx.echo("Enter Yes or No.");
            return;
        }
        // One question per unused name, from the engine's published candidates.
        const core::RenderSnapshot::PurgeCandidates cand = ctx.purge_candidates();
        const auto take = [&](std::uint8_t what, const std::vector<std::string>& names) {
            if (what_ != 0 && what_ != what) {
                return;
            }
            for (const std::string& n : names) {
                if (pattern_ == "*" || upper(n) == upper(pattern_) ||
                    (pattern_.find('*') != std::string::npos && upper(n).rfind(upper(pattern_).substr(0, pattern_.find('*')), 0) == 0)) {
                    queue_.emplace_back(what, n);
                }
            }
        };
        take(1, cand.blocks);
        take(2, cand.dimstyles);
        take(3, cand.groups);
        take(4, cand.layers);
        take(5, cand.tablestyles);
        take(6, cand.images);
        take(7, cand.textstyles);
        if (queue_.empty()) {
            ctx.echo("No unused objects found.");
            done_ = true;
            return;
        }
        state_ = State::Each;
        ask_next(ctx);
        return;
    }
    case State::Each: {
        const auto [what, name] = queue_.front();
        queue_.erase(queue_.begin());
        if (u == "Y" || u == "YES") {
            ctx.submit(core::PurgeCommand{ctx.new_group(), what, name});
        } else if (u == "A" || u == "ALL") {
            ctx.submit(core::PurgeCommand{ctx.new_group(), what, name});
            // ... and everything still queued, without asking.
            for (const auto& [w2, n2] : queue_) {
                ctx.submit(core::PurgeCommand{ctx.new_group(), w2, n2});
            }
            queue_.clear();
        } else if (!u.empty() && u != "N" && u != "NO") {
            queue_.insert(queue_.begin(), {what, name});
            ctx.echo("Enter Yes, No or All.");
            return;
        }
        ask_next(ctx);
        return;
    }
    }
}

void PurgeCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// ALIGN: two source/destination pairs, optional uniform scale
// ---------------------------------------------------------------------------
void AlignCommand::begin_points(CommandContext& ctx) {
    state_ = State::Src1;
    ctx.set_prompt("Specify first source point: ");
}

void AlignCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (!ctx.has_selection()) {
        state_ = State::Select;
        select_.begin(ctx);
        return;
    }
    begin_points(ctx);
}

void AlignCommand::selection_gesture(CommandContext& ctx) {
    if (state_ == State::Select && select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        input(ctx, "");
    }
}

void AlignCommand::align(CommandContext& ctx, bool scale) {
    core::AlignSelectionCommand cmd;
    cmd.src1 = src1_;
    cmd.dst1 = dst1_;
    cmd.src2 = src2_;
    cmd.dst2 = dst2_;
    cmd.scale = scale;
    cmd.group = ctx.group_id();
    ctx.submit(cmd);
    done_ = true;
}

void AlignCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    switch (state_) {
    case State::Select:
        if (select_.active() && select_.input(ctx, text) == SelectObjectsPhase::Result::Continue) {
            return;
        }
        if (!ctx.has_selection()) {
            done_ = true;
            return;
        }
        begin_points(ctx);
        return;
    case State::Src1:
        if (const auto p = read_point(ctx, text)) {
            src1_ = *p;
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Segment, {src1_}});
            state_ = State::Dst1;
            ctx.set_prompt("Specify first destination point: ");
        }
        return;
    case State::Dst1:
        if (const auto p = read_point(ctx, text)) {
            dst1_ = *p;
            ctx.set_last_point(*p);
            ctx.clear_preview();
            state_ = State::Src2;
            ctx.set_prompt("Specify second source point or <continue>: ");
        }
        return;
    case State::Src2:
        if (t.empty()) {
            // One pair: the objects move from the source point to the destination.
            ctx.submit(core::MoveSelectionCommand{dst1_ - src1_, ctx.group_id()});
            done_ = true;
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            src2_ = *p;
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Segment, {src2_}});
            state_ = State::Dst2;
            ctx.set_prompt("Specify second destination point: ");
        }
        return;
    case State::Dst2:
        if (const auto p = read_point(ctx, text)) {
            dst2_ = *p;
            ctx.set_last_point(*p);
            ctx.clear_preview();
            state_ = State::Src3;
            ctx.set_prompt("Specify third source point or <continue>: ");
        }
        return;
    case State::Src3:
        if (t.empty()) {
            state_ = State::Scale;
            ctx.set_prompt("Scale objects based on alignment points? [Yes/No] <N>: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Segment, {*p}});
            state_ = State::Dst3;
            ctx.set_prompt("Specify third destination point: ");
        }
        return;
    case State::Dst3:
        if (read_point(ctx, text)) {
            // Three pairs fix a plane in 3D; in a plane the first two decide, unscaled.
            ctx.clear_preview();
            align(ctx, false);
        }
        return;
    case State::Scale: {
        const std::string u = upper(t);
        align(ctx, u == "Y" || u == "YES");
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
    state_ = State::Measure;
    changed_ = 0;
    prompt_measure(ctx);
}

void LengthenCommand::prompt_measure(CommandContext& ctx) {
    using Mode = core::LengthenCommand::Mode;
    const char* def = s_mode_ == Mode::Delta || s_mode_ == Mode::DeltaAngle ? "DElta"
                      : s_mode_ == Mode::Percent                            ? "Percent"
                      : s_mode_ == Mode::Dynamic                            ? "DYnamic"
                                                                            : "Total";
    ctx.set_prompt(std::string("Select an object to measure or [DElta/Percent/Total/DYnamic] <") + def + ">: ");
}

void LengthenCommand::input(CommandContext& ctx, const std::string& text) {
    using Mode = core::LengthenCommand::Mode;
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    const auto ask_change = [&] {
        state_ = State::Pick;
        ctx.set_prompt(changed_ > 0 ? "Select an object to change or [Undo]: " : "Select an object to change: ");
    };
    const auto ask_amount = [&](Mode m) {
        s_mode_ = m;
        asking_angle_ = false;
        state_ = State::Amount;
        if (m == Mode::Delta) {
            ctx.set_prompt("Enter delta length or [Angle] <" + core::units::format_length(s_delta_, ctx.units()) + ">: ");
        } else if (m == Mode::Percent) {
            ctx.set_prompt("Enter percentage length <" + core::units::format_length(s_percent_, ctx.units()) + ">: ");
        } else {
            ctx.set_prompt("Specify total length or [Angle] <" + core::units::format_length(s_total_, ctx.units()) + ">: ");
        }
    };
    switch (state_) {
    case State::Measure:
        if (t.empty()) {
            // Enter takes the default option.
            if (s_mode_ == Mode::Dynamic) {
                ask_change();
            } else {
                ask_amount(s_mode_ == Mode::DeltaAngle ? Mode::Delta : s_mode_ == Mode::TotalAngle ? Mode::Total : s_mode_);
            }
            return;
        }
        if (u == "DE" || u == "DELTA") {
            ask_amount(Mode::Delta);
            return;
        }
        if (u == "P" || u == "PERCENT") {
            ask_amount(Mode::Percent);
            return;
        }
        if (u == "T" || u == "TOTAL") {
            ask_amount(Mode::Total);
            return;
        }
        if (u == "DY" || u == "DYNAMIC") {
            s_mode_ = Mode::Dynamic;
            ask_change();
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            core::LengthenCommand q;
            q.pick = *p;
            q.pick_radius = ctx.pick_radius();
            q.mode = Mode::Measure;
            ctx.submit(q); // the engine reports `Current length: ...`
            prompt_measure(ctx);
        }
        return;
    case State::Amount: {
        if (!asking_angle_ && (u == "A" || u == "ANGLE") && s_mode_ != Mode::Percent) {
            asking_angle_ = true;
            const bool delta = s_mode_ == Mode::Delta;
            s_mode_ = delta ? Mode::DeltaAngle : Mode::TotalAngle;
            ctx.set_prompt(std::string(delta ? "Enter delta angle <" : "Specify total angle <") +
                           core::units::format_angle(s_angle_, ctx.units()) + ">: ");
            return;
        }
        double v = 0.0;
        if (!t.empty() && !parse_number(t, v)) {
            ctx.echo("Enter a number.");
            return;
        }
        if (asking_angle_) {
            if (!t.empty()) {
                s_angle_ = core::to_radians(v);
            }
        } else if (s_mode_ == Mode::Delta) {
            if (!t.empty()) {
                s_delta_ = v;
            }
        } else if (s_mode_ == Mode::Percent) {
            if (!t.empty()) {
                if (v <= 0.0) {
                    ctx.echo("Enter a value greater than zero.");
                    return;
                }
                s_percent_ = v;
            }
        } else if (!t.empty()) {
            if (v <= 0.0) {
                ctx.echo("Enter a value greater than zero.");
                return;
            }
            s_total_ = v;
        }
        ask_change();
        return;
    }
    case State::Pick: {
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (u == "U" || u == "UNDO") {
            if (changed_ == 0) {
                ctx.echo("Nothing to undo.");
            } else {
                ctx.submit(core::UndoLastGroupCommand{ctx.command_group()});
                --changed_;
            }
            ask_change();
            return;
        }
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        if (s_mode_ == Mode::Dynamic) {
            dyn_pick_ = *p;
            state_ = State::DynEnd;
            ctx.set_preview({PreviewKind::Segment, {*p}});
            ctx.set_prompt("Specify new end point: ");
            return;
        }
        if (changed_ > 0) {
            (void)ctx.new_group(); // every change its own undo step
        }
        core::LengthenCommand cmd;
        cmd.pick = *p;
        cmd.pick_radius = ctx.pick_radius();
        cmd.mode = s_mode_;
        cmd.value = s_mode_ == Mode::Delta     ? s_delta_
                    : s_mode_ == Mode::Percent ? s_percent_
                    : (s_mode_ == Mode::DeltaAngle || s_mode_ == Mode::TotalAngle) ? s_angle_
                                                                                   : s_total_;
        cmd.group = ctx.group_id();
        ctx.submit(cmd);
        ++changed_;
        ask_change();
        return;
    }
    case State::DynEnd:
        if (const auto p = read_point(ctx, text)) {
            if (changed_ > 0) {
                (void)ctx.new_group();
            }
            core::LengthenCommand cmd;
            cmd.pick = dyn_pick_;
            cmd.pick_radius = ctx.pick_radius();
            cmd.mode = Mode::Dynamic;
            cmd.to = *p;
            cmd.group = ctx.group_id();
            ctx.submit(cmd);
            ++changed_;
            ctx.set_preview({});
            ask_change();
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
            // AutoCAD: the selecting click doubles as the first break point (and is the
            // last point, so "@" at the next prompt breaks at a single point).
            pick_ = *p;
            p1_ = *p;
            ctx.set_last_point(*p);
            if (at_point_) {
                state_ = State::AtPoint;
                ctx.set_prompt("Specify break point: ");
                return;
            }
            state_ = State::Second;
            ctx.set_prompt("Specify second break point or [First point]: ");
        }
        return;
    case State::AtPoint:
        if (const auto p = read_point(ctx, text)) {
            fire(*p, *p); // BREAKATPOINT: split with no gap
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
            ctx.set_last_point(*p);
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
    // Isocircle is offered while isometric drafting is on, as AutoCAD offers it.
    ctx.set_prompt(ctx.grid_settings().isometric ? "Specify axis endpoint of ellipse or [Arc/Center/Isocircle]: "
                                                 : "Specify axis endpoint of ellipse or [Arc/Center]: ");
}

void EllipseCommand::commit_isocircle(CommandContext& ctx, double r) {
    if (r <= 1e-12) {
        ctx.echo("Requires a radius greater than zero.");
        return;
    }
    // Left: axes 90 and 150, the long axis square to 30 (at 120); Top: 30 and 150, long axis
    // horizontal; Right: 90 and 30, long axis at 60.
    static constexpr double kMajorDeg[3] = {120.0, 0.0, 60.0};
    const double a = core::to_radians(kMajorDeg[std::min<int>(ctx.grid_settings().isoplane, 2)]);
    const double major = r * std::sqrt(1.5);
    core::AddEllipseCommand e;
    e.center = iso_center_;
    e.major = core::Vec2{std::cos(a), std::sin(a)} * major;
    e.ratio = 1.0 / std::sqrt(3.0); // sqrt(1/2) / sqrt(3/2)
    e.group = ctx.group_id();
    ctx.set_preview({});
    ctx.submit(std::move(e));
    done_ = true;
}

void IsoPlaneCommand::start(CommandContext& ctx) {
    static constexpr const char* kNames[3] = {"Left", "Top", "Right"};
    const core::GridSettings g = ctx.grid_settings();
    const int cur = std::min<int>(g.isoplane, 2);
    if (draft_) {
        ctx.set_prompt(std::string("Enter isometric drafting option [Orthographic/isoplane Left/isoplane Top/isoplane Right] <") +
                       (g.isometric ? "Orthographic" : kNames[cur]) + ">: ");
    } else {
        ctx.set_prompt(std::string("Enter isometric plane setting [Left/Top/Right] <") + kNames[(cur + 1) % 3] + ">: ");
    }
}

void IsoPlaneCommand::input(CommandContext& ctx, const std::string& text) {
    static constexpr const char* kNames[3] = {"Left", "Top", "Right"};
    const std::string u = upper(trimmed(text));
    core::GridSettings g = ctx.grid_settings();
    const int cur = std::min<int>(g.isoplane, 2);
    int plane = -1;
    if (u == "L" || u == "LEFT" || u == "ISOPLANE LEFT") {
        plane = 0;
    } else if (u == "T" || u == "TOP" || u == "ISOPLANE TOP") {
        plane = 1;
    } else if (u == "R" || u == "RIGHT" || u == "ISOPLANE RIGHT") {
        plane = 2;
    } else if ((draft_ && (u == "O" || u == "ORTHOGRAPHIC")) || (draft_ && u.empty() && g.isometric)) {
        g.isometric = false;
        ctx.set_grid_settings(g);
        ctx.echo("Isometric drafting off.");
        done_ = true;
        return;
    } else if (u.empty()) {
        plane = draft_ ? cur : (cur + 1) % 3;
    } else {
        ctx.echo("Invalid option keyword.");
        return;
    }
    g.isoplane = static_cast<std::uint8_t>(plane);
    if (draft_) {
        g.isometric = true;
    }
    ctx.set_grid_settings(g);
    ctx.echo(std::string("Current isoplane: ") + kNames[plane]);
    done_ = true;
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
    ctx.set_preview({});
    done_ = true;
    if (!s_pellipse_) {
        ctx.submit(core::AddEllipseCommand{center_, major_, ratio_, start_, end_, ctx.group_id(), {}});
        return;
    }
    // PELLIPSE 1: a polyline of arcs, a biarc on each sixteenth of the turn, tangent to the
    // ellipse at every sample.
    core::EllipseData e = shape();
    const bool full = core::ellipse::is_full(e);
    const double sweep = core::ellipse::sweep_of(e);
    const int steps = std::max(2, static_cast<int>(std::ceil(sweep / (core::kTwoPi / 16.0) - 1e-9)));
    const core::Vec2 n = core::ellipse::minor_axis(e);
    const auto tangent = [&](double t) {
        return core::normalized(core::Vec2{-e.major.x * std::sin(t) + n.x * std::cos(t),
                                           -e.major.y * std::sin(t) + n.y * std::cos(t)});
    };
    core::AddPolylineCommand p;
    p.closed = full;
    p.group = ctx.group_id();
    const int count = full ? steps : steps + 1;
    for (int i = 0; i < steps; ++i) {
        const double t0 = e.start + sweep * i / steps;
        const double t1 = e.start + sweep * (i + 1) / steps;
        const core::polyline_ops::Biarc a = core::polyline_ops::biarc(
            core::ellipse::point_at(e, t0), tangent(t0), core::ellipse::point_at(e, t1), tangent(t1));
        p.points.push_back(core::ellipse::point_at(e, t0));
        p.bulges.push_back(a.bulge0);
        p.points.push_back(a.joint);
        p.bulges.push_back(a.bulge1);
    }
    if (count > steps) {
        p.points.push_back(core::ellipse::point_at(e, e.start + sweep));
        p.bulges.push_back(0.0);
    }
    ctx.submit(std::move(p));
}

void PellipseCommand::start(CommandContext& ctx) {
    ctx.set_prompt(std::string("Enter new value for PELLIPSE <") + (EllipseCommand::s_pellipse_ ? "1" : "0") + ">: ");
}

void PellipseCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (!t.empty()) {
        if (t != "0" && t != "1") {
            ctx.echo("Requires 0 or 1.");
            return;
        }
        EllipseCommand::s_pellipse_ = t == "1";
    }
    done_ = true;
}

void PellipseCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
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
        if ((u == "I" || u == "ISOCIRCLE") && !arc_) {
            if (!ctx.grid_settings().isometric) {
                ctx.echo("Isocircle needs isometric drafting: ISODRAFT, or SNAP Style Isometric.");
                return;
            }
            state_ = State::IsoCenter;
            ctx.set_prompt("Specify center of isocircle: ");
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
    case State::IsoCenter:
        if (const auto p = read_point(ctx, text)) {
            iso_center_ = *p;
            ctx.set_last_point(*p);
            state_ = State::IsoRadius;
            ctx.set_preview({PreviewKind::Segment, {*p}});
            ctx.set_prompt("Specify radius of isocircle or [Diameter]: ");
        }
        return;
    case State::IsoRadius:
    case State::IsoDiameter: {
        if (state_ == State::IsoRadius && (u == "D" || u == "DIAMETER")) {
            state_ = State::IsoDiameter;
            ctx.set_prompt("Specify diameter of isocircle: ");
            return;
        }
        double v = 0.0;
        if (parse_number(t, v)) {
            commit_isocircle(ctx, state_ == State::IsoDiameter ? v * 0.5 : v);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            const double d = core::distance(*p, iso_center_);
            commit_isocircle(ctx, state_ == State::IsoDiameter ? d * 0.5 : d);
        }
        return;
    }
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
            PreviewSpec pv{PreviewKind::Ellipse, {center_}};
            pv.major = first_dir_ * half_;
            pv.ellipse_stage = 3; // the circle on the first axis, turned by the cursor's angle
            ctx.set_preview(std::move(pv));
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

void SplineCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        convert_selection(ctx);
    }
}

void SplineCommand::convert_selection(CommandContext& ctx) {
    if (ctx.has_selection()) {
        // PEDIT's Spline on each: the polyline's vertices fitted by a spline, which takes
        // its place (one undo step).
        core::PeditCommand c;
        c.op = 4;
        c.selection = true;
        c.group = ctx.group_id();
        ctx.submit(std::move(c));
    }
    done_ = true;
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
    case State::Object:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            convert_selection(ctx);
        }
        return;
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
            ctx.set_prompt("Enter degree of spline <" + std::to_string(degree_) + ">: ");
            return;
        }
        if (u == "O" || u == "OBJECT") {
            state_ = State::Object;
            select_.begin(ctx, "Select spline-fit polylines: ");
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
    ctx.clear_preview();
    done_ = true;
}

void DonutCommand::take_inner(CommandContext& ctx, double v) {
    inner_ = v;
    state_ = State::Outer;
    ctx.set_prompt("Specify outside diameter of donut <" + fmt4(std::max(outer_, inner_)) + ">: ");
}

void DonutCommand::take_outer(CommandContext& ctx, double v) {
    if (v <= inner_) {
        ctx.echo("Value must be greater than the inside diameter.");
        state_ = State::Outer;
        return;
    }
    outer_ = v;
    s_inner_ = inner_;
    s_outer_ = outer_;
    state_ = State::Center;
    // The donut rides on the cursor while its centre is asked for: both edges of the ring.
    PreviewSpec pv;
    pv.kind = PreviewKind::Outline;
    constexpr int kSteps = 48;
    for (const double r : {inner_ * 0.5, outer_ * 0.5}) {
        if (r <= 0.0) {
            continue;
        }
        for (int i = 0; i < kSteps; ++i) {
            const double a0 = 2.0 * 3.14159265358979323846 * i / kSteps;
            const double a1 = 2.0 * 3.14159265358979323846 * (i + 1) / kSteps;
            pv.points.push_back({r * std::cos(a0), r * std::sin(a0)});
            pv.points.push_back({r * std::cos(a1), r * std::sin(a1)});
        }
    }
    ctx.set_preview(std::move(pv));
    ctx.set_prompt("Specify center of donut or <exit>: ");
}

void DonutCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    double v = 0.0;
    switch (state_) {
    case State::Inner:
        // A diameter typed, or shown as the distance between two points.
        if (t.empty()) {
            take_inner(ctx, inner_);
        } else if (parse_number(t, v)) {
            if (v < 0.0) {
                ctx.echo("Enter a diameter of 0 or more.");
                return;
            }
            take_inner(ctx, v);
        } else if (const auto p = read_point(ctx, text)) {
            first_ = *p;
            ctx.set_last_point(*p);
            state_ = State::InnerSecond;
            ctx.set_preview({PreviewKind::Segment, {first_}});
            ctx.set_prompt("Specify second point: ");
        }
        return;
    case State::InnerSecond:
        if (const auto p = read_point(ctx, text)) {
            ctx.clear_preview();
            take_inner(ctx, core::distance(first_, *p));
        }
        return;
    case State::Outer:
        if (t.empty()) {
            take_outer(ctx, std::max(outer_, inner_));
        } else if (parse_number(t, v)) {
            take_outer(ctx, v);
        } else if (const auto p = read_point(ctx, text)) {
            first_ = *p;
            ctx.set_last_point(*p);
            state_ = State::OuterSecond;
            ctx.set_preview({PreviewKind::Segment, {first_}});
            ctx.set_prompt("Specify second point: ");
        }
        return;
    case State::OuterSecond:
        if (const auto p = read_point(ctx, text)) {
            ctx.clear_preview();
            take_outer(ctx, core::distance(first_, *p));
            if (state_ == State::Outer) {
                ctx.set_prompt("Specify outside diameter of donut <" +
                               fmt4(std::max(outer_, inner_)) + ">: ");
            }
        }
        return;
    case State::Center: {
        if (t.empty()) {
            ctx.clear_preview();
            done_ = true;
            return;
        }
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        // AutoCAD's donut: a closed polyline of two half-circle arcs on the mean
        // diameter, as wide as the ring is thick. FILLMODE decides whether it is filled.
        const double mean_r = (inner_ + outer_) * 0.25;
        const double width = (outer_ - inner_) * 0.5;
        core::AddPolylineCommand ring;
        ring.points = {{p->x - mean_r, p->y}, {p->x + mean_r, p->y}};
        ring.bulges = {1.0, 1.0};
        ring.widths = {width, width, width, width};
        ring.closed = true;
        ring.group = ctx.group_id();
        ctx.submit(std::move(ring));
        ctx.set_last_point(*p);
        ctx.set_prompt("Specify center of donut or <exit>: ");
        return;
    }
    }
}

// ---------------------------------------------------------------------------
// FILL / FILLMODE / PLINEWID
// ---------------------------------------------------------------------------
void FillCommand::start(CommandContext& ctx) {
    if (sysvar_) {
        ctx.set_prompt(std::string("Enter new value for FILLMODE <") + (ctx.fillmode() ? "1" : "0") + ">: ");
    } else {
        ctx.set_prompt(std::string("Enter mode [ON/OFF] <") + (ctx.fillmode() ? "ON" : "OFF") + ">: ");
    }
}

void FillCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    if (u.empty()) {
        done_ = true; // Enter keeps the mode
        return;
    }
    if (sysvar_) {
        if (u != "0" && u != "1") {
            ctx.echo("Requires 0 or 1.");
            return;
        }
        ctx.submit(core::SetFillModeCommand{u == "1"});
    } else {
        if (u != "ON" && u != "OFF") {
            ctx.echo("Invalid option keyword.");
            return;
        }
        ctx.submit(core::SetFillModeCommand{u == "ON"});
    }
    done_ = true;
}

void FillCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void PlinewidCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Enter new value for PLINEWID <" + fmt4(PolylineCommand::s_width_) + ">: ");
}

void PlinewidCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (!t.empty()) {
        double v = 0.0;
        if (!parse_number(t, v) || v < 0.0) {
            ctx.echo("Requires a width of zero or more.");
            return;
        }
        PolylineCommand::s_width_ = v;
    }
    done_ = true;
}

void PlinewidCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// VIEW
// ---------------------------------------------------------------------------
void ViewCommand::start(CommandContext& ctx) {
    if (dialog_ && ctx.view() != nullptr && ctx.view()->view_manager()) {
        done_ = true; // the View Manager does the rest
        return;
    }
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
            ctx.submit(core::ListGroupsCommand{}); // the engine lists them
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
    ctx.set_prompt("Enter new value for PICKSTYLE <" + std::to_string(ctx.pickstyle()) + ">: ");
}

void PickStyleCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void PickStyleCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    double v = ctx.pickstyle();
    if (!t.empty() && (!parse_number(t, v) || v < 0.0 || v > 3.0 || v != std::floor(v))) {
        ctx.echo("Enter 0, 1, 2 or 3 (1 = groups, 2 = associative hatches, 3 = both).");
        return;
    }
    const int iv = static_cast<int>(v);
    ctx.submit(core::SetPickStyleCommand{(iv & 1) != 0, (iv & 2) != 0});
    done_ = true;
}

// ---------------------------------------------------------------------------
// OSNAP / -OSNAP: running object snaps
// ---------------------------------------------------------------------------
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
    ctx.echo("Current osnap modes: " + snap_list(cur));
    ctx.set_prompt("Enter list of object snap modes <" + snap_codes(cur) + ">: ");
}

void OsnapModesCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void OsnapModesCommand::input(CommandContext& ctx, const std::string& text) {
    if (trimmed(text).empty()) {
        done_ = true; // Enter keeps the modes
        return;
    }
    std::uint32_t mask = 0;
    std::string unknown;
    if (!parse_snap_list(text, mask, nullptr, nullptr, &unknown)) {
        ctx.echo("Invalid object snap mode \"" + unknown +
                 "\". Use END, MID, CEN, GCEN, NOD, QUA, INT, EXT, INS, PER, TAN, NEA, APP, PAR, NONE or ALL.");
        return;
    }
    if (ctx.view() != nullptr) {
        ctx.view()->set_snap_mask(mask);
    }
    ctx.echo("Object snap modes: " + snap_list(mask));
    done_ = true;
}

namespace {
// An angle in degrees as a system variable shows it: no trailing zeros.
std::string degrees_text(double radians) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", core::to_degrees(radians));
    std::string t(buf);
    while (!t.empty() && t.back() == '0') {
        t.pop_back();
    }
    if (!t.empty() && t.back() == '.') {
        t.pop_back();
    }
    return t;
}
} // namespace

std::string TrackingVarCommand::name() const {
    switch (var_) {
    case Var::PolarAng:
        return "POLARANG";
    case Var::PolarAddAng:
        return "POLARADDANG";
    case Var::PolarMode:
        return "POLARMODE";
    case Var::PolarDist:
        return "POLARDIST";
    case Var::SnapType:
        return "SNAPTYPE";
    case Var::AutoSnap:
        return "AUTOSNAP";
    case Var::TempOverrides:
        return "TEMPOVERRIDES";
    }
    return "POLARANG";
}

std::string TrackingVarCommand::current(CommandContext& ctx) const {
    const TrackingSettings t = ctx.tracking_settings();
    switch (var_) {
    case Var::PolarAng:
        return degrees_text(t.polar_increment);
    case Var::PolarAddAng: {
        std::string list;
        for (const double a : t.additional) {
            list += (list.empty() ? "" : ";") + degrees_text(a);
        }
        return "\"" + list + "\"";
    }
    case Var::PolarMode:
        return std::to_string(t.polarmode());
    case Var::PolarDist:
        return fmt4(t.polar_distance);
    case Var::SnapType:
        return std::to_string(ctx.snap_type());
    case Var::AutoSnap:
        // Marker, tooltips and magnet are always on (1 + 2 + 4 + 32); 8 and 16 are the
        // two tracking modes.
        return std::to_string(39 + (ctx.polar_tracking() ? 8 : 0) + (ctx.object_snap_tracking() ? 16 : 0));
    case Var::TempOverrides:
        return ctx.temp_overrides() ? "1" : "0";
    }
    return {};
}

void TrackingVarCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Enter new value for " + name() + (var_ == Var::PolarAddAng ? ", or . for none" : "") +
                   " <" + current(ctx) + ">: ");
}

void TrackingVarCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void TrackingVarCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (t.empty()) {
        done_ = true; // Enter keeps the value
        return;
    }
    TrackingSettings settings = ctx.tracking_settings();
    double v = 0.0;
    const auto whole = [&](double lo, double hi) {
        return parse_number(t, v) && v >= lo && v <= hi && v == std::floor(v);
    };
    switch (var_) {
    case Var::PolarAng:
        if (!parse_number(t, v) || !(v > 0.0) || v > 360.0) {
            ctx.echo("Requires an angle greater than 0, up to 360.");
            return;
        }
        settings.polar_increment = core::to_radians(v);
        break;
    case Var::PolarAddAng: {
        std::vector<double> angles;
        if (t != ".") {
            std::string item;
            std::stringstream list(t);
            while (std::getline(list, item, ';')) {
                if (trimmed(item).empty()) {
                    continue;
                }
                double a = 0.0;
                if (!parse_number(trimmed(item), a)) {
                    ctx.echo("Requires angles separated by semicolons, or . for none.");
                    return;
                }
                angles.push_back(core::to_radians(a));
            }
            if (angles.size() > 10) {
                ctx.echo("No more than 10 additional angles.");
                return;
            }
        }
        settings.additional = std::move(angles);
        break;
    }
    case Var::PolarMode:
        if (!whole(0.0, 15.0)) {
            ctx.echo("Requires an integer between 0 and 15.");
            return;
        }
        settings.set_polarmode(static_cast<int>(v));
        break;
    case Var::PolarDist:
        if (!parse_number(t, v) || v < 0.0) {
            ctx.echo("Requires a distance of zero or more.");
            return;
        }
        settings.polar_distance = v;
        break;
    case Var::SnapType:
        if (!whole(0.0, 1.0)) {
            ctx.echo("Requires 0 or 1.");
            return;
        }
        ctx.set_snap_type(static_cast<int>(v));
        break;
    case Var::AutoSnap:
        if (!whole(0.0, 63.0)) {
            ctx.echo("Requires an integer between 0 and 63.");
            return;
        }
        ctx.set_polar_tracking((static_cast<int>(v) & 8) != 0);
        ctx.set_object_snap_tracking((static_cast<int>(v) & 16) != 0);
        break;
    case Var::TempOverrides:
        if (!whole(0.0, 1.0)) {
            ctx.echo("Requires 0 or 1.");
            return;
        }
        ctx.set_temp_overrides(v == 1.0);
        break;
    }
    ctx.set_tracking_settings(settings);
    done_ = true;
}

void OsmodeCommand::start(CommandContext& ctx) {
    const std::uint32_t cur = ctx.view() != nullptr ? ctx.view()->snap_mask() : 0;
    ctx.set_prompt("Enter new value for OSMODE <" + std::to_string(osmode_of(cur)) + ">: ");
}

void OsmodeCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void OsmodeCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (t.empty()) {
        done_ = true;
        return;
    }
    double v = 0.0;
    if (!parse_number(t, v) || v < 0.0 || v > 32767.0 || v != std::floor(v)) {
        ctx.echo("Requires an integer between 0 and 32767.");
        return;
    }
    if (ctx.view() != nullptr) {
        ctx.view()->set_snap_mask(mask_of_osmode(static_cast<int>(v)));
    }
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
            ctx.set_prompt("Enter mode [ON/OFF/Display but not plot] <ON>: ");
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
            ctx.set_prompt(pts_.size() >= 3 ? "Specify next point or [Close/Undo]: "
                                            : "Specify next point or [Undo]: ");
        }
        return;
    case State::Frames:
        if (u == "OFF") {
            ctx.submit(core::SetWipeoutFramesCommand{false});
        } else if (t.empty() || u == "ON") {
            ctx.submit(core::SetWipeoutFramesCommand{true});
        } else if (u == "D" || u == "DISPLAY" || u == "DISPLAY BUT NOT PLOT") {
            ctx.submit(core::SetWipeoutFramesCommand{true, false});
        } else {
            ctx.echo("Enter ON, OFF or Display but not plot.");
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
    if (ctx.view() != nullptr) {
        // The Field window, as AutoCAD opens; the command line only where there is none.
        std::string code;
        const int r = ctx.view()->field_dialog(code);
        if (r == 0 || (r == 1 && code.empty())) {
            done_ = true;
            return;
        }
        if (r == 1) {
            code_ = code;
            state_ = State::Point;
            ctx.set_prompt("Specify start point: ");
            return;
        }
    }
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
    ctx.set_prompt(multiple_
                       ? "Enter an option [Close/Open/Join/Width/Fit/Spline/Decurve/Ltype gen/Reverse/Undo]: "
                       : "Enter an option [Close/Open/Join/Width/Edit vertex/Fit/Spline/Decurve/Ltype gen/Reverse/Undo]: ");
}

void PeditCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        if (!ctx.has_selection()) {
            done_ = true;
            return;
        }
        state_ = State::Option;
        prompt_option(ctx);
    }
}

void PeditCommand::prompt_vertex(CommandContext& ctx) const {
    ctx.set_prompt("Enter a vertex editing option [Next/Previous/Break/Insert/Move/Regen/Straighten/Tangent/Width/eXit] "
                   "<N>: ");
}

void PeditCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Select;
    if (mpedit_) {
        input(ctx, "M"); // MPEDIT: Multiple from the start
        return;
    }
    ctx.set_prompt("Select polyline or [Multiple]: ");
}

void PeditCommand::cancel(CommandContext& ctx) {
    if (state_ == State::Vertex || state_ == State::VInsert || state_ == State::VMoveTo || state_ == State::VBreak ||
        state_ == State::VTangent || state_ == State::VWidthStart || state_ == State::VWidthEnd) {
        ctx.submit(core::PeditCommand{pick_, ctx.pick_radius(), 17, {}, {}, 0}); // the X goes
    }
    ctx.set_preview({});
    ctx.echo("*Cancel*");
    done_ = true;
}

void PeditCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    const auto op = [&](std::uint8_t code, core::Vec2 p1 = {}, core::Vec2 p2 = {}) {
        core::PeditCommand c{pick_, ctx.pick_radius(), code, p1, p2, ctx.new_group()};
        c.selection = multiple_;
        ctx.submit(c);
    };
    switch (state_) {
    case State::Select:
        if (u == "M" || u == "MULTIPLE") {
            multiple_ = true;
            state_ = State::MultiSelect;
            if (ctx.has_selection()) {
                state_ = State::Option;
                prompt_option(ctx);
                return;
            }
            select_.begin(ctx);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            pick_ = *p;
            ctx.set_last_point(*p);
            // PEDITACCEPT 0: a line or arc picked is offered as a polyline first.
            if (ctx.input_is_pick() &&
                (ctx.hovered_kind() == core::EntityKind::Line || ctx.hovered_kind() == core::EntityKind::Arc)) {
                state_ = State::Accept;
                ctx.echo("Object selected is not a polyline");
                ctx.set_prompt("Do you want to turn it into one? <Y> ");
                return;
            }
            state_ = State::Option;
            prompt_option(ctx);
        }
        return;
    case State::Accept:
        if (t.empty() || u == "Y" || u == "YES") {
            state_ = State::Option;
            prompt_option(ctx);
        } else if (u == "N" || u == "NO") {
            state_ = State::Select;
            ctx.set_prompt("Select polyline or [Multiple]: ");
        } else {
            ctx.echo("Enter Y or N.");
        }
        return;
    case State::MultiSelect:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            if (!ctx.has_selection()) {
                done_ = true;
                return;
            }
            state_ = State::Option;
            prompt_option(ctx);
        }
        return;
    case State::LtypeGen:
        if (u == "ON" || u == "OFF" || t.empty()) {
            op(22, {u == "ON" ? 1.0 : 0.0, 0.0});
        } else {
            ctx.echo("Enter ON or OFF.");
            return;
        }
        state_ = State::Option;
        prompt_option(ctx);
        return;
    case State::Fuzz: {
        double fuzz = 0.0;
        if (!t.empty() && (!parse_number(t, fuzz) || fuzz < 0.0)) {
            ctx.echo("Requires a distance of zero or more.");
            return;
        }
        ctx.submit(core::JoinSelectionCommand{fuzz, ctx.new_group(), true}); // a polyline, as PEDIT makes
        state_ = State::Option;
        prompt_option(ctx);
        return;
    }
    case State::Option:
        if (t.empty() || u == "X" || u == "EXIT") {
            done_ = true;
            return;
        }
        if (u == "C" || u == "CLOSE") {
            op(0);
        } else if (u == "O" || u == "OPEN") {
            op(1);
        } else if ((u == "J" || u == "JOIN") && multiple_) {
            // The ends within the fuzz distance of each other are joined.
            state_ = State::Fuzz;
            ctx.set_prompt("Enter fuzz distance <0.0000>: ");
            return;
        } else if (u == "J" || u == "JOIN") {
            join_picks_ = {pick_};
            state_ = State::JoinTargets;
            ctx.set_prompt("Select objects to join: ");
            return;
        } else if (u == "W" || u == "WIDTH") {
            state_ = State::WidthVal;
            ctx.set_prompt("Specify new width for all segments: ");
            return;
        } else if ((u == "E" || u == "EDIT VERTEX" || u == "EDIT") && !multiple_) {
            ctx.submit(core::PeditCommand{pick_, ctx.pick_radius(), 11, {}, {}, 0}); // the X on the first vertex
            state_ = State::Vertex;
            prompt_vertex(ctx);
            return;
        } else if (u == "F" || u == "FIT") {
            op(10);
        } else if (u == "S" || u == "SPLINE") {
            op(4);
            done_ = true; // the polyline is a spline now
            return;
        } else if (u == "D" || u == "DECURVE") {
            op(3);
        } else if (u == "L" || u == "LTYPE GEN" || u == "LTYPE") {
            state_ = State::LtypeGen;
            ctx.set_prompt("Enter polyline linetype generation option [ON/OFF] <Off>: ");
            return;
        } else if (u == "R" || u == "REVERSE") {
            op(2);
        } else if (u == "U" || u == "UNDO") {
            // Multiple: the polylines as they were are the ones selected again.
            ctx.submit(core::UndoLastGroupCommand{ctx.command_group(), multiple_});
        } else {
            ctx.echo(multiple_ ? "Enter Close, Open, Join, Width, Fit, Spline, Decurve, Reverse, Undo or Enter to finish."
                               : "Enter Close, Open, Join, Width, Edit vertex, Fit, Spline, Decurve, Reverse, Undo or Enter to finish.");
        }
        prompt_option(ctx);
        return;
    case State::JoinTargets:
        if (t.empty()) {
            if (join_picks_.size() > 1) {
                core::JoinPickCommand j{join_picks_, ctx.pick_radius(), ctx.new_group()};
                j.polyline = true; // PEDIT joins into a polyline, never one line or arc
                ctx.submit(std::move(j));
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
    case State::Vertex: {
        const auto vop = [&](std::uint8_t code, core::Vec2 p1 = {}, core::Vec2 p2 = {}) {
            ctx.submit(core::PeditCommand{pick_, ctx.pick_radius(), code, p1, p2, ctx.new_group()});
        };
        if (u == "X" || u == "EXIT") {
            vop(17);
            state_ = State::Option;
            prompt_option(ctx);
            return;
        }
        if (t.empty() || u == "N" || u == "NEXT") {
            vop(12);
        } else if (u == "P" || u == "PREVIOUS") {
            vop(13);
        } else if (u == "B" || u == "BREAK" || u == "S" || u == "STRAIGHTEN") {
            straighten_ = u[0] == 'S';
            vop(14); // the vertex the break or straightening starts at
            state_ = State::VBreak;
            ctx.set_prompt("Enter an option [Next/Previous/Go/eXit] <N>: ");
            return;
        } else if (u == "I" || u == "INSERT") {
            state_ = State::VInsert;
            ctx.set_prompt("Specify location for new vertex: ");
            return;
        } else if (u == "M" || u == "MOVE") {
            state_ = State::VMoveTo;
            ctx.set_prompt("Specify new location for marked vertex: ");
            return;
        } else if (u == "W" || u == "WIDTH") {
            state_ = State::VWidthStart;
            ctx.set_prompt("Specify starting width for next segment <0.0000>: ");
            return;
        } else if (u == "T" || u == "TANGENT") {
            state_ = State::VTangent;
            ctx.set_prompt("Specify direction of vertex tangent: ");
            return;
        } else if (u != "R" && u != "REGEN") {
            ctx.echo("Enter Next, Previous, Break, Insert, Move, Regen, Straighten, Tangent, Width or eXit.");
        }
        prompt_vertex(ctx);
        return;
    }
    case State::VBreak:
        if (t.empty() || u == "N" || u == "NEXT") {
            op(12);
        } else if (u == "P" || u == "PREVIOUS") {
            op(13);
        } else if (u == "G" || u == "GO") {
            op(straighten_ ? 16 : 15);
            state_ = State::Vertex;
            prompt_vertex(ctx);
            return;
        } else if (u == "X" || u == "EXIT") {
            op(21);
            state_ = State::Vertex;
            prompt_vertex(ctx);
            return;
        } else {
            ctx.echo("Enter Next, Previous, Go or eXit.");
        }
        ctx.set_prompt("Enter an option [Next/Previous/Go/eXit] <N>: ");
        return;
    case State::VTangent:
        if (!t.empty()) {
            ctx.echo("Vertex tangents are not kept; Fit makes its own.");
        }
        state_ = State::Vertex;
        prompt_vertex(ctx);
        return;
    case State::WidthVal: {
        double w = 0.0;
        if (t.empty()) {
            state_ = State::Option;
            prompt_option(ctx);
            return;
        }
        if (!parse_number(t, w) || w < 0.0) {
            ctx.echo("Requires a width of zero or more.");
            return;
        }
        op(8, {w, 0.0});
        state_ = State::Option;
        prompt_option(ctx);
        return;
    }
    case State::VWidthStart: {
        double w = 0.0;
        if (!t.empty() && (!parse_number(t, w) || w < 0.0)) {
            ctx.echo("Requires a width of zero or more.");
            return;
        }
        vw_start_ = w;
        state_ = State::VWidthEnd;
        ctx.set_prompt("Specify ending width for next segment <" + fmt4(w) + ">: ");
        return;
    }
    case State::VWidthEnd: {
        double w = vw_start_;
        if (!t.empty() && (!parse_number(t, w) || w < 0.0)) {
            ctx.echo("Requires a width of zero or more.");
            return;
        }
        op(20, {}, {vw_start_, w});
        state_ = State::Vertex;
        prompt_vertex(ctx);
        return;
    }
    case State::VInsert:
        if (const auto p = read_point(ctx, text)) {
            op(18, *p);
            state_ = State::Vertex;
            prompt_vertex(ctx);
        }
        return;
    case State::VMoveTo:
        if (const auto p = read_point(ctx, text)) {
            op(19, *p);
            pick_ = *p; // the moved vertex is on the polyline: keep picking there
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
        if (const bool pick = t == "~"; pick || u.ends_with(".MUSA") || u.ends_with(".DXF") ||
                                        t.find_first_of("/\\") != std::string::npos) {
            // A drawing: read in as a block named after the file.
            file_ = pick && ctx.view() != nullptr
                        ? ctx.view()->open_file_dialog("Drawings (*.musa *.dxf);;All files (*)")
                        : (pick ? std::string() : t);
            if (file_.empty()) {
                return; // nothing chosen: the name is asked again
            }
            name_ = std::filesystem::path(file_).stem().string();
            const std::vector<std::string> names = ctx.block_names();
            if (std::find(names.begin(), names.end(), name_) != names.end()) {
                state_ = State::Redefine;
                ctx.set_prompt("Block \"" + name_ + "\" already exists. Redefine it? [Yes/No] <N>: ");
                return;
            }
            ctx.submit(core::LoadBlockFileCommand{file_, name_, false});
            state_ = State::Point;
            ctx.set_prompt("Specify insertion point or [Scale/Rotate]: ");
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
    case State::Redefine:
        if (!u.empty() && u != "Y" && u != "YES" && u != "N" && u != "NO") {
            ctx.echo("Enter Yes or No.");
            return;
        }
        ctx.submit(core::LoadBlockFileCommand{file_, name_, u == "Y" || u == "YES"});
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
            poly_.clear();
            state_ = State::PolyFirst;
            ctx.set_prompt("Specify first point: ");
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
    case State::PolyFirst:
        if (const auto p = read_point(ctx, text)) {
            poly_ = {*p};
            ctx.set_last_point(*p);
            state_ = State::PolyNext;
            ctx.set_preview({PreviewKind::Polyline, poly_});
            ctx.set_prompt("Specify next point: ");
        }
        return;
    case State::PolyNext:
        if (t.empty() || u == "C" || u == "CLOSE") {
            if (poly_.size() < 3) {
                ctx.echo("A polygonal boundary needs at least three points.");
                return;
            }
            ctx.set_preview({});
            ctx.submit(core::SetImagePolyClipCommand{pick_, ctx.pick_radius(), poly_, ctx.group_id()});
            done_ = true;
            return;
        }
        if (u == "U" || u == "UNDO") {
            if (!poly_.empty()) {
                poly_.pop_back();
            }
            if (poly_.empty()) {
                state_ = State::PolyFirst;
                ctx.set_preview({});
                ctx.set_prompt("Specify first point: ");
            } else {
                ctx.set_last_point(poly_.back());
                ctx.set_preview({PreviewKind::Polyline, poly_});
            }
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            poly_.push_back(*p);
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Polyline, poly_});
            ctx.set_prompt(poly_.size() >= 3 ? "Specify next point or [Undo/Close] <Close>: "
                                             : "Specify next point or [Undo]: ");
        }
        return;
    }
}

namespace {
constexpr const char* kAciNames[8] = {"", "RED", "YELLOW", "GREEN", "CYAN", "BLUE", "MAGENTA", "WHITE"};
constexpr const char* kLinetypeNames[4] = {"Continuous", "Dashed", "Center", "Hidden"};
constexpr const char* kLinetypeDescriptions[4] = {"Solid line", "Dashed __ __ __ __", "Center ____ _ ____ _",
                                                  "Hidden __ __ __ __ (shorter)"};
/// A colour as AutoCAD names it: BYLAYER, a colour name for 1..7, else r,g,b.
std::string color_text(const core::EntityProps& p) {
    if (p.color_by_layer()) {
        return "BYLAYER";
    }
    for (long i = 1; i <= 7; ++i) {
        if (core::io::dxf_aci_to_rgb(i) == p.color) {
            return kAciNames[i];
        }
    }
    return std::to_string(p.color.r) + "," + std::to_string(p.color.g) + "," + std::to_string(p.color.b);
}
std::string lineweight_text(const core::EntityProps& p) {
    if (p.lineweight_by_layer()) {
        return "BYLAYER";
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f mm", p.lineweight / 100.0);
    return buf;
}
/// AutoCAD's lineweights (1/100 mm): a typed one goes to the nearest of them.
constexpr int kStdLineweights[] = {0, 5, 9, 13, 15, 18, 20, 25, 30, 35, 40, 50, 53, 60, 70, 80, 90, 100, 106, 120, 140, 158, 200, 211};
int nearest_lineweight(double hundredths) {
    int best = 0;
    for (const int w : kStdLineweights) {
        if (std::abs(w - hundredths) < std::abs(best - hundredths)) {
            best = w;
        }
    }
    return best;
}
} // namespace

void CurrentPropCommand::start(CommandContext& ctx) {
    if (dialog_ && ctx.view() != nullptr) {
        core::EntityProps p = ctx.current_props();
        int r = -1;
        if (kind_ == Kind::Color) {
            bool by_layer = p.color_by_layer();
            core::Rgb c = p.color;
            r = ctx.view()->select_color_dialog(by_layer, c);
            if (r == 1) {
                p.set_color_by_layer(by_layer);
                p.color = c;
            }
        } else if (kind_ == Kind::Lineweight) {
            bool by_layer = p.lineweight_by_layer();
            std::uint8_t lw = p.lineweight;
            r = ctx.view()->lineweight_dialog(by_layer, lw);
            if (r == 1) {
                p.set_lineweight_by_layer(by_layer);
                p.lineweight = lw;
            }
        } else {
            r = ctx.view()->linetype_manager() ? 0 : -1;
        }
        if (r >= 0) {
            if (r == 1) {
                set(ctx, p);
            }
            done_ = true;
            return;
        }
    }
    ask(ctx);
}

void CurrentPropCommand::ask(CommandContext& ctx) {
    const core::EntityProps p = ctx.current_props();
    switch (kind_) {
    case Kind::Color:
        step_ = Step::Value;
        if (name_ == "CECOLOR") {
            ctx.set_prompt("Enter new value for CECOLOR <\"" + color_text(p) + "\">: ");
        } else {
            ctx.set_prompt("Enter default object color [Truecolor/COlorbook] <" + color_text(p) + ">: ");
        }
        return;
    case Kind::Lineweight:
        step_ = Step::Value;
        if (name_ == "CELWEIGHT") {
            ctx.set_prompt("Enter new value for CELWEIGHT <" +
                           std::to_string(p.lineweight_by_layer() ? -1 : static_cast<int>(p.lineweight)) + ">: ");
        } else {
            ctx.echo("Current lineweight: " + lineweight_text(p));
            ctx.set_prompt("Enter default lineweight for new objects or [?]: ");
        }
        return;
    case Kind::Linetype:
        if (name_ == "CELTYPE") {
            step_ = Step::LtSet;
            ctx.set_prompt(std::string("Enter new value for CELTYPE <\"") +
                           (p.linetype_by_layer() ? "BYLAYER" : kLinetypeNames[static_cast<int>(p.linetype) & 3]) + "\">: ");
        } else {
            step_ = Step::LtOption;
            ctx.set_prompt("Enter an option [?/Create/Load/Set]: ");
        }
        return;
    }
}

void CurrentPropCommand::set(CommandContext& ctx, const core::EntityProps& p) {
    ctx.submit(core::SetCurrentPropsCommand{p});
}

void CurrentPropCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    std::string u = upper(t);
    if (u.size() >= 2 && u.front() == '"' && u.back() == '"') {
        u = u.substr(1, u.size() - 2); // CECOLOR "RED" as the prompt shows it
    }
    core::EntityProps p = ctx.current_props();
    switch (step_) {
    case Step::Value:
        if (kind_ == Kind::Color) {
            if (t.empty()) {
                done_ = true;
                return;
            }
            if (u == "T" || u == "TRUECOLOR") {
                step_ = Step::TrueColor;
                ctx.set_prompt("Red, Green, Blue: ");
                return;
            }
            if (u == "CO" || u == "COLORBOOK") {
                ctx.echo("Color books are not available; use a colour number or Truecolor.");
                return;
            }
            if (u == "BYLAYER") {
                p.set_color_by_layer(true);
            } else if (u == "BYBLOCK") {
                ctx.echo("ByBlock is not available yet; the colour stays " + color_text(p) + ".");
                return;
            } else {
                long aci = 0;
                for (long i = 1; i <= 7; ++i) {
                    aci = u == kAciNames[i] ? i : aci;
                }
                if (aci == 0) {
                    double n = 0.0;
                    const std::optional<double> v = parse_number(t, n) ? std::optional<double>(n) : std::nullopt;
                    if (!v || *v < 1.0 || *v > 255.0 || *v != std::floor(*v)) {
                        ctx.echo("Enter a colour number from 1 to 255, a colour name, BYLAYER or Truecolor.");
                        return;
                    }
                    aci = static_cast<long>(*v);
                }
                p.set_color_by_layer(false);
                p.color = core::io::dxf_aci_to_rgb(aci);
            }
            set(ctx, p);
            done_ = true;
            return;
        }
        // Lineweight.
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (u == "?") {
            std::string list = "BYLAYER, DEFAULT";
            for (const int w : kStdLineweights) {
                char buf[16];
                std::snprintf(buf, sizeof buf, ", %.2f", w / 100.0);
                list += buf;
            }
            ctx.echo(list + " (mm)");
            return;
        }
        if (u == "BYLAYER" || (name_ == "CELWEIGHT" && t == "-1")) {
            p.set_lineweight_by_layer(true);
        } else if (u == "DEFAULT" || (name_ == "CELWEIGHT" && t == "-3")) {
            p.set_lineweight_by_layer(false);
            p.lineweight = 25; // LWDEFAULT
        } else if (u == "BYBLOCK" || (name_ == "CELWEIGHT" && t == "-2")) {
            ctx.echo("ByBlock is not available yet.");
            return;
        } else {
            double n = 0.0;
            const std::optional<double> v = parse_number(t, n) ? std::optional<double>(n) : std::nullopt;
            const double hundredths = v ? (name_ == "CELWEIGHT" ? *v : *v * 100.0) : -1.0;
            if (!v || hundredths < 0.0 || hundredths > 211.0) {
                ctx.echo(name_ == "CELWEIGHT" ? "Requires -1, -3 or 0 to 211." : "Requires a lineweight from 0 to 2.11 mm.");
                return;
            }
            p.set_lineweight_by_layer(false);
            p.lineweight = static_cast<std::uint8_t>(nearest_lineweight(hundredths));
        }
        set(ctx, p);
        done_ = true;
        return;
    case Step::TrueColor: {
        int r = -1;
        int g = -1;
        int b = -1;
        if (std::sscanf(t.c_str(), "%d,%d,%d", &r, &g, &b) != 3 || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 ||
            b > 255) {
            ctx.echo("Enter red, green and blue, 0 to 255 each.");
            return;
        }
        p.set_color_by_layer(false);
        p.color = core::Rgb{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b)};
        set(ctx, p);
        done_ = true;
        return;
    }
    case Step::LtOption:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (u == "?") {
            step_ = Step::LtList;
            ctx.set_prompt("Enter linetype(s) to list <*>: ");
            return;
        }
        if (u == "S" || u == "SET") {
            step_ = Step::LtSet;
            ctx.set_prompt(std::string("Specify linetype name or [?] <") +
                           (p.linetype_by_layer() ? "BYLAYER" : kLinetypeNames[static_cast<int>(p.linetype) & 3]) + ">: ");
            return;
        }
        if (u == "C" || u == "CREATE" || u == "L" || u == "LOAD") {
            ctx.echo("Continuous, Dashed, Center and Hidden are loaded; other linetypes cannot be created or loaded yet.");
            return;
        }
        ctx.echo("Invalid option keyword.");
        return;
    case Step::LtList:
        ctx.echo("Name           Description");
        for (int i = 0; i < 4; ++i) {
            std::string line = kLinetypeNames[i];
            line.resize(15, ' ');
            ctx.echo(line + kLinetypeDescriptions[i]);
        }
        step_ = Step::LtOption;
        ctx.set_prompt("Enter an option [?/Create/Load/Set]: ");
        return;
    case Step::LtSet: {
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (u == "?") {
            ctx.echo("Linetypes: BYLAYER, Continuous, Dashed, Center, Hidden");
            return;
        }
        if (u == "BYLAYER") {
            p.set_linetype_by_layer(true);
        } else {
            int found = -1;
            for (int i = 0; i < 4; ++i) {
                found = u == upper(kLinetypeNames[i]) ? i : found;
            }
            if (found < 0) {
                ctx.echo("Linetype " + t + " is not loaded: Continuous, Dashed, Center or Hidden.");
                return;
            }
            p.set_linetype_by_layer(false);
            p.linetype = static_cast<core::Linetype>(found);
        }
        set(ctx, p);
        done_ = true;
        return;
    }
    }
}

void LwDisplayCommand::start(CommandContext& ctx) {
    const int on = ctx.view() != nullptr ? ctx.view()->selection_setting("LWDISPLAY") : 1;
    ctx.set_prompt(std::string("Enter new value for LWDISPLAY <") + (on != 0 ? "ON" : "OFF") + ">: ");
}

void LwDisplayCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    if (!u.empty()) {
        const bool on = u == "ON" || u == "1";
        if (!on && u != "OFF" && u != "0") {
            ctx.echo("Enter ON or OFF.");
            return;
        }
        if (ctx.view() == nullptr || !ctx.view()->set_selection_setting("LWDISPLAY", on ? 1 : 0)) {
            ctx.submit(core::SetLineweightDisplayCommand{on});
        }
    }
    done_ = true;
}

void RenameCommand::start(CommandContext& ctx) {
    step_ = Step::Kind;
    ctx.set_prompt("Enter object type to rename "
                   "[Block/Dimstyle/LAyer/LType/Material/multileadeRstyle/Plotstyle/textStyle/Tablestyle/Ucs/VPort/View]: ");
}

void RenameCommand::input(CommandContext& ctx, const std::string& text) {
    using K = core::RenameObjectCommand::Kind;
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::Kind:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (u == "B" || u == "BLOCK") {
            c_.kind = K::Block;
            what_ = "block";
        } else if (u == "D" || u == "DIMSTYLE") {
            c_.kind = K::DimStyle;
            what_ = "dimension style";
        } else if (u == "LA" || u == "LAYER") {
            c_.kind = K::Layer;
            what_ = "layer";
        } else if (u == "S" || u == "TEXTSTYLE") {
            c_.kind = K::TextStyle;
            what_ = "text style";
        } else if (u == "T" || u == "TABLESTYLE") {
            c_.kind = K::TableStyle;
            what_ = "table style";
        } else if (u == "V" || u == "VIEW") {
            c_.kind = K::View;
            what_ = "view";
        } else if (u == "LT" || u == "LTYPE" || u == "M" || u == "MATERIAL" || u == "R" || u == "MULTILEADERSTYLE" ||
                   u == "P" || u == "PLOTSTYLE" || u == "U" || u == "UCS" || u == "VP" || u == "VPORT") {
            ctx.echo("Those have no names to change here: the drawing has none of its own.");
            return;
        } else {
            ctx.echo("Invalid option keyword.");
            return;
        }
        step_ = Step::From;
        ctx.set_prompt("Enter old " + what_ + " name: ");
        return;
    case Step::From:
        if (t.empty()) {
            done_ = true;
            return;
        }
        c_.from = t;
        step_ = Step::To;
        ctx.set_prompt("Enter new " + what_ + " name: ");
        return;
    case Step::To:
        if (t.empty()) {
            done_ = true;
            return;
        }
        c_.to = t;
        ctx.submit(c_);
        done_ = true;
        return;
    }
}

void NcopyCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select nested objects to copy: ");
}

void NcopyCommand::copy_all(CommandContext& ctx, core::Vec2 offset) {
    for (const core::Vec2& p : picks_) {
        ctx.submit(core::NestedCopyCommand{p, radius_, offset, ctx.group_id()});
    }
}

void NcopyCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::Pick:
        if (t.empty()) {
            if (picks_.empty()) {
                done_ = true;
                return;
            }
            step_ = Step::Base;
            ctx.set_prompt("Specify base point or displacement, or [Multiple]: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            picks_.push_back(*p);
            radius_ = ctx.pick_radius();
            ctx.set_prompt("Select nested objects to copy: ");
        }
        return;
    case Step::Base:
        if (t.empty()) {
            copy_all(ctx, {}); // in place
            done_ = true;
            return;
        }
        if (u == "M" || u == "MULTIPLE") {
            multiple_ = true;
            ctx.set_prompt("Specify base point or displacement: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            base_ = *p;
            ctx.set_last_point(*p);
            step_ = Step::Second;
            ctx.set_preview(PreviewSpec{PreviewKind::Segment, {*p}});
            ctx.set_prompt(multiple_ ? "Specify second point of displacement: "
                                     : "Specify second point of displacement, or <use first point as displacement>: ");
        }
        return;
    case Step::Second:
        if (t.empty()) {
            ctx.clear_preview();
            if (!multiple_) {
                copy_all(ctx, base_); // the first point as the displacement
            }
            done_ = true;
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            copy_all(ctx, *p - base_);
            if (!multiple_) {
                ctx.clear_preview();
                done_ = true;
                return;
            }
            (void)ctx.new_group(); // each copy its own undo step
        }
        return;
    }
}

void DataExtractionCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    step_ = Step::From;
    ctx.set_prompt("Extract from [Drawing/Selection] <Drawing>: ");
}

void DataExtractionCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        ask_output(ctx);
    }
}

void DataExtractionCommand::ask_output(CommandContext& ctx) {
    step_ = Step::Output;
    ctx.set_prompt("Output to [Table/File/Both] <Table>: ");
}

void DataExtractionCommand::after_file(CommandContext& ctx) {
    if (table_) {
        step_ = Step::Place;
        ctx.set_prompt("Specify insertion point: ");
        return;
    }
    c_.group = ctx.group_id();
    ctx.submit(c_);
    done_ = true;
}

void DataExtractionCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::From:
        if (u == "S" || u == "SELECTION") {
            c_.all = false;
            if (ctx.has_selection()) {
                ask_output(ctx);
                return;
            }
            step_ = Step::Select;
            select_.begin(ctx, "Select objects: ");
            return;
        }
        if (!t.empty() && u != "D" && u != "DRAWING") {
            ctx.echo("Invalid option keyword.");
            return;
        }
        c_.all = true;
        ask_output(ctx);
        return;
    case Step::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            ask_output(ctx);
        }
        return;
    case Step::Output:
        if (u == "F" || u == "FILE") {
            table_ = false;
            file_ = true;
        } else if (u == "B" || u == "BOTH") {
            table_ = true;
            file_ = true;
        } else if (t.empty() || u == "T" || u == "TABLE") {
            table_ = true;
            file_ = false;
        } else {
            ctx.echo("Invalid option keyword.");
            return;
        }
        if (!file_) {
            after_file(ctx);
            return;
        }
        step_ = Step::File;
        if (ctx.view() != nullptr) {
            const std::string path = ctx.view()->save_file_dialog("CSV (comma delimited) (*.csv)", "extraction.csv");
            if (!path.empty()) {
                c_.path = path;
                after_file(ctx);
                return;
            }
        }
        ctx.set_prompt("Enter file name: ");
        return;
    case Step::File:
        if (t.empty()) {
            done_ = true;
            return;
        }
        c_.path = t.size() >= 4 && upper(t.substr(t.size() - 4)) == ".CSV" ? t : t + ".csv";
        after_file(ctx);
        return;
    case Step::Place:
        if (const auto p = read_point(ctx, text)) {
            c_.table_at = *p;
            c_.group = ctx.group_id();
            ctx.submit(c_);
            done_ = true;
        }
        return;
    }
}

void PlanCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    const bool ucs = u == "U" || u == "UCS";
    if (!u.empty() && !ucs && u != "C" && u != "CURRENT" && u != "CURRENT UCS" && u != "W" && u != "WORLD") {
        ctx.echo("Invalid option keyword.");
        return;
    }
    if (ucs) {
        ctx.echo("Only the World UCS is there; it is shown.");
    }
    if (ctx.view() != nullptr) {
        ctx.view()->zoom_extents();
    }
    done_ = true;
}

void SetVarCommand::input(CommandContext& ctx, const std::string& text) {
    // The system variables there are commands for (each asks `Enter new value for X <..>:`).
    static constexpr const char* kVariables[] = {
        "ATTDISP",    "AUTOSNAP",       "CECOLOR",   "CELTSCALE",  "CELTYPE",          "CELWEIGHT",
        "DIMLAYER",   "DIMSTYLE",       "EDGEMODE",  "FILLMODE",   "GRIPCOLOR",        "GRIPHOT",
        "GRIPS",      "GRIPSIZE",       "HIGHLIGHT", "IMAGEFRAME", "INSUNITS", "INSUNITSDEFSOURCE", "INSUNITSDEFTARGET", "LAYERPMODE",
        "LTSCALE",    "LWDISPLAY",      "MIRRTEXT",  "MSLTSCALE",  "OSMODE",           "PDMODE",
        "PDSIZE",     "PICKADD",        "PICKAUTO",  "PICKBOX",    "PICKDRAG",         "PICKFIRST",
        "POLARANG",   "PROJMODE",       "PSLTSCALE", "SELECTIONCYCLING", "SELECTIONPREVIEW", "TEMPOVERRIDES",
        "TEXTEDITMODE", "TEXTSIZE", "TRANSPARENCYDISPLAY", "TRIMEXTENDMODE"};
    if (sub_) {
        sub_->input(ctx, text);
        done_ = sub_->done();
        return;
    }
    const std::string u = upper(trimmed(text));
    if (u.empty()) {
        done_ = true;
        return;
    }
    if (u == "?") {
        std::string list;
        for (const char* v : kVariables) {
            list += (list.empty() ? "" : ", ") + std::string(v);
        }
        ctx.echo(list);
        return;
    }
    const bool known = std::any_of(std::begin(kVariables), std::end(kVariables), [&](const char* v) { return u == v; });
    sub_ = known ? ctx.make_command(u) : nullptr;
    if (!sub_) {
        ctx.echo("Unknown variable name.  Type SETVAR ? for a list of variables.");
        return;
    }
    sub_->start(ctx);
    done_ = sub_->done();
}

void NavBarCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    if (!u.empty() && u != "ON" && u != "OFF") {
        ctx.echo("Enter ON or OFF.");
        return;
    }
    if (ctx.view() == nullptr || !ctx.view()->set_navbar(u != "OFF")) {
        ctx.echo("NAVBAR needs the drawing window.");
    }
    done_ = true;
}

void UcsIconCommand::start(CommandContext& ctx) {
    if (ctx.view() == nullptr) {
        ctx.echo("UCSICON needs the drawing window.");
        done_ = true;
        return;
    }
    ask(ctx);
}

void UcsIconCommand::ask(CommandContext& ctx) {
    const bool on = (ctx.view()->selection_setting("UCSICON") & 1) != 0;
    ctx.set_prompt(std::string("Enter an option [ON/OFF/") + (all_ ? "" : "All/") + "Noorigin/ORigin] <" +
                   (on ? "ON" : "OFF") + ">: ");
}

void UcsIconCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    int v = ctx.view()->selection_setting("UCSICON");
    if (u.empty()) {
        done_ = true;
        return;
    }
    if ((u == "A" || u == "ALL") && !all_) {
        all_ = true;
        ask(ctx);
        return;
    }
    if (u == "ON") {
        v |= 1;
    } else if (u == "OFF") {
        v &= ~1;
    } else if (u == "N" || u == "NOORIGIN") {
        v &= ~2;
    } else if (u == "OR" || u == "ORIGIN") {
        v |= 2;
    } else {
        ctx.echo("Invalid option keyword.");
        ask(ctx);
        return;
    }
    (void)ctx.view()->set_selection_setting("UCSICON", v);
    done_ = true;
}

void WorkspaceCommand::start(CommandContext& ctx) {
    if (ctx.view() == nullptr || ctx.view()->workspaces().empty()) {
        ctx.echo(name() + " needs the drawing window.");
        done_ = true;
        return;
    }
    if (mode_ == Mode::Save) {
        (void)ctx.view()->workspace_op("dialog", {}, {});
        done_ = true;
        return;
    }
    ask(ctx, mode_ == Mode::Current ? Step::Current : Step::Option);
}

void WorkspaceCommand::ask(CommandContext& ctx, Step step) {
    step_ = step;
    const std::string cur = ctx.view()->current_workspace();
    switch (step) {
    case Step::Option:
        ctx.set_prompt("Enter workspace option [setCurrent/SAveas/Rename/Delete/?] <setCurrent>: ");
        break;
    case Step::Current:
        ctx.set_prompt(mode_ == Mode::Current ? "Enter new value for WSCURRENT <\"" + cur + "\">: "
                                              : "Enter name of workspace to make current [?] <" + cur + ">: ");
        break;
    case Step::SaveAs:
        ctx.set_prompt("Enter name of workspace to save [?] <" + cur + ">: ");
        break;
    case Step::Replace:
        ctx.set_prompt("Workspace \"" + name_ + "\" already exists. Replace it? [Yes/No] <N>: ");
        break;
    case Step::Rename:
        ctx.set_prompt("Enter name of workspace to rename [?]: ");
        break;
    case Step::RenameTo:
        ctx.set_prompt("Enter new name for workspace \"" + name_ + "\": ");
        break;
    case Step::Delete:
        ctx.set_prompt("Enter name of workspace to delete [?]: ");
        break;
    }
}

void WorkspaceCommand::list(CommandContext& ctx) const {
    const std::string cur = ctx.view()->current_workspace();
    ctx.echo("Workspaces:");
    for (const std::string& w : ctx.view()->workspaces()) {
        ctx.echo((w == cur ? "  * " : "    ") + w);
    }
}

void WorkspaceCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    const std::vector<std::string> names = ctx.view()->workspaces();
    // A name as the user typed it, matched to a workspace whatever its case.
    const auto known = [&](const std::string& n) -> std::string {
        for (const std::string& w : names) {
            if (upper(w) == upper(n)) {
                return w;
            }
        }
        return {};
    };
    if (u == "?" && step_ != Step::Replace && step_ != Step::RenameTo) {
        list(ctx);
        ask(ctx, step_);
        return;
    }
    switch (step_) {
    case Step::Option:
        if (u.empty() || u == "C" || u == "SETCURRENT") {
            ask(ctx, Step::Current);
        } else if (u == "SA" || u == "SAVEAS") {
            ask(ctx, Step::SaveAs);
        } else if (u == "R" || u == "RENAME") {
            ask(ctx, Step::Rename);
        } else if (u == "D" || u == "DELETE") {
            ask(ctx, Step::Delete);
        } else {
            ctx.echo("Invalid option keyword.");
            ask(ctx, Step::Option);
        }
        return;
    case Step::Current: {
        if (t.empty()) {
            done_ = true;
            return;
        }
        const std::string w = known(t);
        if (w.empty()) {
            ctx.echo("Workspace \"" + t + "\" not found.");
            ask(ctx, Step::Current);
            return;
        }
        (void)ctx.view()->workspace_op("current", w, {});
        done_ = true;
        return;
    }
    case Step::SaveAs:
        name_ = t.empty() ? ctx.view()->current_workspace() : t;
        if (const std::string w = known(name_); !w.empty()) {
            name_ = w;
            if (!t.empty()) {
                ask(ctx, Step::Replace);
                return;
            }
        }
        if (!ctx.view()->workspace_op("saveas", name_, {})) {
            ctx.echo("\"" + name_ + "\" is built in: save the workspace under another name.");
        }
        done_ = true;
        return;
    case Step::Replace:
        if (u == "Y" || u == "YES") {
            if (!ctx.view()->workspace_op("saveas", name_, {})) {
                ctx.echo("\"" + name_ + "\" is built in: save the workspace under another name.");
            }
        }
        done_ = true;
        return;
    case Step::Rename:
    case Step::Delete: {
        if (t.empty()) {
            done_ = true;
            return;
        }
        const std::string w = known(t);
        if (w.empty()) {
            ctx.echo("Workspace \"" + t + "\" not found.");
            ask(ctx, step_);
            return;
        }
        if (w == names.front()) {
            ctx.echo("\"" + w + "\" is built in: it cannot be renamed or deleted.");
            done_ = true;
            return;
        }
        name_ = w;
        if (step_ == Step::Rename) {
            ask(ctx, Step::RenameTo);
            return;
        }
        if (!ctx.view()->workspace_op("delete", name_, {})) {
            ctx.echo("The current workspace cannot be deleted.");
        }
        done_ = true;
        return;
    }
    case Step::RenameTo:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (!known(t).empty() && upper(t) != upper(name_)) {
            ctx.echo("Workspace \"" + t + "\" already exists.");
            ask(ctx, Step::RenameTo);
            return;
        }
        (void)ctx.view()->workspace_op("rename", name_, t);
        done_ = true;
        return;
    }
}

void DimStyleFileCommand::start(CommandContext& ctx) {
    if (import_) {
        ask_file(ctx);
        return;
    }
    step_ = Step::Names;
    ctx.set_prompt("Dimension styles to export <*>: ");
}

void DimStyleFileCommand::ask_file(CommandContext& ctx) {
    step_ = Step::File;
    if (ctx.view() != nullptr) {
        const std::string filter = "Dimension styles (*.dim);;All files (*)";
        const std::string path =
            import_ ? ctx.view()->open_file_dialog(filter) : ctx.view()->save_file_dialog(filter, "styles.dim");
        if (!path.empty()) {
            got_file(ctx, path);
            return;
        }
    }
    ctx.set_prompt(import_ ? "Import filename: " : "Export filename: ");
}

void DimStyleFileCommand::got_file(CommandContext& ctx, const std::string& path) {
    path_ = path;
    if (!import_) {
        if (path_.size() < 4 || upper(path_.substr(path_.size() - 4)) != ".DIM") {
            path_ += ".dim";
        }
        ctx.submit(core::ExportDimStylesCommand{path_, names_ == "*" ? std::string() : names_});
        done_ = true;
        return;
    }
    step_ = Step::Overwrite;
    ctx.set_prompt("Existing styles of the same name [Keep/Overwrite] <Keep>: ");
}

void DimStyleFileCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::Names:
        names_ = t.empty() ? std::string("*") : t;
        ask_file(ctx);
        return;
    case Step::File:
        if (t.empty()) {
            done_ = true; // no file: nothing done
            return;
        }
        got_file(ctx, t);
        return;
    case Step::Overwrite:
        if (!t.empty() && u != "K" && u != "KEEP" && u != "O" && u != "OVERWRITE") {
            ctx.echo("Enter Keep or Overwrite.");
            return;
        }
        ctx.submit(core::ImportDimStylesCommand{path_, u == "O" || u == "OVERWRITE"});
        done_ = true;
        return;
    }
}

void DimStyleFileCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void UpdateFieldCommand::start(CommandContext& ctx) {
    if (ctx.has_selection()) {
        after(ctx);
        return;
    }
    select_.begin(ctx, "Select objects: ");
}

void UpdateFieldCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        after(ctx);
    }
}

void UpdateFieldCommand::input(CommandContext& ctx, const std::string& text) {
    if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
        after(ctx);
    }
}

void UpdateFieldCommand::after(CommandContext& ctx) {
    if (ctx.has_selection()) {
        ctx.submit(core::UpdateFieldsCommand{}); // counts them; a field is worked out each time it is drawn
    }
    done_ = true;
}

void UpdateFieldCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void BlockEditUiCommand::start(CommandContext& ctx) {
    using Op = core::BlockEditCommand::Op;
    switch (mode_) {
    case Mode::Open:
        if (ctx.view() != nullptr && ctx.view()->window_action("BEDIT")) {
            done_ = true; // the Edit Block Definition window asks
            return;
        }
        [[fallthrough]];
    case Mode::OpenTyped:
        ctx.set_prompt("Enter block name or [?]: ");
        return;
    case Mode::Close:
        ctx.set_prompt("Save changes to block? [Yes/No] <Yes>: ");
        return;
    case Mode::Save: {
        core::BlockEditCommand c;
        c.op = Op::Save;
        c.group = ctx.group_id();
        ctx.submit(std::move(c));
        done_ = true;
        return;
    }
    }
}

void BlockEditUiCommand::input(CommandContext& ctx, const std::string& text) {
    using Op = core::BlockEditCommand::Op;
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    if (mode_ == Mode::Close) {
        if (!u.empty() && u != "Y" && u != "YES" && u != "N" && u != "NO") {
            ctx.echo("Enter Yes or No.");
            return;
        }
        core::BlockEditCommand c;
        c.op = Op::Close;
        c.save = !(u == "N" || u == "NO");
        c.group = ctx.group_id();
        ctx.submit(std::move(c));
        done_ = true;
        return;
    }
    if (t.empty()) {
        done_ = true;
        return;
    }
    if (u == "?") {
        std::string list;
        for (const std::string& n : ctx.block_names()) {
            list += (list.empty() ? "" : ", ") + n;
        }
        ctx.echo(list.empty() ? std::string("No blocks in this drawing.") : list);
        ctx.set_prompt("Enter block name or [?]: ");
        return;
    }
    core::BlockEditCommand c;
    c.op = Op::Open;
    c.name = t;
    c.group = ctx.group_id();
    ctx.submit(std::move(c));
    done_ = true;
}

void ExtrimUiCommand::input(CommandContext& ctx, const std::string& text) {
    if (trimmed(text).empty()) {
        done_ = true;
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    if (!edge_) {
        edge_ = *p;
        ctx.set_prompt("Specify the side to trim on:");
        return;
    }
    ctx.submit(core::ExtrimCommand{*edge_, *p, ctx.pick_radius(), ctx.group_id()});
    done_ = true;
}

void VpSyncUiCommand::input(CommandContext& ctx, const std::string& text) {
    if (!picked_) {
        if (trimmed(text).empty()) {
            done_ = true;
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            master_ = *p;
            picked_ = true;
            select_.begin(ctx, "Select viewports to be aligned to master viewport: ");
        }
        return;
    }
    if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void VpSyncUiCommand::finish(CommandContext& ctx) {
    if (ctx.has_selection()) {
        ctx.submit(core::VpSyncCommand{master_, ctx.pick_radius(), ctx.group_id()});
    }
    done_ = true;
}

bool XListCommand::trimmed_empty(const std::string& t) {
    return trimmed(t).empty();
}

std::optional<core::Vec2> XListCommand::pick_point(CommandContext& ctx, const std::string& text) {
    return read_point(ctx, text);
}

void AttFileCommand::start(CommandContext& ctx) {
    if (in_ || ctx.has_selection()) {
        ask_file(ctx);
        return;
    }
    select_.begin(ctx, "Select objects: ");
}

void AttFileCommand::ask_file(CommandContext& ctx) {
    if (!in_ && !ctx.has_selection()) {
        done_ = true;
        return;
    }
    if (ctx.view() != nullptr) {
        const std::string filter = "Text files (*.txt);;All files (*)";
        const std::string path =
            in_ ? ctx.view()->open_file_dialog(filter) : ctx.view()->save_file_dialog(filter, "attributes.txt");
        if (!path.empty()) {
            finish(ctx, path);
        }
        done_ = true;
        return;
    }
    asking_name_ = true;
    ctx.set_prompt("Enter file name: ");
}

void AttFileCommand::finish(CommandContext& ctx, const std::string& path) {
    if (in_) {
        ctx.submit(core::AttInCommand{path, ctx.group_id()});
    } else {
        ctx.submit(core::AttOutCommand{path});
    }
}

void AttFileCommand::input(CommandContext& ctx, const std::string& text) {
    if (asking_name_) {
        const std::string t = trimmed(text);
        if (!t.empty()) {
            finish(ctx, t);
        }
        done_ = true;
        return;
    }
    if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
        ask_file(ctx);
    }
}

void DataLinkUpdateUiCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Select an option [Update data link/Write data link] <Update data link>: ");
}

void DataLinkUpdateUiCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        after_select(ctx);
    }
}

void DataLinkUpdateUiCommand::after_select(CommandContext& ctx) {
    if (ctx.has_selection()) {
        core::DataLinkUpdateCommand c;
        c.write = write_;
        c.selection = true;
        c.group = ctx.group_id();
        ctx.submit(std::move(c));
    }
    done_ = true;
}

void DataLinkUpdateUiCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::Option:
        if (u.empty() || u == "U" || u == "UPDATE" || u == "UPDATE DATA LINK") {
            write_ = false;
        } else if (u == "W" || u == "WRITE" || u == "WRITE DATA LINK") {
            write_ = true;
        } else {
            ctx.echo("Invalid option keyword.");
            start(ctx);
            return;
        }
        step_ = Step::Objects;
        select_.begin(ctx, write_ ? "Select objects: " : "Select objects or [Data link/all data linKs]: ");
        return;
    case Step::Objects:
        if (!write_ && (u == "D" || u == "DATA LINK")) {
            step_ = Step::LinkName;
            ctx.set_prompt("Enter data link name or [?]: ");
            return;
        }
        if (!write_ && (u == "K" || u == "ALL DATA LINKS")) {
            core::DataLinkUpdateCommand c;
            c.group = ctx.group_id();
            ctx.submit(std::move(c));
            done_ = true;
            return;
        }
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            after_select(ctx);
        }
        return;
    case Step::LinkName: {
        const std::vector<std::string> names = ctx.data_link_names();
        if (u == "?") {
            ctx.echo(names.empty() ? std::string("No data links in this drawing.") : "Data links:");
            for (const std::string& n : names) {
                ctx.echo("  " + n);
            }
            ctx.set_prompt("Enter data link name or [?]: ");
            return;
        }
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (std::none_of(names.begin(), names.end(), [&](const std::string& n) { return upper(n) == u; })) {
            ctx.echo("Data link \"" + t + "\" not found.");
            ctx.set_prompt("Enter data link name or [?]: ");
            return;
        }
        core::DataLinkUpdateCommand c;
        c.link = t;
        c.group = ctx.group_id();
        ctx.submit(std::move(c));
        done_ = true;
        return;
    }
    }
}

void DataLinkUpdateUiCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
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
                ctx.submit(core::SetActiveSpaceCommand{0, std::string()});
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

namespace {
/// PSPACE from a viewport edit: the model view on screen becomes the viewport's view.
void leave_mspace(CommandContext& ctx) {
    core::Vec2 center{};
    double scale = 0.0;
    if (ctx.view() == nullptr || !ctx.view()->current_view(center, scale)) {
        scale = 0.0; // keep the viewport's scale when there is no camera (headless)
    }
    ctx.submit(core::LeaveMspaceCommand{center, scale});
}
} // namespace

void ModelSpaceCommand::start(CommandContext& ctx) {
    if (ctx.mspace_active()) {
        leave_mspace(ctx);
        if (!to_paper_) {
            ctx.submit(core::SetActiveSpaceCommand{0, std::string()});
        }
    } else if (!to_paper_) {
        ctx.submit(core::SetActiveSpaceCommand{0, std::string()});
    } else if (ctx.active_space() != 0) {
        ctx.echo("Already in paper space.");
    } else {
        ctx.submit(core::SetActiveSpaceCommand{0xFF, std::string()}); // the engine picks the first layout
    }
    done_ = true;
}

void MspaceCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (ctx.mspace_active()) {
        ctx.echo("Already editing model space through a viewport; PSPACE returns to the sheet.");
        done_ = true;
        return;
    }
    if (ctx.active_space() == 0) {
        ctx.echo("MSPACE works on a layout with a viewport (double-click inside one).");
        done_ = true;
        return;
    }
    ctx.set_prompt("Select viewport (or double-click inside one): ");
}

void MspaceCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void MspaceCommand::input(CommandContext& ctx, const std::string& text) {
    if (const auto p = read_point(ctx, text)) {
        core::Vec2 center{};
        double ppm = 1.0;
        if (ctx.view() == nullptr || !ctx.view()->current_view(center, ppm) || ppm <= 0.0) {
            ppm = 1.0;
        }
        ctx.submit(core::EnterMspaceCommand{*p, ctx.pick_radius(), ppm});
        done_ = true;
    }
}

// ---------------------------------------------------------------------------
// VPORTS
// ---------------------------------------------------------------------------
namespace {
/// The live tiles, or one whole-window tile when the view cannot say.
std::vector<core::TiledViewport> live_tiles(CommandContext& ctx, int& active) {
    std::vector<core::TiledViewport> tiles = ctx.view() != nullptr ? ctx.view()->tiled_viewports()
                                                                   : std::vector<core::TiledViewport>{};
    active = ctx.view() != nullptr ? ctx.view()->active_tile() : 0;
    if (tiles.empty()) {
        tiles.push_back(core::TiledViewport{});
        active = 0;
    }
    if (active < 0 || static_cast<std::size_t>(active) >= tiles.size()) {
        active = 0;
    }
    return tiles;
}
/// The tile containing the point (window fractions), or none.
std::optional<std::size_t> tile_at(const std::vector<core::TiledViewport>& tiles, core::Vec2 p) {
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const core::TiledViewport& t = tiles[i];
        if (p.x >= t.x0 && p.x <= t.x1 && p.y >= t.y0 && p.y <= t.y1) {
            return i;
        }
    }
    return std::nullopt;
}
} // namespace

void VportsCommand::prompt_option(CommandContext& ctx) {
    state_ = State::Option;
    ctx.set_prompt("Enter an option [Save/Restore/Delete/Join/SIngle/?/2/3/4] <3>: ");
}

void VportsCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (ctx.active_space() != 0 && !ctx.mspace_active()) {
        ctx.echo("VPORTS tiles the model-space window; a layout has MVIEW viewports instead.");
        done_ = true;
        return;
    }
    prompt_option(ctx);
}

void VportsCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void VportsCommand::apply_split(CommandContext& ctx, const char* kind) {
    int active = 0;
    std::vector<core::TiledViewport> tiles = live_tiles(ctx, active);
    const std::vector<core::TiledViewport> parts =
        core::split_vport(tiles[static_cast<std::size_t>(active)], kind);
    tiles.erase(tiles.begin() + active);
    tiles.insert(tiles.begin() + active, parts.begin(), parts.end());
    core::VportsCommand c;
    c.op = core::VportsCommand::Op::Set;
    c.tiles = std::move(tiles);
    c.active = active;
    ctx.submit(c);
    done_ = true;
}

void VportsCommand::input(CommandContext& ctx, const std::string& text) {
    using Op = core::VportsCommand::Op;
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Option: {
        if (t.empty() || u == "3") {
            state_ = State::Three;
            ctx.set_prompt("Enter a configuration option [Horizontal/Vertical/Above/Below/Left/Right] <Right>: ");
            return;
        }
        if (u == "2") {
            state_ = State::Two;
            ctx.set_prompt("Enter a configuration option [Horizontal/Vertical] <Vertical>: ");
            return;
        }
        if (u == "4") {
            apply_split(ctx, "4");
            return;
        }
        if (u == "SI" || u == "SINGLE") {
            int active = 0;
            const std::vector<core::TiledViewport> tiles = live_tiles(ctx, active);
            core::TiledViewport one = tiles[static_cast<std::size_t>(active)];
            one.x0 = 0.0;
            one.y0 = 0.0;
            one.x1 = 1.0;
            one.y1 = 1.0;
            core::VportsCommand c;
            c.op = Op::Set;
            c.tiles = {one};
            ctx.submit(c);
            done_ = true;
            return;
        }
        if (u == "S" || u == "SAVE") {
            state_ = State::SaveName;
            ctx.set_prompt("Enter name for new viewport configuration: ");
            return;
        }
        if (u == "R" || u == "RESTORE") {
            state_ = State::RestoreName;
            ctx.set_prompt("Enter name of viewport configuration to restore: ");
            return;
        }
        if (u == "D" || u == "DELETE") {
            state_ = State::DeleteName;
            ctx.set_prompt("Enter name of viewport configuration to delete: ");
            return;
        }
        if (u == "J" || u == "JOIN") {
            int active = 0;
            if (live_tiles(ctx, active).size() < 2) {
                ctx.echo("Only one viewport: nothing to join.");
                return;
            }
            state_ = State::JoinDominant;
            ctx.set_prompt("Select dominant viewport <current>: ");
            return;
        }
        if (u == "?") {
            core::VportsCommand c;
            c.op = Op::List;
            ctx.submit(c);
            return;
        }
        ctx.echo("Enter S, R, D, J, SI, ?, 2, 3 or 4.");
        return;
    }
    case State::Two: {
        if (t.empty() || u == "V" || u == "VERTICAL") {
            apply_split(ctx, "2v");
        } else if (u == "H" || u == "HORIZONTAL") {
            apply_split(ctx, "2h");
        } else {
            ctx.echo("Enter H (horizontal) or V (vertical).");
        }
        return;
    }
    case State::Three: {
        if (t.empty() || u == "R" || u == "RIGHT") {
            apply_split(ctx, "3r");
        } else if (u == "L" || u == "LEFT") {
            apply_split(ctx, "3l");
        } else if (u == "A" || u == "ABOVE") {
            apply_split(ctx, "3a");
        } else if (u == "B" || u == "BELOW") {
            apply_split(ctx, "3b");
        } else if (u == "V" || u == "VERTICAL") {
            apply_split(ctx, "3v");
        } else if (u == "H" || u == "HORIZONTAL") {
            apply_split(ctx, "3h");
        } else {
            ctx.echo("Enter H, V, A, B, L or R.");
        }
        return;
    }
    case State::SaveName:
    case State::RestoreName:
    case State::DeleteName: {
        if (t.empty()) {
            prompt_option(ctx);
            return;
        }
        core::VportsCommand c;
        c.name = t;
        if (state_ == State::SaveName) {
            int active = 0;
            c.op = Op::Save;
            c.tiles = live_tiles(ctx, active);
        } else {
            c.op = state_ == State::RestoreName ? Op::Restore : Op::Delete;
        }
        ctx.submit(c);
        done_ = true;
        return;
    }
    case State::JoinDominant: {
        int active = 0;
        const std::vector<core::TiledViewport> tiles = live_tiles(ctx, active);
        dominant_ = static_cast<std::size_t>(active);
        if (!t.empty()) {
            // A point in window fractions (0..1) names a viewport, as the dialog would.
            const auto p = read_point(ctx, text);
            const auto hit = p ? tile_at(tiles, *p) : std::nullopt;
            if (!hit) {
                ctx.echo("Enter a point inside a viewport (window fractions, 0..1) or press Enter.");
                return;
            }
            dominant_ = *hit;
        }
        state_ = State::JoinOther;
        ctx.set_prompt("Select viewport to join: ");
        return;
    }
    case State::JoinOther: {
        int active = 0;
        std::vector<core::TiledViewport> tiles = live_tiles(ctx, active);
        const auto p = read_point(ctx, text);
        const auto hit = p ? tile_at(tiles, *p) : std::nullopt;
        if (!hit) {
            ctx.echo("Enter a point inside the viewport to join (window fractions, 0..1).");
            return;
        }
        if (!core::join_vports(tiles, dominant_, *hit)) {
            ctx.echo("The selected viewports do not form a rectangle.");
            done_ = true;
            return;
        }
        core::VportsCommand c;
        c.op = Op::Set;
        c.tiles = std::move(tiles);
        c.active = static_cast<int>(dominant_ < *hit ? dominant_ : dominant_ - 1);
        ctx.submit(c);
        done_ = true;
        return;
    }
    }
}

// ---------------------------------------------------------------------------
// EATTEDIT / BATTMAN (dialogs through the view)
// ---------------------------------------------------------------------------
void EatteditCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select a block reference: ");
}

void EatteditCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void EatteditCommand::input(CommandContext& ctx, const std::string& text) {
    if (const auto p = read_point(ctx, text)) {
        if (ctx.view() != nullptr) {
            ctx.view()->attribute_editor_at(*p, ctx.pick_radius());
        }
        done_ = true;
        return;
    }
    if (trimmed(text).empty()) {
        done_ = true;
        return;
    }
    ctx.echo("Pick a block reference that has attributes.");
}

void BattmanCommand::start(CommandContext& ctx) {
    if (ctx.view() != nullptr) {
        ctx.view()->block_attribute_manager();
    }
    done_ = true;
}

void BattmanCommand::input(CommandContext&, const std::string&) {}

void BattmanCommand::cancel(CommandContext&) { done_ = true; }

// ---------------------------------------------------------------------------
// VPLAYER
// ---------------------------------------------------------------------------
namespace {
/// "walls, doors  TEXT" -> {"walls", "doors", "TEXT"}: layer lists are comma or space
/// separated, as at AutoCAD's layer-name prompts.
std::vector<std::string> split_layer_names(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (const char ch : text) {
        if (ch == ',' || ch == ' ' || ch == '\t') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}
} // namespace

void VplayerCommand::prompt_option(CommandContext& ctx) {
    state_ = State::Option;
    names_.clear();
    from_selection_ = false;
    default_mode_ = false;
    ctx.set_prompt("Enter an option [?/Freeze/Thaw/Reset/Newfrz/Vpvisdflt]: ");
}

void VplayerCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    prompt_option(ctx);
}

void VplayerCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void VplayerCommand::ask_target(CommandContext& ctx) {
    state_ = State::Target;
    ctx.set_prompt("Enter an option [All/Select/Current] <Current>: ");
}

void VplayerCommand::finish(CommandContext& ctx, core::SetViewportLayerFreezeCommand::Target target,
                            core::Vec2 pick) {
    core::SetViewportLayerFreezeCommand c;
    c.op = op_;
    c.target = target;
    c.pick = pick;
    c.pick_radius = ctx.pick_radius();
    c.layer_names = names_;
    c.from_selection = from_selection_;
    ctx.submit(c);
    prompt_option(ctx); // AutoCAD: VPLAYER returns to its option prompt until Enter
}

void VplayerCommand::input(CommandContext& ctx, const std::string& text) {
    using Op = core::SetViewportLayerFreezeCommand::Op;
    using Target = core::SetViewportLayerFreezeCommand::Target;
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Option: {
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (u == "?") {
            core::SetViewportLayerFreezeCommand c;
            c.op = Op::List;
            ctx.submit(c);
            return;
        }
        if (u == "F" || u == "FREEZE" || u == "T" || u == "THAW") {
            op_ = (u[0] == 'F') ? Op::Freeze : Op::Thaw;
            state_ = State::Layers;
            ctx.set_prompt(std::string("Enter layer name(s) to ") + (op_ == Op::Freeze ? "freeze" : "thaw") +
                           " or <select objects>: ");
            return;
        }
        if (u == "R" || u == "RESET") {
            op_ = Op::Reset;
            ask_target(ctx);
            return;
        }
        if (u == "N" || u == "NEWFRZ") {
            state_ = State::NewNames;
            ctx.set_prompt("Enter name(s) of new layers frozen in all viewports: ");
            return;
        }
        if (u == "V" || u == "VPVISDFLT") {
            state_ = State::DefaultLayers;
            ctx.set_prompt("Enter layer name(s) to change viewport visibility or <select objects>: ");
            return;
        }
        ctx.echo("Enter ?, F (freeze), T (thaw), R (reset), N (newfrz) or V (vpvisdflt).");
        return;
    }
    case State::Layers:
    case State::DefaultLayers: {
        default_mode_ = state_ == State::DefaultLayers;
        if (t.empty()) {
            state_ = State::SelectObjects;
            ctx.set_prompt("Select objects: ");
            return;
        }
        names_ = split_layer_names(t);
        if (default_mode_) {
            state_ = State::DefaultValue;
            ctx.set_prompt("Enter a viewport visibility [Frozen/Thawed] <Thawed>: ");
        } else {
            ask_target(ctx);
        }
        return;
    }
    case State::SelectObjects: {
        if (t.empty()) {
            if (!ctx.has_selection()) {
                ctx.echo("Nothing selected.");
                prompt_option(ctx);
                return;
            }
            from_selection_ = true;
            if (default_mode_) {
                state_ = State::DefaultValue;
                ctx.set_prompt("Enter a viewport visibility [Frozen/Thawed] <Thawed>: ");
            } else {
                ask_target(ctx);
            }
            return;
        }
        if (u == "ALL") {
            ctx.submit(core::SelectAllCommand{});
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            ctx.submit(core::SelectPickCommand{*p, ctx.pick_radius(), true, true});
            return;
        }
        ctx.echo("Pick objects, type ALL, or press Enter to finish.");
        return;
    }
    case State::Target: {
        if (t.empty() || u == "C" || u == "CURRENT") {
            finish(ctx, Target::Current, {});
        } else if (u == "A" || u == "ALL") {
            finish(ctx, Target::All, {});
        } else if (u == "S" || u == "SELECT") {
            state_ = State::PickViewport;
            ctx.set_prompt("Select viewport: ");
        } else {
            ctx.echo("Enter A (all), S (select) or C (current).");
        }
        return;
    }
    case State::PickViewport: {
        if (const auto p = read_point(ctx, text)) {
            finish(ctx, Target::Pick, *p);
            return;
        }
        ctx.echo("Pick a point inside the viewport (or on its frame).");
        return;
    }
    case State::NewNames: {
        if (!t.empty()) {
            core::SetViewportLayerFreezeCommand c;
            c.op = Op::Newfrz;
            c.layer_names = split_layer_names(t);
            ctx.submit(c);
        }
        prompt_option(ctx);
        return;
    }
    case State::DefaultValue: {
        core::SetViewportLayerFreezeCommand c;
        c.op = Op::VisDefault;
        c.layer_names = names_;
        c.from_selection = from_selection_;
        c.value = (u == "F" || u == "FROZEN");
        if (!t.empty() && !c.value && u != "T" && u != "THAWED") {
            ctx.echo("Enter F (frozen) or T (thawed).");
            return;
        }
        ctx.submit(c);
        prompt_option(ctx);
        return;
    }
    }
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

void XrefCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::Option;
    ctx.set_prompt("Enter an option [?/Attach/Detach/Reload] <Attach>: ");
}

void XrefCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void XrefCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Option:
        if (u == "?") {
            ctx.submit(core::XrefListCommand{});
            done_ = true;
            return;
        }
        if (u == "D" || u == "DETACH" || u == "R" || u == "RELOAD") {
            detach_ = u == "D" || u == "DETACH";
            state_ = State::Name;
            ctx.set_prompt(detach_ ? "Enter xref name to detach: " : "Enter xref name to reload <all>: ");
            return;
        }
        if (!(t.empty() || u == "A" || u == "ATTACH")) {
            ctx.echo("Enter ?, Attach, Detach or Reload.");
            return;
        }
        state_ = State::File;
        ctx.set_prompt("Enter drawing file name (~ for the file dialog): ");
        return;
    case State::File:
        if (t == "~" || t.empty()) {
            path_ = ctx.view() != nullptr
                        ? ctx.view()->open_file_dialog("Drawings (*.musa *.dxf);;All files (*)")
                        : std::string();
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
        ctx.set_prompt("Specify rotation angle <0>: ");
        return;
    }
    case State::Rotation: {
        double deg = 0.0;
        if (!t.empty() && !parse_number(t, deg)) {
            ctx.echo("Enter an angle in degrees.");
            return;
        }
        ctx.submit(core::XrefAttachCommand{path_, pos_, scale_, core::to_radians(deg), ctx.group_id()});
        done_ = true;
        return;
    }
    case State::Name:
        if (detach_) {
            if (t.empty()) {
                ctx.echo("An xref name is required.");
                return;
            }
            ctx.submit(core::XrefDetachCommand{t, ctx.group_id()});
        } else {
            ctx.submit(core::XrefReloadCommand{t});
        }
        done_ = true;
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
    if (dialog_ && ctx.view() != nullptr && ctx.view()->text_style_dialog()) {
        done_ = true; // the Text Style window does the rest
        return;
    }
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
        if (!t.empty() && u != "Y" && u != "YES" && u != "N" && u != "NO") {
            ctx.echo("Enter Yes or No.");
            return;
        }
        if (state_ == State::Backwards) {
            ts_.backwards = u == "Y" || u == "YES";
        } else if (state_ == State::Upside) {
            ts_.upside_down = u == "Y" || u == "YES";
        } else if (u == "Y" || u == "YES") {
            ctx.echo("Vertical text is not available yet; the style stays horizontal.");
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
void UnitsCommand::prompts(CommandContext& ctx) {
    ctx.echo("Report formats:      (Examples)");
    ctx.echo("1.  Scientific       1.55E+01");
    ctx.echo("2.  Decimal          15.50");
    ctx.echo("3.  Engineering      1'-3.50\"");
    ctx.echo("4.  Architectural    1'-3 1/2\"");
    ctx.echo("5.  Fractional       15 1/2");
    ctx.set_prompt("Enter choice, 1 to 5 <" + std::to_string(static_cast<int>(u_.linear)) + ">: ");
}

void UnitsCommand::start(CommandContext& ctx) {
    u_ = ctx.units();
    if (dialog_ && ctx.view() != nullptr && ctx.view()->units_dialog()) {
        done_ = true; // the Drawing Units dialog does the rest
        return;
    }
    state_ = State::Linear;
    prompts(ctx);
}

void UnitsCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void UnitsCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    double v = 0.0;
    const bool fractional = u_.linear == core::LinearFormat::Architectural || u_.linear == core::LinearFormat::Fractional;
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
            ctx.echo("Enter a choice from 1 to 5.");
            return;
        }
        state_ = State::LinearPrecision;
        if (u_.linear == core::LinearFormat::Architectural || u_.linear == core::LinearFormat::Fractional) {
            ctx.set_prompt("Enter denominator of smallest fraction to display (1, 2, 4, 8, 16, 32, 64, 128, or 256) <" +
                           std::to_string(1 << std::min<int>(u_.linear_precision, 8)) + ">: ");
        } else {
            ctx.set_prompt("Enter number of digits to right of decimal point (0 to 8) <" +
                           std::to_string(u_.linear_precision) + ">: ");
        }
        return;
    case State::LinearPrecision:
        if (!t.empty()) {
            if (!parse_number(t, v) || v < 0.0) {
                ctx.echo(fractional ? "Enter 1, 2, 4, 8, 16, 32, 64, 128 or 256." : "Enter a value from 0 to 8.");
                return;
            }
            if (fractional) {
                int n = 0;
                while (n < 8 && (1 << n) < static_cast<int>(v)) {
                    ++n;
                }
                if ((1 << n) != static_cast<int>(v)) {
                    ctx.echo("Enter 1, 2, 4, 8, 16, 32, 64, 128 or 256.");
                    return;
                }
                u_.linear_precision = static_cast<std::uint8_t>(n);
            } else {
                if (v > 8.0) {
                    ctx.echo("Enter a value from 0 to 8.");
                    return;
                }
                u_.linear_precision = static_cast<std::uint8_t>(v);
            }
        }
        state_ = State::Angular;
        ctx.echo("Systems of angle measure:      (Examples)");
        ctx.echo("1.  Decimal degrees          45.0000");
        ctx.echo("2.  Degrees/minutes/seconds  45d0'0\"");
        ctx.echo("3.  Grads                    50.0000g");
        ctx.echo("4.  Radians                  0.7854r");
        ctx.echo("5.  Surveyor's units         N 45d0'0\" E");
        ctx.set_prompt("Enter choice, 1 to 5 <" + std::to_string(static_cast<int>(u_.angular) + 1) + ">: ");
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
            ctx.echo("Enter a choice from 1 to 5.");
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
        ctx.echo("Direction for angle 0:");
        ctx.echo("   East    3 o'clock =    0");
        ctx.echo("   North  12 o'clock =   90");
        ctx.echo("   West    9 o'clock =  180");
        ctx.echo("   South   6 o'clock =  270");
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
        ctx.set_prompt(std::string("Do you want angles measured clockwise? [Yes/No] <") + (u_.clockwise ? "Y" : "N") + ">: ");
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

void InsunitsCommand::start(CommandContext& ctx) {
    const core::DrawingUnits u = ctx.units();
    ctx.set_prompt("Enter new value for INSUNITS <" + std::to_string(u.insunits) + ">: ");
}

void InsunitsCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    core::DrawingUnits u = ctx.units();
    if (!t.empty()) {
        double v = 0.0;
        if (!parse_number(t, v) || v < 0.0 || v > 20.0 || v != std::floor(v)) {
            ctx.echo("Enter a value from 0 to 20 (0 unitless, 1 inches, 2 feet, 4 millimeters, 5 centimeters, 6 meters ...).");
            return;
        }
        u.insunits = static_cast<std::uint8_t>(v);
        ctx.submit(core::SetUnitsCommand{u});
        ctx.echo(std::string("INSUNITS = ") + std::to_string(u.insunits) + " (" + core::units::insunits_name(u.insunits) + ")");
    }
    done_ = true;
}

void InsunitsCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
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

void XlineCommand::preview(CommandContext& ctx, int mode) {
    PreviewSpec pv;
    pv.kind = PreviewKind::Xline;
    pv.xline_mode = mode;
    pv.xline_ray = ray_;
    pv.xline_angle = angle_;
    if (mode == 0) {
        pv.points = {root_};
    } else if (mode == 4) {
        pv.points = {bvertex_, bstart_};
    }
    ctx.set_preview(pv);
}

void XlineCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::First;
    mode_ = 0;
    if (ray_) {
        ctx.set_prompt("Specify start point: ");
    } else {
        ctx.set_prompt("Specify a point or [Hor/Ver/Ang/Bisect/Offset]: ");
    }
}

void XlineCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    double v = 0.0;
    switch (state_) {
    case State::First: {
        if (!ray_) {
            if (u == "H" || u == "HOR") {
                mode_ = 1;
                state_ = State::Through;
                preview(ctx, 1);
                ctx.set_prompt("Specify through point: ");
                return;
            }
            if (u == "V" || u == "VER") {
                mode_ = 2;
                state_ = State::Through;
                preview(ctx, 2);
                ctx.set_prompt("Specify through point: ");
                return;
            }
            if (u == "A" || u == "ANG") {
                state_ = State::Angle;
                ctx.set_prompt("Enter angle of xline (0) or [Reference]: ");
                return;
            }
            if (u == "B" || u == "BISECT") {
                state_ = State::BisectVertex;
                ctx.set_prompt("Specify angle vertex point: ");
                return;
            }
            if (u == "O" || u == "OFFSET") {
                state_ = State::OffsetDist;
                ctx.set_prompt("Specify offset distance or [Through] <" +
                               (s_offset_ > 0.0 ? core::units::format_length(s_offset_, ctx.units()) : std::string("Through")) +
                               ">: ");
                return;
            }
        }
        if (const auto p = read_point(ctx, text)) {
            root_ = *p;
            ctx.set_last_point(*p);
            state_ = State::Through;
            preview(ctx, 0);
            ctx.set_prompt("Specify through point: ");
        }
        return;
    }
    case State::Angle: {
        if (u == "R" || u == "REFERENCE") {
            state_ = State::RefLine;
            ctx.set_prompt("Select a line object: ");
            return;
        }
        double deg = 0.0;
        if (!t.empty() && !parse_number(t, deg)) {
            ctx.echo("Enter an angle in degrees.");
            return;
        }
        angle_ = core::to_radians(deg);
        mode_ = 3;
        state_ = State::Through;
        preview(ctx, 3);
        ctx.set_prompt("Specify through point: ");
        return;
    }
    case State::RefLine:
        if (const auto p = read_point(ctx, text)) {
            ref_pick_ = *p;
            state_ = State::RefAngle;
            ctx.set_prompt("Enter angle of xline <0>: ");
        }
        return;
    case State::RefAngle: {
        double deg = 0.0;
        if (!t.empty() && !parse_number(t, deg)) {
            ctx.echo("Enter an angle in degrees.");
            return;
        }
        angle_ = core::to_radians(deg);
        mode_ = 4; // relative to the reference line: the engine resolves it per point
        state_ = State::Through;
        ctx.clear_preview();
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
        if (mode_ == 4) {
            ctx.submit(core::XlineReferenceCommand{*p, ref_pick_, ctx.pick_radius(), angle_, ctx.group_id()});
            ctx.set_prompt("Specify through point: ");
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
            if (core::length_squared(dir) <= 1e-24) {
                ctx.echo("The through point must differ from the first point.");
                return;
            }
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
            ctx.set_preview({PreviewKind::Segment, {bvertex_}});
            ctx.set_prompt("Specify angle start point: ");
        }
        return;
    case State::BisectStart:
        if (const auto p = read_point(ctx, text)) {
            bstart_ = *p;
            state_ = State::BisectEnd;
            preview(ctx, 4);
            ctx.set_prompt("Specify angle end point: ");
        }
        return;
    case State::BisectEnd: {
        if (t.empty()) {
            done_ = true;
            return;
        }
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        const core::Vec2 d0 = core::normalized(bstart_ - bvertex_);
        const core::Vec2 d1 = core::normalized(*p - bvertex_);
        const core::Vec2 bis = d0 + d1; // the angle bisector direction
        if (core::length_squared(bis) <= 1e-24) {
            ctx.echo("The two directions are opposite: no bisector.");
            return;
        }
        emit(ctx, bvertex_, bis);
        ctx.set_prompt("Specify angle end point: "); // more bisectors from the same start
        return;
    }
    case State::OffsetDist:
        if (t.empty()) {
            // the remembered distance (or Through)
        } else if (u == "T" || u == "THROUGH") {
            s_offset_ = 0.0;
        } else if (parse_number(t, v) && v > 0.0) {
            s_offset_ = v;
        } else {
            ctx.echo("Value must be positive and nonzero.");
            return;
        }
        state_ = State::OffsetLine;
        ctx.set_prompt("Select a line object: ");
        return;
    case State::OffsetLine:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            offset_pick_ = *p;
            state_ = State::OffsetSide;
            ctx.set_prompt(s_offset_ > 0.0 ? "Specify side to offset: " : "Specify through point: ");
        }
        return;
    case State::OffsetSide:
        if (const auto p = read_point(ctx, text)) {
            ctx.submit(core::XlineOffsetCommand{offset_pick_, ctx.pick_radius(), s_offset_, *p,
                                                s_offset_ <= 0.0, ctx.group_id()});
            state_ = State::OffsetLine; // the next line, until Enter
            ctx.set_prompt("Select a line object: ");
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
    // The prompt follows the type in force, which is remembered for the session.
    static constexpr const char* kFirst[] = {"Specify first corner point", "Specify start point",
                                             "Specify first point"};
    ctx.set_prompt(std::string(kFirst[s_type_]) +
                   " or [Arc length/Object/Rectangular/Polygonal/Freehand/Style] <Object>: ");
}

void RevcloudCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    static constexpr const char* kType[] = {"Rectangular", "Polygonal", "Freehand"};
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Minimum arc length: %.4f   Maximum arc length: %.4f   Style: %s   Type: %s",
                  min_arc_, max_arc_, s_calligraphy_ ? "Calligraphy" : "Normal", kType[s_type_]);
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
    if (s_calligraphy_) {
        pc.widths = core::polyline_ops::calligraphy_widths(pc.points, closed);
    }
    pc.group = ctx.group_id();
    ctx.clear_preview();
    ctx.submit(std::move(pc));
    ctx.echo("Revision cloud created.");
    done_ = true;
}

void RevcloudCommand::freehand(CommandContext& ctx, const std::vector<core::Vec2>& path) {
    // Thin the drag to points an arc apart, then cloud it: closed when it ends back near
    // where it started (within two arc lengths), open otherwise.
    const double step = arc_len();
    std::vector<core::Vec2> pts;
    for (const core::Vec2& p : path) {
        if (pts.empty() || core::distance(pts.back(), p) >= step) {
            pts.push_back(p);
        }
    }
    if (pts.size() < 2) {
        ctx.echo("Drag further: the path is shorter than an arc.");
        return;
    }
    const bool closed = pts.size() >= 3 && core::distance(pts.front(), path.back()) <= 2.0 * step;
    if (!closed && core::distance(pts.back(), path.back()) > 1e-9) {
        pts.push_back(path.back());
    }
    emit_cloud(ctx, pts, closed);
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
        if (u == "R" || u == "RECTANGULAR" || u == "P" || u == "POLYGONAL" || u == "F" || u == "FREEHAND") {
            s_type_ = u.front() == 'R' ? 0 : (u.front() == 'P' ? 1 : 2);
            main_prompt(ctx);
            return;
        }
        if (u == "S" || u == "STYLE") {
            state_ = State::Style;
            ctx.set_prompt(std::string("Select arc style [Normal/Calligraphy] <") +
                           (s_calligraphy_ ? "Calligraphy" : "Normal") + ">: ");
            return;
        }
        if (u == "M" || u == "MODIFY") {
            ctx.echo("Modify is not supported yet; draw a new cloud.");
            return;
        }
        // A point at the main prompt starts the type in force: a rectangle's first corner,
        // or a path clicked point by point.
        if (const auto p = read_point(ctx, text); p && s_type_ == 0) {
            first_ = *p;
            ctx.set_last_point(*p);
            state_ = State::RectSecond;
            ctx.set_preview(PreviewSpec{PreviewKind::Rectangle, {first_}});
            ctx.set_prompt("Specify opposite corner: ");
        } else if (p) {
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
            cmd.calligraphy = s_calligraphy_;
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
            s_calligraphy_ = true;
        } else if (u == "N" || u == "NORMAL") {
            s_calligraphy_ = false;
        } else if (!t.empty()) {
            ctx.echo("Invalid option keyword.");
            return;
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
    select_.begin(ctx);
}

void ExplodeCommand::finish(CommandContext& ctx) {
    if (ctx.has_selection()) {
        ctx.submit(core::ExplodeSelectionCommand{ctx.group_id(), burst_});
    }
    done_ = true;
}

void ExplodeCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void ExplodeCommand::input(CommandContext& ctx, const std::string& text) {
    if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
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
    ctx.set_prompt("Enter number of sides <" + std::to_string(s_sides_) + ">: ");
}

void PolygonCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    switch (state_) {
    case State::Sides: {
        if (!t.empty()) {
            const int n = parse_int(t, 0);
            if (n < 3 || n > 1024) {
                ctx.echo("Requires an integer between 3 and 1024.");
                return;
            }
            sides_ = n;
            s_sides_ = n;
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
        if (!t.empty()) {
            inscribed_ = !(u == "C" || u == "CIRCUMSCRIBED");
            s_inscribed_ = inscribed_;
        }
        state_ = State::Radius;
        refresh_preview(ctx);
        ctx.set_prompt("Specify radius of circle: ");
        return;
    }
    case State::Radius: {
        double dist = 0.0;
        double angle = 0.0;
        if (double typed = 0.0; parse_number(t, typed)) {
            // A typed radius stands the polygon on a horizontal bottom edge (AutoCAD):
            // inscribed, a vertex sits half a step past straight down; circumscribed, the
            // bottom edge's midpoint is straight down.
            dist = typed;
            angle = inscribed_ ? -core::kHalfPi - core::kPi / static_cast<double>(sides_) : -core::kHalfPi;
        } else {
            const auto p = read_point(ctx, text);
            if (!p) {
                return;
            }
            const core::Vec2 r = *p - center_; // the pick fixes the orientation
            dist = core::length(r);
            angle = std::atan2(r.y, r.x);
        }
        if (dist <= 1e-12) {
            ctx.echo("The radius must be greater than zero.");
            return;
        }
        const std::vector<core::Vec2> v = core::polygon_vertices(center_, dist, sides_, inscribed_, angle);
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
            PreviewSpec pv{PreviewKind::Polygon, {edge1_}};
            pv.sides = sides_;
            pv.polygon_edge = true;
            ctx.set_preview(pv);
            ctx.set_prompt("Specify second endpoint of edge: ");
        }
        return;
    case State::Edge2: {
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        // Edge mode: the two picks ARE one side; the polygon stands on it, to its left
        // (the side AutoCAD builds towards) -- the same rule the band draws.
        const std::vector<core::Vec2> v = core::polygon_on_edge(edge1_, *p, sides_);
        if (v.empty()) {
            ctx.echo("The two edge endpoints must differ.");
            return;
        }
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
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Current point modes:  PDMODE=%u  PDSIZE=%s", static_cast<unsigned>(ctx.pdmode()),
                  fmt4(ctx.pdsize()).c_str());
    ctx.echo(buf);
    ctx.set_prompt("Specify a point: ");
}

void PointCommand::input(CommandContext& ctx, const std::string& text) {
    if (const auto p = read_point(ctx, text)) {
        ctx.submit(core::AddPointCommand{*p, ctx.group_id(), {}});
        ctx.set_last_point(*p);
        done_ = true; // one point; MULTIPLE POINT (the ribbon's Multiple Points) goes on
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
    block_.clear();
    ctx.set_prompt(measure_ ? "Select object to measure: " : "Select object to divide: ");
}

void DivideCommand::amount_prompt(CommandContext& ctx) {
    state_ = State::Amount;
    if (block_.empty()) {
        ctx.set_prompt(measure_ ? "Specify length of segment or [Block]: " : "Enter the number of segments or [Block]: ");
    } else {
        ctx.set_prompt(measure_ ? "Specify length of segment: " : "Enter the number of segments: ");
    }
}

void DivideCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Pick:
        if (const auto p = read_point(ctx, text)) {
            pick_ = *p;
            amount_prompt(ctx);
        }
        return;
    case State::BlockName:
        if (t.empty()) {
            ctx.echo("Enter the name of a block.");
            return;
        }
        block_ = t;
        state_ = State::BlockAlign;
        ctx.set_prompt("Align block with object? [Yes/No] <Y>: ");
        return;
    case State::BlockAlign:
        if (t.empty() || u == "Y" || u == "YES") {
            align_ = true;
        } else if (u == "N" || u == "NO") {
            align_ = false;
        } else {
            ctx.echo("Enter Yes or No.");
            return;
        }
        amount_prompt(ctx);
        return;
    case State::Amount: {
        if (block_.empty() && (u == "B" || u == "BLOCK")) {
            state_ = State::BlockName;
            ctx.set_prompt("Enter name of block to insert: ");
            return;
        }
        core::DividePathCommand cmd;
        cmd.pick = pick_;
        cmd.pick_radius = ctx.pick_radius();
        cmd.group = ctx.group_id();
        cmd.block = block_;
        cmd.align = align_;
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
    ctx.clear_last_point();
    if (!ctx.has_selection()) {
        state_ = State::Select;
        select_.begin(ctx);
        return;
    }
    begin(ctx);
}

void ArrayCommand::selection_gesture(CommandContext& ctx) {
    if (state_ == State::Select && select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        input(ctx, "");
    }
}

void ArrayCommand::begin(CommandContext& ctx) {
    state_ = State::Type;
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
    case State::Select:
        if (select_.active() && select_.input(ctx, text) == SelectObjectsPhase::Result::Continue) {
            return;
        }
        if (!ctx.has_selection()) {
            done_ = true;
            return;
        }
        begin(ctx);
        return;
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

// ---------------------------------------------------------------------------
// FILLET (radius, then two lines)
// ---------------------------------------------------------------------------
void FilletCommand::prompt_first(CommandContext& ctx) {
    state_ = State::First;
    ctx.set_prompt("Select first object or [Undo/Polyline/Radius/Trim/Multiple]: ");
}

namespace {
/// FILLET / CHAMFER's second-object prompt: what a pick there would make shows under the
/// cursor.
void fillet_band(CommandContext& ctx, core::Vec2 first, double radius, bool trim, bool chamfer, double d1, double d2) {
    PreviewSpec pv;
    pv.fillet_hover = true;
    pv.fillet_first = first;
    pv.fillet_radius = radius;
    pv.fillet_trim = trim;
    pv.fillet_chamfer = chamfer;
    pv.chamfer_d1 = d1;
    pv.chamfer_d2 = d2;
    ctx.set_preview(std::move(pv));
}
} // namespace

void FilletCommand::start(CommandContext& ctx) {
    ctx.echo(std::string("Current settings: Mode = ") + (s_trim_ ? "TRIM" : "NOTRIM") +
             ", Radius = " + core::units::format_length(s_radius_, ctx.units()));
    prompt_first(ctx);
}

void FilletCommand::after_fillet(CommandContext& ctx) {
    if (multiple_) {
        (void)ctx.new_group(); // each fillet its own undo step
        prompt_first(ctx);
    } else {
        done_ = true;
    }
}

void FilletCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    double v = 0.0;
    switch (state_) {
    case State::First:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (u == "U" || u == "UNDO") {
            if (last_group_ == 0) {
                ctx.echo("Nothing to undo.");
                return;
            }
            ctx.submit(core::UndoLastGroupCommand{ctx.command_group()});
            last_group_ = 0;
            return;
        }
        if (u == "P" || u == "POLYLINE") {
            state_ = State::Polyline;
            ctx.set_prompt("Select 2D polyline or [Radius]: ");
            return;
        }
        if (u == "R" || u == "RADIUS") {
            return_ = State::First;
            state_ = State::Radius;
            ctx.set_prompt("Specify fillet radius <" + core::units::format_length(s_radius_, ctx.units()) + ">: ");
            return;
        }
        if (u == "T" || u == "TRIM") {
            state_ = State::TrimMode;
            ctx.set_prompt(std::string("Enter Trim mode option [Trim/No trim] <") + (s_trim_ ? "Trim" : "No trim") + ">: ");
            return;
        }
        if (u == "M" || u == "MULTIPLE") {
            multiple_ = true;
            prompt_first(ctx);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            pick1_ = *p;
            state_ = State::Second;
            fillet_band(ctx, pick1_, s_radius_, s_trim_, false, 0.0, 0.0);
            ctx.set_prompt("Select second object or shift-select to apply corner or [Radius]: ");
        }
        return;
    case State::Second:
        if (u == "R" || u == "RADIUS") {
            ctx.set_preview({});
            return_ = State::Second;
            state_ = State::Radius;
            ctx.set_prompt("Specify fillet radius <" + core::units::format_length(s_radius_, ctx.units()) + ">: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            // Shift at the pick: a sharp corner (radius 0) this once.
            const double r = ctx.shift_held() ? 0.0 : s_radius_;
            ctx.set_preview({});
            last_group_ = ctx.group_id();
            ctx.submit(core::FilletPickCommand{pick1_, *p, r, ctx.pick_radius(), last_group_, s_trim_});
            after_fillet(ctx);
        }
        return;
    case State::Radius:
        if (t.empty()) {
            // Enter keeps the radius
        } else if (parse_number(t, v) && v >= 0.0) {
            s_radius_ = v;
        } else if (const auto p = read_point(ctx, text)) {
            s_radius_ = ctx.last_point() ? core::distance(*ctx.last_point(), *p) : 0.0;
        } else {
            ctx.echo("Value must be positive or zero.");
            return;
        }
        state_ = return_;
        if (state_ == State::First) {
            prompt_first(ctx);
        } else if (state_ == State::Second) {
            fillet_band(ctx, pick1_, s_radius_, s_trim_, false, 0.0, 0.0);
            ctx.set_prompt("Select second object or shift-select to apply corner or [Radius]: ");
        } else {
            ctx.set_prompt("Select 2D polyline or [Radius]: ");
        }
        return;
    case State::TrimMode:
        if (t.empty() || u == "T" || u == "TRIM") {
            s_trim_ = true;
        } else if (u == "N" || u == "NO TRIM" || u == "NOTRIM" || u == "NO") {
            s_trim_ = false;
        } else {
            ctx.echo("Enter Trim or No trim.");
            return;
        }
        prompt_first(ctx);
        return;
    case State::Polyline:
        if (u == "R" || u == "RADIUS") {
            return_ = State::Polyline;
            state_ = State::Radius;
            ctx.set_prompt("Specify fillet radius <" + core::units::format_length(s_radius_, ctx.units()) + ">: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            last_group_ = ctx.group_id();
            ctx.submit(core::FilletPolylineCommand{*p, s_radius_, ctx.pick_radius(), last_group_});
            after_fillet(ctx);
        }
        return;
    }
}

void FilletCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// CHAMFER (Distance method, or Angle method: a length on the first line and an angle)
// ---------------------------------------------------------------------------
double ChamferCommand::dist1() const {
    return s_angle_method_ ? s_length_ : s_dist1_;
}

double ChamferCommand::dist2() const {
    return s_angle_method_ ? s_length_ * std::tan(core::to_radians(s_angle_)) : s_dist2_;
}

void ChamferCommand::prompt_first(CommandContext& ctx) {
    state_ = State::First;
    ctx.set_prompt("Select first line or [Undo/Polyline/Distance/Angle/Trim/mEthod/Multiple]: ");
}

void ChamferCommand::start(CommandContext& ctx) {
    const std::string mode = s_trim_ ? "(TRIM mode) " : "(NOTRIM mode) ";
    if (s_angle_method_) {
        ctx.echo(mode + "Current chamfer Length = " + core::units::format_length(s_length_, ctx.units()) +
                 ", Angle = " + core::units::format_angle(core::to_radians(s_angle_), ctx.units()));
    } else {
        ctx.echo(mode + "Current chamfer Dist1 = " + core::units::format_length(s_dist1_, ctx.units()) +
                 ", Dist2 = " + core::units::format_length(s_dist2_, ctx.units()));
    }
    prompt_first(ctx);
}

void ChamferCommand::after_chamfer(CommandContext& ctx) {
    if (multiple_) {
        (void)ctx.new_group();
        prompt_first(ctx);
    } else {
        done_ = true;
    }
}

void ChamferCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    const auto back = [&] {
        state_ = return_;
        if (state_ == State::First) {
            prompt_first(ctx);
        } else if (state_ == State::Second) {
            fillet_band(ctx, pick1_, 0.0, s_trim_, true, dist1(), dist2());
            ctx.set_prompt("Select second line or shift-select to apply corner or [Distance/Angle/Method]: ");
        } else {
            ctx.set_prompt("Select 2D polyline or [Distance/Angle/mEthod]: ");
        }
    };
    const auto ask_distance = [&](State ret) {
        return_ = ret;
        state_ = State::Dist1;
        ctx.set_prompt("Specify first chamfer distance <" + core::units::format_length(s_dist1_, ctx.units()) + ">: ");
    };
    const auto ask_angle = [&](State ret) {
        return_ = ret;
        state_ = State::AngleLen;
        ctx.set_prompt("Specify chamfer length on the first line <" +
                       core::units::format_length(s_length_, ctx.units()) + ">: ");
    };
    const auto ask_method = [&](State ret) {
        return_ = ret;
        state_ = State::Method;
        ctx.set_prompt(std::string("Enter trim method [Distance/Angle] <") + (s_angle_method_ ? "Angle" : "Distance") + ">: ");
    };
    double v = 0.0;
    switch (state_) {
    case State::First:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (u == "U" || u == "UNDO") {
            if (last_group_ == 0) {
                ctx.echo("Nothing to undo.");
                return;
            }
            ctx.submit(core::UndoLastGroupCommand{ctx.command_group()});
            last_group_ = 0;
            return;
        }
        if (u == "P" || u == "POLYLINE") {
            state_ = State::Polyline;
            ctx.set_prompt("Select 2D polyline or [Distance/Angle/mEthod]: ");
            return;
        }
        if (u == "D" || u == "DISTANCE") {
            ask_distance(State::First);
            return;
        }
        if (u == "A" || u == "ANGLE") {
            ask_angle(State::First);
            return;
        }
        if (u == "T" || u == "TRIM") {
            state_ = State::TrimMode;
            ctx.set_prompt(std::string("Enter Trim mode option [Trim/No trim] <") + (s_trim_ ? "Trim" : "No trim") + ">: ");
            return;
        }
        if (u == "E" || u == "METHOD") {
            ask_method(State::First);
            return;
        }
        if (u == "M" || u == "MULTIPLE") {
            multiple_ = true;
            prompt_first(ctx);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            pick1_ = *p;
            state_ = State::Second;
            fillet_band(ctx, pick1_, 0.0, s_trim_, true, dist1(), dist2());
            ctx.set_prompt("Select second line or shift-select to apply corner or [Distance/Angle/Method]: ");
        }
        return;
    case State::Second:
        if (u == "D" || u == "DISTANCE" || u == "A" || u == "ANGLE" || u == "M" || u == "METHOD") {
            ctx.set_preview({});
        }
        if (u == "D" || u == "DISTANCE") {
            ask_distance(State::Second);
            return;
        }
        if (u == "A" || u == "ANGLE") {
            ask_angle(State::Second);
            return;
        }
        if (u == "M" || u == "METHOD") {
            ask_method(State::Second);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            const bool sharp = ctx.shift_held(); // a clean corner this once
            ctx.set_preview({});
            last_group_ = ctx.group_id();
            ctx.submit(core::ChamferPickCommand{pick1_, *p, sharp ? 0.0 : dist1(), sharp ? 0.0 : dist2(),
                                                ctx.pick_radius(), last_group_, s_trim_});
            after_chamfer(ctx);
        }
        return;
    case State::Dist1:
        if (t.empty()) {
            // keeps the first distance
        } else if (parse_number(t, v) && v >= 0.0) {
            s_dist1_ = v;
        } else if (const auto p = read_point(ctx, text)) {
            s_dist1_ = ctx.last_point() ? core::distance(*ctx.last_point(), *p) : 0.0;
        } else {
            ctx.echo("Value must be positive or zero.");
            return;
        }
        s_angle_method_ = false;
        state_ = State::Dist2;
        // AutoCAD defaults the second distance to the first just entered.
        ctx.set_prompt("Specify second chamfer distance <" + core::units::format_length(s_dist1_, ctx.units()) + ">: ");
        return;
    case State::Dist2:
        if (t.empty()) {
            s_dist2_ = s_dist1_;
        } else if (parse_number(t, v) && v >= 0.0) {
            s_dist2_ = v;
        } else if (const auto p = read_point(ctx, text)) {
            s_dist2_ = ctx.last_point() ? core::distance(*ctx.last_point(), *p) : 0.0;
        } else {
            ctx.echo("Value must be positive or zero.");
            return;
        }
        back();
        return;
    case State::AngleLen:
        if (t.empty()) {
            // keeps the length
        } else if (parse_number(t, v) && v >= 0.0) {
            s_length_ = v;
        } else if (const auto p = read_point(ctx, text)) {
            s_length_ = ctx.last_point() ? core::distance(*ctx.last_point(), *p) : 0.0;
        } else {
            ctx.echo("Value must be positive or zero.");
            return;
        }
        state_ = State::AngleVal;
        ctx.set_prompt("Specify chamfer angle from the first line <" +
                       core::units::format_angle(core::to_radians(s_angle_), ctx.units()) + ">: ");
        return;
    case State::AngleVal:
        if (t.empty()) {
            // keeps the angle
        } else if (parse_number(t, v)) {
            s_angle_ = v;
        } else {
            ctx.echo("Requires valid numeric angle.");
            return;
        }
        s_angle_method_ = true;
        back();
        return;
    case State::Method:
        if (t.empty()) {
            // keeps the method
        } else if (u == "D" || u == "DISTANCE") {
            s_angle_method_ = false;
        } else if (u == "A" || u == "ANGLE") {
            s_angle_method_ = true;
        } else {
            ctx.echo("Enter Distance or Angle.");
            return;
        }
        back();
        return;
    case State::TrimMode:
        if (t.empty() || u == "T" || u == "TRIM") {
            s_trim_ = true;
        } else if (u == "N" || u == "NO TRIM" || u == "NOTRIM" || u == "NO") {
            s_trim_ = false;
        } else {
            ctx.echo("Enter Trim or No trim.");
            return;
        }
        prompt_first(ctx);
        return;
    case State::Polyline:
        if (u == "D" || u == "DISTANCE") {
            ask_distance(State::Polyline);
            return;
        }
        if (u == "A" || u == "ANGLE") {
            ask_angle(State::Polyline);
            return;
        }
        if (u == "E" || u == "METHOD") {
            ask_method(State::Polyline);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            last_group_ = ctx.group_id();
            ctx.submit(core::ChamferPolylineCommand{*p, dist1(), dist2(), ctx.pick_radius(), last_group_});
            after_chamfer(ctx);
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
namespace {
constexpr const char* kJustifyList = "[Left/Center/Right/Align/Middle/Fit/TL/TC/TR/ML/MC/MR/BL/BC/BR]";
// The line under a line: AutoCAD's single-line spacing, 5 / 3 of the height.
constexpr double kTextLineSpacing = 5.0 / 3.0;
} // namespace

void TextCommand::read_style(CommandContext& ctx) {
    const std::vector<core::TextStyle> styles = ctx.text_styles();
    const std::uint16_t cur = ctx.current_text_style();
    style_.clear();
    style_factor_ = 1.0;
    fixed_height_ = false;
    if (cur > 0 && cur < styles.size()) {
        style_ = styles[cur].name;
        style_factor_ = styles[cur].width_factor;
        if (styles[cur].height > 0.0) {
            height_ = styles[cur].height; // a style with a height fixes it: no prompt
            fixed_height_ = true;
        }
    }
}

void TextCommand::prompt_point(CommandContext& ctx) {
    state_ = State::Point;
    ctx.clear_preview();
    ctx.set_prompt(justify_ == 0 ? std::string("Specify start point of text or [Justify/Style]: ")
                                 : std::string(core::text::kJustifyInfo[justify_].prompt));
}

void TextCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    read_style(ctx);
    ctx.echo("Current text style: \"" + (style_.empty() ? std::string("Standard") : style_) +
             "\"  Text height: " + core::units::format_length(height_, ctx.units()) +
             "  Annotative: No  Justify: " + core::text::kJustifyInfo[justify_].name);
    // The remembered justification is said, but each run starts at the start point
    // prompt, as AutoCAD's does.
    ctx.set_prompt("Specify start point of text or [Justify/Style]: ");
}

void TextCommand::prompt_height(CommandContext& ctx) {
    state_ = State::Height;
    ctx.set_preview({PreviewKind::Segment, {pos_}});
    ctx.set_prompt("Specify height <" + core::units::format_length(height_, ctx.units()) + ">: ");
}

void TextCommand::prompt_rotation(CommandContext& ctx) {
    state_ = State::Rotation;
    ctx.set_preview({PreviewKind::Segment, {pos_}});
    ctx.set_prompt("Specify rotation angle of text <" + core::units::format_angle(rotation_, ctx.units()) +
                   ">: ");
}

void TextCommand::show_line(CommandContext& ctx) {
    PreviewSpec pv;
    pv.kind = PreviewKind::Text;
    pv.points = {pos_, align_};
    pv.text_justify = justify_;
    pv.text_height = height_;
    pv.text_rotation = rotation_;
    pv.text_width_factor = style_factor_;
    ctx.set_preview(pv);
}

void TextCommand::begin_content(CommandContext& ctx) {
    state_ = State::Content;
    s_height_ = height_;
    s_rotation_ = rotation_;
    s_justify_ = justify_;
    show_line(ctx);
    ctx.set_prompt("Enter text: ");
}

void TextCommand::after_point(CommandContext& ctx) {
    // Aligned takes its height and its direction from its two points, Fit its direction.
    if (justify_ == 3) {
        begin_content(ctx);
    } else if (justify_ == 5) {
        if (fixed_height_) {
            begin_content(ctx);
        } else {
            prompt_height(ctx);
        }
    } else if (fixed_height_) {
        prompt_rotation(ctx);
    } else {
        prompt_height(ctx);
    }
}

void TextCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Point: {
        if (u == "J" || u == "JUSTIFY") {
            state_ = State::Justify;
            ctx.set_prompt(std::string("Enter an option ") + kJustifyList + ": ");
            return;
        }
        if (u == "S" || u == "STYLE") {
            state_ = State::Style;
            ctx.set_prompt("Enter style name or [?] <" + (style_.empty() ? std::string("Standard") : style_) +
                           ">: ");
            return;
        }
        // A justification may be given here without going through Justify.
        if (const int j = core::text::justify_of_keyword(u); j >= 0 && !t.empty()) {
            justify_ = static_cast<std::uint8_t>(j);
            prompt_point(ctx);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            pos_ = *p;
            align_ = *p;
            ctx.set_last_point(*p);
            if (core::text::two_point(justify_)) {
                state_ = State::Second;
                ctx.set_preview({PreviewKind::Segment, {pos_}});
                ctx.set_prompt("Specify second endpoint of text baseline: ");
                return;
            }
            after_point(ctx);
        }
        return;
    }
    case State::Justify: {
        if (t.empty()) {
            prompt_point(ctx);
            return;
        }
        const int j = core::text::justify_of_keyword(u);
        if (j < 0) {
            ctx.echo("Invalid option keyword.");
            return;
        }
        justify_ = static_cast<std::uint8_t>(j);
        prompt_point(ctx);
        return;
    }
    case State::Style: {
        const std::vector<core::TextStyle> styles = ctx.text_styles();
        if (u == "?") {
            ctx.echo("Text styles:");
            ctx.echo("  Standard");
            for (std::size_t i = 1; i < styles.size(); ++i) {
                ctx.echo("  " + styles[i].name);
            }
            return;
        }
        if (!t.empty()) {
            bool found = u == "STANDARD";
            std::string name;
            double factor = 1.0;
            double fixed = 0.0;
            for (std::size_t i = 1; i < styles.size() && !found; ++i) {
                if (upper(styles[i].name) == u) {
                    found = true;
                    name = styles[i].name;
                    factor = styles[i].width_factor;
                    fixed = styles[i].height;
                }
            }
            if (!found) {
                ctx.echo("Cannot find text style \"" + t + "\".");
                return;
            }
            style_ = name;
            style_factor_ = factor;
            fixed_height_ = fixed > 0.0;
            if (fixed_height_) {
                height_ = fixed;
            }
            ctx.submit(core::SetCurrentTextStyleCommand{name.empty() ? std::string("Standard") : name});
        }
        prompt_point(ctx);
        return;
    }
    case State::Second:
        if (const auto p = read_point(ctx, text)) {
            if (core::distance(pos_, *p) < 1e-12) {
                ctx.echo("The two endpoints must differ.");
                return;
            }
            align_ = *p;
            rotation_ = std::atan2(p->y - pos_.y, p->x - pos_.x);
            after_point(ctx);
        }
        return;
    case State::Height: {
        if (!t.empty()) {
            double h = 0.0;
            if (parse_number(t, h)) {
                // typed
            } else if (const auto p = read_point(ctx, text)) {
                h = core::distance(pos_, *p);
            } else {
                return;
            }
            if (!(h > 0.0)) {
                ctx.echo("Value must be positive and nonzero.");
                return;
            }
            height_ = h;
        }
        if (justify_ == 5) {
            begin_content(ctx); // Fit's direction is its two points'
        } else {
            prompt_rotation(ctx);
        }
        return;
    }
    case State::Rotation: {
        if (!t.empty()) {
            double deg = 0.0;
            if (parse_number(t, deg)) {
                rotation_ = core::to_radians(deg);
            } else if (const auto p = read_point(ctx, text)) {
                if (core::distance(pos_, *p) > 1e-12) {
                    rotation_ = std::atan2(p->y - pos_.y, p->x - pos_.x);
                }
            } else {
                return;
            }
        }
        begin_content(ctx);
        return;
    }
    case State::Content: {
        if (t.empty()) {
            // An empty line ends the command.
            ctx.clear_preview();
            if (lines_ > 0) {
                ctx.echo(std::to_string(lines_) + (lines_ == 1 ? " line of text placed." : " lines of text placed."));
            }
            done_ = true;
            return;
        }
        core::AddTextCommand cmd{pos_, height_, rotation_, justify_, text, ctx.group_id()};
        cmd.style = style_; // the text style (its font applies)
        cmd.align = align_;
        ctx.submit(std::move(cmd));
        ++lines_;
        // The next line stands under this one: down the page in the text's own frame.
        const double drop = height_ * kTextLineSpacing;
        const core::Vec2 down{std::sin(rotation_) * drop, -std::cos(rotation_) * drop};
        pos_ = pos_ + down;
        align_ = align_ + down;
        (void)ctx.new_group(); // every line its own undo step
        show_line(ctx);
        ctx.set_prompt("Enter text: ");
        return;
    }
    }
}

// ---------------------------------------------------------------------------
// JUSTIFYTEXT / SCALETEXT / TXT2MTXT
// ---------------------------------------------------------------------------
void TextToolCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (ctx.has_selection()) {
        selected(ctx);
        return;
    }
    select_.begin(ctx);
}

void TextToolCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void TextToolCommand::selection_gesture(CommandContext& ctx) {
    if (state_ == State::Select && select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        selected(ctx);
    }
}

void TextToolCommand::selected(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        done_ = true;
        return;
    }
    switch (tool_) {
    case Tool::Justify:
        state_ = State::Justify;
        ctx.set_prompt(std::string("Enter a justification option "
                                   "[Left/Align/Fit/Center/Middle/Right/TL/TC/TR/ML/MC/MR/BL/BC/BR] <") +
                       core::text::kJustifyInfo[s_justify_].name + ">: ");
        return;
    case Tool::Scale:
        state_ = State::Base;
        ctx.set_prompt("Enter a base point option for scaling "
                       "[Existing/Left/Center/Middle/Right/TL/TC/TR/ML/MC/MR/BL/BC/BR] <Existing>: ");
        return;
    case Tool::ToMText:
        ctx.submit(core::TextToMTextCommand{ctx.group_id()});
        done_ = true;
        return;
    }
}

void TextToolCommand::prompt_height(CommandContext& ctx) {
    state_ = State::Height;
    ctx.set_prompt("Specify new model height or [Paper height/Match object/Scale factor] <" +
                   core::units::format_length(s_height_, ctx.units()) + ">: ");
}

void TextToolCommand::scale(CommandContext& ctx, std::uint8_t mode, double value, core::Vec2 pick) {
    core::ScaleTextCommand c;
    c.base = base_;
    c.mode = mode;
    c.value = value;
    c.match_pick = pick;
    c.pick_radius = ctx.pick_radius();
    c.group = ctx.group_id();
    ctx.submit(c);
    done_ = true;
}

void TextToolCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    double v = 0.0;
    switch (state_) {
    case State::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            selected(ctx);
        }
        return;
    case State::Justify: {
        int j = s_justify_;
        if (!t.empty()) {
            j = core::text::justify_of_keyword(u);
            if (j < 0) {
                ctx.echo("Invalid option keyword.");
                return;
            }
        }
        s_justify_ = static_cast<std::uint8_t>(j);
        ctx.submit(core::JustifyTextCommand{s_justify_, ctx.group_id()});
        done_ = true;
        return;
    }
    case State::Base: {
        base_ = 0;
        if (!t.empty() && u != "E" && u != "EXISTING") {
            const int j = core::text::justify_of_keyword(u);
            if (j < 0 || core::text::two_point(static_cast<std::uint8_t>(j))) {
                ctx.echo("Invalid option keyword.");
                return;
            }
            base_ = static_cast<std::uint8_t>(j + 1);
        }
        prompt_height(ctx);
        return;
    }
    case State::Height:
        if (t.empty()) {
            scale(ctx, 0, s_height_);
            return;
        }
        if (u == "P" || u == "PAPER" || u == "PAPER HEIGHT") {
            ctx.echo("Paper height applies to annotative objects; none are selected.");
            return;
        }
        if (u == "M" || u == "MATCH" || u == "MATCH OBJECT") {
            state_ = State::Match;
            ctx.set_prompt("Select a text object with the desired height: ");
            return;
        }
        if (u == "S" || u == "SCALE" || u == "SCALE FACTOR") {
            state_ = State::Factor;
            ctx.set_prompt("Specify scale factor or [Reference] <" + fmt4(s_factor_) + ">: ");
            return;
        }
        if (!parse_number(t, v) || !(v > 0.0)) {
            ctx.echo("Value must be positive and nonzero.");
            return;
        }
        s_height_ = v;
        scale(ctx, 0, v);
        return;
    case State::Match:
        if (const auto p = read_point(ctx, text)) {
            scale(ctx, 2, 0.0, *p);
        }
        return;
    case State::Factor:
        if (t.empty()) {
            scale(ctx, 1, s_factor_);
            return;
        }
        if (u == "R" || u == "REFERENCE") {
            state_ = State::RefLength;
            ctx.set_prompt("Specify reference length <1>: ");
            return;
        }
        if (!parse_number(t, v) || !(v > 0.0)) {
            ctx.echo("Value must be positive and nonzero.");
            return;
        }
        s_factor_ = v;
        scale(ctx, 1, v);
        return;
    case State::RefLength:
        ref_ = 1.0;
        if (!t.empty() && (!parse_number(t, ref_) || !(ref_ > 0.0))) {
            ctx.echo("Value must be positive and nonzero.");
            ref_ = 1.0;
            return;
        }
        state_ = State::RefNew;
        ctx.set_prompt("Specify new length: ");
        return;
    case State::RefNew:
        if (!parse_number(t, v) || !(v > 0.0)) {
            ctx.echo("Value must be positive and nonzero.");
            return;
        }
        s_factor_ = v / ref_;
        scale(ctx, 1, s_factor_);
        return;
    }
}

void TextCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

namespace {
/// A plain number ("30", "-12.5"), or nothing: what an angle prompt takes as a value
/// before it tries the text as a point.
std::optional<double> parse_number(const std::string& t) {
    // A decimal, a fraction, or feet and inches (units::parse_length_text).
    return core::units::parse_length_text(t);
}

/// Mtext / Text / Angle at a dimension's placement prompt: true when `u` asked for one
/// (the next input is then the text, or the text's angle -- see read_dim_text_angle).
bool dim_text_option(CommandContext& ctx, const std::string& u, bool& typing, bool& angling) {
    if (u == "M" || u == "MTEXT" || u == "T" || u == "TEXT") {
        typing = true;
        ctx.set_prompt("Enter dimension text (<> for the measurement): ");
        return true;
    }
    if (u == "A" || u == "ANGLE") {
        angling = true;
        ctx.set_prompt("Specify angle of dimension text: ");
        return true;
    }
    return false;
}

/// The answer to `Specify angle of dimension text:` -- degrees, 0 (or Enter) for the
/// dimension's own lay-out. Nothing when it is not a number (the prompt stays).
std::optional<double> read_dim_text_angle(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (t.empty()) {
        return 0.0;
    }
    if (const std::optional<double> deg = parse_number(t)) {
        return core::to_radians(*deg);
    }
    ctx.echo("Requires an angle in degrees.");
    return std::nullopt;
}

/// The current dimension style, as the command layer last heard of it (Standard's
/// defaults when it has not yet).
core::DimStyle current_dim_style_of(const CommandContext& ctx) {
    const std::vector<core::DimStyle> styles = ctx.dim_styles();
    const std::uint16_t i = ctx.current_dim_style();
    return i < styles.size() ? styles[i] : core::DimStyle{"Standard"};
}
/// A leader's text stands at its dimension style's text height.
double current_dim_text_height(const CommandContext& ctx) { return current_dim_style_of(ctx).text_height; }

bool is_object_keyword(const std::string& text) {
    std::string u;
    for (const char c : text) {
        u += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return u == "O" || u == "OBJECT";
}
} // namespace

namespace {
// Rubber-band the full dimension at the cursor (Phase 16 Part C). Object dims pass no
// points: the UI uses the snapshot's resolved pending_dim_* (set by
// ResolveDimObjectCommand at the object pick). DIMLINEAR / DIMALIGNED / DIMANGULAR
// build their own (with the angle or the three points).
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
    ctx.set_prompt("Specify first extension line origin or <select object>: ");
}

void LinearDimensionCommand::show_preview(CommandContext& ctx) const {
    PreviewSpec s;
    s.kind = PreviewKind::Dimension;
    s.dim_type = static_cast<int>(type_);
    if (!object_) {
        s.points = {a_, b_}; // an object's def points come from the snapshot's pending_dim
    }
    s.dim_angle_fixed = angle_fixed_;
    s.dim_angle = angle_;
    ctx.set_preview(std::move(s));
}

void LinearDimensionCommand::place_prompt(CommandContext& ctx) {
    state_ = State::Place;
    ctx.set_prompt(type_ == core::DimType::Linear && !angle_fixed_
                       ? "Specify dimension line location or [Mtext/Text/Angle/Horizontal/Vertical/Rotated]: "
                       : "Specify dimension line location or [Mtext/Text/Angle]: ");
}

void LinearDimensionCommand::enter_place(CommandContext& ctx) {
    show_preview(ctx); // rubber-band to the cursor
    place_prompt(ctx);
}

void LinearDimensionCommand::place(CommandContext& ctx, core::Vec2 at) {
    const std::optional<double> fixed = angle_fixed_ ? std::optional<double>(angle_) : std::nullopt;
    if (object_) {
        core::AddObjectDimensionCommand c;
        c.type = static_cast<std::uint8_t>(type_);
        c.pick1 = obj_pick_;
        c.pick2 = at;
        c.pick_radius = ctx.pick_radius();
        c.style = ctx.current_dim_style();
        c.group = ctx.group_id();
        if (type_ == core::DimType::Linear) {
            c.line_angle = fixed;
        }
        c.text_override = text_;
        c.text_angle = text_angle_;
        ctx.submit(std::move(c));
        ctx.echo("Dimension placed from object.");
    } else {
        core::DimData d;
        d.type = type_;
        d.a = a_;
        d.b = b_;
        d.line_pt = at;
        if (type_ == core::DimType::Linear) {
            core::orient_linear_dim(d, fixed);
        }
        ctx.submit(core::AddDimensionCommand{.type = static_cast<std::uint8_t>(type_),
                                             .a = a_,
                                             .b = b_,
                                             .line_pt = at,
                                             .style = ctx.current_dim_style(),
                                             .group = ctx.group_id(),
                                             .text_override = text_,
                                             .aux = d.aux,
                                             .text_angle = text_angle_});
        ctx.echo("Dimension placed.");
    }
    ctx.set_preview({});
    done_ = true;
}

void LinearDimensionCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::First:
        // Enter selects the object to dimension (AutoCAD's <select object>); [Object] is
        // kept for the scripts that type it.
        if (t.empty() || is_object_keyword(t)) {
            state_ = State::SelectObj;
            ctx.set_prompt("Select object to dimension: ");
            return;
        }
        break;
    case State::Text:
        // The text as typed; `<>` stands for the measurement, and an empty answer keeps
        // the measurement alone.
        text_ = t;
        show_preview(ctx);
        place_prompt(ctx);
        return;
    case State::TextAngle:
        if (const std::optional<double> a = read_dim_text_angle(ctx, text)) {
            text_angle_ = *a;
            show_preview(ctx);
            place_prompt(ctx);
        }
        return;
    case State::Rotation:
        if (t.empty()) {
            angle_ = 0.0;
        } else if (const std::optional<double> deg = parse_number(t)) {
            angle_ = core::to_radians(*deg);
        } else if (const auto p = read_point(ctx, text)) {
            angle_from_ = *p;
            state_ = State::Rotation2;
            ctx.set_prompt("Specify second point: ");
            return;
        } else {
            return;
        }
        angle_fixed_ = true;
        show_preview(ctx);
        place_prompt(ctx);
        return;
    case State::Rotation2:
        if (const auto p = read_point(ctx, text)) {
            const core::Vec2 d = *p - angle_from_;
            if (core::length_squared(d) < 1e-18) {
                ctx.echo("The two points are the same; specify a second point.");
                return;
            }
            angle_ = std::atan2(d.y, d.x);
            angle_fixed_ = true;
            show_preview(ctx);
            place_prompt(ctx);
        }
        return;
    case State::Place:
        if (u == "M" || u == "MTEXT" || u == "T" || u == "TEXT") {
            state_ = State::Text;
            // What Enter keeps: the text typed before, else the measurement -- shown when
            // the points are known (an object's are on the geometry side).
            std::string keep = text_;
            if (keep.empty() && !object_) {
                core::DimData d;
                d.type = type_;
                d.a = a_;
                d.b = b_;
                d.line_pt = ctx.cursor_world().value_or((a_ + b_) * 0.5);
                if (type_ == core::DimType::Linear) {
                    core::orient_linear_dim(d, angle_fixed_ ? std::optional<double>(angle_) : std::nullopt);
                }
                keep = core::units::format_length(core::dim_measure(d), ctx.units());
            }
            ctx.set_prompt(keep.empty() ? "Enter dimension text (<> for the measurement): "
                                        : "Enter dimension text <" + keep + ">: ");
            return;
        }
        if (u == "A" || u == "ANGLE") {
            state_ = State::TextAngle;
            ctx.set_prompt("Specify angle of dimension text: ");
            return;
        }
        if (type_ == core::DimType::Linear && !angle_fixed_) {
            if (u == "H" || u == "HORIZONTAL" || u == "V" || u == "VERTICAL") {
                angle_fixed_ = true;
                angle_ = u[0] == 'H' ? 0.0 : core::kHalfPi;
                show_preview(ctx);
                place_prompt(ctx);
                return;
            }
            if (u == "R" || u == "ROTATED") {
                state_ = State::Rotation;
                ctx.set_prompt("Specify angle of dimension line <0>: ");
                return;
            }
        }
        break;
    case State::Second:
    case State::SelectObj:
        break;
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
        enter_place(ctx);
        return;
    case State::SelectObj:
        obj_pick_ = *p;
        ctx.set_last_point(*p);
        object_ = true;
        // Resolve the object's def points once for the placement preview.
        ctx.submit(core::ResolveDimObjectCommand{static_cast<std::uint8_t>(type_), obj_pick_,
                                                 obj_pick_, ctx.pick_radius()});
        enter_place(ctx);
        return;
    case State::Place:
        place(ctx, *p);
        return;
    case State::Text:
    case State::TextAngle:
    case State::Rotation:
    case State::Rotation2:
        return;
    }
}

void LinearDimensionCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.set_preview({});
    done_ = true;
}

// ---------------------------------------------------------------------------
// DIMRADIUS / DIMDIAMETER: select the circle/arc -> place. The value comes from
// the entity's own geometry (resolved on the geometry thread).
// ---------------------------------------------------------------------------
void RadialDimensionCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select arc or circle: ");
}

void RadialDimensionCommand::input(CommandContext& ctx, const std::string& text) {
    if (state_ == State::Place) {
        if (typing_) {
            typing_ = false;
            text_ = trimmed(text);
            ctx.set_prompt("Specify dimension line location or [Mtext/Text/Angle]: ");
            return;
        }
        if (angling_) {
            if (const std::optional<double> a = read_dim_text_angle(ctx, text)) {
                angling_ = false;
                text_angle_ = *a;
                ctx.set_prompt("Specify dimension line location or [Mtext/Text/Angle]: ");
            }
            return;
        }
        if (dim_text_option(ctx, upper(trimmed(text)), typing_, angling_)) {
            return;
        }
    }
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
        ctx.set_prompt("Specify dimension line location or [Mtext/Text/Angle]: ");
        return;
    }
    core::AddObjectDimensionCommand c{static_cast<std::uint8_t>(type_), obj_pick_, *p,
                                      ctx.pick_radius(), ctx.current_dim_style(), ctx.group_id()};
    c.text_override = text_;
    c.text_angle = text_angle_;
    ctx.submit(std::move(c));
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
    if (typing_) {
        typing_ = false;
        text_ = trimmed(text);
        ctx.set_prompt("Specify leader endpoint or [Xdatum/Ydatum/Mtext/Text/Angle]: ");
        return;
    }
    if (angling_) {
        if (const std::optional<double> a = read_dim_text_angle(ctx, text)) {
            angling_ = false;
            text_angle_ = *a;
            ctx.set_prompt("Specify leader endpoint or [Xdatum/Ydatum/Mtext/Text/Angle]: ");
        }
        return;
    }
    if (dim_text_option(ctx, u, typing_, angling_)) {
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
        dim.style = ctx.current_dim_style();
        dim.text_override = text_;
        dim.aux = aux;
        dim.text_angle = text_angle_;
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
    if (state_ == State::Place) {
        if (typing_) {
            typing_ = false;
            text_ = trimmed(text);
            ctx.set_prompt("Specify dimension line location or [Mtext/Text/Angle]: ");
            return;
        }
        if (angling_) {
            if (const std::optional<double> a = read_dim_text_angle(ctx, text)) {
                angling_ = false;
                text_angle_ = *a;
                ctx.set_prompt("Specify dimension line location or [Mtext/Text/Angle]: ");
            }
            return;
        }
        if (dim_text_option(ctx, u, typing_, angling_)) {
            return;
        }
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
        core::AddObjectDimensionCommand c{static_cast<std::uint8_t>(core::DimType::Jogged),
                                          obj_pick_, place_, ctx.pick_radius(), ctx.current_dim_style(),
                                          ctx.group_id(), override_, *p};
        c.text_override = text_;
        c.text_angle = text_angle_;
        ctx.submit(std::move(c));
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

void ArcLengthDimensionCommand::place_prompt(CommandContext& ctx) {
    state_ = State::Place;
    ctx.set_prompt(std::string("Specify arc length dimension location, or [Mtext/Text/Angle/Partial/") +
                   (leader_ ? "No leader" : "Leader") + "]: ");
}

void ArcLengthDimensionCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    if (state_ == State::Place) {
        if (typing_) {
            typing_ = false;
            text_ = trimmed(text);
            place_prompt(ctx);
            return;
        }
        if (angling_) {
            if (const std::optional<double> a = read_dim_text_angle(ctx, text)) {
                angling_ = false;
                text_angle_ = *a;
                place_prompt(ctx);
            }
            return;
        }
        if (dim_text_option(ctx, u, typing_, angling_)) {
            return;
        }
        if (u == "P" || u == "PARTIAL") {
            state_ = State::Partial1;
            ctx.set_prompt("Specify first point for arc length dimension: ");
            return;
        }
        if (u == "L" || u == "LEADER" || u == "N" || u == "NO LEADER" || u == "NOLEADER") {
            leader_ = u[0] == 'L';
            place_prompt(ctx);
            return;
        }
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    if (state_ == State::Select) {
        obj_pick_ = *p;
        ctx.set_last_point(*p);
        ctx.submit(core::ResolveDimObjectCommand{static_cast<std::uint8_t>(core::DimType::ArcLength),
                                                 obj_pick_, obj_pick_, ctx.pick_radius()});
        preview_object_dim(ctx, core::DimType::ArcLength);
        place_prompt(ctx);
        return;
    }
    if (state_ == State::Partial1) {
        partial_from_ = *p;
        state_ = State::Partial2;
        ctx.set_prompt("Specify second point for arc length dimension: ");
        return;
    }
    if (state_ == State::Partial2) {
        partial_to_ = *p;
        place_prompt(ctx);
        return;
    }
    core::AddObjectDimensionCommand c{static_cast<std::uint8_t>(core::DimType::ArcLength),
                                      obj_pick_, *p, ctx.pick_radius(), ctx.current_dim_style(), ctx.group_id()};
    c.text_override = text_;
    c.text_angle = text_angle_;
    c.arc_leader = leader_;
    c.partial_from = partial_from_;
    c.partial_to = partial_to_;
    ctx.submit(std::move(c));
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
    state_ = State::Select;
    ctx.set_prompt("Select arc, circle, line, or <specify vertex>: ");
}

void AngularDimensionCommand::show_preview(CommandContext& ctx) const {
    PreviewSpec s;
    s.kind = PreviewKind::Dimension;
    s.dim_type = static_cast<int>(core::DimType::Angular);
    if (points_) {
        s.points = {vertex_, end1_, end2_};
    }
    s.dim_has_quadrant = quadrant_.has_value();
    s.dim_quadrant = quadrant_.value_or(core::Vec2{});
    ctx.set_preview(std::move(s));
}

void AngularDimensionCommand::place_prompt(CommandContext& ctx) {
    state_ = State::Place;
    show_preview(ctx);
    ctx.set_prompt("Specify dimension arc line location or [Mtext/Text/Angle/Quadrant]: ");
}

void AngularDimensionCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    if (state_ == State::Select && t.empty()) {
        state_ = State::Vertex;
        ctx.set_prompt("Specify angle vertex: ");
        return;
    }
    if (state_ == State::Text) {
        text_ = t; // `<>` stands for the measurement; empty keeps it alone
        place_prompt(ctx);
        return;
    }
    if (state_ == State::TextAngle) {
        if (const std::optional<double> a = read_dim_text_angle(ctx, text)) {
            text_angle_ = *a;
            place_prompt(ctx);
        }
        return;
    }
    if (state_ == State::Place) {
        if (u == "Q" || u == "QUADRANT") {
            state_ = State::Quadrant;
            ctx.set_prompt("Specify quadrant: ");
            return;
        }
        bool typing = false;
        bool angling = false;
        if (dim_text_option(ctx, u, typing, angling)) {
            if (typing) {
                state_ = State::Text;
            } else if (angling) {
                state_ = State::TextAngle;
            }
            return;
        }
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    switch (state_) {
    case State::Select: {
        pick1_ = *p;
        ctx.set_last_point(*p);
        // What is under the pick decides the next prompt (as the smart DIM reads it);
        // typed coordinates with nothing hovered are taken as a line.
        const std::optional<core::EntityKind> k = ctx.hovered_kind();
        if (k == core::EntityKind::Arc) {
            ctx.submit(core::ResolveDimObjectCommand{static_cast<std::uint8_t>(core::DimType::Angular), pick1_,
                                                     pick1_, ctx.pick_radius()});
            place_prompt(ctx);
        } else if (k == core::EntityKind::Circle) {
            state_ = State::CircleEnd;
            ctx.set_prompt("Specify second angle endpoint: ");
        } else {
            state_ = State::Line2;
            ctx.set_prompt("Select second line: ");
        }
        return;
    }
    case State::Line2:
    case State::CircleEnd:
        pick2_ = *p;
        ctx.submit(core::ResolveDimObjectCommand{static_cast<std::uint8_t>(core::DimType::Angular), pick1_, pick2_,
                                                 ctx.pick_radius()});
        place_prompt(ctx);
        return;
    case State::Vertex:
        vertex_ = *p;
        ctx.set_last_point(*p);
        state_ = State::First;
        ctx.set_prompt("Specify first angle endpoint: ");
        return;
    case State::First:
        end1_ = *p;
        ctx.set_last_point(*p);
        state_ = State::Second;
        ctx.set_prompt("Specify second angle endpoint: ");
        return;
    case State::Second:
        end2_ = *p;
        points_ = true;
        place_prompt(ctx);
        return;
    case State::Quadrant:
        quadrant_ = *p;
        place_prompt(ctx);
        return;
    case State::Place:
        if (points_) {
            core::DimData d;
            d.type = core::DimType::Angular;
            d.a = vertex_;
            d.b = end1_;
            d.line_pt = end2_;
            core::place_angular_dim(d, *p, core::AngularFrom::Points, quadrant_);
            core::AddDimensionCommand c;
            c.type = static_cast<std::uint8_t>(core::DimType::Angular);
            c.a = d.a;
            c.b = d.b;
            c.line_pt = d.line_pt;
            c.aux = d.aux;
            c.style = ctx.current_dim_style();
            c.group = ctx.group_id();
            c.text_override = text_;
            c.text_angle = text_angle_;
            ctx.submit(std::move(c));
        } else {
            core::AddObjectDimensionCommand c{static_cast<std::uint8_t>(core::DimType::Angular), pick1_, pick2_,
                                              ctx.pick_radius(), ctx.current_dim_style(), ctx.group_id()};
            c.arc_at = *p;
            c.quadrant = quadrant_;
            c.text_override = text_;
            c.text_angle = text_angle_;
            ctx.submit(std::move(c));
        }
        ctx.set_preview({});
        ctx.echo("Angular dimension placed.");
        done_ = true;
        return;
    case State::Text:
    case State::TextAngle:
        return;
    }
}

void AngularDimensionCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.set_preview({});
    done_ = true;
}

// ---------------------------------------------------------------------------
// DIM: smart all-in-one. Hover previews the type; on pick it reads the hovered
// entity kind and dispatches to the shared object-aware machinery.
// ---------------------------------------------------------------------------
namespace {
bool dimensionable(core::EntityKind k) {
    return k == core::EntityKind::Line || k == core::EntityKind::Polyline ||
           k == core::EntityKind::Circle || k == core::EntityKind::Arc;
}
} // namespace

void DimCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    made_ = 0;
    state_ = State::Main;
    prompt(ctx);
}

void DimCommand::prompt(CommandContext& ctx) {
    switch (state_) {
    case State::Main:
        ctx.set_prompt("Select objects or specify first extension line origin or "
                       "[Angular/Baseline/Continue/Ordinate/aliGn/Distribute/Layer/Undo]: ");
        return;
    case State::AlignBase:
    case State::DistBase:
        ctx.set_prompt("Select base dimension: ");
        return;
    case State::DistMethod:
        ctx.set_prompt(std::string("Specify method of distribution [Equal/Offset] <") + (equal_ ? "Equal" : "Offset") +
                       ">: ");
        return;
    case State::DistOffset:
        ctx.set_prompt("Specify offset distance <" + core::units::format_length(offset_, ctx.units()) + ">: ");
        return;
    case State::Layer:
        ctx.set_prompt("Enter layer name or select object <use current>: ");
        return;
    case State::Select:
        return; // the selection step prompts
    }
}

void DimCommand::begin(CommandContext& ctx, std::unique_ptr<ICommand> sub) {
    if (made_ > 0) {
        (void)ctx.new_group(); // every dimension its own undo step
    }
    ctx.set_preview({});
    sub_ = std::move(sub);
    sub_from_line_ = false;
    sub_->start(ctx);
}

void DimCommand::after_sub(CommandContext& ctx) {
    if (sub_ && sub_->done()) {
        sub_.reset();
        sub_from_line_ = false;
        ++made_;
        ctx.set_preview({});
        state_ = State::Main;
        prompt(ctx);
    }
}

void DimCommand::hover(CommandContext& ctx, std::optional<core::EntityKind> kind) {
    if (sub_) {
        sub_->hover(ctx, kind);
        return;
    }
    if (state_ != State::Main) {
        return;
    }
    // What a pick here would make, previewed at the cursor.
    const std::optional<core::Vec2> at = ctx.cursor_world();
    if (kind && dimensionable(*kind) && at) {
        const core::DimType t = *kind == core::EntityKind::Circle ? core::DimType::Diameter
                                : *kind == core::EntityKind::Arc  ? core::DimType::Radius
                                                                  : core::DimType::Aligned;
        ctx.submit(core::ResolveDimObjectCommand{static_cast<std::uint8_t>(t), *at, *at, ctx.pick_radius()});
        preview_object_dim(ctx, t);
    } else {
        ctx.set_preview({});
    }
}

void DimCommand::arrange(CommandContext& ctx) {
    if (ctx.has_selection()) {
        if (made_ > 0) {
            (void)ctx.new_group();
        }
        core::DimArrangeCommand c;
        c.op = align_ ? core::DimArrangeCommand::Op::Align
                      : (equal_ ? core::DimArrangeCommand::Op::DistributeEqual : core::DimArrangeCommand::Op::DistributeOffset);
        c.base_pick = base_;
        c.radius = ctx.pick_radius();
        c.offset = offset_;
        c.group = ctx.group_id();
        ctx.submit(c);
        ++made_;
    }
    state_ = State::Main;
    prompt(ctx);
}

void DimCommand::selection_gesture(CommandContext& ctx) {
    if (sub_) {
        sub_->selection_gesture(ctx);
        after_sub(ctx);
        return;
    }
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        arrange(ctx);
    }
}

void DimCommand::input(CommandContext& ctx, const std::string& text) {
    if (sub_) {
        // A line's dimension being placed: a second line picked makes the angle between them.
        if (sub_from_line_ && ctx.input_is_pick() && ctx.hovered_kind() == core::EntityKind::Line) {
            if (const auto p = read_point(ctx, text)) {
                sub_->cancel(ctx);
                ctx.set_preview({});
                sub_ = std::make_unique<AngularDimensionCommand>();
                sub_from_line_ = false;
                sub_->start(ctx);
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.10g,%.10g", line_pick_.x, line_pick_.y);
                sub_->input(ctx, buf); // the first line (its kind is a line, as hovered now)
                sub_->input(ctx, text); // the second
                after_sub(ctx);
                return;
            }
        }
        sub_->input(ctx, text);
        after_sub(ctx);
        return;
    }
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            arrange(ctx);
        }
        return;
    case State::AlignBase:
    case State::DistBase: {
        if (t.empty()) {
            state_ = State::Main;
            prompt(ctx);
            return;
        }
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        base_ = *p;
        state_ = State::Select;
        ctx.submit(core::ClearSelectionCommand{});
        select_.begin(ctx, align_ ? "Select dimensions to align: " : "Select dimensions to distribute: ");
        return;
    }
    case State::DistMethod:
        if (t.empty() || u == "E" || u == "EQUAL") {
            equal_ = true;
            state_ = State::Select;
            ctx.submit(core::ClearSelectionCommand{});
            select_.begin(ctx, "Select dimensions to distribute: ");
        } else if (u == "O" || u == "OFFSET") {
            equal_ = false;
            state_ = State::DistOffset;
            prompt(ctx);
        } else {
            ctx.echo("Invalid option keyword.");
            prompt(ctx);
        }
        return;
    case State::DistOffset:
        if (!t.empty()) {
            const std::optional<double> v = parse_number(t);
            if (!v || *v <= 0.0) {
                ctx.echo("Requires a positive distance.");
                return;
            }
            offset_ = *v;
        }
        state_ = State::DistBase;
        prompt(ctx);
        return;
    case State::Layer: {
        core::SetDimLayerCommand c;
        if (ctx.input_is_pick()) {
            if (const auto p = read_point(ctx, text)) {
                c.pick = *p;
                c.radius = ctx.pick_radius();
            }
        } else {
            c.name = t;
        }
        ctx.submit(std::move(c));
        state_ = State::Main;
        prompt(ctx);
        return;
    }
    case State::Main:
        break;
    }

    if (t.empty()) {
        ctx.set_preview({});
        done_ = true;
        return;
    }
    if (u == "A" || u == "ANGULAR") {
        begin(ctx, std::make_unique<AngularDimensionCommand>());
        return;
    }
    if (u == "B" || u == "BASELINE") {
        begin(ctx, std::make_unique<ChainDimCommand>(true));
        return;
    }
    if (u == "C" || u == "CONTINUE") {
        begin(ctx, std::make_unique<ChainDimCommand>(false));
        return;
    }
    if (u == "O" || u == "ORDINATE") {
        begin(ctx, std::make_unique<OrdinateDimensionCommand>());
        return;
    }
    if (u == "G" || u == "ALIGN") {
        align_ = true;
        state_ = State::AlignBase;
        prompt(ctx);
        return;
    }
    if (u == "D" || u == "DISTRIBUTE") {
        align_ = false;
        state_ = State::DistMethod;
        prompt(ctx);
        return;
    }
    if (u == "L" || u == "LAYER") {
        state_ = State::Layer;
        prompt(ctx);
        return;
    }
    if (u == "U" || u == "UNDO") {
        if (made_ == 0) {
            ctx.echo("Nothing to undo.");
        } else {
            ctx.submit(core::UndoLastGroupCommand{ctx.command_group()});
            --made_;
        }
        prompt(ctx);
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    // What is under the pick decides the kind.
    const std::optional<core::EntityKind> kind = ctx.input_is_pick() ? ctx.hovered_kind() : std::nullopt;
    if (kind == core::EntityKind::Circle || kind == core::EntityKind::Arc) {
        const bool circle = kind == core::EntityKind::Circle;
        begin(ctx, std::make_unique<RadialDimensionCommand>(circle ? core::DimType::Diameter : core::DimType::Radius,
                                                            circle ? "DIMDIAMETER" : "DIMRADIUS"));
        sub_->input(ctx, text); // the circle or arc
    } else if (kind == core::EntityKind::Line || kind == core::EntityKind::Polyline) {
        begin(ctx, std::make_unique<LinearDimensionCommand>(core::DimType::Aligned, "DIMALIGNED"));
        sub_->input(ctx, "");   // <select object>
        sub_->input(ctx, text); // the line
        sub_from_line_ = kind == core::EntityKind::Line;
        line_pick_ = *p;
        if (sub_from_line_) {
            ctx.set_prompt("Specify dimension line location or second line for angle [Mtext/Text/Angle]: ");
        }
    } else {
        begin(ctx, std::make_unique<LinearDimensionCommand>(core::DimType::Linear, "DIMLINEAR"));
        sub_->input(ctx, text); // the first extension line origin
    }
    after_sub(ctx);
}

void DimCommand::cancel(CommandContext& ctx) {
    if (sub_) {
        sub_->cancel(ctx);
        sub_.reset();
    } else {
        ctx.echo("*Cancel*");
    }
    ctx.set_preview({});
    done_ = true;
}

// ---------------------------------------------------------------------------
// DIMCONTINUE / DIMBASELINE (issues #28, #57), DIMLAYER
// ---------------------------------------------------------------------------
void ChainDimCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    core::ChainDimensionCommand c;
    c.op = core::ChainDimensionCommand::Op::Start;
    c.baseline = baseline_;
    ctx.submit(c);
    state_ = State::Origin;
    prompt(ctx);
}

void ChainDimCommand::prompt(CommandContext& ctx) {
    switch (state_) {
    case State::Origin:
        ctx.set_prompt(baseline_ ? "Specify a second extension line origin or [Select/Offset/Undo] <Select>: "
                                 : "Specify a second extension line origin or [Select/Undo] <Select>: ");
        return;
    case State::Select:
        ctx.set_prompt(baseline_ ? "Select base dimension: " : "Select continued dimension: ");
        return;
    case State::Offset: {
        const double show = spacing_ > 0.0 ? spacing_ : current_dim_text_height(ctx) * 1.5;
        ctx.set_prompt("Specify the baseline offset distance <" + core::units::format_length(show, ctx.units()) + ">: ");
        return;
    }
    }
}

void ChainDimCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Select: {
        if (t.empty()) {
            done_ = true; // Enter at the Select prompt ends, as in AutoCAD
            return;
        }
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        core::ChainDimensionCommand c;
        c.op = core::ChainDimensionCommand::Op::Select;
        c.at = *p;
        c.baseline = baseline_;
        c.pick_radius = ctx.pick_radius();
        ctx.submit(c);
        state_ = State::Origin;
        prompt(ctx);
        return;
    }
    case State::Offset:
        if (!t.empty()) {
            const std::optional<double> v = parse_number(t);
            if (!v || *v < 0.0) {
                ctx.echo("Requires a distance of zero or more.");
                return;
            }
            spacing_ = *v;
        }
        state_ = State::Origin;
        prompt(ctx);
        return;
    case State::Origin:
        break;
    }
    if (t.empty() || u == "S" || u == "SELECT") {
        state_ = State::Select;
        prompt(ctx);
        return;
    }
    if (u == "U" || u == "UNDO") {
        if (added_ == 0) {
            ctx.echo("Nothing to undo.");
        } else {
            ctx.submit(core::UndoLastGroupCommand{ctx.command_group()});
            core::ChainDimensionCommand back;
            back.op = core::ChainDimensionCommand::Op::Back;
            back.baseline = baseline_;
            ctx.submit(back);
            --added_;
        }
        prompt(ctx);
        return;
    }
    if (baseline_ && (u == "O" || u == "OFFSET")) {
        state_ = State::Offset;
        prompt(ctx);
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        // A FRESH undo group per pick, so each chained dimension undoes on its own.
        core::ChainDimensionCommand c{*p, baseline_, ctx.new_group()};
        c.spacing = spacing_;
        ctx.submit(c);
        ctx.set_last_point(*p);
        ++added_;
        // The engine reports success or the honest reason it could not.
        prompt(ctx);
    }
}

void ChainDimCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void DimLayerCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Enter new value for DIMLAYER, or . for use current <use current>: ");
}

void DimLayerCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (!t.empty()) {
        ctx.submit(core::SetDimLayerCommand{t == "." ? std::string() : t});
    }
    done_ = true;
}

void DimLayerCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// Inquiry (issues #30, #63): DIST / ID / AREA / LIST / MEASUREGEOM / MASSPROP / TIME /
// STATUS / CAL / DWGPROPS
// ---------------------------------------------------------------------------
namespace {
double poly_area(const std::vector<core::Vec2>& p) {
    double a = 0.0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const core::Vec2 u = p[i];
        const core::Vec2 v = p[(i + 1) % p.size()];
        a += u.x * v.y - v.x * u.y;
    }
    return std::abs(a) * 0.5;
}
double poly_length(const std::vector<core::Vec2>& p, bool closed) {
    double l = 0.0;
    for (std::size_t i = 1; i < p.size(); ++i) {
        l += core::distance(p[i - 1], p[i]);
    }
    if (closed && p.size() > 2) {
        l += core::distance(p.back(), p.front());
    }
    return l;
}
std::string dist_readout(CommandContext& ctx, core::Vec2 a, core::Vec2 b) {
    // AutoCAD's DIST block, the Z figures included (always zero in 2D).
    const core::Vec2 d = b - a;
    const core::DrawingUnits u = ctx.units();
    return "Distance = " + core::units::format_length(core::length(d), u) +
           ",  Angle in XY Plane = " + core::units::format_angle(std::atan2(d.y, d.x), u) +
           ",  Angle from XY Plane = " + core::units::format_angle(0.0, u) + "\nDelta X = " +
           core::units::format_length(d.x, u) + ",  Delta Y = " + core::units::format_length(d.y, u) +
           ",  Delta Z = " + core::units::format_length(0.0, u);
}
} // namespace

void DistCommand::prompt_next(CommandContext& ctx) {
    PreviewSpec pv;
    pv.kind = PreviewKind::Polyline;
    pv.points = pts_;
    ctx.set_preview(pv);
    ctx.set_prompt("Specify next point or [Length/Undo/Total] <Total>: ");
}

void DistCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    state_ = State::First;
    pts_.clear();
    total_ = 0.0;
    ctx.set_prompt("Specify first point or [Multiple points]: ");
}

void DistCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::First:
        if (u == "M" || u == "MULTIPLE" || u == "MULTIPLE POINTS") {
            state_ = State::MultiFirst;
            ctx.set_prompt("Specify first point: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            first_ = *p;
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Segment, {*p}});
            state_ = State::Second;
            ctx.set_prompt("Specify second point or [Multiple points]: ");
        }
        return;
    case State::Second:
        if (u == "M" || u == "MULTIPLE" || u == "MULTIPLE POINTS") {
            pts_ = {first_};
            state_ = State::MultiNext;
            prompt_next(ctx);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            // Answered from the picked points alone -- no store access, so no round trip.
            ctx.echo(dist_readout(ctx, first_, *p));
            done_ = true;
        }
        return;
    case State::MultiFirst:
        if (const auto p = read_point(ctx, text)) {
            pts_ = {*p};
            ctx.set_last_point(*p);
            state_ = State::MultiNext;
            prompt_next(ctx);
        }
        return;
    case State::MultiNext:
        if (t.empty() || u == "T" || u == "TOTAL") {
            ctx.echo("Distance = " + core::units::format_length(total_, ctx.units()));
            done_ = true;
            return;
        }
        if (u == "U" || u == "UNDO") {
            if (pts_.size() > 1) {
                total_ -= core::distance(pts_[pts_.size() - 2], pts_.back());
                pts_.pop_back();
                ctx.set_last_point(pts_.back());
            }
            prompt_next(ctx);
            return;
        }
        if (u == "L" || u == "LENGTH") {
            ctx.set_prompt("Specify length of line: ");
            return; // a number goes along the cursor (direct distance entry)
        }
        if (const auto p = read_point(ctx, text)) {
            total_ += core::distance(pts_.back(), *p);
            pts_.push_back(*p);
            ctx.set_last_point(*p);
            ctx.echo("Distance = " + core::units::format_length(total_, ctx.units()));
            prompt_next(ctx);
        }
        return;
    }
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
        ctx.echo("X = " + core::units::format_length(p->x, u) + "     Y = " + core::units::format_length(p->y, u) +
                 "     Z = " + core::units::format_length(0.0, u));
        ctx.set_last_point(*p);
        done_ = true;
    }
}

void IdCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// --- AREA -----------------------------------------------------------------
void AreaCommand::prompt_first(CommandContext& ctx) {
    state_ = State::First;
    pts_.clear();
    marks_.clear();
    have_dir_ = false;
    ctx.clear_preview();
    if (mode_ > 0) {
        ctx.set_prompt("Specify first corner point or [Object/Subtract area/eXit] <Object>: ");
    } else if (mode_ < 0) {
        ctx.set_prompt("Specify first corner point or [Object/Add area/eXit] <Object>: ");
    } else {
        ctx.set_prompt("Specify first corner point or [Object/Add area/Subtract area] <Object>: ");
    }
}

void AreaCommand::refresh_preview(CommandContext& ctx) {
    PreviewSpec pv;
    pv.kind = PreviewKind::Polyline;
    pv.points = pts_;
    ctx.set_preview(pv);
}

void AreaCommand::prompt_next(CommandContext& ctx) {
    state_ = State::Next;
    refresh_preview(ctx);
    ctx.set_prompt(pts_.size() >= 3 ? "Specify next point or [Arc/Length/Undo/Total] <Total>: "
                                    : "Specify next point or [Arc/Length/Undo]: ");
}

void AreaCommand::add_point(CommandContext& ctx, core::Vec2 p) {
    marks_.push_back(pts_.size());
    if (!pts_.empty()) {
        const core::Vec2 d = p - pts_.back();
        if (core::length(d) > 1e-12) {
            last_dir_ = std::atan2(d.y, d.x);
            have_dir_ = true;
        }
    }
    pts_.push_back(p);
    ctx.set_last_point(p);
}

void AreaCommand::add_arc(CommandContext& ctx, const core::ConstructedArc& arc) {
    // The arc as the polygon sees it: sampled from its start round to the end point.
    marks_.push_back(pts_.size());
    const int n = std::max(8, static_cast<int>(std::abs(arc.sweep) / core::kTwoPi * 64.0));
    for (int i = 1; i <= n; ++i) {
        const double a = arc.start + arc.sweep * static_cast<double>(i) / n;
        pts_.push_back({arc.center.x + arc.radius * std::cos(a), arc.center.y + arc.radius * std::sin(a)});
    }
    pts_.back() = arc.end_point;
    last_dir_ = arc.end_tangent;
    have_dir_ = true;
    ctx.set_last_point(arc.end_point);
}

void AreaCommand::undo_segment(CommandContext& ctx) {
    if (marks_.empty()) {
        ctx.echo("Nothing to undo.");
        return;
    }
    pts_.resize(marks_.back());
    marks_.pop_back();
    if (pts_.size() >= 2) {
        const core::Vec2 d = pts_.back() - pts_[pts_.size() - 2];
        last_dir_ = std::atan2(d.y, d.x);
        have_dir_ = core::length(d) > 1e-12;
    } else {
        have_dir_ = false;
    }
    if (!pts_.empty()) {
        ctx.set_last_point(pts_.back());
    }
}

void AreaCommand::submit(CommandContext& ctx, core::AreaQueryCommand q) {
    q.pick_radius = ctx.pick_radius();
    q.mode = mode_;
    q.reset = mode_ != 0 && first_total_;
    if (volume_ && q.height <= 0.0) {
        pending_ = q;
        state_ = State::Height;
        ctx.set_prompt("Specify height: ");
        return;
    }
    ctx.submit(q); // the engine reports the value and keeps the running total
    first_total_ = false;
    if (mode_ == 0) {
        done_ = true;
        ctx.clear_preview();
    } else {
        prompt_first(ctx);
    }
}

void AreaCommand::finish_points(CommandContext& ctx) {
    core::AreaQueryCommand q;
    q.from_points = true;
    q.points_area = pts_.size() >= 3 ? poly_area(pts_) : 0.0;
    q.points_perimeter = poly_length(pts_, pts_.size() >= 3);
    submit(ctx, q);
}

void AreaCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    mode_ = 0;
    first_total_ = true;
    prompt_first(ctx);
}

void AreaCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::First:
        if (t.empty() || u == "O" || u == "OBJECT") {
            state_ = State::Object;
            ctx.set_prompt("Select objects: ");
            return;
        }
        if (u == "A" || u == "ADD" || u == "ADD AREA") {
            mode_ = 1;
            prompt_first(ctx);
            return;
        }
        if (u == "S" || u == "SUBTRACT" || u == "SUBTRACT AREA") {
            mode_ = -1;
            prompt_first(ctx);
            return;
        }
        if ((u == "X" || u == "EXIT") && mode_ != 0) {
            done_ = true;
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            add_point(ctx, *p);
            prompt_next(ctx);
        }
        return;
    case State::Object:
        if (t.empty()) {
            if (mode_ == 0) {
                done_ = true;
            } else {
                prompt_first(ctx);
            }
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            core::AreaQueryCommand q;
            q.at = *p;
            submit(ctx, q);
        }
        return;
    case State::Next:
        if (t.empty() || u == "T" || u == "TOTAL") {
            finish_points(ctx);
            return;
        }
        if (u == "A" || u == "ARC") {
            state_ = State::ArcEnd;
            ctx.set_prompt("Specify endpoint of arc or [Second pt/Line/Undo]: ");
            return;
        }
        if (u == "L" || u == "LENGTH") {
            state_ = State::Length;
            ctx.set_prompt("Specify length of line: ");
            return;
        }
        if (u == "U" || u == "UNDO") {
            undo_segment(ctx);
            prompt_next(ctx);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            add_point(ctx, *p);
            prompt_next(ctx);
        }
        return;
    case State::Length:
        if (const auto p = read_point(ctx, text)) { // a bare number goes along the cursor
            add_point(ctx, *p);
        }
        prompt_next(ctx);
        return;
    case State::ArcEnd:
        if (t.empty() || u == "T" || u == "TOTAL") {
            finish_points(ctx);
            return;
        }
        if (u == "L" || u == "LINE") {
            prompt_next(ctx);
            return;
        }
        if (u == "S" || u == "SECOND" || u == "SECOND PT") {
            state_ = State::ArcSecond;
            ctx.set_prompt("Specify second point on arc: ");
            return;
        }
        if (u == "U" || u == "UNDO") {
            undo_segment(ctx);
            refresh_preview(ctx);
            ctx.set_prompt("Specify endpoint of arc or [Second pt/Line/Undo]: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            if (!have_dir_) {
                ctx.echo("No direction to continue from: give a Second pt first.");
                return;
            }
            // Tangent to the last segment: the centre sits on the normal at the start.
            const core::Vec2 s0 = pts_.back();
            const core::Vec2 tdir{std::cos(last_dir_), std::sin(last_dir_)};
            const core::Vec2 n{-tdir.y, tdir.x};
            const core::Vec2 d = *p - s0;
            const double nd = n.x * d.x + n.y * d.y;
            if (std::abs(nd) < 1e-9 * (1.0 + core::length(d))) {
                add_point(ctx, *p); // collinear: a straight segment
            } else {
                const double r = core::length_squared(d) / (2.0 * nd); // signed: + centre to the left
                const core::Vec2 c = s0 + n * r;
                const double a0 = std::atan2(s0.y - c.y, s0.x - c.x);
                const double a1 = std::atan2(p->y - c.y, p->x - c.x);
                double sweep = a1 - a0;
                if (r > 0.0) { // counter-clockwise
                    while (sweep <= 0.0) {
                        sweep += core::kTwoPi;
                    }
                } else {
                    while (sweep >= 0.0) {
                        sweep -= core::kTwoPi;
                    }
                }
                core::ConstructedArc arc = core::arc_detail::from_sweep(c, std::abs(r), a0, sweep);
                arc.end_point = *p;
                add_arc(ctx, arc);
            }
            refresh_preview(ctx);
            ctx.set_prompt("Specify endpoint of arc or [Second pt/Line/Undo]: ");
        }
        return;
    case State::ArcSecond:
        if (const auto p = read_point(ctx, text)) {
            arc_second_ = *p;
            state_ = State::ArcSecondEnd;
            ctx.set_prompt("Specify end point of arc: ");
        }
        return;
    case State::ArcSecondEnd:
        if (const auto p = read_point(ctx, text)) {
            const core::Vec2 s0 = pts_.back();
            if (const auto arc = core::arc_three_points(s0, arc_second_, *p)) {
                add_arc(ctx, *arc);
            } else {
                ctx.echo("The three points are in a line: a straight segment is used.");
                add_point(ctx, *p);
            }
            state_ = State::ArcEnd;
            refresh_preview(ctx);
            ctx.set_prompt("Specify endpoint of arc or [Second pt/Line/Undo]: ");
        }
        return;
    case State::Height: {
        double h = 0.0;
        if (!parse_number(t, h) || !(h > 0.0)) {
            ctx.echo("Enter a positive height.");
            return;
        }
        core::AreaQueryCommand q = pending_;
        q.height = h;
        ctx.submit(q);
        first_total_ = false;
        if (mode_ == 0) {
            done_ = true;
            ctx.clear_preview();
        } else {
            prompt_first(ctx);
        }
        return;
    }
    }
}

void AreaCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// --- LIST -----------------------------------------------------------------
void ListCommand::finish(CommandContext& ctx) {
    if (ctx.has_selection()) {
        core::ListQueryCommand q;
        q.selection = true;
        ctx.submit(q);
    }
    done_ = true;
}

void ListCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (ctx.has_selection()) {
        finish(ctx);
        return;
    }
    select_.begin(ctx);
}

void ListCommand::input(CommandContext& ctx, const std::string& text) {
    if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void ListCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void ListCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// --- MEASUREGEOM ----------------------------------------------------------
void MeasureGeomCommand::prompt_option(CommandContext& ctx) {
    state_ = State::Option;
    ctx.clear_preview();
    if (quick_) {
        ctx.set_prompt("Click an object to measure, or [Distance/Radius/Angle/ARea/Volume/Mode/eXit] <eXit>: ");
    } else if (measured_) {
        ctx.set_prompt("Enter an option [Distance/Radius/Angle/ARea/Volume/Quick/Mode/eXit] <eXit>: ");
    } else {
        ctx.set_prompt("Enter an option [Distance/Radius/Angle/ARea/Volume/Quick/Mode] <Distance>: ");
    }
}

void MeasureGeomCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    measured_ = false;
    prompt_option(ctx);
}

void MeasureGeomCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void MeasureGeomCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    if (state_ == State::Area) {
        area_->input(ctx, text);
        if (area_->done()) {
            area_.reset();
            measured_ = true;
            prompt_option(ctx);
        }
        return;
    }
    switch (state_) {
    case State::Option:
        if (t.empty()) {
            if (measured_ || quick_) {
                done_ = true;
                return;
            }
            state_ = State::DistFirst;
            ctx.set_prompt("Specify first point: ");
            return;
        }
        if (u == "X" || u == "EXIT") {
            done_ = true;
        } else if (u == "D" || u == "DISTANCE") {
            state_ = State::DistFirst;
            ctx.set_prompt("Specify first point: ");
        } else if (u == "R" || u == "RADIUS") {
            state_ = State::Radius;
            ctx.set_prompt("Select arc or circle: ");
        } else if (u == "A" || u == "ANGLE") {
            state_ = State::AngleFirst;
            ctx.set_prompt("Select arc, circle, line, or <specify vertex>: ");
        } else if (u == "AR" || u == "AREA" || u == "V" || u == "VOLUME") {
            area_ = std::make_unique<AreaCommand>();
            area_->set_volume(u == "V" || u == "VOLUME");
            state_ = State::Area;
            area_->start(ctx);
        } else if (u == "Q" || u == "QUICK") {
            quick_ = true;
            prompt_option(ctx);
        } else if (u == "M" || u == "MODE") {
            state_ = State::Mode;
            ctx.set_prompt(std::string("Enter a mode [Quick/Standard] <") + (quick_ ? "Quick" : "Standard") + ">: ");
        } else if (quick_) {
            if (const auto p = read_point(ctx, text)) {
                core::MeasureQueryCommand q;
                q.at = *p;
                q.pick_radius = ctx.pick_radius();
                q.what = 2;
                ctx.submit(q);
                measured_ = true;
                prompt_option(ctx);
            }
        } else {
            ctx.echo("Enter Distance, Radius, Angle, ARea, Volume, Quick, Mode or eXit.");
        }
        return;
    case State::Mode:
        if (u == "Q" || u == "QUICK") {
            quick_ = true;
        } else if (u == "S" || u == "STANDARD") {
            quick_ = false;
        } else if (!t.empty()) {
            ctx.echo("Enter Quick or Standard.");
            return;
        }
        prompt_option(ctx);
        return;
    case State::DistFirst:
        if (const auto p = read_point(ctx, text)) {
            first_ = *p;
            ctx.set_last_point(*p);
            ctx.set_preview({PreviewKind::Segment, {*p}});
            state_ = State::DistSecond;
            ctx.set_prompt("Specify second point: ");
        }
        return;
    case State::DistSecond:
        if (const auto p = read_point(ctx, text)) {
            ctx.echo(dist_readout(ctx, first_, *p));
            measured_ = true;
            prompt_option(ctx);
        }
        return;
    case State::Radius:
        if (const auto p = read_point(ctx, text)) {
            core::MeasureQueryCommand q;
            q.at = *p;
            q.pick_radius = ctx.pick_radius();
            q.what = 0;
            ctx.submit(q);
            measured_ = true;
            prompt_option(ctx);
        }
        return;
    case State::AngleFirst:
        if (const auto p = read_point(ctx, text)) {
            first_ = *p;
            const auto k = ctx.hovered_kind();
            if (k.has_value() && (*k == core::EntityKind::Arc || *k == core::EntityKind::Circle)) {
                core::MeasureQueryCommand q;
                q.at = *p;
                q.at2 = *p;
                q.pick_radius = ctx.pick_radius();
                q.what = 1;
                ctx.submit(q);
                measured_ = true;
                prompt_option(ctx);
                return;
            }
            state_ = State::AngleSecond;
            ctx.set_prompt("Select second line: ");
        }
        return;
    case State::AngleSecond:
        if (const auto p = read_point(ctx, text)) {
            core::MeasureQueryCommand q;
            q.at = first_;
            q.at2 = *p;
            q.pick_radius = ctx.pick_radius();
            q.what = 1;
            ctx.submit(q);
            measured_ = true;
            prompt_option(ctx);
        }
        return;
    case State::Quick:
    case State::Area:
        return;
    }
}

// --- MASSPROP / TIME / STATUS / CAL / DWGPROPS ----------------------------
void MassPropCommand::finish(CommandContext& ctx) {
    if (ctx.has_selection()) {
        ctx.submit(core::MassPropQueryCommand{});
    }
    done_ = true;
}

void MassPropCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (ctx.has_selection()) {
        finish(ctx);
        return;
    }
    select_.begin(ctx);
}

void MassPropCommand::input(CommandContext& ctx, const std::string& text) {
    if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void MassPropCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void MassPropCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void TimeCommand::start(CommandContext& ctx) {
    ctx.submit(core::TimeCommand{0}); // the engine prints the times
    ctx.set_prompt("Enter option [Display/ON/OFF/Reset]: ");
}

void TimeCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    if (u.empty()) {
        done_ = true;
        return;
    }
    if (u == "D" || u == "DISPLAY") {
        ctx.submit(core::TimeCommand{0});
    } else if (u == "ON") {
        ctx.submit(core::TimeCommand{1});
    } else if (u == "OFF") {
        ctx.submit(core::TimeCommand{2});
    } else if (u == "R" || u == "RESET") {
        ctx.submit(core::TimeCommand{3});
    } else {
        ctx.echo("Enter Display, ON, OFF or Reset.");
    }
    ctx.set_prompt("Enter option [Display/ON/OFF/Reset]: ");
}

void TimeCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void StatusCommand::start(CommandContext& ctx) {
    ctx.submit(core::StatusQueryCommand{ctx.status_modes()});
    done_ = true;
}

void CalCommand::start(CommandContext& ctx) {
    if (palette_ && ctx.view() != nullptr && ctx.view()->quickcalc_dialog()) {
        done_ = true;
        return;
    }
    ctx.set_prompt(">> Expression: ");
}

void CalCommand::input(CommandContext& ctx, const std::string& text) {
    std::string error;
    const auto v = calc::evaluate(text, error);
    if (!v) {
        ctx.echo("CAL: " + error);
        return;
    }
    ctx.echo(calc::format(*v));
    done_ = true;
}

void CalCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void DwgPropsCommand::start(CommandContext& ctx) {
    if (ctx.view() != nullptr && ctx.view()->dwgprops_dialog()) {
        done_ = true;
        return;
    }
    const core::DrawingProps p = ctx.drawing_props();
    ctx.echo("Title: " + p.title + "\nSubject: " + p.subject + "\nAuthor: " + p.author + "\nKeywords: " + p.keywords +
             "\nComments: " + p.comments + "\nHyperlink base: " + p.hyperlink_base);
    for (const auto& [k, v] : p.custom) {
        ctx.echo(k + ": " + v);
    }
    done_ = true;
}

// ---------------------------------------------------------------------------
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
namespace {
/// The name of table style `i` in `styles` ("Standard" when the list is not known).
std::string table_style_name(const std::vector<core::TableStyle>& styles, std::uint16_t i) {
    return i < styles.size() ? styles[i].name : std::string("Standard");
}
/// The index of the table style named `name` (any case), if there is one.
std::optional<std::uint16_t> find_table_style(const std::vector<core::TableStyle>& styles, const std::string& name) {
    if (styles.empty()) {
        return upper(name) == "STANDARD" ? std::optional<std::uint16_t>(0) : std::nullopt;
    }
    for (std::size_t i = 0; i < styles.size(); ++i) {
        if (upper(styles[i].name) == upper(name)) {
            return static_cast<std::uint16_t>(i);
        }
    }
    return std::nullopt;
}
std::string table_style_list(const std::vector<core::TableStyle>& styles) {
    std::string s = "Table styles:";
    if (styles.empty()) {
        return s + " Standard";
    }
    for (std::size_t i = 0; i < styles.size(); ++i) {
        s += (i == 0 ? " " : ", ") + styles[i].name;
    }
    return s;
}
} // namespace

void TableCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    mode_ = Mode::Cols;
    style_ = ctx.current_table_style();
    if (dialog_ && ctx.view() != nullptr) {
        InsertTableSettings io{style_, cols_, col_w_, rows_, row_lines_, title_, header_};
        const int r = ctx.view()->insert_table_dialog(io);
        if (r == 0) {
            done_ = true; // cancelled
            return;
        }
        if (r == 1) {
            style_ = io.style;
            cols_ = std::clamp(io.cols, 1, 500);
            col_w_ = io.col_width > 1e-6 ? io.col_width : col_w_;
            rows_ = std::clamp(io.rows, 1, 500);
            row_lines_ = std::max(io.row_lines, 1);
            title_ = io.title;
            header_ = io.header;
            link_ = io.link;
            mode_ = Mode::DialogPlace;
            outline(ctx);
            ctx.set_prompt("Specify insertion point: ");
            return;
        }
    }
    ctx.echo("Current table style: " + table_style_name(ctx.table_styles(), style_) + "  Cell width: " + fmt4(col_w_) +
             "  Cell height: " + std::to_string(row_lines_) + " line(s)");
    ctx.set_prompt("Enter number of columns or [Auto] <" + std::to_string(cols_) + ">: ");
}

void TableCommand::outline(CommandContext& ctx) const {
    if (!link_.empty()) {
        return; // the file decides its size
    }
    const std::vector<core::TableStyle> styles = ctx.table_styles();
    const double line = style_ < styles.size() ? kLine * styles[style_].data_height / 2.5 : kLine;
    const int rows = rows_ + (title_ ? 1 : 0) + (header_ ? 1 : 0);
    const double w = col_w_ * cols_;
    const double h = line * row_lines_;
    PreviewSpec pv;
    pv.kind = PreviewKind::Outline;
    for (int r = 0; r <= rows; ++r) { // the top-left corner at the cursor
        pv.points.push_back({0.0, -h * r});
        pv.points.push_back({w, -h * r});
    }
    for (int c = 0; c <= cols_; ++c) {
        // The title row is one cell across: its column lines start below it.
        const double top = (title_ && c > 0 && c < cols_) ? -h : 0.0;
        pv.points.push_back({col_w_ * c, top});
        pv.points.push_back({col_w_ * c, -h * rows});
    }
    ctx.set_preview(std::move(pv));
}

void TableCommand::place_prompt(CommandContext& ctx) {
    mode_ = Mode::Place;
    outline(ctx);
    ctx.set_prompt("Specify insertion point or [Style/Width/Height]: ");
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
    const std::string u = upper(t);
    switch (mode_) {
    case Mode::Cols:
    case Mode::Rows: {
        if (u == "A" || u == "AUTO") {
            ctx.echo("Auto sizing from a window is not available yet; enter a number.");
            return;
        }
        double v = mode_ == Mode::Cols ? cols_ : rows_;
        if (!t.empty()) {
            const std::optional<double> n = parse_number(t);
            if (!n || *n < 1.0 || *n > 500.0 || *n != std::floor(*n)) {
                ctx.echo("Requires a whole number from 1 to 500.");
                return;
            }
            v = *n;
        }
        if (mode_ == Mode::Cols) {
            cols_ = static_cast<int>(v);
            mode_ = Mode::Rows;
            ctx.set_prompt("Enter number of data rows or [Auto] <" + std::to_string(rows_) + ">: ");
        } else {
            rows_ = static_cast<int>(v);
            place_prompt(ctx);
        }
        return;
    }
    case Mode::ColWidth:
        number(col_w_, 1e-6);
        place_prompt(ctx);
        return;
    case Mode::RowHeight: {
        double v = row_lines_;
        number(v, 1.0);
        row_lines_ = static_cast<int>(v);
        place_prompt(ctx);
        return;
    }
    case Mode::Style:
        if (t == "?") {
            ctx.echo(table_style_list(ctx.table_styles()));
            return;
        }
        if (!t.empty()) {
            const std::optional<std::uint16_t> i = find_table_style(ctx.table_styles(), t);
            if (!i) {
                ctx.echo("Cannot find table style \"" + t + "\".");
                return;
            }
            style_ = *i;
        }
        place_prompt(ctx);
        return;
    case Mode::DialogPlace:
    case Mode::Place:
        if (mode_ == Mode::Place && (u == "W" || u == "WIDTH")) {
            mode_ = Mode::ColWidth;
            ctx.set_prompt("Specify column width <" + fmt4(col_w_) + ">: ");
            return;
        }
        if (mode_ == Mode::Place && (u == "H" || u == "HEIGHT")) {
            mode_ = Mode::RowHeight;
            ctx.set_prompt("Specify row height in lines <" + std::to_string(row_lines_) + ">: ");
            return;
        }
        if (mode_ == Mode::Place && (u == "S" || u == "STYLE")) {
            mode_ = Mode::Style;
            ctx.set_prompt("Enter a table style name or [?] <" + table_style_name(ctx.table_styles(), style_) + ">: ");
            return;
        }
        if (const auto p = read_point(ctx, text); p && !link_.empty()) {
            // From a data link: the file's rows and columns, read by the engine.
            ctx.submit(core::InsertLinkedTableCommand{link_, *p, ctx.group_id(), style_});
            done_ = true;
            return;
        } else if (p) {
            ctx.set_preview({});
            // A title row and a header row above the data rows, as AutoCAD's Standard style
            // (the Insert Table window can leave either out).
            const int rows = rows_ + (title_ ? 1 : 0) + (header_ ? 1 : 0);
            const std::vector<core::TableStyle> styles = ctx.table_styles();
            // A line is Standard's (2.5 text) scaled to the style's data text height.
            const double line = style_ < styles.size() ? kLine * styles[style_].data_height / 2.5 : kLine;
            core::AddTableCommand cmd;
            cmd.rows = static_cast<std::uint16_t>(rows);
            cmd.cols = static_cast<std::uint16_t>(cols_);
            cmd.style = style_;
            cmd.col_widths.assign(static_cast<std::size_t>(cols_), col_w_);
            cmd.row_heights.assign(static_cast<std::size_t>(rows), line * row_lines_);
            cmd.has_title = title_;
            cmd.has_header = header_;
            const std::size_t n = static_cast<std::size_t>(rows) * cols_;
            cmd.cells.assign(n, core::TableCell{});
            cmd.texts.assign(n, std::string{});
            cmd.pos = *p;
            cmd.group = ctx.group_id();
            ctx.submit(std::move(cmd));
            // From the Insert Table window the first cell opens for typing, as in AutoCAD.
            const core::Vec2 first{p->x + col_w_ * 0.5, p->y - line * row_lines_ * 0.5};
            if (!dialog_ || ctx.view() == nullptr || !ctx.view()->edit_text_soon(first)) {
                ctx.echo("Table placed. Double-click a cell to edit it.");
            }
            done_ = true;
        }
        return;
    }
}

void TableCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.set_preview({});
    done_ = true;
}

void TableEditCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Pick a table cell: ");
}

void TableEditCommand::input(CommandContext& ctx, const std::string& text) {
    if (picked_) {
        ctx.submit(core::EditTextContentCommand{at_, radius_, text, ctx.group_id()});
        done_ = true;
        return;
    }
    if (trimmed(text).empty()) {
        done_ = true;
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        if (ctx.view() != nullptr && ctx.view()->edit_text_at(*p)) {
            done_ = true; // the editor does the rest
            return;
        }
        at_ = *p;
        radius_ = ctx.pick_radius();
        picked_ = true;
        ctx.set_prompt("Enter new text: ");
    }
}

void TableEditCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void TableStyleUiCommand::start(CommandContext& ctx) {
    if (ctx.view() != nullptr && ctx.view()->table_style_dialog()) {
        done_ = true; // the Table Style window does the rest
        return;
    }
    ctx.set_prompt("Enter table style name to make current or [?] <" +
                   table_style_name(ctx.table_styles(), ctx.current_table_style()) + ">: ");
}

void TableStyleUiCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (t == "?") {
        ctx.echo(table_style_list(ctx.table_styles()));
        return;
    }
    if (!t.empty()) {
        if (!find_table_style(ctx.table_styles(), t)) {
            ctx.echo("Cannot find table style \"" + t + "\".");
            return;
        }
        ctx.submit(core::TableStyleCommand{core::TableStyleCommand::Op::SetCurrent, t, {}});
    }
    done_ = true;
}

void TableStyleUiCommand::cancel(CommandContext& ctx) {
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
    if (ctx.view() != nullptr) {
        // The Geometric Tolerance window, as AutoCAD opens; the command line only where
        // there is no window.
        std::vector<std::string> cells;
        const int r = ctx.view()->tolerance_dialog(cells);
        if (r == 0 || (r == 1 && cells.empty())) {
            done_ = true;
            return;
        }
        if (r == 1) {
            cells_ = std::move(cells);
            mode_ = Mode::Place;
            ctx.set_prompt("Enter tolerance location: ");
            return;
        }
    }
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
            ctx.set_prompt("Enter tolerance location: ");
            return;
        }
        if (cells_.size() >= 5) { // characteristic + tolerance + up to three datums
            ctx.echo("A feature control frame carries at most five cells.");
            mode_ = Mode::Place;
            ctx.set_prompt("Enter tolerance location: ");
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
        cmd.style = ctx.current_dim_style();
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
            cmd.style = ctx.current_dim_style();
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
    ctx.set_prompt("Specify leader start point: ");
}

void LeaderCommand::more_prompt(CommandContext& ctx) {
    state_ = pts_.size() < 2 ? State::Next : State::More;
    ctx.set_preview(PreviewSpec{PreviewKind::Polyline, pts_});
    ctx.set_prompt(pts_.size() < 2 ? "Specify next point: "
                                   : "Specify next point or [Annotation/Format/Undo] <Annotation>: ");
}

void LeaderCommand::finish(CommandContext& ctx) {
    ctx.clear_preview();
    std::string content;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        content += (i > 0 ? "\n" : "") + lines_[i];
    }
    const double h = current_dim_text_height(ctx);
    if (pts_.size() == 2 && lines_.size() <= 1) {
        ctx.submit(core::AddLeaderCommand{pts_[0], pts_[1], h, ctx.current_dim_style(), content, ctx.group_id()});
    } else {
        // The text beside the last point, on the side the leader comes in from.
        const bool right = pts_.back().x >= pts_[pts_.size() - 2].x;
        core::MTextBlock b;
        b.height = h;
        b.attach = right ? 3 : 5; // middle left / middle right
        b.pos = pts_.back() + core::Vec2{right ? h * 0.5 : -h * 0.5, 0.0};
        ctx.submit(core::AddMLeaderCommand{pts_, ctx.current_dim_style(), b, content, ctx.group_id()});
    }
    ctx.echo("Leader placed.");
    done_ = true;
}

void LeaderCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Start:
    case State::Next:
        if (const auto p = read_point(ctx, text)) {
            pts_.push_back(*p);
            ctx.set_last_point(*p);
            more_prompt(ctx);
        }
        return;
    case State::More:
        if (t.empty() || u == "A" || u == "ANNOTATION") {
            ctx.clear_preview();
            state_ = State::AnnoFirst;
            ctx.set_prompt("Enter first line of annotation text or <options>: ");
            return;
        }
        if (u == "F" || u == "FORMAT") {
            state_ = State::Format;
            ctx.set_prompt("Enter leader format option [Spline/STraight/Arrow/None] <Exit>: ");
            return;
        }
        if (u == "U" || u == "UNDO") {
            pts_.pop_back();
            ctx.set_last_point(pts_.back());
            more_prompt(ctx);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            pts_.push_back(*p);
            ctx.set_last_point(*p);
            more_prompt(ctx);
        }
        return;
    case State::Format:
        if (u == "S" || u == "SPLINE") {
            ctx.echo("Spline leaders are not available yet; the leader stays straight.");
        } else if (u == "N" || u == "NONE") {
            ctx.echo("A leader here always has its arrowhead.");
        } else if (!t.empty() && u != "ST" && u != "STRAIGHT" && u != "A" && u != "ARROW" && u != "E" && u != "EXIT") {
            ctx.echo("Enter Spline, STraight, Arrow, None or Exit.");
            return;
        }
        more_prompt(ctx);
        return;
    case State::AnnoFirst:
        if (t.empty()) {
            state_ = State::AnnoOption;
            ctx.set_prompt("Enter an annotation option [Tolerance/Copy/Block/None/Mtext] <Mtext>: ");
            return;
        }
        lines_.push_back(t);
        state_ = State::AnnoNext;
        ctx.set_prompt("Enter next line of annotation text: ");
        return;
    case State::AnnoNext:
        if (t.empty()) {
            finish(ctx);
            return;
        }
        lines_.push_back(t);
        return;
    case State::AnnoOption:
        if (u == "N" || u == "NONE") {
            finish(ctx);
            return;
        }
        if (t.empty() || u == "M" || u == "MTEXT") {
            state_ = State::AnnoMtext;
            ctx.set_prompt("Enter annotation text: ");
            return;
        }
        if (u == "T" || u == "TOLERANCE" || u == "C" || u == "COPY" || u == "B" || u == "BLOCK") {
            ctx.echo("Tolerance, Copy and Block annotations are not available yet.");
        } else {
            ctx.echo("Enter Tolerance, Copy, Block, None or Mtext.");
        }
        return;
    case State::AnnoMtext:
        if (!t.empty()) {
            lines_.push_back(t);
        }
        finish(ctx);
        return;
    }
}

void LeaderCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.clear_preview();
    done_ = true;
}

// ---------------------------------------------------------------------------
// MLEADER (issue #61)
// ---------------------------------------------------------------------------
void MLeaderCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    // The current multileader style: its leader points, landing and content to begin with
    // (the Options change them for this leader), its arrow and text when it is placed.
    const std::vector<core::MLeaderStyle> styles = ctx.mleader_styles();
    const std::uint16_t cur = ctx.current_mleader_style();
    style_ = cur < styles.size() ? styles[cur] : core::MLeaderStyle{};
    s_max_points_ = style_.max_points;
    s_content_ = style_.text;
    s_landing_ = style_.landing;
    first_prompt(ctx);
}

void MLeaderCommand::first_prompt(CommandContext& ctx) {
    state_ = State::First;
    switch (order_) {
    case Order::Arrow:
        ctx.set_prompt("Specify leader arrowhead location or [leader Landing first/Content first/Options] <Options>: ");
        return;
    case Order::Landing:
        ctx.set_prompt("Specify leader landing location or [leader arrowHead first/Content first/Options] <Options>: ");
        return;
    case Order::Content:
        ctx.set_prompt("Specify location of the text or [leader arrowHead first/leader Landing first/Options] <Options>: ");
        return;
    }
}

void MLeaderCommand::after_points(CommandContext& ctx) {
    ctx.set_preview(PreviewSpec{PreviewKind::Polyline, pts_});
    if (order_ == Order::Arrow) {
        if (static_cast<int>(pts_.size()) < s_max_points_ - 1) {
            state_ = State::Next;
            ctx.set_prompt("Specify next point: ");
        } else {
            state_ = State::Landing;
            ctx.set_prompt("Specify leader landing location: ");
        }
        return;
    }
    state_ = State::Arrow; // the landing (or the text) is there: the arrowhead next
    ctx.set_prompt("Specify leader arrowhead location: ");
}

void MLeaderCommand::finish(CommandContext& ctx, const std::string& text) {
    ctx.clear_preview();
    const double h = style_.text_height > 0.0 ? style_.text_height : current_dim_text_height(ctx);
    std::vector<core::Vec2> v = pts_;
    const bool right = v.back().x >= v[v.size() - 2].x;
    const double side = right ? 1.0 : -1.0;
    if (s_landing_) {
        v.push_back(v.back() + core::Vec2{side * style_.landing_distance, 0.0}); // the horizontal landing
    }
    core::MTextBlock b;
    b.height = h;
    b.attach = right ? 3 : 5; // middle left / middle right, a gap beyond the landing
    b.pos = v.back() + core::Vec2{side * style_.landing_gap, 0.0};
    core::AddMLeaderCommand c{v, ctx.current_dim_style(), b, s_content_ ? text : std::string(), ctx.group_id()};
    c.overrides.arrow_type = style_.arrow; // the style's arrowhead
    c.overrides.arrow_size = style_.arrow_size;
    c.overrides.set(core::DimOverrides::kArrowType, true);
    c.overrides.set(core::DimOverrides::kArrowSize, true);
    ctx.submit(std::move(c));
    ctx.echo("Multileader placed.");
    done_ = true;
}

void MLeaderCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::First:
        if (t.empty() || u == "O" || u == "OPTIONS") {
            state_ = State::Options;
            ctx.set_prompt("Enter an option [Leader type/leader lAnding/Content type/Maxpoints/First angle/Second "
                           "angle/eXit options] <eXit options>: ");
            return;
        }
        if (u == "L" || u == "LANDING" || u == "LEADER LANDING FIRST") {
            order_ = Order::Landing;
            first_prompt(ctx);
            return;
        }
        if (u == "H" || u == "ARROWHEAD" || u == "LEADER ARROWHEAD FIRST") {
            order_ = Order::Arrow;
            first_prompt(ctx);
            return;
        }
        if (u == "C" || u == "CONTENT" || u == "CONTENT FIRST") {
            order_ = Order::Content;
            first_prompt(ctx);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            ctx.set_last_point(*p);
            if (order_ == Order::Content) {
                text_at_ = *p;
                if (!s_content_) {
                    pts_ = {*p};
                    after_points(ctx);
                    return;
                }
                state_ = State::Text;
                ctx.set_prompt("Enter annotation text: ");
                return;
            }
            pts_ = {*p};
            after_points(ctx);
        }
        return;
    case State::Next:
    case State::Landing:
        if (state_ == State::Next && t.empty()) {
            state_ = State::Landing; // Enter: the last point is the landing
            if (pts_.size() >= 2) {
                if (s_content_) {
                    state_ = State::Text;
                    ctx.set_prompt("Enter annotation text: ");
                } else {
                    finish(ctx, {});
                }
            }
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            pts_.push_back(*p);
            ctx.set_last_point(*p);
            if (state_ == State::Next) {
                after_points(ctx);
                return;
            }
            if (s_content_) {
                ctx.clear_preview();
                state_ = State::Text;
                ctx.set_prompt("Enter annotation text: ");
            } else {
                finish(ctx, {});
            }
        }
        return;
    case State::Arrow:
        if (const auto p = read_point(ctx, text)) {
            pts_.insert(pts_.begin(), *p); // the arrowhead first, the landing (or text) last
            if (order_ == Order::Content) {
                finish(ctx, text_);
            } else if (s_content_) {
                ctx.clear_preview();
                state_ = State::Text;
                ctx.set_prompt("Enter annotation text: ");
            } else {
                finish(ctx, {});
            }
        }
        return;
    case State::Text:
        if (order_ == Order::Content && pts_.empty()) {
            text_ = t;
            pts_ = {*text_at_};
            after_points(ctx);
            return;
        }
        finish(ctx, t);
        return;
    case State::Options:
        if (t.empty() || u == "X" || u == "EXIT" || u == "EXIT OPTIONS") {
            first_prompt(ctx);
        } else if (u == "M" || u == "MAXPOINTS") {
            state_ = State::MaxPoints;
            ctx.set_prompt("Enter the maximum points for leader line <" + std::to_string(s_max_points_) + ">: ");
        } else if (u == "C" || u == "CONTENT TYPE") {
            state_ = State::ContentType;
            ctx.set_prompt(std::string("Select a content type [Mtext/None] <") + (s_content_ ? "Mtext" : "None") + ">: ");
        } else if (u == "L" || u == "LEADER TYPE") {
            state_ = State::LeaderType;
            ctx.set_prompt("Select a leader type [Straight/sPline/None] <Straight>: ");
        } else if (u == "A" || u == "LANDING" || u == "LEADER LANDING") {
            state_ = State::UseLanding;
            ctx.set_prompt(std::string("Use landing [Yes/No] <") + (s_landing_ ? "Yes" : "No") + ">: ");
        } else if (u == "F" || u == "S" || u == "FIRST ANGLE" || u == "SECOND ANGLE") {
            ctx.echo("Angle constraints are not available yet.");
        } else {
            ctx.echo("Invalid option keyword.");
        }
        return;
    case State::MaxPoints: {
        if (!t.empty()) {
            const std::optional<double> n = parse_number(t);
            if (!n || *n < 2.0 || *n > 99.0 || *n != std::floor(*n)) {
                ctx.echo("Requires a whole number from 2 to 99.");
                return;
            }
            s_max_points_ = static_cast<int>(*n);
        }
        state_ = State::Options;
        ctx.set_prompt("Enter an option [Leader type/leader lAnding/Content type/Maxpoints/First angle/Second "
                       "angle/eXit options] <eXit options>: ");
        return;
    }
    case State::ContentType:
    case State::LeaderType:
    case State::UseLanding:
        if (state_ == State::ContentType) {
            if (u == "N" || u == "NONE") {
                s_content_ = false;
            } else if (t.empty() || u == "M" || u == "MTEXT") {
                s_content_ = t.empty() ? s_content_ : true;
            } else if (u == "B" || u == "BLOCK") {
                ctx.echo("Block content is not available yet.");
                return;
            } else {
                ctx.echo("Enter Mtext or None.");
                return;
            }
        } else if (state_ == State::LeaderType) {
            if (u == "P" || u == "SPLINE") {
                ctx.echo("Spline leaders are not available yet; the leader stays straight.");
            } else if (u == "N" || u == "NONE") {
                ctx.echo("A multileader here always has its leader line.");
            }
        } else if (u == "Y" || u == "YES" || u == "N" || u == "NO") {
            s_landing_ = u[0] == 'Y';
        } else if (!t.empty()) {
            ctx.echo("Enter Yes or No.");
            return;
        }
        state_ = State::Options;
        ctx.set_prompt("Enter an option [Leader type/leader lAnding/Content type/Maxpoints/First angle/Second "
                       "angle/eXit options] <eXit options>: ");
        return;
    }
}

void MLeaderCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.clear_preview();
    done_ = true;
}

// ---------------------------------------------------------------------------
// MTEXT: two corners (insertion + wrap width) -> paragraph text.
// ---------------------------------------------------------------------------
void MTextCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    // The current text style: its font, and its height when it has one (else 2.5).
    const std::vector<core::TextStyle> styles = ctx.text_styles();
    const std::uint16_t cur = ctx.current_text_style();
    height_ = 2.5;
    std::string style_name = "Standard";
    if (cur < styles.size()) {
        style_name = styles[cur].name;
        font_ = styles[cur].font;
        if (styles[cur].height > 0.0) {
            height_ = styles[cur].height;
        }
    }
    ctx.echo("Current text style:  \"" + style_name + "\"  Text height:  " + fmt4(height_) + "  Annotative:  No");
    ctx.set_prompt("Specify first corner: ");
}

void MTextCommand::corner_prompt(CommandContext& ctx) {
    state_ = State::Second;
    PreviewSpec pv{PreviewKind::Rectangle, {c1_}};
    pv.mtext_attach = attach_; // the box shows the way the text will flow
    ctx.set_preview(std::move(pv));
    ctx.set_prompt("Specify opposite corner or [Height/Justify/Line spacing/Rotation/Style/Width/Columns]: ");
}

void MTextCommand::place(CommandContext& ctx, core::Vec2 other) {
    // The justification picks the point of the box the text hangs from.
    const double x0 = std::min(c1_.x, other.x);
    const double x1 = std::max(c1_.x, other.x);
    const double y0 = std::min(c1_.y, other.y);
    const double y1 = std::max(c1_.y, other.y);
    const int col = attach_ % 3;
    const int row = attach_ / 3;
    pos_ = {col == 0 ? x0 : col == 1 ? (x0 + x1) * 0.5 : x1, row == 0 ? y1 : row == 1 ? (y0 + y1) * 0.5 : y0};
    width_ = x1 - x0;
    state_ = State::Content;
    ctx.clear_preview();
    ctx.set_prompt(typed_ ? "MText: " : "Enter text: ");
}

void MTextCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Content: {
        if (typed_) {
            // -MTEXT: one line at a time; an empty line ends the text.
            if (!text.empty()) {
                lines_.push_back(text);
                ctx.set_prompt("MText: ");
                return;
            }
            if (lines_.empty()) {
                done_ = true;
                return;
            }
        }
        std::string content = text;
        if (typed_) {
            content.clear();
            for (std::size_t i = 0; i < lines_.size(); ++i) {
                content += (i > 0 ? "\n" : "") + lines_[i];
            }
        }
        core::MTextBlock b;
        b.pos = pos_;
        b.width = width_;
        b.height = height_;
        b.attach = attach_;
        b.rotation = rotation_;
        b.line_spacing = spacing_;
        core::AddMTextCommand m{b, content, ctx.group_id()};
        m.font = font_;
        ctx.submit(std::move(m));
        ctx.echo("MText placed.");
        ctx.clear_preview();
        done_ = true;
        return;
    }
    case State::First:
        if (const auto p = read_point(ctx, text)) {
            c1_ = *p;
            ctx.set_last_point(*p);
            corner_prompt(ctx);
        }
        return;
    case State::Second:
        if (u == "H" || u == "HEIGHT") {
            state_ = State::Height;
            ctx.set_prompt("Specify height <" + fmt4(height_) + ">: ");
            return;
        }
        if (u == "J" || u == "JUSTIFY") {
            static constexpr const char* kJ[] = {"TL", "TC", "TR", "ML", "MC", "MR", "BL", "BC", "BR"};
            state_ = State::Justify;
            ctx.set_prompt(std::string("Enter justification [TL/TC/TR/ML/MC/MR/BL/BC/BR] <") + kJ[attach_ % 9] + ">: ");
            return;
        }
        if (u == "L" || u == "LINE SPACING") {
            state_ = State::SpacingType;
            ctx.set_prompt("Enter line spacing type [At least/Exactly] <At least>: ");
            return;
        }
        if (u == "R" || u == "ROTATION") {
            state_ = State::Rotation;
            ctx.set_prompt("Specify rotation angle <" + fmt4(core::to_degrees(rotation_)) + ">: ");
            return;
        }
        if (u == "S" || u == "STYLE") {
            state_ = State::Style;
            ctx.set_prompt("Enter style name or [?] <" +
                           (ctx.current_text_style() < ctx.text_styles().size()
                                ? ctx.text_styles()[ctx.current_text_style()].name
                                : std::string("Standard")) +
                           ">: ");
            return;
        }
        if (u == "W" || u == "WIDTH") {
            state_ = State::Width;
            ctx.set_prompt("Specify width: ");
            return;
        }
        if (u == "C" || u == "COLUMNS") {
            state_ = State::Columns;
            ctx.set_prompt("Enter column type [Dynamic/Static/No columns] <Dynamic>: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            place(ctx, *p);
        }
        return;
    case State::Height:
    case State::Rotation:
    case State::Spacing:
    case State::Width: {
        if (t.empty() && state_ != State::Width) {
            corner_prompt(ctx);
            return;
        }
        std::string num = t;
        const bool times = state_ == State::Spacing && !num.empty() && (num.back() == 'x' || num.back() == 'X');
        if (times) {
            num.pop_back();
        }
        const std::optional<double> v = state_ == State::Width ? std::optional<double>{} : parse_number(num);
        if (state_ == State::Width) {
            // A width typed, or a point that gives it from the first corner.
            if (const std::optional<double> w = parse_number(t); w && *w >= 0.0) {
                place(ctx, c1_ + core::Vec2{*w, -height_});
                width_ = *w;
                pos_ = c1_; // the first corner is where the text hangs from
                return;
            }
            if (const auto p = read_point(ctx, text)) {
                place(ctx, core::Vec2{p->x, c1_.y - height_});
                pos_ = c1_;
            }
            return;
        }
        if (!v || (state_ != State::Rotation && *v <= 0.0)) {
            ctx.echo(state_ == State::Rotation ? "Requires an angle." : "Requires a positive value.");
            return;
        }
        if (state_ == State::Height) {
            height_ = *v;
        } else if (state_ == State::Rotation) {
            rotation_ = core::to_radians(*v);
        } else {
            spacing_ = times ? *v : *v / (height_ * 5.0 / 3.0); // a distance is a multiple of the single line
        }
        corner_prompt(ctx);
        return;
    }
    case State::Justify: {
        static constexpr const char* kJ[] = {"TL", "TC", "TR", "ML", "MC", "MR", "BL", "BC", "BR"};
        if (!t.empty()) {
            int hit = -1;
            for (int i = 0; i < 9; ++i) {
                hit = u == kJ[i] ? i : hit;
            }
            if (hit < 0) {
                ctx.echo("Enter TL, TC, TR, ML, MC, MR, BL, BC or BR.");
                return;
            }
            attach_ = static_cast<std::uint8_t>(hit);
        }
        corner_prompt(ctx);
        return;
    }
    case State::SpacingType:
        if (!t.empty() && u != "A" && u != "AT LEAST" && u != "E" && u != "EXACTLY") {
            ctx.echo("Enter At least or Exactly.");
            return;
        }
        state_ = State::Spacing;
        ctx.set_prompt("Enter line spacing factor or distance <" + fmt4(spacing_) + "x>: ");
        return;
    case State::Style: {
        const std::vector<core::TextStyle> styles = ctx.text_styles();
        if (t == "?") {
            std::string line = "Text styles:";
            for (const core::TextStyle& s : styles) {
                line += " " + s.name;
            }
            ctx.echo(line);
            return;
        }
        if (!t.empty()) {
            const auto it = std::find_if(styles.begin(), styles.end(), [&](const core::TextStyle& s) {
                return upper(s.name) == u;
            });
            if (it == styles.end()) {
                ctx.echo("Cannot find text style \"" + t + "\".");
                return;
            }
            font_ = it->font;
            if (it->height > 0.0) {
                height_ = it->height;
            }
        }
        corner_prompt(ctx);
        return;
    }
    case State::Columns:
        if (!t.empty() && u != "N" && u != "NO COLUMNS") {
            ctx.echo("Columns are not available yet; the text is one column.");
        }
        corner_prompt(ctx);
        return;
    }
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
    ctx.set_prompt("Specify first leader point, or [Settings] <Settings>: ");
}

void QLeaderCommand::after_points(CommandContext& ctx) {
    ctx.clear_preview();
    if (!s_.mtext) {
        finish(ctx); // annotation None: the leader alone
        return;
    }
    if (s_.prompt_width) {
        state_ = State::Width;
        ctx.set_prompt("Specify text width <" + fmt4(width_) + ">: ");
        return;
    }
    state_ = State::FirstLine;
    ctx.set_prompt("Enter first line of annotation text <Mtext>: ");
}

void QLeaderCommand::finish(CommandContext& ctx) {
    std::string content;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        content += (i == 0 ? "" : "\n") + lines_[i];
    }
    core::MTextBlock b;
    b.pos = verts_.back(); // the text at the last point
    b.height = current_dim_text_height(ctx);
    b.width = width_;
    // Text to the right of a leader coming from the left, to the left of one from the right
    // (Always left justify: to the right whichever way it comes).
    const bool leftward = verts_.size() >= 2 && verts_.back().x < verts_[verts_.size() - 2].x;
    b.attach = (leftward && !s_.left_justify) ? 2 : 0; // TR or TL
    core::AddMLeaderCommand m{verts_, ctx.current_dim_style(), b, content, ctx.group_id()};
    if (s_.arrow != 255) {
        m.overrides.arrow_type = s_.arrow;
        m.overrides.set(core::DimOverrides::kArrowType, true);
    }
    ctx.submit(std::move(m));
    ctx.echo("Leader placed.");
    done_ = true;
}

void QLeaderCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Arrow:
        if (t.empty() || u == "S" || u == "SETTINGS") {
            QLeaderSettings io = s_;
            const int r = ctx.view() != nullptr ? ctx.view()->qleader_settings_dialog(io) : -1;
            if (r == 1) {
                s_ = io;
            } else if (r < 0) {
                ctx.echo("Leader Settings need the drawing window.");
            }
            start(ctx);
            return;
        }
        break;
    case State::Vertices:
        if (t.empty()) {
            if (verts_.size() >= 2) {
                after_points(ctx);
            }
            return;
        }
        break;
    case State::Width: {
        if (!t.empty()) {
            double v = 0.0;
            if (parse_number(t, v) && v >= 0.0) {
                width_ = v;
            } else if (const auto p = read_point(ctx, text)) {
                width_ = core::distance(*p, verts_.back());
            } else {
                return;
            }
        }
        state_ = State::FirstLine;
        ctx.set_prompt("Enter first line of annotation text <Mtext>: ");
        return;
    }
    case State::FirstLine:
    case State::NextLine:
        if (text.empty()) {
            finish(ctx); // the lines typed so far (none: the leader alone)
            return;
        }
        lines_.push_back(text);
        state_ = State::NextLine;
        ctx.set_prompt("Enter next line of annotation text: ");
        return;
    }
    auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    // Angle constraints: the first and second segments held to multiples of the angle set.
    const std::size_t seg = verts_.size(); // 1: the first segment, 2: the second
    if (seg == 1 || seg == 2) {
        static constexpr double kStep[6] = {0.0, 180.0, 90.0, 45.0, 30.0, 15.0};
        const std::uint8_t c = seg == 1 ? s_.first_angle : s_.second_angle;
        if (c > 0 && c < 6) {
            const core::Vec2 d = *p - verts_.back();
            const double len = core::length(d);
            const double step = core::to_radians(kStep[c]);
            const double a = std::round(std::atan2(d.y, d.x) / step) * step;
            p = verts_.back() + core::Vec2{std::cos(a), std::sin(a)} * len;
        }
    }
    verts_.push_back(*p);
    ctx.set_last_point(*p);
    state_ = State::Vertices;
    if (s_.max_points > 0 && static_cast<int>(verts_.size()) >= s_.max_points) {
        after_points(ctx); // as many points as the settings take
        return;
    }
    ctx.set_preview(PreviewSpec{PreviewKind::Polyline, verts_});
    ctx.set_prompt("Specify next point: ");
}

void QLeaderCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.clear_preview();
    done_ = true;
}

// ---------------------------------------------------------------------------
// TEXTEDIT / DDEDIT: pick a text entity, then type its new content.
// ---------------------------------------------------------------------------
void TextEditCommand::prompt_pick(CommandContext& ctx) {
    state_ = State::Pick;
    ctx.set_prompt("Select an annotation object or [Undo/Mode]: ");
}

void TextEditCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.echo(std::string("Current settings: Edit mode = ") + (s_single_ ? "Single" : "Multiple"));
    prompt_pick(ctx);
}

void TextEditCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Content:
        ctx.submit(core::EditTextContentCommand{at_, radius_, text, ctx.group_id()});
        ctx.echo("Text edited.");
        ++edits_;
        if (s_single_) {
            done_ = true;
            return;
        }
        (void)ctx.new_group(); // every edit its own undo step
        prompt_pick(ctx);
        return;
    case State::Mode:
        if (u == "S" || u == "SINGLE") {
            s_single_ = true;
        } else if (u == "M" || u == "MULTIPLE") {
            s_single_ = false;
        } else if (!t.empty()) {
            ctx.echo("Invalid option keyword.");
            return;
        }
        prompt_pick(ctx);
        return;
    case State::Pick:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (u == "U" || u == "UNDO") {
            if (edits_ == 0) {
                ctx.echo("Nothing to undo.");
                return;
            }
            ctx.submit(core::UndoLastGroupCommand{ctx.command_group()});
            --edits_;
            return;
        }
        if (u == "M" || u == "MODE") {
            state_ = State::Mode;
            ctx.set_prompt(std::string("Enter a text edit mode option [Single/Multiple] <") +
                           (s_single_ ? "Single" : "Multiple") + ">: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            at_ = *p;
            radius_ = ctx.pick_radius();
            state_ = State::Content;
            ctx.set_prompt("Enter new text: ");
        }
        return;
    }
}

void TextEditModeCommand::start(CommandContext& ctx) {
    ctx.set_prompt(std::string("Enter new value for TEXTEDITMODE <") + (TextEditCommand::s_single_ ? "1" : "0") +
                   ">: ");
}

void TextEditModeCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (!t.empty()) {
        if (t != "0" && t != "1") {
            ctx.echo("Requires 0 or 1.");
            return;
        }
        TextEditCommand::s_single_ = t == "1";
    }
    done_ = true;
}

void TextEditModeCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
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

void OptionsCommand::start(CommandContext& ctx) {
    if (ViewControl* v = ctx.view(); v != nullptr) {
        v->options_dialog();
    }
    done_ = true;
}

// ---------------------------------------------------------------------------
// LTSCALE: prompt for the global linetype scale factor, then apply it.
// ---------------------------------------------------------------------------
void LtscaleCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Enter new linetype scale factor <" + core::units::format_length(ctx.ltscale(), ctx.units()) + ">: ");
}

void LtscaleCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (t.empty()) {
        done_ = true; // Enter keeps the current value
        return;
    }
    double scale = 0.0;
    if (!parse_number(t, scale)) {
        ctx.echo("Requires a numeric scale.");
        return;
    }
    if (!(scale > 0.0)) {
        ctx.echo("Value must be positive.");
        return;
    }
    ctx.submit(core::SetLtscaleCommand{scale});
    ctx.echo("Regenerating model.");
    done_ = true;
}

void LtscaleCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// -GROUP / GROUPEDIT (issue #53)
// ---------------------------------------------------------------------------
void DashGroupCommand::prompt_option(CommandContext& ctx) {
    state_ = State::Option;
    ctx.set_prompt("Enter a group option [?/Order/Add/Remove/Explode/REName/Selectable/Create] <Create>: ");
}

void DashGroupCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    prompt_option(ctx);
}

void DashGroupCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void DashGroupCommand::finish_select(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        ctx.echo("Nothing selected.");
        done_ = true;
        return;
    }
    if (op_ == 'C') {
        ctx.submit(core::CreateGroupCommand{name_, text_});
    } else {
        core::GroupEditCommand cmd;
        cmd.name = name_;
        cmd.op = op_ == 'A' ? 0 : 1;
        ctx.submit(cmd);
    }
    done_ = true;
}

void DashGroupCommand::selection_gesture(CommandContext& ctx) {
    if (state_ == State::Select && select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        finish_select(ctx);
    }
}

void DashGroupCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    double v = 0.0;
    switch (state_) {
    case State::Option:
        if (u == "?") {
            ctx.submit(core::ListGroupsCommand{});
            prompt_option(ctx);
            return;
        }
        if (u.empty() || u == "C" || u == "CREATE") {
            op_ = 'C';
            state_ = State::Name;
            ctx.set_prompt("Enter a group name or [?]: ");
            return;
        }
        if (u == "O" || u == "ORDER" || u == "A" || u == "ADD" || u == "R" || u == "REMOVE" || u == "E" ||
            u == "EXPLODE" || u == "RE" || u == "REN" || u == "RENAME" || u == "S" || u == "SELECTABLE") {
            op_ = u[0] == 'R' && u.size() >= 2 && u[1] == 'E' ? 'N' : u[0];
            state_ = State::Name;
            ctx.set_prompt("Enter a group name or [?]: ");
            return;
        }
        ctx.echo("Enter ?, Order, Add, Remove, Explode, REName, Selectable or Create.");
        return;
    case State::Name:
        if (u == "?") {
            ctx.submit(core::ListGroupsCommand{});
            return;
        }
        if (t.empty() && op_ != 'C') {
            ctx.echo("A group name is needed.");
            return;
        }
        name_ = t;
        switch (op_) {
        case 'C':
            state_ = State::Description;
            ctx.set_prompt("Enter a group description: ");
            return;
        case 'A':
        case 'R':
            state_ = State::Select;
            select_.begin(ctx, op_ == 'A' ? "Select objects to add to group: " : "Select objects to remove from group: ");
            return;
        case 'E': {
            core::GroupEditCommand cmd;
            cmd.name = name_;
            cmd.op = 6;
            ctx.submit(cmd);
            done_ = true;
            return;
        }
        case 'N':
            state_ = State::NewName;
            ctx.set_prompt("Enter a new name for the group: ");
            return;
        case 'S':
            state_ = State::Selectable;
            ctx.set_prompt("This group is selectable. Do you want to change it? [Yes/No] <N>: ");
            return;
        case 'O':
            state_ = State::From;
            ctx.set_prompt("Enter object position number to reorder (0 is first): ");
            return;
        default:
            done_ = true;
            return;
        }
    case State::Description:
        text_ = t;
        state_ = State::Select;
        select_.begin(ctx);
        return;
    case State::NewName: {
        core::GroupEditCommand cmd;
        cmd.name = name_;
        cmd.op = 2;
        cmd.text = t;
        ctx.submit(cmd);
        done_ = true;
        return;
    }
    case State::Selectable: {
        core::GroupEditCommand cmd;
        cmd.name = name_;
        cmd.op = 4;
        cmd.flag = !(u == "Y" || u == "YES"); // "change it" flips the (assumed on) state
        ctx.submit(cmd);
        done_ = true;
        return;
    }
    case State::From:
        if (!parse_number(t, v) || v < 0.0) {
            ctx.echo("Enter a position number.");
            return;
        }
        from_ = static_cast<int>(v);
        state_ = State::To;
        ctx.set_prompt("Enter new position number for the object: ");
        return;
    case State::To: {
        if (!parse_number(t, v) || v < 0.0) {
            ctx.echo("Enter a position number.");
            return;
        }
        core::GroupEditCommand cmd;
        cmd.name = name_;
        cmd.op = 5;
        cmd.from = from_;
        cmd.to = static_cast<int>(v);
        ctx.submit(cmd);
        done_ = true;
        return;
    }
    case State::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            finish_select(ctx);
        }
        return;
    }
}

void GroupEditCommand::prompt_option(CommandContext& ctx) {
    state_ = State::Option;
    ctx.set_prompt("Enter an option [Add objects/Remove objects/REName]: ");
}

void GroupEditCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    cmd_ = core::GroupEditCommand{};
    state_ = State::Pick;
    ctx.set_prompt("Select group or [Name]: ");
}

void GroupEditCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void GroupEditCommand::selection_gesture(CommandContext& ctx) {
    if (state_ == State::Select && select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        input(ctx, "");
    }
}

void GroupEditCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Pick:
        if (u == "N" || u == "NAME") {
            state_ = State::Name;
            ctx.set_prompt("Enter group name or [?]: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            cmd_.by_pick = true;
            cmd_.pick = *p;
            cmd_.pick_radius = ctx.pick_radius();
            prompt_option(ctx);
        }
        return;
    case State::Name:
        if (u == "?") {
            ctx.submit(core::ListGroupsCommand{});
            return;
        }
        if (t.empty()) {
            ctx.echo("A group name is needed.");
            return;
        }
        cmd_.by_pick = false;
        cmd_.name = t;
        prompt_option(ctx);
        return;
    case State::Option:
        if (u == "A" || u == "ADD" || u == "ADD OBJECTS") {
            cmd_.op = 0;
            state_ = State::Select;
            select_.begin(ctx, "Select objects to add to group: ");
        } else if (u == "R" || u == "REMOVE" || u == "REMOVE OBJECTS") {
            cmd_.op = 1;
            state_ = State::Select;
            select_.begin(ctx, "Select objects to remove from group: ");
        } else if (u == "RE" || u == "RENAME") {
            cmd_.op = 2;
            state_ = State::NewName;
            ctx.set_prompt("Enter a new name for the group: ");
        } else {
            ctx.echo("Enter Add objects, Remove objects or REName.");
        }
        return;
    case State::NewName:
        cmd_.text = t;
        ctx.submit(cmd_);
        done_ = true;
        return;
    case State::Select:
        if (select_.active() && select_.input(ctx, text) == SelectObjectsPhase::Result::Continue) {
            return;
        }
        if (!ctx.has_selection()) {
            ctx.echo("Nothing selected.");
            done_ = true;
            return;
        }
        ctx.submit(cmd_);
        done_ = true;
        return;
    }
}

// ---------------------------------------------------------------------------
// SELECT / SELECTSIMILAR / QSELECT / FILTER / ISOLATEOBJECTS and the selection
// system variables (issue #46)
// ---------------------------------------------------------------------------
void SelectCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    select_.begin(ctx);
}

void SelectCommand::input(CommandContext& ctx, const std::string& text) {
    if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
        done_ = true; // the set stays selected for the next command
    }
}

void SelectCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        done_ = true;
    }
}

void SelectCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void SelectSimilarCommand::finish(CommandContext& ctx) {
    if (ctx.has_selection()) {
        ctx.submit(core::SelectSimilarCommand{s_mode_});
    }
    done_ = true;
}

void SelectSimilarCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (ctx.has_selection()) {
        finish(ctx);
        return;
    }
    selecting_ = true;
    select_.begin(ctx);
}

void SelectSimilarCommand::input(CommandContext& ctx, const std::string& text) {
    if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void SelectSimilarCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void SelectSimilarCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void SelectSimilarModeCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Enter new value for SELECTSIMILARMODE <" + std::to_string(SelectSimilarCommand::s_mode_) + ">: ");
}

void SelectSimilarModeCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (!t.empty()) {
        double v = 0.0;
        if (!parse_number(t, v) || v < 0.0 || v > 255.0 || v != std::floor(v)) {
            ctx.echo("Enter a bit sum from 0 to 255 (1 colour, 2 layer, 4 linetype, 8 linetype scale, "
                     "16 lineweight, 32 plot style, 64 object style, 128 name).");
            return;
        }
        SelectSimilarCommand::s_mode_ = static_cast<std::uint32_t>(v);
    }
    done_ = true;
}

void SelectSimilarModeCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void QSelectCommand::start(CommandContext& ctx) {
    if (ctx.view() == nullptr || !ctx.view()->quick_select_dialog(filter_)) {
        ctx.echo(std::string(filter_ ? "FILTER" : "QSELECT") + " needs the Quick Select dialog, which is not available here.");
    }
    done_ = true;
}

void IsolateCommand::finish(CommandContext& ctx) {
    if (mode_ == 2 || ctx.has_selection()) {
        ctx.submit(core::IsolateObjectsCommand{mode_});
    }
    done_ = true;
}

void IsolateCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (mode_ == 2 || ctx.has_selection()) {
        finish(ctx);
        return;
    }
    selecting_ = true;
    select_.begin(ctx);
}

void IsolateCommand::input(CommandContext& ctx, const std::string& text) {
    if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void IsolateCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void IsolateCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void SelectionSettingCommand::start(CommandContext& ctx) {
    const int cur = ctx.view() != nullptr ? ctx.view()->selection_setting(var_) : 0;
    ctx.set_prompt("Enter new value for " + var_ + " <" + std::to_string(cur) + ">: ");
}

void SelectionSettingCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (!t.empty()) {
        double v = 0.0;
        if (!parse_number(t, v) || v != std::floor(v)) {
            ctx.echo("Requires an integer value.");
            return;
        }
        if (ctx.view() == nullptr || !ctx.view()->set_selection_setting(var_, static_cast<int>(v))) {
            ctx.echo("Value out of range for " + var_ + ".");
            return;
        }
    }
    done_ = true;
}

void SelectionSettingCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void LtscaleVarCommand::start(CommandContext& ctx) {
    std::string cur;
    if (var_ == "CELTSCALE") {
        cur = core::units::format_length(ctx.current_celtscale(), ctx.units());
    } else if (var_ == "PSLTSCALE") {
        cur = ctx.psltscale() ? "1" : "0";
    } else {
        cur = ctx.msltscale() ? "1" : "0";
    }
    ctx.set_prompt("Enter new value for " + var_ + " <" + cur + ">: ");
}

void LtscaleVarCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (t.empty()) {
        done_ = true;
        return;
    }
    double v = 0.0;
    if (!parse_number(t, v)) {
        ctx.echo("Requires a numeric value.");
        return;
    }
    if (var_ == "CELTSCALE") {
        if (!(v > 0.0)) {
            ctx.echo("Value must be positive.");
            return;
        }
        ctx.submit(core::SetCeltscaleCommand{v});
    } else if (v != 0.0 && v != 1.0) {
        ctx.echo("Enter 0 or 1.");
        return;
    } else if (var_ == "PSLTSCALE") {
        ctx.submit(core::SetLtscaleModesCommand{v != 0.0, ctx.msltscale()});
        ctx.echo("Regenerating model.");
    } else {
        ctx.submit(core::SetLtscaleModesCommand{ctx.psltscale(), v != 0.0});
    }
    done_ = true;
}

void LtscaleVarCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// DIMSTYLE / -DIMSTYLE, and the dimension variables.
// ---------------------------------------------------------------------------
namespace {
std::string dim_value_text(std::string_view var, double v) {
    const std::string u = upper(std::string(var));
    if (u == "DIMTXT" || u == "DIMASZ" || u == "DIMEXO" || u == "DIMEXE" || u == "DIMRND" || u == "DIMLFAC" ||
        u == "DIMALTF" || u == "DIMALTRND") {
        return fmt4(v);
    }
    if (u == "DIMDSEP") {
        return std::string(1, v == 44.0 ? ',' : '.');
    }
    if (u == "DIMCLRD" || u == "DIMCLRE" || u == "DIMCLRT") {
        return v >= 256.0 ? std::string("BYLAYER") : (v < 0.0 ? std::string("(true colour)") : std::to_string(static_cast<int>(v)));
    }
    return std::to_string(static_cast<int>(v));
}
void dim_styles_list(CommandContext& ctx) {
    const std::vector<core::DimStyle> styles = ctx.dim_styles();
    std::string line = "Named dimension styles:";
    if (styles.empty()) {
        line += " Standard";
    }
    for (const core::DimStyle& st : styles) {
        line += " " + st.name;
    }
    ctx.echo(line);
}
const char* dim_var_meaning(std::string_view var) {
    const std::string u = upper(std::string(var));
    if (u == "DIMTXT") return "Text height";
    if (u == "DIMASZ") return "Arrow size";
    if (u == "DIMBLK") return "Arrowhead: 0 filled, 1 tick, 2 open, 3 dot";
    if (u == "DIMDEC") return "Decimal places";
    if (u == "DIMDSEP") return "Decimal separator";
    if (u == "DIMZIN") return "Zero suppression: 4 leading, 8 trailing";
    if (u == "DIMEXO") return "Extension line offset";
    if (u == "DIMEXE") return "Extension beyond the dimension line";
    if (u == "DIMTAD") return "Text above the dimension line (0 centred)";
    if (u == "DIMATFIT") return "Text fit: 0 automatic, 1 inside, 2 outside";
    if (u == "DIMTMOVE") return "Moved text: 0 with its dimension line, 1 with a leader, 2 alone";
    if (u == "DIMLWD") return "Dimension lineweight (1/100 mm)";
    if (u == "DIMCLRD") return "Dimension line and arrowhead colour (ACI, 256 BYLAYER)";
    if (u == "DIMCLRE") return "Extension line colour";
    if (u == "DIMLUNIT") return "Linear units: 1 scientific, 2 decimal, 3 engineering, 4 architectural, 5 fractional";
    if (u == "DIMFRAC") return "Fractions: 0 horizontal, 1 diagonal, 2 not stacked";
    if (u == "DIMRND") return "Rounding of lengths (0 none)";
    if (u == "DIMLFAC") return "Measurement scale factor";
    if (u == "DIMAUNIT") return "Angles: 0 degrees, 1 deg/min/sec, 2 grads, 3 radians";
    if (u == "DIMADEC") return "Angular decimal places (-1 as DIMDEC)";
    if (u == "DIMALT") return "Alternate units shown";
    if (u == "DIMALTU") return "Alternate units format";
    if (u == "DIMALTD") return "Alternate units decimal places";
    if (u == "DIMALTF") return "Alternate units multiplier";
    if (u == "DIMALTRND") return "Rounding of alternate units";
    return "Text colour";
}
} // namespace

void DimStyleCommand::option_prompt(CommandContext& ctx) {
    state_ = State::Option;
    ctx.set_prompt("Enter a dimension style option [Save/Restore/STatus/?] <Restore>: ");
}

void DimStyleCommand::start(CommandContext& ctx) {
    if (dialog_ && ctx.view() != nullptr && ctx.view()->dimstyle_dialog()) {
        done_ = true; // the Dimension Style Manager does the rest
        return;
    }
    ctx.echo("Current dimension style: " + current_dim_style_of(ctx).name);
    option_prompt(ctx);
}

void DimStyleCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void DimStyleCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    const std::vector<core::DimStyle> styles = ctx.dim_styles();
    const auto exists = [&](const std::string& name) {
        for (const core::DimStyle& st : styles) {
            if (st.name == name) {
                return true;
            }
        }
        return name == "Standard";
    };
    switch (state_) {
    case State::Option:
        if (u == "?") {
            dim_styles_list(ctx);
            option_prompt(ctx);
            return;
        }
        if (u.empty() || u == "R" || u == "RESTORE") {
            state_ = State::Restore;
            ctx.set_prompt("Enter a dimension style name or [?] <" + current_dim_style_of(ctx).name + ">: ");
            return;
        }
        if (u == "S" || u == "SAVE") {
            state_ = State::Save;
            ctx.set_prompt("Enter name for new dimension style or [?]: ");
            return;
        }
        if (u == "ST" || u == "STATUS") {
            const core::DimStyle cur = current_dim_style_of(ctx);
            ctx.echo("DIMSTYLE  " + cur.name + "  Current dimension style");
            for (const char* var : core::kDimVars) {
                if (const std::optional<double> v = core::dim_var_value(cur, var)) {
                    std::string row = var;
                    row.resize(10, ' ');
                    std::string val = dim_value_text(var, *v);
                    val.resize(10, ' ');
                    ctx.echo(row + val + dim_var_meaning(var));
                }
            }
            done_ = true;
            return;
        }
        ctx.echo("Enter Save, Restore, STatus or ?.");
        option_prompt(ctx);
        return;
    case State::Restore:
        if (u == "?") {
            dim_styles_list(ctx);
            return;
        }
        if (t.empty()) {
            done_ = true; // the current style stays
            return;
        }
        if (!exists(t)) {
            ctx.echo("Dimension style \"" + t + "\" not found.");
            return;
        }
        ctx.submit(core::SetCurrentDimStyleCommand{t, false});
        done_ = true;
        return;
    case State::Save:
        if (u == "?") {
            dim_styles_list(ctx);
            return;
        }
        if (t.empty()) {
            ctx.echo("Enter a name for the new dimension style.");
            return;
        }
        if (exists(t)) {
            save_name_ = t;
            state_ = State::Redefine;
            ctx.set_prompt("That name is already in use, redefine it? [Yes/No] <N>: ");
            return;
        }
        ctx.submit(core::SetCurrentDimStyleCommand{t, true});
        done_ = true;
        return;
    case State::Redefine:
        if (u == "Y" || u == "YES") {
            ctx.submit(core::SetCurrentDimStyleCommand{save_name_, true});
        } else {
            ctx.echo("Dimension style \"" + save_name_ + "\" left as it was.");
        }
        done_ = true;
        return;
    }
}

void DimVarCommand::start(CommandContext& ctx) {
    const core::DimStyle cur = current_dim_style_of(ctx);
    const std::optional<double> v = core::dim_var_value(cur, var_);
    ctx.set_prompt("Enter new value for " + var_ + " <" + (v ? dim_value_text(var_, *v) : std::string()) + ">: ");
}

void DimVarCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void DimVarCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    if (t.empty()) {
        done_ = true; // the value stays
        return;
    }
    double v = 0.0;
    if (var_ == "DIMDSEP" && t.size() == 1 && (t[0] == '.' || t[0] == ',')) {
        v = t[0] == ',' ? 44.0 : 46.0;
    } else if ((var_ == "DIMCLRD" || var_ == "DIMCLRE" || var_ == "DIMCLRT") && (u == "BYLAYER" || u == "BYBLOCK")) {
        v = 256.0;
    } else if (const std::optional<double> n = parse_number(t)) {
        v = *n;
    } else {
        ctx.echo("Enter a number.");
        return;
    }
    core::DimStyle probe = current_dim_style_of(ctx);
    if (!core::apply_dim_var(probe, var_, v)) {
        ctx.echo(std::string(dim_var_meaning(var_)) + ": that value is out of range.");
        return;
    }
    ctx.submit(core::SetDimVarCommand{var_, v});
    done_ = true;
}


// ---------------------------------------------------------------------------
// Dimension editing (issue #60): DIMEDIT / DIMTEDIT / DIMSPACE / DIMCENTER / DIMOVERRIDE
// ---------------------------------------------------------------------------
void DimEditTextCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Enter type of dimension editing [Home/New/Rotate/Oblique] <Home>: ");
}

void DimEditTextCommand::to_select(CommandContext& ctx) {
    state_ = State::Select;
    if (ctx.has_selection()) {
        finish(ctx);
        return;
    }
    select_.begin(ctx);
}

void DimEditTextCommand::finish(CommandContext& ctx) {
    if (ctx.has_selection()) {
        edit_.group = ctx.group_id();
        ctx.submit(edit_);
    }
    done_ = true;
}

void DimEditTextCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void DimEditTextCommand::input(CommandContext& ctx, const std::string& text) {
    using Op = core::DimEditCommand::Op;
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Type:
        if (t.empty() || u == "H" || u == "HOME") {
            edit_.op = Op::Home;
            to_select(ctx);
        } else if (u == "N" || u == "NEW") {
            edit_.op = Op::New;
            state_ = State::Text;
            ctx.set_prompt("Enter dimension text (<> for the measurement) <<>>: ");
        } else if (u == "R" || u == "ROTATE") {
            edit_.op = Op::Rotate;
            state_ = State::Angle;
            ctx.set_prompt("Specify angle for dimension text: ");
        } else if (u == "O" || u == "OBLIQUE") {
            edit_.op = Op::Oblique;
            state_ = State::Angle;
            ctx.set_prompt("Enter obliquing angle (press ENTER for none): ");
        } else {
            ctx.echo("Invalid option keyword.");
            start(ctx);
        }
        return;
    case State::Text:
        edit_.text = t == "<>" ? std::string() : t;
        to_select(ctx);
        return;
    case State::Angle:
        if (const std::optional<double> a = read_dim_text_angle(ctx, text)) {
            edit_.angle = *a;
            to_select(ctx);
        }
        return;
    case State::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            finish(ctx);
        }
        return;
    }
}

void DimEditTextCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void DimTextEditCommand::start(CommandContext& ctx) { ctx.set_prompt("Select dimension: "); }

void DimTextEditCommand::submit(CommandContext& ctx, core::DimEditCommand::Op op) {
    core::DimEditCommand c;
    c.op = op;
    c.pick = pick_;
    c.radius = radius_;
    c.group = ctx.group_id();
    ctx.submit(c);
    done_ = true;
}

void DimTextEditCommand::input(CommandContext& ctx, const std::string& text) {
    using Op = core::DimEditCommand::Op;
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Pick: {
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (ctx.input_is_pick() && ctx.hovered_kind() != core::EntityKind::Dimension) {
            ctx.echo("Select a dimension.");
            return;
        }
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        pick_ = *p;
        radius_ = ctx.pick_radius();
        state_ = State::Where;
        ctx.set_prompt("Specify new location for dimension text or [Left/Right/Center/Home/Angle]: ");
        return;
    }
    case State::Where:
        if (u == "L" || u == "LEFT") {
            submit(ctx, Op::Left);
        } else if (u == "R" || u == "RIGHT") {
            submit(ctx, Op::Right);
        } else if (u == "C" || u == "CENTER") {
            submit(ctx, Op::Center);
        } else if (u == "H" || u == "HOME") {
            submit(ctx, Op::Home);
        } else if (u == "A" || u == "ANGLE") {
            state_ = State::Angle;
            ctx.set_prompt("Specify angle for dimension text: ");
        } else if (t.empty()) {
            done_ = true;
        } else if (const auto p = read_point(ctx, text)) {
            core::DimEditCommand c;
            c.op = Op::Move;
            c.pick = pick_;
            c.radius = radius_;
            c.to = *p;
            c.group = ctx.group_id();
            ctx.submit(c);
            done_ = true;
        }
        return;
    case State::Angle:
        if (const std::optional<double> a = read_dim_text_angle(ctx, text)) {
            core::DimEditCommand c;
            c.op = Op::Rotate;
            c.angle = *a;
            c.pick = pick_;
            c.radius = radius_;
            c.group = ctx.group_id();
            ctx.submit(c);
            done_ = true;
        }
        return;
    }
}

void DimTextEditCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void DimSpaceCommand::start(CommandContext& ctx) { ctx.set_prompt("Select base dimension: "); }

void DimSpaceCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        state_ = State::Value;
        ctx.set_prompt("Enter value or [Auto] <Auto>: ");
    }
}

void DimSpaceCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Base: {
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (ctx.input_is_pick() && ctx.hovered_kind() != core::EntityKind::Dimension) {
            ctx.echo("Select a dimension.");
            return;
        }
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        base_ = *p;
        radius_ = ctx.pick_radius();
        state_ = State::Select;
        ctx.submit(core::ClearSelectionCommand{});
        select_.begin(ctx, "Select dimensions to space: ");
        return;
    }
    case State::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            state_ = State::Value;
            ctx.set_prompt("Enter value or [Auto] <Auto>: ");
        }
        return;
    case State::Value: {
        double offset = -1.0; // Auto
        if (!t.empty() && u != "A" && u != "AUTO") {
            const std::optional<double> v = parse_number(t);
            if (!v || *v < 0.0) {
                ctx.echo("Requires a distance of 0 or more, or Auto.");
                return;
            }
            offset = *v;
        }
        if (ctx.has_selection()) {
            core::DimArrangeCommand c;
            c.op = offset == 0.0 ? core::DimArrangeCommand::Op::Align : core::DimArrangeCommand::Op::DistributeOffset;
            c.base_pick = base_;
            c.radius = radius_;
            c.offset = offset;
            c.group = ctx.group_id();
            ctx.submit(c);
        }
        done_ = true;
        return;
    }
    }
}

void DimSpaceCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void DimCenterCommand::start(CommandContext& ctx) {
    ctx.set_prompt(s_lines_ ? "Select arc or circle or [Lines] <with center lines>: "
                            : "Select arc or circle or [Lines] <mark only>: ");
}

void DimCenterCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    if (t.empty()) {
        done_ = true;
        return;
    }
    if (u == "L" || u == "LINES") {
        s_lines_ = !s_lines_;
        ctx.echo(s_lines_ ? "Center lines on." : "Center lines off.");
        start(ctx);
        return;
    }
    if (ctx.input_is_pick() && ctx.hovered_kind() != core::EntityKind::Circle &&
        ctx.hovered_kind() != core::EntityKind::Arc) {
        ctx.echo("Select an arc or a circle.");
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        core::AddCenterMarkCommand c;
        c.pick = *p;
        c.radius = ctx.pick_radius();
        c.lines = s_lines_;
        c.group = ctx.group_id();
        ctx.submit(c);
        done_ = true;
    }
}

void DimCenterCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void DimOverrideCommand::start(CommandContext& ctx) { prompt(ctx); }

void DimOverrideCommand::prompt(CommandContext& ctx) {
    state_ = State::Name;
    ctx.set_prompt(set_.vars.empty() ? "Enter dimension variable name to override or [Clear overrides]: "
                                     : "Enter dimension variable name to override: ");
}

void DimOverrideCommand::to_select(CommandContext& ctx) {
    state_ = State::Select;
    if (ctx.has_selection()) {
        finish(ctx);
        return;
    }
    select_.begin(ctx);
}

void DimOverrideCommand::finish(CommandContext& ctx) {
    if (ctx.has_selection()) {
        set_.group = ctx.group_id();
        ctx.submit(set_);
    }
    done_ = true;
}

void DimOverrideCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        finish(ctx);
    }
}

void DimOverrideCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Name: {
        if (t.empty()) {
            if (set_.vars.empty()) {
                done_ = true;
            } else {
                to_select(ctx);
            }
            return;
        }
        if (set_.vars.empty() && (u == "C" || u == "CLEAR")) {
            set_.clear = true;
            to_select(ctx);
            return;
        }
        const auto is = [&](const auto& names) {
            return std::any_of(std::begin(names), std::end(names), [&](const char* n) { return u == n; });
        };
        if (is(core::kDimOverrideVars)) {
            var_ = u;
            const std::optional<double> v = core::dim_var_value(current_dim_style_of(ctx), var_);
            state_ = State::Value;
            ctx.set_prompt("Enter new value for dimension variable <" + (v ? dim_value_text(var_, *v) : std::string()) +
                           ">: ");
        } else if (is(core::kDimVars)) {
            ctx.echo(u + " is a style setting; a dimension cannot have its own.");
            prompt(ctx);
        } else {
            ctx.echo("Unknown dimension variable.");
            prompt(ctx);
        }
        return;
    }
    case State::Value: {
        if (!t.empty()) {
            const std::optional<double> v = parse_number(t);
            core::DimOverrides probe;
            if (!v || !core::apply_dim_override(probe, var_, *v)) {
                ctx.echo(std::string(dim_var_meaning(var_)) + ": that value is out of range" +
                         (var_.starts_with("DIMCLR") ? " (an ACI colour, 1 to 255)." : "."));
                return;
            }
            set_.vars.emplace_back(var_, *v);
        }
        prompt(ctx);
        return;
    }
    case State::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            finish(ctx);
        }
        return;
    }
}

void DimOverrideCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}


// ---------------------------------------------------------------------------
// Modify and draw tools (issues #45, #55): REVERSE / OVERKILL / COPYTOLAYER / LAYMCH /
// CHPROP / BLEND / REDO / MREDO / TRACE / SOLID / CENTERMARK / CENTERLINE / BOUNDARY
// ---------------------------------------------------------------------------
void SelectThenCommand::start(CommandContext& ctx) {
    if (ctx.has_selection()) {
        selected(ctx);
        return;
    }
    select_.begin(ctx);
}

void SelectThenCommand::selected(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        done_ = true;
        return;
    }
    if (tool_ == Tool::Reverse) {
        ctx.submit(core::ReverseSelectionCommand{ctx.group_id()});
        done_ = true;
        return;
    }
    state_ = State::Option;
    option_prompt(ctx);
}

void SelectThenCommand::option_prompt(CommandContext& ctx) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Current settings: Tolerance=%.6g, Combine partial overlap=%s, Combine end to end=%s",
                  s_overkill_.tolerance, s_overkill_.combine ? "Yes" : "No", s_overkill_.end_to_end ? "Yes" : "No");
    ctx.echo(buf);
    ctx.set_prompt("Enter an option to change [Done/tOlerance/combine parTial overlap/combine Endtoend] <done>: ");
}

void SelectThenCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        selected(ctx);
    }
}

void SelectThenCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            selected(ctx);
        }
        return;
    case State::Option:
        if (t.empty() || u == "D" || u == "DONE") {
            core::OverkillCommand c = s_overkill_;
            c.group = ctx.group_id();
            ctx.submit(c);
            done_ = true;
        } else if (u == "O" || u == "TOLERANCE") {
            state_ = State::Tolerance;
            ctx.set_prompt("Specify tolerance <" + fmt4(s_overkill_.tolerance) + ">: ");
        } else if (u == "T" || u == "PARTIAL") {
            s_overkill_.combine = !s_overkill_.combine;
            option_prompt(ctx);
        } else if (u == "E" || u == "ENDTOEND") {
            s_overkill_.end_to_end = !s_overkill_.end_to_end;
            if (s_overkill_.end_to_end) {
                s_overkill_.combine = true;
            }
            option_prompt(ctx);
        } else {
            ctx.echo("Invalid option keyword.");
            option_prompt(ctx);
        }
        return;
    case State::Tolerance:
        if (!t.empty()) {
            const std::optional<double> v = parse_number(t);
            if (!v || *v < 0.0) {
                ctx.echo("Requires a distance of 0 or more.");
                return;
            }
            s_overkill_.tolerance = *v;
        }
        state_ = State::Option;
        option_prompt(ctx);
        return;
    }
}

void SelectThenCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void LayerCopyCommand::start(CommandContext& ctx) {
    if (ctx.has_selection()) {
        to_layer(ctx);
        return;
    }
    select_.begin(ctx, copy_ ? "Select objects to copy: " : "Select objects to be changed: ");
}

void LayerCopyCommand::to_layer(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        done_ = true;
        return;
    }
    state_ = State::Layer;
    ctx.set_prompt(copy_ ? "Select object on destination layer or [Name] <Name>: "
                         : "Select object on destination layer or [Name]: ");
}

void LayerCopyCommand::to_base(CommandContext& ctx) {
    if (!copy_) {
        if (c_.pick) {
            ctx.submit(core::LayerToolCommand{core::LayerToolCommand::Op::Match, *c_.pick, c_.radius, ctx.group_id()});
        } else {
            core::ChangePropsCommand c;
            c.layer = c_.name;
            c.group = ctx.group_id();
            ctx.submit(c);
        }
        done_ = true;
        return;
    }
    state_ = State::Base;
    ctx.set_prompt("Specify base point or [Displacement/eXit] <eXit>: ");
}

void LayerCopyCommand::finish(CommandContext& ctx, core::Vec2 offset) {
    c_.offset = offset;
    c_.group = ctx.group_id();
    ctx.submit(c_);
    done_ = true;
}

void LayerCopyCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        to_layer(ctx);
    }
}

void LayerCopyCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            to_layer(ctx);
        }
        return;
    case State::Layer:
        if ((t.empty() && copy_) || u == "N" || u == "NAME") {
            state_ = State::Name;
            ctx.set_prompt("Enter layer name: ");
            return;
        }
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            c_.pick = *p;
            c_.radius = ctx.pick_radius();
            to_base(ctx);
        }
        return;
    case State::Name:
        if (t.empty()) {
            done_ = true;
            return;
        }
        c_.name = t;
        to_base(ctx);
        return;
    case State::Base:
        if (t.empty() || u == "X" || u == "EXIT") {
            finish(ctx, {});
        } else if (u == "D" || u == "DISPLACEMENT") {
            state_ = State::Displacement;
            ctx.set_prompt("Specify displacement <0,0>: ");
        } else if (const auto p = read_point(ctx, text)) {
            base_ = *p;
            state_ = State::Second;
            ctx.set_preview({PreviewKind::Move, {base_}});
            ctx.set_prompt("Specify second point of displacement or <use first point as displacement>: ");
        }
        return;
    case State::Second:
        ctx.set_preview({});
        if (t.empty()) {
            finish(ctx, base_);
        } else if (const auto p = read_point(ctx, text)) {
            finish(ctx, *p - base_);
        }
        return;
    case State::Displacement:
        if (t.empty()) {
            finish(ctx, {});
        } else if (const auto p = read_point(ctx, text)) {
            finish(ctx, *p);
        }
        return;
    }
}

void LayerCopyCommand::cancel(CommandContext& ctx) {
    ctx.set_preview({});
    ctx.echo("*Cancel*");
    done_ = true;
}

void ChPropCommand::start(CommandContext& ctx) {
    if (ctx.has_selection()) {
        property_prompt(ctx);
        return;
    }
    select_.begin(ctx);
}

void ChPropCommand::property_prompt(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        done_ = true;
        return;
    }
    state_ = State::Property;
    ctx.set_prompt("Enter property to change [Color/LAyer/LType/ltScale/LWeight]: ");
}

void ChPropCommand::finish(CommandContext& ctx) {
    if (c_.color || !c_.layer.empty() || c_.linetype || c_.celtscale > 0.0 || c_.lineweight) {
        c_.group = ctx.group_id();
        ctx.submit(c_);
    }
    done_ = true;
}

void ChPropCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        property_prompt(ctx);
    }
}

void ChPropCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            property_prompt(ctx);
        }
        return;
    case State::Property:
        if (t.empty()) {
            finish(ctx);
        } else if (u == "C" || u == "COLOR" || u == "COLOUR") {
            state_ = State::Color;
            ctx.set_prompt("Enter new color [Truecolor] <BYLAYER>: ");
        } else if (u == "LA" || u == "LAYER") {
            state_ = State::Layer;
            ctx.set_prompt("Enter new layer name: ");
        } else if (u == "LT" || u == "LTYPE") {
            state_ = State::Linetype;
            ctx.set_prompt("Enter new linetype name [Continuous/Dashed/Center/Hidden] <BYLAYER>: ");
        } else if (u == "S" || u == "LTSCALE") {
            state_ = State::Ltscale;
            ctx.set_prompt("Specify new linetype scale <1.0000>: ");
        } else if (u == "LW" || u == "LWEIGHT") {
            state_ = State::Lineweight;
            ctx.set_prompt("Enter new lineweight in mm <BYLAYER>: ");
        } else {
            ctx.echo("Invalid option keyword.");
            property_prompt(ctx);
        }
        return;
    case State::Color: {
        static constexpr const char* kNames[] = {"RED", "YELLOW", "GREEN", "CYAN", "BLUE", "MAGENTA", "WHITE"};
        if (t.empty() || u == "BYLAYER" || u == "BYBLOCK") {
            c_.color = std::make_pair(true, core::Rgb{});
            property_prompt(ctx);
            return;
        }
        if (u == "T" || u == "TRUECOLOR") {
            state_ = State::TrueColor;
            ctx.set_prompt("Red, Green, Blue: ");
            return;
        }
        long aci = 0;
        for (long i = 0; i < 7; ++i) {
            aci = u == kNames[i] ? i + 1 : aci;
        }
        if (aci == 0) {
            const std::optional<double> v = parse_number(t);
            if (v && *v >= 1.0 && *v <= 255.0 && *v == std::floor(*v)) {
                aci = static_cast<long>(*v);
            }
        }
        if (aci == 0) {
            ctx.echo("Enter a colour number (1 to 255), a colour name, BYLAYER or Truecolor.");
            return;
        }
        c_.color = std::make_pair(false, core::io::dxf_aci_to_rgb(aci));
        property_prompt(ctx);
        return;
    }
    case State::TrueColor: {
        int r = -1;
        int g = -1;
        int b = -1;
        if (std::sscanf(t.c_str(), "%d,%d,%d", &r, &g, &b) != 3 || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 ||
            b > 255) {
            ctx.echo("Enter three values from 0 to 255, separated by commas.");
            return;
        }
        c_.color = std::make_pair(false, core::Rgb{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                                                   static_cast<std::uint8_t>(b)});
        property_prompt(ctx);
        return;
    }
    case State::Layer:
        if (!t.empty()) {
            c_.layer = t;
        }
        property_prompt(ctx);
        return;
    case State::Linetype:
        if (t.empty() || u == "BYLAYER" || u == "BYBLOCK") {
            c_.linetype = std::make_pair(true, core::Linetype::Continuous);
        } else if (u == "CONTINUOUS" || u == "DASHED" || u == "CENTER" || u == "HIDDEN") {
            const core::Linetype lt = u == "DASHED"   ? core::Linetype::Dashed
                                      : u == "CENTER" ? core::Linetype::Center
                                      : u == "HIDDEN" ? core::Linetype::Hidden
                                                      : core::Linetype::Continuous;
            c_.linetype = std::make_pair(false, lt);
        } else {
            ctx.echo("Linetype " + t + " is not loaded: Continuous, Dashed, Center or Hidden.");
            return;
        }
        property_prompt(ctx);
        return;
    case State::Ltscale:
        if (!t.empty()) {
            const std::optional<double> v = parse_number(t);
            if (!v || *v <= 0.0) {
                ctx.echo("Requires a positive number.");
                return;
            }
            c_.celtscale = *v;
        }
        property_prompt(ctx);
        return;
    case State::Lineweight:
        if (t.empty() || u == "BYLAYER" || u == "BYBLOCK") {
            c_.lineweight = std::make_pair(true, std::uint8_t{0});
        } else {
            const std::optional<double> v = parse_number(t);
            if (!v || *v < 0.0 || *v > 2.11) {
                ctx.echo("Requires a lineweight from 0 to 2.11 mm, or BYLAYER.");
                return;
            }
            c_.lineweight = std::make_pair(false, static_cast<std::uint8_t>(std::lround(*v * 100.0)));
        }
        property_prompt(ctx);
        return;
    }
}

void ChPropCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void BlendCurvesCommand::start(CommandContext& ctx) {
    ctx.echo(std::string("Current setting: Continuity = ") + (s_smooth_ ? "Smooth" : "Tangent"));
    first_prompt(ctx);
}

void BlendCurvesCommand::first_prompt(CommandContext& ctx) {
    state_ = State::First;
    ctx.set_prompt("Select first object or [CONtinuity]: ");
}

void BlendCurvesCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::First:
        if (t.empty()) {
            done_ = true;
        } else if (u == "CON" || u == "CONTINUITY") {
            state_ = State::Continuity;
            ctx.set_prompt(std::string("Enter continuity [Tangent/Smooth] <") + (s_smooth_ ? "Smooth" : "Tangent") + ">: ");
        } else if (const auto p = read_point(ctx, text)) {
            first_ = *p;
            radius_ = ctx.pick_radius();
            state_ = State::Second;
            ctx.set_prompt("Select second object: ");
        }
        return;
    case State::Continuity:
        if (u == "T" || u == "TANGENT") {
            s_smooth_ = false;
        } else if (u == "S" || u == "SMOOTH") {
            s_smooth_ = true;
        } else if (!t.empty()) {
            ctx.echo("Invalid option keyword.");
            return;
        }
        first_prompt(ctx);
        return;
    case State::Second:
        if (t.empty()) {
            done_ = true;
        } else if (const auto p = read_point(ctx, text)) {
            ctx.submit(core::BlendCommand{first_, *p, std::max(radius_, ctx.pick_radius()), s_smooth_, ctx.group_id()});
            done_ = true;
        }
        return;
    }
}

void BlendCurvesCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void RedoStepsCommand::start(CommandContext& ctx) {
    if (!many_) {
        ctx.submit(core::RedoLastGroupCommand{});
        ctx.echo("Redo");
        done_ = true;
        return;
    }
    ctx.set_prompt("Enter number of actions or [All/Last]: ");
}

void RedoStepsCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    if (t.empty()) {
        done_ = true;
        return;
    }
    std::uint32_t n = 0;
    if (u == "A" || u == "ALL") {
        n = 0xFFFFFFFFu;
    } else if (u == "L" || u == "LAST") {
        n = 1;
    } else if (const std::optional<double> v = parse_number(t); v && *v >= 1.0 && *v == std::floor(*v)) {
        n = *v > 1e9 ? 0xFFFFFFFFu : static_cast<std::uint32_t>(*v);
    } else {
        ctx.echo("Requires a positive whole number, All or Last.");
        return;
    }
    ctx.submit(core::RedoLastGroupCommand{n});
    done_ = true;
}

void RedoStepsCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void TraceSolidCommand::start(CommandContext& ctx) { prompt(ctx); }

void TraceSolidCommand::prompt(CommandContext& ctx) {
    if (!solid_) {
        if (!width_asked_) {
            ctx.set_prompt("Specify trace width <" + fmt4(s_width_) + ">: ");
        } else {
            ctx.set_prompt(pts_.empty() ? "Specify start point: " : "Specify next point: ");
        }
        return;
    }
    static constexpr const char* kAsk[] = {"Specify first point: ", "Specify second point: ",
                                           "Specify third point: ", "Specify fourth point or <exit>: "};
    ctx.set_prompt(kAsk[std::min<std::size_t>(pts_.size(), 3)]);
}

void TraceSolidCommand::finish_trace(CommandContext& ctx) {
    ctx.set_preview({});
    if (pts_.size() >= 2) {
        core::AddPolylineCommand p;
        p.points = pts_;
        p.widths = {s_width_, s_width_};
        p.group = ctx.group_id();
        ctx.submit(p);
    }
    done_ = true;
}

void TraceSolidCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (!solid_ && !width_asked_) {
        if (!t.empty()) {
            const std::optional<double> v = parse_number(t);
            if (!v || *v < 0.0) {
                ctx.echo("Requires a width of 0 or more.");
                return;
            }
            s_width_ = *v;
        }
        width_asked_ = true;
        prompt(ctx);
        return;
    }
    if (t.empty()) {
        if (!solid_) {
            finish_trace(ctx);
            return;
        }
        if (pts_.size() == 3) { // a triangle
            ctx.submit(core::AddHatchCommand{{pts_}, "SOLID", 1.0, 0.0, {}, ctx.new_group()});
        }
        ctx.set_preview({});
        done_ = true;
        return;
    }
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    pts_.push_back(*p);
    ctx.set_last_point(*p);
    if (solid_ && pts_.size() == 4) {
        // 1, 2, 4, 3 round the edge: the third and fourth points are across from the first two.
        ctx.submit(core::AddHatchCommand{{{pts_[0], pts_[1], pts_[3], pts_[2]}}, "SOLID", 1.0, 0.0, {}, ctx.new_group()});
        pts_ = {pts_[2], pts_[3]};
    }
    ctx.set_preview({PreviewKind::Polyline, pts_});
    prompt(ctx);
}

void TraceSolidCommand::cancel(CommandContext& ctx) {
    ctx.set_preview({});
    ctx.echo("*Cancel*");
    done_ = true;
}

void CenterBoundaryCommand::start(CommandContext& ctx) {
    switch (tool_) {
    case Tool::CenterMark:
        ctx.set_prompt("Select circle or arc to add centermark: ");
        return;
    case Tool::CenterLine:
        ctx.set_prompt(first_ ? "Select second line: " : "Select first line: ");
        return;
    case Tool::Boundary:
        ctx.set_prompt("Pick internal point: ");
        return;
    }
}

void CenterBoundaryCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (t.empty()) {
        done_ = true;
        return;
    }
    switch (tool_) {
    case Tool::CenterMark:
        if (ctx.input_is_pick() && ctx.hovered_kind() != core::EntityKind::Circle &&
            ctx.hovered_kind() != core::EntityKind::Arc) {
            ctx.echo("Select a circle or an arc.");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            core::AddCenterMarkCommand c;
            c.pick = *p;
            c.radius = ctx.pick_radius();
            c.lines = true;
            c.center_linetype = true;
            c.group = made_ > 0 ? ctx.new_group() : ctx.group_id();
            ctx.submit(c);
            ++made_;
            start(ctx);
        }
        return;
    case Tool::CenterLine:
        if (ctx.input_is_pick() && ctx.hovered_kind() != core::EntityKind::Line) {
            ctx.echo("Select a line.");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            if (!first_) {
                first_ = *p;
                radius_ = ctx.pick_radius();
                start(ctx);
                return;
            }
            core::CenterlineCommand c;
            c.pick1 = *first_;
            c.pick2 = *p;
            c.radius = std::max(radius_, ctx.pick_radius());
            c.group = ctx.group_id();
            ctx.submit(c);
            done_ = true;
        }
        return;
    case Tool::Boundary:
        if (const auto p = read_point(ctx, text)) {
            ctx.submit(core::BoundaryCommand{*p, true, made_ > 0 ? ctx.new_group() : ctx.group_id()});
            ++made_;
            start(ctx);
        }
        return;
    }
}

void CenterBoundaryCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}


// ---------------------------------------------------------------------------
// LAYER / -LAYER (issue #69), ORTHO / SNAP / GRID (issue #65)
// ---------------------------------------------------------------------------
void LayerCommand::start(CommandContext& ctx) {
    if (dialog_ && ctx.view() != nullptr && ctx.view()->layer_dialog()) {
        done_ = true; // the Layer Properties Manager does the rest
        return;
    }
    option_prompt(ctx);
}

void LayerCommand::option_prompt(CommandContext& ctx) {
    state_ = State::Option;
    edit_ = {};
    value_text_.clear();
    ctx.set_prompt(
        "Enter an option "
        "[?/Make/Set/New/Rename/ON/OFF/Color/Ltype/LWeight/TRansparency/MATerial/Plot/Freeze/Thaw/LOck/Unlock/stAte/"
        "Description/rEconcile/Xref]: ");
}

void LayerCommand::names_prompt(CommandContext& ctx) {
    using Op = core::LayerEditCommand::Op;
    state_ = State::Names;
    switch (edit_.op) {
    case Op::List:
        ctx.set_prompt("Enter layer name(s) to list <*>: ");
        return;
    case Op::Make:
        ctx.set_prompt("Enter name for new layer (becomes the current layer): ");
        return;
    case Op::Set:
        ctx.set_prompt("Enter layer name to make current or <select object>: ");
        return;
    case Op::New:
        ctx.set_prompt("Enter name list for new layer(s): ");
        return;
    case Op::Rename:
        ctx.set_prompt("Enter old layer name: ");
        return;
    case Op::On:
        ctx.set_prompt("Enter name list of layer(s) to turn On: ");
        return;
    case Op::Off:
        ctx.set_prompt("Enter name list of layer(s) to turn off <current>: ");
        return;
    case Op::Freeze:
        ctx.set_prompt("Enter name list of layer(s) to freeze: ");
        return;
    case Op::Thaw:
        ctx.set_prompt("Enter name list of layer(s) to thaw: ");
        return;
    case Op::Lock:
        ctx.set_prompt("Enter name list of layer(s) to lock <current>: ");
        return;
    case Op::Unlock:
        ctx.set_prompt("Enter name list of layer(s) to unlock <current>: ");
        return;
    case Op::Color:
    case Op::Ltype:
    case Op::LWeight:
    case Op::Transparency:
        ctx.set_prompt("Enter name list of layer(s) for " + value_text_ + " <current>: ");
        return;
    case Op::Plot:
        ctx.set_prompt("Enter layer name(s) for this plot preference <current>: ");
        return;
    case Op::Description:
        ctx.set_prompt("Enter name list of layer(s) to apply the description to <current>: ");
        return;
    }
}

void LayerCommand::input(CommandContext& ctx, const std::string& text) {
    using Op = core::LayerEditCommand::Op;
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (state_) {
    case State::Option: {
        if (t.empty()) {
            done_ = true;
            return;
        }
        struct Kw {
            const char* full;
            const char* abbr;
            Op op;
        };
        static constexpr Kw kKeys[] = {{"?", "?", Op::List},         {"MAKE", "M", Op::Make},
                                       {"SET", "S", Op::Set},         {"NEW", "N", Op::New},
                                       {"RENAME", "R", Op::Rename},   {"ON", "ON", Op::On},
                                       {"OFF", "OFF", Op::Off},       {"COLOR", "C", Op::Color},
                                       {"LTYPE", "L", Op::Ltype},     {"LWEIGHT", "LW", Op::LWeight},
                                       {"FREEZE", "F", Op::Freeze},   {"THAW", "T", Op::Thaw},
                                       {"LOCK", "LO", Op::Lock},      {"UNLOCK", "U", Op::Unlock},
                                       {"PLOT", "P", Op::Plot},       {"DESCRIPTION", "D", Op::Description},
                                       {"TRANSPARENCY", "TR", Op::Transparency}};
        if (u == "MAT" || u == "MATERIAL" || u == "E" || u == "RECONCILE" || u == "X" || u == "XREF") {
            ctx.echo(u.front() == 'M' ? "Layers carry no materials here: drawings are 2D."
                     : u.front() == 'E' ? "Every layer counts as reconciled: new layers are not tracked."
                                         : "Xref layers are set through the xref, not here yet.");
            option_prompt(ctx);
            return;
        }
        if (u == "A" || u == "STATE") {
            ctx.echo("Layer states: -LAYERSTATE on the command line, LAYERSTATE for the Layer States Manager.");
            option_prompt(ctx);
            return;
        }
        const Kw* hit = nullptr;
        for (const Kw& k : kKeys) {
            if (u == k.full || u == k.abbr) {
                hit = &k;
            }
        }
        if (hit == nullptr) {
            ctx.echo("Invalid option keyword.");
            option_prompt(ctx);
            return;
        }
        edit_.op = hit->op;
        if (hit->op == Op::Color) {
            state_ = State::Value;
            ctx.set_prompt("New color [Truecolor]: ");
        } else if (hit->op == Op::Ltype) {
            state_ = State::Value;
            ctx.set_prompt("Enter a loaded linetype name [Continuous/Dashed/Center/Hidden] <Continuous>: ");
        } else if (hit->op == Op::LWeight) {
            state_ = State::Value;
            ctx.set_prompt("Enter lineweight (0.0mm - 2.11mm): ");
        } else if (hit->op == Op::Plot) {
            state_ = State::Value;
            ctx.set_prompt("Enter a plotting preference [Plot/No plot] <Plot>: ");
        } else if (hit->op == Op::Description) {
            state_ = State::Value;
            ctx.set_prompt("Enter layer description: ");
        } else if (hit->op == Op::Transparency) {
            state_ = State::Value;
            ctx.set_prompt("Enter transparency value (0-90): ");
        } else {
            names_prompt(ctx);
        }
        return;
    }
    case State::Value: {
        if (edit_.op == Op::Color) {
            static constexpr const char* kNames[] = {"RED", "YELLOW", "GREEN", "CYAN", "BLUE", "MAGENTA", "WHITE"};
            long aci = 0;
            for (long i = 0; i < 7; ++i) {
                aci = u == kNames[i] ? i + 1 : aci;
            }
            int r = -1;
            int g = -1;
            int b = -1;
            if (aci == 0) {
                if (const std::optional<double> v = parse_number(t); v && *v >= 1.0 && *v <= 255.0 && *v == std::floor(*v)) {
                    aci = static_cast<long>(*v);
                } else if (std::sscanf(t.c_str(), "%d,%d,%d", &r, &g, &b) == 3 && r >= 0 && r <= 255 && g >= 0 &&
                           g <= 255 && b >= 0 && b <= 255) {
                    edit_.color = core::Rgb{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                                            static_cast<std::uint8_t>(b)};
                    value_text_ = "color " + t;
                    names_prompt(ctx);
                    return;
                } else {
                    ctx.echo("Enter a colour number (1 to 255), a colour name, or red,green,blue.");
                    return;
                }
            }
            edit_.color = core::io::dxf_aci_to_rgb(aci);
            value_text_ = "color " + std::to_string(aci);
        } else if (edit_.op == Op::Ltype) {
            const std::string n = t.empty() ? std::string("CONTINUOUS") : u;
            if (n != "CONTINUOUS" && n != "DASHED" && n != "CENTER" && n != "HIDDEN") {
                ctx.echo("Linetype " + t + " is not loaded: Continuous, Dashed, Center or Hidden.");
                return;
            }
            edit_.linetype = n == "DASHED" ? core::Linetype::Dashed
                             : n == "CENTER" ? core::Linetype::Center
                             : n == "HIDDEN" ? core::Linetype::Hidden
                                             : core::Linetype::Continuous;
            value_text_ = "linetype \"" + n + "\"";
        } else if (edit_.op == Op::Plot) {
            if (t.empty() || u == "P" || u == "PLOT") {
                edit_.plot = true;
            } else if (u == "N" || u == "NO" || u == "NO PLOT" || u == "NOPLOT") {
                edit_.plot = false;
            } else {
                ctx.echo("Enter Plot or No plot.");
                return;
            }
        } else if (edit_.op == Op::Description) {
            edit_.description = trimmed(text); // as typed, capitals and all
        } else if (edit_.op == Op::Transparency) {
            const std::optional<double> v = parse_number(t);
            if (!v || *v < 0.0 || *v > 90.0 || *v != std::floor(*v)) {
                ctx.echo("Requires a whole number from 0 to 90.");
                return;
            }
            edit_.transparency = static_cast<std::uint8_t>(*v);
            value_text_ = "transparency " + std::to_string(edit_.transparency) + "%";
        } else {
            const std::optional<double> v = parse_number(t);
            if (!v || *v < 0.0 || *v > 2.11) {
                ctx.echo("Requires a lineweight from 0 to 2.11 mm.");
                return;
            }
            edit_.lineweight = static_cast<std::uint8_t>(std::lround(*v * 100.0));
            char buf[32];
            std::snprintf(buf, sizeof(buf), "lineweight %.2fmm", *v);
            value_text_ = buf;
        }
        names_prompt(ctx);
        return;
    }
    case State::Names:
        if (edit_.op == Op::Set && t.empty()) {
            state_ = State::SetPick;
            ctx.set_prompt("Select object: ");
            return;
        }
        if (t.empty() && (edit_.op == Op::Make || edit_.op == Op::New || edit_.op == Op::Rename ||
                          edit_.op == Op::On || edit_.op == Op::Freeze || edit_.op == Op::Thaw)) {
            option_prompt(ctx); // nothing named: back to the options
            return;
        }
        edit_.names = t;
        if (edit_.op == Op::Rename) {
            state_ = State::RenameTo;
            ctx.set_prompt("Enter new layer name: ");
            return;
        }
        ctx.submit(edit_);
        option_prompt(ctx);
        return;
    case State::RenameTo:
        if (!t.empty()) {
            edit_.to = t;
            ctx.submit(edit_);
        }
        option_prompt(ctx);
        return;
    case State::SetPick:
        if (const auto p = read_point(ctx, text)) {
            ctx.submit(core::LayerToolCommand{core::LayerToolCommand::Op::MakeCurrent, *p, ctx.pick_radius(), 0});
        }
        option_prompt(ctx);
        return;
    }
}

void LayerCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void DraftModeCommand::start(CommandContext& ctx) {
    switch (mode_) {
    case Mode::Ortho:
        ctx.set_prompt(std::string("Enter mode [ON/OFF] <") + (ctx.ortho_mode() ? "ON" : "OFF") + ">: ");
        return;
    case Mode::Snap:
        ctx.set_prompt("Specify snap spacing or [ON/OFF/Aspect/Legacy/Style/Type] <" + fmt4(ctx.snap_spacing()) + ">: ");
        return;
    case Mode::Grid:
        ctx.set_prompt("Specify grid spacing(X) or [ON/OFF/Snap/Major/aDaptive/Limits/Follow/Aspect] <" +
                       fmt4(ctx.grid_settings().x) + ">: ");
        return;
    }
}

void DraftModeCommand::grid_set(CommandContext& ctx, const core::GridSettings& g) {
    ctx.set_grid_settings(g);
    if (ctx.view() != nullptr) {
        ctx.view()->set_grid_mode(true);
    }
    done_ = true;
}

void DraftModeCommand::main_prompt(CommandContext& ctx) {
    step_ = Step::Main;
    start(ctx);
}

void DraftModeCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::AspectX:
    case Step::AspectY: {
        if (mode_ == Mode::Grid) {
            core::GridSettings g = ctx.grid_settings();
            double v = step_ == Step::AspectX ? g.x : g.y;
            if (!t.empty()) {
                const std::optional<double> n = parse_number(t);
                if (!n || *n < 0.0) {
                    ctx.echo("Requires a spacing of zero or more.");
                    return;
                }
                v = *n;
            }
            if (step_ == Step::AspectX) {
                x_ = v;
                step_ = Step::AspectY;
                ctx.set_prompt("Specify the vertical spacing(Y) <" + fmt4(g.y) + ">: ");
                return;
            }
            g.x = x_;
            g.y = v;
            grid_set(ctx, g);
            return;
        }
        double v = step_ == Step::AspectX ? ctx.snap_spacing() : ctx.snap_spacing_y();
        if (!t.empty()) {
            const std::optional<double> n = parse_number(t);
            if (!n || *n <= 0.0) {
                ctx.echo("Requires a positive spacing.");
                return;
            }
            v = *n;
        }
        if (step_ == Step::AspectX) {
            x_ = v;
            step_ = Step::AspectY;
            ctx.set_prompt("Specify vertical spacing <" + fmt4(ctx.snap_spacing_y()) + ">: ");
            return;
        }
        ctx.set_snap_spacing_xy(x_, v);
        ctx.set_snap_mode(true);
        done_ = true;
        return;
    }
    case Step::Type:
        if (u == "P" || u == "POLAR") {
            ctx.set_snap_type(1);
        } else if (u == "G" || u == "GRID" || t.empty()) {
            ctx.set_snap_type(0);
        } else {
            ctx.echo("Enter Polar or Grid.");
            return;
        }
        done_ = true;
        return;
    case Step::Style: {
        core::GridSettings g = ctx.grid_settings();
        if (u == "I" || u == "ISOMETRIC") {
            step_ = Step::IsoSpacing;
            ctx.set_prompt("Specify vertical spacing <" + fmt4(ctx.snap_spacing_y()) + ">: ");
            return;
        }
        if (!t.empty() && u != "S" && u != "STANDARD") {
            ctx.echo("Enter Standard or Isometric.");
            return;
        }
        g.isometric = false;
        ctx.set_grid_settings(g);
        done_ = true;
        return;
    }
    case Step::IsoSpacing: {
        double v = ctx.snap_spacing_y();
        if (!t.empty()) {
            const std::optional<double> n = parse_number(t);
            if (!n || *n <= 0.0) {
                ctx.echo("Requires a positive spacing.");
                return;
            }
            v = *n;
        }
        core::GridSettings g = ctx.grid_settings();
        g.isometric = true;
        ctx.set_grid_settings(g);
        ctx.set_snap_spacing_xy(v, v);
        ctx.set_snap_mode(true);
        done_ = true;
        return;
    }
    case Step::Legacy:
        if (u == "Y" || u == "YES") {
            ctx.echo("Legacy snap behaviour is not available; the cursor snaps only at a point prompt.");
        }
        done_ = true;
        return;
    case Step::Major: {
        core::GridSettings g = ctx.grid_settings();
        if (!t.empty()) {
            const std::optional<double> n = parse_number(t);
            if (!n || *n < 1.0 || *n > 100.0 || *n != std::floor(*n)) {
                ctx.echo("Requires a whole number from 1 to 100.");
                return;
            }
            g.major = static_cast<int>(*n);
        }
        grid_set(ctx, g);
        return;
    }
    case Step::Adaptive:
    case Step::Subdivide:
    case Step::Limits:
    case Step::Follow: {
        const bool yes = u == "Y" || u == "YES";
        if (!t.empty() && !yes && u != "N" && u != "NO") {
            ctx.echo("Enter Yes or No.");
            return;
        }
        core::GridSettings g = ctx.grid_settings();
        if (step_ == Step::Adaptive) {
            g.adaptive = t.empty() ? g.adaptive : yes;
            if (g.adaptive) {
                ctx.set_grid_settings(g);
                step_ = Step::Subdivide;
                ctx.set_prompt(std::string("Allow subdivision below grid spacing [Yes/No] <") +
                               (g.subdivide ? "Yes" : "No") + ">: ");
                return;
            }
        } else if (step_ == Step::Subdivide) {
            g.subdivide = t.empty() ? g.subdivide : yes;
        } else if (step_ == Step::Limits) {
            g.beyond_limits = t.empty() ? g.beyond_limits : yes;
        } else if (yes) {
            ctx.echo("There is no dynamic UCS here; the grid stays on the world's axes.");
        }
        grid_set(ctx, g);
        return;
    }
    case Step::Main:
        break;
    }
    if (t.empty()) {
        done_ = true;
        return;
    }
    if (mode_ == Mode::Grid) {
        core::GridSettings g = ctx.grid_settings();
        if (u == "S" || u == "SNAP") {
            g.x = ctx.snap_spacing(); // the snap spacing, as it is now
            g.y = ctx.snap_spacing_y();
            grid_set(ctx, g);
            return;
        }
        if (u == "M" || u == "MAJOR") {
            step_ = Step::Major;
            ctx.set_prompt("Specify the number of grid divisions per major grid line <" + std::to_string(g.major) +
                           ">: ");
            return;
        }
        if (u == "D" || u == "ADAPTIVE") {
            step_ = Step::Adaptive;
            ctx.set_prompt(std::string("Turn adaptive behavior on [Yes/No] <") + (g.adaptive ? "Yes" : "No") + ">: ");
            return;
        }
        if (u == "L" || u == "LIMITS") {
            step_ = Step::Limits;
            ctx.set_prompt(std::string("Display grid beyond Limits [Yes/No] <") + (g.beyond_limits ? "Yes" : "No") +
                           ">: ");
            return;
        }
        if (u == "F" || u == "FOLLOW") {
            step_ = Step::Follow;
            ctx.set_prompt("Follow Dynamic UCS [Yes/No] <No>: ");
            return;
        }
        if (u == "A" || u == "ASPECT") {
            step_ = Step::AspectX;
            ctx.set_prompt("Specify the horizontal spacing(X) <" + fmt4(g.x) + ">: ");
            return;
        }
        if (u != "ON" && u != "OFF") {
            // A spacing, or a multiple of the snap spacing ("2x"); 0 follows the zoom.
            std::string num = t;
            const bool times = !num.empty() && (num.back() == 'x' || num.back() == 'X');
            if (times) {
                num.pop_back();
            }
            const std::optional<double> v = parse_number(num);
            if (!v || *v < 0.0) {
                ctx.echo("Requires a spacing of zero or more, ON, OFF or an option.");
                return;
            }
            g.x = times ? *v * ctx.snap_spacing() : *v;
            g.y = times ? *v * ctx.snap_spacing_y() : *v;
            grid_set(ctx, g);
            return;
        }
    }
    if (mode_ == Mode::Snap) {
        if (u == "A" || u == "ASPECT") {
            step_ = Step::AspectX;
            ctx.set_prompt("Specify horizontal spacing <" + fmt4(ctx.snap_spacing()) + ">: ");
            return;
        }
        if (u == "T" || u == "TYPE") {
            step_ = Step::Type;
            ctx.set_prompt(std::string("Enter snap type [Polar/Grid] <") + (ctx.snap_type() == 1 ? "Polar" : "Grid") +
                           ">: ");
            return;
        }
        if (u == "S" || u == "STYLE") {
            step_ = Step::Style;
            ctx.set_prompt("Enter snap grid style [Standard/Isometric] <S>: ");
            return;
        }
        if (u == "L" || u == "LEGACY") {
            step_ = Step::Legacy;
            ctx.set_prompt("Enter new value for legacy snap behavior [Yes/No] <No>: ");
            return;
        }
    }
    const bool on = u == "ON";
    if (!on && u != "OFF") {
        if (mode_ == Mode::Snap) {
            const std::optional<double> v = parse_number(t);
            if (!v || *v <= 0.0) {
                ctx.echo("Requires a positive spacing, ON, OFF or an option.");
                return;
            }
            ctx.set_snap_spacing(*v);
            ctx.set_snap_mode(true); // a new spacing turns snap on, as AutoCAD does
            done_ = true;
            return;
        }
        ctx.echo("Enter ON or OFF.");
        return;
    }
    switch (mode_) {
    case Mode::Ortho:
        ctx.set_ortho_mode(on);
        break;
    case Mode::Snap:
        ctx.set_snap_mode(on);
        break;
    case Mode::Grid:
        if (ctx.view() != nullptr) {
            ctx.view()->set_grid_mode(on);
        }
        break;
    }
    done_ = true;
}

void DraftModeCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}


// ---------------------------------------------------------------------------
// QDIM (issue #60)
// ---------------------------------------------------------------------------
void QuickDimUiCommand::start(CommandContext& ctx) {
    if (ctx.has_selection()) {
        position_prompt(ctx);
        return;
    }
    select_.begin(ctx, "Select geometry to dimension: ");
}

void QuickDimUiCommand::position_prompt(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        done_ = true;
        return;
    }
    positioning_ = true;
    static constexpr const char* kNames[] = {"Continuous", "Staggered", "Baseline", "Ordinate", "Radius", "Diameter"};
    ctx.set_prompt(std::string("Specify dimension line position, or [Continuous/Staggered/Baseline/Ordinate/Radius/"
                               "Diameter] <") +
                   kNames[static_cast<std::size_t>(s_mode_)] + ">: ");
}

void QuickDimUiCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        position_prompt(ctx);
    }
}

void QuickDimUiCommand::input(CommandContext& ctx, const std::string& text) {
    using Mode = core::QuickDimCommand::Mode;
    if (!positioning_) {
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            position_prompt(ctx);
        }
        return;
    }
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    if (t.empty()) {
        done_ = true;
        return;
    }
    const std::pair<const char*, Mode> keys[] = {{"CONTINUOUS", Mode::Continuous}, {"STAGGERED", Mode::Staggered},
                                                 {"BASELINE", Mode::Baseline},     {"ORDINATE", Mode::Ordinate},
                                                 {"RADIUS", Mode::Radius},         {"DIAMETER", Mode::Diameter}};
    for (const auto& [word, mode] : keys) {
        if (u == word || (u.size() == 1 && u[0] == word[0])) {
            s_mode_ = mode;
            position_prompt(ctx);
            return;
        }
    }
    if (const auto p = read_point(ctx, text)) {
        core::QuickDimCommand c;
        c.mode = s_mode_;
        c.at = *p;
        c.group = ctx.group_id();
        ctx.submit(c);
        done_ = true;
    }
}

void QuickDimUiCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}


// ---------------------------------------------------------------------------
// MULTIPLE, PDMODE / PDSIZE / PTYPE, LIMITS (issues #40, #65)
// ---------------------------------------------------------------------------
void MultipleCommand::start(CommandContext& ctx) { ctx.set_prompt("Enter command name to repeat: "); }

void BreaklineCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.echo("Block= BRKLINE.DWG, Size= " + fmt4(s_size_) + ", Extension= " + fmt4(s_extension_));
    first_prompt(ctx);
}

void BreaklineCommand::first_prompt(CommandContext& ctx) {
    step_ = Step::First;
    ctx.set_prompt("Specify first point for breakline or [Block/Size/Extension]: ");
}

void BreaklineCommand::make(CommandContext& ctx, core::Vec2 at) {
    const core::Vec2 d = p2_ - p1_;
    const double len = core::length(d);
    const core::Vec2 u = d / len;
    const core::Vec2 n{-u.y, u.x};
    // The symbol's centre on the line, kept clear of its ends.
    const double t = std::clamp(core::dot(at - p1_, u), s_size_ * 0.5, std::max(s_size_ * 0.5, len - s_size_ * 0.5));
    const core::Vec2 c = p1_ + u * t;
    const double s = s_size_;
    const std::vector<core::Vec2> pts{p1_ - u * s_extension_,
                                      c - u * (s * 0.5),
                                      c - u * (s / 6.0) + n * (s * 0.75),
                                      c + u * (s / 6.0) - n * (s * 0.75),
                                      c + u * (s * 0.5),
                                      p2_ + u * s_extension_};
    ctx.clear_preview();
    ctx.submit(core::AddPolylineCommand{pts, false, ctx.group_id()});
    done_ = true;
}

void BreaklineCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::First:
        if (u == "B" || u == "BLOCK") {
            ctx.echo("Only the standard break symbol is drawn here.");
            return;
        }
        if (u == "S" || u == "SIZE") {
            step_ = Step::Size;
            ctx.set_prompt("Breakline symbol size <" + fmt4(s_size_) + ">: ");
            return;
        }
        if (u == "E" || u == "EXTENSION") {
            step_ = Step::Extension;
            ctx.set_prompt("Breakline extension distance <" + fmt4(s_extension_) + ">: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            p1_ = *p;
            ctx.set_last_point(*p);
            step_ = Step::Second;
            ctx.set_preview(PreviewSpec{PreviewKind::Segment, {*p}});
            ctx.set_prompt("Specify second point for breakline: ");
        }
        return;
    case Step::Second:
        if (const auto p = read_point(ctx, text)) {
            if (core::distance(*p, p1_) <= s_size_) {
                ctx.echo("The two points must be further apart than the break symbol.");
                return;
            }
            p2_ = *p;
            ctx.set_last_point(*p);
            step_ = Step::Location;
            ctx.clear_preview();
            ctx.set_prompt("Specify location for break symbol <Midpoint>: ");
        }
        return;
    case Step::Location:
        if (t.empty()) {
            make(ctx, (p1_ + p2_) * 0.5);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            make(ctx, *p);
        }
        return;
    case Step::Size:
    case Step::Extension: {
        if (!t.empty()) {
            const std::optional<double> v = parse_number(t);
            if (!v || *v < 0.0 || (step_ == Step::Size && *v == 0.0)) {
                ctx.echo(step_ == Step::Size ? "Requires a positive size." : "Requires a distance of zero or more.");
                return;
            }
            (step_ == Step::Size ? s_size_ : s_extension_) = *v;
        }
        first_prompt(ctx);
        return;
    }
    }
}

void BreaklineCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.clear_preview();
    done_ = true;
}

// ---------------------------------------------------------------------------
// FIND (issue #62); TCASE and TXT2MTXT (issue #77)
// ---------------------------------------------------------------------------
void FindCommand::start(CommandContext& ctx) {
    if (ctx.view() != nullptr && ctx.view()->find_dialog()) {
        done_ = true; // the Find and Replace window does the rest
        return;
    }
    ctx.set_prompt("Enter the text to find: ");
}

void FindCommand::submit(CommandContext& ctx) {
    find_.group = ctx.group_id();
    ctx.submit(find_);
    done_ = true;
}

void FindCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    const auto yes_no = [&](bool& into) {
        if (u.empty() || u == "N" || u == "NO") {
            into = false;
        } else if (u == "Y" || u == "YES") {
            into = true;
        } else {
            ctx.echo("Enter Yes or No.");
            return false;
        }
        return true;
    };
    switch (step_) {
    case Step::What:
        if (text.empty()) {
            done_ = true;
            return;
        }
        find_.find = text;
        step_ = Step::With;
        ctx.set_prompt("Enter the replacement text, or Enter to find only: ");
        return;
    case Step::With:
        find_.replace = text;
        find_.do_replace = !text.empty();
        step_ = Step::Case;
        ctx.set_prompt("Match case [Yes/No] <No>: ");
        return;
    case Step::Case:
        if (yes_no(find_.match_case)) {
            step_ = Step::Whole;
            ctx.set_prompt("Find whole words only [Yes/No] <No>: ");
        }
        return;
    case Step::Whole:
        if (!yes_no(find_.whole_word)) {
            return;
        }
        if (ctx.has_selection()) {
            step_ = Step::Where;
            ctx.set_prompt("Find where [Selection/Entire drawing] <Entire drawing>: ");
            return;
        }
        submit(ctx);
        return;
    case Step::Where:
        if (u == "S" || u == "SELECTION") {
            find_.selection_only = true;
        } else if (!u.empty() && u != "E" && u != "ENTIRE DRAWING" && u != "ENTIRE") {
            ctx.echo("Enter Selection or Entire drawing.");
            return;
        }
        submit(ctx);
        return;
    }
}

void FindCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void TextCaseCommand::start(CommandContext& ctx) {
    if (ctx.has_selection()) {
        after_selection(ctx);
        return;
    }
    selecting_ = true;
    select_.begin(ctx);
}

void TextCaseCommand::after_selection(CommandContext& ctx) {
    selecting_ = false;
    if (!ctx.has_selection()) {
        done_ = true;
        return;
    }
    ctx.set_prompt("Enter case [Sentence/Lower/Upper/Title/tOggle] <Upper>: ");
}

void TextCaseCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        after_selection(ctx);
    }
}

void TextCaseCommand::input(CommandContext& ctx, const std::string& text) {
    if (selecting_) {
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            after_selection(ctx);
        }
        return;
    }
    using M = core::TextCaseCommand::Mode;
    const std::string u = upper(trimmed(text));
    M mode = M::Upper;
    if (u == "S" || u == "SENTENCE") {
        mode = M::Sentence;
    } else if (u == "L" || u == "LOWER") {
        mode = M::Lower;
    } else if (u == "T" || u == "TITLE") {
        mode = M::Title;
    } else if (u == "O" || u == "TOGGLE") {
        mode = M::Toggle;
    } else if (!u.empty() && u != "U" && u != "UPPER") {
        ctx.echo("Enter Sentence, Lower, Upper, Title or tOggle.");
        return;
    }
    ctx.submit(core::TextCaseCommand{mode, ctx.group_id()});
    done_ = true;
}

void TextCaseCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// Hot grips (issue #54)
// ---------------------------------------------------------------------------
void GripModeCommand::start(CommandContext& ctx) {
    stretching_ = true; // the press on the grip began the engine's drag
    enter(ctx, Mode::Stretch);
}

void GripModeCommand::prompt(CommandContext& ctx) {
    switch (mode_) {
    case Mode::Stretch:
        ctx.echo("** STRETCH **");
        ctx.set_prompt("Specify stretch point or [Base point/Copy/Undo/eXit]: ");
        return;
    case Mode::Move:
        ctx.echo("** MOVE **");
        ctx.set_prompt("Specify move point or [Base point/Copy/Undo/eXit]: ");
        return;
    case Mode::Rotate:
        ctx.echo("** ROTATE **");
        ctx.set_prompt("Specify rotation angle or [Base point/Copy/Undo/Reference/eXit]: ");
        return;
    case Mode::Scale:
        ctx.echo("** SCALE **");
        ctx.set_prompt("Specify scale factor or [Base point/Copy/Undo/Reference/eXit]: ");
        return;
    case Mode::Mirror:
        ctx.echo("** MIRROR **");
        ctx.set_prompt("Specify second point or [Base point/Copy/Undo/eXit]: ");
        return;
    }
}

void GripModeCommand::enter(CommandContext& ctx, Mode m) {
    if (stretching_ && m != Mode::Stretch) {
        ctx.submit(core::GripDragCommand{core::GripDragCommand::Phase::Cancel, {}, 0, {}, 0});
        stretching_ = false;
    } else if (!stretching_ && m == Mode::Stretch) {
        ctx.submit(core::GripDragCommand{core::GripDragCommand::Phase::Begin, handle_, index_, {}, 0});
        stretching_ = true;
    }
    mode_ = m;
    ctx.set_last_point(base_);
    PreviewSpec pv;
    pv.points = {base_};
    switch (m) {
    case Mode::Stretch:
        pv.kind = PreviewKind::Segment;
        pv.grip_hot = true;
        break;
    case Mode::Move:
        pv.kind = PreviewKind::Move;
        break;
    case Mode::Rotate:
        pv.kind = PreviewKind::Rotate;
        break;
    case Mode::Scale:
        pv.kind = PreviewKind::Scale;
        break;
    case Mode::Mirror:
        pv.kind = PreviewKind::Mirror;
        break;
    }
    ctx.set_preview(std::move(pv));
    prompt(ctx);
}

void GripModeCommand::apply(CommandContext& ctx, core::Vec2 p, std::optional<double> typed) {
    const std::uint64_t group = ctx.new_group();
    switch (mode_) {
    case Mode::Stretch:
        ctx.submit(core::GripDragCommand{core::GripDragCommand::Phase::Commit, {}, 0, p, group, copy_});
        stretching_ = false;
        break;
    case Mode::Move:
        if (copy_) {
            ctx.submit(core::CopySelectionCommand{p - base_, group});
        } else {
            ctx.submit(core::MoveSelectionCommand{p - base_, group});
        }
        break;
    case Mode::Rotate: {
        const double angle = typed ? core::to_radians(*typed) : std::atan2(p.y - base_.y, p.x - base_.x);
        ctx.submit(core::RotateSelectionCommand{base_, angle, group, copy_});
        break;
    }
    case Mode::Scale: {
        const double f = typed ? *typed : core::distance(p, base_);
        if (f <= 0.0) {
            ctx.echo("Requires a positive scale factor.");
            return;
        }
        ctx.submit(core::ScaleSelectionCommand{base_, f, group, copy_});
        break;
    }
    case Mode::Mirror:
        if (core::distance(p, base_) < 1e-12) {
            ctx.echo("The mirror line needs two different points.");
            return;
        }
        ctx.submit(core::MirrorSelectionCommand{base_, p, !copy_, group});
        break;
    }
    ++copies_;
    if (!copy_) {
        ctx.clear_preview();
        done_ = true;
        return;
    }
    if (mode_ == Mode::Stretch) {
        enter(ctx, Mode::Stretch); // a fresh drag for the next one
    }
}

void GripModeCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    if (asking_base_) {
        if (const auto p = read_point(ctx, text)) {
            base_ = *p;
            asking_base_ = false;
            enter(ctx, mode_);
        }
        return;
    }
    if (t.empty()) {
        // Enter or Space: the next mode.
        enter(ctx, mode_ == Mode::Mirror ? Mode::Stretch : static_cast<Mode>(static_cast<int>(mode_) + 1));
        return;
    }
    if (u == "X" || u == "EXIT") {
        cancel(ctx);
        return;
    }
    if (u == "B" || u == "BASE" || u == "BASE POINT") {
        if (mode_ == Mode::Stretch) {
            ctx.echo("STRETCH moves the grip itself; a base point is for MOVE, ROTATE, SCALE and MIRROR.");
            return;
        }
        asking_base_ = true;
        ctx.set_prompt("Specify base point: ");
        return;
    }
    if (ref_step_ != 0) {
        // Reference: the angle or length there is, then the one wanted -- typed, or a point
        // (its angle or distance from the base point).
        const bool rot = mode_ == Mode::Rotate;
        double v = 0.0;
        if (t.empty()) {
            v = ref_step_ == 1 ? (rot ? 0.0 : 1.0) : ref_value_;
        } else if (t.find(',') == std::string::npos && !ctx.input_is_pick() && parse_number(t, v)) {
            // typed
        } else if (const auto p = read_point(ctx, text)) {
            v = rot ? core::to_degrees(std::atan2(p->y - base_.y, p->x - base_.x)) : core::distance(*p, base_);
        } else {
            return;
        }
        if (ref_step_ == 1) {
            if (!rot && v <= 0.0) {
                ctx.echo("Requires a positive length.");
                return;
            }
            ref_value_ = v;
            ref_step_ = 2;
            ctx.set_prompt(rot ? "Specify new angle <0>: " : "Specify new length <1.0000>: ");
            return;
        }
        ref_step_ = 0;
        if (!rot && v <= 0.0) {
            ctx.echo("Requires a positive length.");
            prompt(ctx);
            return;
        }
        apply(ctx, base_, rot ? v - ref_value_ : v / ref_value_);
        if (!done_) {
            prompt(ctx);
        }
        return;
    }
    if (u == "C" || u == "COPY") {
        copy_ = true;
        prompt(ctx);
        return;
    }
    if (u == "U" || u == "UNDO") {
        if (copies_ == 0) {
            ctx.echo("Nothing to undo.");
        } else {
            ctx.submit(core::UndoLastGroupCommand{ctx.group_id()});
            --copies_;
        }
        prompt(ctx);
        return;
    }
    if ((u == "R" || u == "REFERENCE") && (mode_ == Mode::Rotate || mode_ == Mode::Scale)) {
        ref_step_ = 1;
        ctx.set_prompt(mode_ == Mode::Rotate ? "Specify reference angle <0>: " : "Specify reference length <1.0000>: ");
        return;
    }
    if ((mode_ == Mode::Rotate || mode_ == Mode::Scale) && t.find(',') == std::string::npos && !ctx.input_is_pick()) {
        double v = 0.0;
        if (parse_number(t, v)) {
            apply(ctx, base_, v);
            return;
        }
    }
    if (const auto p = read_point(ctx, text)) {
        apply(ctx, *p, std::nullopt);
    }
}

void GripModeCommand::cancel(CommandContext& ctx) {
    if (stretching_) {
        ctx.submit(core::GripDragCommand{core::GripDragCommand::Phase::Cancel, {}, 0, {}, 0});
        stretching_ = false;
    }
    ctx.clear_preview();
    done_ = true;
}

// ---------------------------------------------------------------------------
// ADDSELECTED (issue #45)
// ---------------------------------------------------------------------------
void AddSelectedCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select object: ");
}

void AddSelectedCommand::after(CommandContext& ctx) {
    if (sub_ && sub_->done()) {
        ctx.submit(core::PropsFromPickCommand{{}, 0.0, true}); // the current properties back
        done_ = true;
    }
}

void AddSelectedCommand::input(CommandContext& ctx, const std::string& text) {
    if (sub_) {
        sub_->input(ctx, text);
        after(ctx);
        return;
    }
    if (trimmed(text).empty()) {
        done_ = true;
        return;
    }
    const std::optional<core::EntityKind> kind = ctx.hovered_kind();
    const auto p = read_point(ctx, text);
    if (!p) {
        return;
    }
    using K = core::EntityKind;
    const char* alias = nullptr;
    if (kind) {
        switch (*kind) {
        case K::Line: alias = "LINE"; break;
        case K::Polyline: alias = "PLINE"; break;
        case K::Circle: alias = "CIRCLE"; break;
        case K::Arc: alias = "ARC"; break;
        case K::Ellipse: alias = "ELLIPSE"; break;
        case K::Spline: alias = "SPLINE"; break;
        case K::Point: alias = "POINT"; break;
        case K::Text: alias = "TEXT"; break;
        case K::MText: alias = "MTEXT"; break;
        case K::Hatch: alias = "HATCH"; break;
        case K::Dimension: alias = "DIM"; break;
        case K::Leader: alias = "LEADER"; break;
        case K::MLeader: alias = "MLEADER"; break;
        case K::Insert: alias = "INSERT"; break;
        case K::Xline: alias = "XLINE"; break;
        case K::Table: alias = "TABLE"; break;
        default: break;
        }
    }
    if (alias == nullptr) {
        ctx.echo(kind ? "ADDSELECTED does not draw that kind of object." : "No object found.");
        return;
    }
    sub_ = ctx.make_command(alias);
    if (!sub_) {
        done_ = true;
        return;
    }
    ctx.submit(core::PropsFromPickCommand{*p, ctx.pick_radius(), false});
    ctx.echo("Command: " + sub_->name());
    sub_->start(ctx);
    after(ctx);
}

void AddSelectedCommand::selection_gesture(CommandContext& ctx) {
    if (sub_) {
        sub_->selection_gesture(ctx);
        after(ctx);
    }
}

void AddSelectedCommand::freehand(CommandContext& ctx, const std::vector<core::Vec2>& path) {
    if (sub_) {
        sub_->freehand(ctx, path);
        after(ctx);
    }
}

void AddSelectedCommand::cancel(CommandContext& ctx) {
    if (sub_) {
        sub_->cancel(ctx);
        ctx.submit(core::PropsFromPickCommand{{}, 0.0, true});
    } else {
        ctx.echo("*Cancel*");
    }
    done_ = true;
}

// ---------------------------------------------------------------------------
// MLEADERALIGN (issue #61)
// ---------------------------------------------------------------------------
void MLeaderAlignUiCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (ctx.has_selection()) {
        base_prompt(ctx);
        return;
    }
    step_ = Step::Select;
    select_.begin(ctx, "Select multileaders: ");
}

void MLeaderAlignUiCommand::base_prompt(CommandContext& ctx) {
    static constexpr const char* kModes[4] = {"Use current spacing", "Specify spacing", "Distribute", "Parallel"};
    ctx.echo(std::string("Current mode: ") + kModes[static_cast<int>(s_mode_)]);
    if (s_mode_ == Mode::Distribute) {
        step_ = Step::First;
        ctx.set_prompt("Specify first point or [Options]: ");
        return;
    }
    step_ = Step::Base;
    ctx.set_prompt("Select multileader to align to or [Options]: ");
}

void MLeaderAlignUiCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        if (!ctx.has_selection()) {
            done_ = true;
            return;
        }
        base_prompt(ctx);
    }
}

void MLeaderAlignUiCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    switch (step_) {
    case Step::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            if (!ctx.has_selection()) {
                done_ = true;
                return;
            }
            base_prompt(ctx);
        }
        return;
    case Step::Base:
    case Step::First:
        if (u.empty()) {
            done_ = true;
            return;
        }
        if (u == "O" || u == "OPTIONS") {
            step_ = Step::Option;
            ctx.set_prompt("Enter an option [Distribute/make leader segments Parallel/specify Spacing/Use current] "
                           "<Use current>: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            ctx.set_last_point(*p);
            if (step_ == Step::First) {
                first_ = *p;
                step_ = Step::Second;
                ctx.set_preview(PreviewSpec{PreviewKind::Segment, {*p}});
                ctx.set_prompt("Specify second point: ");
                return;
            }
            base_ = *p;
            if (s_mode_ == Mode::Parallel) {
                core::MLeaderAlignCommand c{base_, ctx.pick_radius(), {}, ctx.group_id()};
                c.mode = Mode::Parallel;
                ctx.submit(c);
                done_ = true;
                return;
            }
            step_ = Step::Direction;
            ctx.set_preview(PreviewSpec{PreviewKind::Segment, {*p}});
            ctx.set_prompt("Specify direction: ");
        }
        return;
    case Step::Option:
        if (u == "D" || u == "DISTRIBUTE") {
            s_mode_ = Mode::Distribute;
        } else if (u == "P" || u == "PARALLEL") {
            s_mode_ = Mode::Parallel;
        } else if (u == "S" || u == "SPACING") {
            step_ = Step::Spacing;
            ctx.set_prompt("Specify spacing <" + fmt4(s_spacing_) + ">: ");
            return;
        } else if (u.empty() || u == "U" || u == "USE CURRENT") {
            s_mode_ = Mode::UseCurrent;
        } else {
            ctx.echo("Invalid option keyword.");
            return;
        }
        base_prompt(ctx);
        return;
    case Step::Spacing:
        if (!u.empty()) {
            const std::optional<double> v = parse_number(u);
            if (!v || *v < 0.0) {
                ctx.echo("Requires a spacing of zero or more.");
                return;
            }
            s_spacing_ = *v;
        }
        s_mode_ = Mode::Spacing;
        base_prompt(ctx);
        return;
    case Step::Second:
        if (const auto p = read_point(ctx, text)) {
            ctx.clear_preview();
            core::MLeaderAlignCommand c{};
            c.mode = Mode::Distribute;
            c.from = first_;
            c.to = *p;
            c.group = ctx.group_id();
            ctx.submit(c);
            done_ = true;
        }
        return;
    case Step::Direction:
        if (const auto p = read_point(ctx, text)) {
            ctx.clear_preview();
            core::MLeaderAlignCommand c{base_, ctx.pick_radius(), *p, ctx.group_id()};
            c.mode = s_mode_;
            c.spacing = s_spacing_;
            ctx.submit(c);
            done_ = true;
        }
        return;
    }
}

void MLeaderAlignUiCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.clear_preview();
    done_ = true;
}

// ---------------------------------------------------------------------------
// -DWGUNITS (issue #81)
// ---------------------------------------------------------------------------
namespace {
// -DWGUNITS's six units, as INSUNITS codes.
constexpr std::uint8_t kDwgUnits[] = {1, 2, 4, 5, 14, 6};
constexpr const char* kDwgUnitNames[] = {"Inches", "Feet", "Millimeters", "Centimeters", "Decimeters", "Meters"};
} // namespace

void DwgUnitsCommand::start(CommandContext& ctx) {
    units_ = ctx.units();
    from_ = units_.insunits;
    step_ = Step::Unit;
    ask(ctx);
}

void DwgUnitsCommand::ask(CommandContext& ctx) {
    switch (step_) {
    case Step::Unit: {
        ctx.echo("Drawing units:");
        int cur = 3;
        for (int i = 0; i < 6; ++i) {
            ctx.echo("  " + std::to_string(i + 1) + ". " + kDwgUnitNames[i]);
            cur = kDwgUnits[i] == units_.insunits ? i + 1 : cur;
        }
        ctx.set_prompt("Unit for length <" + std::to_string(cur) + ">: ");
        return;
    }
    case Step::Format:
        ctx.echo("Linear display format:");
        ctx.echo("  1. Scientific  2. Decimal  3. Engineering  4. Architectural  5. Fractional");
        ctx.set_prompt("Linear display format <" + std::to_string(static_cast<int>(units_.linear)) + ">: ");
        return;
    case Step::Precision:
        ctx.set_prompt("Linear display precision <" + std::to_string(units_.linear_precision) + ">: ");
        return;
    case Step::InsertScale:
        ctx.set_prompt("Scale objects from other drawings upon insert? [Yes/No] <Yes>: ");
        return;
    case Step::Match:
        ctx.set_prompt("Match INSUNITS to drawing units? [Yes/No] <Yes>: ");
        return;
    case Step::Scale:
        ctx.set_prompt("Scale objects in current drawing to reflect change in units? [Yes/No] <Yes>: ");
        return;
    }
}

void DwgUnitsCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    const auto whole = [&](int lo, int hi, int& out) {
        double v = 0.0;
        if (t.empty()) {
            return true;
        }
        if (!parse_number(t, v) || v < lo || v > hi || v != std::floor(v)) {
            ctx.echo("Requires a whole number from " + std::to_string(lo) + " to " + std::to_string(hi) + ".");
            return false;
        }
        out = static_cast<int>(v);
        return true;
    };
    const auto yes_no = [&](bool& out) {
        if (t.empty() || u == "Y" || u == "YES") {
            out = true;
        } else if (u == "N" || u == "NO") {
            out = false;
        } else {
            ctx.echo("Enter Yes or No.");
            return false;
        }
        return true;
    };
    switch (step_) {
    case Step::Unit: {
        int choice = 0;
        if (!whole(1, 6, choice)) {
            return;
        }
        if (choice > 0) {
            units_.insunits = kDwgUnits[choice - 1];
        } else if (units_.insunits == 0) {
            units_.insunits = 4; // Millimeters, the default offered
        }
        step_ = Step::Format;
        break;
    }
    case Step::Format: {
        int f = static_cast<int>(units_.linear);
        if (!whole(1, 5, f)) {
            return;
        }
        units_.linear = static_cast<core::LinearFormat>(f);
        step_ = Step::Precision;
        break;
    }
    case Step::Precision: {
        int p = units_.linear_precision;
        if (!whole(0, 8, p)) {
            return;
        }
        units_.linear_precision = static_cast<std::uint8_t>(p);
        step_ = Step::InsertScale;
        break;
    }
    case Step::InsertScale:
    case Step::Match: {
        bool yes = true;
        if (!yes_no(yes)) {
            return;
        }
        if (!yes) {
            ctx.echo(step_ == Step::InsertScale ? "Objects from other drawings are always scaled by their units here."
                                                : "INSUNITS is the drawing unit here.");
        }
        step_ = step_ == Step::InsertScale ? Step::Match : Step::Scale;
        break;
    }
    case Step::Scale: {
        bool yes = true;
        if (!yes_no(yes)) {
            return;
        }
        const double f = core::units::insunits_factor(from_, units_.insunits);
        if (yes && f != 1.0) {
            ctx.submit(core::ScaleDrawingCommand{f, ctx.group_id()});
        }
        ctx.submit(core::SetUnitsCommand{units_});
        done_ = true;
        return;
    }
    }
    ask(ctx);
}

void DwgUnitsCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// MOCORO and FLATTEN (issue #77), CHANGE and HATCHEDIT (issue #55)
// ---------------------------------------------------------------------------
void MocoroCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (ctx.has_selection()) {
        step_ = Step::Base;
        ctx.set_prompt("Base point: ");
        return;
    }
    step_ = Step::Select;
    select_.begin(ctx);
}

void MocoroCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        if (!ctx.has_selection()) {
            done_ = true;
            return;
        }
        step_ = Step::Base;
        ctx.set_prompt("Base point: ");
    }
}

void MocoroCommand::prompt(CommandContext& ctx) {
    step_ = Step::Main;
    ctx.set_last_point(base_);
    PreviewSpec pv;
    pv.points = {base_};
    switch (mode_) {
    case Mode::Move:
        pv.kind = PreviewKind::Move;
        ctx.set_prompt("Second point or [Copy/Base/Rotate/Scale/Undo] <eXit>: ");
        break;
    case Mode::Rotate:
        pv.kind = PreviewKind::Rotate;
        ctx.set_prompt("Rotation angle or [Copy/Base/Move/Scale/Undo] <eXit>: ");
        break;
    case Mode::Scale:
        pv.kind = PreviewKind::Scale;
        ctx.set_prompt("Scale factor or [Copy/Base/Move/Rotate/Undo] <eXit>: ");
        break;
    }
    ctx.set_preview(std::move(pv));
}

void MocoroCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            if (!ctx.has_selection()) {
                done_ = true;
                return;
            }
            step_ = Step::Base;
            ctx.set_prompt("Base point: ");
        }
        return;
    case Step::Base:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            base_ = *p;
            prompt(ctx);
        }
        return;
    case Step::Main:
        break;
    }
    if (new_base_) {
        if (const auto p = read_point(ctx, text)) {
            base_ = *p;
            new_base_ = false;
            prompt(ctx);
        }
        return;
    }
    if (t.empty() || u == "X" || u == "EXIT") {
        ctx.clear_preview();
        done_ = true;
        return;
    }
    if (u == "C" || u == "COPY") {
        copy_ = !copy_;
        ctx.echo(copy_ ? "Copy mode on: the next ones are copies." : "Copy mode off.");
        prompt(ctx);
        return;
    }
    if (u == "B" || u == "BASE") {
        new_base_ = true;
        ctx.set_prompt("Base point: ");
        return;
    }
    if (u == "M" || u == "MOVE" || u == "R" || u == "ROTATE" || u == "S" || u == "SCALE") {
        mode_ = u[0] == 'M' ? Mode::Move : u[0] == 'R' ? Mode::Rotate : Mode::Scale;
        prompt(ctx);
        return;
    }
    if (u == "U" || u == "UNDO") {
        if (ops_ == 0) {
            ctx.echo("Nothing to undo.");
        } else {
            ctx.submit(core::UndoLastGroupCommand{ctx.command_group()});
            --ops_;
        }
        prompt(ctx);
        return;
    }
    std::optional<double> typed;
    if (mode_ != Mode::Move && t.find(',') == std::string::npos && !ctx.input_is_pick()) {
        double v = 0.0;
        if (parse_number(t, v)) {
            typed = v;
        }
    }
    std::optional<core::Vec2> p;
    if (!typed) {
        p = read_point(ctx, text);
        if (!p) {
            return;
        }
    }
    const std::uint64_t group = ctx.new_group();
    switch (mode_) {
    case Mode::Move:
        if (copy_) {
            ctx.submit(core::CopySelectionCommand{*p - base_, group});
        } else {
            ctx.submit(core::MoveSelectionCommand{*p - base_, group});
            base_ = *p; // the objects, and their base, are there now
        }
        break;
    case Mode::Rotate: {
        const double a = typed ? core::to_radians(*typed) : std::atan2(p->y - base_.y, p->x - base_.x);
        ctx.submit(core::RotateSelectionCommand{base_, a, group, copy_});
        break;
    }
    case Mode::Scale: {
        const double f = typed ? *typed : core::distance(*p, base_);
        if (f <= 0.0) {
            ctx.echo("Requires a positive scale factor.");
            return;
        }
        ctx.submit(core::ScaleSelectionCommand{base_, f, group, copy_});
        break;
    }
    }
    ++ops_;
    prompt(ctx);
}

void MocoroCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    ctx.clear_preview();
    done_ = true;
}

void TextCountUiCommand::start(CommandContext& ctx) {
    if (ctx.has_selection()) {
        after(ctx);
        return;
    }
    step_ = Step::Select;
    select_.begin(ctx, "Select objects: ");
}

void TextCountUiCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        after(ctx);
    }
}

void TextCountUiCommand::after(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        done_ = true;
        return;
    }
    step_ = Step::Sort;
    using S = core::TextCountCommand::Sort;
    ctx.set_prompt(std::string("Sort selected objects by [X/Y/Select-order] <") +
                   (s_.sort == S::X ? "X" : s_.sort == S::Y ? "Y" : "Select-order") + ">: ");
}

void TextCountUiCommand::submit(CommandContext& ctx) {
    core::TextCountCommand c = s_;
    c.group = ctx.group_id();
    ctx.submit(std::move(c));
    done_ = true;
}

void TextCountUiCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    using S = core::TextCountCommand::Sort;
    using P = core::TextCountCommand::Place;
    switch (step_) {
    case Step::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            after(ctx);
        }
        return;
    case Step::Sort:
        if (u == "X") {
            s_.sort = S::X;
        } else if (u == "Y") {
            s_.sort = S::Y;
        } else if (u == "S" || u == "SELECT-ORDER") {
            s_.sort = S::Selected;
        } else if (!t.empty()) {
            ctx.echo("Invalid option keyword.");
            return;
        }
        step_ = Step::Numbers;
        ctx.set_prompt("Specify starting number and increment (Start,increment) <" + std::to_string(s_.start) + "," +
                       std::to_string(s_.step) + ">: ");
        return;
    case Step::Numbers:
        if (!t.empty()) {
            long long a = 0;
            long long b = 1;
            char tail = 0;
            const int got = std::sscanf(t.c_str(), "%lld , %lld %c", &a, &b, &tail);
            if (got < 1 || got > 2) {
                ctx.echo("Enter the starting number, or the starting number and the increment, as whole numbers.");
                return;
            }
            s_.start = a;
            if (got == 2) {
                s_.step = b;
            }
        }
        step_ = Step::Place;
        ctx.set_prompt(std::string("Placement of numbers in text [Overwrite/Prefix/Suffix/Find&replace..] <") +
                       (s_.place == P::Overwrite ? "Overwrite"
                        : s_.place == P::Suffix  ? "Suffix"
                        : s_.place == P::Replace ? "Find&replace.."
                                                 : "Prefix") +
                       ">: ");
        return;
    case Step::Place:
        if (u == "O" || u == "OVERWRITE") {
            s_.place = P::Overwrite;
        } else if (u == "P" || u == "PREFIX") {
            s_.place = P::Prefix;
        } else if (u == "S" || u == "SUFFIX") {
            s_.place = P::Suffix;
        } else if (u == "F" || u.starts_with("FIND")) {
            s_.place = P::Replace;
        } else if (!t.empty()) {
            ctx.echo("Invalid option keyword.");
            return;
        }
        if (s_.place == P::Replace) {
            step_ = Step::Find;
            ctx.set_prompt(s_.find.empty() ? std::string("Enter search string: ")
                                           : "Enter search string <" + s_.find + ">: ");
            return;
        }
        submit(ctx);
        return;
    case Step::Find:
        if (!t.empty()) {
            s_.find = t;
        }
        if (s_.find.empty()) {
            ctx.echo("Enter the text the numbers replace.");
            return;
        }
        submit(ctx);
        return;
    }
}

void TextCountUiCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void TextFitUiCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select Text to stretch or shrink: ");
}

void TextFitUiCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::Pick:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            c_.pick = *p;
            c_.radius = ctx.pick_radius();
            step_ = Step::End;
            ctx.set_prompt("Specify end point or [Start point]: ");
        }
        return;
    case Step::End:
    case Step::EndAfterStart:
        if (step_ == Step::End && (u == "S" || u == "START" || u == "START POINT")) {
            step_ = Step::Start;
            ctx.set_prompt("Specify new starting point: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            c_.end = *p;
            c_.group = ctx.group_id();
            ctx.submit(c_);
            done_ = true;
        }
        return;
    case Step::Start:
        if (const auto p = read_point(ctx, text)) {
            c_.start = *p;
            step_ = Step::EndAfterStart;
            ctx.set_prompt("Specify end point: ");
        }
        return;
    }
}

void TextFitUiCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void GlobalAttEditUiCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select block or attribute [Block name]: ");
}

void GlobalAttEditUiCommand::ask_tag(CommandContext& ctx) {
    step_ = Step::Tag;
    if (!c_.block.empty()) {
        std::string tags;
        for (const core::BlockAttDefInfo& a : ctx.block_attdefs(c_.block)) {
            tags += " " + a.tag;
        }
        ctx.echo("Known tag names for block:" + (tags.empty() ? std::string(" (none)") : tags));
    }
    ctx.set_prompt("Select attribute or type attribute name: ");
}

void GlobalAttEditUiCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::Pick:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (u == "B" || u == "BLOCK" || u == "BLOCK NAME") {
            step_ = Step::BlockName;
            ctx.set_prompt("Enter block name: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            c_.pick = *p;
            c_.radius = ctx.pick_radius();
            ask_tag(ctx);
        }
        return;
    case Step::BlockName: {
        if (t.empty()) {
            done_ = true;
            return;
        }
        bool known = false;
        for (const std::string& n : ctx.block_names()) {
            if (upper(n) == u) {
                c_.block = n;
                known = true;
            }
        }
        if (!known) {
            ctx.echo("Block \"" + t + "\" not found.");
            return;
        }
        ask_tag(ctx);
        return;
    }
    case Step::Tag:
        if (t.empty()) {
            done_ = true;
            return;
        }
        c_.tag = t;
        step_ = Step::Value;
        ctx.set_prompt("Enter new text: ");
        return;
    case Step::Value:
        c_.value = t;
        if (!c_.pick) {
            c_.group = ctx.group_id(); // by name: every reference
            ctx.submit(c_);
            done_ = true;
            return;
        }
        step_ = Step::All;
        ctx.set_prompt("Process all of them? [Yes/No] <Yes>: ");
        return;
    case Step::All:
        if (u == "N" || u == "NO") {
            c_.all = false;
        } else if (!t.empty() && u != "Y" && u != "YES") {
            ctx.echo("Enter Yes or No.");
            return;
        }
        c_.group = ctx.group_id();
        ctx.submit(c_);
        done_ = true;
        return;
    }
}

void GlobalAttEditUiCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void VpScaleUiCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select the viewport to list the scale: ");
}

void VpScaleUiCommand::input(CommandContext& ctx, const std::string& text) {
    if (trimmed(text).empty()) {
        done_ = true;
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        ctx.submit(core::VpScaleQueryCommand{*p, ctx.pick_radius()});
        done_ = true;
    }
}

void VpScaleUiCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void TextExplodeUiCommand::start(CommandContext& ctx) {
    if (ctx.has_selection()) {
        after(ctx);
        return;
    }
    select_.begin(ctx, "Select objects: ");
}

void TextExplodeUiCommand::after(CommandContext& ctx) {
    if (ctx.has_selection()) {
        ctx.submit(core::TextExplodeCommand{ctx.group_id()});
    }
    done_ = true;
}

void TextExplodeUiCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        after(ctx);
    }
}

void TextExplodeUiCommand::input(CommandContext& ctx, const std::string& text) {
    if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
        after(ctx);
    }
}

void TextExplodeUiCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void FlattenUiCommand::start(CommandContext& ctx) {
    if (ctx.has_selection()) {
        after(ctx);
        return;
    }
    selecting_ = true;
    select_.begin(ctx, "Select objects to convert to 2d: ");
}

void FlattenUiCommand::after(CommandContext& ctx) {
    selecting_ = false;
    if (!ctx.has_selection()) {
        done_ = true;
        return;
    }
    asked_ = true;
    ctx.set_prompt("Remove hidden lines? <No>: ");
}

void FlattenUiCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        after(ctx);
    }
}

void FlattenUiCommand::input(CommandContext& ctx, const std::string& text) {
    if (selecting_) {
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            after(ctx);
        }
        return;
    }
    const std::string u = upper(trimmed(text));
    if (u == "Y" || u == "YES") {
        ctx.echo("The drawing has no hidden lines to remove; everything stays.");
    } else if (!u.empty() && u != "N" && u != "NO") {
        ctx.echo("Enter Yes or No.");
        return;
    }
    ctx.submit(core::FlattenCommand{ctx.group_id()});
    done_ = true;
}

void FlattenUiCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void ChangeUiCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (ctx.has_selection()) {
        after_selection(ctx);
        return;
    }
    step_ = Step::Select;
    select_.begin(ctx);
}

void ChangeUiCommand::after_selection(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        done_ = true;
        return;
    }
    step_ = Step::Point;
    ctx.set_prompt("Specify change point or [Properties]: ");
}

void ChangeUiCommand::selection_gesture(CommandContext& ctx) {
    if (sub_) {
        sub_->selection_gesture(ctx);
        done_ = sub_->done();
        return;
    }
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        after_selection(ctx);
    }
}

void ChangeUiCommand::next(CommandContext& ctx, Step from) {
    // The text prompts when texts are selected, the block ones when blocks are, then done.
    const std::uint32_t kinds = ctx.selection_kinds();
    const bool texts = (kinds & (1u << static_cast<unsigned>(core::EntityKind::Text))) != 0;
    const bool blocks = (kinds & (1u << static_cast<unsigned>(core::EntityKind::Insert))) != 0;
    const auto ask = [&](Step s) {
        step_ = s;
        switch (s) {
        case Step::TextPos:
            ctx.set_prompt("Specify new text insertion point <no change>: ");
            return;
        case Step::TextStyle:
            ctx.set_prompt("Enter new text style <no change>: ");
            return;
        case Step::TextHeight:
            ctx.set_prompt("Specify new height <no change>: ");
            return;
        case Step::TextRotation:
            ctx.set_prompt("Specify new rotation angle <no change>: ");
            return;
        case Step::TextContent:
            ctx.set_prompt("Enter new text <no change>: ");
            return;
        case Step::BlockPos:
            ctx.set_prompt("Specify new block insertion point <no change>: ");
            return;
        case Step::BlockRotation:
            ctx.set_prompt("Specify new block rotation angle <no change>: ");
            return;
        default:
            return;
        }
    };
    switch (from) {
    case Step::Point:
        if (texts) {
            ask(Step::TextPos);
            return;
        }
        [[fallthrough]];
    case Step::TextContent:
        if (blocks) {
            ask(Step::BlockPos);
            return;
        }
        break;
    case Step::TextPos:
        ask(Step::TextStyle);
        return;
    case Step::TextStyle:
        ask(Step::TextHeight);
        return;
    case Step::TextHeight:
        ask(Step::TextRotation);
        return;
    case Step::TextRotation:
        ask(Step::TextContent);
        return;
    case Step::BlockPos:
        ask(Step::BlockRotation);
        return;
    default:
        break;
    }
    change_.group = ctx.group_id();
    ctx.submit(change_);
    done_ = true;
}

void ChangeUiCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            after_selection(ctx);
        }
        return;
    case Step::Sub:
        sub_->input(ctx, text);
        done_ = sub_->done();
        return;
    case Step::Point:
        if (u == "P" || u == "PROPERTIES") {
            sub_ = ctx.make_command("CHPROP");
            if (!sub_) {
                done_ = true;
                return;
            }
            step_ = Step::Sub;
            sub_->start(ctx);
            done_ = sub_->done();
            return;
        }
        if (!t.empty()) {
            const auto p = read_point(ctx, text);
            if (!p) {
                return;
            }
            change_.point = *p;
        }
        next(ctx, Step::Point);
        return;
    case Step::TextPos:
    case Step::BlockPos:
        if (!t.empty()) {
            const auto p = read_point(ctx, text);
            if (!p) {
                return;
            }
            (step_ == Step::TextPos ? change_.text_pos : change_.block_pos) = *p;
        }
        next(ctx, step_);
        return;
    case Step::TextStyle:
        if (!t.empty()) {
            change_.text_style = t;
        }
        next(ctx, step_);
        return;
    case Step::TextHeight:
    case Step::TextRotation:
    case Step::BlockRotation:
        if (!t.empty()) {
            double v = 0.0;
            if (!parse_number(t, v) || (step_ == Step::TextHeight && v <= 0.0)) {
                ctx.echo(step_ == Step::TextHeight ? "Requires a positive height." : "Requires an angle.");
                return;
            }
            if (step_ == Step::TextHeight) {
                change_.text_height = v;
            } else {
                (step_ == Step::TextRotation ? change_.text_rotation : change_.block_rotation) = core::to_radians(v);
            }
        }
        next(ctx, step_);
        return;
    case Step::TextContent:
        if (!text.empty()) {
            change_.text_content = text;
        }
        next(ctx, step_);
        return;
    }
}

void ChangeUiCommand::cancel(CommandContext& ctx) {
    if (sub_) {
        sub_->cancel(ctx);
    } else {
        ctx.echo("*Cancel*");
    }
    done_ = true;
}

void HatchEditUiCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select hatch object: ");
}

void HatchEditUiCommand::options(CommandContext& ctx) {
    step_ = Step::Option;
    ctx.set_prompt("Enter hatch option [Disassociate/Style/Properties/DRaw order/ADd boundaries/Remove boundaries/"
                   "recreate Boundary/ASsociative/separate Hatches/Origin/ANnotative/hatch COlor/LAyer/Transparency] "
                   "<Properties>: ");
}

void HatchEditUiCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    const auto set = [&](core::PropertyId id, core::PropertyValue v) {
        ctx.submit(core::SetPropertyCommand{id, std::move(v), ctx.group_id()});
    };
    switch (step_) {
    case Step::Pick: {
        if (t.empty()) {
            done_ = true;
            return;
        }
        const std::optional<core::EntityKind> kind = ctx.hovered_kind();
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        if (ctx.input_is_pick() && kind && *kind != core::EntityKind::Hatch) {
            ctx.echo("The object selected is not a hatch.");
            return;
        }
        ctx.submit(core::ClearSelectionCommand{});
        ctx.submit(core::SelectPickCommand{*p, ctx.pick_radius()});
        options(ctx);
        return;
    }
    case Step::Option:
        if (t.empty() || u == "P" || u == "PROPERTIES") {
            step_ = Step::Pattern;
            ctx.set_prompt("Enter a pattern name or [?/Solid/User defined] <unchanged>: ");
        } else if (u == "B" || u == "RECREATE BOUNDARY" || u == "BOUNDARY") {
            step_ = Step::BoundaryType;
            ctx.set_prompt("Enter type of boundary object [Region/Polyline] <Polyline>: ");
        } else {
            ctx.echo("That option is not available yet; Properties and recreate Boundary are.");
            options(ctx);
        }
        return;
    case Step::Pattern: {
        if (t == "?") {
            std::string line = "Patterns: SOLID";
            for (const std::string& n : core::hatch::builtin_pattern_names()) {
                line += " " + n;
            }
            ctx.echo(line);
            return;
        }
        if (u == "U" || u == "USER DEFINED") {
            step_ = Step::UserAngle;
            ctx.set_prompt("Specify angle for crosshatch lines <" + fmt4(user_angle_) + ">: ");
            return;
        }
        if (!t.empty()) {
            const std::string name = (u == "S" || u == "SOLID") ? std::string("SOLID") : u;
            if (name != "SOLID" && core::hatch::builtin_pattern(name) == nullptr) {
                ctx.echo("Unknown pattern \"" + t + "\"; ? lists them.");
                return;
            }
            core::PropertyValue v;
            v.text = name;
            set(core::PropertyId::HatchPattern, v);
            if (name == "SOLID") {
                done_ = true;
                return;
            }
        }
        step_ = Step::Scale;
        ctx.set_prompt("Specify a scale for the pattern <unchanged>: ");
        return;
    }
    case Step::Scale:
    case Step::Angle:
        if (!t.empty()) {
            double v = 0.0;
            if (!parse_number(t, v) || (step_ == Step::Scale && v <= 0.0)) {
                ctx.echo(step_ == Step::Scale ? "Requires a positive scale." : "Requires an angle in degrees.");
                return;
            }
            core::PropertyValue pv;
            pv.num = v;
            set(step_ == Step::Scale ? core::PropertyId::HatchScale : core::PropertyId::HatchAngle, pv);
        }
        if (step_ == Step::Scale) {
            step_ = Step::Angle;
            ctx.set_prompt("Specify an angle for the pattern <unchanged>: ");
            return;
        }
        done_ = true;
        return;
    case Step::UserAngle:
    case Step::UserSpacing: {
        if (!t.empty()) {
            double v = 0.0;
            if (!parse_number(t, v) || (step_ == Step::UserSpacing && v <= 1e-9)) {
                ctx.echo(step_ == Step::UserSpacing ? "Requires a positive spacing." : "Requires an angle in degrees.");
                return;
            }
            (step_ == Step::UserAngle ? user_angle_ : user_spacing_) = v;
        }
        if (step_ == Step::UserAngle) {
            step_ = Step::UserSpacing;
            ctx.set_prompt("Specify spacing between the lines <" + fmt4(user_spacing_) + ">: ");
            return;
        }
        step_ = Step::UserDouble;
        ctx.set_prompt("Double hatch area? [Yes/No] <N>: ");
        return;
    }
    case Step::UserDouble: {
        const bool twice = u == "Y" || u == "YES";
        if (!t.empty() && !twice && u != "N" && u != "NO") {
            ctx.echo("Enter Yes or No.");
            return;
        }
        core::PropertyValue name;
        name.text = twice ? "_USERDOUBLE" : "_USER";
        set(core::PropertyId::HatchPattern, name);
        core::PropertyValue spacing;
        spacing.num = user_spacing_;
        set(core::PropertyId::HatchScale, spacing);
        core::PropertyValue angle;
        angle.num = user_angle_;
        set(core::PropertyId::HatchAngle, angle);
        done_ = true;
        return;
    }
    case Step::BoundaryType:
        if (u == "R" || u == "REGION") {
            ctx.echo("Regions are not available; the boundary is a polyline.");
        } else if (!t.empty() && u != "P" && u != "POLYLINE") {
            ctx.echo("Enter Region or Polyline.");
            return;
        }
        step_ = Step::Associate;
        ctx.set_prompt("Associate hatch with new boundary? [Yes/No] <N>: ");
        return;
    case Step::Associate:
        if (u == "Y" || u == "YES") {
            ctx.echo("Hatches here are not associative; the boundary is drawn alone.");
        } else if (!t.empty() && u != "N" && u != "NO") {
            ctx.echo("Enter Yes or No.");
            return;
        }
        ctx.submit(core::HatchBoundaryCommand{ctx.group_id()});
        done_ = true;
        return;
    }
}

void HatchEditUiCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void TextMaskUiCommand::prompt(CommandContext& ctx) {
    ctx.echo("Current settings: Offset factor = " + fmt4(s_offset_) + ", Mask type = Wipeout");
    step_ = Step::Select;
    select_.begin(ctx, "Select text objects to mask or [Masktype/Offset]: ");
}

void TextMaskUiCommand::start(CommandContext& ctx) {
    if (ctx.has_selection()) {
        after(ctx);
        return;
    }
    prompt(ctx);
}

void TextMaskUiCommand::after(CommandContext& ctx) {
    if (ctx.has_selection()) {
        ctx.submit(core::TextMaskCommand{s_offset_, ctx.group_id()});
    }
    done_ = true;
}

void TextMaskUiCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        after(ctx);
    }
}

void TextMaskUiCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::Select:
        if (u == "O" || u == "OFFSET") {
            step_ = Step::Offset;
            ctx.set_prompt("Enter offset factor relative to text height <" + fmt4(s_offset_) + ">: ");
            return;
        }
        if (u == "M" || u == "MASKTYPE") {
            step_ = Step::Masktype;
            ctx.set_prompt("Specify object type to use for mask [Wipeout/3dface/Solid] <Wipeout>: ");
            return;
        }
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            after(ctx);
        }
        return;
    case Step::Offset: {
        double v = 0.0;
        if (!t.empty()) {
            if (!parse_number(t, v) || v < 0.0) {
                ctx.echo("Requires an offset of zero or more.");
                return;
            }
            s_offset_ = v;
        }
        prompt(ctx);
        return;
    }
    case Step::Masktype:
        if (!t.empty() && u != "W" && u != "WIPEOUT") {
            ctx.echo("Only Wipeout masks are available here.");
        }
        prompt(ctx);
        return;
    }
}

void TextMaskUiCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void DimInspectUiCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    if (ctx.has_selection()) {
        after(ctx);
        return;
    }
    step_ = Step::Select;
    select_.begin(ctx, "Select dimensions: ");
}

void DimInspectUiCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        after(ctx);
    }
}

void DimInspectUiCommand::after(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        done_ = true;
        return;
    }
    if (dialog_ && ctx.view() != nullptr) {
        core::DimInspectCommand io{false, s_shape_, s_label_, s_rate_, 0};
        const int r = ctx.view()->dim_inspect_dialog(io);
        if (r >= 0) {
            if (r == 1) {
                s_shape_ = io.shape;
                s_label_ = io.label;
                s_rate_ = io.rate;
                submit(ctx, io.remove);
            }
            done_ = true;
            return;
        }
    }
    step_ = Step::Option;
    ctx.set_prompt("Add inspection data or [Remove] <Add>: ");
}

void DimInspectUiCommand::submit(CommandContext& ctx, bool remove) {
    ctx.submit(core::DimInspectCommand{remove, s_shape_, s_label_, s_rate_, ctx.group_id()});
    done_ = true;
}

void DimInspectUiCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::Select:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            after(ctx);
        }
        return;
    case Step::Option:
        if (u == "R" || u == "REMOVE") {
            submit(ctx, true);
            return;
        }
        if (!t.empty() && u != "A" && u != "ADD") {
            ctx.echo("Invalid option keyword.");
            return;
        }
        step_ = Step::Shape;
        ctx.set_prompt(std::string("Enter shape option [Round/Angular/None] <") +
                       (s_shape_ == 2 ? "Angular" : s_shape_ == 3 ? "None" : "Round") + ">: ");
        return;
    case Step::Shape:
        if (u == "R" || u == "ROUND") {
            s_shape_ = 1;
        } else if (u == "A" || u == "ANGULAR") {
            s_shape_ = 2;
        } else if (u == "N" || u == "NONE") {
            s_shape_ = 3;
        } else if (!t.empty()) {
            ctx.echo("Invalid option keyword.");
            return;
        }
        step_ = Step::Label;
        ctx.set_prompt(s_label_.empty() ? std::string("Enter label data or <None>: ")
                                        : "Enter label data or <" + s_label_ + ">: ");
        return;
    case Step::Label:
        if (t == ".") {
            s_label_.clear();
        } else if (!t.empty()) {
            s_label_ = t;
        }
        step_ = Step::Rate;
        ctx.set_prompt("Enter inspection rate <" + (s_rate_.empty() ? std::string("100%") : s_rate_) + ">: ");
        return;
    case Step::Rate:
        if (t == ".") {
            s_rate_.clear();
        } else if (!t.empty()) {
            s_rate_ = t;
        }
        submit(ctx, false);
        return;
    }
}

void DimInspectUiCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void SplineEditUiCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select spline: ");
}

void SplineEditUiCommand::options(CommandContext& ctx) {
    step_ = Step::Option;
    ctx.set_prompt("Enter an option [Close/Join/Fit data/Edit vertex/convert to Polyline/Reverse/Undo/eXit] <eXit>: ");
}

void SplineEditUiCommand::vertex_options(CommandContext& ctx) {
    step_ = Step::Vertex;
    ctx.set_prompt("Enter a vertex editing option [Add/Delete/Elevate order/Move/Weight/eXit] <eXit>: ");
}

void SplineEditUiCommand::edit(CommandContext& ctx, core::SplineEditCommand::Op op, core::Vec2 p1, core::Vec2 p2,
                               int precision) {
    ctx.submit(core::SplineEditCommand{op, pick_, ctx.pick_radius(), p1, p2, precision, ctx.new_group()});
    ++edits_;
}

void SplineEditUiCommand::finish(CommandContext& ctx) {
    ctx.submit(core::SplineEditCommand{core::SplineEditCommand::Op::End, {}, 0.0, {}, {}, 10, 0});
    done_ = true;
}

void SplineEditUiCommand::input(CommandContext& ctx, const std::string& text) {
    using Op = core::SplineEditCommand::Op;
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::Pick: {
        if (t.empty()) {
            done_ = true;
            return;
        }
        const std::optional<core::EntityKind> kind = ctx.hovered_kind();
        const auto p = read_point(ctx, text);
        if (!p) {
            return;
        }
        if (ctx.input_is_pick() && kind && *kind != core::EntityKind::Spline) {
            ctx.echo("The object selected is not a spline.");
            return;
        }
        pick_ = *p;
        ctx.submit(core::SplineEditCommand{Op::Begin, pick_, ctx.pick_radius(), {}, {}, 10, 0});
        options(ctx);
        return;
    }
    case Step::Option:
        if (t.empty() || u == "X" || u == "EXIT") {
            finish(ctx);
        } else if (u == "E" || u == "EDIT VERTEX") {
            vertex_options(ctx);
        } else if (u == "P" || u == "CONVERT TO POLYLINE") {
            step_ = Step::Precision;
            ctx.set_prompt("Specify a precision <10>: ");
        } else if (u == "R" || u == "REVERSE") {
            edit(ctx, Op::Reverse);
            options(ctx);
        } else if (u == "U" || u == "UNDO") {
            if (edits_ == 0) {
                ctx.echo("Nothing to undo.");
            } else {
                ctx.submit(core::UndoLastGroupCommand{ctx.command_group()});
                --edits_;
                // The spline as it was is a new object: take it again where it was picked.
                ctx.submit(core::SplineEditCommand{Op::Begin, pick_, ctx.pick_radius(), {}, {}, 10, 0});
            }
            options(ctx);
        } else if (u == "C" || u == "CLOSE" || u == "J" || u == "JOIN" || u == "F" || u == "FIT DATA") {
            ctx.echo("That option is not available yet: Edit vertex, convert to Polyline, Reverse and Undo are.");
            options(ctx);
        } else {
            ctx.echo("Invalid option keyword.");
            options(ctx);
        }
        return;
    case Step::Precision: {
        double v = 10.0;
        if (!t.empty() && (!parse_number(t, v) || v < 0.0 || v > 99.0)) {
            ctx.echo("Requires a precision from 0 to 99.");
            return;
        }
        edit(ctx, Op::ToPolyline, {}, {}, static_cast<int>(v));
        finish(ctx); // the spline is a polyline now
        return;
    }
    case Step::Vertex:
        if (t.empty() || u == "X" || u == "EXIT") {
            options(ctx);
        } else if (u == "A" || u == "ADD") {
            step_ = Step::Add;
            ctx.set_prompt("Specify a control point location on the spline <exit>: ");
        } else if (u == "D" || u == "DELETE") {
            step_ = Step::Delete;
            ctx.set_prompt("Specify control point <exit>: ");
        } else if (u == "M" || u == "MOVE") {
            step_ = Step::MoveFrom;
            ctx.set_prompt("Specify control point to move: ");
        } else if (u == "E" || u == "ELEVATE ORDER" || u == "W" || u == "WEIGHT") {
            ctx.echo("Elevate order and Weight are not available yet.");
            vertex_options(ctx);
        } else {
            ctx.echo("Invalid option keyword.");
            vertex_options(ctx);
        }
        return;
    case Step::Add:
    case Step::Delete:
        if (t.empty()) {
            vertex_options(ctx);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            edit(ctx, step_ == Step::Add ? Op::AddVertex : Op::DeleteVertex, *p);
        }
        return; // the same prompt, for another
    case Step::MoveFrom:
        if (t.empty()) {
            vertex_options(ctx);
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            from_ = *p;
            ctx.set_last_point(*p);
            step_ = Step::MoveTo;
            ctx.set_prompt("Specify new location: ");
        }
        return;
    case Step::MoveTo:
        if (const auto p = read_point(ctx, text)) {
            edit(ctx, Op::MoveVertex, from_, *p);
            vertex_options(ctx);
        }
        return;
    }
}

void SplineEditUiCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    finish(ctx);
}

void DimJogLineCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    ctx.set_prompt("Select dimension to add jog or [Remove]: ");
}

void DimBreakUiCommand::submit(CommandContext& ctx, core::DimBreakCommand c) {
    c.pick = pick_;
    c.pick_radius = ctx.pick_radius();
    c.group = ctx.group_id();
    ctx.submit(std::move(c));
}

void DimBreakUiCommand::selection_gesture(CommandContext& ctx) {
    if (select_.gesture(ctx) == SelectObjectsPhase::Result::Done) {
        after_select(ctx);
    }
}

void DimBreakUiCommand::after_select(CommandContext& ctx) {
    if (!ctx.has_selection()) {
        done_ = true;
        return;
    }
    step_ = Step::MultipleOption;
    ctx.set_prompt("Enter an option [Auto/Remove] <Auto>: ");
}

void DimBreakUiCommand::input(CommandContext& ctx, const std::string& text) {
    using Mode = core::DimBreakCommand::Mode;
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::Dim:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (u == "M" || u == "MULTIPLE") {
            step_ = Step::Multiple;
            select_.begin(ctx, "Select dimensions: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            pick_ = *p;
            step_ = Step::Object;
            ctx.set_prompt("Select object to break dimension or [Auto/Manual/Remove] <Auto>: ");
        }
        return;
    case Step::Object:
        if (t.empty()) {
            if (!broke_) {
                core::DimBreakCommand c; // Enter: Auto
                submit(ctx, c);
            }
            done_ = true;
            return;
        }
        if (!broke_ && (u == "A" || u == "AUTO")) {
            submit(ctx, core::DimBreakCommand{});
            done_ = true;
            return;
        }
        if (!broke_ && (u == "R" || u == "REMOVE")) {
            core::DimBreakCommand c;
            c.mode = Mode::Remove;
            submit(ctx, c);
            done_ = true;
            return;
        }
        if (!broke_ && (u == "M" || u == "MANUAL")) {
            step_ = Step::First;
            ctx.set_prompt("Specify first break point: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            core::DimBreakCommand c;
            c.mode = Mode::Object;
            c.object = *p;
            submit(ctx, c);
            broke_ = true;
            ctx.set_prompt("Select object to break dimension: ");
        }
        return;
    case Step::First:
        if (const auto p = read_point(ctx, text)) {
            first_ = *p;
            ctx.set_last_point(*p);
            step_ = Step::Second;
            ctx.set_prompt("Specify second break point: ");
        }
        return;
    case Step::Second:
        if (const auto p = read_point(ctx, text)) {
            core::DimBreakCommand c;
            c.mode = Mode::Manual;
            c.p1 = first_;
            c.p2 = *p;
            submit(ctx, c);
            done_ = true;
        }
        return;
    case Step::Multiple:
        if (select_.input(ctx, text) == SelectObjectsPhase::Result::Done) {
            after_select(ctx);
        }
        return;
    case Step::MultipleOption: {
        if (!u.empty() && u != "A" && u != "AUTO" && u != "R" && u != "REMOVE") {
            ctx.echo("Invalid option keyword.");
            ctx.set_prompt("Enter an option [Auto/Remove] <Auto>: ");
            return;
        }
        core::DimBreakCommand c;
        c.selection = true;
        c.mode = (u == "R" || u == "REMOVE") ? Mode::Remove : Mode::Auto;
        c.group = ctx.group_id();
        ctx.submit(std::move(c));
        done_ = true;
        return;
    }
    }
}

void DimJogLineCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::Pick:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (u == "R" || u == "REMOVE") {
            step_ = Step::Remove;
            ctx.set_prompt("Select jog to remove: ");
            return;
        }
        if (const auto p = read_point(ctx, text)) {
            pick_ = *p;
            step_ = Step::Location;
            ctx.set_prompt("Specify jog location (or press ENTER): ");
        }
        return;
    case Step::Location: {
        core::DimJogCommand c{pick_, ctx.pick_radius(), std::nullopt, false, ctx.group_id()};
        if (!t.empty()) {
            const auto p = read_point(ctx, text);
            if (!p) {
                return;
            }
            c.at = *p;
        }
        ctx.submit(c);
        done_ = true;
        return;
    }
    case Step::Remove:
        if (const auto p = read_point(ctx, text)) {
            ctx.submit(core::DimJogCommand{*p, ctx.pick_radius(), std::nullopt, true, ctx.group_id()});
            done_ = true;
        }
        return;
    }
}

void DimJogLineCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void NewViewCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (t.empty()) {
        done_ = true;
        return;
    }
    core::Vec2 center;
    double scale = 0.0;
    if (ctx.view() == nullptr || !ctx.view()->current_view(center, scale)) {
        ctx.echo("NEWVIEW needs the drawing window.");
        done_ = true;
        return;
    }
    ctx.submit(core::SaveNamedViewCommand{core::NamedView{t, center, scale}});
    done_ = true;
}

void HelpCommand::start(CommandContext& ctx) {
    if (ctx.view() == nullptr || !ctx.view()->open_help()) {
        ctx.echo("The command reference: https://github.com/MusaCAD/MusaCAD/blob/main/docs/COMMANDS.md");
    }
    done_ = true;
}

// ---------------------------------------------------------------------------
// SKETCH (issue #45)
// ---------------------------------------------------------------------------
void SketchCommand::start(CommandContext& ctx) {
    static constexpr const char* kTypes[] = {"Line", "Polyline", "Spline"};
    ctx.echo(std::string("Type = ") + kTypes[std::clamp(s_type_, 0, 2)] + "  Increment = " + fmt4(s_increment_) +
             "  Tolerance = " + fmt4(s_tolerance_));
    main_prompt(ctx);
}

void SketchCommand::main_prompt(CommandContext& ctx) {
    step_ = Step::Sketch;
    ctx.set_prompt("Specify sketch or [Type/increment/toLerance]: ");
}

void SketchCommand::freehand(CommandContext& ctx, const std::vector<core::Vec2>& path) {
    // The stroke's points, each at least the increment from the last one kept.
    std::vector<core::Vec2> pts;
    for (const core::Vec2& p : path) {
        if (pts.empty() || core::distance(pts.back(), p) >= s_increment_) {
            pts.push_back(p);
        }
    }
    if (pts.size() >= 2 && core::distance(pts.back(), path.back()) > 1e-12) {
        pts.push_back(path.back()); // it ends where the pen came up
    }
    if (pts.size() < 2) {
        return;
    }
    if (s_type_ == 1) {
        ctx.submit(core::AddPolylineCommand{pts, false, ctx.group_id()});
    } else if (s_type_ == 2 && pts.size() >= 3) {
        ctx.submit(core::AddSplineCommand{pts, 3, ctx.group_id()});
    } else {
        for (std::size_t i = 1; i < pts.size(); ++i) {
            ctx.submit(core::AddLineCommand{pts[i - 1], pts[i], ctx.group_id()});
        }
    }
    ++strokes_;
}

void SketchCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::Sketch:
        if (t.empty()) {
            ctx.echo(std::to_string(strokes_) + (strokes_ == 1 ? " sketch stroke recorded." : " sketch strokes recorded."));
            done_ = true;
        } else if (u == "T" || u == "TYPE") {
            step_ = Step::Type;
            static constexpr const char* kTypes[] = {"Line", "Polyline", "Spline"};
            ctx.set_prompt(std::string("Enter sketch type [Line/Polyline/Spline] <") + kTypes[std::clamp(s_type_, 0, 2)] +
                           ">: ");
        } else if (u == "I" || u == "INCREMENT") {
            step_ = Step::Increment;
            ctx.set_prompt("Specify sketch increment <" + fmt4(s_increment_) + ">: ");
        } else if (u == "L" || u == "TOLERANCE") {
            step_ = Step::Tolerance;
            ctx.set_prompt("Specify spline fit tolerance <" + fmt4(s_tolerance_) + ">: ");
        } else {
            ctx.echo("Press and drag to sketch, or enter Type, increment or toLerance.");
        }
        return;
    case Step::Type:
        if (u == "L" || u == "LINE") {
            s_type_ = 0;
        } else if (u == "P" || u == "POLYLINE") {
            s_type_ = 1;
        } else if (u == "S" || u == "SPLINE") {
            s_type_ = 2;
        } else if (!t.empty()) {
            ctx.echo("Enter Line, Polyline or Spline.");
            return;
        }
        main_prompt(ctx);
        return;
    case Step::Increment:
    case Step::Tolerance: {
        if (!t.empty()) {
            const std::optional<double> v = parse_number(t);
            if (!v || *v <= 0.0) {
                ctx.echo("Requires a positive value.");
                return;
            }
            (step_ == Step::Increment ? s_increment_ : s_tolerance_) = *v;
        }
        main_prompt(ctx);
        return;
    }
    }
}

void SketchCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

// ---------------------------------------------------------------------------
// LAYERP / LAYERPMODE, LAYERSTATE (issue #69)
// ---------------------------------------------------------------------------
void LayerPreviousCommand::start(CommandContext& ctx) {
    if (!mode_) {
        ctx.submit(core::LayerPreviousCommand{});
        done_ = true;
        return;
    }
    ctx.set_prompt("Enter LAYERP mode [ON/OFF]: ");
}

void LayerPreviousCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string u = upper(trimmed(text));
    if (u.empty()) {
        ctx.submit(core::SetLayerpModeCommand{}); // says which
    } else if (u == "ON") {
        ctx.submit(core::SetLayerpModeCommand{true});
    } else if (u == "OFF") {
        ctx.submit(core::SetLayerpModeCommand{false});
    } else {
        ctx.echo("Enter ON or OFF.");
        return;
    }
    done_ = true;
}

void LayerPreviousCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void LayerStateCommand::options(CommandContext& ctx) {
    step_ = Step::Option;
    ctx.set_prompt("Enter an option [?/Save/Restore/Delete/Name]: ");
}

void LayerStateCommand::start(CommandContext& ctx) {
    if (dialog_ && ctx.view() != nullptr && ctx.view()->layer_states_manager()) {
        done_ = true; // the Layer States Manager does the rest
        return;
    }
    options(ctx);
}

void LayerStateCommand::input(CommandContext& ctx, const std::string& text) {
    using Op = core::LayerStateCommand::Op;
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    switch (step_) {
    case Step::Option:
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (u == "?") {
            ctx.submit(core::LayerStateCommand{Op::List, {}, {}});
            options(ctx);
            return;
        }
        if (u == "S" || u == "SAVE") {
            op_ = Op::Save;
            ctx.set_prompt("Enter new layer state name: ");
        } else if (u == "R" || u == "RESTORE") {
            op_ = Op::Restore;
            ctx.set_prompt("Enter name of layer state to restore or [?]: ");
        } else if (u == "D" || u == "DELETE") {
            op_ = Op::Delete;
            ctx.set_prompt("Enter name of layer state to delete or [?]: ");
        } else if (u == "N" || u == "NAME") {
            op_ = Op::Rename;
            ctx.set_prompt("Enter name of layer state to rename or [?]: ");
        } else {
            ctx.echo("Invalid option keyword.");
            options(ctx);
            return;
        }
        step_ = Step::Name;
        return;
    case Step::Name:
        if (t.empty()) {
            options(ctx);
            return;
        }
        if (t == "?" && op_ != Op::Save) {
            ctx.submit(core::LayerStateCommand{Op::List, {}, {}});
            return; // the same prompt again
        }
        if (op_ == Op::Rename) {
            from_ = t;
            step_ = Step::NewName;
            ctx.set_prompt("Enter new layer state name: ");
            return;
        }
        ctx.submit(core::LayerStateCommand{op_, t, {}});
        options(ctx);
        return;
    case Step::NewName:
        if (!t.empty()) {
            ctx.submit(core::LayerStateCommand{Op::Rename, from_, t});
        }
        options(ctx);
        return;
    }
}

void LayerStateCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

std::unique_ptr<ICommand> CommandContext::make_command(const std::string& alias) {
    (void)alias;
    return nullptr; // only a context with a command registry makes commands
}

void MultipleCommand::again(CommandContext& ctx) {
    sub_ = ctx.make_command(alias_);
    if (!sub_) {
        done_ = true;
        return;
    }
    (void)ctx.new_group(); // each repetition its own undo step, as a command typed again
    ctx.echo("Command: " + sub_->name());
    sub_->start(ctx);
    if (sub_->done()) {
        done_ = true; // a command that ends at once (a setting) would spin: one run is enough
    }
}

void MultipleCommand::input(CommandContext& ctx, const std::string& text) {
    if (!sub_) {
        const std::string t = upper(trimmed(text));
        if (t.empty()) {
            done_ = true;
            return;
        }
        if (!ctx.make_command(t)) {
            ctx.echo("Unknown command \"" + t + "\".");
            start(ctx);
            return;
        }
        alias_ = t;
        again(ctx);
        return;
    }
    sub_->input(ctx, text);
    if (sub_->done()) {
        again(ctx);
    }
}

void MultipleCommand::hover(CommandContext& ctx, std::optional<core::EntityKind> kind) {
    if (sub_) {
        sub_->hover(ctx, kind);
    }
}

void MultipleCommand::selection_gesture(CommandContext& ctx) {
    if (sub_) {
        sub_->selection_gesture(ctx);
        if (sub_->done()) {
            again(ctx);
        }
    }
}

void MultipleCommand::freehand(CommandContext& ctx, const std::vector<core::Vec2>& path) {
    if (sub_) {
        sub_->freehand(ctx, path);
        if (sub_->done()) {
            again(ctx);
        }
    }
}

void MultipleCommand::cancel(CommandContext& ctx) {
    if (sub_) {
        sub_->cancel(ctx);
        sub_.reset();
    } else {
        ctx.echo("*Cancel*");
    }
    done_ = true;
}

void PointDisplayCommand::start(CommandContext& ctx) {
    mode_ = ctx.pdmode();
    if (var_ == Var::Size) {
        ask_size(ctx);
        return;
    }
    ctx.set_prompt(std::string(var_ == Var::Both ? "Enter point display mode (PDMODE) <" : "Enter new value for PDMODE <") +
                   std::to_string(ctx.pdmode()) + ">: ");
}

void PointDisplayCommand::ask_size(CommandContext& ctx) {
    sizing_ = true;
    ctx.set_prompt(std::string(var_ == Var::Both ? "Enter point size (PDSIZE) <" : "Enter new value for PDSIZE <") +
                   fmt4(ctx.pdsize()) + ">: ");
}

void PointDisplayCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (!sizing_) {
        if (!t.empty()) {
            const std::optional<double> v = parse_number(t);
            const bool whole = v && *v == std::floor(*v) && *v >= 0.0 && *v <= 100.0;
            const int m = whole ? static_cast<int>(*v) : -1;
            if (!whole || (m & 7) > 4 || (m & 24) != 0) { // 0..4, with 32 and 64 added
                ctx.echo("Requires 0, 1, 2, 3 or 4, with 32 or 64 added for a circle or a square round it.");
                return;
            }
            mode_ = static_cast<std::uint8_t>(m);
        }
        if (var_ == Var::Mode) {
            ctx.submit(core::SetPointDisplayCommand{mode_, ctx.pdsize()});
            done_ = true;
            return;
        }
        ask_size(ctx);
        return;
    }
    double size = ctx.pdsize();
    if (!t.empty()) {
        const std::optional<double> v = parse_number(t);
        if (!v) {
            ctx.echo("Requires a size: drawing units, or 0 or less for a size on screen.");
            return;
        }
        size = *v;
    }
    ctx.submit(core::SetPointDisplayCommand{mode_, size});
    done_ = true;
}

void PointDisplayCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

void UnitsDefaultCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Enter new value for " + name() + " <" + std::to_string(target_ ? s_target_ : s_source_) + ">: ");
}

void UnitsDefaultCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (!t.empty()) {
        double v = 0.0;
        if (!parse_number(t, v) || v != std::floor(v) || v < 0.0 || v > 24.0) {
            ctx.echo("Requires an integer between 0 and 24.");
            return;
        }
        (target_ ? s_target_ : s_source_) = static_cast<int>(v);
        ctx.submit(core::SetUnitsDefaultsCommand{static_cast<std::uint8_t>(s_source_), static_cast<std::uint8_t>(s_target_)});
    }
    done_ = true;
}

void TextSizeCommand::start(CommandContext& ctx) {
    ctx.set_prompt("Enter new value for TEXTSIZE <" + fmt4(TextCommand::text_size()) + ">: ");
}

void TextSizeCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    if (!t.empty()) {
        double v = 0.0;
        if (!parse_number(t, v) || v <= 0.0) {
            ctx.echo("Requires a positive value.");
            return;
        }
        TextCommand::set_text_size(v);
    }
    done_ = true;
}

void BaseCommand::start(CommandContext& ctx) {
    ctx.clear_last_point();
    const core::Vec2 b = ctx.insbase();
    ctx.set_prompt("Enter base point <" + fmt4(b.x) + "," + fmt4(b.y) + ",0.0000>: ");
}

void BaseCommand::input(CommandContext& ctx, const std::string& text) {
    if (trimmed(text).empty()) {
        done_ = true; // kept as it is
        return;
    }
    if (const auto p = read_point(ctx, text)) {
        ctx.submit(core::SetInsBaseCommand{*p});
        done_ = true;
    }
}

void LimitsCommand::start(CommandContext& ctx) {
    const CommandContext::Limits lim = ctx.limits();
    ctx.echo(std::string("Reset Model space limits (limits checking ") + (lim.on ? "on" : "off") + "):");
    ctx.set_prompt("Specify lower left corner or [ON/OFF] <" + fmt4(lim.min.x) + "," + fmt4(lim.min.y) + ">: ");
}

void LimitsCommand::input(CommandContext& ctx, const std::string& text) {
    const std::string t = trimmed(text);
    const std::string u = upper(t);
    const CommandContext::Limits lim = ctx.limits();
    if (!second_) {
        if (u == "ON" || u == "OFF") {
            core::SetLimitsCommand c;
            c.on = u == "ON";
            c.corners = false;
            ctx.submit(c);
            done_ = true;
            return;
        }
        lower_ = lim.min;
        if (!t.empty()) {
            const CoordParse p = parse_coordinate(t, std::nullopt);
            if (!p.ok) {
                ctx.echo(p.error);
                return;
            }
            lower_ = p.point;
        }
        second_ = true;
        ctx.set_prompt("Specify upper right corner <" + fmt4(lim.max.x) + "," + fmt4(lim.max.y) + ">: ");
        return;
    }
    core::Vec2 upper_pt = lim.max;
    if (!t.empty()) {
        const CoordParse p = parse_coordinate(t, std::nullopt);
        if (!p.ok) {
            ctx.echo(p.error);
            return;
        }
        upper_pt = p.point;
    }
    if (upper_pt.x <= lower_.x || upper_pt.y <= lower_.y) {
        ctx.echo("The upper right corner must be above and to the right of the lower left.");
        return;
    }
    ctx.submit(core::SetLimitsCommand{lower_, upper_pt, lim.on, true});
    done_ = true;
}

void LimitsCommand::cancel(CommandContext& ctx) {
    ctx.echo("*Cancel*");
    done_ = true;
}

} // namespace musacad::command

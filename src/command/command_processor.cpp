// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/command/command_processor.hpp"

#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "musacad/command/command.hpp"
#include "musacad/command/coordinate.hpp"
#include "musacad/command/snap_keywords.hpp"
#include "musacad/core/crash_report.hpp"
#include "musacad/core/units.hpp"

namespace musacad::command {

namespace {

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

std::string upper(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string first_token(const std::string& s) {
    const std::string t = trimmed(s);
    const auto sp = t.find_first_of(" \t");
    return sp == std::string::npos ? t : t.substr(0, sp);
}

} // namespace

CommandProcessor::CommandProcessor(CommandSink sink, ViewControl* view, CommandOutput& output)
    : sink_(std::move(sink)), view_(view), output_(output), registry_(CommandRegistry::make_default()) {
    show_ready();
}

void CommandProcessor::start_macro(const std::string& alias, std::vector<std::string> tokens) {
    macro_.clear();
    macro_waiting_ = false;
    start_command(alias);
    if (!active_) {
        return; // the command ended at once (or the alias is unknown): nothing to feed
    }
    macro_.assign(tokens.begin(), tokens.end());
    run_macro();
}

void CommandProcessor::run_macro() {
    // Feed tokens until one asks to wait for the user, the queue empties, or the
    // command ends (a rejected token ends the macro too: the prompt then stands as is).
    while (active_ && !macro_.empty()) {
        const std::string token = macro_.front();
        macro_.pop_front();
        if (token == "\\") {
            macro_waiting_ = true;
            return;
        }
        active_->input(*this, token);
        finalize_if_done();
    }
    if (!active_) {
        macro_.clear();
        macro_waiting_ = false;
    }
}

void CommandProcessor::submit_line(const std::string& text) {
    core::crash::note((feeding_pick_ ? "pick " : "> ") + text);
    // History: record every non-empty submitted line (newest last) so the bottom bar
    // AND the on-canvas command-entry box recall from one place. Reset the recall
    // cursor to the newest on each new submission.
    if (!trimmed(text).empty()) {
        if (history_.empty() || history_.back() != text) {
            history_.push_back(text);
        }
    }
    history_cursor_ = static_cast<int>(history_.size());

    if (active_) {
        if (snap_override_ && !feeding_pick_) {
            // Something typed instead of the pick: the override is given up. Enter only
            // takes the prompt back to where it was.
            end_snap_override(true);
            if (trimmed(text).empty()) {
                return;
            }
        }
        if (filter_.kind != PointFilter::Kind::None) {
            feed_filter(text);
            return;
        }
        active_->input(*this, text);
        finalize_if_done();
        if (macro_waiting_ && active_) {
            macro_waiting_ = false; // the user's input released the macro: feed on
            run_macro();
        } else if (!active_) {
            macro_.clear();
            macro_waiting_ = false;
        }
        return;
    }

    const std::string token = first_token(text);
    if (token.empty()) {
        if (!last_command_alias_.empty()) {
            start_command(last_command_alias_); // ENTER repeats last command
        }
        return;
    }
    start_command(token);
}

// ---------------------------------------------------------------------------
// One-time object snaps and point filters
// ---------------------------------------------------------------------------
bool CommandProcessor::try_snap_override(const std::string& text) {
    std::uint32_t mask = 0;
    std::string label;
    std::string joiner;
    if (!parse_snap_list(text, mask, &label, &joiner)) {
        return false;
    }
    const std::string at = snap_override_ ? snap_override_prompt_ : current_prompt_;
    snap_override_ = mask;
    snap_override_name_ = label;
    snap_override_prompt_ = at;
    // AutoCAD writes the mode after the prompt: "Specify next point: _endp of".
    set_prompt(at + "_" + lower(trimmed(text)) + (mask == 0 ? " " : " " + joiner + " "));
    if (view_ != nullptr) {
        view_->snap_override_changed();
    }
    return true;
}

void CommandProcessor::end_snap_override(bool restore_prompt) {
    if (!snap_override_) {
        return;
    }
    snap_override_.reset();
    if (restore_prompt) {
        set_prompt(snap_override_prompt_);
    }
    if (view_ != nullptr) {
        view_->snap_override_changed();
    }
}

bool CommandProcessor::point_modifier(const std::string& text) {
    if (!active_) {
        return false;
    }
    const std::string u = upper(trimmed(text));
    if (u.size() < 2) {
        return false;
    }
    if (filter_.kind == PointFilter::Kind::None) {
        if (u == "FROM" || u == "FRO") {
            begin_filter(PointFilter::Kind::From);
            return true;
        }
        if (u == "M2P" || u == "MTP") {
            begin_filter(PointFilter::Kind::MidBetween);
            return true;
        }
        if (u == "TT") {
            begin_filter(PointFilter::Kind::TempTrack);
            return true;
        }
        if (u == "TK" || u == "TRACK" || u == "TRA") {
            begin_filter(PointFilter::Kind::Track);
            return true;
        }
    }
    return try_snap_override(u);
}

void CommandProcessor::begin_filter(PointFilter::Kind kind) {
    filter_ = PointFilter{kind, 0, {}, current_prompt_, last_point_};
    switch (kind) {
    case PointFilter::Kind::From:
        set_prompt(filter_.prompt + "_from Base point: ");
        break;
    case PointFilter::Kind::MidBetween:
        set_prompt(filter_.prompt + "_m2p First point of mid: ");
        break;
    case PointFilter::Kind::TempTrack:
        set_prompt(filter_.prompt + "_tt Specify temporary OTRACK point: ");
        break;
    case PointFilter::Kind::Track:
        set_prompt(filter_.prompt + "_tk First tracking point: ");
        break;
    case PointFilter::Kind::None:
        break;
    }
}

void CommandProcessor::end_filter() {
    if (filter_.kind == PointFilter::Kind::None) {
        return;
    }
    last_point_ = filter_.last_point;
    set_prompt(filter_.prompt);
    filter_ = PointFilter{};
}

void CommandProcessor::deliver_point(core::Vec2 p) {
    end_filter();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g,%.10g", p.x, p.y);
    active_->input(*this, buf);
    finalize_if_done();
    if (macro_waiting_ && active_) {
        macro_waiting_ = false;
        run_macro();
    } else if (!active_) {
        macro_.clear();
        macro_waiting_ = false;
    }
}

void CommandProcessor::feed_filter(const std::string& text) {
    using Kind = PointFilter::Kind;
    const std::string t = trimmed(text);
    if (t.empty()) {
        if (filter_.kind == Kind::Track && filter_.stage >= 1) {
            deliver_point(filter_.a); // Enter ends tracking: the last point reached
        } else {
            end_filter(); // given up: the command's prompt stands again
        }
        return;
    }
    if (try_snap_override(upper(t))) {
        return; // a snap for the filter's own point
    }
    // The point, read as any point is (relative input from the last point given, a
    // bare distance along the cursor's bearing).
    std::optional<double> bearing;
    if (last_point_ && cursor_world_ && core::distance(*last_point_, *cursor_world_) > 1e-12) {
        bearing = std::atan2(cursor_world_->y - last_point_->y, cursor_world_->x - last_point_->x);
    }
    const CoordParse parsed = parse_coordinate(t, last_point_, bearing);
    if (!parsed.ok) {
        output_.append_line(parsed.error);
        return;
    }
    const core::Vec2 p = parsed.point;
    switch (filter_.kind) {
    case Kind::From:
        if (filter_.stage == 0) {
            filter_.a = p;
            filter_.stage = 1;
            last_point_ = p; // the offset is measured from the base point
            set_prompt(filter_.prompt + "_from Base point: <Offset>: ");
            return;
        }
        deliver_point(p);
        return;
    case Kind::MidBetween:
        if (filter_.stage == 0) {
            filter_.a = p;
            filter_.stage = 1;
            last_point_ = p;
            set_prompt(filter_.prompt + "_m2p First point of mid: Second point of mid: ");
            return;
        }
        deliver_point((filter_.a + p) * 0.5);
        return;
    case Kind::TempTrack: {
        const std::vector<core::Vec2> kept = track_points_;
        end_filter();
        track_points_ = kept;
        track_points_.push_back(p);
        return;
    }
    case Kind::Track: {
        if (filter_.stage == 0) {
            filter_.a = p;
        } else {
            // Each move runs along one axis from the point before it.
            const core::Vec2 d = p - filter_.a;
            filter_.a = std::abs(d.x) >= std::abs(d.y) ? core::Vec2{p.x, filter_.a.y}
                                                       : core::Vec2{filter_.a.x, p.y};
        }
        filter_.stage = 1;
        last_point_ = filter_.a;
        set_prompt(filter_.prompt + "_tk Next point (Press ENTER to end tracking): ");
        return;
    }
    case Kind::None:
        return;
    }
}

void CommandProcessor::drop_point_modifiers() {
    filter_ = PointFilter{};
    track_points_.clear();
    acquired_.clear();
    hover_.reset();
    tracked_ = TrackResult{};
    if (snap_override_) {
        snap_override_.reset();
        if (view_ != nullptr) {
            view_->snap_override_changed();
        }
    }
}

void CommandProcessor::cancel() {
    macro_.clear();
    macro_waiting_ = false;
    drop_point_modifiers();
    if (active_) {
        active_->cancel(*this);
        active_.reset();
        preview_ = PreviewSpec{};
        show_ready();
        return;
    }
    // ESC while idle clears the current selection.
    submit(core::ClearSelectionCommand{});
}

bool CommandProcessor::asking_for_point() const {
    if (!active_ || !temp_overrides_ || active_->in_selection_phase() || active_->wants_selection() ||
        active_->free_text()) {
        return false;
    }
    const std::string prompt = lower(current_prompt_);
    if (prompt.rfind("enter", 0) == 0) {
        return false; // a value, a name or an option is being asked for
    }
    for (const char* word : {"point", "corner", "location", "endpoint"}) {
        if (prompt.find(word) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void CommandProcessor::set_override(Override which, bool held) {
    overrides_[static_cast<std::size_t>(which)] = held;
    if (view_ != nullptr) {
        view_->snap_override_changed(); // the snaps in force may have changed with it
    }
}

void CommandProcessor::clear_overrides() {
    bool any = false;
    for (bool& held : overrides_) {
        any = any || held;
        held = false;
    }
    if (any && view_ != nullptr) {
        view_->snap_override_changed();
    }
}

bool CommandProcessor::effective_osnap(bool running) const noexcept {
    if (override_held(Override::DisableAll)) {
        return false;
    }
    if (override_held(Override::Endpoint) || override_held(Override::Midpoint) ||
        override_held(Override::Center)) {
        return true; // a snap held for the pick works with OSNAP off
    }
    return running != override_held(Override::Osnap);
}

void CommandProcessor::note_snap(std::optional<core::Vec2> snap, const std::string& label, double now) {
    constexpr double kRest = 0.2; // seconds on a point before it is acquired
    const bool tracking = otrack_ != override_held(Override::Otrack);
    if (!active_ || !tracking || override_held(Override::DisableAll) || !snap) {
        hover_.reset();
        return;
    }
    if (hover_ && core::distance(hover_->at, *snap) < 1e-9) {
        if (!hover_->taken && now - hover_->since >= kRest) {
            hover_->taken = true;
            const auto it = std::find_if(acquired_.begin(), acquired_.end(), [&](const TrackPoint& p) {
                return core::distance(p.at, *snap) < 1e-9;
            });
            if (it != acquired_.end()) {
                acquired_.erase(it); // resting on it again lets it go
            } else {
                acquired_.push_back(TrackPoint{*snap, label});
                if (acquired_.size() > 7) {
                    acquired_.erase(acquired_.begin());
                }
            }
        }
        return;
    }
    hover_ = Hover{*snap, label, now, false};
}

std::string CommandProcessor::tracking_tooltip() const {
    if (tracked_.hits.empty()) {
        return {};
    }
    const auto one = [&](const TrackHit& h, bool with_length) {
        std::string text = h.polar ? (h.relative ? "Relative Polar" : "Polar")
                                   : (h.label.empty() ? std::string("Track point") : h.label);
        text += ": ";
        if (with_length) {
            text += core::units::format_length(h.length, units_) + " ";
        }
        std::string angle = core::units::format_angle(h.angle, units_);
        if (units_.angular == core::AngleFormat::DecimalDegrees) {
            angle += "\xC2\xB0"; // the degree sign, UTF-8
        }
        return text + "< " + angle;
    };
    if (tracked_.hits.size() == 1) {
        return one(tracked_.hits[0], true);
    }
    return one(tracked_.hits[0], false) + ", " + one(tracked_.hits[1], false);
}

core::Vec2 CommandProcessor::resolve_constraints(core::Vec2 world) const {
    core::Vec2 p = world;
    tracked_ = TrackResult{};
    tracked_.point = p;
    if (override_held(Override::DisableAll)) {
        return p; // Shift + D: the bare cursor
    }
    // PolarSnap takes the snap grid's place while it is the snap type.
    TrackingSettings settings = tracking_;
    settings.polar_snap = grid_snap_ && polar_snap_type_;
    if (grid_snap_ && grid_spacing_ > 0.0 && !settings.polar_snap) {
        p.x = std::round(p.x / grid_spacing_) * grid_spacing_;
        p.y = std::round(p.y / grid_spacing_) * grid_spacing_;
    }
    const bool ortho = ortho_ != override_held(Override::Ortho);
    const bool polar = !ortho && (polar_ != override_held(Override::Polar));
    const bool otrack = otrack_ != override_held(Override::Otrack);
    // The points tracking runs through: the temporary ones (TT) always, the acquired
    // ones while OTRACK is on.
    std::vector<TrackPoint> points;
    for (const core::Vec2& tp : track_points_) {
        points.push_back(TrackPoint{tp, "Temporary track point"});
    }
    if (otrack) {
        points.insert(points.end(), acquired_.begin(), acquired_.end());
    }
    // TK moves along one axis at a time, whatever ORTHO says.
    const bool tracking = filter_.kind == PointFilter::Kind::Track && filter_.stage >= 1;
    if (last_point_ && (ortho || tracking)) {
        const core::Vec2 d = p - *last_point_;
        const bool y_held = std::abs(d.x) >= std::abs(d.y); // running along x
        p = y_held ? core::Vec2{p.x, last_point_->y} : core::Vec2{last_point_->x, p.y};
        // The ortho direction stops where it crosses a tracking path.
        if (pick_radius_ > 0.0) {
            for (const TrackPoint& tp : points) {
                if (y_held && std::abs(p.x - tp.at.x) <= pick_radius_) {
                    p.x = tp.at.x;
                    break;
                }
                if (!y_held && std::abs(p.y - tp.at.y) <= pick_radius_) {
                    p.y = tp.at.y;
                    break;
                }
            }
        }
        tracked_.point = p;
        return p;
    }
    tracked_ = track(settings, polar, !points.empty(), p, last_point_, last_direction_, points,
                     pick_radius_);
    return tracked_.point;
}

core::Vec2 CommandProcessor::resolve_pick(core::Vec2 world, std::optional<core::Vec2> snap) const {
    // OSNAP wins; otherwise apply ortho/polar/grid-snap to the free cursor point.
    if (snap) {
        tracked_ = TrackResult{}; // on an object snap there is no path in hand
        tracked_.point = *snap;
        return *snap;
    }
    return resolve_constraints(world);
}

void CommandProcessor::submit_freehand(const std::vector<core::Vec2>& path) {
    if (active_ == nullptr || !active_->wants_freehand() || path.size() < 2) {
        return;
    }
    core::crash::note("freehand path of " + std::to_string(path.size()) + " points");
    active_->freehand(*this, path);
    finalize_if_done();
}

void CommandProcessor::pick_point(core::Vec2 world, std::optional<core::Vec2> snap) {
    if (!active_) {
        return; // nothing to receive a point
    }
    if (active_->wants_selection()) {
        submit(core::ErasePickCommand{world, pick_radius_, current_group_});
        output_.append_line("Selected object near picked point.");
        finalize_if_done();
        return;
    }
    if (snap_override_) {
        // The snap typed for this pick decides it: no such point under the cursor is an
        // invalid pick (the prompt asks again), and NON means no snap at all.
        const std::uint32_t mask = *snap_override_;
        const std::string what = snap_override_name_;
        end_snap_override(true);
        if (mask == 0) {
            snap.reset();
        } else if (!snap) {
            output_.append_line("No " + what + " found for specified point.");
            output_.append_line("Invalid point.");
            return;
        }
    }
    const core::Vec2 p = resolve_pick(world, snap);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g,%.10g", p.x, p.y);
    feeding_pick_ = true;
    submit_line(buf); // feed as an absolute coordinate to the active command
    feeding_pick_ = false;
}

std::string CommandProcessor::status_modes() const {
    char grid[64];
    std::snprintf(grid, sizeof(grid), "%.4g", grid_spacing_);
    std::string out = std::string("Snap resolution is       X: ") + grid + "  Y: " + grid +
                      (grid_snap_ ? "  (on)" : "  (off)");
    out += std::string("\nGrid spacing is          X: ") + grid + "  Y: " + grid;
    out += std::string("\nOrtho ") + (ortho_ ? "on" : "off") + "   Polar " + (polar_ ? "on" : "off") +
           "   Snap " + (grid_snap_ ? "on" : "off");
    return out;
}

void CommandProcessor::notify_selection_gesture() {
    if (active_ && active_->in_selection_phase()) {
        active_->selection_gesture(*this);
        finalize_if_done();
    }
}

void CommandProcessor::set_hovered_kind(std::optional<core::EntityKind> kind) {
    if (kind == hovered_kind_) {
        return; // only react to changes
    }
    hovered_kind_ = kind;
    if (active_) {
        active_->hover(*this, hovered_kind_);
    }
}

void CommandProcessor::undo() {
    submit(core::UndoLastGroupCommand{});
    output_.append_line("Undo");
}

void CommandProcessor::redo() {
    submit(core::RedoLastGroupCommand{});
    output_.append_line("Redo");
}

void CommandProcessor::delete_selection() {
    current_group_ = ++group_counter_;
    submit(core::EraseSelectionCommand{current_group_});
    output_.append_line("Erased selection.");
}

void CommandProcessor::start_command(const std::string& alias) {
    drop_point_modifiers();
    std::unique_ptr<ICommand> cmd = registry_.create(alias);
    if (!cmd) {
        output_.append_line("Unknown command \"" + alias + "\".");
        show_ready();
        return;
    }
    // AutoCAD: starting a new command implicitly cancels the one in progress. Run its
    // cancel() cleanly FIRST (so its rubber-band/preview is dropped and any pending op-log
    // group is discarded) rather than letting the move below destroy it mid-flight. The
    // current selection is left untouched -- cancel() never clears it.
    if (active_) {
        active_->cancel(*this);
        active_.reset();
        preview_ = PreviewSpec{};
    }
    current_group_ = ++group_counter_;
    last_command_alias_ = alias;
    active_ = std::move(cmd);
    output_.append_line("Command: " + active_->name());
    active_->start(*this);
    finalize_if_done();
}

void CommandProcessor::finalize_if_done() {
    if (active_ && active_->done()) {
        drop_point_modifiers();
        active_.reset();
        preview_ = PreviewSpec{}; // drop any preview when the command ends
        show_ready();
    }
}

void CommandProcessor::show_ready() {
    current_prompt_ = "Command: ";
    output_.set_prompt(current_prompt_);
}

std::string CommandProcessor::history_recall(int dir) {
    if (history_.empty()) {
        return {};
    }
    // dir +1 = older, -1 = newer. Cursor in [0, size]; size == "past the newest" (blank).
    history_cursor_ = std::clamp(history_cursor_ - dir, 0, static_cast<int>(history_.size()));
    if (history_cursor_ >= static_cast<int>(history_.size())) {
        return {};
    }
    return history_[static_cast<std::size_t>(history_cursor_)];
}

void CommandProcessor::echo(const std::string& line) { output_.append_line(line); }

void CommandProcessor::set_prompt(const std::string& prompt) {
    current_prompt_ = prompt;
    output_.set_prompt(prompt);
}

void CommandProcessor::submit(core::Command command) {
    if (sink_) {
        sink_(std::move(command));
    }
}

} // namespace musacad::command

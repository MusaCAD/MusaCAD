// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "musacad/command/command_context.hpp"
#include "musacad/command/command_registry.hpp"
#include "musacad/core/command.hpp"

namespace musacad::command {

class ICommand;

/// Drives the command-line REPL on the UI thread: looks up commands in the alias
/// table, runs the active command's state machine, manages the per-invocation
/// undo group, ENTER-repeats-last, and ESC-cancel. Geometry effects leave only
/// as core::Command messages through the sink (the UI->geometry MPSC queue);
/// the processor never touches the GeometryStore.
class CommandProcessor : public CommandContext {
public:
    using CommandSink = std::function<void(core::Command)>;

    CommandProcessor(CommandSink sink, ViewControl* view, CommandOutput& output);

    /// Handles one submitted line (ENTER). Empty line repeats the last command
    /// when idle, or is delivered to the active command (e.g. to end LINE).
    void submit_line(const std::string& text);

    /// Starts a command by alias (ribbon clicks, shortcuts, programmatic invocation).
    /// An unambiguous command start: any command in progress is cancelled cleanly first
    /// (its preview dropped, the selection preserved), then the new command begins.
    void start_command(const std::string& alias);

    /// Cancels the active command (ESC).
    void cancel();

    /// Starts `alias` and feeds it `tokens` the way an AutoCAD ribbon macro does: each
    /// token is submitted as a typed line, and the token "\\" (a backslash) waits for the
    /// user's next input before the rest goes on -- so "Circle > 2-Point" is {"2P"} and
    /// "Arc > Start, Center, End" is {"\\", "C"}. The queue empties when the command ends
    /// or is cancelled.
    void start_macro(const std::string& alias, std::vector<std::string> tokens);
    [[nodiscard]] bool macro_pending() const noexcept { return !macro_.empty(); }

    /// Delivers a cursor pick (a viewport click). `snap` is the active OSNAP
    /// point if any (it wins over `world`). For a selection command (ERASE) this
    /// becomes an ErasePick; otherwise it feeds a coordinate (after ortho/polar/
    /// grid-snap resolution) to the active command.
    void pick_point(core::Vec2 world, std::optional<core::Vec2> snap);

    /// The object snap typed for the next pick, if any: its mask stands in for the
    /// running snaps for that pick (0 = NON, no snap at all).
    [[nodiscard]] std::optional<std::uint32_t> snap_override() const noexcept {
        if (snap_override_) {
            return snap_override_;
        }
        std::uint32_t held = 0;
        if (override_held(Override::Endpoint)) {
            held |= core::snap_bit(core::SnapType::Endpoint);
        }
        if (override_held(Override::Midpoint)) {
            held |= core::snap_bit(core::SnapType::Midpoint);
        }
        if (override_held(Override::Center)) {
            held |= core::snap_bit(core::SnapType::Center);
        }
        if (held != 0) {
            return held;
        }
        if (override_held(Override::DisableAll)) {
            return 0u;
        }
        return std::nullopt;
    }
    /// The temporary tracking points (TT) in force: the cursor locks onto the horizontal
    /// and the vertical through each.
    [[nodiscard]] const std::vector<core::Vec2>& track_points() const noexcept { return track_points_; }
    /// True while FROM, M2P, TT or TK is gathering its points.
    [[nodiscard]] bool point_filter_active() const noexcept {
        return filter_.kind != PointFilter::Kind::None;
    }
    bool point_modifier(const std::string& text) override;

    /// Undo / redo the last command group (Ctrl+Z / Ctrl+Y).
    void undo();
    void redo();

    /// Erase the current selection as one undo group (Delete key).
    void delete_selection();

    /// The active command's previous point, if any -- enables deferred OSNAPs
    /// (perpendicular/tangent) on the render-side preview path.
    [[nodiscard]] std::optional<core::Vec2> active_from() const {
        return active_ ? last_point_ : std::nullopt;
    }

    // Drawing-aid modes (set from the UI toggles).
    void set_ortho(bool on) { ortho_ = on; }
    void set_polar(bool on) { polar_ = on; }
    /// Object snap tracking (F11). Switching it off lets the acquired points go.
    void set_otrack(bool on) {
        otrack_ = on;
        if (!on) {
            acquired_.clear();
            hover_.reset();
        }
    }
    [[nodiscard]] bool polar_tracking() const override { return polar_; }
    [[nodiscard]] bool object_snap_tracking() const override { return otrack_; }
    /// As a command switches them (AUTOSNAP): the status bar follows.
    void set_polar_tracking(bool on) override {
        polar_ = on;
        if (on) {
            ortho_ = false; // the two exclude each other, as F8 and F10 do
        }
        if (view_ != nullptr) {
            view_->set_polar_mode(on);
        }
    }
    void set_object_snap_tracking(bool on) override {
        set_otrack(on);
        if (view_ != nullptr) {
            view_->set_otrack_mode(on);
        }
    }
    [[nodiscard]] bool temp_overrides() const override { return temp_overrides_; }
    void set_temp_overrides(bool on) override {
        temp_overrides_ = on;
        if (!on) {
            clear_overrides();
        }
    }
    [[nodiscard]] TrackingSettings tracking_settings() const override { return tracking_; }
    void set_tracking_settings(const TrackingSettings& settings) override { tracking_ = settings; }
    [[nodiscard]] int snap_type() const override { return polar_snap_type_ ? 1 : 0; }
    void set_snap_type(int type) override { polar_snap_type_ = type == 1; }

    /// The keys AutoCAD reads as temporary overrides while they are held: Shift alone
    /// (ORTHO the other way), Shift + A (OSNAP), Shift + X (POLAR), Shift + Q (OTRACK),
    /// Shift + D (no snapping or tracking at all), Shift + E / V / C (Endpoint,
    /// Midpoint or Center alone).
    enum class Override : std::uint8_t {
        Ortho, Osnap, Polar, Otrack, DisableAll, Endpoint, Midpoint, Center, Count
    };
    /// True while the running command's prompt asks for a point (`Specify next point`,
    /// `Specify opposite corner`, `... location`): the only time the override keys are
    /// read, so a name or a text being typed keeps its capitals.
    [[nodiscard]] bool asking_for_point() const;
    void set_override(Override which, bool held);
    [[nodiscard]] bool override_held(Override which) const noexcept {
        return overrides_[static_cast<std::size_t>(which)];
    }
    void clear_overrides();
    /// OSNAP as the held keys leave it, given the status bar's setting.
    [[nodiscard]] bool effective_osnap(bool running) const noexcept;

    /// The object snap under the cursor, handed over on every move: a point the cursor
    /// rests on (a fifth of a second) is acquired for object snap tracking, and let go
    /// when the cursor comes back to rest on it. `now` is a clock in seconds.
    void note_snap(std::optional<core::Vec2> snap, const std::string& label, double now);
    [[nodiscard]] const std::vector<TrackPoint>& acquired_points() const noexcept { return acquired_; }
    /// What tracking made of the cursor last resolved: the paths in hand (none when the
    /// cursor is free), for the dashed lines and the tooltip.
    [[nodiscard]] const TrackResult& tracked() const noexcept { return tracked_; }
    /// "Polar: 12.3456 < 45°", "Endpoint: < 90°, Polar: < 0°"; empty when free.
    [[nodiscard]] std::string tracking_tooltip() const;
    void set_grid_snap(bool on) { grid_snap_ = on; }
    void set_grid_spacing(double s) { grid_spacing_ = s; }
    void set_pick_radius(double world_radius) { pick_radius_ = world_radius; }

    /// Starts a fresh undo group and returns its id, for one-shot commands
    /// submitted outside the command-line state machine (e.g. dialog boxes).
    std::uint64_t begin_group() {
        current_group_ = ++group_counter_;
        return current_group_;
    }

    /// Resolves a raw cursor point exactly as a commit would: OSNAP point wins,
    /// otherwise ortho/polar/grid-snap relative to the anchor. Used by both the
    /// commit path and the render-side preview so they always agree.
    [[nodiscard]] core::Vec2 resolve_pick(core::Vec2 world, std::optional<core::Vec2> snap) const;

    /// The active command's preview request (None when idle).
    [[nodiscard]] const PreviewSpec& preview() const noexcept { return preview_; }

    /// Cached selection count, fed from the published snapshot by the UI.
    void set_selection_count(int n) noexcept { selection_count_ = n; }

    /// Cached hovered-entity kind, fed from the published snapshot by the UI. When
    /// it changes while a command is active, the command's hover() hook fires (the
    /// smart DIM preview). nullopt means the cursor is over empty space.
    void set_hovered_kind(std::optional<core::EntityKind> kind);
    void set_named_views(std::vector<core::NamedView> v) { named_views_ = std::move(v); }
    void set_units(const core::DrawingUnits& u) { units_ = u; }
    void set_text_styles(std::vector<core::TextStyle> v, std::uint16_t current) {
        text_styles_ = std::move(v);
        current_text_style_ = current;
    }
    [[nodiscard]] std::vector<core::TextStyle> text_styles() const override { return text_styles_; }
    [[nodiscard]] std::uint16_t current_text_style() const override { return current_text_style_; }
    void set_block_names(std::vector<std::string> v) { block_names_ = std::move(v); }
    void set_purge_candidates(core::RenderSnapshot::PurgeCandidates p) { purge_ = std::move(p); }
    [[nodiscard]] core::RenderSnapshot::PurgeCandidates purge_candidates() const override { return purge_; }
    void set_ltscales(double ltscale, double celtscale, bool ps, bool ms) noexcept {
        ltscale_ = ltscale;
        celtscale_ = celtscale;
        psltscale_ = ps;
        msltscale_ = ms;
    }
    [[nodiscard]] double ltscale() const override { return ltscale_; }
    void set_fillmode(bool on) noexcept { fillmode_ = on; }
    [[nodiscard]] bool fillmode() const override { return fillmode_; }
    [[nodiscard]] double current_celtscale() const override { return celtscale_; }
    [[nodiscard]] bool psltscale() const override { return psltscale_; }
    [[nodiscard]] bool msltscale() const override { return msltscale_; }
    void set_pickstyle(std::uint8_t v) noexcept { pickstyle_ = v; }
    [[nodiscard]] std::uint8_t pickstyle() const override { return pickstyle_; }
    [[nodiscard]] std::string status_modes() const override;
    void set_drawing_props(core::DrawingProps p) { drawing_props_ = std::move(p); }
    [[nodiscard]] core::DrawingProps drawing_props() const override { return drawing_props_; }
    [[nodiscard]] std::vector<std::string> block_names() const override { return block_names_; }
    void set_layouts(std::vector<std::string> names, std::uint8_t active) {
        layout_names_ = std::move(names);
        active_space_ = active;
    }
    [[nodiscard]] std::vector<std::string> layout_names() const override { return layout_names_; }
    [[nodiscard]] std::uint8_t active_space() const override { return active_space_; }
    void set_mspace_active(bool on) noexcept { mspace_active_ = on; }
    [[nodiscard]] bool mspace_active() const override { return mspace_active_; }
    /// Per block (parallel to the names): its attributes, for INSERT's value prompts.
    void set_block_attdefs(std::vector<std::vector<core::BlockAttDefInfo>> v) {
        block_attdefs_ = std::move(v);
    }
    [[nodiscard]] std::vector<core::BlockAttDefInfo> block_attdefs(const std::string& block) const override {
        for (std::size_t i = 0; i < block_names_.size() && i < block_attdefs_.size(); ++i) {
            if (block_names_[i] == block) {
                return block_attdefs_[i];
            }
        }
        return {};
    }
    [[nodiscard]] core::DrawingUnits units() const override { return units_; }
    [[nodiscard]] std::vector<core::NamedView> named_views() const override { return named_views_; }

    [[nodiscard]] bool has_active_command() const noexcept { return active_ != nullptr; }
    /// True while the running command is taking text as typed (see ICommand::free_text).
    [[nodiscard]] bool wants_free_text() const { return active_ != nullptr && active_->free_text(); }

    /// True while the active command is at its "Select objects:" prompt (see
    /// ICommand::in_selection_phase). The viewport then treats presses and drags as
    /// selection gestures rather than coordinate picks.
    [[nodiscard]] bool in_selection_phase() const {
        return active_ != nullptr && active_->in_selection_phase();
    }
    /// Remove mode at "Select objects:": the viewport's gestures take objects out.
    [[nodiscard]] bool selection_phase_removing() const {
        return active_ != nullptr && active_->selection_removing();
    }
    /// The viewport finished a selection gesture at "Select objects:".
    void notify_selection_gesture();
    [[nodiscard]] const std::string& last_command() const noexcept { return last_command_alias_; }
    [[nodiscard]] const CommandRegistry& registry() const noexcept { return registry_; }
    [[nodiscard]] CommandRegistry& registry() noexcept { return registry_; }

    /// The current prompt ("Command: " when idle). Owned here so any surface (the
    /// bottom bar OR the on-canvas Dynamic Input) renders the same prompt; hiding the
    /// bar never loses it.
    [[nodiscard]] const std::string& current_prompt() const noexcept { return current_prompt_; }

    /// Submitted-line history (newest last), owned here so the bottom bar and the
    /// canvas command-entry box recall from ONE place. `history_recall(+1)` steps to
    /// older entries, `-1` to newer; returns "" past the newest.
    [[nodiscard]] const std::vector<std::string>& history() const noexcept { return history_; }
    [[nodiscard]] std::string history_recall(int dir);
    void history_reset_cursor() noexcept { history_cursor_ = static_cast<int>(history_.size()); }

    // --- CommandContext ---
    void echo(const std::string& line) override;
    void set_prompt(const std::string& prompt) override;
    void submit(core::Command command) override;
    [[nodiscard]] std::uint64_t group_id() const override { return current_group_; }
    [[nodiscard]] std::uint64_t new_group() override { return begin_group(); }
    [[nodiscard]] std::optional<core::Vec2> last_point() const override { return last_point_; }
    void set_last_point(core::Vec2 p) override {
        if (last_point_ && core::distance(*last_point_, p) > 1e-12) {
            // The direction just drawn: what relative polar angles are measured from.
            last_direction_ = std::atan2(p.y - last_point_->y, p.x - last_point_->x);
        }
        last_point_ = p;
        track_points_.clear(); // the points tracked for it have served
        acquired_.clear();
        hover_.reset();
    }
    void clear_last_point() override {
        last_point_.reset();
        last_direction_ = 0.0;
    }
    [[nodiscard]] std::optional<LastSegment> last_segment() const override { return last_segment_; }
    void set_last_segment(LastSegment segment) override { last_segment_ = segment; }
    [[nodiscard]] bool ctrl_held() const override { return ctrl_held_; }
    /// The viewport's Ctrl state at a pick (set before pick_point(), cleared after).
    void set_ctrl_held(bool held) { ctrl_held_ = held; }
    [[nodiscard]] bool shift_held() const override { return shift_held_; }
    void set_shift_held(bool held) { shift_held_ = held; }
    [[nodiscard]] std::optional<core::Vec2> cursor_world() const override { return cursor_world_; }
    /// The viewport's constrained cursor, streamed on every move (direct distance entry).
    void set_cursor_world(core::Vec2 p) { cursor_world_ = p; }
    void set_preview(PreviewSpec spec) override { preview_ = std::move(spec); }
    void clear_preview() override { preview_ = PreviewSpec{}; }
    [[nodiscard]] int selection_count() const override { return selection_count_; }
    [[nodiscard]] double pick_radius() const override { return pick_radius_; }
    [[nodiscard]] std::optional<core::EntityKind> hovered_kind() const override {
        return hovered_kind_;
    }
    [[nodiscard]] ViewControl* view() override { return view_; }

private:
    void finalize_if_done();
    void show_ready();

    /// FROM (a base point, then an offset from it), M2P (the middle of two points), TT
    /// (a temporary tracking point) and TK (a chain of orthogonal moves): the points a
    /// filter asks for are taken here, ahead of the command, which then receives the
    /// one point they make.
    struct PointFilter {
        enum class Kind : std::uint8_t { None, From, MidBetween, TempTrack, Track };
        Kind kind = Kind::None;
        int stage = 0;
        core::Vec2 a{};
        std::string prompt;                   ///< the command's prompt, put back afterwards
        std::optional<core::Vec2> last_point; ///< ... and its last point
    };
    PointFilter filter_;
    std::optional<std::uint32_t> snap_override_;
    std::string snap_override_name_;   ///< "Endpoint", for "No Endpoint found ..."
    std::string snap_override_prompt_; ///< the prompt the override was typed at
    std::vector<core::Vec2> track_points_;
    bool feeding_pick_ = false; ///< submit_line() is delivering a viewport pick
    // Tracking (AutoTrack).
    TrackingSettings tracking_;
    bool otrack_ = false;
    bool polar_snap_type_ = false; ///< SNAPTYPE 1
    bool temp_overrides_ = true;   ///< TEMPOVERRIDES
    double last_direction_ = 0.0;  ///< the direction of the segment that reached last_point_
    std::vector<TrackPoint> acquired_;
    struct Hover {
        core::Vec2 at{};
        std::string label;
        double since = 0.0;
        bool taken = false; ///< this rest has been counted already
    };
    std::optional<Hover> hover_;
    mutable TrackResult tracked_;
    std::array<bool, static_cast<std::size_t>(Override::Count)> overrides_{};
    bool try_snap_override(const std::string& text);
    void end_snap_override(bool restore_prompt);
    void begin_filter(PointFilter::Kind kind);
    void end_filter();
    void feed_filter(const std::string& text);
    void deliver_point(core::Vec2 p);
    void drop_point_modifiers();

    CommandSink sink_;
    ViewControl* view_;
    std::vector<core::NamedView> named_views_;
    core::DrawingUnits units_{};
    std::vector<core::TextStyle> text_styles_;
    std::uint16_t current_text_style_ = 0;
    std::vector<std::string> block_names_;
    std::vector<std::vector<core::BlockAttDefInfo>> block_attdefs_;
    std::vector<std::string> layout_names_;
    std::uint8_t active_space_ = 0;
    bool mspace_active_ = false;
    CommandOutput& output_;
    CommandRegistry registry_;

    [[nodiscard]] core::Vec2 resolve_constraints(core::Vec2 world) const;

    std::unique_ptr<ICommand> active_;
    std::optional<core::Vec2> last_point_;
    std::deque<std::string> macro_;  ///< tokens still to feed (start_macro)
    bool macro_waiting_ = false;     ///< the next user input releases the macro
    void run_macro();
    std::optional<LastSegment> last_segment_;
    bool ctrl_held_ = false;
    bool shift_held_ = false;
    std::optional<core::Vec2> cursor_world_;
    std::uint64_t group_counter_ = 0;
    std::uint64_t current_group_ = 0;
    std::string last_command_alias_;
    std::string current_prompt_ = "Command: "; ///< mirrored to every input surface
    std::vector<std::string> history_;          ///< submitted lines (newest last)
    int history_cursor_ = 0;                     ///< recall position into history_

    bool ortho_ = false;
    bool polar_ = false;
    bool grid_snap_ = false;
    double grid_spacing_ = 1.0;
    double pick_radius_ = 0.0;
    int selection_count_ = 0;
    std::optional<core::EntityKind> hovered_kind_;
    PreviewSpec preview_;
    core::RenderSnapshot::PurgeCandidates purge_;
    double ltscale_ = 1.0;
    double celtscale_ = 1.0;
    bool psltscale_ = true;
    bool fillmode_ = true;
    bool msltscale_ = true;
    std::uint8_t pickstyle_ = 1;
    core::DrawingProps drawing_props_;
};

} // namespace musacad::command

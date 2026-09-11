// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
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

    /// Delivers a cursor pick (a viewport click). `snap` is the active OSNAP
    /// point if any (it wins over `world`). For a selection command (ERASE) this
    /// becomes an ErasePick; otherwise it feeds a coordinate (after ortho/polar/
    /// grid-snap resolution) to the active command.
    void pick_point(core::Vec2 world, std::optional<core::Vec2> snap);

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

    /// True while the active command is at its "Select objects:" prompt (see
    /// ICommand::in_selection_phase). The viewport then treats presses and drags as
    /// selection gestures rather than coordinate picks.
    [[nodiscard]] bool in_selection_phase() const {
        return active_ != nullptr && active_->in_selection_phase();
    }
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
    void set_last_point(core::Vec2 p) override { last_point_ = p; }
    void clear_last_point() override { last_point_.reset(); }
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
};

} // namespace musacad::command

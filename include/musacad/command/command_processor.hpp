#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

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

    /// Resolves a raw cursor point exactly as a commit would: OSNAP point wins,
    /// otherwise ortho/polar/grid-snap relative to the anchor. Used by both the
    /// commit path and the render-side preview so they always agree.
    [[nodiscard]] core::Vec2 resolve_pick(core::Vec2 world, std::optional<core::Vec2> snap) const;

    /// The active command's preview request (None when idle).
    [[nodiscard]] const PreviewSpec& preview() const noexcept { return preview_; }

    /// Cached selection count, fed from the published snapshot by the UI.
    void set_selection_count(int n) noexcept { selection_count_ = n; }

    [[nodiscard]] bool has_active_command() const noexcept { return active_ != nullptr; }
    [[nodiscard]] const std::string& last_command() const noexcept { return last_command_alias_; }
    [[nodiscard]] const CommandRegistry& registry() const noexcept { return registry_; }
    [[nodiscard]] CommandRegistry& registry() noexcept { return registry_; }

    // --- CommandContext ---
    void echo(const std::string& line) override;
    void set_prompt(const std::string& prompt) override;
    void submit(core::Command command) override;
    [[nodiscard]] std::uint64_t group_id() const override { return current_group_; }
    [[nodiscard]] std::optional<core::Vec2> last_point() const override { return last_point_; }
    void set_last_point(core::Vec2 p) override { last_point_ = p; }
    void clear_last_point() override { last_point_.reset(); }
    void set_preview(PreviewSpec spec) override { preview_ = std::move(spec); }
    void clear_preview() override { preview_ = PreviewSpec{}; }
    [[nodiscard]] int selection_count() const override { return selection_count_; }
    [[nodiscard]] double pick_radius() const override { return pick_radius_; }
    [[nodiscard]] ViewControl* view() override { return view_; }

private:
    void start_command(const std::string& alias);
    void finalize_if_done();
    void show_ready();

    CommandSink sink_;
    ViewControl* view_;
    CommandOutput& output_;
    CommandRegistry registry_;

    [[nodiscard]] core::Vec2 resolve_constraints(core::Vec2 world) const;

    std::unique_ptr<ICommand> active_;
    std::optional<core::Vec2> last_point_;
    std::uint64_t group_counter_ = 0;
    std::uint64_t current_group_ = 0;
    std::string last_command_alias_;

    bool ortho_ = false;
    bool polar_ = false;
    bool grid_snap_ = false;
    double grid_spacing_ = 1.0;
    double pick_radius_ = 0.0;
    int selection_count_ = 0;
    PreviewSpec preview_;
};

} // namespace musacad::command

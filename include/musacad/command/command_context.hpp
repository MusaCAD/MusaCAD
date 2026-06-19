// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "musacad/core/command.hpp"
#include "musacad/core/entity_handle.hpp"
#include "musacad/core/math/math.hpp"

namespace musacad::command {

/// What the active command wants previewed as the cursor moves. The renderer
/// composes the actual overlay from this plus the live (constrained) cursor;
/// nothing here touches the GeometryStore.
enum class PreviewKind {
    None,
    Segment,    ///< rubber-band from points[0] to the cursor (LINE next segment)
    Polyline,   ///< chain through points + rubber-band to the cursor
    Rectangle,  ///< rectangle from points[0] to the cursor
    Circle,     ///< circle centred at points[0], radius = |cursor - center|
    Arc,        ///< 3-point arc through points (1 or 2) + the cursor
    Move,       ///< ghost of the selection translated by (cursor - points[0])
    Mirror,     ///< ghost of the selection reflected across points[0]..cursor
    Rotate,     ///< ghost of the selection rotated about points[0] by angle to cursor
    Scale,      ///< ghost of the selection scaled about points[0] by |cursor - base|
    Dimension,  ///< full dimension rubber-band following the cursor to its placement
};

struct PreviewSpec {
    PreviewKind kind = PreviewKind::None;
    std::vector<core::Vec2> points; ///< committed anchors so far

    // Rectangle preview parameters (RECTANGLE Dimensions/Area/Rotation options). When
    // fixed_w/fixed_h are > 0 the previewed rectangle is that fixed size and only its
    // QUADRANT follows the cursor (relative to points[0]); otherwise it is the normal
    // corner-to-corner rubber band. `rect_rotation` (radians) rotates it about points[0].
    // These parameterise the existing PreviewKind::Rectangle path -- not a new pipeline.
    double fixed_w = 0.0;
    double fixed_h = 0.0;
    double rect_rotation = 0.0;
    // True while the command is awaiting a single scalar/keyword at a value sub-prompt
    // (RECTANGLE Dimensions length/width, Area, Rotation) rather than the two-field
    // corner drag. With Dynamic Input on this routes the step to the at-cursor
    // sub-prompt cell instead of the on-geometry Length/Width fields.
    bool scalar_prompt = false;

    // Dimension preview only: the subtype (core::DimType) and style index. `points`
    // holds the def points (a, b) for two-point dims; for object-based dims it is
    // empty and the def points come from the snapshot's pending_dim_* (resolved once
    // at the object pick). The cursor supplies the dimension-line placement.
    int dim_type = -1; ///< -1 = not a dimension preview
    std::uint16_t dim_style = 0;
};

/// Sink for command-line text output (scrollback + the active prompt).
/// Implemented by the command-line widget.
class CommandOutput {
public:
    virtual ~CommandOutput() = default;
    virtual void append_line(const std::string& line) = 0;
    virtual void set_prompt(const std::string& prompt) = 0;
};

/// View operations a command may request (ZOOM). Implemented by the viewport;
/// kept on the render/UI side so it never involves the geometry thread.
class ViewControl {
public:
    virtual ~ViewControl() = default;
    virtual void zoom_extents() = 0;
    virtual void zoom_scale(double factor) = 0;
    /// Toggle the Properties palette (PR). Default no-op (headless/tests).
    virtual void open_properties() {}
    /// DWG import/export via the external converter. Default no-op (headless/tests).
    virtual void import_dwg() {}
    virtual void export_dwg() {}
    /// Open the PLOT/print dialog (PDF + printer). Default no-op (headless/tests).
    virtual void plot_dialog() {}
    /// MATCHPROP: the current Settings filter (which categories copy). Default all-on.
    [[nodiscard]] virtual core::MatchPropFilter match_filter() const { return {}; }
    /// MATCHPROP: open the modal Settings dialog (persists the filter). No-op headless.
    virtual void match_settings_dialog() {}
    /// MATCHPROP: switch the viewport to the match (paintbrush) cursor while picking
    /// targets; restore the normal cursor when `on` is false. No-op headless.
    virtual void set_match_cursor(bool on) { (void)on; }
};

/// The services a running command uses to interact with the system. Commands
/// never touch the GeometryStore: the only way they affect geometry is by
/// emitting a core::Command via submit(), which the processor forwards to the
/// UI->geometry MPSC queue.
class CommandContext {
public:
    virtual ~CommandContext() = default;

    virtual void echo(const std::string& line) = 0;     ///< scrollback message
    virtual void set_prompt(const std::string& prompt) = 0;

    virtual void submit(core::Command command) = 0;     ///< -> geometry queue
    [[nodiscard]] virtual std::uint64_t group_id() const = 0;
    /// Mint a FRESH undo group, so a command can make each step individually undoable
    /// (MATCHPROP: one undo entry per matched target). Default = the current group.
    [[nodiscard]] virtual std::uint64_t new_group() { return group_id(); }

    [[nodiscard]] virtual std::optional<core::Vec2> last_point() const = 0;
    virtual void set_last_point(core::Vec2 p) = 0;
    virtual void clear_last_point() = 0;

    /// What to draw as a cursor-tracking preview (transient, render-side).
    virtual void set_preview(PreviewSpec spec) = 0;
    virtual void clear_preview() = 0;

    /// Current selection state (published from the geometry side, cached UI-side).
    [[nodiscard]] virtual int selection_count() const = 0;
    [[nodiscard]] bool has_selection() const { return selection_count() > 0; }

    /// World-space pick aperture (pixels / camera scale), for pick-based commands.
    [[nodiscard]] virtual double pick_radius() const = 0;

    /// The kind of entity under the cursor (from the published snapshot, cached
    /// UI-side), or nullopt over empty space. Used by the smart DIM preview.
    [[nodiscard]] virtual std::optional<core::EntityKind> hovered_kind() const { return std::nullopt; }

    [[nodiscard]] virtual ViewControl* view() = 0;       ///< may be null in tests
};

} // namespace musacad::command

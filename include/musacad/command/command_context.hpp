// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "musacad/core/block_attdef_info.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/named_view.hpp"
#include "musacad/core/snap.hpp"
#include "musacad/core/text_style.hpp"
#include "musacad/core/units.hpp"
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
    Polygon,    ///< regular n-gon about points[0], sized by the cursor (POLYGON)
    Ellipse,    ///< ELLIPSE rubber band: see PreviewSpec's ellipse fields
    Spline,     ///< SPLINE: the curve through/over points + cursor (see spline fields)
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
    /// Polygon preview: how many sides, and whether the cursor distance is the
    /// circumradius (inscribed) or the apothem (circumscribed). Parameterises the
    /// PreviewKind::Polygon path the same way fixed_w/fixed_h parameterise Rectangle.
    int sides = 4;
    bool inscribed = true;
    /// STRETCH's second-point step: besides the Segment rubber line from points[0], the
    /// viewport streams the cursor delta to the engine, which previews the whole
    /// selection stretched by it (StretchPreviewCommand). A flag on the existing Segment
    /// kind rather than a new kind, so Dynamic Input's length/angle fields and ortho
    /// behave exactly as they do for LINE with no second code path.
    bool live_stretch = false;
    /// ELLIPSE preview (PreviewKind::Ellipse): points[0] is the centre and `major` the
    /// major half-axis. Stage 0: the other half-axis follows the cursor, axes swapping
    /// when it grows past the major -- the same rule the command commits with. Stage 1:
    /// the ellipse is fixed (`ratio`) and a rubber line from the centre shows the start
    /// angle being picked. Stage 2: the elliptical arc from `ellipse_start` to the
    /// cursor's parameter.
    core::Vec2 major{};
    double ratio = 1.0;
    int ellipse_stage = 0;
    double ellipse_start = 0.0;
    /// SPLINE preview (PreviewKind::Spline): `points` are the fit points (spline_fit)
    /// or the control vertices so far; the cursor is appended as the next one and the
    /// curve is fitted/evaluated exactly as the command will commit it.
    bool spline_fit = true;
    int spline_degree = 3;
    int spline_knots = 0; ///< 0 chord, 1 square root, 2 uniform
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
    /// VIEW: the current camera (world centre, pixels per drawing unit) and the
    /// viewport size in pixels. Defaults say "unavailable" for headless hosts.
    [[nodiscard]] virtual bool current_view(core::Vec2& center, double& scale) {
        (void)center;
        (void)scale;
        return false;
    }
    virtual void set_view(core::Vec2 center, double scale) {
        (void)center;
        (void)scale;
    }
    [[nodiscard]] virtual bool viewport_size(int& w, int& h) const {
        (void)w;
        (void)h;
        return false;
    }
    /// Running object snaps: the mask (bits of SnapType), a setter (-OSNAP), and the
    /// settings dialog (OSNAP / DDOSNAP).
    [[nodiscard]] virtual std::uint32_t snap_mask() const { return 0; }
    virtual void set_snap_mask(std::uint32_t mask) { (void)mask; }
    virtual void osnap_settings_dialog() {}
    /// IMAGEATTACH's file picker; returns the chosen path, or "" (none / headless).
    [[nodiscard]] virtual std::string image_file_dialog() { return {}; }
    /// A generic open-file picker (XREF Attach); `filter` is a Qt-style filter string.
    [[nodiscard]] virtual std::string open_file_dialog(const std::string& /*filter*/) { return {}; }
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

    [[nodiscard]] virtual ViewControl* view() = 0;
    /// The drawing's saved views (VIEW Restore / ?), as last published by the engine.
    [[nodiscard]] virtual std::vector<core::NamedView> named_views() const { return {}; }
    /// The drawing's display units (UNITS), as last published by the engine.
    [[nodiscard]] virtual core::DrawingUnits units() const { return {}; }
    /// The drawing's text styles (STYLE table) and the current one, as last published.
    [[nodiscard]] virtual std::vector<core::TextStyle> text_styles() const { return {}; }
    [[nodiscard]] virtual std::uint16_t current_text_style() const { return 0; }
    /// The drawing's block-definition names (INSERT ? and the prompt default).
    [[nodiscard]] virtual std::vector<std::string> block_names() const { return {}; }       ///< may be null in tests
    /// The layout names (LAYOUT ?, Set) and the active space (0 = model).
    [[nodiscard]] virtual std::vector<std::string> layout_names() const { return {}; }
    [[nodiscard]] virtual std::uint8_t active_space() const { return 0; }
    /// True while model space is being edited through a viewport (MSPACE).
    [[nodiscard]] virtual bool mspace_active() const { return false; }
    /// A block's attributes (ATTDEFs), in prompt order; INSERT asks for their values.
    [[nodiscard]] virtual std::vector<core::BlockAttDefInfo> block_attdefs(const std::string& /*block*/) const {
        return {};
    }
};

} // namespace musacad::command

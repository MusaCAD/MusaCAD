// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <memory>
#include <string>
#include <optional>
#include <vector>

#include "musacad/command/command.hpp"
#include "musacad/core/math/arc_construct.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/properties.hpp"

namespace musacad::core {
struct EllipseData; // returned by value from EllipseCommand::shape (defined in the .cpp)
} // namespace musacad::core

namespace musacad::command {

// Each command is a small state machine. They share no control flow with the
// alias table or the processor.

class LineCommand final : public ICommand {
public:
    std::string name() const override { return "LINE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::vector<core::Vec2> points_;
    /// Continuing from an arc: the line leaves tangent to it, so only a length is asked.
    std::optional<double> fixed_dir_;
    bool done_ = false;
    void add_segment(CommandContext& ctx, core::Vec2 to);
    void prompt_next(CommandContext& ctx);
};

class CircleCommand final : public ICommand {
public:
    std::string name() const override { return "CIRCLE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    /// AutoCAD's methods: centre + radius / [Diameter]; 3P; 2P; Ttr (two objects and a
    /// radius); Tan, Tan, Tan (three objects). The last radius is the next default
    /// (CIRCLERAD).
    enum class State : std::uint8_t {
        Center, Radius, Diameter,
        TwoFirst, TwoSecond,
        ThreeFirst, ThreeSecond, ThreeThird,
        TanFirst, TanSecond, TanRadius,
        TttFirst, TttSecond, TttThird,
    };
    State state_ = State::Center;
    core::Vec2 center_{};
    std::vector<core::Vec2> pts_;
    bool done_ = false;
    void finish(CommandContext& ctx, core::Vec2 center, double radius);
    void prompt_radius(CommandContext& ctx);
};

class PolylineCommand final : public ICommand {
public:
    std::string name() const override { return "PLINE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    /// PLINEWID: the width a new polyline starts with (the last ending width given).
    inline static double s_width_ = 0.0;

private:
    /// Line mode (`[Arc/Close/Halfwidth/Length/Undo/Width]`) and Arc mode (`[Angle/CEnter/
    /// CLose/Direction/Halfwidth/Line/Radius/Second pt/Undo/Width]`) with AutoCAD's
    /// sub-steps; every arc segment is a bulge on the vertex it leaves, tangent to the
    /// previous segment unless a direction, centre, radius or second point says
    /// otherwise. Width / Halfwidth ask for a starting and an ending value: the next
    /// segment tapers between them and the ending one stays in force after it.
    enum class State : std::uint8_t {
        Start, Next, Length,
        ArcNext, ArcAngle, ArcAngleEnd, ArcAngleRadius, ArcAngleChordDir, ArcCenter, ArcCenterEnd,
        ArcCenterAngle, ArcCenterLength, ArcDirection, ArcDirectionEnd, ArcRadius, ArcRadiusEnd,
        ArcRadiusAngle, ArcRadiusChordDir, ArcSecond, ArcSecondEnd,
        WidthStart, WidthEnd,
    };
    State state_ = State::Start;
    State resume_ = State::Next;   ///< the mode the width prompts go back to
    std::vector<core::Vec2> points_;
    std::vector<double> bulges_;   ///< bulges_[i]: the segment points_[i] -> points_[i + 1]
    std::vector<double> tangents_; ///< the heading at the end of each segment
    std::vector<double> widths_;   ///< two per committed segment: its start and end width
    double start_w_ = 0.0;         ///< the width the next segment starts with
    double end_w_ = 0.0;           ///< ... and ends with
    bool half_ = false;            ///< the width prompt in hand asks for half-widths
    void begin_width(CommandContext& ctx, bool half);
    double angle_ = 0.0;
    double radius_ = 0.0;
    double direction_ = 0.0;
    core::Vec2 center_{};
    core::Vec2 second_{};
    bool done_ = false;
    [[nodiscard]] double start_tangent(CommandContext& ctx) const;
    void add_segment(CommandContext& ctx, core::Vec2 end, double bulge, double end_tangent);
    void add_arc(CommandContext& ctx, const std::optional<core::ConstructedArc>& arc);
    void prompt_next(CommandContext& ctx);
    void refresh_preview(CommandContext& ctx, int arc_mode = 0);
    void finish(CommandContext& ctx, bool closed);
    void undo_segment(CommandContext& ctx);
};

class ArcCommand final : public ICommand {
public:
    std::string name() const override { return "ARC"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    /// AutoCAD's methods: three points; the Center branch (start, centre, then the end,
    /// an included Angle or a chord Length); the End branch (start, end, then the centre,
    /// an Angle, a tangent Direction or a Radius); the centre first; Continue (Enter at
    /// the first prompt: tangent from the last line or arc). Ctrl at a pick flips the
    /// direction where the prompt says so.
    enum class State : std::uint8_t {
        Start, Second, ThreeEnd,
        AwaitCenter, CenterEnd, CenterAngle, CenterLength,
        AwaitEnd, EndCenter, EndAngle, EndDirection, EndRadius,
        CenterFirst, CenterStart, ContinueEnd,
    };
    State state_ = State::Start;
    core::Vec2 s_{};
    core::Vec2 m_{};
    core::Vec2 c_{};
    core::Vec2 e_{};
    double tangent_ = 0.0;
    bool done_ = false;
    void preview(CommandContext& ctx, int mode);
    void commit(CommandContext& ctx, const std::optional<core::ConstructedArc>& arc);
};

class RectangleCommand final : public ICommand {
public:
    std::string name() const override { return "RECTANG"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    // AwaitCorner is the AutoCAD "Specify other corner or [Area/Dimensions/Rotation]"
    // hub: it accepts the placement pick OR the option keywords. The Dim*/Area*/Rot
    // states gather typed values, exactly like CIRCLE's [Diameter] sub-step.
    enum class State {
        First,
        AwaitCorner,
        DimLen,
        DimWid,
        AreaVal,
        AreaSide,
        AreaSideVal,
        RotVal,
        RotPick1,  ///< [Rotation] > [Pick points]: the first of two points
        RotPick2,
        ChamferD1, ///< [Chamfer] first distance (before the first corner, as in AutoCAD)
        ChamferD2, ///< [Chamfer] second distance
        FilletR,   ///< [Fillet] radius
        LineWidth, ///< [Width]: the polyline width of the rectangle
        Elevation, ///< [Elevation]
        Thickness, ///< [Thickness]
    } state_ = State::First;
    // The line width, elevation and thickness stay in force for later rectangles too.
    inline static double s_line_width_ = 0.0;
    inline static double s_elevation_ = 0.0;
    inline static double s_thickness_ = 0.0;
    double line_width_ = s_line_width_;
    double elevation_ = s_elevation_;
    double thickness_ = s_thickness_;
    [[nodiscard]] std::string first_prompt() const {
        return "Specify first corner point or [Chamfer/Elevation/Fillet/Thickness/Width]: ";
    }
    // Corner treatment. AutoCAD keeps the last chamfer distances / fillet radius as the
    // default for every later rectangle in the session, and setting one clears the
    // other -- both mirrored here through the session-wide statics.
    inline static double s_chamfer_d1_ = 0.0;
    inline static double s_chamfer_d2_ = 0.0;
    inline static double s_fillet_r_ = 0.0;
    // The last length, width, area and rotation: the defaults AutoCAD offers next time
    // (the rotation stays in force for later rectangles, as it does there).
    inline static double s_length_ = 10.0;
    inline static double s_width_ = 10.0;
    inline static double s_area_ = 100.0;
    inline static double s_rotation_ = 0.0;
    core::Vec2 rot_p1_{};
    double chamfer_d1_ = s_chamfer_d1_;
    double chamfer_d2_ = s_chamfer_d2_;
    double fillet_r_ = s_fillet_r_;
    core::Vec2 first_{};
    double length_ = 0.0;   ///< fixed width along X (0 => corner-to-corner, no fixed size)
    double width_ = 0.0;    ///< fixed width along Y
    double rotation_ = s_rotation_; ///< radians, applied about first_
    double area_ = 0.0;
    bool area_by_length_ = true; ///< Area option: user gave Length (else Width)
    bool has_dims_ = false;      ///< fixed (length_, width_) chosen -> quadrant-flip placement
    bool done_ = false;
};

/// AutoCAD's "Select objects:" prompt as a reusable step (issue #46). While it is
/// `active()` the viewport's picks, windows and lassos accumulate on their own; typed
/// answers come through `input()`: a point picks; Window, Crossing, BOX, ALL, Fence,
/// WPolygon, CPolygon, Group, Last, Previous, Add, Remove, Multiple, Undo, AUto and
/// SIngle do what they do in AutoCAD; Enter ends the step (`Done`). The owner then reads
/// `ctx.has_selection()`: nothing selected ends the command quietly, as AutoCAD does.
class SelectObjectsPhase {
public:
    enum class Result { Continue, Done };
    void begin(CommandContext& ctx, std::string prompt = "Select objects: ");
    Result input(CommandContext& ctx, const std::string& text);
    /// The viewport finished a gesture; in SIngle mode that ends the step.
    Result gesture(CommandContext& ctx);
    [[nodiscard]] bool removing() const noexcept { return remove_; }
    [[nodiscard]] bool active() const noexcept { return active_; }

private:
    enum class Sub { Objects, Corner1, Corner2, Fence, Polygon, GroupName };
    void reprompt(CommandContext& ctx);
    Sub sub_ = Sub::Objects;
    bool active_ = false;
    bool crossing_ = false;
    bool box_ = false;
    bool remove_ = false;
    bool single_ = false;
    std::string prompt_;
    core::Vec2 c1_{};
    std::vector<core::Vec2> pts_;
};

/// ERASE: `Select objects:` gathers picks, windows and keywords until Enter, then erases
/// the set (a pre-selection is erased at once); OOPS brings the set back.
class EraseCommand final : public ICommand {
public:
    std::string name() const override { return "ERASE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && select_.active(); }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    void finish(CommandContext& ctx);
    SelectObjectsPhase select_;
    bool done_ = false;
};

/// OOPS: the objects the last ERASE removed come back.
class OopsCommand final : public ICommand {
public:
    std::string name() const override { return "OOPS"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext&) override { done_ = true; }
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

class UndoCommand final : public ICommand {
public:
    std::string name() const override { return "U"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

class ZoomCommand final : public ICommand {
public:
    std::string name() const override { return "ZOOM"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

// --- Modify commands (operate on the current selection / a pick) ---

class MoveCommand final : public ICommand {
public:
    std::string name() const override { return "MOVE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    /// `Specify base point or [Displacement] <Displacement>:`, then `Specify second point
    /// or <use first point as displacement>:`; the last displacement is the next default.
    enum class State : std::uint8_t { Select, Base, Second, Displacement };
    State state_ = State::Base;
    inline static core::Vec2 s_displacement_{};
    std::optional<core::Vec2> base_;
    bool done_ = false;

public:
    bool in_selection_phase() const override { return !done_ && state_ == State::Select; }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    void begin_base(CommandContext& ctx);
    SelectObjectsPhase select_;
};

class CopyCommand final : public ICommand {
public:
    std::string name() const override { return "COPY"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    /// `Specify base point or [Displacement/mOde] <Displacement>:`; the first placement
    /// `[Array] <use first point as displacement>`, later ones `[Array/Exit/Undo] <Exit>`
    /// in Multiple mode; Array lays `count` items along the vector (or Fit spreads them to
    /// the point). Every placement is its own undo step. The mode is kept for the session.
    enum class State : std::uint8_t { Select, Base, Displacement, Mode, Second, ArrayCount, ArrayEnd, ArrayFit };
    State state_ = State::Base;
    inline static core::Vec2 s_displacement_{};
    inline static bool s_single_ = false;
    std::optional<core::Vec2> base_;
    int placed_ = 0;
    int array_count_ = 0;
    bool done_ = false;
    void place(CommandContext& ctx, core::Vec2 delta);
    void prompt_second(CommandContext& ctx);
    void begin_base(CommandContext& ctx);
    SelectObjectsPhase select_;

public:
    bool in_selection_phase() const override { return !done_ && state_ == State::Select; }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;
};

/// MIRRTEXT: `Enter new value for MIRRTEXT <0>:` -- 0 keeps mirrored text readable
/// (AutoCAD's default), 1 reflects it.
class MirrtextCommand final : public ICommand {
public:
    std::string name() const override { return "MIRRTEXT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    inline static bool s_value_ = false; ///< the session's MIRRTEXT

private:
    bool done_ = false;
};

class MirrorCommand final : public ICommand {
public:
    std::string name() const override { return "MIRROR"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Select, First, Second, Ask } state_ = State::First;
    core::Vec2 p1_{};
    core::Vec2 p2_{};
    bool done_ = false;
    SelectObjectsPhase select_;

public:
    bool in_selection_phase() const override { return !done_ && state_ == State::Select; }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;
};

class OffsetCommand final : public ICommand {
public:
    std::string name() const override { return "OFFSET"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    /// OFFSETGAPTYPE: what joins a polyline's straight segments where the offset has
    /// parted them -- 0 carried on to their crossing, 1 an arc, 2 a bevel.
    inline static int s_gap_type_ = 0;

private:
    /// AutoCAD's flow: `Specify offset distance or [Through/Erase/Layer] <last>:` (a value,
    /// two points, or Through), `Select object to offset or [Exit/Undo] <Exit>:`, `Specify
    /// point on side to offset or [Exit/Multiple/Undo] <Exit>:`. The distance (or Through),
    /// Erase and Layer settings are kept for the session (OFFSETDIST).
    enum class State : std::uint8_t { Distance, Second, Erase, Layer, Object, Side, Multiple };
    State state_ = State::Distance;
    inline static double s_distance_ = 0.0; ///< 0 = Through
    inline static bool s_erase_ = false;
    inline static bool s_layer_current_ = false;
    core::Vec2 first_{};
    core::Vec2 object_pick_{};
    int placed_ = 0;
    bool from_last_multiple_ = false; ///< in [Multiple], the next offset steps from the last
    bool done_ = false;
    void prompt_distance(CommandContext& ctx);
    void prompt_object(CommandContext& ctx);
    void prompt_side(CommandContext& ctx);
    void place(CommandContext& ctx, core::Vec2 side, bool from_last);
    /// The offset a click would make follows the cursor at the side prompt.
    void show_preview(CommandContext& ctx, bool from_last);
    void end_preview(CommandContext& ctx);
    bool previewing_ = false;
};

/// OFFSETGAPTYPE: `Enter new value for OFFSETGAPTYPE <0>:` (0, 1 or 2).
class OffsetGapTypeCommand final : public ICommand {
public:
    std::string name() const override { return "OFFSETGAPTYPE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

// JOIN: pick a source object, then pick lines/arcs/open polylines that share endpoints
// with it; commits a single polyline (closed if the chain loops). Pick-based like OFFSET.
class JoinCommand final : public ICommand {
public:
    std::string name() const override { return "JOIN"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

    bool in_selection_phase() const override { return !done_ && select_.active(); }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    /// `Select source object or multiple objects to join at once:` (a pick: the source),
    /// then `Select objects to join:` with picks and windows -- for an arc or elliptical
    /// arc source `[cLose]` makes it whole.
    enum class State { Source, Targets } state_ = State::Source;
    core::Vec2 source_{};
    bool closable_ = false;
    SelectObjectsPhase select_;
    bool done_ = false;
};

// MATCHPROP / MA: pick a source object, then pick destination object(s) or [Settings].
// Each destination immediately adopts the source's matched properties (universal always;
// family-scoped only within a shared family). A paintbrush cursor is shown while picking
// targets, and each matched target is its own undo entry. Pick-based like JOIN.
class MatchPropCommand final : public ICommand {
public:
    std::string name() const override { return "MATCHPROP"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Source, Targets } state_ = State::Source;
    bool done_ = false;
    void begin_targets(CommandContext& ctx);

public:
    /// Destinations are a "Select objects:" step: a pick, a window or a crossing applies
    /// the source's properties to what it catches, as one undo step each.
    bool in_selection_phase() const override { return !done_ && state_ == State::Targets; }
    void selection_gesture(CommandContext& ctx) override;
};

// HATCH / H: fill a closed boundary with a pattern (Part A: SOLID, from selected closed
// polylines). Noun-verb: with a pre-selection, hatches it immediately; otherwise prompts to
// pick a closed boundary. The engine extracts the boundary loops (UI never touches the store).
class HatchCommand final : public ICommand {
public:
    std::string name() const override { return "HATCH"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class Mode { PickPoint, Pattern, Scale, Angle, GradientColor, GradientAngle };
    bool done_ = false;
    Mode mode_ = Mode::PickPoint;
    std::string pattern_ = "SOLID"; // SOLID, GRADIENT, or a known line pattern (e.g. ANSI31)
    double scale_ = 1.0;
    double angle_ = 0.0; // radians (pattern rotation / gradient direction)
    core::Rgb color2_{255, 255, 255}; // GRADIENT: the second colour
};

/// WIPEOUT: a mask polygon (a hatch with the WIPEOUT pattern) from picked points, or
/// [Polyline] from a closed polyline; [Frames] toggles WIPEOUTFRAME.
class WipeoutCommand final : public ICommand {
public:
    std::string name() const override { return "WIPEOUT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { First, Next, Frames, PolyPick, PolyErase } state_ = State::First;
    std::vector<core::Vec2> pts_;
    core::Vec2 poly_pick_{};
    bool done_ = false;
};

/// FIELD: place a text whose content is a field code (%<Date>%, %<Time>%, %<Filename>%,
/// %<Login>%), expanded at layout time and refreshed on every regen.
class FieldCommand final : public ICommand {
public:
    std::string name() const override { return "FIELD"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Name, Point, Height, Rotation } state_ = State::Name;
    std::string code_;
    core::Vec2 pos_{};
    double height_ = 2.5;
    bool done_ = false;
};

/// TOLERANCE / TOL -- a GD&T feature control frame. Command-line Q&A: the cells are
/// typed one at a time (the characteristic first), Enter on an empty line finishes the
/// list, then a pick places the frame. The ribbon button opens the Ph11 ParameterDialog
/// instead, which is the case the "dialogs when a dialog genuinely fits" rule
/// contemplates -- an FCF is genuinely multi-parameter. Both end at AddFcfCommand.
class ToleranceCommand final : public ICommand {
public:
    std::string name() const override { return "TOLERANCE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class Mode { Cells, Place };
    void prompt_cell(CommandContext& ctx);
    bool done_ = false;
    Mode mode_ = Mode::Cells;
    std::vector<std::string> cells_;
};

/// DATUM / DIMDATUM -- a datum feature symbol: the letter, the point on the feature,
/// then the box placement.
class DatumCommand final : public ICommand {
public:
    std::string name() const override { return "DATUM"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class Mode { Letter, Tip, Place };
    bool done_ = false;
    Mode mode_ = Mode::Letter;
    std::string letter_ = "A";
    core::Vec2 tip_{};
};

/// TABLE / TB -- insert a table (issue #22). Command-line Q&A: rows, columns, column
/// width, row height, then a placement pick. Cells start empty; they are filled by
/// editing, which keeps the command a placement tool rather than a data-entry form.
/// STRETCH / S -- move the vertices inside a crossing window (issue #24). AutoCAD insists
/// on a crossing window for this command, so the command asks for one rather than
/// consuming the current selection: "which points move" is the window's job, and a
/// pre-existing selection cannot express it.
/// Inquiry commands (issue #30). DIST and ID answer from the picked points alone, so
/// they never reach the store; AREA and LIST submit a query the geometry thread resolves
/// and reports through the status channel.
/// DIMCONTINUE (DCO) / DIMBASELINE (DBA) -- issue #28. Each pick adds another dimension
/// chained from the previous one, so the command loops until Esc/Enter, which is how a
/// row of holes actually gets dimensioned.
/// DIMCONTINUE / DIMBASELINE (#57): `Specify a second extension line origin or
/// [Select/Undo] <Select>:` (DIMBASELINE adds Offset), one dimension after another from the
/// last one drawn -- linear, aligned, angular or ordinate; Enter (or Select) asks which
/// dimension to go on from, and Enter there ends.
class ChainDimCommand final : public ICommand {
public:
    explicit ChainDimCommand(bool baseline) : baseline_(baseline) {}
    std::string name() const override { return baseline_ ? "DIMBASELINE" : "DIMCONTINUE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State : std::uint8_t { Origin, Select, Offset };
    void prompt(CommandContext& ctx);
    bool baseline_ = false;
    State state_ = State::Origin;
    int added_ = 0;        ///< dimensions this run, for Undo
    double spacing_ = 0.0; ///< Offset (0 = the style's, 1.5 text heights)
    bool done_ = false;
};

/// DIMLAYER: `Enter new value for DIMLAYER, or . for use current <...>:` -- the layer new
/// dimensions go on.
class DimLayerCommand final : public ICommand {
public:
    std::string name() const override { return "DIMLAYER"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// DIMEDIT: Home, New, Rotate or Oblique, then `Select objects:` (a pre-selection is
/// edited at once).
class DimEditTextCommand final : public ICommand {
public:
    std::string name() const override { return "DIMEDIT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && select_.active(); }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    enum class State : std::uint8_t { Type, Text, Angle, Select };
    void to_select(CommandContext& ctx);
    void finish(CommandContext& ctx);
    State state_ = State::Type;
    core::DimEditCommand edit_{};
    SelectObjectsPhase select_;
    bool done_ = false;
};

/// DIMTEDIT: a dimension, then where its text goes, or Left / Right / Center / Home /
/// Angle.
class DimTextEditCommand final : public ICommand {
public:
    std::string name() const override { return "DIMTEDIT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State : std::uint8_t { Pick, Where, Angle };
    void submit(CommandContext& ctx, core::DimEditCommand::Op op);
    State state_ = State::Pick;
    core::Vec2 pick_{};
    double radius_ = 0.0;
    bool done_ = false;
};

/// DIMSPACE: a base dimension, the dimensions to space from it, then the spacing --
/// Auto (twice the text height), a distance, or 0 to line them up with the base.
class DimSpaceCommand final : public ICommand {
public:
    std::string name() const override { return "DIMSPACE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && select_.active(); }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    enum class State : std::uint8_t { Base, Select, Value };
    State state_ = State::Base;
    core::Vec2 base_{};
    double radius_ = 0.0;
    SelectObjectsPhase select_;
    bool done_ = false;
};

/// DIMCENTER: a centre mark on the arc or circle picked; Lines adds centre lines.
class DimCenterCommand final : public ICommand {
public:
    std::string name() const override { return "DIMCENTER"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    static inline bool s_lines_ = false;
    bool done_ = false;
};

/// DIMOVERRIDE: dimension variables and their values (or Clear overrides), then
/// `Select objects:` -- the dimensions take them as their own.
class DimOverrideCommand final : public ICommand {
public:
    std::string name() const override { return "DIMOVERRIDE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && select_.active(); }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    enum class State : std::uint8_t { Name, Value, Select };
    void prompt(CommandContext& ctx);
    void to_select(CommandContext& ctx);
    void finish(CommandContext& ctx);
    State state_ = State::Name;
    std::string var_;
    core::SetDimOverrideCommand set_{};
    SelectObjectsPhase select_;
    bool done_ = false;
};

/// REVERSE: `Select objects:`, then the lines, polylines and splines run the other way.
/// OVERKILL: `Select objects:`, then its settings, then duplicates go and overlapping
/// collinear lines become one.
class SelectThenCommand final : public ICommand {
public:
    enum class Tool : std::uint8_t { Reverse, Overkill };
    explicit SelectThenCommand(Tool t) : tool_(t) {}
    std::string name() const override { return tool_ == Tool::Reverse ? "REVERSE" : "OVERKILL"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && select_.active(); }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    enum class State : std::uint8_t { Select, Option, Tolerance };
    void selected(CommandContext& ctx);
    void option_prompt(CommandContext& ctx);
    Tool tool_;
    State state_ = State::Select;
    static inline core::OverkillCommand s_overkill_{};
    SelectObjectsPhase select_;
    bool done_ = false;
};

/// COPYTOLAYER: the objects, the destination layer (an object on it, or Name), then a
/// base point and second point (or Displacement) the copies move by.
/// LAYMCH: the objects, then an object on the destination layer (or Name).
class LayerCopyCommand final : public ICommand {
public:
    explicit LayerCopyCommand(bool copy) : copy_(copy) {}
    std::string name() const override { return copy_ ? "COPYTOLAYER" : "LAYMCH"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && select_.active(); }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    enum class State : std::uint8_t { Select, Layer, Name, Base, Second, Displacement };
    void to_layer(CommandContext& ctx);
    void to_base(CommandContext& ctx);
    void finish(CommandContext& ctx, core::Vec2 offset);
    bool copy_;
    State state_ = State::Select;
    core::CopyToLayerCommand c_{};
    core::Vec2 base_{};
    SelectObjectsPhase select_;
    bool done_ = false;
};

/// CHPROP: the objects, then `Enter property to change [Color/LAyer/LType/ltScale/LWeight]:`
/// as often as you like; Enter makes the changes, one undo step.
class ChPropCommand final : public ICommand {
public:
    std::string name() const override { return "CHPROP"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && select_.active(); }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    enum class State : std::uint8_t { Select, Property, Color, TrueColor, Layer, Linetype, Ltscale, Lineweight };
    void property_prompt(CommandContext& ctx);
    void finish(CommandContext& ctx);
    State state_ = State::Select;
    core::ChangePropsCommand c_{};
    SelectObjectsPhase select_;
    bool done_ = false;
};

/// BLEND: `Select first object or [CONtinuity]:`, `Select second object:` -- a spline
/// between the two ends picked.
class BlendCurvesCommand final : public ICommand {
public:
    std::string name() const override { return "BLEND"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State : std::uint8_t { First, Continuity, Second };
    void first_prompt(CommandContext& ctx);
    static inline bool s_smooth_ = false;
    State state_ = State::First;
    core::Vec2 first_{};
    double radius_ = 0.0;
    bool done_ = false;
};

/// REDO (one undone step back) and MREDO (`Enter number of actions or [All/Last]:`).
class RedoStepsCommand final : public ICommand {
public:
    explicit RedoStepsCommand(bool many) : many_(many) {}
    std::string name() const override { return many_ ? "MREDO" : "REDO"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool many_;
    bool done_ = false;
};

/// TRACE: a width, then point after point -- a wide polyline of straight segments.
/// SOLID: points in threes and fours -- filled triangles and quadrilaterals, the third
/// and fourth points of one the first and second of the next.
class TraceSolidCommand final : public ICommand {
public:
    explicit TraceSolidCommand(bool solid) : solid_(solid) {}
    std::string name() const override { return solid_ ? "SOLID" : "TRACE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    void prompt(CommandContext& ctx);
    void finish_trace(CommandContext& ctx);
    static inline double s_width_ = 1.0; ///< TRACEWID
    bool solid_;
    bool width_asked_ = false;
    std::vector<core::Vec2> pts_;
    bool done_ = false;
};

/// CENTERMARK: a centre mark with centre lines on each circle or arc picked, until Enter.
/// CENTERLINE: two lines, then the centre line between them.
/// BOUNDARY: `Pick internal point:` until Enter -- closed polylines round each area.
class CenterBoundaryCommand final : public ICommand {
public:
    enum class Tool : std::uint8_t { CenterMark, CenterLine, Boundary };
    explicit CenterBoundaryCommand(Tool t) : tool_(t) {}
    std::string name() const override {
        return tool_ == Tool::CenterMark ? "CENTERMARK" : tool_ == Tool::CenterLine ? "CENTERLINE" : "BOUNDARY";
    }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    Tool tool_;
    std::optional<core::Vec2> first_;
    double radius_ = 0.0;
    int made_ = 0;
    bool done_ = false;
};

/// DIST: two points, or `[Multiple points]` with a running total; AutoCAD's full readout.
class DistCommand final : public ICommand {
public:
    std::string name() const override { return "DIST"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { First, Second, MultiFirst, MultiNext };
    void prompt_next(CommandContext& ctx);
    State state_ = State::First;
    bool done_ = false;
    core::Vec2 first_{};
    std::vector<core::Vec2> pts_;
    double total_ = 0.0;
};

class IdCommand final : public ICommand {
public:
    std::string name() const override { return "ID"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// AREA (issue #63): `Specify first corner point or [Object/Add area/Subtract area]
/// <Object>:`, points with `[Arc/Length/Undo]` then `[Arc/Length/Undo/Total] <Total>:`,
/// Add and Subtract modes with a running total (kept by the engine), and Object. The
/// arc sub-mode draws a tangent arc to the endpoint, or one through a Second pt.
/// MEASUREGEOM's ARea and Volume run the same flow (`set_volume` asks a height).
class AreaCommand final : public ICommand {
public:
    std::string name() const override { return "AREA"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    void set_volume(bool on) { volume_ = on; }

private:
    enum class State { First, Object, Next, ArcEnd, ArcSecond, ArcSecondEnd, Length, Height };
    void prompt_first(CommandContext& ctx);
    void prompt_next(CommandContext& ctx);
    void add_point(CommandContext& ctx, core::Vec2 p);
    void add_arc(CommandContext& ctx, const core::ConstructedArc& arc);
    void undo_segment(CommandContext& ctx);
    void finish_points(CommandContext& ctx);
    void submit(CommandContext& ctx, core::AreaQueryCommand q);
    void refresh_preview(CommandContext& ctx);
    State state_ = State::First;
    std::int8_t mode_ = 0;      ///< 0 one value, 1 Add, -1 Subtract
    bool first_total_ = true;   ///< the first Add / Subtract starts the total at zero
    std::vector<core::Vec2> pts_;
    std::vector<std::size_t> marks_; ///< pts_ size before each segment (Undo)
    core::Vec2 arc_second_{};
    double last_dir_ = 0.0;
    bool have_dir_ = false;
    bool volume_ = false;
    core::AreaQueryCommand pending_{}; ///< the query awaiting a height (Volume)
    bool done_ = false;
};

/// LIST: `Select objects:` then AutoCAD's block per object (kind, layer, space, handle,
/// colour, linetype, lineweight, the geometry, area and perimeter).
class ListCommand final : public ICommand {
public:
    std::string name() const override { return "LIST"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && select_.active(); }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    void finish(CommandContext& ctx);
    SelectObjectsPhase select_;
    bool done_ = false;
};

/// MEASUREGEOM (MEA): `[Distance/Radius/Angle/ARea/Volume/Quick/Mode]`, repeating until
/// eXit. Quick mode measures the object clicked (length, radius and angle, area).
class MeasureGeomCommand final : public ICommand {
public:
    std::string name() const override { return "MEASUREGEOM"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Option, DistFirst, DistSecond, Radius, AngleFirst, AngleSecond, Quick, Mode, Area };
    void prompt_option(CommandContext& ctx);
    State state_ = State::Option;
    bool measured_ = false; ///< after the first measurement the default is eXit
    bool quick_ = false;    ///< Mode: Quick (the default in AutoCAD 2020+) or Standard
    core::Vec2 first_{};
    std::unique_ptr<AreaCommand> area_;
    bool done_ = false;
};

/// MASSPROP: the area properties (area, perimeter, bounding box, centroid, moments,
/// radii of gyration, principal moments) of the selected closed shapes.
class MassPropCommand final : public ICommand {
public:
    std::string name() const override { return "MASSPROP"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && select_.active(); }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    void finish(CommandContext& ctx);
    SelectObjectsPhase select_;
    bool done_ = false;
};

/// TIME: the drawing's times, then `[Display/ON/OFF/Reset]` for the elapsed timer.
class TimeCommand final : public ICommand {
public:
    std::string name() const override { return "TIME"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// STATUS: the drawing's counts, extents, modes and current settings.
class StatusCommand final : public ICommand {
public:
    std::string name() const override { return "STATUS"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext&) override { done_ = true; }
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// CAL: one expression at the command line (see calc.hpp); QUICKCALC opens the
/// calculator palette, or falls back to the same prompt.
class CalCommand final : public ICommand {
public:
    explicit CalCommand(bool palette) : palette_(palette) {}
    std::string name() const override { return palette_ ? "QUICKCALC" : "CAL"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool palette_;
    bool done_ = false;
};

/// DWGPROPS: the Drawing Properties dialog (General, Summary, Statistics, Custom), or
/// the summary printed where there is no dialog.
class DwgPropsCommand final : public ICommand {
public:
    std::string name() const override { return "DWGPROPS"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext&) override { done_ = true; }
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// STRETCH (AutoCAD S), prompt for prompt:
///
///   Select objects to stretch by crossing-window or crossing-polygon...
///   Select objects:                                   -- Enter / right-click ends it
///   Specify base point or [Displacement] <Displacement>:
///   Specify second point or <use first point as displacement>:
///
/// Objects already selected when the command starts skip the first prompt (noun-verb).
/// During the second-point step the whole selection is previewed stretched under the
/// cursor, live, honouring ortho. Which vertices move is AutoCAD's rule, decided by the
/// engine from the crossing windows that built the selection.
class StretchCommand final : public ICommand {
public:
    std::string name() const override { return "STRETCH"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return mode_ == Mode::Select; }

private:
    enum class Mode { Select, Base, Displacement, Second };
    void begin_base(CommandContext& ctx);
    void finish(CommandContext& ctx, core::Vec2 delta);

    bool done_ = false;
    Mode mode_ = Mode::Select;
    core::Vec2 base_{};
};

class TableCommand final : public ICommand {
public:
    std::string name() const override { return "TABLE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class Mode { Rows, Cols, ColWidth, RowHeight, Place };
    bool done_ = false;
    Mode mode_ = Mode::Rows;
    int rows_ = 4;
    int cols_ = 3;
    double col_w_ = 40.0;
    double row_h_ = 8.0;
};

/// TRIM / EXTEND as AutoCAD has them. Quick mode (TRIMEXTENDMODE 1) takes every object as
/// a cutting edge: a pick trims the object there between its nearest crossings, a press
/// and drag on empty space draws a freehand path and two clicks a fence that trim all they
/// cross, and what has nothing to trim it to is deleted. Standard mode asks for the cutting
/// edges first (Enter: every object) and offers Fence, Crossing and Edge (an edge counted
/// along its extension, EDGEMODE). Shift-select swaps trim and extend; cuTting edges
/// (Boundary edges), Crossing, mOde, Project, eRase and Undo. Every pick, path or erase is
/// one undo step.
class TrimCommand final : public ICommand {
public:
    explicit TrimCommand(bool extend = false) : extend_(extend) {}
    std::string name() const override { return extend_ ? "EXTEND" : "TRIM"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool wants_freehand() const override { return !done_ && state_ == State::Objects; }
    void freehand(CommandContext& ctx, const std::vector<core::Vec2>& path) override;
    bool in_selection_phase() const override {
        return !done_ && (state_ == State::Edges || state_ == State::Erase) && select_.active();
    }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

    inline static int s_mode_ = 1;     ///< TRIMEXTENDMODE: 1 Quick, 0 Standard
    inline static int s_edgemode_ = 0; ///< EDGEMODE: 1 an edge counts along its extension
    inline static int s_projmode_ = 1; ///< PROJMODE: 0 None, 1 UCS, 2 View (2D: kept, no effect)

private:
    enum class State : std::uint8_t { Edges, Objects, Fence, Corner, Mode, Project, Edge, Erase };
    void prompt(CommandContext& ctx);
    void begin_edges(CommandContext& ctx);
    void end_edges(CommandContext& ctx);
    void end_erase(CommandContext& ctx);
    void finish(CommandContext& ctx);
    void path(CommandContext& ctx, std::vector<core::Vec2> points, bool extend, bool window = false);
    void step(CommandContext& ctx); ///< a new undo step after the first
    State state_ = State::Objects;
    bool extend_ = false;
    bool done_ = false;
    int picks_ = 0;
    std::vector<core::Vec2> fence_; ///< a fence being drawn
    bool fence_open_ = false;       ///< typed Fence: points until Enter (else two picks)
    std::optional<core::Vec2> corner_; ///< Crossing's first corner
    SelectObjectsPhase select_;
};

/// The layer tools. LAYOFF and LAYFRZ take one object after another (`[Undo]` takes the last
/// back) until Enter; LAYLCK, LAYULK and LAYMCUR take one; LAYCUR and LAYISO take a
/// selection (objects selected beforehand at once); LAYUNISO, LAYON and LAYTHW act at once.
class LayToolCommand final : public ICommand {
public:
    enum class Kind : std::uint8_t { Off, Freeze, Lock, Unlock, MakeCurrent, ToCurrent, Isolate, Unisolate, AllOn, AllThaw };
    explicit LayToolCommand(Kind kind) : kind_(kind) {}
    std::string name() const override;
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && select_.active(); }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    void prompt(CommandContext& ctx);
    void finish_selection(CommandContext& ctx);
    Kind kind_;
    int changed_ = 0; ///< LAYOFF / LAYFRZ: objects taken so far, for [Undo]
    SelectObjectsPhase select_;
    bool done_ = false;
};

/// A whole-number setting typed as a command: `Enter new value for NAME <v>:` (TRIMEXTENDMODE,
/// EDGEMODE, PROJMODE).
class IntVarCommand final : public ICommand {
public:
    IntVarCommand(std::string var, int lo, int hi, int* value)
        : var_(std::move(var)), lo_(lo), hi_(hi), value_(value) {}
    std::string name() const override { return var_; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::string var_;
    int lo_;
    int hi_;
    int* value_;
    bool done_ = false;
};

/// COPYCLIP / CUTCLIP (Ctrl+C / Ctrl+X) and COPYBASE / CUTBASE (a base point first): the
/// objects go to the clipboard -- the drawing's own, and the system's as Musa CAD objects
/// and a picture other programs take.
class ClipCopyCommand final : public ICommand {
public:
    ClipCopyCommand(bool cut, bool with_base) : cut_(cut), with_base_(with_base) {}
    std::string name() const override {
        return cut_ ? (with_base_ ? "CUTBASE" : "CUTCLIP") : (with_base_ ? "COPYBASE" : "COPYCLIP");
    }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && select_.active(); }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    void finish(CommandContext& ctx);
    bool cut_;
    bool with_base_;
    std::optional<core::Vec2> base_;
    SelectObjectsPhase select_;
    bool done_ = false;
};

/// PASTECLIP (Ctrl+V): what the clipboard holds, at `Specify insertion point:` -- Musa CAD
/// objects (from this window or another; they follow the cursor), an image (embedded,
/// its lower-left corner at the point) or text (an MTEXT). PASTEORIG: objects at their own
/// coordinates, no point asked.
class PasteClipCommand final : public ICommand {
public:
    explicit PasteClipCommand(bool original) : original_(original) {}
    std::string name() const override { return original_ ? "PASTEORIG" : "PASTECLIP"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool original_;
    std::string kind_;
    bool done_ = false;
};

class RotateCommand final : public ICommand {
public:
    std::string name() const override { return "ROTATE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    /// AutoCAD's flow: base point; angle or [Copy/Reference]; Reference asks the
    /// reference angle (a value, or two points), then the new angle or [Points] (a
    /// value, a point from the base, or two points).
    enum class State : std::uint8_t { Select, Base, Angle, RefAngle, RefSecond, NewAngle, NewFirst, NewSecond };
    State state_ = State::Base;
    std::optional<core::Vec2> base_;
    core::Vec2 first_{};     ///< the first of a two-point angle
    bool copy_ = false;      ///< [Copy]: rotate copies, keep the originals
    double ref_angle_ = 0.0; ///< [Reference]: rotation = new - reference
    bool done_ = false;
    void commit(CommandContext& ctx, double angle);
    void prompt_angle(CommandContext& ctx);
    void begin_base(CommandContext& ctx);
    SelectObjectsPhase select_;

public:
    bool in_selection_phase() const override { return !done_ && state_ == State::Select; }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;
};

class ScaleCommand final : public ICommand {
public:
    std::string name() const override { return "SCALE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    /// AutoCAD's flow: base point; factor or [Copy/Reference]; Reference asks the
    /// reference length (a value, or two points), then the new length or [Points] (a
    /// value, a point from the base, or two points). A picked factor is the distance
    /// from the base point in drawing units.
    enum class State : std::uint8_t { Select, Base, Factor, RefLength, RefSecond, NewLength, NewFirst, NewSecond };
    State state_ = State::Base;
    std::optional<core::Vec2> base_;
    core::Vec2 first_{};   ///< the first of a two-point length
    bool copy_ = false;    ///< [Copy]
    double ref_len_ = 1.0; ///< [Reference]: factor = new length / reference length
    bool done_ = false;
    void commit(CommandContext& ctx, double factor);
    void prompt_factor(CommandContext& ctx);
    void begin_base(CommandContext& ctx);
    SelectObjectsPhase select_;

public:
    bool in_selection_phase() const override { return !done_ && state_ == State::Select; }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;
};

/// OSNAP (OS, DDOSNAP): the running object-snap settings dialog.
class OsnapCommand final : public ICommand {
public:
    std::string name() const override { return "OSNAP"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// The tracking system variables, each `Enter new value for NAME <current>:` --
/// POLARANG (the increment angle, degrees), POLARADDANG (additional angles, separated by
/// semicolons; `.` for none), POLARMODE (1 relative, 2 track along every polar angle,
/// 4 use the additional angles), POLARDIST (the PolarSnap distance), SNAPTYPE (0 grid,
/// 1 PolarSnap), AUTOSNAP (8 polar tracking, 16 object snap tracking) and TEMPOVERRIDES.
class TrackingVarCommand final : public ICommand {
public:
    enum class Var : std::uint8_t { PolarAng, PolarAddAng, PolarMode, PolarDist, SnapType, AutoSnap, TempOverrides };
    explicit TrackingVarCommand(Var var) : var_(var) {}
    std::string name() const override;
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    Var var_;
    bool done_ = false;
    [[nodiscard]] std::string current(CommandContext& ctx) const;
};

/// OSMODE: `Enter new value for OSMODE <4133>:` -- the running snaps as AutoCAD's bit sum.
class OsmodeCommand final : public ICommand {
public:
    std::string name() const override { return "OSMODE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// -OSNAP: `Enter list of object snap modes <End,Cen,Int,Ext>:` (END, MID, CEN, GCEN, NOD,
/// QUA, INT, EXT, INS, PER, TAN, NEA, APP, PAR, NONE, ALL) -- sets the running snaps
/// from the command line; Enter keeps them.
class OsnapModesCommand final : public ICommand {
public:
    std::string name() const override { return "-OSNAP"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// PURGE (AutoCAD PU). No prompts: an imported drawing's unused layers just go, and the
/// engine reports how many. Undo is deliberately NOT offered -- purging removes symbol
/// table entries nothing refers to, so there is no geometry to restore.
/// PURGE opens the Purge dialog; -PURGE (or PURGE where there is no dialog) runs
/// AutoCAD's prompts: the type, `Enter name(s) to purge <*>:`, `Verify each name to be
/// purged? [Yes/No] <Y>:` and then one question per name.
class PurgeCommand final : public ICommand {
public:
    explicit PurgeCommand(bool dialog = true) : dialog_(dialog) {}
    std::string name() const override { return dialog_ ? "PURGE" : "-PURGE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Type, Names, Verify, Each };
    void ask_next(CommandContext& ctx);
    bool dialog_;
    bool done_ = false;
    State state_ = State::Type;
    std::uint8_t what_ = 0;
    std::string pattern_ = "*";
    std::vector<std::pair<std::uint8_t, std::string>> queue_; ///< (type, name) still to ask
};

/// ALIGN (AutoCAD AL): fit the selection between two known points in one step, with an
/// optional uniform scale. Two source/destination pairs, then the scale question --
/// AutoCAD's 2D flow, without the third pair that only means anything in 3D.
class AlignCommand final : public ICommand {
public:
    std::string name() const override { return "ALIGN"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    /// AutoCAD's flow: one pair (Enter at the second source point) just moves; two pairs
    /// align, then `Specify third source point or <continue>:` and, with two pairs, the
    /// scale question. A rubber line runs from each source point to its destination.
    enum class State { Select, Src1, Dst1, Src2, Dst2, Src3, Dst3, Scale };
    State state_ = State::Src1;
    core::Vec2 src1_{};
    core::Vec2 dst1_{};
    core::Vec2 src2_{};
    core::Vec2 dst2_{};
    bool done_ = false;
    void align(CommandContext& ctx, bool scale);
    void begin_points(CommandContext& ctx);
    SelectObjectsPhase select_;

public:
    bool in_selection_phase() const override { return !done_ && state_ == State::Select; }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;
};

/// LENGTHEN (AutoCAD LEN): change the length of a line or arc. The mode is chosen
/// first, as in AutoCAD, then the amount, then the object -- and the end nearer the
/// pick is the one that moves.
class LengthenCommand final : public ICommand {
public:
    std::string name() const override { return "LENGTHEN"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    /// AutoCAD's flow: `Select an object to measure or [DElta/Percent/Total/DYnamic]:` --
    /// a pick there reports the length -- then the amount (Angle for an arc's), then
    /// `Select an object to change or [Undo]:` one object after another; DYnamic asks where
    /// the end goes. The mode and the amounts are kept for the session.
    enum class State { Measure, Amount, Pick, DynEnd };
    void prompt_measure(CommandContext& ctx);
    State state_ = State::Measure;
    inline static core::LengthenCommand::Mode s_mode_ = core::LengthenCommand::Mode::Total;
    inline static double s_delta_ = 0.0;
    inline static double s_percent_ = 100.0;
    inline static double s_total_ = 1.0;
    inline static double s_angle_ = 0.0; ///< DeltaAngle / TotalAngle, radians
    bool asking_angle_ = false;
    int changed_ = 0;
    core::Vec2 dyn_pick_{};
    bool done_ = false;
};

/// BREAK (AutoCAD BR) and BREAKATPOINT.
///
/// AutoCAD's flow is unusual and worth keeping: the pick that SELECTS the object is
/// also the first break point, so the common case is two clicks. `First point` re-asks
/// for it when the selecting click was not where the break should be.
class BreakCommand final : public ICommand {
public:
    /// `at_point` true = BREAKATPOINT: split with no gap, one point only.
    explicit BreakCommand(bool at_point = false) : at_point_(at_point) {}

    std::string name() const override { return at_point_ ? "BREAKATPOINT" : "BREAK"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Select, Second, FirstAgain, AtPoint };
    bool at_point_ = false;
    State state_ = State::Select;
    core::Vec2 pick_{};
    core::Vec2 p1_{};
    bool done_ = false;
};

/// ELLIPSE (AutoCAD EL): axis-endpoint and Center methods, the Rotation option for the
/// second axis, and Arc (elliptical arc) with start/end by angle, parameter or included
/// angle. Commits a real ellipse entity; the second axis may come out longer than the
/// first, in which case the axes swap so the stored major is the longer one (AutoCAD's
/// rule), and arc angles are still measured from the FIRST axis the user gave.
class EllipseCommand final : public ICommand {
public:
    std::string name() const override { return "ELLIPSE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State {
        Start,      ///< first axis endpoint or [Arc/Center]
        Center,     ///< centre point (Center method)
        AxisEnd,    ///< axis endpoint after a centre
        OtherEnd,   ///< other endpoint of the first axis
        OtherDist,  ///< distance to the other axis or [Rotation]
        Rotation,   ///< rotation angle about the major axis
        ArcStart,   ///< start angle or [Parameter]
        ArcEnd,     ///< end angle or [Parameter/Included angle]
        ArcIncluded ///< included angle
    };
    void define_axes(CommandContext& ctx, double other_half);
    void after_axes(CommandContext& ctx);
    double param_from_input(const std::string& text, bool parameter_mode, bool* ok,
                            CommandContext& ctx) const;
    void commit(CommandContext& ctx);
    core::EllipseData shape() const;

    State state_ = State::Start;
    bool arc_ = false;
    bool swapped_ = false;
    bool start_param_mode_ = false;
    bool end_param_mode_ = false;
    core::Vec2 axis_a_{};
    core::Vec2 center_{};
    core::Vec2 first_dir_{1.0, 0.0}; ///< unit direction of the FIRST axis the user gave
    double half_ = 0.0;              ///< half-length of that first axis
    core::Vec2 major_{};
    double ratio_ = 1.0;
    double start_ = 0.0;
    double end_ = 0.0;
    bool done_ = false;

public:
    /// PELLIPSE 1: ELLIPSE draws a polyline of arcs (two per sixteenth of a turn) instead.
    inline static bool s_pellipse_ = false;
};

/// PELLIPSE: `Enter new value for PELLIPSE <0>:` -- 1 makes ELLIPSE draw polylines.
class PellipseCommand final : public ICommand {
public:
    std::string name() const override { return "PELLIPSE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// SPLINE (AutoCAD SPL). Method Fit (the curve passes through the picked points; the
/// Knots option chooses chord / square-root / uniform parameterisation) or CV (the
/// picked points are the control vertices; Degree 1..10). Undo drops the last point,
/// Close returns to the first. Tangency, fit tolerance and Object are reported as not
/// supported rather than silently ignored. Settings persist for the session.
class SplineCommand final : public ICommand {
public:
    std::string name() const override { return "SPLINE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { First, MethodPick, KnotsPick, DegreePick, Next };
    void prompt_first(CommandContext& ctx) const;
    void prompt_next(CommandContext& ctx) const;
    void refresh_preview(CommandContext& ctx) const;
    void finish(CommandContext& ctx, bool close);

    inline static bool s_fit_ = true;
    inline static int s_degree_ = 3;
    inline static int s_knots_ = 0;

    State state_ = State::First;
    bool fit_ = true;
    int degree_ = 3;
    int knots_ = 0;
    std::vector<core::Vec2> pts_;
    bool done_ = false;
};

/// PEDIT (PE): select a polyline (a line or arc is converted), then AutoCAD's option
/// menu: Close/Open, Join (through JOIN), Edit vertex (Insert/Delete/Move), Spline,
/// Decurve, Reverse, Undo. Width, Fit and Ltype gen are reported as not supported.
class PeditCommand final : public ICommand {
public:
    std::string name() const override { return "PEDIT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && select_.active(); }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    enum class State {
        Select, Option, JoinTargets, Vertex, VInsert, VDelete, VMoveFrom, VMoveTo,
        Accept,      ///< a line or arc picked: `Do you want to turn it into one? <Y>`
        MultiSelect, ///< [Multiple]: `Select objects:`
        Fuzz,        ///< [Multiple] > [Join]: the fuzz distance
        WidthVal,    ///< [Width]: one width for every segment
        VWidthAt,    ///< [Edit vertex] > [Width]: the vertex the segment leaves
        VWidthStart, ///< ... its starting width
        VWidthEnd,   ///< ... and its ending width
    };
    double vw_start_ = 0.0;
    bool multiple_ = false;
    SelectObjectsPhase select_;
    void prompt_option(CommandContext& ctx) const;
    void prompt_vertex(CommandContext& ctx) const;
    State state_ = State::Select;
    core::Vec2 pick_{};
    core::Vec2 vfrom_{};
    std::vector<core::Vec2> join_picks_;
    bool done_ = false;
};

/// BLOCK (B, -BLOCK): name, base point, then "Select objects:"; Enter makes the
/// selection a block definition and replaces it with one insert in place. Redefining an
/// existing name updates every insert of it.
class BlockCommand final : public ICommand {
public:
    std::string name() const override { return "BLOCK"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && state_ == State::Select; }

private:
    enum class State { Name, Base, Select } state_ = State::Name;
    std::string name_;
    core::Vec2 base_{};
    bool done_ = false;
};

/// INSERT (I, -INSERT): block name (? lists), insertion point, X and Y scale, rotation.
class InsertCommand final : public ICommand {
public:
    std::string name() const override { return "INSERT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Name, Point, ScaleX, ScaleY, Rotation, Attrib } state_ = State::Name;
    inline static std::string s_last_;
    std::string name_;
    core::Vec2 pos_{};
    double sx_ = 1.0;
    double sy_ = 1.0;
    double rot_ = 0.0;
    // Attribute values: one prompt per attribute that is neither Constant nor Preset.
    std::vector<core::BlockAttDefInfo> attdefs_;
    std::vector<std::string> values_;
    std::size_t attrib_index_ = 0;
    void next_attrib_or_finish(CommandContext& ctx);
    bool done_ = false;
};

/// ATTDEF (ATT, -ATTDEF): an attribute definition -- modes, tag, prompt, default, then
/// the text placement. In model space it shows its tag; BLOCK makes it an attribute.
class AttdefCommand final : public ICommand {
public:
    std::string name() const override { return "ATTDEF"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Modes, Tag, Prompt, Default, Point, Height, Rotation } state_ = State::Modes;
    void prompt_modes(CommandContext& ctx);
    std::uint8_t flags_ = 0;
    std::string tag_;
    std::string prompt_;
    std::string default_;
    core::Vec2 pos_{};
    inline static double s_height_ = 2.5;
    double height_ = 2.5;
    bool done_ = false;
};

/// REFEDIT: pick a block reference to edit its definition in place (its members become
/// a working set of ordinary objects); REFCLOSE ends the edit.
class RefeditCommand final : public ICommand {
public:
    std::string name() const override { return "REFEDIT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// REFSET: add objects to, or remove them from, the working set of a REFEDIT.
class RefsetCommand final : public ICommand {
public:
    std::string name() const override { return "REFSET"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && state_ == State::Select; }

private:
    enum class State { Option, Select } state_ = State::Option;
    bool add_ = true;
    bool done_ = false;
};

/// REFCLOSE: save the working set back into the block definition, or discard.
class RefcloseCommand final : public ICommand {
public:
    std::string name() const override { return "REFCLOSE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// IMAGEATTACH (IAT): a raster file (typed path, or ~ for the file dialog), whether to
/// embed it, then insertion point, scale and rotation.
class ImageAttachCommand final : public ICommand {
public:
    std::string name() const override { return "IMAGEATTACH"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { File, Embed, Point, Scale, Rotation } state_ = State::File;
    void ask_embed(CommandContext& ctx);
    std::string path_;
    bool embed_ = false;
    core::Vec2 pos_{};
    double scale_ = 1.0;
    bool done_ = false;
};

/// IMAGECLIP (ICL): a rectangular clipping boundary on an image, or Delete / ON / OFF.
class ImageClipCommand final : public ICommand {
public:
    std::string name() const override { return "IMAGECLIP"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Pick, Option, Shape, First, Second, PolyFirst, PolyNext } state_ = State::Pick;
    core::Vec2 pick_{};
    core::Vec2 first_{};
    std::vector<core::Vec2> poly_;
    bool done_ = false;
};

/// IMAGEFRAME: 0 hidden, 1 shown and plotted, 2 shown on screen only.
class ImageFrameCommand final : public ICommand {
public:
    std::string name() const override { return "IMAGEFRAME"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// LAYOUT: Copy / Delete / New / Rename / Set / ? over the drawing's layouts.
class LayoutCommand final : public ICommand {
public:
    std::string name() const override { return "LAYOUT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Option, Name, NewName } state_ = State::Option;
    core::LayoutCommand::Op op_ = core::LayoutCommand::Op::New;
    bool set_ = false; ///< the Set option (switch), which is not a LayoutCommand op
    std::string name_;
    bool done_ = false;
};

/// MODEL: back to model space. PSPACE: to the last layout used (viewports: MSPACE).
class ModelSpaceCommand final : public ICommand {
public:
    explicit ModelSpaceCommand(bool to_paper) : to_paper_(to_paper) {}
    std::string name() const override { return to_paper_ ? "PSPACE" : "MODEL"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext&) override { done_ = true; }
    bool done() const override { return done_; }

private:
    bool to_paper_;
    bool done_ = false;
};

/// MVIEW: a viewport on the layout from two corners (or Fit: the sheet inside a margin),
/// showing the whole model; ON / OFF / Scale / Center adjust a picked viewport.
class MviewCommand final : public ICommand {
public:
    std::string name() const override { return "MVIEW"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { First, Second, PickOnOff, PickScale, Scale, PickCenter, Center } state_ = State::First;
    core::Vec2 first_{};
    core::Vec2 pick_{};
    int on_ = 1;
    bool done_ = false;
};

/// MSPACE: edit model space through a viewport (pick one, or double-click inside one).
class MspaceCommand final : public ICommand {
public:
    std::string name() const override { return "MSPACE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// VPORTS (-VPORTS, VIEWPORTS): tiled model-space viewports. 2 / 3 / 4 split the current
/// viewport (Horizontal / Vertical, and for three the side the large one takes); SIngle
/// keeps the current view in one viewport; Join merges two that share an edge; Save /
/// Restore / Delete / ? manage named configurations.
class VportsCommand final : public ICommand {
public:
    std::string name() const override { return "VPORTS"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Option, Two, Three, SaveName, RestoreName, DeleteName, JoinDominant, JoinOther } state_ =
        State::Option;
    void prompt_option(CommandContext& ctx);
    void apply_split(CommandContext& ctx, const char* kind);
    std::size_t dominant_ = 0;
    bool done_ = false;
};

/// EATTEDIT: pick a block reference; its attribute values open in the Enhanced Attribute
/// Editor (a dialog, through the view). -ATTEDIT stays the command-line form.
class EatteditCommand final : public ICommand {
public:
    std::string name() const override { return "EATTEDIT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// BATTMAN: the Block Attribute Manager (a dialog, through the view): a block's
/// attribute definitions -- edit, reorder, remove -- and sync its references.
class BattmanCommand final : public ICommand {
public:
    std::string name() const override { return "BATTMAN"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// VPLAYER: layer visibility per viewport. Freeze / Thaw layers (named, or those of picked
/// objects) in the current viewport, all viewports or a selected one; Reset restores a
/// viewport's defaults; Newfrz creates layers frozen in every viewport; Vpvisdflt sets a
/// layer's default for new viewports; ? lists what each viewport freezes.
class VplayerCommand final : public ICommand {
public:
    std::string name() const override { return "VPLAYER"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && state_ == State::SelectObjects; }

private:
    enum class State {
        Option,
        Layers,
        SelectObjects,
        Target,
        PickViewport,
        NewNames,
        DefaultLayers,
        DefaultValue
    } state_ = State::Option;
    void prompt_option(CommandContext& ctx);
    void ask_target(CommandContext& ctx);
    void finish(CommandContext& ctx, core::SetViewportLayerFreezeCommand::Target target,
                core::Vec2 pick);

    core::SetViewportLayerFreezeCommand::Op op_ = core::SetViewportLayerFreezeCommand::Op::Freeze;
    std::vector<std::string> names_;
    bool from_selection_ = false;
    bool default_mode_ = false; ///< the object selection feeds Vpvisdflt, not Freeze/Thaw
    bool done_ = false;
};

/// XREF (XR): ? / Attach (a drawing file, insertion point, scale, rotation) / Detach /
/// Reload. An xref is a block that follows its file.
class XrefCommand final : public ICommand {
public:
    std::string name() const override { return "XREF"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Option, File, Point, Scale, Rotation, Name } state_ = State::Option;
    bool detach_ = false;
    std::string path_;
    core::Vec2 pos_{};
    double scale_ = 1.0;
    bool done_ = false;
};

/// ATTDISP: show every attribute, hide every attribute, or let each keep its own mode.
class AttdispCommand final : public ICommand {
public:
    std::string name() const override { return "ATTDISP"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// ATTEDIT (-ATTEDIT): change one attribute value (by tag, or all) of a block reference.
class AtteditCommand final : public ICommand {
public:
    std::string name() const override { return "ATTEDIT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Pick, Tag, Value } state_ = State::Pick;
    core::Vec2 pick_{};
    std::string tag_;
    bool done_ = false;
};

/// WBLOCK (W): a block by name, or the whole drawing, written to a .musa file.
class WblockCommand final : public ICommand {
public:
    std::string name() const override { return "WBLOCK"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Name, Path } state_ = State::Name;
    std::string name_;
    bool done_ = false;
};

/// REGEN (RE): rebuild and republish the scene.
class RegenCommand final : public ICommand {
public:
    std::string name() const override { return "REGEN"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// STYLE (ST, -STYLE): the command-line flow -- name (existing or new), font, fixed
/// height (0 = ask at TEXT), width factor, obliquing angle; backwards / upside-down /
/// vertical are reported as not supported. The style becomes current.
class StyleCommand final : public ICommand {
public:
    std::string name() const override { return "STYLE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Name, Font, Height, Width, Oblique, Backwards, Upside, Vertical };
    State state_ = State::Name;
    core::TextStyle ts_{};
    bool done_ = false;
};

/// UNITS (UN, -UNITS): the command-line flow -- linear format and precision, angle
/// format and precision, direction of angle 0, clockwise -- stored with the drawing.
/// UNITS opens the Drawing Units dialog; -UNITS (or UNITS with no dialog) prints
/// AutoCAD's numbered format tables and asks `Enter choice, 1 to 5 <2>:` and the rest.
class UnitsCommand final : public ICommand {
public:
    explicit UnitsCommand(bool dialog = true) : dialog_(dialog) {}
    std::string name() const override { return dialog_ ? "UNITS" : "-UNITS"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Linear, LinearPrecision, Angular, AngularPrecision, Base, Clockwise };
    void prompts(CommandContext& ctx);
    bool dialog_;
    State state_ = State::Linear;
    core::DrawingUnits u_{};
    bool done_ = false;
};

/// INSUNITS: the drawing's insertion unit (0..20; AutoCAD's numbering).
class InsunitsCommand final : public ICommand {
public:
    std::string name() const override { return "INSUNITS"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// SELECT: a "Select objects:" step on its own; the set stays for the next command
/// (and is what Previous recalls).
class SelectCommand final : public ICommand {
public:
    std::string name() const override { return "SELECT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_; }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    SelectObjectsPhase select_;
    bool done_ = false;
};

/// SELECTSIMILAR: objects of the same kind and properties as the selected ones
/// (SELECTSIMILARMODE says which properties count).
class SelectSimilarCommand final : public ICommand {
public:
    std::string name() const override { return "SELECTSIMILAR"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && selecting_; }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;
    inline static std::uint32_t s_mode_ = 130; ///< SELECTSIMILARMODE

private:
    void finish(CommandContext& ctx);
    SelectObjectsPhase select_;
    bool selecting_ = false;
    bool done_ = false;
};

/// SELECTSIMILARMODE: the bit sum (1 colour, 2 layer, 4 linetype, 8 linetype scale, 16
/// lineweight, 32 plot style, 64 object style, 128 name).
class SelectSimilarModeCommand final : public ICommand {
public:
    std::string name() const override { return "SELECTSIMILARMODE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// QSELECT and FILTER open the Quick Select dialog (FILTER with several conditions).
class QSelectCommand final : public ICommand {
public:
    explicit QSelectCommand(bool filter) : filter_(filter) {}
    std::string name() const override { return filter_ ? "FILTER" : "QSELECT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext&) override { done_ = true; }
    bool done() const override { return done_; }

private:
    bool filter_;
    bool done_ = false;
};

/// ISOLATEOBJECTS (0), HIDEOBJECTS (1) and UNISOLATEOBJECTS (2).
class IsolateCommand final : public ICommand {
public:
    explicit IsolateCommand(std::uint8_t mode) : mode_(mode) {}
    std::string name() const override {
        return mode_ == 0 ? "ISOLATEOBJECTS" : mode_ == 1 ? "HIDEOBJECTS" : "UNISOLATEOBJECTS";
    }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && selecting_; }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    void finish(CommandContext& ctx);
    std::uint8_t mode_;
    SelectObjectsPhase select_;
    bool selecting_ = false;
    bool done_ = false;
};

/// The selection system variables (PICKBOX, PICKFIRST, PICKADD, PICKAUTO, PICKDRAG,
/// HIGHLIGHT, SELECTIONPREVIEW, SELECTIONCYCLING): `Enter new value for X <current>:`,
/// kept by the viewport.
class SelectionSettingCommand final : public ICommand {
public:
    explicit SelectionSettingCommand(std::string var) : var_(std::move(var)) {}
    std::string name() const override { return var_; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::string var_;
    bool done_ = false;
};

/// CELTSCALE, PSLTSCALE and MSLTSCALE as system variables.
class LtscaleVarCommand final : public ICommand {
public:
    explicit LtscaleVarCommand(std::string var) : var_(std::move(var)) {}
    std::string name() const override { return var_; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::string var_;
    bool done_ = false;
};

/// AUDIT: "Fix any errors detected? [Yes/No] <N>:" then the engine's report.
class AuditDrawingCommand final : public ICommand {
public:
    std::string name() const override { return "AUDIT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// DONUT (DO): inside diameter, outside diameter, then centres until Enter. Drawn as a
/// SOLID hatch with two circular loops (an inside diameter of 0 gives a filled disc):
/// the polyline-with-width AutoCAD uses has no counterpart here yet.
class DonutCommand final : public ICommand {
public:
    std::string name() const override { return "DONUT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    /// A diameter is typed, or shown by two points (InnerSecond / OuterSecond).
    enum class State { Inner, InnerSecond, Outer, OuterSecond, Center } state_ = State::Inner;
    inline static double s_inner_ = 0.5;
    inline static double s_outer_ = 1.0;
    double inner_ = 0.5;
    double outer_ = 1.0;
    core::Vec2 first_{};
    bool done_ = false;
    void take_inner(CommandContext& ctx, double v);
    void take_outer(CommandContext& ctx, double v);
};

/// FILL (`Enter mode [ON/OFF] <ON>:`) and FILLMODE (`Enter new value for FILLMODE <1>:`):
/// whether wide polylines, solids and hatches are filled.
class FillCommand final : public ICommand {
public:
    explicit FillCommand(bool sysvar) : sysvar_(sysvar) {}
    std::string name() const override { return sysvar_ ? "FILLMODE" : "FILL"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool sysvar_ = false;
    bool done_ = false;
};

/// PLINEWID: `Enter new value for PLINEWID <0.0000>:` -- the width new polylines start with.
class PlinewidCommand final : public ICommand {
public:
    std::string name() const override { return "PLINEWID"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// VIEW (V): Save / Restore / Delete / ? / Window, on the drawing's named-view table.
/// Orthographic and Ucs are reported as not applicable (2D).
class ViewCommand final : public ICommand {
public:
    std::string name() const override { return "VIEW"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Option, SaveName, RestoreName, DeleteName, WinFirst, WinSecond, WinName };
    State state_ = State::Option;
    core::Vec2 w0_{};
    core::Vec2 w1_{};
    bool done_ = false;
};

/// GROUP (G): "Select objects or [Name/Description]:", Enter makes the selection a group
/// (unnamed groups are "*A1", "*A2", ... as in AutoCAD). Group creation is not on the
/// undo stack (it changes no geometry); UNGROUP reverses it.
class GroupCommand final : public ICommand {
public:
    std::string name() const override { return "GROUP"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && state_ == State::Select; }

private:
    enum class State { Select, Name, Description } state_ = State::Select;
    std::string name_;
    std::string description_;
    bool done_ = false;
};

/// -GROUP: `[?/Order/Add/Remove/Explode/REName/Selectable/Create] <Create>`.
class DashGroupCommand final : public ICommand {
public:
    std::string name() const override { return "-GROUP"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && state_ == State::Select; }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    enum class State { Option, Name, NewName, Description, Selectable, From, To, Select };
    void prompt_option(CommandContext& ctx);
    void finish_select(CommandContext& ctx);
    State state_ = State::Option;
    char op_ = 'C'; ///< ?, O, A, R, E, N (rename), S, C
    std::string name_;
    std::string text_;
    int from_ = 0;
    bool done_ = false;
    SelectObjectsPhase select_;
};

/// GROUPEDIT: `Select group or [Name]:` then `[Add objects/Remove objects/REName]`.
class GroupEditCommand final : public ICommand {
public:
    std::string name() const override { return "GROUPEDIT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && state_ == State::Select; }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    enum class State { Pick, Name, Option, NewName, Select };
    void prompt_option(CommandContext& ctx);
    State state_ = State::Pick;
    core::GroupEditCommand cmd_{};
    bool done_ = false;
    SelectObjectsPhase select_;
};

/// UNGROUP: pick a member (or give a name) to dissolve the group.
class UngroupCommand final : public ICommand {
public:
    std::string name() const override { return "UNGROUP"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool by_name_ = false;
    bool done_ = false;
};

/// PICKSTYLE: 1 = picking a group member selects its whole group, 0 = members only.
class PickStyleCommand final : public ICommand {
public:
    std::string name() const override { return "PICKSTYLE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// XLINE (AutoCAD XL) and RAY: construction lines. XLINE offers Hor/Ver/Ang/Bisect and
/// the default two-point form (all repeat until Enter); Offset is deferred (it needs a
/// picked reference, noted in docs/COMMANDS.md). RAY is start point + through points.
class XlineCommand final : public ICommand {
public:
    /// `ray` = the RAY command (semi-infinite from the start point).
    explicit XlineCommand(bool ray = false) : ray_(ray) {}

    std::string name() const override { return ray_ ? "RAY" : "XLINE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State {
        First, Through, Angle, BisectVertex, BisectStart, BisectEnd,
        RefLine, RefAngle, OffsetDist, OffsetLine, OffsetSide
    };
    void emit(CommandContext& ctx, core::Vec2 base, core::Vec2 dir);
    void preview(CommandContext& ctx, int mode);
    inline static double s_offset_ = 0.0; ///< XLINE Offset's last distance (0 = Through)
    core::Vec2 ref_pick_{};
    core::Vec2 offset_pick_{};

    bool ray_ = false;
    State state_ = State::First;
    core::Vec2 root_{};       ///< the through-point family's fixed point (XLINE) or start (RAY)
    core::Vec2 bvertex_{};
    core::Vec2 bstart_{};
    double angle_ = 0.0;      ///< radians, for the Ang option
    int mode_ = 0;            ///< 0 = two-point/through, 1 = horizontal, 2 = vertical, 3 = angle
    bool done_ = false;
};

/// REVCLOUD (AutoCAD). Arc length, Object (with Reverse direction), Rectangular,
/// Polygonal, and Freehand as a clicked path (the cursor is not tracked while a button
/// is held here, so the path is picked point by point and Enter closes it). Style
/// accepts Normal only; Modify is not offered. The lobes come from
/// core::polyline_ops::revcloud_from_path, so an Object conversion and a drawn cloud
/// produce identical arcs.
class RevcloudCommand final : public ICommand {
public:
    std::string name() const override { return "REVCLOUD"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State {
        Main,
        ArcMin,
        ArcMax,
        RectFirst,
        RectSecond,
        PathNext,
        ObjectPick,
        ObjectReverse,
        Style,
    };
    void main_prompt(CommandContext& ctx);
    void emit_cloud(CommandContext& ctx, const std::vector<core::Vec2>& path, bool closed);
    [[nodiscard]] double arc_len() const { return 0.5 * (min_arc_ + max_arc_); }

    inline static double s_min_arc_ = 0.5; ///< session defaults, as AutoCAD keeps them
    inline static double s_max_arc_ = 0.5;
    inline static int s_type_ = 2;            ///< 0 Rectangular, 1 Polygonal, 2 Freehand
    inline static bool s_calligraphy_ = false; ///< the Calligraphy style
    double min_arc_ = s_min_arc_;
    double max_arc_ = s_max_arc_;
    State state_ = State::Main;
    core::Vec2 first_{};
    std::vector<core::Vec2> path_;
    bool done_ = false;
};

/// EXPLODE (AutoCAD X). "Select objects:" then Enter, or a pre-selected set; the engine
/// decides what each kind becomes and reports what it could not break.
class ExplodeCommand final : public ICommand {
public:
    std::string name() const override { return "EXPLODE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && select_.active(); }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    void finish(CommandContext& ctx);
    SelectObjectsPhase select_;
    bool done_ = false;
};

/// POLYGON (AutoCAD POL). A regular n-gon, committed as an ordinary closed polyline.
///
/// Follows AutoCAD's two ways of sizing one: about a CENTRE, where the cursor distance
/// is either the circumradius (Inscribed) or the apothem (Circumscribed), or by one
/// EDGE, where two picks give a side and the polygon is built to its left.
class PolygonCommand final : public ICommand {
public:
    std::string name() const override { return "POLYGON"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Sides, Center, Fit, Radius, Edge1, Edge2 };
    void refresh_preview(CommandContext& ctx);

    State state_ = State::Sides;
    inline static int s_sides_ = 4;       ///< POLYSIDES: the last side count
    inline static bool s_inscribed_ = true;
    int sides_ = s_sides_;
    bool inscribed_ = s_inscribed_;
    core::Vec2 center_{};
    core::Vec2 edge1_{};
    bool done_ = false;
};

/// POINT (AutoCAD PO). Places a POINT entity at each pick and keeps going until Esc,
/// which is what AutoCAD does -- points are almost always placed in groups.
class PointCommand final : public ICommand {
public:
    std::string name() const override { return "POINT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// DIVIDE and MEASURE (AutoCAD DIV / ME). Both pick a curve and then place POINT marks
/// along it; they differ only in whether the second answer is a segment COUNT or a
/// segment LENGTH, so they share one state machine.
class DivideCommand final : public ICommand {
public:
    /// `measure` false = DIVIDE (into N equal parts), true = MEASURE (every N units).
    explicit DivideCommand(bool measure = false) : measure_(measure) {}

    std::string name() const override { return measure_ ? "MEASURE" : "DIVIDE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Pick, Amount, BlockName, BlockAlign };
    bool measure_ = false;
    State state_ = State::Pick;
    core::Vec2 pick_{};
    std::string block_; ///< [Block]
    bool align_ = true;
    void amount_prompt(CommandContext& ctx);
    bool done_ = false;
};

/// The whole AutoCAD array family in one state machine.
///
/// ARRAY (AR) and -ARRAY ask for the type, exactly as AutoCAD does; ARRAYRECT,
/// ARRAYPOLAR and ARRAYPATH jump straight to their own prompts. One class because the
/// three types differ only in which prompts they ask -- splitting them would duplicate
/// the selection guard, the parsing and the cancel path three ways.
///
/// ARRAYEDIT and ARRAYCLOSE are deliberately absent: both edit an ASSOCIATIVE array,
/// which is a parametric entity that remembers its source and parameters. Musa CAD's
/// arrays are non-associative -- the same thing AutoCAD's own -ARRAY produces -- so
/// there is no association to reopen. See docs/COMMANDS.md.
class ArrayCommand final : public ICommand {
public:
    /// Which prompts to ask. `Ask` is ARRAY/-ARRAY; the rest are the direct commands.
    enum class Type { Ask, Rect, Polar, Path };

    explicit ArrayCommand(Type type = Type::Ask) : type_(type) {}

    std::string name() const override {
        switch (type_) {
        case Type::Rect:
            return "ARRAYRECT";
        case Type::Polar:
            return "ARRAYPOLAR";
        case Type::Path:
            return "ARRAYPATH";
        case Type::Ask:
            break;
        }
        return "ARRAY";
    }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    /// The path pick selects a curve, but it is a COORDINATE pick here (the engine
    /// resolves the curve from the point), so the normal snap rules apply.
    bool wants_selection() const override { return false; }
    bool in_selection_phase() const override { return !done_ && state_ == State::Select; }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    void begin(CommandContext& ctx);
    SelectObjectsPhase select_;
    enum class State {
        Select,
        Type,
        // Rectangular
        Rows,
        Cols,
        RowSpace,
        ColSpace,
        Angle,
        // Polar
        Center,
        Count,
        Fill,
        RotateItems,
        // Path
        PathPick,
        PathMethod,
        PathCount,
        PathSpacing,
        PathAlign,
    };
    void begin_rect(CommandContext& ctx);
    void begin_polar(CommandContext& ctx);
    void begin_path(CommandContext& ctx);

    Type type_ = Type::Ask;
    State state_ = State::Type;
    int rows_ = 1;
    int cols_ = 1;
    double row_space_ = 0.0;
    double col_space_ = 0.0;
    int count_ = 1;
    double fill_ = 0.0;
    core::Vec2 center_{};
    // Path
    core::Vec2 path_pick_{};
    double path_spacing_ = 0.0;
    bool done_ = false;
};

/// EXTEND: every pick is its own undo step and `Undo` takes the last one back.

class FilletCommand final : public ICommand {
public:
    std::string name() const override { return "FILLET"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    /// AutoCAD's flow: `Select first object or [Undo/Polyline/Radius/Trim/Multiple]:`, then
    /// `Select second object or shift-select to apply corner or [Radius]:`. The radius and
    /// the Trim mode are kept for the session (FILLETRAD, TRIMMODE).
    enum class State : std::uint8_t { First, Second, Radius, TrimMode, Polyline };
    State state_ = State::First;
    State return_ = State::First; ///< where a [Radius] sub-step goes back to
    inline static double s_radius_ = 0.0;
    inline static bool s_trim_ = true;
    bool multiple_ = false;
    std::uint64_t last_group_ = 0; ///< the last fillet of this run, for [Undo]
    core::Vec2 pick1_{};
    bool done_ = false;
    void prompt_first(CommandContext& ctx);
    void after_fillet(CommandContext& ctx);
};

class ChamferCommand final : public ICommand {
public:
    std::string name() const override { return "CHAMFER"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    /// AutoCAD's flow: `Select first line or [Undo/Polyline/Distance/Angle/Trim/mEthod/
    /// Multiple]:`, then `Select second line or shift-select to apply corner or
    /// [Distance/Angle/Method]:`. The distances, the length and angle, the method and the
    /// Trim mode are kept for the session (CHAMFERA/B/C/D, CHAMMODE, TRIMMODE).
    enum class State : std::uint8_t {
        First, Second, Dist1, Dist2, AngleLen, AngleVal, Method, TrimMode, Polyline
    };
    State state_ = State::First;
    State return_ = State::First;
    inline static double s_dist1_ = 0.0;
    inline static double s_dist2_ = 0.0;
    inline static double s_length_ = 0.0;
    inline static double s_angle_ = 0.0; ///< degrees
    inline static bool s_angle_method_ = false;
    inline static bool s_trim_ = true;
    bool multiple_ = false;
    std::uint64_t last_group_ = 0;
    core::Vec2 pick1_{};
    bool done_ = false;
    [[nodiscard]] double dist1() const;
    [[nodiscard]] double dist2() const;
    void prompt_first(CommandContext& ctx);
    void after_chamfer(CommandContext& ctx);
};

// --- Annotation (Phase 13) -------------------------------------------------

class TextCommand final : public ICommand {
public:
    std::string name() const override { return "TEXT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    /// AutoCAD's flow: `Specify start point of text or [Justify/Style]:` (a justification
    /// may be typed here too), the point its justification asks for (two for Align and
    /// Fit), `Specify height <last>:` and `Specify rotation angle of text <last>:` (a
    /// value or a point; remembered), then the text line after line -- each Enter starts
    /// the next line under the last, an empty line ends the command.
    enum class State { Point, Justify, Style, Second, Height, Rotation, Content } state_ = State::Point;
    inline static double s_height_ = 2.5;   ///< TEXTSIZE
    inline static double s_rotation_ = 0.0; ///< the last rotation
    inline static std::uint8_t s_justify_ = 0;
    core::Vec2 pos_{};
    core::Vec2 align_{};
    std::uint8_t justify_ = s_justify_;
    double height_ = s_height_;
    double rotation_ = s_rotation_;
    std::string style_;         ///< the text style's name ("" = Standard)
    double style_factor_ = 1.0; ///< its width factor, for the line shown while typed
    bool fixed_height_ = false; ///< the style fixes the height: no height prompt
    int lines_ = 0;
    bool done_ = false;
    void read_style(CommandContext& ctx);
    void prompt_point(CommandContext& ctx);
    void after_point(CommandContext& ctx);
    void prompt_height(CommandContext& ctx);
    void prompt_rotation(CommandContext& ctx);
    void begin_content(CommandContext& ctx);
    void show_line(CommandContext& ctx);

public:
    bool free_text() const override { return !done_ && state_ == State::Content; }
};

/// JUSTIFYTEXT, SCALETEXT and TXT2MTXT: `Select objects:`, then what the tool asks.
class TextToolCommand final : public ICommand {
public:
    enum class Tool : std::uint8_t { Justify, Scale, ToMText };
    explicit TextToolCommand(Tool tool) : tool_(tool) {}
    std::string name() const override {
        return tool_ == Tool::Justify ? "JUSTIFYTEXT" : tool_ == Tool::Scale ? "SCALETEXT" : "TXT2MTXT";
    }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override { return !done_ && state_ == State::Select && select_.active(); }
    bool selection_removing() const override { return select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    enum class State { Select, Justify, Base, Height, Match, Factor, RefLength, RefNew } state_ = State::Select;
    inline static std::uint8_t s_justify_ = 0;
    inline static double s_height_ = 2.5;
    inline static double s_factor_ = 2.0;
    Tool tool_;
    SelectObjectsPhase select_;
    std::uint8_t base_ = 0;
    double ref_ = 1.0;
    bool done_ = false;
    void selected(CommandContext& ctx);
    void prompt_height(CommandContext& ctx);
    void scale(CommandContext& ctx, std::uint8_t mode, double value, core::Vec2 pick = {});
};

/// DIMLINEAR / DIMALIGNED share one state machine, parameterised by type/name.
class LinearDimensionCommand final : public ICommand {
public:
    LinearDimensionCommand(core::DimType type, std::string name)
        : type_(type), name_(std::move(name)) {}
    std::string name() const override { return name_; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    // Two points (First -> Second) or an object (Enter at the first prompt, or the
    // [Object] keyword: SelectObj), then Place with its options: Text asks for the
    // text, Rotated for the angle (a value, or two points: Rotation -> Rotation2).
    enum class State { First, Second, SelectObj, Place, Text, TextAngle, Rotation, Rotation2 } state_ = State::First;
    double text_angle_ = 0.0; ///< Angle: radians; 0 = along the dimension line
    void enter_place(CommandContext& ctx);
    void place_prompt(CommandContext& ctx);
    void show_preview(CommandContext& ctx) const;
    void place(CommandContext& ctx, core::Vec2 at);
    core::DimType type_;
    std::string name_;
    core::Vec2 a_{};
    core::Vec2 b_{};
    core::Vec2 obj_pick_{};
    bool object_ = false;      ///< placing a dimension of the selected object
    bool angle_fixed_ = false; ///< Horizontal / Vertical / Rotated chose the angle
    double angle_ = 0.0;       ///< ... this one (radians)
    core::Vec2 angle_from_{};  ///< Rotated by two points: the first
    std::string text_;         ///< the text typed at Text / Mtext ("" = the measurement)
    bool done_ = false;
};

/// DIMRADIUS / DIMDIAMETER: select a circle or arc, then place the dimension line.
/// The value comes from the entity's own centre + radius (object-aware).
class RadialDimensionCommand final : public ICommand {
public:
    RadialDimensionCommand(core::DimType type, std::string name)
        : type_(type), name_(std::move(name)) {}
    std::string name() const override { return name_; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Select, Place } state_ = State::Select;
    core::DimType type_;
    std::string name_;
    core::Vec2 obj_pick_{};
    std::string text_;   ///< typed at Mtext / Text ("" = the measurement)
    bool typing_ = false; ///< the next input is that text
    bool angling_ = false;    ///< the next input is the text's angle (Angle)
    double text_angle_ = 0.0; ///< radians; 0 = as the dimension lays it out
    bool done_ = false;
};

/// DIMORDINATE (DOR): a feature point, then the leader endpoint; the datum axis is
/// chosen from the leader's direction (a mostly vertical leader measures X), or forced
/// with [Xdatum/Ydatum]; Mtext / Text type the text (Angle is not there yet).
class OrdinateDimensionCommand final : public ICommand {
public:
    std::string name() const override { return "DIMORDINATE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Feature, End } state_ = State::Feature;
    core::Vec2 feature_{};
    int forced_ = -1; ///< -1 auto, 0 X datum, 1 Y datum
    std::string text_;
    bool typing_ = false;
    bool angling_ = false;
    double text_angle_ = 0.0;
    bool done_ = false;
};

/// DIMJOGGED (DJO): select an arc or circle, give the centre location override, the
/// dimension line location, then the jog location -- AutoCAD's four steps.
class JoggedDimensionCommand final : public ICommand {
public:
    std::string name() const override { return "DIMJOGGED"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Select, Override, Place, Jog } state_ = State::Select;
    core::Vec2 obj_pick_{};
    core::Vec2 override_{};
    core::Vec2 place_{};
    std::string text_;
    bool typing_ = false;
    bool angling_ = false;
    double text_angle_ = 0.0;
    bool done_ = false;
};

/// DIMARC (DAR): select an arc or a polyline arc segment, then place the dimension
/// arc; the value is the true arc length. Mtext / Text type the text; Partial, Leader
/// and Angle are not there yet.
class ArcLengthDimensionCommand final : public ICommand {
public:
    std::string name() const override { return "DIMARC"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Select, Place, Partial1, Partial2 } state_ = State::Select;
    core::Vec2 obj_pick_{};
    std::string text_;
    bool typing_ = false;
    bool angling_ = false;
    double text_angle_ = 0.0;
    bool leader_ = false;                       ///< [Leader]
    std::optional<core::Vec2> partial_from_{};  ///< [Partial]: the part of the arc
    std::optional<core::Vec2> partial_to_{};
    void place_prompt(CommandContext& ctx);
    bool done_ = false;
};

/// DIMANGULAR: select two lines (or polyline edges); the angle is read from the
/// entities' directions (object-aware).
class AngularDimensionCommand final : public ICommand {
public:
    std::string name() const override { return "DIMANGULAR"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    // Select (an arc, a circle, a line, or Enter for the vertex) -> Line2 / CircleEnd, or
    // Vertex -> First -> Second; then Place, with Text and Quadrant on the way.
    enum class State { Select, Line2, CircleEnd, Vertex, First, Second, Place, Text, TextAngle, Quadrant } state_ = State::Select;
    double text_angle_ = 0.0; ///< Angle: radians; 0 = upright on the arc
    void place_prompt(CommandContext& ctx);
    void show_preview(CommandContext& ctx) const;
    core::Vec2 pick1_{};
    core::Vec2 pick2_{};
    bool points_ = false;     ///< three points (vertex, endpoints) rather than an object
    core::Vec2 vertex_{};
    core::Vec2 end1_{};
    core::Vec2 end2_{};
    std::optional<core::Vec2> quadrant_{};
    std::string text_;
    bool done_ = false;
};

/// DIM (#57): AutoCAD's one command for every dimension, one after another until Enter.
/// An object picked is dimensioned by its kind -- a line aligned (a second line picked at
/// the placement prompt makes it angular), a circle by its diameter, an arc by its radius --
/// two points linearly; the dimension the hovered object would get is previewed. Angular,
/// Baseline, Continue and Ordinate run those commands; aliGn and Distribute arrange
/// dimensions already drawn; Layer sets DIMLAYER; Undo takes the last dimension back. The
/// kinds are the dimension commands themselves, run inside DIM.
class DimCommand final : public ICommand {
public:
    std::string name() const override { return "DIM"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void hover(CommandContext& ctx, std::optional<core::EntityKind> kind) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool in_selection_phase() const override {
        return !done_ && (sub_ ? sub_->in_selection_phase() : select_.active());
    }
    bool selection_removing() const override { return sub_ ? sub_->selection_removing() : select_.removing(); }
    void selection_gesture(CommandContext& ctx) override;

private:
    enum class State : std::uint8_t { Main, AlignBase, DistMethod, DistOffset, DistBase, Select, Layer };
    void prompt(CommandContext& ctx);
    void begin(CommandContext& ctx, std::unique_ptr<ICommand> sub);
    void after_sub(CommandContext& ctx);
    void arrange(CommandContext& ctx);
    State state_ = State::Main;
    std::unique_ptr<ICommand> sub_;
    bool sub_from_line_ = false;       ///< sub_ is placing a line's dimension (a second line: angular)
    core::Vec2 line_pick_{};
    int made_ = 0;                     ///< dimensions this run, for Undo
    bool align_ = true;                ///< the selection step is aliGn (else Distribute)
    bool equal_ = true;                ///< Distribute Equal (else Offset)
    double offset_ = 3.75;
    core::Vec2 base_{};
    SelectObjectsPhase select_;
    bool done_ = false;
};

/// LEADER: pick the arrow tip, the landing point, then enter the label.
class LeaderCommand final : public ICommand {
public:
    std::string name() const override { return "LEADER"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Tip, Knee, Content } state_ = State::Tip;
    core::Vec2 tip_{};
    core::Vec2 knee_{};
    bool done_ = false;
};

/// MTEXT (MT/T): pick two corners (insertion + wrap width), then enter paragraph
/// text. Wraps within the defined width across multiple lines.
class MTextCommand final : public ICommand {
public:
    std::string name() const override { return "MTEXT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { First, Second, Content } state_ = State::First;
    core::Vec2 c1_{};
    core::Vec2 pos_{};
    double width_ = 0.0;
    bool done_ = false;
};

/// QLEADER (LE/QLEADER): pick the arrow point, then leader vertices (Enter to
/// finish), then enter the annotation (MTEXT). Arrow + leader line + attached text.
class QLeaderCommand final : public ICommand {
public:
    std::string name() const override { return "QLEADER"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Arrow, Vertices, Content } state_ = State::Arrow;
    std::vector<core::Vec2> verts_;
    bool done_ = false;
};

/// TEXTEDIT / DDEDIT (ED): pick a text-bearing entity (TEXT / MTEXT / QLEADER
/// label), then type the new content. The scriptable/keyboard path to text edit;
/// same one-undo-group content change as the double-click editor.
class TextEditCommand final : public ICommand {
public:
    std::string name() const override { return "TEXTEDIT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    /// `Select an annotation object or [Undo/Mode]:`, then the new text; with
    /// TEXTEDITMODE 0 (Multiple) the command asks for the next object until Enter.
    enum class State { Pick, Mode, Content } state_ = State::Pick;
    core::Vec2 at_{};
    double radius_ = 0.0;
    int edits_ = 0;
    bool done_ = false;
    void prompt_pick(CommandContext& ctx);

public:
    inline static bool s_single_ = false; ///< TEXTEDITMODE 1
    bool free_text() const override { return !done_ && state_ == State::Content; }
};

/// TEXTEDITMODE: 0 TEXTEDIT repeats (Multiple), 1 it edits one object (Single).
class TextEditModeCommand final : public ICommand {
public:
    std::string name() const override { return "TEXTEDITMODE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// LTSCALE: set the global linetype scale (drawing-wide). Prompts for the factor,
/// then submits SetLtscaleCommand; all non-continuous entities re-dash live.
class LtscaleCommand final : public ICommand {
public:
    std::string name() const override { return "LTSCALE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// DWGIN / DWGOUT: one-shot view commands that trigger the external-converter DWG
/// import/export via ViewControl (the MainWindow owns the file dialog + converter).
class DwgInCommand final : public ICommand {
public:
    std::string name() const override { return "DWGIN"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext&) override { done_ = true; }
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

class DwgOutCommand final : public ICommand {
public:
    std::string name() const override { return "DWGOUT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext&) override { done_ = true; }
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// PR / PROPERTIES / PROPS / CH: toggle the Properties palette. A one-shot view
/// command -- it opens the panel via ViewControl and finishes immediately.
class PropertiesCommand final : public ICommand {
public:
    std::string name() const override { return "PROPERTIES"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext&) override { done_ = true; }
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// DIMSTYLE (D, DST, DDIM): the Dimension Style Manager -- the drawing's styles, Set
/// Current, New, Modify. -DIMSTYLE (and DIMSTYLE with no window) at the command line:
/// `Enter a dimension style option [Save/Restore/STatus/?] <Restore>:`.
class DimStyleCommand final : public ICommand {
public:
    explicit DimStyleCommand(bool dialog) : dialog_(dialog) {}
    std::string name() const override { return dialog_ ? "DIMSTYLE" : "-DIMSTYLE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Option, Restore, Save, Redefine } state_ = State::Option;
    void option_prompt(CommandContext& ctx);
    bool dialog_ = true;
    std::string save_name_;
    bool done_ = false;
};

/// A dimension variable (DIMTXT, DIMASZ, DIMBLK, DIMDEC, DIMDSEP, DIMZIN, DIMEXO, DIMEXE,
/// DIMTAD, DIMATFIT, DIMLWD, DIMCLRD, DIMCLRE, DIMCLRT): `Enter new value for DIMTXT
/// <2.5000>:` sets it in the current dimension style.
class DimVarCommand final : public ICommand {
public:
    explicit DimVarCommand(std::string var) : var_(std::move(var)) {}
    std::string name() const override { return var_; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::string var_;
    bool done_ = false;
};

/// OPTIONS (OP): open the application settings. One-shot view command.
class OptionsCommand final : public ICommand {
public:
    std::string name() const override { return "OPTIONS"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext&) override { done_ = true; }
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// PLOT / PRINT: open the plot dialog (PDF + printer). One-shot view command.
class PlotCommand final : public ICommand {
public:
    std::string name() const override { return "PLOT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext&) override { done_ = true; }
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

} // namespace musacad::command

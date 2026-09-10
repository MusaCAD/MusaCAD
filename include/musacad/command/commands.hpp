// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <vector>

#include "musacad/command/command.hpp"
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
    bool done_ = false;
};

class CircleCommand final : public ICommand {
public:
    std::string name() const override { return "CIRCLE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Center, Radius, Diameter } state_ = State::Center;
    core::Vec2 center_{};
    bool done_ = false;
};

class PolylineCommand final : public ICommand {
public:
    std::string name() const override { return "PLINE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    void prompt_next(CommandContext& ctx);
    std::vector<core::Vec2> points_;
    bool done_ = false;
};

class ArcCommand final : public ICommand {
public:
    std::string name() const override { return "ARC"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::vector<core::Vec2> points_;
    bool done_ = false;
};

class RectangleCommand final : public ICommand {
public:
    std::string name() const override { return "RECTANGLE"; }
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
        ChamferD1, ///< [Chamfer] first distance (before the first corner, as in AutoCAD)
        ChamferD2, ///< [Chamfer] second distance
        FilletR,   ///< [Fillet] radius
    } state_ = State::First;
    // Corner treatment. AutoCAD keeps the last chamfer distances / fillet radius as the
    // default for every later rectangle in the session, and setting one clears the
    // other -- both mirrored here through the session-wide statics.
    inline static double s_chamfer_d1_ = 0.0;
    inline static double s_chamfer_d2_ = 0.0;
    inline static double s_fillet_r_ = 0.0;
    double chamfer_d1_ = s_chamfer_d1_;
    double chamfer_d2_ = s_chamfer_d2_;
    double fillet_r_ = s_fillet_r_;
    core::Vec2 first_{};
    double length_ = 0.0;   ///< fixed width along X (0 => corner-to-corner, no fixed size)
    double width_ = 0.0;    ///< fixed width along Y
    double rotation_ = 0.0; ///< radians, applied about first_
    double area_ = 0.0;
    bool area_by_length_ = true; ///< Area option: user gave Length (else Width)
    bool has_dims_ = false;      ///< fixed (length_, width_) chosen -> quadrant-flip placement
    bool done_ = false;
};

class EraseCommand final : public ICommand {
public:
    std::string name() const override { return "ERASE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool wants_selection() const override { return true; }

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
    std::optional<core::Vec2> base_;
    bool done_ = false;
};

class CopyCommand final : public ICommand {
public:
    std::string name() const override { return "COPY"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::optional<core::Vec2> base_;
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
    enum class State { First, Second, Ask } state_ = State::First;
    core::Vec2 p1_{};
    core::Vec2 p2_{};
    bool done_ = false;
};

class OffsetCommand final : public ICommand {
public:
    std::string name() const override { return "OFFSET"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Distance, Object, Side } state_ = State::Distance;
    double distance_ = 0.0;
    core::Vec2 object_pick_{};
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

private:
    enum class State { Source, Targets } state_ = State::Source;
    std::vector<core::Vec2> picks_;
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
class ChainDimCommand final : public ICommand {
public:
    explicit ChainDimCommand(bool baseline) : baseline_(baseline) {}
    std::string name() const override { return baseline_ ? "DIMBASELINE" : "DIMCONTINUE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool baseline_ = false;
    bool done_ = false;
};

class DistCommand final : public ICommand {
public:
    std::string name() const override { return "DIST"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
    bool have_first_ = false;
    core::Vec2 first_{};
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

class AreaCommand final : public ICommand {
public:
    std::string name() const override { return "AREA"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

class ListCommand final : public ICommand {
public:
    std::string name() const override { return "LIST"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
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

class TrimCommand final : public ICommand {
public:
    std::string name() const override { return "TRIM"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
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
    std::optional<core::Vec2> base_;
    bool copy_ = false;      ///< [Copy]: rotate copies, keep the originals
    bool reference_ = false; ///< [Reference]: angle = new - reference
    double ref_angle_ = 0.0;
    bool have_ref_ = false;
    bool done_ = false;
};

class ScaleCommand final : public ICommand {
public:
    std::string name() const override { return "SCALE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::optional<core::Vec2> base_;
    bool copy_ = false;      ///< [Copy]
    bool reference_ = false; ///< [Reference]: factor = new length / reference length
    double ref_len_ = 1.0;
    bool have_ref_ = false;
    bool done_ = false;
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

/// -OSNAP: "Enter list of object snap modes:" (END,MID,CEN,NOD,QUA,INT,PER,TAN,NEA,INS,
/// APP,PAR,NONE,ALL) -- sets the running snaps from the command line.
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
class PurgeCommand final : public ICommand {
public:
    std::string name() const override { return "PURGE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
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
    enum class State { Src1, Dst1, Src2, Dst2, Scale };
    State state_ = State::Src1;
    core::Vec2 src1_{};
    core::Vec2 dst1_{};
    core::Vec2 src2_{};
    core::Vec2 dst2_{};
    bool done_ = false;
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
    enum class State { Mode, Amount, Pick };
    State state_ = State::Mode;
    core::LengthenCommand::Mode mode_ = core::LengthenCommand::Mode::Total;
    double value_ = 0.0;
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
    enum class State { Select, Second, FirstAgain };
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

private:
    enum class State { Select, Option, JoinTargets, Vertex, VInsert, VDelete, VMoveFrom, VMoveTo };
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
    enum class State { Pick, Option, Shape, First, Second } state_ = State::Pick;
    core::Vec2 pick_{};
    core::Vec2 first_{};
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

/// MSPACE: editing model space through a viewport is not available yet; says so.
class MspaceCommand final : public ICommand {
public:
    std::string name() const override { return "MSPACE"; }
    void start(CommandContext& ctx) override {
        ctx.echo("Editing model space through a viewport (MSPACE) is not available yet; use MODEL.");
        done_ = true;
    }
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext&) override { done_ = true; }
    bool done() const override { return done_; }

private:
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
class UnitsCommand final : public ICommand {
public:
    std::string name() const override { return "UNITS"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Linear, LinearPrecision, Angular, AngularPrecision, Base, Clockwise };
    State state_ = State::Linear;
    core::DrawingUnits u_{};
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
    enum class State { Inner, Outer, Center } state_ = State::Inner;
    inline static double s_inner_ = 0.5;
    inline static double s_outer_ = 1.0;
    double inner_ = 0.5;
    double outer_ = 1.0;
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
    enum class State { First, Through, Angle, BisectVertex, BisectStart, BisectEnd };
    void emit(CommandContext& ctx, core::Vec2 base, core::Vec2 dir);

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
    bool in_selection_phase() const override { return !done_; }

private:
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
    int sides_ = 4;
    bool inscribed_ = true;
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
    enum class State { Pick, Amount };
    bool measure_ = false;
    State state_ = State::Pick;
    core::Vec2 pick_{};
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

private:
    enum class State {
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

class ExtendCommand final : public ICommand {
public:
    std::string name() const override { return "EXTEND"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

class FilletCommand final : public ICommand {
public:
    std::string name() const override { return "FILLET"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Radius, First, Second } state_ = State::Radius;
    double radius_ = 0.0;
    core::Vec2 pick1_{};
    bool done_ = false;
};

class ChamferCommand final : public ICommand {
public:
    std::string name() const override { return "CHAMFER"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    // Distance method (Dist1->Dist2) or Angle method (AngleLen->AngleVal, default
    // 45 degrees), then pick the two lines.
    enum class State { Dist1, Dist2, AngleLen, AngleVal, First, Second } state_ = State::Dist1;
    double dist1_ = 0.0;
    double dist2_ = 0.0;
    double length_ = 0.0; // chamfer length on the first line (Angle method)
    core::Vec2 pick1_{};
    bool done_ = false;
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
    enum class State { Point, Height, Rotation, Content } state_ = State::Point;
    core::Vec2 pos_{};
    double height_ = 2.5;
    double rotation_ = 0.0;
    std::string style_;         ///< the current text style's name ("" = Standard)
    bool fixed_height_ = false; ///< the style fixes the height: no height prompt
    bool done_ = false;
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
    // Two-point flow: First -> Second -> Place. Object flow (via the [Object]
    // keyword or an empty first input): SelectObj -> ObjPlace.
    enum class State { First, Second, Place, SelectObj, ObjPlace } state_ = State::First;
    core::DimType type_;
    std::string name_;
    core::Vec2 a_{};
    core::Vec2 b_{};
    core::Vec2 obj_pick_{};
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
    bool done_ = false;
};

/// DIMORDINATE (DOR): a feature point, then the leader endpoint; the datum axis is
/// chosen from the leader's direction (a mostly vertical leader measures X), or forced
/// with [Xdatum/Ydatum]. Mtext/Text/Angle are reported as not supported.
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
    bool done_ = false;
};

/// DIMARC (DAR): select an arc or a polyline arc segment, then place the dimension
/// arc; the value is the true arc length. Partial/Leader/Mtext/Text/Angle are
/// reported as not supported.
class ArcLengthDimensionCommand final : public ICommand {
public:
    std::string name() const override { return "DIMARC"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Select, Place } state_ = State::Select;
    core::Vec2 obj_pick_{};
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
    enum class State { Line1, Line2, Place } state_ = State::Line1;
    core::Vec2 pick1_{};
    core::Vec2 pick2_{};
    bool done_ = false;
};

/// DIM: AutoCAD's smart all-in-one dimension. As the cursor moves over candidates
/// it previews the type it would create; on pick it reads the hovered entity kind
/// (circle -> diameter, arc -> radius, line/polyline -> linear) and dispatches to
/// the SAME object-aware machinery as DIMRADIUS/DIMDIAMETER/DIMLINEAR.
class DimCommand final : public ICommand {
public:
    std::string name() const override { return "DIM"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void hover(CommandContext& ctx, std::optional<core::EntityKind> kind) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Select, Place } state_ = State::Select;
    core::DimType type_ = core::DimType::Linear;
    core::Vec2 obj_pick_{};
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
    enum class State { Pick, Content } state_ = State::Pick;
    core::Vec2 at_{};
    double radius_ = 0.0;
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

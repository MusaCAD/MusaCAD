#pragma once

#include <vector>

#include "musacad/command/command.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/properties.hpp"

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
    enum class State { Center, Radius } state_ = State::Center;
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
    enum class State { First, Second } state_ = State::First;
    core::Vec2 first_{};
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
    bool done_ = false;
};

class ArrayCommand final : public ICommand {
public:
    std::string name() const override { return "ARRAY"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Type, Rows, Cols, RowSpace, ColSpace, Center, Count, Fill, RotateItems };
    State state_ = State::Type;
    int rows_ = 1;
    int cols_ = 1;
    double row_space_ = 0.0;
    int count_ = 1;
    double fill_ = 0.0;
    core::Vec2 center_{};
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
    enum class State { Line1, Line2 } state_ = State::Line1;
    core::Vec2 pick1_{};
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

} // namespace musacad::command

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// MLEADER and LEADER's options (#61).

#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"

using namespace musacad::core;
using musacad::command::CommandOutput;
using musacad::command::CommandProcessor;
using Catch::Approx;

namespace {
struct Out : CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
};

struct H {
    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    template <class T>
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
    void run(std::initializer_list<const char*> lines) {
        for (const char* l : lines) {
            proc.submit_line(l);
        }
    }
};
} // namespace

TEST_CASE("MLEADER: arrowhead, landing, text beside a horizontal landing") {
    H h;
    h.run({"MLD"});
    REQUIRE(h.out.prompt ==
            "Specify leader arrowhead location or [leader Landing first/Content first/Options] <Options>: ");
    h.run({"0,0"});
    REQUIRE(h.out.prompt == "Specify leader landing location: ");
    h.run({"10,10"});
    REQUIRE(h.out.prompt == "Enter annotation text: ");
    REQUIRE(h.proc.wants_free_text());
    h.run({"SEE DETAIL A"});
    const auto* m = h.last<AddMLeaderCommand>();
    REQUIRE(m != nullptr);
    REQUIRE(m->content == "SEE DETAIL A");
    REQUIRE(m->vertices.size() == 3); // tip, landing, the end of the landing
    REQUIRE(m->vertices[0] == Vec2{0, 0});
    REQUIRE(m->vertices[1] == Vec2{10, 10});
    REQUIRE(m->vertices[2].y == Approx(10.0));
    REQUIRE(m->vertices[2].x > 10.0);
    REQUIRE(m->block.attach == 3); // middle left, to the right of the landing
    REQUIRE(m->block.pos.x > m->vertices[2].x);
    REQUIRE_FALSE(h.proc.has_active_command());

    // Landing first, leftwards: the text to the left.
    h.run({"MLEADER", "L"});
    REQUIRE(h.out.prompt ==
            "Specify leader landing location or [leader arrowHead first/Content first/Options] <Options>: ");
    h.run({"0,0"});
    REQUIRE(h.out.prompt == "Specify leader arrowhead location: ");
    h.run({"10,-5", "NOTE"});
    m = h.last<AddMLeaderCommand>();
    REQUIRE(m->vertices[0] == Vec2{10, -5});
    REQUIRE(m->vertices[1] == Vec2{0, 0});
    REQUIRE(m->block.attach == 5);
    REQUIRE(m->vertices[2].x < 0.0);

    // Content first: the text, then the arrowhead.
    h.run({"MLEADER", "C"});
    h.run({"20,20", "LABEL", "0,0"});
    m = h.last<AddMLeaderCommand>();
    REQUIRE(m->content == "LABEL");
    REQUIRE(m->vertices[0] == Vec2{0, 0});

    // Options: three points, no landing, no text.
    h.run({"MLEADER", "O", "M", "3", "A", "N", "C", "N", "X"});
    REQUIRE(h.out.prompt ==
            "Specify leader arrowhead location or [leader Landing first/Content first/Options] <Options>: ");
    h.run({"0,0"});
    REQUIRE(h.out.prompt == "Specify next point: ");
    h.run({"5,5"});
    REQUIRE(h.out.prompt == "Specify leader landing location: ");
    h.run({"10,5"});
    m = h.last<AddMLeaderCommand>();
    REQUIRE(m->vertices.size() == 3);
    REQUIRE(m->content.empty());
    h.run({"MLEADER", "O", "M", "2", "A", "Y", "C", "M", "X"}); // back to the defaults
    h.proc.cancel();
}

TEST_CASE("LEADER: points until Enter, lines of annotation, Undo and Format") {
    H h;
    h.run({"LEADER"});
    REQUIRE(h.out.prompt == "Specify leader start point: ");
    h.run({"0,0"});
    REQUIRE(h.out.prompt == "Specify next point: ");
    h.run({"10,10"});
    REQUIRE(h.out.prompt == "Specify next point or [Annotation/Format/Undo] <Annotation>: ");
    h.run({""});
    REQUIRE(h.out.prompt == "Enter first line of annotation text or <options>: ");
    h.run({"SEE NOTE", ""});
    const auto* l = h.last<AddLeaderCommand>();
    REQUIRE(l != nullptr);
    REQUIRE(l->tip == Vec2{0, 0});
    REQUIRE(l->knee == Vec2{10, 10});
    REQUIRE(l->content == "SEE NOTE");

    h.run({"LEADER", "0,0", "5,5", "9,9", "U", "10,5", "F"});
    REQUIRE(h.out.prompt == "Enter leader format option [Spline/STraight/Arrow/None] <Exit>: ");
    h.run({"S"});
    REQUIRE(h.out.prompt == "Specify next point or [Annotation/Format/Undo] <Annotation>: ");
    h.run({"20,5", "A", "FIRST", "SECOND", ""});
    const auto* m = h.last<AddMLeaderCommand>();
    REQUIRE(m != nullptr);
    REQUIRE(m->vertices.size() == 4);
    REQUIRE(m->vertices[2] == Vec2{10, 5});
    REQUIRE(m->content == "FIRST\nSECOND");
    REQUIRE((m->leader_flags & LeaderForm::kSpline) != 0);

    h.run({"LEADER", "0,0", "5,5", "", ""});
    REQUIRE(h.out.prompt == "Enter an annotation option [Tolerance/Copy/Block/None/Mtext] <Mtext>: ");
    h.run({"N"});
    REQUIRE(h.last<AddLeaderCommand>()->content.empty());
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("MTEXT: the corner options -- height, justification, line spacing, rotation, width") {
    H h;
    h.run({"MT", "0,0"});
    REQUIRE(h.out.prompt == "Specify opposite corner or [Height/Justify/Line spacing/Rotation/Style/Width/Columns]: ");
    h.run({"H", "5", "J", "MC", "L", ""});
    REQUIRE(h.out.prompt == "Enter line spacing factor or distance <1.0000x>: ");
    h.run({"2x", "R", "30", "40,-20"});
    REQUIRE(h.out.prompt == "Enter text: ");
    REQUIRE(h.proc.wants_free_text());
    h.run({"TWO WORDS"});
    const auto* m = h.last<AddMTextCommand>();
    REQUIRE(m != nullptr);
    REQUIRE(m->content == "TWO WORDS");
    REQUIRE(m->block.height == 5.0);
    REQUIRE(m->block.attach == 4);
    REQUIRE(m->block.pos == Vec2{20, -10});
    REQUIRE(m->block.line_spacing == 2.0);
    REQUIRE(m->block.rotation == Approx(to_radians(30.0)));
    REQUIRE(m->block.width == 40.0);
    h.run({"MT", "10,10", "W", "25", "X"});
    m = h.last<AddMTextCommand>();
    REQUIRE(m->block.pos == Vec2{10, 10});
    REQUIRE(m->block.width == 25.0);
    h.run({"MT", "0,0", "J", "QQ"});
    REQUIRE(h.out.lines.back() == "Enter TL, TC, TR, ML, MC, MR, BL, BC or BR.");
    h.proc.cancel();
}

TEST_CASE("TABLE at the command line (-TABLE): columns, data rows, then the point with Width and Height; a title and a header row") {
    H h;
    h.run({"-TABLE"});
    REQUIRE(h.out.lines.back() == "Current table style: Standard  Cell width: 63.5000  Cell height: 1 line(s)");
    REQUIRE(h.out.prompt == "Enter number of columns or [Auto] <5>: ");
    h.run({"3"});
    REQUIRE(h.out.prompt == "Enter number of data rows or [Auto] <1>: ");
    h.run({"4"});
    REQUIRE(h.out.prompt == "Specify insertion point or [Style/Width/Height]: ");
    h.run({"W", "20", "H", "2", "S", "?"});
    REQUIRE(h.out.lines.back() == "Table styles: Standard");
    h.run({"", "10,10"});
    const auto* t = h.last<AddTableCommand>();
    REQUIRE(t != nullptr);
    REQUIRE(t->cols == 3);
    REQUIRE(t->rows == 6); // a title, a header, four data rows
    REQUIRE(t->has_title);
    REQUIRE(t->has_header);
    REQUIRE(t->col_widths == std::vector<double>(3, 20.0));
    REQUIRE(t->row_heights[0] == 18.0);
    REQUIRE(t->pos == Vec2{10, 10});
}

TEST_CASE("SKETCH: strokes kept as lines, a polyline or a spline, points an increment apart") {
    H h;
    h.run({"SKETCH"});
    REQUIRE(h.out.lines.back() == "Type = Line  Increment = 1.0000  Tolerance = 0.5000");
    REQUIRE(h.out.prompt == "Specify sketch or [Type/increment/toLerance]: ");
    REQUIRE(h.proc.wants_freehand());
    h.proc.submit_freehand({{0, 0}, {0.4, 0}, {1.0, 0}, {1.5, 0}, {2.2, 0}, {2.5, 0}});
    std::size_t lines = 0;
    for (const Command& c : h.cmds) {
        lines += std::holds_alternative<AddLineCommand>(c) ? 1 : 0;
    }
    REQUIRE(lines == 3); // 0 -> 1 -> 2.2 -> 2.5
    h.run({"T", "P"});
    h.proc.submit_freehand({{0, 5}, {3, 5}, {3, 8}});
    REQUIRE(h.last<AddPolylineCommand>()->points.size() == 3);
    h.run({"T", "S", "I", "0.5"});
    h.proc.submit_freehand({{0, 9}, {1, 10}, {2, 9}, {3, 10}});
    REQUIRE(h.last<AddSplineCommand>()->control_points.size() == 4);
    h.run({""});
    REQUIRE(h.out.lines.back() == "3 sketch strokes recorded.");
    REQUIRE_FALSE(h.proc.has_active_command());
    h.run({"SKETCH", "T", "L", "I", "1", ""}); // the defaults again
}

namespace {
struct CameraView : musacad::command::ViewControl {
    Vec2 center{0, 0};
    double scale = 1.0;
    void zoom_extents() override {
        center = {50, 50};
        scale = 4.0;
    }
    void zoom_scale(double f) override { scale *= f; }
    bool current_view(Vec2& c, double& s) override {
        c = center;
        s = scale;
        return true;
    }
    void set_view(Vec2 c, double s) override {
        center = c;
        scale = s;
    }
    bool viewport_size(int& w, int& h) const override {
        w = 800;
        h = 600;
        return true;
    }
};
} // namespace

TEST_CASE("ZOOM Window, Center, Previous and Scale; -PAN by a displacement") {
    CameraView view;
    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[&cmds](Command c) { cmds.push_back(std::move(c)); }, &view, out};
    proc.submit_line("Z");
    REQUIRE(out.prompt == "Specify corner of window, enter a scale factor (nX or nXP), or "
                          "[All/Center/Dynamic/Extents/Previous/Scale/Window/Object] <real time>: ");
    proc.submit_line("0,0"); // a corner at once: a window
    proc.submit_line("100,50");
    REQUIRE(view.center == Vec2{50, 25});
    REQUIRE(view.scale == Approx(8.0)); // 800 / 100, 600 / 50 = 12: the smaller
    proc.submit_line("Z");
    proc.submit_line("C");
    proc.submit_line("10,10");
    REQUIRE(out.prompt == "Enter magnification or height <75.0000>: ");
    proc.submit_line("300");
    REQUIRE(view.center == Vec2{10, 10});
    REQUIRE(view.scale == Approx(2.0));
    proc.submit_line("Z");
    proc.submit_line("2X");
    REQUIRE(view.scale == Approx(4.0));
    proc.submit_line("Z");
    proc.submit_line("P");
    REQUIRE(view.scale == Approx(2.0)); // back one
    proc.submit_line("Z");
    proc.submit_line("P");
    REQUIRE(view.center == Vec2{50, 25});
    proc.set_limits({0, 0}, {400, 300}, false);
    proc.submit_line("Z");
    proc.submit_line("S");
    proc.submit_line("1"); // the limits fill the view
    REQUIRE(view.center == Vec2{200, 150});
    REQUIRE(view.scale == Approx(2.0));
    proc.submit_line("-PAN");
    REQUIRE(out.prompt == "Specify base point or displacement: ");
    proc.submit_line("0,0");
    proc.submit_line("10,5");
    REQUIRE(view.center == Vec2{190, 145});
    REQUIRE_FALSE(proc.has_active_command());
    proc.submit_line("PAN");
    proc.submit_line("");
    REQUIRE_FALSE(proc.has_active_command());
}

TEST_CASE("BREAKLINE: a polyline past both points with a zigzag at the break") {
    H h;
    h.run({"BREAKLINE"});
    REQUIRE(h.out.lines.back() == "Block= BRKLINE.DWG, Size= 1.0000, Extension= 1.2500");
    REQUIRE(h.out.prompt == "Specify first point for breakline or [Block/Size/Extension]: ");
    h.run({"S", "2", "0,0", "10,0"});
    REQUIRE(h.out.prompt == "Specify location for break symbol <Midpoint>: ");
    h.run({""});
    const auto* p = h.last<AddPolylineCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->points.size() == 6);
    REQUIRE(p->points[0].x == Approx(-1.25));
    REQUIRE(p->points[1].x == Approx(4.0));
    REQUIRE(p->points[2].y == Approx(1.5));
    REQUIRE(p->points[3].y == Approx(-1.5));
    REQUIRE(p->points[4].x == Approx(6.0));
    REQUIRE(p->points[5].x == Approx(11.25));
    h.run({"BREAKLINE", "S", "1", ""});
    h.proc.cancel();
}

TEST_CASE("HATCH: [Select objects/Undo/seTtings], the pattern asked and remembered; -HATCH Properties") {
    H h;
    h.run({"H"});
    REQUIRE(h.out.prompt == "Pick internal point or [Select objects/Undo/seTtings]: ");
    h.run({"T"});
    REQUIRE(h.out.prompt.rfind("Enter a pattern name or [?/Solid/User defined/Gradient] <", 0) == 0);
    h.run({"?"});
    REQUIRE(h.out.lines.back().rfind("Patterns: SOLID", 0) == 0);
    h.run({"ANSI37"});
    REQUIRE(h.out.prompt == "Specify a scale for the pattern <1.0000>: ");
    h.run({"2", "45"});
    REQUIRE(h.out.prompt == "Pick internal point or [Select objects/Undo/seTtings]: ");
    h.run({"5,5"});
    const auto* p = h.last<HatchPickPointCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->pattern_name == "ANSI37");
    REQUIRE(p->pattern_scale == 2.0);
    REQUIRE(p->pattern_angle == Approx(to_radians(45.0)));
    h.run({"U"});
    REQUIRE(std::holds_alternative<UndoLastGroupCommand>(h.cmds.back()));
    h.run({""});
    REQUIRE_FALSE(h.proc.has_active_command());
    h.run({"H", "1,1", ""}); // remembered
    REQUIRE(h.last<HatchPickPointCommand>()->pattern_name == "ANSI37");
    h.run({"-HATCH"});
    REQUIRE(h.out.prompt.rfind("Specify internal point or [Properties/Select objects/draW boundary/", 0) == 0);
    h.run({"P", "S", "2,2", ""});
    REQUIRE(h.last<HatchPickPointCommand>()->pattern_name == "SOLID");
    h.run({"H", "T", "ANSI31", "1", "0", ""}); // back to the defaults
}

TEST_CASE("Hot grips: STRETCH, then Enter or Space through MOVE, ROTATE, SCALE and MIRROR") {
    using P = GripDragCommand::Phase;
    H h;
    h.proc.set_selection_count(1);
    const EntityHandle line{0, 1, EntityKind::Line};
    h.proc.start_grip_mode(line, 1, {10, 0});
    REQUIRE(h.out.lines.back() == "** STRETCH **");
    REQUIRE(h.out.prompt == "Specify stretch point or [Base point/Copy/Undo/eXit]: ");
    REQUIRE(h.proc.preview().grip_hot);
    h.run({"20,5"});
    REQUIRE(h.last<GripDragCommand>()->phase == P::Commit);
    REQUIRE(h.last<GripDragCommand>()->pos == Vec2{20, 5});
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.start_grip_mode(line, 1, {10, 0});
    h.run({""});
    REQUIRE(h.last<GripDragCommand>()->phase == P::Cancel); // STRETCH's drag ends
    REQUIRE(h.out.lines.back() == "** MOVE **");
    REQUIRE(h.proc.preview().kind == musacad::command::PreviewKind::Move);
    h.run({"C", "15,0", "20,0"});
    REQUIRE(h.last<CopySelectionCommand>()->delta == Vec2{10, 0});
    REQUIRE(h.proc.has_active_command()); // Copy goes on
    h.run({"X"});
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.start_grip_mode(line, 1, {10, 0});
    h.run({"", ""});
    REQUIRE(h.out.lines.back() == "** ROTATE **");
    h.run({"90"});
    REQUIRE(h.last<RotateSelectionCommand>()->angle == Approx(kHalfPi));
    REQUIRE(h.last<RotateSelectionCommand>()->base == Vec2{10, 0});

    h.proc.start_grip_mode(line, 1, {10, 0});
    h.run({"", "", "", "B"});
    REQUIRE(h.out.prompt == "Specify base point: ");
    h.run({"0,0", "2"});
    REQUIRE(h.last<ScaleSelectionCommand>()->factor == 2.0);
    REQUIRE(h.last<ScaleSelectionCommand>()->base == Vec2{0, 0});

    h.proc.start_grip_mode(line, 1, {10, 0});
    h.run({"", "", "", ""});
    REQUIRE(h.out.lines.back() == "** MIRROR **");
    h.run({"10,10"});
    REQUIRE(h.last<MirrorSelectionCommand>()->b == Vec2{10, 10});
    REQUIRE(h.last<MirrorSelectionCommand>()->erase_source);

    h.proc.start_grip_mode(line, 1, {10, 0});
    h.run({"", "", "", "", ""}); // round to STRETCH: the drag begins again
    REQUIRE(h.last<GripDragCommand>()->phase == P::Begin);
    h.proc.cancel();
    REQUIRE(h.last<GripDragCommand>()->phase == P::Cancel);
}

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Modify and draw tools (#45, #55): REVERSE, COPYTOLAYER, LAYMCH, CHPROP, OVERKILL, BLEND,
// MREDO, TRACE, SOLID, BOUNDARY, CENTERMARK and CENTERLINE.

#include <chrono>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad::core;
using musacad::command::CommandOutput;
using musacad::command::CommandProcessor;
using Catch::Approx;

namespace {
template <class Pred>
bool wait_until(GeometryEngine& e, Pred pred) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        e.consume_snapshot();
        if (pred(e.snapshot())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

io::Document dump(GeometryEngine& engine, const char* name) {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
    // Wait for THIS save: the status alone can be a stale "Saved" from the dump before,
    // when the command in between (an undo, say) reported nothing.
    engine.consume_snapshot();
    const std::uint64_t before = engine.snapshot().document_version;
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [before](const auto& s) { return s.document_version > before; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    std::filesystem::remove(p);
    return doc;
}

bool says(GeometryEngine& e, const std::string& what) {
    return wait_until(e, [&](const auto& s) { return s.status == what; });
}

void select_all(GeometryEngine& e, std::size_t n) {
    e.submit(SelectAllCommand{});
    REQUIRE(wait_until(e, [n](const auto& s) { return s.selection.size() == n; }));
}

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
};
} // namespace

TEST_CASE("REVERSE turns lines, polylines (bulges and widths too) and splines round") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    AddPolylineCommand pl;
    pl.points = {{0, 0}, {10, 0}, {10, 10}};
    pl.bulges = {0.5, 0.0, 0.0};
    pl.widths = {1, 2, 3, 4, 0, 0};
    pl.group = 2;
    engine.submit(pl);
    engine.submit(AddSplineCommand{{{0, 0}, {1, 1}, {2, 0}, {3, 1}}, 3, 3});
    engine.submit(AddCircleCommand{{50, 50}, 5.0, 4}); // not reversible: left as it is
    select_all(engine, 4);
    engine.submit(ReverseSelectionCommand{5});
    REQUIRE(says(engine, "3 objects reversed."));
    const io::Document doc = dump(engine, "musacad_tools_reverse.musa");
    REQUIRE(doc.lines[0].a == Vec2{10, 0});
    REQUIRE(doc.polylines[0].points == std::vector<Vec2>{{10, 10}, {10, 0}, {0, 0}});
    REQUIRE(doc.polylines[0].bulges[1] == Approx(-0.5));
    REQUIRE(doc.polylines[0].bulges[0] == Approx(0.0));
    REQUIRE(doc.polylines[0].widths == std::vector<double>{4, 3, 2, 1, 0, 0});
    REQUIRE(doc.splines[0].control_points == std::vector<Vec2>{{3, 1}, {2, 0}, {1, 1}, {0, 0}});
    REQUIRE(doc.circles.size() == 1);
    engine.stop();
}

TEST_CASE("COPYTOLAYER copies to a layer named (made if need be) or picked; LAYMCH moves to one") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    select_all(engine, 1);
    engine.submit(CopyToLayerCommand{"Walls", std::nullopt, 0.0, {0, 5}, 2});
    REQUIRE(says(engine, "1 object copied to layer \"Walls\" (a new layer)."));
    io::Document doc = dump(engine, "musacad_tools_copytolayer.musa");
    REQUIRE(doc.layers.size() == 2);
    REQUIRE(doc.layers[1].name == "Walls");
    REQUIRE(doc.lines.size() == 2);
    REQUIRE(doc.lines[0].props.layer == 0);
    REQUIRE(doc.lines[1].props.layer == 1);
    REQUIRE(doc.lines[1].a == Vec2{0, 5});

    // LAYMCH: the original line onto the layer of the copy.
    engine.submit(SelectWindowCommand{{-1, -1}, {11, 1}, false, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(LayerToolCommand{LayerToolCommand::Op::Match, {5, 5}, 1.0, 3});
    REQUIRE(says(engine, "1 object changed to layer \"Walls\"."));
    doc = dump(engine, "musacad_tools_laymch.musa");
    REQUIRE(doc.lines[0].props.layer == 1);
    REQUIRE(doc.lines[1].props.layer == 1);
    engine.stop();
}

TEST_CASE("CHPROP changes colour, layer, linetype, linetype scale and lineweight in one step") {
    GeometryEngine engine;
    engine.start();
    Layer red;
    red.name = "Red";
    engine.submit(AddLayerCommand{red});
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    select_all(engine, 1);
    ChangePropsCommand c;
    c.color = std::make_pair(false, Rgb{255, 0, 0});
    c.layer = "red";
    c.linetype = std::make_pair(false, Linetype::Dashed);
    c.celtscale = 2.0;
    c.lineweight = std::make_pair(false, std::uint8_t{50});
    c.group = 2;
    engine.submit(c);
    REQUIRE(says(engine, "Properties changed on 1 object."));
    io::Document doc = dump(engine, "musacad_tools_chprop.musa");
    const EntityProps p = doc.lines[0].props;
    REQUIRE(p.layer == 1);
    REQUIRE_FALSE(p.color_by_layer());
    REQUIRE(p.color == Rgb{255, 0, 0});
    REQUIRE(p.linetype == Linetype::Dashed);
    REQUIRE(p.lineweight == 50);
    REQUIRE(doc.lines[0].celtscale == Approx(2.0));
    engine.submit(UndoLastGroupCommand{}); // one step
    doc = dump(engine, "musacad_tools_chprop_undo.musa");
    REQUIRE(doc.lines[0].props.layer == 0);
    REQUIRE(doc.lines[0].celtscale == Approx(1.0));

    select_all(engine, 1);
    ChangePropsCommand bad;
    bad.layer = "nope";
    engine.submit(bad);
    REQUIRE(says(engine, "Layer \"nope\" not found."));
    engine.stop();
}

TEST_CASE("OVERKILL deletes duplicates and combines overlapping collinear lines") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{10, 0}, {0, 0}, 2});  // the same line, the other way
    engine.submit(AddLineCommand{{5, 0}, {20, 0}, 3});  // overlaps it
    engine.submit(AddLineCommand{{20, 0}, {30, 0}, 4}); // meets it end to end
    engine.submit(AddLineCommand{{0, 5}, {10, 5}, 5});  // apart
    engine.submit(AddCircleCommand{{50, 50}, 5.0, 6});
    engine.submit(AddCircleCommand{{50, 50}, 5.0, 7});
    select_all(engine, 7);
    engine.submit(OverkillCommand{1e-6, true, false, 8});
    REQUIRE(says(engine, "2 duplicates deleted, 2 overlapping objects combined."));
    io::Document doc = dump(engine, "musacad_tools_overkill.musa");
    REQUIRE(doc.circles.size() == 1);
    REQUIRE(doc.lines.size() == 3);
    bool spans = false;
    for (const io::DocLine& l : doc.lines) {
        spans = spans || (std::min(l.a.x, l.b.x) == Approx(0.0) && std::max(l.a.x, l.b.x) == Approx(20.0) && l.a.y == 0.0);
    }
    REQUIRE(spans);

    select_all(engine, 4);
    engine.submit(OverkillCommand{1e-6, true, true, 9}); // end to end as well
    REQUIRE(says(engine, "0 duplicates deleted, 2 overlapping objects combined."));
    doc = dump(engine, "musacad_tools_overkill_e2e.musa");
    REQUIRE(doc.lines.size() == 2);
    engine.submit(UndoLastGroupCommand{});
    engine.submit(UndoLastGroupCommand{});
    doc = dump(engine, "musacad_tools_overkill_undo.musa");
    REQUIRE(doc.lines.size() == 5);
    REQUIRE(doc.circles.size() == 2);
    engine.stop();
}

TEST_CASE("BLEND joins two ends with a spline leaving each along its object") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{20, 5}, {30, 5}, 2});
    engine.submit(AddArcCommand{{0, 40}, 10.0, 0.0, kPi / 2.0, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(BlendCommand{{9, 0}, {21, 5}, 1.0, false, 4});
    REQUIRE(says(engine, "Blend created."));
    engine.submit(BlendCommand{{9, 0}, {21, 5}, 1.0, true, 5});
    // From the arc's end at the top, which runs off to the left.
    engine.submit(BlendCommand{{0, 50}, {1, 0}, 1.0, false, 6});
    io::Document doc = dump(engine, "musacad_tools_blend.musa");
    REQUIRE(doc.splines.size() == 3);
    const double k = std::hypot(10.0, 5.0) / 3.0;
    REQUIRE(doc.splines[0].degree == 3);
    REQUIRE(doc.splines[0].control_points.size() == 4);
    REQUIRE(doc.splines[0].control_points[0] == Vec2{10, 0});
    REQUIRE(doc.splines[0].control_points[1].x == Approx(10.0 + k));
    REQUIRE(doc.splines[0].control_points[1].y == Approx(0.0).margin(1e-12));
    REQUIRE(doc.splines[0].control_points[2].x == Approx(20.0 - k));
    REQUIRE(doc.splines[0].control_points[3] == Vec2{20, 5});
    REQUIRE(doc.splines[1].degree == 5);
    REQUIRE(doc.splines[1].control_points.size() == 6);
    const std::vector<Vec2>& a = doc.splines[2].control_points;
    REQUIRE(a[0].x == Approx(0.0).margin(1e-9));
    REQUIRE(a[0].y == Approx(50.0));
    REQUIRE(a[1].x < -1.0); // off to the left, along the arc
    REQUIRE(a[1].y == Approx(50.0));
    REQUIRE(a[3] == Vec2{0, 0});
    engine.submit(BlendCommand{{100, 100}, {9, 0}, 1.0, false, 7});
    REQUIRE(says(engine, "Select lines, arcs, open polylines, splines or elliptical arcs."));
    engine.stop();
}

TEST_CASE("BOUNDARY, CENTERLINE and CENTERMARK; MREDO redoes several steps") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    engine.submit(AddLineCommand{{100, 0}, {100, 100}, 1});
    engine.submit(AddLineCommand{{100, 100}, {0, 100}, 1});
    engine.submit(AddLineCommand{{0, 100}, {0, 0}, 1});
    engine.submit(AddCircleCommand{{50, 50}, 10.0, 1});
    engine.submit(BoundaryCommand{{20, 20}, true, 2});
    REQUIRE(says(engine, "BOUNDARY created 2 polylines.")); // the square and the circle in it
    engine.submit(AddLineCommand{{200, 0}, {300, 0}, 3});
    engine.submit(AddLineCommand{{300, 0}, {300, 100}, 3});
    engine.submit(AddLineCommand{{300, 100}, {200, 100}, 3});
    engine.submit(AddLineCommand{{200, 100}, {200, 0}, 3});
    engine.submit(AddCircleCommand{{250, 50}, 10.0, 3});
    engine.submit(BoundaryCommand{{220, 20}, false, 3}); // islands off: the square alone
    REQUIRE(says(engine, "BOUNDARY created 1 polyline."));
    engine.submit(BoundaryCommand{{500, 500}, true, 4});
    REQUIRE(says(engine, "No closed boundary found round that point."));
    io::Document doc = dump(engine, "musacad_tools_boundary.musa");
    REQUIRE(doc.polylines.size() == 3);
    for (const io::DocPolyline& p : doc.polylines) {
        REQUIRE(p.closed);
    }

    // The centre line of two lines drawn opposite ways, 3.5 past each end.
    engine.submit(AddLineCommand{{0, 200}, {100, 200}, 5});
    engine.submit(AddLineCommand{{100, 210}, {0, 210}, 6});
    engine.submit(CenterlineCommand{{50, 200}, {50, 210}, 1.0, 3.5, 7});
    REQUIRE(says(engine, "Center line created."));
    engine.submit(CenterlineCommand{{50, 200}, {60, 50}, 1.0, 3.5, 8}); // a circle: no
    REQUIRE(says(engine, "Select two lines."));
    engine.submit(AddCircleCommand{{400, 400}, 20.0, 10});
    engine.submit(AddCenterMarkCommand{{420, 400}, 1.0, 2.0, true, 11, true});
    REQUIRE(says(engine, "Center mark and center lines added."));
    doc = dump(engine, "musacad_tools_center.musa");
    const io::DocLine& cl = doc.lines[10]; // after the two squares and the two lines
    REQUIRE(cl.a.x == Approx(-3.5));
    REQUIRE(cl.a.y == Approx(205.0));
    REQUIRE(cl.b.x == Approx(103.5));
    REQUIRE_FALSE(cl.props.linetype_by_layer());
    REQUIRE(cl.props.linetype == Linetype::Center);
    REQUIRE(doc.lines.size() == 17);
    REQUIRE(doc.lines[11].props.linetype_by_layer()); // the cross
    REQUIRE(doc.lines[13].props.linetype == Linetype::Center); // a centre line
    REQUIRE_FALSE(doc.lines[13].props.linetype_by_layer());

    // The mark, the circle and the centre line undone; two redone, then the rest.
    engine.submit(UndoLastGroupCommand{});
    engine.submit(UndoLastGroupCommand{});
    engine.submit(UndoLastGroupCommand{});
    engine.submit(RedoLastGroupCommand{2});
    doc = dump(engine, "musacad_tools_mredo.musa");
    REQUIRE(doc.lines.size() == 11);
    REQUIRE(doc.circles.size() == 3);
    engine.submit(RedoLastGroupCommand{0xFFFFFFFFu});
    doc = dump(engine, "musacad_tools_mredo_all.musa");
    REQUIRE(doc.lines.size() == 17);
    engine.stop();
}

TEST_CASE("The modify and draw tools' prompts") {
    H h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("REVERSE");
    REQUIRE(h.last<ReverseSelectionCommand>() != nullptr);
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.submit_line("COPYTOLAYER");
    REQUIRE(h.out.prompt == "Select object on destination layer or [Name] <Name>: ");
    h.proc.submit_line("");
    REQUIRE(h.out.prompt == "Enter layer name: ");
    h.proc.submit_line("Walls");
    REQUIRE(h.out.prompt == "Specify base point or [Displacement/eXit] <eXit>: ");
    h.proc.submit_line("1,1");
    h.proc.submit_line("6,6");
    const auto* ctl = h.last<CopyToLayerCommand>();
    REQUIRE(ctl != nullptr);
    REQUIRE(ctl->name == "Walls");
    REQUIRE(ctl->offset == Vec2{5, 5});

    h.proc.submit_line("LAYMCH");
    REQUIRE(h.out.prompt == "Select object on destination layer or [Name]: ");
    h.proc.submit_line("N");
    h.proc.submit_line("B");
    REQUIRE(h.last<ChangePropsCommand>()->layer == "B");

    h.proc.submit_line("CHPROP");
    REQUIRE(h.out.prompt == "Enter property to change [Color/LAyer/LType/ltScale/LWeight]: ");
    h.proc.submit_line("C");
    REQUIRE(h.out.prompt == "Enter new color [Truecolor] <BYLAYER>: ");
    h.proc.submit_line("red");
    h.proc.submit_line("LW");
    h.proc.submit_line("0.5");
    h.proc.submit_line("LT");
    h.proc.submit_line("dotted");
    REQUIRE(h.out.lines.back() == "Linetype dotted is not loaded: Continuous, Dashed, Center or Hidden.");
    h.proc.submit_line("hidden");
    h.proc.submit_line("");
    const auto* cp = h.last<ChangePropsCommand>();
    REQUIRE(cp->color.has_value());
    REQUIRE_FALSE(cp->color->first);
    REQUIRE(cp->color->second == Rgb{255, 0, 0});
    REQUIRE(cp->lineweight->second == 50);
    REQUIRE(cp->linetype->second == Linetype::Hidden);
    REQUIRE(cp->layer.empty());

    h.proc.set_selection_count(0);
    h.proc.submit_line("OVERKILL");
    REQUIRE(h.out.prompt == "Select objects: ");
    h.proc.set_selection_count(3);
    h.proc.submit_line("");
    REQUIRE(h.out.prompt == "Enter an option to change [Done/tOlerance/combine parTial overlap/combine Endtoend] <done>: ");
    h.proc.submit_line("O");
    h.proc.submit_line("0.01");
    h.proc.submit_line("E");
    REQUIRE(h.out.lines.back() == "Current settings: Tolerance=0.01, Combine partial overlap=Yes, Combine end to end=Yes");
    h.proc.submit_line("");
    REQUIRE(h.last<OverkillCommand>()->tolerance == Approx(0.01));
    REQUIRE(h.last<OverkillCommand>()->end_to_end);

    h.proc.submit_line("BLEND");
    REQUIRE(h.out.lines.back() == "Current setting: Continuity = Tangent");
    h.proc.submit_line("CON");
    h.proc.submit_line("S");
    REQUIRE(h.out.prompt == "Select first object or [CONtinuity]: ");
    h.proc.submit_line("10,0");
    REQUIRE(h.out.prompt == "Select second object: ");
    h.proc.submit_line("20,5");
    REQUIRE(h.last<BlendCommand>()->smooth);
    REQUIRE(h.last<BlendCommand>()->pick2 == Vec2{20, 5});

    h.proc.submit_line("MREDO");
    REQUIRE(h.out.prompt == "Enter number of actions or [All/Last]: ");
    h.proc.submit_line("3");
    REQUIRE(h.last<RedoLastGroupCommand>()->count == 3);
    h.proc.submit_line("REDO");
    REQUIRE(h.last<RedoLastGroupCommand>()->count == 1);

    h.proc.submit_line("TRACE");
    REQUIRE(h.out.prompt == "Specify trace width <1.0000>: ");
    h.proc.submit_line("2");
    REQUIRE(h.out.prompt == "Specify start point: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompt == "Specify next point: ");
    h.proc.submit_line("10,0");
    h.proc.submit_line("10,10");
    h.proc.submit_line("");
    const auto* tr = h.last<AddPolylineCommand>();
    REQUIRE(tr->points.size() == 3);
    REQUIRE(tr->widths == std::vector<double>{2, 2});

    h.proc.submit_line("SOLID");
    REQUIRE(h.out.prompt == "Specify first point: ");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,0");
    h.proc.submit_line("0,10");
    REQUIRE(h.out.prompt == "Specify fourth point or <exit>: ");
    h.proc.submit_line("10,10");
    const auto* so = h.last<AddHatchCommand>();
    REQUIRE(so->pattern_name == "SOLID");
    REQUIRE(so->loops[0] == std::vector<Vec2>{{0, 0}, {10, 0}, {10, 10}, {0, 10}});
    REQUIRE(h.out.prompt == "Specify third point: ");
    h.proc.submit_line("");
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.submit_line("CL");
    REQUIRE(h.out.prompt == "Select first line: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompt == "Select second line: ");
    h.proc.submit_line("0,10");
    REQUIRE(h.last<CenterlineCommand>()->pick2 == Vec2{0, 10});

    h.proc.submit_line("BO");
    REQUIRE(h.out.prompt == "Pick internal point: ");
    h.proc.submit_line("5,5");
    REQUIRE(h.last<BoundaryCommand>()->point == Vec2{5, 5});
    REQUIRE(h.out.prompt == "Pick internal point: ");
    h.proc.submit_line("");
    REQUIRE_FALSE(h.proc.has_active_command());
}

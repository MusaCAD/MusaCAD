// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// The ARRAY family. AutoCAD ships ARRAY/-ARRAY (which ask for the type),
// ARRAYRECT, ARRAYPOLAR and ARRAYPATH, plus ARRAYEDIT/ARRAYCLOSE for ASSOCIATIVE
// arrays. Musa CAD's arrays are non-associative -- the same thing AutoCAD's own -ARRAY
// produces -- so the four creation commands are the whole family here; there is no
// association for ARRAYEDIT to reopen.
//
// Covered below: rectangular (including the axis angle), polar, and path in both of
// AutoCAD's methods (Divide and Measure), aligned and not, on open and closed paths.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad::core;
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

/// How many circle centres the snapshot contains near `p`. Circles are used as the
/// arrayed item because a circle's tessellation has an obvious centroid.
int circles_near(const RenderSnapshot& s, Vec2 p, double tol) {
    // Circle tessellations are contiguous runs; count vertices within tol of p and
    // divide out, which is enough to answer "is there an item here".
    int n = 0;
    for (const Vec2& v : s.line_vertices) {
        if (length(v - p) <= tol) {
            ++n;
        }
    }
    return n;
}

bool has_vertex_near(const RenderSnapshot& s, Vec2 p, double tol) {
    return std::any_of(s.line_vertices.begin(), s.line_vertices.end(),
                       [&](const Vec2& v) { return length(v - p) <= tol; });
}

struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string&) override {}
    void set_prompt(const std::string&) override {}
};
struct ProcHarness {
    std::vector<Command> cmds;
    SilentOutput out;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
};
} // namespace

// ---------------------------------------------------------------------------
// Rectangular
// ---------------------------------------------------------------------------

TEST_CASE("ARRAY: a rectangular array's axis angle rotates the lattice, not the items") {
    // AutoCAD's "Angle of axes": the rows/columns run along a rotated pair of axes
    // while each copy keeps the orientation it was drawn with. A 1x2 array at 90
    // degrees therefore puts the second item straight UP, not to the right.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {1, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(ArrayRectCommand{
        .rows = 1, .cols = 2, .dx = 10.0, .dy = 0.0, .angle = kPi / 2.0, .group = 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    // The copy sits 10 along +Y ...
    REQUIRE(has_vertex_near(s, {0, 10}, 1e-6));
    REQUIRE(has_vertex_near(s, {1, 10}, 1e-6)); // ... and is still horizontal.
    engine.stop();
}

TEST_CASE("ARRAY: a rectangular array reports how many copies it made") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {1, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(ArrayRectCommand{.rows = 2, .cols = 3, .dx = 5, .dy = 5, .group = 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Array created: 5 copies.") != std::string::npos;
    }));
    engine.stop();
}

TEST_CASE("ARRAY: arraying nothing says so instead of silently doing nothing") {
    // Ph10.1: the engine reports what actually happened rather than letting the
    // command guess a success message.
    GeometryEngine engine;
    engine.start();
    engine.submit(ArrayRectCommand{.rows = 2, .cols = 2, .dx = 5, .dy = 5, .group = 1});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Nothing selected to array.") != std::string::npos;
    }));
    engine.stop();
}

// ---------------------------------------------------------------------------
// Path -- Divide
// ---------------------------------------------------------------------------

TEST_CASE("ARRAY: a path array spreads N items over the whole path (Divide)") {
    // A 100-long horizontal line as the path, a small circle as the item. Five items
    // over an OPEN path land at 0, 25, 50, 75, 100 -- endpoints included, which is what
    // AutoCAD's Divide method does.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});   // the path
    engine.submit(AddCircleCommand{{0, 50}, 2.0, 2});     // the item
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 4; }));

    // Select ONLY the circle.
    engine.submit(SelectPickCommand{{0, 52}, 1.0, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    ArrayPathCommand c;
    c.pick = {50.0, 0.0}; // on the line
    c.pick_radius = 1.0;
    c.count = 5;
    c.spacing = 0.0; // Divide
    c.align = false;
    c.group = 4;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Path array created") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    // A circle of radius 2 centred on each station: look for a vertex at centre+radius.
    for (double x : {0.0, 25.0, 50.0, 75.0, 100.0}) {
        REQUIRE(circles_near(s, {x, 0.0}, 2.05) > 0);
    }
    engine.stop();
}

TEST_CASE("ARRAY: a closed path gets no duplicate item at the seam") {
    // On a closed path the start and end are the same point, so N items must divide
    // by N, not N-1 -- otherwise the last item lands on top of the first.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 50.0, 1});  // the closed path
    engine.submit(AddCircleCommand{{0, 200}, 1.0, 2}); // the item
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(SelectPickCommand{{0, 201}, 0.5, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    ArrayPathCommand c;
    c.pick = {50.0, 0.0}; // on the big circle
    c.pick_radius = 1.0;
    c.count = 4;
    c.align = false;
    c.group = 4;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Path array created") != std::string::npos;
    }));
    engine.consume_snapshot();
    // Four items a quarter-turn apart around the circle, none doubled at the seam.
    const RenderSnapshot& s = engine.snapshot();
    for (int i = 0; i < 4; ++i) {
        const double a = kTwoPi * static_cast<double>(i) / 4.0;
        REQUIRE(circles_near(s, {50.0 * std::cos(a), 50.0 * std::sin(a)}, 1.6) > 0);
    }
    engine.stop();
}

// ---------------------------------------------------------------------------
// Path -- Measure
// ---------------------------------------------------------------------------

TEST_CASE("ARRAY: Measure steps by distance and stops at the end of the path") {
    // Spacing 30 on a 100-long path fits stations at 0, 30, 60, 90 -- four items. The
    // fifth would be at 120, past the end, so it is not placed.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    engine.submit(AddCircleCommand{{0, 50}, 1.0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 4; }));
    engine.submit(SelectPickCommand{{0, 51}, 0.5, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    ArrayPathCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.count = 0; // as many as fit
    c.spacing = 30.0;
    c.align = false;
    c.group = 4;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Path array created") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    for (double x : {0.0, 30.0, 60.0, 90.0}) {
        REQUIRE(circles_near(s, {x, 0.0}, 1.05) > 0);
    }
    REQUIRE(circles_near(s, {120.0, 0.0}, 1.05) == 0); // never past the end
    engine.stop();
}

TEST_CASE("ARRAY: Measure honours an item-count cap") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    engine.submit(AddCircleCommand{{0, 50}, 1.0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 4; }));
    engine.submit(SelectPickCommand{{0, 51}, 0.5, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    ArrayPathCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.count = 2; // cap: only 0 and 30, even though 60 and 90 would fit
    c.spacing = 30.0;
    c.align = false;
    c.group = 4;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Path array created") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(circles_near(s, {30.0, 0.0}, 1.05) > 0);
    REQUIRE(circles_near(s, {60.0, 0.0}, 1.05) == 0);
    engine.stop();
}

// ---------------------------------------------------------------------------
// Path -- alignment and guards
// ---------------------------------------------------------------------------

TEST_CASE("ARRAY: aligned items turn with the path; unaligned ones do not") {
    // The path turns a right angle at (100,0). An item on the vertical leg must be
    // rotated 90 degrees when aligned, and untouched when not.
    const auto run = [](bool align) {
        GeometryEngine engine;
        engine.start();
        engine.submit(AddPolylineCommand{{{0, 0}, {100, 0}, {100, 100}}, false, 1});
        // A horizontal 10-long line as the item, centred at (0,-50).
        engine.submit(AddLineCommand{{-5, -50}, {5, -50}, 2});
        REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() >= 6; }));
        engine.submit(SelectPickCommand{{0, -50}, 1.0, false});
        REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

        ArrayPathCommand c;
        c.pick = {50.0, 0.0};
        c.pick_radius = 1.0;
        c.count = 3; // stations at 0, 100 (the corner) and 200 (the far end)
        c.align = align;
        c.group = 4;
        engine.submit(c);
        REQUIRE(wait_until(engine, [](const auto& s) {
            return s.status.find("Path array created") != std::string::npos;
        }));
        engine.consume_snapshot();
        // The item at the LAST station, (100,100), on the vertical leg.
        const RenderSnapshot& s = engine.snapshot();
        const bool vertical = has_vertex_near(s, {100, 95}, 1e-6) &&
                              has_vertex_near(s, {100, 105}, 1e-6);
        const bool horizontal = has_vertex_near(s, {95, 100}, 1e-6) &&
                                has_vertex_near(s, {105, 100}, 1e-6);
        engine.stop();
        return std::pair{vertical, horizontal};
    };

    const auto [av, ah] = run(true);
    REQUIRE(av);  // aligned: turned to follow the vertical leg
    REQUIRE(!ah);

    const auto [uv, uh] = run(false);
    REQUIRE(uh); // unaligned: still horizontal
    REQUIRE(!uv);
}

TEST_CASE("ARRAY: a path array refuses to array the path along itself") {
    // If the path is also in the selection every copy drags a copy of the path with
    // it, which is never what anyone means.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    ArrayPathCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.count = 4;
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("the path curve is part of the selection") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().line_vertices.size() == 2); // nothing was created
    engine.stop();
}

TEST_CASE("ARRAY: a path array with no curve under the pick says so") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 50}, 1.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    ArrayPathCommand c;
    c.pick = {900.0, 900.0}; // empty space
    c.pick_radius = 1.0;
    c.count = 4;
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("no curve under the pick") != std::string::npos;
    }));
    engine.stop();
}

TEST_CASE("ARRAY: a path array is one undo group") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    engine.submit(AddCircleCommand{{0, 50}, 1.0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 4; }));
    engine.submit(SelectPickCommand{{0, 51}, 0.5, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    const std::size_t before = engine.snapshot().line_vertices.size();

    ArrayPathCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.count = 5;
    c.group = 4;
    engine.submit(c);
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.line_vertices.size() > before; }));

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine,
                       [&](const auto& s) { return s.line_vertices.size() == before; }));
    engine.stop();
}

// ---------------------------------------------------------------------------
// The command flows
// ---------------------------------------------------------------------------

TEST_CASE("ARRAY: ARRAY still asks the classic four rectangular prompts") {
    // The legacy -ARRAY flow is unchanged: rows, columns, row spacing, column spacing,
    // and then it fires. No axis-angle prompt was inserted into it.
    ProcHarness h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("AR");
    h.proc.submit_line("R");
    h.proc.submit_line("2");
    h.proc.submit_line("3");
    h.proc.submit_line("10");
    h.proc.submit_line("15");
    REQUIRE(h.cmds.size() == 1);
    const auto* ar = std::get_if<ArrayRectCommand>(&h.cmds[0]);
    REQUIRE(ar != nullptr);
    REQUIRE(ar->rows == 2);
    REQUIRE(ar->cols == 3);
    REQUIRE(ar->dx == Approx(15.0));
    REQUIRE(ar->dy == Approx(10.0));
    REQUIRE(ar->angle == Approx(0.0));
}

TEST_CASE("ARRAY: ARRAYRECT adds the axis-angle prompt") {
    ProcHarness h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("ARRAYRECT");
    h.proc.submit_line("2");
    h.proc.submit_line("3");
    h.proc.submit_line("10");
    h.proc.submit_line("15");
    REQUIRE(h.cmds.empty()); // not finished: the angle is still to come
    h.proc.submit_line("30");
    REQUIRE(h.cmds.size() == 1);
    const auto* ar = std::get_if<ArrayRectCommand>(&h.cmds[0]);
    REQUIRE(ar != nullptr);
    REQUIRE(ar->angle == Approx(to_radians(30.0)));
}

TEST_CASE("ARRAY: ARRAY's PA and PO keywords are told apart, and a bare P is refused") {
    // AutoCAD distinguishes PAth from POlar by two letters; guessing at a bare "P"
    // would silently build the wrong kind of array.
    {
        ProcHarness h;
        h.proc.set_selection_count(1);
        h.proc.submit_line("AR");
        h.proc.submit_line("PO");
        h.proc.submit_line("0,0"); // centre
        h.proc.submit_line("6");   // count
        h.proc.submit_line("360"); // fill
        h.proc.submit_line("Y");
        REQUIRE(h.cmds.size() == 1);
        REQUIRE(std::get_if<ArrayPolarCommand>(&h.cmds[0]) != nullptr);
    }
    {
        ProcHarness h;
        h.proc.set_selection_count(1);
        h.proc.submit_line("AR");
        h.proc.submit_line("PA");
        h.proc.submit_line("50,0"); // path pick
        h.proc.submit_line("D");    // Divide
        h.proc.submit_line("5");    // count
        h.proc.submit_line("Y");    // align
        REQUIRE(h.cmds.size() == 1);
        const auto* ap = std::get_if<ArrayPathCommand>(&h.cmds[0]);
        REQUIRE(ap != nullptr);
        REQUIRE(ap->count == 5);
        REQUIRE(ap->spacing == Approx(0.0));
        REQUIRE(ap->align);
    }
    {
        ProcHarness h;
        h.proc.set_selection_count(1);
        h.proc.submit_line("AR");
        h.proc.submit_line("P"); // ambiguous -> refused, still waiting on the type
        h.proc.submit_line("R"); // now a rectangular array
        h.proc.submit_line("1");
        h.proc.submit_line("2");
        h.proc.submit_line("5");
        h.proc.submit_line("5");
        REQUIRE(h.cmds.size() == 1);
        REQUIRE(std::get_if<ArrayRectCommand>(&h.cmds[0]) != nullptr);
    }
}

TEST_CASE("ARRAY: ARRAYPATH's Measure branch carries the spacing through") {
    ProcHarness h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("ARRAYPATH");
    h.proc.submit_line("50,0");
    h.proc.submit_line("M");  // Measure
    h.proc.submit_line("25"); // spacing
    h.proc.submit_line("N");  // do not align
    REQUIRE(h.cmds.size() == 1);
    const auto* ap = std::get_if<ArrayPathCommand>(&h.cmds[0]);
    REQUIRE(ap != nullptr);
    REQUIRE(ap->spacing == Approx(25.0));
    REQUIRE(ap->count == 0); // as many as fit
    REQUIRE(!ap->align);
}

TEST_CASE("ARRAY: every array command asks Select objects with an empty selection, and ends when nothing is chosen") {
    for (const char* alias : {"AR", "ARRAY", "-ARRAY", "ARRAYRECT", "ARRAYPOLAR", "ARRAYPATH"}) {
        ProcHarness h;
        h.proc.set_selection_count(0);
        h.proc.submit_line(alias);
        INFO(alias);
        REQUIRE(h.proc.in_selection_phase()); // verb-noun (#46)
        h.proc.submit_line("");               // Enter with nothing chosen
        REQUIRE(h.cmds.empty());
        REQUIRE(!h.proc.has_active_command()); // it ended rather than prompting for the array
    }
}

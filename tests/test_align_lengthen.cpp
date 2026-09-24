// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// ALIGN and LENGTHEN (issue #27).
//
// ALIGN is move + rotate (+ optional uniform scale) expressed as "put these two points
// on those two points", which is how a detail gets fitted between two known features in
// one step. It is composed from the existing per-kind transforms rather than a new
// matrix path, so every entity kind is aligned by code that already knows how to rotate
// and scale it.
//
// LENGTHEN's one subtlety is that the END NEARER THE PICK moves and the other stays --
// that is what makes it different from just editing a coordinate.

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

TEST_CASE("#27: ALIGN moves and rotates the selection onto the destination points") {
    // A horizontal 10-long line from (0,0). Align (0,0)->(5,5) and (10,0)->(5,15):
    // it must end up vertical, running from (5,5) up to (5,15).
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    AlignSelectionCommand c;
    c.src1 = {0, 0};
    c.dst1 = {5, 5};
    c.src2 = {10, 0};
    c.dst2 = {5, 15};
    c.scale = false;
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Aligned 1 object.") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(has_vertex_near(s, {5, 5}, 1e-9));
    REQUIRE(has_vertex_near(s, {5, 15}, 1e-9)); // length preserved: still 10 long
    engine.stop();
}

TEST_CASE("#27: ALIGN without scaling keeps the size even when the targets are further apart") {
    // Destination points 20 apart, source points 10 apart, scale OFF: the line keeps
    // its own length of 10 and only its start is pinned.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(AlignSelectionCommand{{0, 0}, {0, 0}, {10, 0}, {20, 0}, false, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Aligned") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(has_vertex_near(engine.snapshot(), {10, 0}, 1e-9));  // unchanged length
    REQUIRE(!has_vertex_near(engine.snapshot(), {20, 0}, 1e-9));
    engine.stop();
}

TEST_CASE("#27: ALIGN with scaling stretches the selection onto both points") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(AlignSelectionCommand{{0, 0}, {0, 0}, {10, 0}, {20, 0}, true, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Aligned") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(has_vertex_near(engine.snapshot(), {0, 0}, 1e-9));
    REQUIRE(has_vertex_near(engine.snapshot(), {20, 0}, 1e-9)); // scaled x2 onto the target
    engine.stop();
}

TEST_CASE("#27: ALIGN scales a circle's radius too, not just its position") {
    // Composition through the existing transforms means a circle is scaled by the code
    // that already knows a circle's radius has to move with it.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 5.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(AlignSelectionCommand{{0, 0}, {0, 0}, {10, 0}, {20, 0}, true, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Aligned") != std::string::npos;
    }));
    engine.consume_snapshot();
    for (const Vec2& v : engine.snapshot().line_vertices) {
        REQUIRE(length(v) == Approx(10.0).margin(0.2)); // radius doubled with the scale
    }
    engine.stop();
}

TEST_CASE("#27: ALIGN refuses degenerate point pairs and an empty selection") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    engine.submit(AlignSelectionCommand{{0, 0}, {0, 0}, {10, 0}, {20, 0}, false, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Nothing selected to align.") != std::string::npos;
    }));

    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(AlignSelectionCommand{{0, 0}, {5, 5}, {0, 0}, {5, 15}, false, 3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("must differ") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(has_vertex_near(engine.snapshot(), {10, 0}, 1e-9)); // untouched
    engine.stop();
}

TEST_CASE("#27: LENGTHEN moves the end nearer the pick and anchors the other") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    LengthenCommand c;
    c.pick = {98.0, 0.0}; // near the far end
    c.pick_radius = 3.0;
    c.mode = LengthenCommand::Mode::Total;
    c.value = 150.0;
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Length changed from 100 to 150.") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(has_vertex_near(engine.snapshot(), {0, 0}, 1e-9));   // anchored
    REQUIRE(has_vertex_near(engine.snapshot(), {150, 0}, 1e-9)); // moved
    engine.stop();
}

TEST_CASE("#27: picking near the OTHER end moves that end instead") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    LengthenCommand c;
    c.pick = {2.0, 0.0}; // near the near end
    c.pick_radius = 3.0;
    c.mode = LengthenCommand::Mode::Total;
    c.value = 150.0;
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Length changed") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(has_vertex_near(engine.snapshot(), {100, 0}, 1e-9)); // anchored
    REQUIRE(has_vertex_near(engine.snapshot(), {-50, 0}, 1e-9)); // grew backwards
    engine.stop();
}

TEST_CASE("#27: LENGTHEN's Delta and Percent modes") {
    const auto run = [](LengthenCommand::Mode m, double v) {
        GeometryEngine engine;
        engine.start();
        engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
        REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
        LengthenCommand c;
        c.pick = {99.0, 0.0};
        c.pick_radius = 3.0;
        c.mode = m;
        c.value = v;
        c.group = 2;
        engine.submit(c);
        REQUIRE(wait_until(engine, [](const auto& s) {
            return s.status.find("Length changed") != std::string::npos;
        }));
        engine.consume_snapshot();
        double maxx = -1e9;
        for (const Vec2& p : engine.snapshot().line_vertices) {
            maxx = std::max(maxx, p.x);
        }
        engine.stop();
        return maxx;
    };
    REQUIRE(run(LengthenCommand::Mode::Delta, 25.0) == Approx(125.0));   // +25
    REQUIRE(run(LengthenCommand::Mode::Delta, -40.0) == Approx(60.0));   // shortens
    REQUIRE(run(LengthenCommand::Mode::Percent, 50.0) == Approx(50.0));  // half
    REQUIRE(run(LengthenCommand::Mode::Percent, 200.0) == Approx(200.0));
}

TEST_CASE("#27: LENGTHEN works on an arc, in arc length") {
    // A quarter circle of radius 10 is 15.707... long; doubling it makes a half circle,
    // so the far end swings round to (-10, 0).
    GeometryEngine engine;
    engine.start();
    engine.submit(AddArcCommand{{0, 0}, 10.0, 0.0, kPi / 2.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    LengthenCommand c;
    c.pick = {0.0, 10.0}; // the end of the sweep
    c.pick_radius = 2.0;
    c.mode = LengthenCommand::Mode::Percent;
    c.value = 200.0;
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Length changed") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(has_vertex_near(engine.snapshot(), {10, 0}, 0.2));  // fixed end
    REQUIRE(has_vertex_near(engine.snapshot(), {-10, 0}, 0.2)); // swung to a half circle
    engine.stop();
}

TEST_CASE("#27: LENGTHEN refuses to erase the object or to touch a shape with no end") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    engine.submit(AddCircleCommand{{500, 500}, 10.0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 2; }));

    LengthenCommand kill;
    kill.pick = {99.0, 0.0};
    kill.pick_radius = 3.0;
    kill.mode = LengthenCommand::Mode::Delta;
    kill.value = -200.0; // would leave negative length
    kill.group = 3;
    engine.submit(kill);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("would leave nothing") != std::string::npos;
    }));

    LengthenCommand circle;
    circle.pick = {510.0, 500.0};
    circle.pick_radius = 3.0;
    circle.mode = LengthenCommand::Mode::Total;
    circle.value = 50.0;
    circle.group = 4;
    engine.submit(circle);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("only lines and arcs") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(has_vertex_near(engine.snapshot(), {100, 0}, 1e-9)); // the line survived intact
    engine.stop();
}

TEST_CASE("#27: the ALIGN and LENGTHEN command flows") {
    {
        ProcHarness h;
        h.proc.set_selection_count(1);
        h.proc.submit_line("AL");
        h.proc.submit_line("0,0");
        h.proc.submit_line("5,5");
        h.proc.submit_line("10,0");
        h.proc.submit_line("5,15");
        h.proc.submit_line("");  // no third pair
        h.proc.submit_line("Y");
        REQUIRE(h.cmds.size() == 1);
        const auto* a = std::get_if<AlignSelectionCommand>(&h.cmds[0]);
        REQUIRE(a != nullptr);
        REQUIRE(a->src1 == Vec2{0, 0});
        REQUIRE(a->dst2 == Vec2{5, 15});
        REQUIRE(a->scale);
    }
    {
        ProcHarness h;
        h.proc.set_selection_count(0);
        h.proc.submit_line("ALIGN"); // no selection -> ends without prompting
        REQUIRE(h.cmds.empty());
        REQUIRE(!h.proc.has_active_command());
    }
    {
        ProcHarness h;
        h.proc.submit_line("LEN");
        h.proc.submit_line("DE"); // Delta
        h.proc.submit_line("25");
        h.proc.submit_line("99,0");
        REQUIRE(h.cmds.size() == 1);
        const auto* l = std::get_if<LengthenCommand>(&h.cmds[0]);
        REQUIRE(l != nullptr);
        REQUIRE(l->mode == LengthenCommand::Mode::Delta);
        REQUIRE(l->value == Approx(25.0));
    }
    {
        // Percent and Total both refuse a non-positive amount rather than guessing.
        ProcHarness h;
        h.proc.submit_line("LENGTHEN");
        h.proc.submit_line("P");
        h.proc.submit_line("0");
        REQUIRE(h.cmds.empty());
        REQUIRE(h.proc.has_active_command());
        h.proc.submit_line("50");
        h.proc.submit_line("99,0");
        REQUIRE(h.cmds.size() == 1);
    }
}

TEST_CASE("#52: ALIGN with one pair moves; a third pair aligns unscaled; the rubber lines") {
    {
        ProcHarness h;
        h.proc.set_selection_count(1);
        h.proc.submit_line("AL");
        h.proc.submit_line("0,0");
        REQUIRE(h.proc.preview().kind == musacad::command::PreviewKind::Segment);
        h.proc.submit_line("5,5");
        h.proc.submit_line(""); // one pair: a plain move
        REQUIRE(h.cmds.size() == 1);
        const auto* m = std::get_if<MoveSelectionCommand>(&h.cmds[0]);
        REQUIRE(m != nullptr);
        REQUIRE(m->delta == Vec2{5, 5});
        REQUIRE(!h.proc.has_active_command());
    }
    {
        ProcHarness h;
        h.proc.set_selection_count(1);
        h.proc.submit_line("AL");
        h.proc.submit_line("0,0");
        h.proc.submit_line("5,5");
        h.proc.submit_line("10,0");
        h.proc.submit_line("5,15");
        h.proc.submit_line("20,0");  // a third pair: no scale question
        h.proc.submit_line("5,25");
        REQUIRE(h.cmds.size() == 1);
        const auto* a = std::get_if<AlignSelectionCommand>(&h.cmds[0]);
        REQUIRE(a != nullptr);
        REQUIRE(!a->scale);
        REQUIRE(!h.proc.has_active_command());
    }
}

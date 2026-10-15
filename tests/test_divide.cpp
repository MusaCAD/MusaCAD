// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// POINT, DIVIDE and MEASURE (issue #27, and #23's POINT).
//
// Point entities were already stored, drawn, picked, bounded and persisted -- what was
// missing was any command that creates one, which is also what blocked DIVIDE and
// MEASURE. The two marking commands differ only in AutoCAD's placement rule:
//
//   DIVIDE  n -> n-1 marks on an OPEN curve (its ends already divide it), but n on a
//                CLOSED one, where there is no free end to count from.
//   MEASURE d -> a mark every d from the start, never one AT the start.

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

std::vector<Vec2> marks(const RenderSnapshot& s) {
    std::vector<Vec2> out(s.points.begin(), s.points.end());
    std::sort(out.begin(), out.end(), [](const Vec2& a, const Vec2& b) {
        return a.x != b.x ? a.x < b.x : a.y < b.y;
    });
    return out;
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

TEST_CASE("#23: a POINT entity can finally be created, and it round-trips") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPointCommand{{7.0, 3.0}, 1, {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.points.size() == 1; }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().points[0].x == Approx(7.0));
    REQUIRE(engine.snapshot().points[0].y == Approx(3.0));

    // It participates in the ordinary edit machinery: select, move, undo.
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(MoveSelectionCommand{{10.0, 0.0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return !s.points.empty() && s.points[0].x > 16.0;
    }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return !s.points.empty() && s.points[0].x == Approx(7.0);
    }));
    engine.stop();
}

TEST_CASE("#27: DIVIDE puts n-1 marks inside an open curve") {
    // A 100-long line into 4 segments -> marks at 25, 50, 75. Nothing at the ends:
    // the endpoints already divide the curve, which is AutoCAD's rule.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    DividePathCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.segments = 4;
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Divided: 3 points placed.") != std::string::npos;
    }));
    engine.consume_snapshot();
    const std::vector<Vec2> m = marks(engine.snapshot());
    REQUIRE(m.size() == 3);
    REQUIRE(m[0].x == Approx(25.0));
    REQUIRE(m[1].x == Approx(50.0));
    REQUIRE(m[2].x == Approx(75.0));
    engine.stop();
}

TEST_CASE("#27: DIVIDE puts n marks on a closed curve") {
    // A circle has no free end, so 4 segments means 4 marks, not 3.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 50.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    DividePathCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.segments = 4;
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Divided: 4 points placed.") != std::string::npos;
    }));
    engine.consume_snapshot();
    // Every mark sits on the circle, a quarter-turn apart.
    for (const Vec2& p : engine.snapshot().points) {
        REQUIRE(length(p) == Approx(50.0).margin(0.5));
    }
    engine.stop();
}

TEST_CASE("#27: MEASURE steps from the start and never marks the start itself") {
    // 100 long, every 30 -> marks at 30, 60, 90. Not at 0, and not at 120.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    DividePathCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.distance = 30.0;
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Measured: 3 points placed.") != std::string::npos;
    }));
    engine.consume_snapshot();
    const std::vector<Vec2> m = marks(engine.snapshot());
    REQUIRE(m.size() == 3);
    REQUIRE(m[0].x == Approx(30.0));
    REQUIRE(m[1].x == Approx(60.0));
    REQUIRE(m[2].x == Approx(90.0));
    engine.stop();
}

TEST_CASE("#27: marks follow a curve rather than the straight line between its ends") {
    // On an arc the marks must land ON the arc. This is why both commands share the
    // ARRAYPATH sampler instead of measuring chords.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddArcCommand{{0, 0}, 50.0, 0.0, kPi, 1}); // upper half circle
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    DividePathCommand c;
    c.pick = {0.0, 50.0};
    c.pick_radius = 2.0;
    c.segments = 4;
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Divided: 3 points placed.") != std::string::npos;
    }));
    engine.consume_snapshot();
    for (const Vec2& p : engine.snapshot().points) {
        REQUIRE(length(p) == Approx(50.0).margin(0.5)); // on the arc, not the chord
        REQUIRE(p.y > 0.0);
    }
    engine.stop();
}

TEST_CASE("#27: the marked curve is left alone and the marks are one undo group") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    DividePathCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.segments = 5;
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.points.size() == 4; }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().line_vertices.size() == 2); // the curve is untouched

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.points.empty() && s.line_vertices.size() == 2;
    }));
    engine.stop();
}

TEST_CASE("#27: DIVIDE and MEASURE report honestly when they cannot act") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    DividePathCommand miss;
    miss.pick = {900.0, 900.0};
    miss.pick_radius = 1.0;
    miss.segments = 3;
    miss.group = 2;
    engine.submit(miss);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Divide: no curve under the pick.") != std::string::npos;
    }));

    DividePathCommand too_long;
    too_long.pick = {5.0, 0.0};
    too_long.pick_radius = 1.0;
    too_long.distance = 500.0; // longer than the 10-unit line
    too_long.group = 3;
    engine.submit(too_long);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("longer than the curve") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().points.empty());
    engine.stop();
}

TEST_CASE("#23, #40: POINT places one point; MULTIPLE POINT stays open for repeated picks until Esc") {
    // AutoCAD's POINT places one point; the ribbon's Multiple Points is MULTIPLE POINT.
    ProcHarness h;
    h.proc.submit_line("PO");
    h.proc.submit_line("1,1");
    REQUIRE(h.cmds.size() == 1);
    REQUIRE_FALSE(h.proc.has_active_command());
    h.cmds.clear();
    h.proc.submit_line("MULTIPLE");
    h.proc.submit_line("PO");
    h.proc.submit_line("1,1");
    h.proc.submit_line("2,2");
    h.proc.submit_line("3,3");
    REQUIRE(h.cmds.size() == 3);
    REQUIRE(h.proc.has_active_command()); // still going
    for (const Command& c : h.cmds) {
        REQUIRE(std::get_if<AddPointCommand>(&c) != nullptr);
    }
    h.proc.cancel();
    REQUIRE(!h.proc.has_active_command());
}

TEST_CASE("#27: the DIVIDE and MEASURE flows carry their amount through") {
    {
        ProcHarness h;
        h.proc.submit_line("DIV");
        h.proc.submit_line("50,0");
        h.proc.submit_line("6");
        REQUIRE(h.cmds.size() == 1);
        const auto* d = std::get_if<DividePathCommand>(&h.cmds[0]);
        REQUIRE(d != nullptr);
        REQUIRE(d->segments == 6);
        REQUIRE(d->distance == Approx(0.0));
    }
    {
        ProcHarness h;
        h.proc.submit_line("ME");
        h.proc.submit_line("50,0");
        h.proc.submit_line("12.5");
        REQUIRE(h.cmds.size() == 1);
        const auto* d = std::get_if<DividePathCommand>(&h.cmds[0]);
        REQUIRE(d != nullptr);
        REQUIRE(d->distance == Approx(12.5));
        REQUIRE(d->segments == 0);
    }
}

TEST_CASE("#27: DIVIDE refuses a segment count below two rather than guessing") {
    ProcHarness h;
    h.proc.submit_line("DIVIDE");
    h.proc.submit_line("50,0");
    h.proc.submit_line("1"); // meaningless: one segment is the whole curve
    REQUIRE(h.cmds.empty());
    REQUIRE(h.proc.has_active_command()); // still asking
    h.proc.submit_line("3");
    REQUIRE(h.cmds.size() == 1);
}

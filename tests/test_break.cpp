// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// BREAK and BREAKATPOINT (issue #27). Break removes the piece of a curve between two
// points; break-at-point splits without removing anything. The interesting cases are
// the ones where "the piece between" is ambiguous until you fix a convention:
//
//   * a CIRCLE has no ends, so AutoCAD removes the counter-clockwise piece from the
//     first point to the second, and what survives is a single arc;
//   * a CLOSED polyline likewise becomes one open run;
//   * a break at or past an END of an open curve just shortens it, rather than leaving
//     a zero-length stub behind.

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

TEST_CASE("#27: breaking a line leaves the two outer pieces and removes the middle") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    BreakCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.p1 = {30.0, 0.0};
    c.p2 = {70.0, 0.0};
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Broke 1 object into 2.") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(s.line_vertices.size() == 4); // two segments
    REQUIRE(has_vertex_near(s, {0, 0}, 1e-9));
    REQUIRE(has_vertex_near(s, {30, 0}, 1e-9));
    REQUIRE(has_vertex_near(s, {70, 0}, 1e-9));
    REQUIRE(has_vertex_near(s, {100, 0}, 1e-9));
    // Nothing left in the gap.
    REQUIRE(!has_vertex_near(s, {50, 0}, 1e-9));
    engine.stop();
}

TEST_CASE("#27: the break points do not have to be given in order along the curve") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    BreakCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.p1 = {70.0, 0.0}; // given "backwards"
    c.p2 = {30.0, 0.0};
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    engine.consume_snapshot();
    REQUIRE(has_vertex_near(engine.snapshot(), {30, 0}, 1e-9));
    REQUIRE(has_vertex_near(engine.snapshot(), {70, 0}, 1e-9));
    engine.stop();
}

TEST_CASE("#27: breaking at an end just shortens the line, with no zero-length stub") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    BreakCommand c;
    c.pick = {10.0, 0.0};
    c.pick_radius = 1.0;
    c.p1 = {0.0, 0.0}; // the very start
    c.p2 = {40.0, 0.0};
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Broke 1 object into 1.") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().line_vertices.size() == 2); // ONE piece, not two
    REQUIRE(has_vertex_near(engine.snapshot(), {40, 0}, 1e-9));
    REQUIRE(has_vertex_near(engine.snapshot(), {100, 0}, 1e-9));
    engine.stop();
}

TEST_CASE("#27: BREAKATPOINT splits a line in two with no gap") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    BreakCommand c;
    c.pick = {40.0, 0.0};
    c.pick_radius = 1.0;
    c.p1 = {40.0, 0.0};
    c.p2 = {40.0, 0.0}; // same point
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Split into 2 objects.") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(s.line_vertices.size() == 4);
    // Both pieces meet at the split point: no gap.
    int at_split = 0;
    for (const Vec2& v : s.line_vertices) {
        if (length(v - Vec2{40, 0}) < 1e-9) {
            ++at_split;
        }
    }
    REQUIRE(at_split == 2);
    engine.stop();
}

TEST_CASE("#27: breaking a circle leaves a single arc") {
    // A circle with a piece missing IS an arc, so the result is one entity, not two.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 50.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    const std::size_t full = engine.snapshot().line_vertices.size();

    BreakCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.p1 = {50.0, 0.0};  // angle 0
    c.p2 = {0.0, 50.0};  // angle 90 -- the quarter between them goes
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Broke 1 object into 1.") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(s.line_vertices.size() < full);   // three quarters of a circle
    REQUIRE(!s.line_vertices.empty());
    for (const Vec2& v : s.line_vertices) {
        REQUIRE(length(v) == Approx(50.0).margin(0.5)); // still on the circle
    }
    // The removed quarter is the first quadrant, so nothing survives near (35,35).
    REQUIRE(!has_vertex_near(s, {35.36, 35.36}, 2.0));
    engine.stop();
}

TEST_CASE("#27: a circle refuses a break at a single point") {
    // There is no free end to split at, so BREAKATPOINT on a circle is meaningless.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 50.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    const std::size_t full = engine.snapshot().line_vertices.size();

    BreakCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.p1 = {50.0, 0.0};
    c.p2 = {50.0, 0.0};
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("needs two different break points") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().line_vertices.size() == full); // untouched
    engine.stop();
}

TEST_CASE("#27: breaking an arc keeps both remaining ends of the sweep") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddArcCommand{{0, 0}, 50.0, 0.0, kPi, 1}); // upper half
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    BreakCommand c;
    c.pick = {0.0, 50.0};
    c.pick_radius = 2.0;
    // Break out the middle third of the sweep (60 to 120 degrees).
    c.p1 = {50.0 * std::cos(kPi / 3.0), 50.0 * std::sin(kPi / 3.0)};
    c.p2 = {50.0 * std::cos(2.0 * kPi / 3.0), 50.0 * std::sin(2.0 * kPi / 3.0)};
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Broke 1 object into 2.") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(has_vertex_near(s, {50, 0}, 0.5));   // the original start survives
    REQUIRE(has_vertex_near(s, {-50, 0}, 0.5));  // and the original end
    REQUIRE(!has_vertex_near(s, {0, 50}, 1.0));  // the middle is gone
    engine.stop();
}

TEST_CASE("#27: breaking an open polyline splits it into two runs") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {50, 0}, {50, 50}, {100, 50}}, false, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 6; }));

    BreakCommand c;
    c.pick = {50.0, 20.0};
    c.pick_radius = 1.0;
    c.p1 = {50.0, 10.0};
    c.p2 = {50.0, 40.0};
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Broke 1 object into 2.") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(has_vertex_near(s, {0, 0}, 1e-9));    // start of the first run
    REQUIRE(has_vertex_near(s, {50, 10}, 1e-9));  // where it was cut
    REQUIRE(has_vertex_near(s, {50, 40}, 1e-9));  // where the second run starts
    REQUIRE(has_vertex_near(s, {100, 50}, 1e-9)); // end of the second run
    engine.stop();
}

TEST_CASE("#27: breaking a closed polyline leaves one open run") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}, true, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    BreakCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.p1 = {20.0, 0.0};
    c.p2 = {80.0, 0.0};
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Broke 1 object into 1.") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(has_vertex_near(s, {80, 0}, 1e-9));
    REQUIRE(has_vertex_near(s, {20, 0}, 1e-9));
    REQUIRE(has_vertex_near(s, {100, 100}, 1e-9)); // it went the long way round
    REQUIRE(!has_vertex_near(s, {50, 0}, 1e-9));   // the picked stretch is gone
    engine.stop();
}

TEST_CASE("#27: BREAK is one undo group and reports honestly when it misses") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    BreakCommand miss;
    miss.pick = {900.0, 900.0};
    miss.pick_radius = 1.0;
    miss.p1 = {900.0, 900.0};
    miss.p2 = {910.0, 900.0};
    miss.group = 2;
    engine.submit(miss);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Break: no curve under the pick.") != std::string::npos;
    }));

    BreakCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.p1 = {30.0, 0.0};
    c.p2 = {70.0, 0.0};
    c.group = 3;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.line_vertices.size() == 2 && has_vertex_near(s, {100, 0}, 1e-9);
    }));
    engine.stop();
}

TEST_CASE("#27: the selecting click doubles as the first break point") {
    // AutoCAD's flow: two clicks in the common case. "First point" re-asks when the
    // selecting click was not where the break belongs.
    {
        ProcHarness h;
        h.proc.submit_line("BR");
        h.proc.submit_line("30,0"); // selects AND is the first break point
        h.proc.submit_line("70,0");
        REQUIRE(h.cmds.size() == 1);
        const auto* b = std::get_if<BreakCommand>(&h.cmds[0]);
        REQUIRE(b != nullptr);
        REQUIRE(b->pick == Vec2{30, 0});
        REQUIRE(b->p1 == Vec2{30, 0});
        REQUIRE(b->p2 == Vec2{70, 0});
    }
    {
        ProcHarness h;
        h.proc.submit_line("BREAK");
        h.proc.submit_line("50,0"); // selects here
        h.proc.submit_line("F");    // ...but break somewhere else
        h.proc.submit_line("30,0");
        h.proc.submit_line("70,0");
        REQUIRE(h.cmds.size() == 1);
        const auto* b = std::get_if<BreakCommand>(&h.cmds[0]);
        REQUIRE(b != nullptr);
        REQUIRE(b->pick == Vec2{50, 0}); // still selected by the first click
        REQUIRE(b->p1 == Vec2{30, 0});
        REQUIRE(b->p2 == Vec2{70, 0});
    }
}

TEST_CASE("#27: BREAKATPOINT selects, then takes the one break point and fires") {
    ProcHarness h;
    h.proc.submit_line("BREAKATPOINT");
    h.proc.submit_line("40,0"); // selects the object
    h.proc.submit_line("40,0"); // the break point (AutoCAD asks it separately)
    REQUIRE(h.cmds.size() == 1);
    const auto* b = std::get_if<BreakCommand>(&h.cmds[0]);
    REQUIRE(b != nullptr);
    REQUIRE(b->p1 == b->p2); // the signal for "split, do not remove"
    REQUIRE(!h.proc.has_active_command());
}

TEST_CASE("#52: BREAK @ breaks at the selecting point; BREAKATPOINT asks the point") {
    {
        ProcHarness h;
        h.proc.submit_line("BREAK");
        h.proc.submit_line("50,0");
        h.proc.submit_line("@"); // the last point: a single break point
        REQUIRE(h.cmds.size() == 1);
        const auto* b = std::get_if<BreakCommand>(&h.cmds[0]);
        REQUIRE(b != nullptr);
        REQUIRE(b->p1.x == Approx(50.0));
        REQUIRE(b->p2.x == Approx(50.0));
    }
    {
        ProcHarness h;
        h.proc.submit_line("BREAKATPOINT");
        h.proc.submit_line("50,0"); // selects
        REQUIRE(h.cmds.empty());    // and asks the point
        h.proc.submit_line("30,0");
        REQUIRE(h.cmds.size() == 1);
        const auto* b = std::get_if<BreakCommand>(&h.cmds[0]);
        REQUIRE(b != nullptr);
        REQUIRE(b->pick.x == Approx(50.0));
        REQUIRE(b->p1.x == Approx(30.0));
        REQUIRE(b->p2.x == Approx(30.0));
    }
}

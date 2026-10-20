// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// REVCLOUD (issue #33). A cloud is a closed polyline of outward arcs; Rectangular and
// Polygonal build it from points, Object converts an existing curve (replacing it), and
// Reverse direction flips the lobes. Lobe geometry is core::polyline_ops, tested there.

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
struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string&) override {}
    void set_prompt(const std::string&) override {}
};
struct ProcHarness {
    std::vector<Command> cmds;
    SilentOutput out;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
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

TEST_CASE("#33: REVCLOUD Rectangular makes a closed cloud of outward arcs") {
    ProcHarness h;
    h.proc.submit_line("REVCLOUD");
    h.proc.submit_line("A");   // arc length
    h.proc.submit_line("25");
    h.proc.submit_line("25");
    h.proc.submit_line("R");
    h.proc.submit_line("0,0");
    h.proc.submit_line("100,50");
    const auto* p = h.last<AddPolylineCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->closed);
    REQUIRE(p->points.size() == 12); // 4 + 2 + 4 + 2 lobes of 25
    REQUIRE(p->bulges.size() == 12);
    for (double b : p->bulges) {
        REQUIRE(b == Approx(std::tan(kPi / 6.0))); // outward (CCW arcs) for a CCW loop
    }
}

TEST_CASE("#33: REVCLOUD Polygonal closes on Enter and needs three points") {
    ProcHarness h;
    h.proc.submit_line("REVCLOUD");
    h.proc.submit_line("A");
    h.proc.submit_line("20");
    h.proc.submit_line("20");
    h.proc.submit_line("P");
    h.proc.submit_line("0,0");
    h.proc.submit_line("100,0");
    h.proc.submit_line("");   // only two points: refused, still open
    REQUIRE(h.last<AddPolylineCommand>() == nullptr);
    REQUIRE(h.proc.has_active_command());
    h.proc.submit_line("50,80");
    h.proc.submit_line("");   // closes
    const auto* p = h.last<AddPolylineCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->closed);
    REQUIRE(p->points.size() > 3);
    REQUIRE(!h.proc.has_active_command());
}

TEST_CASE("#33: a point at the main prompt starts a clicked path (the Freehand type)") {
    ProcHarness h;
    h.proc.submit_line("REVCLOUD");
    h.proc.submit_line("A");
    h.proc.submit_line("30");
    h.proc.submit_line("30");
    h.proc.submit_line("0,0");
    h.proc.submit_line("60,0");
    h.proc.submit_line("60,60");
    h.proc.submit_line("");
    const auto* p = h.last<AddPolylineCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->closed);
}

TEST_CASE("#33: REVCLOUD Object converts a circle into a closed cloud and can reverse it") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 50.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    RevcloudObjectCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.arc_len = 30.0;
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Converted to a revision cloud of") != std::string::npos;
    }));
    engine.consume_snapshot();
    // The lobes reach outside the original circle: the drawn geometry extends past r=50.
    const auto max_r = [](const RenderSnapshot& s) {
        double m = 0.0;
        for (const Vec2& v : s.line_vertices) {
            m = std::max(m, length(v));
        }
        return m;
    };
    REQUIRE(max_r(engine.snapshot()) > 51.0);
    REQUIRE(engine.snapshot().selection.size() == 1); // the cloud is selected

    engine.submit(RevcloudReverseCommand{3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Reversed the arc direction.") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(max_r(engine.snapshot()) < 50.5); // lobes now point inward

    engine.submit(UndoLastGroupCommand{});
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return std::abs(s.bounds_max.x - 50.0) < 0.5 && std::abs(s.bounds_min.x + 50.0) < 0.5;
    }));
    engine.stop();
}

TEST_CASE("#33: REVCLOUD Object on a line makes an open cloud; a miss says so") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    RevcloudObjectCommand miss;
    miss.pick = {500.0, 500.0};
    miss.pick_radius = 1.0;
    miss.arc_len = 20.0;
    miss.group = 2;
    engine.submit(miss);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("no object under the pick") != std::string::npos;
    }));

    RevcloudObjectCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.arc_len = 20.0;
    c.group = 3;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Converted to a revision cloud of") != std::string::npos;
    }));
    engine.consume_snapshot();
    double top = 0.0;
    for (const Vec2& v : engine.snapshot().line_vertices) {
        top = std::max(top, std::abs(v.y));
    }
    REQUIRE(top > 1.0); // lobes stand off the line
    engine.stop();
}

TEST_CASE("#33: the REVCLOUD Object flow asks about reversing, and Enter defaults to No") {
    ProcHarness h;
    h.proc.submit_line("REVCLOUD");
    h.proc.submit_line("");     // <Object> default
    h.proc.submit_line("50,0"); // pick
    REQUIRE(h.last<RevcloudObjectCommand>() != nullptr);
    REQUIRE(h.proc.has_active_command()); // "Reverse direction [Yes/No] <No>:"
    h.proc.submit_line("");
    REQUIRE(h.last<RevcloudReverseCommand>() == nullptr);
    REQUIRE(!h.proc.has_active_command());

    ProcHarness g;
    g.proc.submit_line("REVCLOUD");
    g.proc.submit_line("O");
    g.proc.submit_line("50,0");
    g.proc.submit_line("Y");
    REQUIRE(g.last<RevcloudReverseCommand>() != nullptr);
}

TEST_CASE("#33: REVCLOUD arc lengths are validated and remembered") {
    {
        ProcHarness h;
        h.proc.submit_line("REVCLOUD");
        h.proc.submit_line("A");
        h.proc.submit_line("-1"); // refused
        REQUIRE(h.proc.has_active_command());
        h.proc.submit_line("10");
        h.proc.submit_line("5"); // below the minimum: refused
        h.proc.submit_line("10");
        h.proc.submit_line("R");
        h.proc.submit_line("0,0");
        h.proc.submit_line("40,10");
        const auto* p = h.last<AddPolylineCommand>();
        REQUIRE(p != nullptr);
        REQUIRE(p->points.size() == 10); // 4 + 1 + 4 + 1 lobes of 10
    }
    {
        ProcHarness h; // the session keeps the last arc length
        h.proc.submit_line("REVCLOUD");
        h.proc.submit_line("R");
        h.proc.submit_line("0,0");
        h.proc.submit_line("40,10");
        REQUIRE(h.last<AddPolylineCommand>()->points.size() == 10);
    }
}

TEST_CASE("#40: REVCLOUD Modify redraws a stretch of a closed cloud and erases the side picked") {
    GeometryEngine engine;
    engine.start();
    // A 100 x 100 square with vertices at x 20 and 80 on its bottom edge: the new stretch
    // leaves at the first (the vertex nearest the pick) and comes back at the second, bowed
    // down to y -30; the bottom edge's middle is erased.
    AddPolylineCommand sq;
    sq.points = {{0, 0}, {20, 0}, {80, 0}, {100, 0}, {100, 100}, {0, 100}};
    sq.closed = true;
    sq.group = 1;
    engine.submit(sq);
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    RevcloudObjectCommand c;
    c.pick = {20.5, 0.5};
    c.pick_radius = 2.0;
    c.arc_len = 10.0;
    c.group = 2;
    c.modify = true;
    c.path = {{20.5, 0.5}, {20.0, -30.0}, {80.0, -30.0}, {79.0, 0.5}};
    c.side = {50.0, 0.0};
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Revision cloud modified.") != std::string::npos;
    }));
    engine.consume_snapshot();
    const auto& s = engine.snapshot();
    REQUIRE(s.selection.size() == 1);
    // The cloud reaches below the stretch's path (its lobes face out); the square's other
    // edges are still there, and nothing is left on the bottom edge between x 25 and 75.
    REQUIRE(s.bounds_min.y < -31.0);
    REQUIRE(s.bounds_max.x == Approx(100.0).margin(1e-6));
    REQUIRE(s.bounds_max.y == Approx(100.0).margin(1e-6));
    bool middle = false;
    for (std::size_t i = 0; i + 1 < s.line_vertices.size(); i += 2) {
        const Vec2 a = s.line_vertices[i];
        const Vec2 b = s.line_vertices[i + 1];
        if (std::abs(a.y) < 1e-6 && std::abs(b.y) < 1e-6 && std::max(a.x, b.x) > 25.0 &&
            std::min(a.x, b.x) < 75.0) {
            middle = true;
        }
    }
    REQUIRE_FALSE(middle);

    // One undo puts the square back.
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s2) { return std::abs(s2.bounds_min.y) < 1e-6; }));
    engine.stop();
}

TEST_CASE("#40: REVCLOUD Modify on an open polyline replaces the part between the two vertices") {
    GeometryEngine engine;
    engine.start();
    AddPolylineCommand pl;
    pl.points = {{0, 0}, {30, 0}, {70, 0}, {100, 0}};
    pl.group = 1;
    engine.submit(pl);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 6; }));

    RevcloudObjectCommand c;
    c.pick = {70.0, 0.0}; // picked at the far end: the stretch runs back to x 30
    c.pick_radius = 2.0;
    c.arc_len = 10.0;
    c.group = 2;
    c.modify = true;
    c.path = {{70.0, 0.0}, {70.0, 20.0}, {30.0, 20.0}, {30.0, 0.0}};
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Revision cloud modified.") != std::string::npos;
    }));
    engine.consume_snapshot();
    const auto& s = engine.snapshot();
    REQUIRE(s.bounds_min.x == Approx(0.0).margin(1e-6));
    REQUIRE(s.bounds_max.x == Approx(100.0).margin(1e-6));
    REQUIRE(s.bounds_max.y > 20.5);
    // The ends are kept and run on into the stretch; the middle of the line is gone.
    bool head = false;
    bool tail = false;
    bool middle = false;
    for (std::size_t i = 0; i + 1 < s.line_vertices.size(); i += 2) {
        const Vec2 a = s.line_vertices[i];
        const Vec2 b = s.line_vertices[i + 1];
        if (std::abs(a.y) > 1e-6 || std::abs(b.y) > 1e-6) {
            continue;
        }
        head = head || (std::min(a.x, b.x) < 1e-6 && std::max(a.x, b.x) > 29.0);
        tail = tail || (std::max(a.x, b.x) > 100.0 - 1e-6 && std::min(a.x, b.x) < 71.0);
        middle = middle || (std::max(a.x, b.x) > 35.0 && std::min(a.x, b.x) < 65.0);
    }
    REQUIRE(head);
    REQUIRE(tail);
    REQUIRE_FALSE(middle);
    engine.stop();
}

TEST_CASE("#40: the REVCLOUD Modify flow: Select polyline, the points, the side and Reverse") {
    ProcHarness h;
    h.proc.submit_line("REVCLOUD");
    h.proc.submit_line("M");
    h.proc.submit_line("20,0");   // Select polyline: (typed, so the side is asked)
    h.proc.submit_line("");       // Enter before a point: refused
    REQUIRE(h.proc.has_active_command());
    h.proc.submit_line("20,-30");
    h.proc.submit_line("50,-50");
    h.proc.submit_line("U");      // Undo drops 50,-50
    h.proc.submit_line("80,-30");
    h.proc.submit_line("80,0");
    h.proc.submit_line("");       // Enter: back onto the polyline
    REQUIRE(h.last<RevcloudObjectCommand>() == nullptr);
    h.proc.submit_line("50,0");   // Pick a side to erase:
    h.proc.submit_line("Y");      // Reverse direction
    const auto* c = h.last<RevcloudObjectCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->modify);
    REQUIRE(c->reverse);
    REQUIRE(c->path.size() == 4);
    REQUIRE(c->path[1].x == Approx(20.0));
    REQUIRE(c->path[2].x == Approx(80.0));
    REQUIRE(c->side.x == Approx(50.0));
    REQUIRE(c->pick.x == Approx(20.0));
    REQUIRE(!h.proc.has_active_command());
}

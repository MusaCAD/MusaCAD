// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// PEDIT: close/open, reverse (bulge signs follow), decurve, spline, vertex insert /
// delete / move, and the line-to-polyline conversion; every edit an undo group.

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
bool eq(Vec2 p, Vec2 q, double eps = 1e-6) { return std::abs(p.x - q.x) < eps && std::abs(p.y - q.y) < eps; }
bool has_segment(const RenderSnapshot& s, Vec2 a, Vec2 b) {
    for (std::size_t i = 0; i + 1 < s.line_vertices.size(); i += 2) {
        const Vec2 p = s.line_vertices[i];
        const Vec2 q = s.line_vertices[i + 1];
        if ((eq(p, a) && eq(q, b)) || (eq(p, b) && eq(q, a))) {
            return true;
        }
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
    int count() const {
        int n = 0;
        for (const Command& c : cmds) {
            n += std::holds_alternative<T>(c) ? 1 : 0;
        }
        return n;
    }
    template <class T>
    const T* last() const {
        const T* found = nullptr;
        for (const Command& c : cmds) {
            if (const auto* p = std::get_if<T>(&c)) {
                found = p;
            }
        }
        return found;
    }
};
} // namespace

TEST_CASE("PEDIT: Close/Open, vertex Insert/Delete/Move, Reverse and Decurve, each undoable") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {10, 0}, {10, 10}}, false, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));

    engine.submit(PeditCommand{{5, 0}, 1.0, 0, {}, {}, 2}); // Close
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {10, 10}, {0, 0}); }));
    engine.submit(PeditCommand{{5, 0}, 1.0, 1, {}, {}, 3}); // Open
    REQUIRE(wait_until(engine, [](const auto& s) { return !has_segment(s, {10, 10}, {0, 0}); }));

    engine.submit(PeditCommand{{5, 0}, 1.0, 5, {5, 3}, {}, 4}); // insert (5,3) into the first segment
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {5, 3}) && has_segment(s, {5, 3}, {10, 0}); }));
    engine.submit(PeditCommand{{5, 3}, 1.0, 7, {5, 3}, {5, -4}, 5}); // move it to (5,-4)
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {5, -4}); }));
    engine.submit(PeditCommand{{5, -4}, 1.0, 6, {5, -4}, {}, 6}); // delete it again
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {5, -4}); }));
    engine.submit(RedoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));

    // Too few vertices to delete from an open 2-vertex polyline: refused.
    engine.submit(NewDocumentCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    engine.submit(AddPolylineCommand{{{0, 0}, {10, 0}}, false, 7});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(PeditCommand{{5, 0}, 1.0, 6, {0, 0}, {}, 8});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("too few vertices") != std::string::npos; }));
    engine.stop();
}

TEST_CASE("PEDIT: Reverse flips bulge signs so the arc stays put; Decurve straightens; Spline makes a spline-fit polyline") {
    GeometryEngine engine;
    engine.start();
    // (0,0)->(10,0) as a lower semicircle (bulge 1), then (10,0)->(10,10) straight.
    engine.submit(AddPolylineCommand{{{0, 0}, {10, 0}, {10, 10}}, false, 1, {}, {1.0, 0.0, 0.0}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 10; }));
    const auto min_y = [](const RenderSnapshot& s) {
        double m = 0.0;
        for (const Vec2& p : s.line_vertices) {
            m = std::min(m, p.y);
        }
        return m;
    };
    engine.consume_snapshot();
    REQUIRE(min_y(engine.snapshot()) == Approx(-5.0).margin(0.05)); // the arc dips to -5

    engine.submit(PeditCommand{{10, 5}, 1.0, 2, {}, {}, 2}); // Reverse
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Reversed."; }));
    engine.consume_snapshot();
    REQUIRE(min_y(engine.snapshot()) == Approx(-5.0).margin(0.05)); // same shape, reversed order
    REQUIRE(has_segment(engine.snapshot(), {10, 10}, {10, 0}));

    engine.submit(PeditCommand{{10, 5}, 1.0, 3, {}, {}, 3}); // Decurve
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Decurved."; }));
    engine.consume_snapshot();
    REQUIRE(min_y(engine.snapshot()) == Approx(0.0).margin(1e-6));
    REQUIRE(has_segment(engine.snapshot(), {10, 0}, {0, 0}));

    engine.submit(PeditCommand{{10, 5}, 1.0, 4, {6.0, 8.0}, {}, 4}); // Spline: a spline-fit polyline
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Spline fit."; }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().selection.size() == 1);
    REQUIRE(engine.snapshot().selection[0].kind == EntityKind::Polyline);
    engine.stop();
}

TEST_CASE("PEDIT: a line under the pick is converted to a polyline first") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{10, 0}, {10, 10}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    engine.submit(PeditCommand{{5, 0}, 1.0, 5, {5, 2}, {}, 3}); // insert a vertex into the line
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status == "Converted to a polyline. Vertex inserted.";
    }));
    engine.consume_snapshot();
    REQUIRE(has_segment(engine.snapshot(), {0, 0}, {5, 2}));
    REQUIRE(engine.snapshot().selection[0].kind == EntityKind::Polyline);
    engine.stop();
}

TEST_CASE("PEDIT command flow: options map to edits, the vertex editor, Join hands off to JOIN") {
    ProcHarness h;
    h.proc.submit_line("PE");
    h.proc.submit_line("5,0");
    h.proc.submit_line("C");
    h.proc.submit_line("R");
    h.proc.submit_line("E");
    h.proc.submit_line("I");
    h.proc.submit_line("5,3");
    h.proc.submit_line("M"); // the vertex under the X moves
    h.proc.submit_line("5,-4");
    h.proc.submit_line("X");
    h.proc.submit_line("J");
    h.proc.submit_line("20,0");
    h.proc.submit_line("");
    h.proc.submit_line("U");
    h.proc.submit_line("");
    REQUIRE(h.count<PeditCommand>() == 6); // Close, Reverse; the vertex editor's start, Insert, Move, eXit
    REQUIRE(h.count<JoinPickCommand>() == 1);
    REQUIRE(h.count<UndoLastGroupCommand>() == 1);
    REQUIRE(h.last<JoinPickCommand>()->picks.size() == 2);
    REQUIRE(!h.proc.has_active_command());
    // Insert puts the point after the current vertex; Move takes the current one there.
    int inserts = 0;
    int moves = 0;
    for (const Command& c : h.cmds) {
        if (const auto* p = std::get_if<PeditCommand>(&c)) {
            if (p->op == 18) {
                ++inserts;
                REQUIRE(p->p1 == Vec2{5, 3});
            }
            if (p->op == 19) {
                ++moves;
                REQUIRE(p->p1 == Vec2{5, -4});
            }
        }
    }
    REQUIRE(inserts == 1);
    REQUIRE(moves == 1);
}

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// TRIM and EXTEND as AutoCAD has them (issue #48): Quick and Standard modes, chosen
// cutting edges, Edge=Extend, Crossing, eRase, mOde, Project, TRIMEXTENDMODE / EDGEMODE /
// PROJMODE.

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad::core;
using musacad::command::CommandOutput;
using musacad::command::CommandProcessor;
using musacad::command::TrimCommand;

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

bool has_segment(const RenderSnapshot& s, Vec2 a, Vec2 b, double eps = 1e-6) {
    const auto eq = [&](Vec2 p, Vec2 q) { return std::abs(p.x - q.x) < eps && std::abs(p.y - q.y) < eps; };
    for (std::size_t i = 0; i + 1 < s.line_vertices.size(); i += 2) {
        const Vec2 p = s.line_vertices[i];
        const Vec2 q = s.line_vertices[i + 1];
        if ((eq(p, a) && eq(q, b)) || (eq(p, b) && eq(q, a))) {
            return true;
        }
    }
    return false;
}

std::size_t segments(const RenderSnapshot& s) { return s.line_vertices.size() / 2; }

struct Out : CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
    [[nodiscard]] bool any_contains(const std::string& sub) const {
        for (const auto& l : lines) {
            if (l.find(sub) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
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

/// The settings are kept for the session; every test leaves them as AutoCAD ships them.
struct DefaultTrimSettings {
    DefaultTrimSettings() { reset(); }
    ~DefaultTrimSettings() { reset(); }
    static void reset() {
        TrimCommand::s_mode_ = 1;
        TrimCommand::s_edgemode_ = 0;
        TrimCommand::s_projmode_ = 1;
    }
};
} // namespace

// ---------------------------------------------------------------------------
// The engine: chosen edges, Edge=Extend, Crossing
// ---------------------------------------------------------------------------

TEST_CASE("TRIM with chosen cutting edges trims only at them, and deletes nothing in Standard mode") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {30, 0}, 1});    // to trim
    engine.submit(AddLineCommand{{10, -5}, {10, 5}, 2});  // the chosen edge
    engine.submit(AddLineCommand{{20, -5}, {20, 5}, 3});  // crosses too, not chosen
    engine.submit(AddLineCommand{{40, -5}, {40, 5}, 4});  // stands alone
    REQUIRE(wait_until(engine, [](const auto& s) { return segments(s) == 4; }));

    engine.submit(SelectPickCommand{{10, 3}, 0.5, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(SetTrimEdgesCommand{SetTrimEdgesCommand::Op::FromSelection, false});
    engine.submit(TrimPickCommand{{25, 0}, 0.5, 10, false});
    // Only the chosen edge cuts: everything right of x = 10 goes, past the other line.
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {0, 0}, {10, 0}) && !has_segment(s, {0, 0}, {20, 0});
    }));
    REQUIRE(has_segment(engine.snapshot(), {20, -5}, {20, 5}));

    // Standard mode: an object with no edge to trim to stays.
    engine.submit(TrimPickCommand{{40, 1}, 0.5, 11, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("no crossing edge") != std::string::npos; }));
    REQUIRE(has_segment(engine.snapshot(), {40, -5}, {40, 5}));

    // Back to every object: the chosen edges stop showing as the selection.
    engine.submit(SetTrimEdgesCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.empty(); }));
    engine.stop();
}

TEST_CASE("TRIM Edge=Extend: an edge that does not reach the object cuts it along its extension") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {30, 0}, 1});  // to trim
    engine.submit(AddLineCommand{{10, 5}, {10, 8}, 2}); // short of it
    REQUIRE(wait_until(engine, [](const auto& s) { return segments(s) == 2; }));
    engine.submit(SelectPickCommand{{10, 6}, 0.5, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(SetTrimEdgesCommand{SetTrimEdgesCommand::Op::FromSelection, false});
    engine.submit(TrimPickCommand{{25, 0}, 0.5, 10, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("no crossing edge") != std::string::npos; }));

    engine.submit(SetTrimEdgesCommand{SetTrimEdgesCommand::Op::EdgeMode, true});
    engine.submit(TrimPickCommand{{25, 0}, 0.5, 11, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));
    engine.stop();
}

TEST_CASE("EXTEND Edge=Extend: a boundary short of the path is reached along its extension") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});  // to extend
    engine.submit(AddLineCommand{{20, 5}, {20, 8}, 2}); // the boundary, off to the side
    REQUIRE(wait_until(engine, [](const auto& s) { return segments(s) == 2; }));
    engine.submit(ExtendPickCommand{{9, 0}, 0.5, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("no boundary ahead") != std::string::npos; }));

    engine.submit(SetTrimEdgesCommand{SetTrimEdgesCommand::Op::All, true});
    engine.submit(ExtendPickCommand{{9, 0}, 0.5, 11});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {20, 0}); }));
    engine.stop();
}

TEST_CASE("EXTEND an arc stops at a boundary line only where the line is, unless Edge=Extend") {
    GeometryEngine engine;
    engine.start();
    // A quarter arc from (10,0) round to (0,10); the line x = -10 .. -5 at y = 0 lies on
    // the arc's circle's path only through its extension... a vertical line at x = -6
    // from y = 20 to 30 is off the circle; its extension crosses the circle at y = +-8.
    engine.submit(AddArcCommand{{0, 0}, 10.0, 0.0, 1.5707963267948966, 1});
    engine.submit(AddLineCommand{{-6, 20}, {-6, 30}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return segments(s) > 1; }));
    engine.submit(ExtendPickCommand{{0.5, 9.9}, 0.5, 10}); // the (0,10) end
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("no boundary ahead") != std::string::npos; }));
    engine.submit(SetTrimEdgesCommand{SetTrimEdgesCommand::Op::All, true});
    engine.submit(ExtendPickCommand{{0.5, 9.9}, 0.5, 11});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Extended."; }));
    engine.stop();
}

TEST_CASE("TRIM Crossing: each object crossing the window once, and what lies wholly inside") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {30, 0}, 1}); // the ground
    engine.submit(AddLineCommand{{5, -10}, {5, 10}, 2});
    engine.submit(AddLineCommand{{15, -10}, {15, 10}, 3});
    engine.submit(AddLineCommand{{25, -10}, {25, 10}, 4});
    engine.submit(AddLineCommand{{10, -6}, {12, -6}, 5}); // wholly inside, no edge
    REQUIRE(wait_until(engine, [](const auto& s) { return segments(s) == 5; }));

    TrimPathCommand c{{{2, -8}, {28, -2}}, false, 0.5, 10};
    c.window = true;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {5, 0}, {5, 10}) && has_segment(s, {15, 0}, {15, 10}) &&
               has_segment(s, {25, 0}, {25, 10}) && !has_segment(s, {10, -6}, {12, -6});
    }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().status.find("Trimmed 3 objects, deleted 1") != std::string::npos);
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return segments(s) == 5 && has_segment(s, {5, -10}, {5, 10}); }));
    engine.stop();
}

TEST_CASE("A trimmed cutting edge stays a cutting edge in its pieces") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{10, -10}, {10, 10}, 1}); // the edge, trimmed first
    engine.submit(AddLineCommand{{0, 5}, {20, 5}, 2});     // cuts the edge
    engine.submit(AddLineCommand{{0, -5}, {20, -5}, 3});   // trimmed at the edge afterwards
    REQUIRE(wait_until(engine, [](const auto& s) { return segments(s) == 3; }));
    engine.submit(SelectWindowCommand{{-1, -11}, {21, 11}, false, false, false}); // all three
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 3; }));
    engine.submit(SetTrimEdgesCommand{SetTrimEdgesCommand::Op::FromSelection, false});
    engine.submit(TrimPickCommand{{10, 8}, 0.5, 10, false}); // the edge's top
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {10, -10}, {10, 5}); }));
    engine.submit(TrimPickCommand{{15, -5}, 0.5, 11, false}); // cut by the edge's remaining piece
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, -5}, {10, -5}); }));
    engine.stop();
}

// ---------------------------------------------------------------------------
// The command
// ---------------------------------------------------------------------------

TEST_CASE("TRIM opens with the current settings and AutoCAD's Quick-mode options") {
    DefaultTrimSettings defaults;
    H h;
    h.proc.submit_line("TRIM");
    REQUIRE(h.out.any_contains("Current settings: Projection=UCS, Edge=None, Mode=Quick"));
    REQUIRE(h.out.prompt == "Select object to trim or shift-select to extend or [cuTting edges/Crossing/mOde/Project/eRase]: ");
    const auto* edges = h.last<SetTrimEdgesCommand>();
    REQUIRE(edges != nullptr);
    REQUIRE(edges->op == SetTrimEdgesCommand::Op::All);
    h.proc.submit_line("5,5");
    REQUIRE(h.last<TrimPickCommand>()->quick);
    REQUIRE(h.out.prompt.find("/eRase/Undo]: ") != std::string::npos);
    h.proc.submit_line("");
    REQUIRE_FALSE(h.proc.has_active_command());
    REQUIRE(h.last<SetTrimEdgesCommand>()->op == SetTrimEdgesCommand::Op::All); // let go at the end
}

TEST_CASE("TRIM mOde Standard asks for the cutting edges; Edge, a pick and an implied crossing follow") {
    DefaultTrimSettings defaults;
    H h;
    h.proc.submit_line("TR");
    h.proc.submit_line("O");
    REQUIRE(h.out.prompt == "Enter a trim mode option [Quick/Standard] <Quick>: ");
    h.proc.submit_line("S");
    REQUIRE(TrimCommand::s_mode_ == 0);
    REQUIRE(h.out.lines.back() == "Select cutting edges ...");
    REQUIRE(h.out.prompt == "Select objects or <select all>: ");
    REQUIRE(h.proc.in_selection_phase());
    REQUIRE(h.last<ClearSelectionCommand>() != nullptr);
    h.proc.submit_line(""); // <select all>
    REQUIRE(h.last<SetTrimEdgesCommand>()->op == SetTrimEdgesCommand::Op::FromSelection);
    REQUIRE(h.out.prompt ==
            "Select object to trim or shift-select to extend or [cuTting edges/Fence/Crossing/mOde/Project/Edge/eRase]: ");

    h.proc.submit_line("E");
    REQUIRE(h.out.prompt == "Enter an implied edge extension mode [Extend/No extend] <No extend>: ");
    h.proc.submit_line("E");
    REQUIRE(TrimCommand::s_edgemode_ == 1);
    const auto* edge = h.last<SetTrimEdgesCommand>();
    REQUIRE(edge->op == SetTrimEdgesCommand::Op::EdgeMode);
    REQUIRE(edge->edge_extend);

    h.proc.set_hovered_kind(EntityKind::Line);
    h.proc.pick_point({3, 3}, std::nullopt);
    REQUIRE_FALSE(h.last<TrimPickCommand>()->quick); // Standard mode deletes nothing

    h.proc.set_hovered_kind(std::nullopt);
    h.proc.pick_point({0, 0}, std::nullopt); // empty space: a crossing window
    REQUIRE(h.out.prompt == "Specify opposite corner: ");
    h.proc.pick_point({10, 10}, std::nullopt);
    const auto* w = h.last<TrimPathCommand>();
    REQUIRE(w != nullptr);
    REQUIRE(w->window);
    REQUIRE_FALSE(w->quick);
    h.proc.cancel();

    // The next TRIM starts where this one left off.
    h.proc.submit_line("TR");
    REQUIRE(h.out.any_contains("Current settings: Projection=UCS, Edge=Extend, Mode=Standard"));
    REQUIRE(h.out.prompt == "Select objects or <select all>: ");
    h.proc.cancel();
}

TEST_CASE("TRIM Standard mode takes objects selected beforehand as the cutting edges") {
    DefaultTrimSettings defaults;
    TrimCommand::s_mode_ = 0;
    H h;
    h.proc.set_selection_count(2);
    h.proc.submit_line("TRIM");
    REQUIRE(h.last<SetTrimEdgesCommand>()->op == SetTrimEdgesCommand::Op::FromSelection);
    REQUIRE_FALSE(h.proc.in_selection_phase());
    REQUIRE(h.out.prompt.rfind("Select object to trim", 0) == 0);
    h.proc.cancel();
}

TEST_CASE("TRIM Crossing, Project and eRase") {
    DefaultTrimSettings defaults;
    H h;
    h.proc.submit_line("TR");
    h.proc.submit_line("C");
    REQUIRE(h.out.prompt == "Specify first corner: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompt == "Specify opposite corner: ");
    h.proc.submit_line("10,5");
    const auto* w = h.last<TrimPathCommand>();
    REQUIRE(w->window);
    REQUIRE(w->quick);
    REQUIRE(std::abs(w->path[1].x - 10.0) < 1e-12);
    const std::uint64_t window_group = w->group; // `w` points into a vector that grows

    h.proc.submit_line("P");
    REQUIRE(h.out.prompt == "Enter a projection option [None/Ucs/View] <Ucs>: ");
    h.proc.submit_line("V");
    REQUIRE(TrimCommand::s_projmode_ == 2);

    h.proc.submit_line("R");
    REQUIRE(h.out.prompt == "Select objects to erase or <exit>: ");
    REQUIRE(h.proc.in_selection_phase());
    h.proc.set_selection_count(1);
    h.proc.submit_line("");
    REQUIRE(h.last<EraseSelectionCommand>() != nullptr);
    REQUIRE(h.last<EraseSelectionCommand>()->group != window_group); // its own undo step
    REQUIRE(h.out.prompt.find("/eRase/Undo]: ") != std::string::npos);
    h.proc.cancel();
}

TEST_CASE("EXTEND: Boundary edges and the Quick-mode options") {
    DefaultTrimSettings defaults;
    H h;
    h.proc.submit_line("EX");
    REQUIRE(h.out.prompt == "Select object to extend or shift-select to trim or [Boundary edges/Crossing/mOde/Project]: ");
    h.proc.submit_line("B");
    REQUIRE(h.out.lines.back() == "Select boundary edges ...");
    REQUIRE(h.out.prompt == "Select objects or <select all>: ");
    h.proc.submit_line("");
    h.proc.submit_line("O");
    REQUIRE(h.out.prompt == "Enter an extend mode option [Quick/Standard] <Quick>: ");
    h.proc.submit_line("");
    h.proc.submit_line("R"); // no eRase in EXTEND: not a point either
    REQUIRE(h.last<EraseSelectionCommand>() == nullptr);
    h.proc.cancel();
}

TEST_CASE("TRIMEXTENDMODE, EDGEMODE and PROJMODE at the command line") {
    DefaultTrimSettings defaults;
    H h;
    h.proc.submit_line("TRIMEXTENDMODE");
    REQUIRE(h.out.prompt == "Enter new value for TRIMEXTENDMODE <1>: ");
    h.proc.submit_line("2");
    REQUIRE(h.out.lines.back() == "Requires an integer between 0 and 1.");
    h.proc.submit_line("0");
    REQUIRE(TrimCommand::s_mode_ == 0);
    h.proc.submit_line("EDGEMODE");
    h.proc.submit_line("1");
    REQUIRE(TrimCommand::s_edgemode_ == 1);
    h.proc.submit_line("PROJMODE");
    REQUIRE(h.out.prompt == "Enter new value for PROJMODE <1>: ");
    h.proc.submit_line("0");
    REQUIRE(TrimCommand::s_projmode_ == 0);
    h.proc.submit_line("TRIM");
    REQUIRE(h.out.any_contains("Current settings: Projection=None, Edge=Extend, Mode=Standard"));
    h.proc.cancel();
}

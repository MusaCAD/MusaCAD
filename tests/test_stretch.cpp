// STRETCH (issue #24), as AutoCAD does it.
//
// The selection decides everything. A crossing window records which vertices the user
// "caught"; STRETCH then moves only those, and an object that was selected any other way
// (a pick, an ordinary window, or fully enclosed) moves whole. The engine remembers the
// crossing windows that built the selection, so the command supplies only a
// displacement -- and objects selected BEFORE the command (noun-verb) stretch correctly.
//
// The live rubber band is built by the same function as the commit
// (stretched_commands), previewed on a scratch store: what is shown is what lands.

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
#include "musacad/core/dimension.hpp"
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

bool has_vertex_near(const std::vector<Vec2>& v, Vec2 p, double tol) {
    return std::any_of(v.begin(), v.end(), [&](const Vec2& q) { return length(q - p) <= tol; });
}
bool has_vertex_near(const RenderSnapshot& s, Vec2 p, double tol) {
    return has_vertex_near(s.line_vertices, p, tol);
}
double max_x(const RenderSnapshot& s) {
    double m = -1e9;
    for (const Vec2& v : s.line_vertices) {
        m = std::max(m, v.x);
    }
    return m;
}
double min_x(const RenderSnapshot& s) {
    double m = 1e9;
    for (const Vec2& v : s.line_vertices) {
        m = std::min(m, v.x);
    }
    return m;
}

/// A crossing window as the viewport submits one from a right-to-left drag at a
/// command's "Select objects:" prompt.
SelectWindowCommand crossing(Vec2 mn, Vec2 mx, bool additive = false) {
    return SelectWindowCommand{mn, mx, /*crossing=*/true, additive, /*announce=*/true};
}

bool stretched_status(const RenderSnapshot& s) {
    return s.status.find("Stretched") != std::string::npos;
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

// ---------------------------------------------------------------------------
// AutoCAD's rule: crossed => only the caught vertices move
// ---------------------------------------------------------------------------

TEST_CASE("#24: a crossing window moves only the endpoint it caught") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    engine.submit(crossing({90, -10}, {110, 10})); // catches the right end only
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(StretchSelectionCommand{{20, 0}, 2});
    REQUIRE(wait_until(engine, stretched_status));
    engine.consume_snapshot();
    REQUIRE(min_x(engine.snapshot()) == Approx(0.0));   // anchored end stayed
    REQUIRE(max_x(engine.snapshot()) == Approx(120.0)); // caught end moved
    engine.stop();
}

TEST_CASE("STRETCH: a crossing polygon or lasso catches vertices as a crossing window does") {
    // The default PICKDRAG drag is a lasso, which reaches the engine as a crossing
    // polygon: the end inside it moves, the other stays.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectPolygonCommand{{{90, -10}, {110, -5}, {105, 10}, {92, 8}}, /*crossing=*/true, false, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(StretchSelectionCommand{{20, 0}, 2});
    REQUIRE(wait_until(engine, stretched_status));
    engine.consume_snapshot();
    REQUIRE(min_x(engine.snapshot()) == Approx(0.0));
    REQUIRE(max_x(engine.snapshot()) == Approx(120.0));
    engine.stop();
}

TEST_CASE("STRETCH takes C and CP at Select objects") {
    ProcHarness h;
    h.proc.submit_line("STRETCH");
    h.proc.submit_line("CP");
    h.proc.submit_line("90,-10");
    h.proc.submit_line("110,-10");
    h.proc.submit_line("100,10");
    h.proc.submit_line("");
    const auto* poly = h.last<SelectPolygonCommand>();
    REQUIRE(poly != nullptr);
    REQUIRE(poly->crossing);
    REQUIRE(poly->points.size() == 3);
    h.proc.submit_line("C");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,10");
    const auto* win = h.last<SelectWindowCommand>();
    REQUIRE(win != nullptr);
    REQUIRE(win->crossing);
}

TEST_CASE("#24: an object fully enclosed by the crossing window moves whole") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(crossing({-5, -5}, {15, 5}));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(StretchSelectionCommand{{0, 30}, 2});
    REQUIRE(wait_until(engine, stretched_status));
    engine.consume_snapshot();
    for (const Vec2& v : engine.snapshot().line_vertices) {
        REQUIRE(v.y == Approx(30.0)); // both endpoints moved
    }
    engine.stop();
}

TEST_CASE("#24: a line that merely passes through the window is left alone") {
    // Crossed, so selected -- but neither endpoint is inside, so nothing moves. AutoCAD
    // leaves it too, and the engine says why rather than claiming a stretch.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(crossing({40, -10}, {60, 10})); // the middle: crosses, catches no end
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(StretchSelectionCommand{{0, 30}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("no vertex of the selection lies inside") != std::string::npos;
    }));
    engine.consume_snapshot();
    for (const Vec2& v : engine.snapshot().line_vertices) {
        REQUIRE(v.y == Approx(0.0)); // untouched
    }
    engine.stop();
}

TEST_CASE("#24: an object selected by a PICK moves whole") {
    // AutoCAD: "objects that are selected individually are moved rather than stretched".
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectPickCommand{{50, 0}, 1.0, false, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(StretchSelectionCommand{{0, 30}, 2});
    REQUIRE(wait_until(engine, stretched_status));
    engine.consume_snapshot();
    for (const Vec2& v : engine.snapshot().line_vertices) {
        REQUIRE(v.y == Approx(30.0));
    }
    engine.stop();
}

TEST_CASE("#24: an object selected by an ordinary WINDOW moves whole") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectWindowCommand{{-5, -5}, {105, 5}, /*crossing=*/false, false, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(StretchSelectionCommand{{0, 30}, 2});
    REQUIRE(wait_until(engine, stretched_status));
    engine.consume_snapshot();
    for (const Vec2& v : engine.snapshot().line_vertices) {
        REQUIRE(v.y == Approx(30.0));
    }
    engine.stop();
}

TEST_CASE("#24: a crossing window plus an added pick: one stretches, the other moves") {
    // The selection accumulates at "Select objects:". The line caught by the window
    // stretches its caught end; the line added by a pick moves whole.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    engine.submit(AddLineCommand{{0, 50}, {100, 50}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    engine.submit(crossing({90, -10}, {110, 10}));        // catches line 1's right end
    engine.submit(SelectPickCommand{{50, 50}, 1.0, true, true}); // adds line 2
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(StretchSelectionCommand{{20, 0}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Stretched 2 objects.") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(has_vertex_near(s, {0, 0}, 1e-9));    // line 1: left end anchored
    REQUIRE(has_vertex_near(s, {120, 0}, 1e-9));  //         right end moved
    REQUIRE(has_vertex_near(s, {20, 50}, 1e-9));  // line 2: moved whole
    REQUIRE(has_vertex_near(s, {120, 50}, 1e-9));
    engine.stop();
}

TEST_CASE("#24: replacing the selection forgets the crossing window") {
    // A crossing window is a record about ONE selection. A fresh (non-additive) pick
    // replaces the selection, and the old window must not decide anything about it.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(crossing({90, -10}, {110, 10}));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(SelectPickCommand{{50, 0}, 1.0, false, false}); // replaces it
    engine.submit(StretchSelectionCommand{{0, 30}, 2});
    REQUIRE(wait_until(engine, stretched_status));
    engine.consume_snapshot();
    for (const Vec2& v : engine.snapshot().line_vertices) {
        REQUIRE(v.y == Approx(30.0)); // moved whole, not stretched by a stale window
    }
    engine.stop();
}

TEST_CASE("#24: nothing selected, and the engine says so") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(StretchSelectionCommand{{5, 5}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Nothing selected to stretch.") != std::string::npos;
    }));
    engine.stop();
}

// ---------------------------------------------------------------------------
// Per-kind rules
// ---------------------------------------------------------------------------

TEST_CASE("#24: a circle moves when its centre is caught and is never deformed") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 10.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    const std::size_t before = engine.snapshot().line_vertices.size();

    // The window must actually CROSS the circle to select it (a box floating inside
    // the rim touches nothing); this one crosses the rim at x=10 and contains the centre.
    engine.submit(crossing({-1, -1}, {12, 1}));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(StretchSelectionCommand{{50, 0}, 2});
    REQUIRE(wait_until(engine, stretched_status));
    engine.consume_snapshot();
    const RenderSnapshot& s2 = engine.snapshot();
    REQUIRE(s2.line_vertices.size() == before); // same tessellation: still that circle
    double cx = 0.0;
    for (const Vec2& v : s2.line_vertices) {
        cx += v.x;
    }
    cx /= static_cast<double>(s2.line_vertices.size());
    REQUIRE(cx == Approx(50.0).margin(0.5));
    engine.stop();
}

TEST_CASE("#24: a circle caught only by its rim stays put") {
    // Circles cannot be stretched (AutoCAD); the centre is the only stretch point.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 10.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(crossing({8, -3}, {14, 3})); // crosses the rim; centre outside
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(StretchSelectionCommand{{50, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("no vertex of the selection lies inside") != std::string::npos;
    }));
    engine.stop();
}

TEST_CASE("#24: an arc endpoint stretches with the arc's height above its chord preserved") {
    // Upper semicircle, r=50, from (50,0) round to (-50,0); its midpoint (0,50) sits
    // 50 above the chord. Drag the (50,0) end +50 in x. The chord becomes 150 long, the
    // sagitta stays 50, so r' = (75^2 + 50^2) / 100 = 81.25 and the new apex is at
    // (25, 50): the arc flattens as its chord grows instead of swinging about a fixed
    // centre -- AutoCAD's rule.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddArcCommand{{0, 0}, 50.0, 0.0, kPi, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(crossing({40, -10}, {60, 10})); // catches the (50,0) end only
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(StretchSelectionCommand{{50, 0}, 2});
    REQUIRE(wait_until(engine, stretched_status));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(has_vertex_near(s, {100, 0}, 0.6));  // the caught end moved
    REQUIRE(has_vertex_near(s, {-50, 0}, 0.6));  // the other end stayed
    // The apex is the arc's highest point; read it as the extreme rather than hunting
    // for a tessellation vertex that happens to land on it.
    double top = -1e9;
    double top_x = 0.0;
    for (const Vec2& v : s.line_vertices) {
        if (v.y > top) {
            top = v.y;
            top_x = v.x;
        }
    }
    REQUIRE(top == Approx(50.0).margin(1.0));   // the sagitta did not change
    REQUIRE(top_x == Approx(25.0).margin(6.0)); // ...and it sits over the new chord's middle
    engine.stop();
}

TEST_CASE("#24: an arc with both ends caught moves whole") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddArcCommand{{0, 0}, 50.0, 0.0, kPi, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(crossing({-60, -10}, {60, 60}));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(StretchSelectionCommand{{0, 100}, 2});
    REQUIRE(wait_until(engine, stretched_status));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(has_vertex_near(s, {50, 100}, 0.6));
    REQUIRE(has_vertex_near(s, {-50, 100}, 0.6));
    double top = -1e9;
    for (const Vec2& v : s.line_vertices) {
        top = std::max(top, v.y);
    }
    REQUIRE(top == Approx(150.0).margin(1.0)); // same shape, translated
    engine.stop();
}

TEST_CASE("#24: a stretched feature's dimension follows it (def points move)") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear),
                                      {0, 0}, {100, 0}, {50, -10}, 0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 10; }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().bounds_max.x == Approx(100.0).margin(2.0));

    engine.submit(crossing({90, -20}, {130, 20})); // right end of BOTH line and dim
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(StretchSelectionCommand{{25, 0}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Stretched 2") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().bounds_max.x == Approx(125.0).margin(2.0));
    engine.stop();
}

TEST_CASE("#24: half a rectangle stretches, the other half stays -- end to end") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {100, 0}, {100, 50}, {0, 50}}, true, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(crossing({50, -10}, {130, 60}));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(StretchSelectionCommand{{30, 0}, 2});
    REQUIRE(wait_until(engine, stretched_status));
    engine.consume_snapshot();
    double miny = 1e9;
    double maxy = -1e9;
    for (const Vec2& v : engine.snapshot().line_vertices) {
        miny = std::min(miny, v.y);
        maxy = std::max(maxy, v.y);
    }
    REQUIRE(min_x(engine.snapshot()) == Approx(0.0));   // un-windowed half stayed
    REQUIRE(max_x(engine.snapshot()) == Approx(130.0)); // windowed half moved
    REQUIRE(miny == Approx(0.0));
    REQUIRE(maxy == Approx(50.0));
    engine.stop();
}

TEST_CASE("#24: stretch is ONE undo group -- Ctrl+Z restores everything") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    engine.submit(AddLineCommand{{0, 10}, {100, 10}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    engine.submit(crossing({90, -20}, {130, 20}));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(StretchSelectionCommand{{30, 0}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Stretched 2") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(max_x(engine.snapshot()) == Approx(130.0));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.line_vertices.size() == 4 && max_x(s) < 100.5;
    }));
    engine.stop();
}

// ---------------------------------------------------------------------------
// The live rubber band and the "N found" echo
// ---------------------------------------------------------------------------

TEST_CASE("#24: the live preview shows the stretched shape while the store is untouched") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(crossing({90, -10}, {110, 10}));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    const std::uint64_t gv = engine.snapshot().geometry_version;

    engine.submit(StretchPreviewCommand{{30, 0}, true});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_vertex_near(s.grip_preview_segments, {130, 0}, 1e-9);
    }));
    {
        const RenderSnapshot& s = engine.snapshot();
        REQUIRE(has_vertex_near(s.grip_preview_segments, {0, 0}, 1e-9)); // anchored end
        REQUIRE(s.geometry_version == gv);              // zero churn in the real store
        REQUIRE(max_x(s) == Approx(100.0));             // the drawing itself is unchanged
    }
    // The cursor moves on: the band follows it.
    engine.submit(StretchPreviewCommand{{-40, 0}, true});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_vertex_near(s.grip_preview_segments, {60, 0}, 1e-9);
    }));
    // Cancel: the band goes, nothing changed.
    engine.submit(StretchPreviewCommand{{}, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grip_preview_segments.empty(); }));
    engine.consume_snapshot();
    REQUIRE(max_x(engine.snapshot()) == Approx(100.0));
    engine.stop();
}

TEST_CASE("#24: the commit ends the preview") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(crossing({90, -10}, {110, 10}));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(StretchPreviewCommand{{30, 0}, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.grip_preview_segments.empty(); }));
    engine.submit(StretchSelectionCommand{{30, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return stretched_status(s) && s.grip_preview_segments.empty() && max_x(s) > 129.0;
    }));
    engine.stop();
}

TEST_CASE("#24: selecting at a command prompt echoes AutoCAD's N found") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    engine.submit(AddLineCommand{{0, 50}, {100, 50}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    engine.submit(crossing({90, -10}, {110, 10}));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 found."; }));
    engine.submit(SelectPickCommand{{50, 50}, 1.0, true, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 found, 2 total."; }));
    engine.submit(crossing({500, 500}, {600, 600}, true)); // empty window: still honest
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "0 found, 2 total."; }));
    engine.stop();
}

// ---------------------------------------------------------------------------
// The command, prompt for prompt
// ---------------------------------------------------------------------------

TEST_CASE("#24: verb-noun -- Select objects, Enter, base point, second point") {
    ProcHarness h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("S");
    REQUIRE(h.proc.has_active_command());
    REQUIRE(h.proc.in_selection_phase()); // the viewport now runs selection gestures
    h.proc.set_selection_count(1);        // ...which the viewport's drags produce
    h.proc.submit_line("");               // Enter (or right-click) ends the selection
    REQUIRE(!h.proc.in_selection_phase());
    REQUIRE(h.cmds.empty());              // nothing fired yet
    h.proc.submit_line("100,0");          // base point
    REQUIRE(h.proc.preview().live_stretch); // the band is live from here
    REQUIRE(h.proc.preview().points.at(0) == Vec2{100, 0});
    h.proc.submit_line("130,0");          // second point
    const auto* st = h.last<StretchSelectionCommand>();
    REQUIRE(st != nullptr);
    REQUIRE(st->delta == Vec2{30, 0});
    REQUIRE(!h.proc.has_active_command());
    REQUIRE(!h.proc.preview().live_stretch);
}

TEST_CASE("#24: noun-verb -- a pre-selected set skips Select objects") {
    ProcHarness h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("STRETCH");
    REQUIRE(!h.proc.in_selection_phase()); // straight to the base point
    h.proc.submit_line("0,0");
    h.proc.submit_line("0,25");
    const auto* st = h.last<StretchSelectionCommand>();
    REQUIRE(st != nullptr);
    REQUIRE(st->delta == Vec2{0, 25});
}

TEST_CASE("#24: Enter with nothing selected ends the command without stretching") {
    ProcHarness h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("S");
    h.proc.submit_line("");
    REQUIRE(!h.proc.has_active_command());
    REQUIRE(h.last<StretchSelectionCommand>() == nullptr);
}

TEST_CASE("#24: ALL at Select objects selects everything and keeps selecting") {
    ProcHarness h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("S");
    h.proc.submit_line("ALL");
    REQUIRE(h.last<SelectAllCommand>() != nullptr);
    REQUIRE(h.proc.in_selection_phase()); // Enter still has to finish it, as in AutoCAD
}

TEST_CASE("#24: a typed coordinate at Select objects is a pick there") {
    ProcHarness h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("S");
    h.proc.submit_line("50,0");
    const auto* pk = h.last<SelectPickCommand>();
    REQUIRE(pk != nullptr);
    REQUIRE(pk->world == Vec2{50, 0});
    REQUIRE(pk->additive);
    REQUIRE(pk->announce);
    REQUIRE(h.proc.in_selection_phase());
}

TEST_CASE("#24: the Displacement option takes the vector directly") {
    for (const char* how : {"D", "DISPLACEMENT", ""}) {
        ProcHarness h;
        h.proc.set_selection_count(1);
        h.proc.submit_line("S");
        h.proc.submit_line(how); // D, or Enter for the <Displacement> default
        h.proc.submit_line("30,-5");
        INFO(how);
        const auto* st = h.last<StretchSelectionCommand>();
        REQUIRE(st != nullptr);
        REQUIRE(st->delta == Vec2{30, -5});
    }
}

TEST_CASE("#24: Enter at the second point uses the first point as the displacement") {
    ProcHarness h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("S");
    h.proc.submit_line("30,0"); // base point
    h.proc.submit_line("");     // <use first point as displacement>
    const auto* st = h.last<StretchSelectionCommand>();
    REQUIRE(st != nullptr);
    REQUIRE(st->delta == Vec2{30, 0});
}

TEST_CASE("#24: ORTHO constrains the second point, as it does in the video") {
    ProcHarness h;
    h.proc.set_selection_count(1);
    h.proc.set_ortho(true);
    h.proc.submit_line("S");
    h.proc.submit_line("100,0");                        // base
    h.proc.pick_point(Vec2{130.0, 7.0}, std::nullopt);  // a slightly-off cursor
    const auto* st = h.last<StretchSelectionCommand>();
    REQUIRE(st != nullptr);
    REQUIRE(st->delta == Vec2{30, 0}); // snapped onto the axis
}

TEST_CASE("#24: Esc during the second point drops the rubber band") {
    ProcHarness h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("S");
    h.proc.submit_line("100,0");
    REQUIRE(h.proc.preview().live_stretch);
    h.proc.cancel();
    REQUIRE(!h.proc.has_active_command());
    REQUIRE(!h.proc.preview().live_stretch);
    const auto* pv = h.last<StretchPreviewCommand>();
    REQUIRE(pv != nullptr);
    REQUIRE(!pv->active); // the engine is told to clear the preview
    REQUIRE(h.last<StretchSelectionCommand>() == nullptr);
}

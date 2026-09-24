// Part B intersection-dependent commands: EXTEND, TRIM (by a curve), FILLET,
// CHAMFER. Each is one undoable group.

#include <chrono>
#include <cmath>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad::core;

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
    const auto eq = [&](Vec2 p, Vec2 q) {
        return std::abs(p.x - q.x) < eps && std::abs(p.y - q.y) < eps;
    };
    for (std::size_t i = 0; i + 1 < s.line_vertices.size(); i += 2) {
        const Vec2 p = s.line_vertices[i];
        const Vec2 q = s.line_vertices[i + 1];
        if ((eq(p, a) && eq(q, b)) || (eq(p, b) && eq(q, a))) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("EXTEND lengthens a line to a boundary edge; undo restores") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {5, 0}, 1});    // line to extend
    engine.submit(AddLineCommand{{10, -5}, {10, 5}, 2}); // boundary at x=10
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));

    engine.submit(ExtendPickCommand{{4.5, 0}, 1.0, 10}); // pick near the (5,0) end
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {5, 0}); }));
    engine.stop();
}

TEST_CASE("TRIM a line at its intersections with a circle (analytic line/circle)") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {20, 0}, 1});
    engine.submit(AddCircleCommand{{10, 0}, 3.0, 2}); // crosses at (7,0) and (13,0)
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() >= 2; }));

    engine.submit(TrimPickCommand{{10, 0}, 1.0, 10}); // pick the middle span
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {0, 0}, {7, 0}) && has_segment(s, {13, 0}, {20, 0});
    }));
    REQUIRE_FALSE(has_segment(engine.snapshot(), {0, 0}, {20, 0}));

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {20, 0}); }));
    engine.stop();
}

TEST_CASE("FILLET radius 0 trims/extends two lines to a clean corner") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {8, 0}, 1});    // horizontal (y=0)
    engine.submit(AddLineCommand{{12, 0}, {12, 10}, 2}); // vertical (x=12)
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));

    // Corner is (12,0); keep the picked ends.
    engine.submit(FilletPickCommand{{1, 0}, {12, 9}, 0.0, 1.0, 10});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {0, 0}, {12, 0}) && has_segment(s, {12, 10}, {12, 0});
    }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {0, 0}, {8, 0}) && has_segment(s, {12, 0}, {12, 10});
    }));
    engine.stop();
}

TEST_CASE("FILLET radius > 0 rounds the corner with a tangent arc") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1}); // y=0
    engine.submit(AddLineCommand{{0, 0}, {0, 10}, 2}); // x=0  (corner at origin, 90 deg)
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));

    engine.submit(FilletPickCommand{{10, 0}, {0, 10}, 2.0, 1.0, 10});
    // Lines trimmed to the tangent points; an arc joins them (extra tessellated
    // vertices beyond the two trimmed lines).
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {10, 0}, {2, 0}) && has_segment(s, {0, 10}, {0, 2}) &&
               s.line_vertices.size() > 4;
    }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    engine.stop();
}

TEST_CASE("CHAMFER bevels the corner between two lines; undo restores") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1}); // y=0
    engine.submit(AddLineCommand{{0, 0}, {0, 10}, 2}); // x=0
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));

    engine.submit(ChamferPickCommand{{10, 0}, {0, 10}, 2.0, 3.0, 1.0, 10});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {10, 0}, {2, 0}) && has_segment(s, {0, 10}, {0, 3}) &&
               has_segment(s, {2, 0}, {0, 3});
    }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    engine.stop();
}

// ---------------------------------------------------------------------------
// TRIM / EXTEND must preserve the entity's own properties (issue #27).
//
// Both rebuild the entity as a fresh Add* command. Neither carried the original's
// EntityProps, so the pieces were stamped with the CURRENT layer -- trim a line drawn
// on "HIDDEN" while layer 0 is current and it silently changed layer, colour, linetype
// and lineweight. They are the same object, shortened, and must stay so.
// ---------------------------------------------------------------------------

TEST_CASE("#27: TRIM keeps the trimmed line's layer, not the current one") {
    GeometryEngine engine;
    engine.start();
    Layer red;
    red.name = "red";
    red.color = {255, 0, 0};
    engine.submit(AddLayerCommand{red}); // index 1
    engine.submit(SetCurrentLayerCommand{1});
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});    // the line to trim
    engine.submit(AddLineCommand{{50, -10}, {50, 10}, 2}); // the cutting edge
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));

    // The user moves on to layer 0 (white) before trimming -- the ordinary case.
    engine.submit(SetCurrentLayerCommand{0});
    engine.submit(TrimPickCommand{{80, 0}, 1.0, 3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Trimmed.") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(!engine.snapshot().line_batches.empty());
    for (const ColorBatch& b : engine.snapshot().line_batches) {
        REQUIRE(b.color == Rgb{255, 0, 0}); // still red: nothing jumped to layer 0
    }
    engine.stop();
}

TEST_CASE("#27: EXTEND keeps the extended line's layer, not the current one") {
    GeometryEngine engine;
    engine.start();
    Layer red;
    red.name = "red";
    red.color = {255, 0, 0};
    engine.submit(AddLayerCommand{red}); // index 1
    engine.submit(SetCurrentLayerCommand{1});
    engine.submit(AddLineCommand{{0, 0}, {50, 0}, 1});      // the line to extend
    engine.submit(AddLineCommand{{100, -10}, {100, 10}, 2}); // the boundary
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));

    engine.submit(SetCurrentLayerCommand{0});
    engine.submit(ExtendPickCommand{{45, 0}, 1.0, 3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Extended.") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(!engine.snapshot().line_batches.empty());
    for (const ColorBatch& b : engine.snapshot().line_batches) {
        REQUIRE(b.color == Rgb{255, 0, 0});
    }
    engine.stop();
}

// ---------------------------------------------------------------------------
// TRIM and EXTEND on CURVES (issue #27).
//
// The cutting/boundary side already handled curves; it was the *modified* entity that
// had to be a line. Arcs and circles now trim, and arcs extend. The circle case is the
// one with a convention to fix: a circle has no ends, so the crossings themselves bound
// the piece to remove, and what survives is a single arc.
// ---------------------------------------------------------------------------

TEST_CASE("#27: TRIM cuts an ARC back to a crossing line") {
    // A semicircle of radius 50 crossed by the vertical x=0 line at (0,50). Picking the
    // left half removes it, leaving the right quarter from (50,0) to (0,50).
    GeometryEngine engine;
    engine.start();
    engine.submit(AddArcCommand{{0, 0}, 50.0, 0.0, kPi, 1});
    engine.submit(AddLineCommand{{0, -60}, {0, 60}, 2}); // crosses the arc at (0,50)
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    engine.submit(TrimPickCommand{{-45.0, 20.0}, 3.0, 3}); // pick the left half of the arc
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Trimmed.") != std::string::npos;
    }));
    engine.consume_snapshot();
    // Nothing of the arc survives on the left; the right quarter does.
    bool any_left = false;
    bool any_right = false;
    for (const Vec2& v : engine.snapshot().line_vertices) {
        if (std::abs(length(v) - 50.0) < 0.5) { // on the arc, not the cutting line
            if (v.x < -1.0) {
                any_left = true;
            }
            if (v.x > 1.0) {
                any_right = true;
            }
        }
    }
    REQUIRE(!any_left);
    REQUIRE(any_right);
    engine.stop();
}

TEST_CASE("#27: TRIM turns a CIRCLE into an arc between two crossings") {
    // Two vertical cutters at x=+-30 cross the circle four times. Picking the top piece
    // removes the span between the two crossings that bracket it.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 50.0, 1});
    engine.submit(AddLineCommand{{-30, -60}, {-30, 60}, 2});
    engine.submit(AddLineCommand{{30, -60}, {30, 60}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    engine.submit(TrimPickCommand{{0.0, 50.0}, 3.0, 4}); // the top of the circle
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Trimmed.") != std::string::npos;
    }));
    engine.consume_snapshot();
    // The top of the circle is gone; the bottom is still there.
    bool top = false;
    bool bottom = false;
    for (const Vec2& v : engine.snapshot().line_vertices) {
        if (std::abs(length(v) - 50.0) < 0.5) {
            if (v.y > 45.0) {
                top = true;
            }
            if (v.y < -45.0) {
                bottom = true;
            }
        }
    }
    REQUIRE(!top);
    REQUIRE(bottom);
    engine.stop();
}

TEST_CASE("#27: a circle with only one crossing is refused, not half-trimmed") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 50.0, 1});
    engine.submit(AddLineCommand{{50, 0}, {90, 0}, 2}); // touches at one point only
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    const std::size_t before = engine.snapshot().line_vertices.size();

    engine.submit(TrimPickCommand{{0.0, 50.0}, 3.0, 3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("needs two crossing edges") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().line_vertices.size() == before); // untouched
    engine.stop();
}

TEST_CASE("#27: EXTEND grows an ARC round to a boundary") {
    // A quarter circle from (50,0) to (0,50), and a boundary line along x=-50. Extending
    // the (0,50) end sweeps it round to (-50,0).
    GeometryEngine engine;
    engine.start();
    engine.submit(AddArcCommand{{0, 0}, 50.0, 0.0, kPi / 2.0, 1});
    engine.submit(AddLineCommand{{-50, -10}, {-50, 60}, 2}); // tangent-ish boundary at (-50,0)
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    engine.submit(ExtendPickCommand{{0.0, 50.0}, 3.0, 3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Extended.") != std::string::npos;
    }));
    engine.consume_snapshot();
    bool reached = false;
    bool kept_start = false;
    for (const Vec2& v : engine.snapshot().line_vertices) {
        if (length(v - Vec2{-50, 0}) < 1.0) {
            reached = true;
        }
        if (length(v - Vec2{50, 0}) < 1.0) {
            kept_start = true;
        }
    }
    REQUIRE(reached);
    REQUIRE(kept_start); // the other end never moved
    engine.stop();
}

TEST_CASE("#27: EXTEND on an arc with nothing ahead says so") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddArcCommand{{0, 0}, 50.0, 0.0, kPi / 2.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(ExtendPickCommand{{0.0, 50.0}, 3.0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("no boundary ahead") != std::string::npos;
    }));
    engine.stop();
}

// ---------------------------------------------------------------------------
// #49: the corner operations keep the objects' properties, No trim only adds the
// arc / bevel, and [Polyline] treats every corner.
// ---------------------------------------------------------------------------
TEST_CASE("#49: FILLET and CHAMFER keep the layer of the lines they trim") {
    GeometryEngine engine;
    engine.start();
    Layer walls;
    walls.name = "walls";
    engine.submit(AddLayerCommand{walls});
    EntityProps on_walls;
    on_walls.layer = 1;
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1, on_walls});
    engine.submit(AddLineCommand{{10, 0}, {10, 10}, 1, on_walls});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4 && s.layers.size() == 2; }));
    engine.submit(FilletPickCommand{{5, 0}, {10, 5}, 2.0, 1.0, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Filleted."; }));
    REQUIRE(engine.snapshot().line_vertices.size() > 4); // two trimmed lines and the arc
    // Everything the fillet left -- both trimmed lines and the arc -- is on "walls":
    // freezing that layer leaves nothing drawn.
    Layer frozen = walls;
    frozen.frozen = true;
    engine.submit(SetLayerCommand{1, frozen});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    engine.stop();
}

TEST_CASE("#49: No trim keeps the lines and adds only the arc; [Polyline] rounds every corner") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{10, 0}, {10, 10}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    FilletPickCommand notrim{{5, 0}, {10, 5}, 2.0, 1.0, 10};
    notrim.trim = false;
    engine.submit(notrim);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Filleted."; }));
    REQUIRE(has_segment(engine.snapshot(), {0, 0}, {10, 0}));   // untouched
    REQUIRE(has_segment(engine.snapshot(), {10, 0}, {10, 10})); // untouched
    REQUIRE(engine.snapshot().line_vertices.size() > 4);        // the arc was added
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));

    engine.submit(EraseCommand{EraseScope::All});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    engine.submit(AddPolylineCommand{{{0, 0}, {20, 0}, {20, 10}, {0, 10}}, true, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));
    engine.submit(FilletPolylineCommand{{10, 0}, 2.0, 1.0, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("4 lines were filleted", 0) == 0; }));
    // Four rounded corners: the straight runs are shorter by 2 at each end.
    REQUIRE(has_segment(engine.snapshot(), {2, 0}, {18, 0}));
    REQUIRE(has_segment(engine.snapshot(), {20, 2}, {20, 8}));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));
    engine.submit(ChamferPolylineCommand{{10, 0}, 3.0, 3.0, 1.0, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("4 lines were chamfered", 0) == 0; }));
    REQUIRE(has_segment(engine.snapshot(), {3, 0}, {17, 0}));
    REQUIRE(has_segment(engine.snapshot(), {17, 0}, {20, 3}));
    engine.stop();
}

TEST_CASE("#50: OFFSET Through, Erase source, the current layer, and Multiple from the last offset") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    OffsetPickCommand through{{5, 0}, 1.0, 0.0, {5, 3}, 2};
    through.through = true;
    engine.submit(through);
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 3}, {10, 3}); }));
    OffsetPickCommand erase{{5, 3}, 1.0, 2.0, {5, 9}, 3};
    erase.erase_source = true;
    engine.submit(erase);
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 5}, {10, 5}) && !has_segment(s, {0, 3}, {10, 3}); }));
    OffsetPickCommand multi{{5, 5}, 1.0, 2.0, {5, 9}, 4};
    multi.from_last = true; // the newest offset (y = 5) again, not the pick's object
    engine.submit(multi);
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 7}, {10, 7}); }));
    // Layer Current: the new line lands on the current layer, not the source's.
    Layer walls;
    walls.name = "walls";
    engine.submit(AddLayerCommand{walls});
    engine.submit(SetCurrentLayerCommand{1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.current_layer == 1; }));
    OffsetPickCommand cur{{5, 7}, 1.0, 2.0, {5, 20}, 5};
    cur.to_current_layer = true;
    engine.submit(cur);
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 9}, {10, 9}); }));
    Layer frozen = walls;
    frozen.frozen = true;
    engine.submit(SetLayerCommand{1, frozen});
    REQUIRE(wait_until(engine, [](const auto& s) { return !has_segment(s, {0, 9}, {10, 9}) && has_segment(s, {0, 7}, {10, 7}); }));
    engine.stop();
}

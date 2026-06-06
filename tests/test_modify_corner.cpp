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

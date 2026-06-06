// Part A transform commands: ROTATE, SCALE, ARRAY (rectangular + polar), each on
// the selection and undoable as one group.

#include <chrono>
#include <cmath>
#include <thread>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("ROTATE turns the selection about a base point; undo restores") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    // 90 degrees about the origin: (10,0) -> (0,10).
    engine.submit(RotateSelectionCommand{{0, 0}, std::atan2(1.0, 0.0), 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {0, 10}); }));

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));
    engine.stop();
}

TEST_CASE("SCALE resizes the selection about a base point; undo restores") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(ScaleSelectionCommand{{0, 0}, 2.0, 10}); // (10,0) -> (20,0)
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {20, 0}); }));

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));
    engine.stop();
}

TEST_CASE("ARRAY rectangular replicates the selection on a grid; undo restores") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {1, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    // 2 rows x 3 cols, spacing 10 -> 6 lines total (original + 5 copies).
    engine.submit(ArrayRectCommand{2, 3, 10.0, 10.0, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 12; }));
    REQUIRE(has_segment(engine.snapshot(), {0, 0}, {1, 0}));     // original
    REQUIRE(has_segment(engine.snapshot(), {20, 10}, {21, 10})); // row 1, col 2

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.stop();
}

TEST_CASE("ARRAY polar replicates the selection around a centre; undo restores") {
    GeometryEngine engine;
    engine.start();
    // A short line near (10,0); polar array of 4 around the origin (full circle).
    engine.submit(AddLineCommand{{10, 0}, {11, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(ArrayPolarCommand{{0, 0}, 4, std::atan2(0.0, -1.0) * 2.0, true, 10});
    // 4 items total = original + 3 copies -> 8 vertices. The 90-degree copy maps
    // (10,0)->(0,10), (11,0)->(0,11).
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));
    REQUIRE(has_segment(engine.snapshot(), {0, 10}, {0, 11}, 1e-6));

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.stop();
}

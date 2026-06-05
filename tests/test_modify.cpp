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
// Is there a segment with the given (unordered) endpoints?
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
} // namespace

TEST_CASE("MOVE translates the selection and is undoable") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(MoveSelectionCommand{{0, 5}, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 5}, {10, 5}); }));

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));
    engine.stop();
}

TEST_CASE("COPY leaves the original and adds a copy; undo removes the copy") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(CopySelectionCommand{{0, 5}, 10});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {0, 0}, {10, 0}) && has_segment(s, {0, 5}, {10, 5});
    }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.stop();
}

TEST_CASE("MIRROR reflects the selection across a line") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {4, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    // Mirror across the vertical line x = 5 -> (0,0)->(4,0) becomes (10,0)->(6,0).
    engine.submit(MirrorSelectionCommand{{5, 0}, {5, 1}, false, 10});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {0, 0}, {4, 0}) && has_segment(s, {10, 0}, {6, 0});
    }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.stop();
}

TEST_CASE("OFFSET creates a parallel line on the picked side") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    // Offset distance 2 toward +y.
    engine.submit(OffsetPickCommand{{5, 0}, 1.0, 2.0, {5, 5}, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 2}, {10, 2}); }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.stop();
}

TEST_CASE("TRIM cuts a line at its nearest intersection") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});  // horizontal
    engine.submit(AddLineCommand{{5, -5}, {5, 5}, 2});  // vertical cutter at x=5
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));

    // Pick the horizontal line left of the crossing -> removes [0,5], leaves [5,10].
    engine.submit(TrimPickCommand{{2, 0}, 1.0, 10});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {5, 0}, {10, 0}) && !has_segment(s, {0, 0}, {10, 0});
    }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));
    engine.stop();
}

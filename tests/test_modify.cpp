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

// The closing-the-workflow case: four separate edges -> JOIN -> one CLOSED polyline,
// which then OFFSETs uniformly (Item 1). If JOIN failed (still four lines), a single
// offset pick could not produce all four inner edges.
TEST_CASE("JOIN merges four lines into a closed polyline that OFFSETs uniformly") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{10, 0}, {10, 10}, 2});
    engine.submit(AddLineCommand{{10, 10}, {0, 10}, 3});
    engine.submit(AddLineCommand{{0, 10}, {0, 0}, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));

    // Pick each edge (midpoints); the chain loops -> a single closed polyline (selected).
    engine.submit(JoinPickCommand{{{5, 0}, {10, 5}, {5, 10}, {0, 5}}, 1.0, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selected_line_vertices.size() == 8; }));

    // Offset that ONE closed polyline inward by 2 -> a uniform inner rectangle.
    engine.submit(OffsetPickCommand{{5, 0}, 1.0, 2.0, {5, 5}, 11});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {2, 2}, {8, 2}) && has_segment(s, {8, 2}, {8, 8}) &&
               has_segment(s, {8, 8}, {2, 8}) && has_segment(s, {2, 8}, {2, 2});
    }));
    // The outer (source) polyline is untouched by OFFSET (create-only).
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));
    engine.stop();
}

// JOIN connects entities sharing endpoints and skips disjoint ones; the joined open
// polyline then offsets with a properly re-mitered corner.
TEST_CASE("JOIN connects sharing lines and skips a disjoint one") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});    // source
    engine.submit(AddLineCommand{{10, 0}, {10, 10}, 2});  // shares (10,0)
    engine.submit(AddLineCommand{{20, 20}, {30, 20}, 3}); // disjoint -> skipped
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 6; }));

    engine.submit(JoinPickCommand{{{5, 0}, {10, 5}, {25, 20}}, 1.0, 10});
    // Two of three joined into one open polyline (2 segments -> 4 selected vertices).
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selected_line_vertices.size() == 4; }));

    // Offsetting the joined polyline re-miters the corner (a single line could not yield
    // the (8,2) corner); the disjoint line is left alone.
    engine.submit(OffsetPickCommand{{5, 0}, 1.0, 2.0, {5, 5}, 11});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {0, 2}, {8, 2}) && has_segment(s, {8, 2}, {8, 10}) &&
               has_segment(s, {20, 20}, {30, 20});
    }));
    engine.stop();
}

// Rollover (hover) highlight: the geometry thread computes the entity under the
// cursor's pick-box (same pick query as a click) and publishes it in the
// snapshot. Visual only -- it never changes the selection set.

#include <chrono>
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
// SetCursor with snapping off (mask 0) so only the hover-pick is exercised.
SetCursorCommand cursor_at(Vec2 w, double r) { return SetCursorCommand{w, r, false, 0u, {}, false}; }
} // namespace

TEST_CASE("Hover: the entity under the cursor is published, geometry untouched") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{0, 50}, {10, 50}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));

    // Hover over the y=0 line.
    engine.submit(cursor_at({5, 0}, 2.0));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.has_hover; }));
    {
        const auto& s = engine.snapshot();
        REQUIRE(s.hover.kind == EntityKind::Line);
        REQUIRE(s.hover_line_vertices.size() == 2);
        REQUIRE(s.hover_line_vertices[0].y == 0.0);
        // Hover does not select.
        REQUIRE(s.selection.empty());
        // Geometry is unchanged.
        REQUIRE(s.line_vertices.size() == 4);
    }

    // Move to empty space -> no hover.
    engine.submit(cursor_at({500, 500}, 2.0));
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.has_hover; }));

    engine.stop();
}

TEST_CASE("Hover does not highlight an already-selected entity") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    engine.submit(SelectPickCommand{{5, 0}, 2.0, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    // Hover over the selected line -> no separate hover highlight.
    engine.submit(cursor_at({5, 0}, 2.0));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    engine.consume_snapshot();
    REQUIRE_FALSE(engine.snapshot().has_hover);

    engine.stop();
}

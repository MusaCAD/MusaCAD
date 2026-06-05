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
std::size_t sel_count(GeometryEngine& e) {
    e.consume_snapshot();
    return e.snapshot().selection.size();
}
} // namespace

TEST_CASE("Selection: single pick, window vs crossing, shift-add, clear, all") {
    GeometryEngine engine;
    engine.start();
    // Three horizontal lines at y = 0, 50, 100.
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{0, 50}, {10, 50}, 2});
    engine.submit(AddLineCommand{{0, 100}, {10, 100}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 6; }));

    // Single-click pick near the middle line.
    engine.submit(SelectPickCommand{{5, 50}, 3.0, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    REQUIRE(engine.snapshot().selection[0].kind == EntityKind::Line);

    // Window select (fully enclosed): a box around only the y=0 line.
    engine.submit(SelectWindowCommand{{-1, -1}, {11, 1}, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    // Crossing select: a small box that merely touches the y=50 line.
    engine.submit(SelectWindowCommand{{4, 49}, {6, 51}, true, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    // The same small box as a WINDOW selects nothing (line not fully enclosed).
    engine.submit(SelectWindowCommand{{4, 49}, {6, 51}, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.empty(); }));

    // Shift-add: pick two different lines.
    engine.submit(SelectPickCommand{{5, 0}, 3.0, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(SelectPickCommand{{5, 100}, 3.0, true}); // additive
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));

    // Clear.
    engine.submit(ClearSelectionCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.empty(); }));

    // Select all.
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 3; }));

    engine.stop();
}

TEST_CASE("Selection: window crossing direction distinguishes the two modes on a diagonal") {
    GeometryEngine engine;
    engine.start();
    // A long line from (0,0) to (100,100) -- a box in the middle crosses it.
    engine.submit(AddLineCommand{{0, 0}, {100, 100}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    // Crossing box in the middle: touches the line -> selected.
    engine.submit(SelectWindowCommand{{40, 40}, {60, 60}, true, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    // Same box as window: line not fully enclosed -> not selected.
    engine.submit(SelectWindowCommand{{40, 40}, {60, 60}, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.empty(); }));

    engine.stop();
}

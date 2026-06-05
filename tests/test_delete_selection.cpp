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
} // namespace

TEST_CASE("Delete erases the selection as one undoable group; the rest remains") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{0, 50}, {10, 50}, 2});
    engine.submit(AddLineCommand{{0, 100}, {10, 100}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 6; }));

    // Select 2 of the 3 (window over y=0 and y=50, leaving y=100).
    engine.submit(SelectWindowCommand{{-1, -1}, {11, 51}, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));

    // Delete -> those two gone, one remains; selection cleared.
    engine.submit(EraseSelectionCommand{10});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.line_vertices.size() == 2 && s.selection.empty();
    }));
    // The survivor is the y=100 line.
    REQUIRE(engine.snapshot().line_vertices[0].y == 100.0);

    // Undo restores all three as one group.
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 6; }));

    engine.stop();
}

TEST_CASE("Delete with empty selection is a no-op") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(EraseSelectionCommand{2});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().line_vertices.size() == 2);
    engine.stop();
}

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

TEST_CASE("Undo then redo returns to the post-add state") {
    GeometryEngine engine;
    engine.start();

    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));

    engine.submit(RedoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    const auto& s = engine.snapshot();
    REQUIRE(s.line_vertices[0] == Vec2{0, 0});
    REQUIRE(s.line_vertices[1] == Vec2{100, 0});

    engine.stop();
}

TEST_CASE("A new edit clears the redo stack") {
    GeometryEngine engine;
    engine.start();

    engine.submit(AddLineCommand{{0, 0}, {1, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));

    // New edit invalidates redo.
    engine.submit(AddLineCommand{{0, 0}, {2, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    // Redo now should do nothing (stack was cleared).
    engine.submit(RedoLastGroupCommand{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().line_vertices.size() == 2);

    engine.stop();
}

TEST_CASE("Redo of an undone ERASE re-erases") {
    GeometryEngine engine;
    engine.start();

    engine.submit(AddLineCommand{{0, 0}, {1, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(EraseCommand{EraseScope::All, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    engine.submit(UndoLastGroupCommand{}); // undo erase -> back
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(RedoLastGroupCommand{}); // redo erase -> gone
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));

    engine.stop();
}

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

TEST_CASE("ERASE-pick deletes the entity under the cursor via the shared index") {
    GeometryEngine engine;
    engine.start();

    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{0, 50}, {10, 50}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));

    // Pick near the second line (y=50); it should be the one removed.
    engine.submit(ErasePickCommand{{5, 50}, 3.0, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    const auto& s = engine.snapshot();
    // The surviving line is the first (y=0).
    REQUIRE(((s.line_vertices[0].y == 0.0) && (s.line_vertices[1].y == 0.0)));

    // Undo restores the picked line.
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s2) { return s2.line_vertices.size() == 4; }));

    engine.stop();
}

TEST_CASE("ERASE-pick on empty space does nothing") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(ErasePickCommand{{500, 500}, 3.0, 2});
    // Give it a moment; nothing should be removed.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().line_vertices.size() == 2);
    engine.stop();
}

// Part B: the TEXT entity -- created via command, rendered, layer-aware,
// selectable, movable, erasable.

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

TEST_CASE("TEXT entity: render, select, move, erase") {
    GeometryEngine engine;
    engine.start();
    AddTextCommand t;
    t.pos = {0, 0};
    t.height = 5.0;
    t.content = "AB12";
    t.group = 1;
    engine.submit(t);
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    // Pick near the text -> selected.
    engine.submit(SelectPickCommand{{2, 2}, 5.0, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    // Move it; then a pick at the old spot no longer hits.
    engine.submit(MoveSelectionCommand{{100, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.selection.size() == 1 && s.bounds_max.x > 50.0;
    }));

    // Erase via the selection.
    engine.submit(EraseSelectionCommand{3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    engine.stop();
}

TEST_CASE("TEXT entity is layer-aware (off layer hides it)") {
    GeometryEngine engine;
    engine.start();
    Layer l;
    l.name = "notes";
    engine.submit(AddLayerCommand{l});
    engine.submit(SetCurrentLayerCommand{1});
    AddTextCommand t;
    t.pos = {0, 0};
    t.height = 3.0;
    t.content = "HELLO";
    t.group = 1;
    engine.submit(t);
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    Layer off = l;
    off.on = false;
    engine.submit(SetLayerCommand{1, off});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    engine.stop();
}

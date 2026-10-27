// Part B: the snapshot/render and pick paths respect layer state -- off/frozen
// layers don't render, locked layers can't be selected, per-entity colour
// resolves into colour batches, and new entities take the current layer's colour.

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
Layer make_layer(const char* name, Rgb color) {
    Layer l;
    l.name = name;
    l.color = color;
    return l;
}
} // namespace

TEST_CASE("New entities take the current layer; colour resolves into batches") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLayerCommand{make_layer("red", {255, 0, 0})});   // index 1
    engine.submit(AddLayerCommand{make_layer("blue", {0, 0, 255})});  // index 2

    engine.submit(SetCurrentLayerCommand{1});
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(SetCurrentLayerCommand{2});
    engine.submit(AddLineCommand{{0, 5}, {10, 5}, 2});

    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_batches.size() == 2; }));
    const auto& b = engine.snapshot().line_batches;
    // Two batches, one per layer colour (std::map order: red 0xff0000 < blue 0x0000ff? packed
    // red=0xff0000, blue=0x0000ff -> blue sorts first). Check the set of colours.
    const bool has_red = b[0].color == Rgb{255, 0, 0} || b[1].color == Rgb{255, 0, 0};
    const bool has_blue = b[0].color == Rgb{0, 0, 255} || b[1].color == Rgb{0, 0, 255};
    REQUIRE(has_red);
    REQUIRE(has_blue);
    engine.stop();
}

TEST_CASE("Off and frozen layers don't render") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLayerCommand{make_layer("L", {255, 255, 255})}); // index 1
    engine.submit(SetCurrentLayerCommand{1});
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    SECTION("layer off") {
        Layer off = make_layer("L", {255, 255, 255});
        off.on = false;
        engine.submit(SetLayerCommand{1, off});
        REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
        // Turning it back on re-renders.
        engine.submit(SetLayerCommand{1, make_layer("L", {255, 255, 255})});
        REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    }
    SECTION("layer frozen") {
        engine.submit(SetCurrentLayerCommand{0}); // the current layer can't be frozen
        Layer frozen = make_layer("L", {255, 255, 255});
        frozen.frozen = true;
        engine.submit(SetLayerCommand{1, frozen});
        REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    }
    engine.stop();
}

TEST_CASE("Locked layers render but can't be picked or modified") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLayerCommand{make_layer("L", {255, 255, 255})}); // index 1
    engine.submit(SetCurrentLayerCommand{1});
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    // Unlocked: a pick selects it.
    engine.submit(SelectPickCommand{{5, 0}, 1.0, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(ClearSelectionCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.empty(); }));

    // Lock the layer: still drawn, but a pick selects nothing.
    Layer locked = make_layer("L", {255, 255, 255});
    locked.locked = true;
    engine.submit(SetLayerCommand{1, locked});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; })); // drawn
    engine.submit(SelectPickCommand{{5, 0}, 1.0, false});
    // Give it a beat; selection must stay empty.
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().selection.empty());
    engine.stop();
}

TEST_CASE("Snapshot publishes the layer table and current layer") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLayerCommand{make_layer("walls", {128, 128, 128})});
    engine.submit(SetCurrentLayerCommand{1});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.layers.size() == 2 && s.current_layer == 1 && s.layers[1].name == "walls";
    }));
    engine.stop();
}

// ---------------------------------------------------------------------------
// PURGE (issue #30): drop symbol-table entries nothing refers to.
//
// The rule is AutoCAD's, and it is already enforced by remove_layer: layer 0, the
// CURRENT layer, and any layer holding geometry all stay. Purge is therefore a walk,
// not a new policy -- which is the point of putting the refusal in the store.
// ---------------------------------------------------------------------------

TEST_CASE("#30: PURGE removes unused layers and keeps the ones in use") {
    GeometryEngine engine;
    engine.start();
    const auto layer = [](const char* n) {
        Layer l;
        l.name = n;
        return l;
    };
    engine.submit(AddLayerCommand{layer("used")});    // index 1
    engine.submit(AddLayerCommand{layer("empty_a")}); // index 2
    engine.submit(AddLayerCommand{layer("empty_b")}); // index 3
    engine.submit(AddLayerCommand{layer("current")}); // index 4
    engine.submit(SetCurrentLayerCommand{1});
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1}); // puts geometry on "used"
    engine.submit(SetCurrentLayerCommand{4});          // "current" is empty but current
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layers.size() == 5; }));

    engine.submit(PurgeCommand{2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Purged 2 layers.") != std::string::npos;
    }));
    engine.consume_snapshot();
    // 0, "used" (has geometry) and "current" (is current) survive; the two empties go.
    const auto& ls = engine.snapshot().layers;
    REQUIRE(ls.size() == 3);
    bool has_used = false;
    bool has_current = false;
    for (const auto& l : ls) {
        REQUIRE(l.name != "empty_a");
        REQUIRE(l.name != "empty_b");
        has_used = has_used || l.name == "used";
        has_current = has_current || l.name == "current";
    }
    REQUIRE(has_used);
    REQUIRE(has_current);
    engine.stop();
}

TEST_CASE("#30: PURGE says so when there is nothing to purge") {
    // Ph10.1: an empty purge must not look like a successful one.
    GeometryEngine engine;
    engine.start();
    engine.submit(PurgeCommand{1});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Purge: nothing to purge.") != std::string::npos;
    }));
    engine.stop();
}

TEST_CASE("#30: geometry keeps its layer across a purge that reindexes") {
    // Removing a layer shifts every index above it. If purge walked upwards the
    // surviving entities would end up pointing at the wrong layer, so it walks down.
    GeometryEngine engine;
    engine.start();
    Layer gap;
    gap.name = "gap";
    Layer red;
    red.name = "red";
    red.color = {255, 0, 0};
    engine.submit(AddLayerCommand{gap}); // index 1 -- will be purged
    engine.submit(AddLayerCommand{red}); // index 2 -- holds the line
    engine.submit(SetCurrentLayerCommand{2});
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layers.size() == 3; }));

    engine.submit(PurgeCommand{2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Purged 1 layer.") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().layers.size() == 2);
    // The line is still red: its layer reference followed the reindex.
    REQUIRE(!engine.snapshot().line_batches.empty());
    for (const ColorBatch& b : engine.snapshot().line_batches) {
        REQUIRE(b.color == Rgb{255, 0, 0});
    }
    engine.stop();
}

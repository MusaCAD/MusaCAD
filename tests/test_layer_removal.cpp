// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Removing a layer (PURGE, the Layer Manager's delete) renumbers every reference to the
// layers after it: the objects in the drawing, the objects inside block definitions, and
// what the undo history would bring back. A layer used only inside a block is in use.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"

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

io::Document dump(GeometryEngine& engine, const char* name) {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
    // Wait for THIS save: the status alone can be a stale "Saved" from the dump before,
    // when the command in between (an undo, say) reported nothing.
    engine.consume_snapshot();
    const std::uint64_t before = engine.snapshot().document_version;
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [before](const auto& s) { return s.document_version > before; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    std::filesystem::remove(p);
    return doc;
}

void add_layer(GeometryEngine& e, const char* name) {
    Layer l;
    l.name = name;
    e.submit(AddLayerCommand{l});
}

AddLineCommand line_on(std::uint16_t layer, Vec2 a, Vec2 b, std::uint64_t group) {
    EntityProps p;
    p.layer = layer;
    return AddLineCommand{a, b, group, p};
}
} // namespace

TEST_CASE("PURGE keeps a layer used inside a block, and the block's objects stay on it") {
    GeometryEngine engine;
    engine.start();
    add_layer(engine, "Spare"); // 1, unused: purged
    add_layer(engine, "Inner"); // 2, used only inside the block
    engine.submit(line_on(2, {0, 0}, {10, 0}, 1));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(DefineBlockCommand{"B", {0, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.block_names.size() == 1; }));
    PurgeCommand purge;
    purge.what = 4; // layers
    purge.group = 3;
    engine.submit(purge);
    const io::Document doc = dump(engine, "musacad_layer_removal_purge.musa");
    REQUIRE(doc.layers.size() == 2);
    REQUIRE(doc.layers[1].name == "Inner");
    REQUIRE(doc.block_defs.size() == 1);
    REQUIRE(doc.block_defs[0].lines.size() == 1);
    REQUIRE(doc.block_defs[0].lines[0].props.layer == 1); // renumbered with its layer
    engine.stop();
}

TEST_CASE("After a layer is removed, undo brings objects back on the layers they were on") {
    GeometryEngine engine;
    engine.start();
    add_layer(engine, "A"); // 1
    add_layer(engine, "B"); // 2
    engine.submit(line_on(2, {0, 0}, {10, 0}, 1)); // on B
    engine.submit(line_on(1, {0, 5}, {10, 5}, 2)); // on A
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(EraseSelectionCommand{3});
    engine.submit(RemoveLayerCommand{1}); // A, now empty
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Layer removed."; }));
    engine.submit(UndoLastGroupCommand{});
    const io::Document doc = dump(engine, "musacad_layer_removal_undo.musa");
    REQUIRE(doc.layers.size() == 2);
    REQUIRE(doc.layers[1].name == "B");
    REQUIRE(doc.lines.size() == 2);
    for (const io::DocLine& l : doc.lines) {
        // The line on B is back on B (now 1); the one on A, whose layer is gone, on 0.
        REQUIRE(l.props.layer == (l.a.y == 0.0 ? 1 : 0));
    }
    engine.stop();
}

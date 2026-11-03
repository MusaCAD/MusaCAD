// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Hatches inside block definitions (v46): read from a DXF, drawn where the block is
// inserted (fills and line patterns), saved in both formats, carried by EXPLODE, BLOCK and
// WBLOCK. Real drawings keep section fills, logos and title-block shading in blocks.

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/block_resolve.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad::core;
using namespace musacad::core::io;
using Catch::Approx;

namespace {
DocHatch square_hatch(const char* pattern, double x0) {
    DocHatch h;
    h.loops = {{{x0, 0}, {x0 + 10, 0}, {x0 + 10, 10}, {x0, 10}}};
    h.pattern_name = pattern;
    return h;
}
Document drawing_with_hatched_block() {
    Document doc;
    DocBlockDef b;
    b.name = "PANEL";
    b.lines.push_back(DocLine{{0, 0}, {20, 0}});
    b.hatches.push_back(square_hatch("SOLID", 0));
    DocHatch lines = square_hatch("ANSI31", 10);
    lines.color2 = {1, 2, 3};
    lines.iso = true;
    b.hatches.push_back(lines);
    doc.block_defs.push_back(b);
    doc.inserts.push_back(DocInsert{"PANEL", {100, 0}});
    return doc;
}
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

TEST_CASE("Block hatches: a DXF block keeps its HATCH and SOLID, and reads back from an export") {
    const Document doc = drawing_with_hatched_block();
    Document back;
    REQUIRE(parse_dxf(serialize_dxf(doc), back).ok);
    REQUIRE(back.block_defs.size() == 1);
    const DocBlockDef& b = back.block_defs[0];
    REQUIRE(b.hatches.size() == 2);
    CHECK(b.hatches[0].pattern_name == "SOLID");
    CHECK(b.hatches[1].pattern_name == "ANSI31");
    REQUIRE(b.hatches[1].loops.size() == 1);
    CHECK(b.hatches[1].loops[0].size() == 4);

    // A SOLID entity inside a block is a filled hatch too.
    const std::string blk = "0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\nB\n10\n0\n20\n0\n0\nSOLID\n8\n0\n10\n0\n20\n0\n11\n4\n21\n0\n"
                            "12\n2\n22\n3\n13\n2\n23\n3\n0\nENDBLK\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n";
    Document solid;
    REQUIRE(parse_dxf(blk, solid).ok);
    REQUIRE(solid.block_defs.size() == 1);
    REQUIRE(solid.block_defs[0].hatches.size() == 1);
    CHECK(solid.block_defs[0].hatches[0].loops[0].size() == 3);
}

TEST_CASE("Block hatches: the native format keeps them, iso size and second colour included") {
    const Document doc = drawing_with_hatched_block();
    Document back;
    REQUIRE(parse_native(serialize_native(doc), back).ok);
    REQUIRE(back.block_defs.size() == 1);
    REQUIRE(back.block_defs[0].hatches.size() == 2);
    const DocHatch& h = back.block_defs[0].hatches[1];
    CHECK(h.pattern_name == "ANSI31");
    CHECK(h.iso);
    CHECK(h.color2.r == 1);
    CHECK(h.color2.b == 3);
    CHECK(back.hatches.empty()); // they stay in the block, not model space
    // Through the store and back: nothing lost.
    GeometryStore store;
    populate_store(store, back);
    const Document again = document_from_store(store);
    REQUIRE(again.block_defs.size() == 1);
    CHECK(again.block_defs[0].hatches == back.block_defs[0].hatches);
}

TEST_CASE("Block hatches: drawn where the block is inserted, filled and patterned") {
    GeometryStore store;
    populate_store(store, drawing_with_hatched_block());
    REQUIRE(store.inserts().slot_count() == 1);
    const InsertData& ins = store.inserts().data()[0];
    std::vector<InsertSeg> segs;
    std::vector<InsertFill> fills;
    resolve_insert(store, ins, 0.01, segs, &fills);
    REQUIRE_FALSE(fills.empty());
    for (const InsertFill& f : fills) { // the solid square, moved to x = 100..110
        for (const Vec2 p : {f.a, f.b, f.c}) {
            CHECK(p.x >= 100.0 - 1e-9);
            CHECK(p.x <= 110.0 + 1e-9);
        }
    }
    std::size_t pattern_lines = 0;
    for (const InsertSeg& s : segs) {
        pattern_lines += s.a.x > 110.0 - 1e-9 && s.b.x > 110.0 - 1e-9 && std::abs(s.a.y - s.b.y) > 1e-9;
    }
    CHECK(pattern_lines > 3); // ANSI31's slanted lines across the second square
    // FILLMODE off: no fills from the block either.
    store.set_fillmode(false);
    fills.clear();
    segs.clear();
    resolve_insert(store, ins, 0.01, segs, &fills);
    CHECK(fills.empty());
}

TEST_CASE("Block hatches: BLOCK takes a hatch in, EXPLODE gives it back where it was") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    AddHatchCommand hc;
    hc.loops = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    hc.group = 2;
    engine.submit(hc);
    REQUIRE(wait_until(engine, [](const RenderSnapshot& s) { return !s.fill_vertices.empty(); }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const RenderSnapshot& s) { return s.selection.size() == 2; }));
    engine.submit(DefineBlockCommand{"H", {0, 0}, 3});
    REQUIRE(wait_until(engine, [](const RenderSnapshot& s) {
        return s.status.find("Block \"H\" defined with 2 objects.") != std::string::npos;
    }));
    engine.consume_snapshot();
    CHECK_FALSE(engine.snapshot().fill_vertices.empty()); // the insert draws the fill
    REQUIRE(engine.snapshot().selection.size() == 1);
    engine.submit(ExplodeSelectionCommand{4});
    REQUIRE(wait_until(engine, [](const RenderSnapshot& s) {
        return s.selection.size() == 2 && !s.fill_vertices.empty();
    }));
    bool hatch_back = false;
    for (const EntityHandle& h : engine.snapshot().selection) {
        hatch_back = hatch_back || h.kind == EntityKind::Hatch;
    }
    CHECK(hatch_back);
    engine.stop();
}

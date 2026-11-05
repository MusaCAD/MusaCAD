// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Multilines inside block definitions (#45, v49): read from and written to DXF, saved in the
// drawing, drawn where the block is inserted in their style, taken in by BLOCK and given back
// by EXPLODE. Walls drawn with MLINE are often kept in blocks (a room, a standard bay).

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
#include "musacad/core/mline.hpp"

using namespace musacad::core;
using namespace musacad::core::io;
using Catch::Approx;

namespace {
// A block WALL holding a multiline in a style of three lines, filled, cut once on its first
// segment; and a reference to it at (100, 0).
Document drawing_with_walled_block() {
    Document doc;
    MLineStyle three;
    three.name = "THREE";
    three.flags = MLineStyle::kFill;
    three.elements = {MLineElement{0.5}, MLineElement{0.0}, MLineElement{-0.5}};
    doc.mline_styles = {MLineStyle{}, three};
    DocBlockDef b;
    b.name = "WALL";
    b.lines.push_back(DocLine{{0, -5}, {0, 5}});
    DocMLine m;
    m.vertices = {{0, 0}, {20, 0}, {20, 10}};
    m.style = 1;
    m.scale = 2.0;
    m.justification = 1; // Zero: the path down the middle
    m.breaks = {{{2.0, 4.0}}};
    b.mlines.push_back(m);
    doc.block_defs.push_back(b);
    doc.inserts.push_back(DocInsert{"WALL", {100, 0}});
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

TEST_CASE("Block multilines: DXF writes them inside the BLOCK and reads them back there") {
    const Document doc = drawing_with_walled_block();
    const std::string dxf = serialize_dxf(doc);
    // WALL's BLOCK ... ENDBLK (the spaces' blocks come first).
    const std::size_t blk = dxf.find("\nBLOCK\n", dxf.find("SECTION\n2\nBLOCKS"));
    const std::size_t at = dxf.find("\n2\nWALL\n", blk);
    const std::size_t end = dxf.find("\nENDBLK\n", at);
    REQUIRE(at != std::string::npos);
    CHECK(dxf.substr(at, end - at).find("\nMLINE\n") != std::string::npos);
    Document back;
    REQUIRE(parse_dxf(dxf, back).ok);
    CHECK(back.mlines.empty()); // in the block, not in model space
    const auto wall = std::find_if(back.block_defs.begin(), back.block_defs.end(),
                                   [](const DocBlockDef& b) { return b.name == "WALL"; });
    REQUIRE(wall != back.block_defs.end());
    REQUIRE(wall->mlines.size() == 1);
    const DocMLine& m = wall->mlines[0];
    REQUIRE(m.style < back.mline_styles.size());
    CHECK(back.mline_styles[m.style].name == "THREE");
    CHECK(m.scale == Approx(2.0));
    CHECK(m.justification == 1);
    REQUIRE(m.vertices.size() == 3);
    CHECK(m.vertices[2].x == Approx(20.0));
    CHECK(m.vertices[2].y == Approx(10.0));
    REQUIRE(!m.breaks.empty());
    REQUIRE(!m.breaks[0].empty());
    CHECK(m.breaks[0][0] == std::vector<double>{2.0, 4.0});
}

TEST_CASE("Block multilines: the drawing keeps them, through the store and back") {
    const Document doc = drawing_with_walled_block();
    Document back;
    REQUIRE(parse_native(serialize_native(doc), back).ok);
    REQUIRE(back.block_defs.size() == 1);
    CHECK(back.block_defs[0].mlines == doc.block_defs[0].mlines);
    CHECK(back.mlines.empty());
    GeometryStore store;
    populate_store(store, back);
    const Document again = document_from_store(store);
    REQUIRE(again.block_defs.size() == 1);
    CHECK(again.block_defs[0].mlines == doc.block_defs[0].mlines);
}

TEST_CASE("Block multilines: drawn where the block is inserted, in their style, filled") {
    GeometryStore store;
    populate_store(store, drawing_with_walled_block());
    REQUIRE(store.inserts().slot_count() == 1);
    const InsertData& ins = store.inserts().data()[0];
    std::vector<InsertSeg> segs;
    std::vector<InsertFill> fills;
    resolve_insert(store, ins, 0.01, segs, &fills);
    // The three lines along the first segment: offsets 1, 0 and -1 (scale 2), moved to x = 100.
    for (const double y : {1.0, 0.0, -1.0}) {
        bool found = false;
        for (const InsertSeg& s : segs) {
            found = found || (std::abs(s.a.y - y) < 1e-9 && std::abs(s.b.y - y) < 1e-9 && s.a.x >= 100.0 - 1.0 - 1e-9 &&
                              s.b.x <= 121.0 + 1e-9 && std::abs(s.b.x - s.a.x) > 1.0);
        }
        CHECK(found);
    }
    // The cut: nothing of the top line between 2 and 4 along it.
    for (const InsertSeg& s : segs) {
        if (std::abs(s.a.y - 1.0) < 1e-9 && std::abs(s.b.y - 1.0) < 1e-9) {
            const double lo = std::min(s.a.x, s.b.x);
            const double hi = std::max(s.a.x, s.b.x);
            CHECK_FALSE((lo < 102.9 && hi > 103.1)); // the cut: 102 to 104
        }
    }
    CHECK_FALSE(fills.empty()); // the style is filled
    store.set_fillmode(false);
    fills.clear();
    segs.clear();
    resolve_insert(store, ins, 0.01, segs, &fills);
    CHECK(fills.empty());
}

TEST_CASE("Block multilines: their style is in use, and deleting an earlier one keeps it right") {
    Document doc = drawing_with_walled_block();
    MLineStyle spare;
    spare.name = "SPARE";
    doc.mline_styles.insert(doc.mline_styles.begin() + 1, spare); // STANDARD, SPARE, THREE
    doc.block_defs[0].mlines[0].style = 2;
    GeometryStore store;
    populate_store(store, doc);
    CHECK(store.mline_style_in_use(2));
    CHECK_FALSE(store.remove_mline_style(2)); // the block's multiline is in it
    REQUIRE(store.remove_mline_style(1));
    const BlockDef* b = store.block(0);
    REQUIRE(b != nullptr);
    REQUIRE(b->content.mlines.size() == 1);
    CHECK(store.mline_styles()[b->content.mlines[0].style].name == "THREE");
}

TEST_CASE("Block multilines: BLOCK takes a multiline in, EXPLODE gives it back") {
    GeometryEngine engine;
    engine.start();
    AddMLineCommand mc;
    mc.points = {{0, 0}, {10, 0}, {10, 10}};
    mc.scale = 2.0;
    mc.group = 1;
    engine.submit(mc);
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const RenderSnapshot& s) { return s.selection.size() == 1; }));
    engine.submit(DefineBlockCommand{"M", {0, 0}, 2});
    REQUIRE(wait_until(engine, [](const RenderSnapshot& s) {
        return s.status.find("Block \"M\" defined with 1 object.") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().selection.size() == 1);
    CHECK(engine.snapshot().selection[0].kind == EntityKind::Insert);
    engine.submit(ExplodeSelectionCommand{3});
    REQUIRE(wait_until(engine, [](const RenderSnapshot& s) {
        return s.selection.size() == 1 && s.selection[0].kind == EntityKind::MLine;
    }));
    engine.stop();
}

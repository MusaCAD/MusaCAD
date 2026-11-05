// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Multileaders with block content (#61): MLEADER's Content type Block -- AutoCAD's standard
// tag blocks or the drawing's own, the attribute values asked -- drawn, kept in the drawing
// and in DXF, exploded into references; MLEADERCOLLECT making several one.

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/mleader_blocks.hpp"

using namespace musacad::core;
using musacad::command::CommandOutput;
using musacad::command::CommandProcessor;
using Catch::Approx;

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
    std::filesystem::remove(p);
    engine.consume_snapshot();
    const std::uint64_t before = engine.snapshot().document_version;
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.document_version > before && !s.dirty; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    std::filesystem::remove(p);
    return doc;
}

/// A balloon from `tip` with its landing ending at `end`, numbered `n`.
AddBlockMLeaderCommand balloon(Vec2 tip, Vec2 end, const char* n, std::uint64_t group) {
    AddBlockMLeaderCommand c;
    c.vertices = {tip, end - Vec2{5, 0}, end};
    c.block = "_TagCircle";
    c.values = {n};
    c.scale = 2.0;
    c.leader_flags = LeaderForm::kLanding;
    c.group = group;
    return c;
}
} // namespace

TEST_CASE("#61 a balloon: the tag block added to the drawing, against the landing, kept in the file") {
    GeometryEngine engine;
    engine.start();
    engine.submit(balloon({0, 0}, {20, 10}, "7", 1));
    io::Document doc = dump(engine, "musacad_mleader_block.musa");
    REQUIRE(doc.block_defs.size() == 1);
    REQUIRE(doc.block_defs[0].name == "_TagCircle");
    REQUIRE(doc.mleaders.size() == 1);
    const io::DocMLeader& m = doc.mleaders[0];
    REQUIRE(m.content_block == "_TagCircle");
    REQUIRE(m.content == "7");
    // The circle (radius 1.25 at scale 2) touches the landing's end on its left: its centre
    // 2.5 to the right.
    REQUIRE(m.block.pos.x == Approx(22.5));
    REQUIRE(m.block.pos.y == Approx(10.0));
    REQUIRE(m.block.height == Approx(2.0));

    // Drawn: the circle as lines round (22.5, 10), and the number.
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    int on_circle = 0;
    for (const Vec2& v : s.line_vertices) {
        on_circle += std::abs(distance(v, {22.5, 10}) - 2.5) < 0.05 ? 1 : 0;
    }
    REQUIRE(on_circle > 20);

    // Native round trip: as it was.
    io::Document again;
    const std::string text = io::serialize_native(doc);
    REQUIRE(io::parse_native(text, again).ok);
    REQUIRE(again.mleaders == doc.mleaders);

    // A second balloon reuses the block; EXPLODE makes references with their values.
    engine.submit(balloon({50, 0}, {70, 10}, "8", 2));
    doc = dump(engine, "musacad_mleader_block2.musa");
    REQUIRE(doc.block_defs.size() == 1);
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& st) { return st.selection.size() == 2; }));
    engine.submit(ExplodeSelectionCommand{3});
    doc = dump(engine, "musacad_mleader_block3.musa");
    REQUIRE(doc.mleaders.empty());
    REQUIRE(doc.inserts.size() == 2);
    REQUIRE((doc.inserts[0].attribs.at(0).value == "7" || doc.inserts[1].attribs.at(0).value == "7"));
    engine.stop();
}

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

struct Out : CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
};

struct H {
    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    void run(std::initializer_list<const char*> lines) {
        for (const char* l : lines) {
            proc.submit_line(l);
        }
    }
    template <class T>
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
};

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

TEST_CASE("#61 MLEADER's Content type Block asks for the block, then its attribute values") {
    H h;
    h.run({"MLEADER", "O", "C"});
    REQUIRE(h.out.prompt == "Select a content type [Block/Mtext/None] <Mtext>: ");
    h.run({"B"});
    REQUIRE(h.out.prompt == "Enter block name or [?] <_TagCircle>: ");
    h.run({"?"});
    REQUIRE(h.out.lines.back().find("_TagHexagon") != std::string::npos);
    h.run({"NOSUCH"});
    REQUIRE(h.out.lines.back() == "Block \"NOSUCH\" not found.");
    h.run({"_taghexagon", "X", "0,0", "10,5"});
    REQUIRE(h.out.prompt == "Enter tag number <TAGNUMBER>: ");
    h.run({"12"});
    const auto* c = h.last<AddBlockMLeaderCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->block == "_TagHexagon");
    REQUIRE(c->values == std::vector<std::string>{"12"});
    REQUIRE(c->vertices.front() == Vec2{0, 0});
    REQUIRE_FALSE(h.proc.has_active_command());
    // The Options are for that multileader: the next takes its content from the style again.
    h.run({"MLEADER", "0,0", "10,5", "NOTE"});
    REQUIRE(h.last<AddMLeaderCommand>()->content == "NOTE");
}

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

TEST_CASE("#61 DXF: a balloon goes out as its leader and a reference; AutoCAD's block content comes in") {
    io::Document doc;
    io::DocBlockDef tag;
    tag.name = "_TagCircle";
    tag.circles.push_back({{0, 0}, 1.25});
    io::DocAttDef a;
    a.text.content = "TAGNUMBER";
    a.def = "TAGNUMBER";
    tag.attdefs.push_back(a);
    doc.block_defs.push_back(tag);
    io::DocMLeader m;
    m.vertices = {{0, 0}, {15, 10}, {20, 10}};
    m.content_block = "_TagCircle";
    m.content = std::string("4\x1E") + "5";
    m.block.pos = {22.5, 10};
    m.block.height = 2.0;
    doc.mleaders.push_back(m);
    const std::string dxf = io::serialize_dxf(doc);
    io::Document back;
    REQUIRE(io::parse_dxf(dxf, back).ok);
    REQUIRE(back.inserts.size() == 2); // one reference a balloon
    REQUIRE(back.inserts[0].block_name == "_TagCircle");
    REQUIRE(back.inserts[0].attribs.at(0).value == "4");
    REQUIRE(back.inserts[1].attribs.at(0).value == "5");
    REQUIRE(back.inserts[1].pos.x == Approx(27.5)); // the next in the row

    // AutoCAD's MULTILEADER with block content: the block by its record's handle (341), its
    // place (15/25) and scale (16), the values after the context (177, 302).
    const std::string acad =
        "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nBLOCK_RECORD\n"
        "0\nBLOCK_RECORD\n5\nB1\n100\nAcDbSymbolTableRecord\n100\nAcDbBlockTableRecord\n2\n_TagBox\n"
        "0\nENDTAB\n0\nENDSEC\n"
        "0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\n_TagBox\n70\n0\n10\n0\n20\n0\n"
        "0\nLINE\n8\n0\n10\n-1\n20\n-1\n11\n1\n21\n-1\n"
        "0\nATTDEF\n8\n0\n10\n0\n20\n0\n40\n1\n1\nTAGNUMBER\n3\nEnter tag number\n2\nTAGNUMBER\n70\n0\n"
        "0\nENDBLK\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n0\nMULTILEADER\n5\nM1\n8\n0\n100\nAcDbMLeader\n270\n2\n"
        "300\nCONTEXT_DATA{\n40\n1.0\n10\n0\n20\n0\n30\n0\n41\n1.5\n290\n0\n296\n1\n341\nB1\n"
        "14\n0\n24\n0\n34\n1\n15\n30\n25\n12\n35\n0\n16\n3\n26\n3\n36\n3\n46\n0\n"
        "302\nLEADER{\n290\n1\n291\n1\n10\n27\n20\n12\n30\n0\n11\n1\n21\n0\n31\n0\n40\n2\n"
        "304\nLEADER_LINE{\n10\n0\n20\n0\n30\n0\n305\n}\n303\n}\n301\n}\n"
        "170\n1\n172\n1\n344\nB1\n330\nA9\n177\n1\n44\n0\n302\n42\n"
        "0\nENDSEC\n0\nEOF\n";
    io::Document ac;
    REQUIRE(io::parse_dxf(acad, ac).ok);
    REQUIRE(ac.mleaders.size() == 1);
    REQUIRE(ac.mleaders[0].content_block == "_TagBox");
    REQUIRE(ac.mleaders[0].content == "42");
    REQUIRE(ac.mleaders[0].block.pos == Vec2{30, 12});
    REQUIRE(ac.mleaders[0].block.height == Approx(3.0));
}

TEST_CASE("#61 a multileader style of type Block: MLEADER takes its block, the file keeps it") {
    io::Document doc;
    MLeaderStyle balloons;
    balloons.name = "Balloons";
    balloons.text = false;
    balloons.block = "_TagBox";
    doc.mleader_styles = {MLeaderStyle{}, balloons};
    doc.current_mleader_style = 1;
    io::Document back;
    REQUIRE(io::parse_native(io::serialize_native(doc), back).ok);
    REQUIRE(back.mleader_styles.size() == 2);
    REQUIRE(back.mleader_styles[1].block == "_TagBox");
    REQUIRE(back.mleader_styles[0].block.empty());

    GeometryEngine engine;
    engine.start();
    engine.submit(MLeaderStyleCommand{MLeaderStyleCommand::Op::Add, "Balloons", balloons});
    engine.submit(MLeaderStyleCommand{MLeaderStyleCommand::Op::SetCurrent, "Balloons", {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.current_mleader_style == 1; }));
    engine.stop();
}

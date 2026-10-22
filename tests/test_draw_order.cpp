// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Draw order, transparency on single objects and ByBlock (#55, #68, #69): every object has a
// draw order -- a new one over the rest, a copy over its source, an edit where it was --
// DRAWORDER, TEXTTOFRONT and HATCHTOBACK change it, the scene's depths follow it; an object's
// own transparency and ByBlock colour, linetype and lineweight; all of it kept in the drawing
// file and in DXF.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
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
#include "musacad/core/properties.hpp"

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
    engine.consume_snapshot();
    const std::uint64_t before = engine.snapshot().document_version;
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [before](const auto& s) { return s.document_version > before; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    std::filesystem::remove(p);
    return doc;
}

// The order of the line whose first point is `a`.
std::uint32_t line_order(const io::Document& doc, Vec2 a) {
    for (const io::DocLine& l : doc.lines) {
        if (std::abs(l.a.x - a.x) < 1e-9 && std::abs(l.a.y - a.y) < 1e-9) {
            return l.props.order;
        }
    }
    FAIL("no such line");
    return 0;
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
} // namespace

TEST_CASE("EntityProps: transparency, ByBlock and a draw order that equality leaves out") {
    STATIC_REQUIRE(sizeof(EntityProps) == 16);
    EntityProps a;
    EntityProps b;
    b.order = 77;
    REQUIRE(a == b); // alike, whichever is above
    b.transparency = 40;
    REQUIRE_FALSE(a == b);

    EntityProps p;
    p.set_color_by_block(true);
    REQUIRE(p.color_by_block());
    REQUIRE_FALSE(p.color_by_layer());
    p.set_color_by_layer(false); // an explicit colour: no longer ByBlock
    REQUIRE_FALSE(p.color_by_block());
    p.set_linetype_by_block(true);
    p.set_lineweight_by_block(true);
    p.color = Rgb{255, 0, 0};
    p.set_color_by_block(true);
    const ResolvedProps r = resolve(p, Layer{"0"});
    REQUIRE(r.color == Rgb{255, 255, 255}); // ByBlock outside a reference: white
    REQUIRE(r.linetype == Linetype::Continuous);
    REQUIRE(r.lineweight == 25);
    p.set_lineweight_by_layer(true);
    REQUIRE_FALSE(p.lineweight_by_block());
}

TEST_CASE("A new object goes over the rest, an edit keeps its place, a copy goes on top") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{0, 10}, {10, 10}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    io::Document doc = dump(engine, "musacad_order1.musa");
    const std::uint32_t first = line_order(doc, {0, 0});
    const std::uint32_t second = line_order(doc, {0, 10});
    REQUIRE(first < second);

    // MOVE: the first line keeps its place under the second.
    engine.submit(SelectPickCommand{{5, 0}, 1.0, false, false});
    engine.submit(MoveSelectionCommand{{0, -5}, 3});
    doc = dump(engine, "musacad_order2.musa");
    REQUIRE(line_order(doc, {0, -5}) < line_order(doc, {0, 10}));

    // COPY: the copy goes over both.
    engine.submit(ClearSelectionCommand{});
    engine.submit(SelectPickCommand{{5, -5}, 1.0, false, false});
    engine.submit(CopySelectionCommand{{0, 30}, 4});
    doc = dump(engine, "musacad_order3.musa");
    REQUIRE(line_order(doc, {0, 25}) > line_order(doc, {0, 10}));
    REQUIRE(line_order(doc, {0, -5}) < line_order(doc, {0, 10}));
    engine.stop();
}

TEST_CASE("DRAWORDER Front, Back, Above and Under; TEXTTOFRONT and HATCHTOBACK; undo") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});   // A
    engine.submit(AddLineCommand{{0, 10}, {10, 10}, 2}); // B
    engine.submit(AddLineCommand{{0, 20}, {10, 20}, 3}); // C
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 6; }));
    const auto pick = [&](Vec2 at) {
        engine.submit(ClearSelectionCommand{});
        engine.submit(SelectPickCommand{at, 1.0, false, false});
    };
    pick({5, 0});
    engine.submit(DrawOrderCommand{DrawOrderCommand::Op::Front, 7, 4}); // A over B and C
    io::Document doc = dump(engine, "musacad_draworder1.musa");
    REQUIRE(line_order(doc, {0, 0}) > line_order(doc, {0, 20}));
    REQUIRE(line_order(doc, {0, 0}) > line_order(doc, {0, 10}));

    pick({5, 20});
    engine.submit(DrawOrderCommand{DrawOrderCommand::Op::Back, 7, 5}); // C under everything
    doc = dump(engine, "musacad_draworder2.musa");
    REQUIRE(line_order(doc, {0, 20}) < line_order(doc, {0, 10}));
    REQUIRE(line_order(doc, {0, 20}) < line_order(doc, {0, 0}));

    // C just above B (the reference): between B and A.
    pick({5, 20});
    engine.submit(DrawOrderCommand{DrawOrderCommand::Op::Keep, 7, 0});
    pick({5, 10});
    engine.submit(DrawOrderCommand{DrawOrderCommand::Op::Above, 7, 6});
    doc = dump(engine, "musacad_draworder3.musa");
    REQUIRE(line_order(doc, {0, 20}) > line_order(doc, {0, 10}));
    REQUIRE(line_order(doc, {0, 20}) < line_order(doc, {0, 0}));
    // And under it again.
    pick({5, 20});
    engine.submit(DrawOrderCommand{DrawOrderCommand::Op::Keep, 7, 0});
    pick({5, 10});
    engine.submit(DrawOrderCommand{DrawOrderCommand::Op::Under, 7, 7});
    doc = dump(engine, "musacad_draworder4.musa");
    REQUIRE(line_order(doc, {0, 20}) < line_order(doc, {0, 10}));
    engine.submit(UndoLastGroupCommand{});
    doc = dump(engine, "musacad_draworder5.musa");
    REQUIRE(line_order(doc, {0, 20}) > line_order(doc, {0, 10}));

    // A hatch drawn last goes to the back; a text drawn first comes to the front.
    AddTextCommand t;
    t.pos = {0, 40};
    t.height = 2.5;
    t.content = "NOTE";
    t.group = 8;
    engine.submit(t);
    AddHatchCommand h;
    h.loops = {{{0, 30}, {10, 30}, {10, 50}, {0, 50}}};
    h.pattern_name = "SOLID";
    h.group = 9;
    engine.submit(h);
    engine.submit(DrawOrderCommand{DrawOrderCommand::Op::HatchToBack, 7, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 hatch object sent to back."; }));
    engine.submit(DrawOrderCommand{DrawOrderCommand::Op::TextToFront, 1, 11});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 object brought to front."; }));
    doc = dump(engine, "musacad_draworder6.musa");
    REQUIRE(doc.hatches[0].props.order < line_order(doc, {0, 20}));
    REQUIRE(doc.texts[0].props.order > line_order(doc, {0, 0}));

    // The scene: the hatch's fill underneath the line work.
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(!s.fill_depth.empty());
    REQUIRE(s.fill_depth.size() == s.fill_vertices.size());
    REQUIRE(s.line_depth.size() == s.line_vertices.size() / 2);
    float shallowest_line = 1.0f;
    for (const float d : s.line_depth) {
        shallowest_line = std::min(shallowest_line, d);
    }
    REQUIRE(s.fill_depth.front() > shallowest_line);
    engine.stop();
}

TEST_CASE("An object's own transparency and ByBlock colour in the scene; the Transparency property") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    PropertyValue v;
    v.num = 50;
    engine.submit(SetPropertyCommand{PropertyId::Transparency, v, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return !s.line_batches.empty() && s.line_batches.front().alpha < 200;
    }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().line_batches.front().alpha == Approx(128).margin(1));
    io::Document doc = dump(engine, "musacad_transparency.musa");
    REQUIRE(doc.lines[0].props.transparency == 50);
    PropertyValue by_layer;
    by_layer.flag = true;
    engine.submit(SetPropertyCommand{PropertyId::Transparency, by_layer, 3});
    doc = dump(engine, "musacad_transparency2.musa");
    REQUIRE(doc.lines[0].props.transparency == EntityProps::kTransparencyByLayer);

    // A block whose line is ByBlock, referenced in red: drawn red.
    PropertyValue by_block;
    by_block.text = "ByBlock";
    engine.submit(SelectAllCommand{});
    engine.submit(SetPropertyCommand{PropertyId::Color, by_block, 4});
    engine.submit(SelectAllCommand{});
    engine.submit(DefineBlockCommand{"BB", {0, 0}, 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.block_names.size() == 1; }));
    EntityProps red;
    red.set_color_by_layer(false);
    red.color = Rgb{255, 0, 0};
    engine.submit(AddInsertCommand{0, {100, 0}, 1.0, 1.0, 0.0, 6, red, {}});
    REQUIRE(wait_until(engine, [](const auto& s) {
        for (const ColorBatch& b : s.line_batches) {
            if (b.color == Rgb{255, 0, 0}) {
                return true;
            }
        }
        return false;
    }));
    engine.stop();
}

TEST_CASE("The drawing file keeps the draw order, transparency and ByBlock, in blocks too") {
    io::Document doc;
    io::DocLine low;
    low.a = {0, 0};
    low.b = {1, 0};
    low.props.order = 5000;
    io::DocLine high;
    high.a = {0, 1};
    high.b = {1, 1};
    high.props.order = 4000;
    high.props.transparency = 30;
    high.props.set_color_by_block(true);
    doc.lines = {low, high};
    io::DocCircle mid;
    mid.radius = 1.0;
    mid.props.order = 4500;
    doc.circles.push_back(mid);
    io::DocBlockDef b;
    b.name = "B";
    io::DocLine in_block;
    in_block.props.set_lineweight_by_block(true);
    in_block.props.transparency = EntityProps::kTransparencyByBlock;
    b.lines.push_back(in_block);
    doc.block_defs.push_back(b);

    const std::string text = io::serialize_native(doc);
    REQUIRE(text.find("\nDRAWORDER 3 LINE 1 CIRCLE 0 LINE 0\n") != std::string::npos);
    io::Document back;
    REQUIRE(io::parse_native(text, back).ok);
    REQUIRE(back.lines[1].props.order < back.circles[0].props.order);
    REQUIRE(back.circles[0].props.order < back.lines[0].props.order);
    REQUIRE(back.lines[1].props.transparency == 30);
    REQUIRE(back.lines[1].props.color_by_block());
    REQUIRE(back.block_defs[0].lines[0].props.lineweight_by_block());
    REQUIRE(back.block_defs[0].lines[0].props.transparency == EntityProps::kTransparencyByBlock);
}

TEST_CASE("DXF: ByBlock (62 0, BYBLOCK, 370 -2), transparency (440) and the file's order as the draw order") {
    io::Document doc;
    io::DocLine l;
    l.a = {0, 0};
    l.b = {1, 0};
    l.props.set_color_by_block(true);
    l.props.set_linetype_by_block(true);
    l.props.set_lineweight_by_block(true);
    l.props.transparency = 40;
    doc.lines.push_back(l);
    const std::string dxf = io::serialize_dxf(doc);
    REQUIRE(dxf.find("\n62\n0\n") != std::string::npos);
    REQUIRE(dxf.find("\n6\nBYBLOCK\n") != std::string::npos);
    REQUIRE(dxf.find("\n370\n-2\n") != std::string::npos);
    REQUIRE(dxf.find("\n440\n33554585\n") != std::string::npos); // 0x02000000 | 153 (60 % opaque)
    io::Document back;
    REQUIRE(io::parse_dxf(dxf, back).ok);
    REQUIRE(back.lines.size() == 1);
    REQUIRE(back.lines[0].props.color_by_block());
    REQUIRE(back.lines[0].props.linetype_by_block());
    REQUIRE(back.lines[0].props.lineweight_by_block());
    REQUIRE(back.lines[0].props.transparency == 40);

    const std::string two =
        "0\nSECTION\n2\nENTITIES\n0\nCIRCLE\n8\n0\n10\n0\n20\n0\n40\n1\n0\nLINE\n8\n0\n10\n0\n20\n0\n11\n1\n21\n0\n"
        "440\n16777216\n0\nENDSEC\n0\nEOF\n";
    io::Document d;
    REQUIRE(io::parse_dxf(two, d).ok);
    REQUIRE(d.circles[0].props.order < d.lines[0].props.order); // the circle came first
    REQUIRE(d.lines[0].props.transparency == EntityProps::kTransparencyByBlock);
}

TEST_CASE("DRAWORDER, TEXTTOFRONT, HATCHTOBACK and CETRANSPARENCY at the command line") {
    H h;
    h.proc.set_selection_count(1);
    h.run({"DRAWORDER"});
    REQUIRE(h.out.prompt == "Enter object ordering option [Above objects/Under objects/Front/Back] <Back>: ");
    h.run({"F"});
    REQUIRE(h.last<DrawOrderCommand>()->op == DrawOrderCommand::Op::Front);
    h.run({"DRAWORDER", ""});
    REQUIRE(h.last<DrawOrderCommand>()->op == DrawOrderCommand::Op::Back);
    h.run({"DRAWORDER", "A"});
    REQUIRE(h.last<DrawOrderCommand>()->op == DrawOrderCommand::Op::Keep);
    REQUIRE(h.out.prompt == "Select reference objects: ");
    h.run({"5,5", ""});
    REQUIRE(h.last<DrawOrderCommand>()->op == DrawOrderCommand::Op::Above);
    REQUIRE(!h.proc.has_active_command());

    h.run({"TEXTTOFRONT"});
    REQUIRE(h.out.prompt == "Bring to front [Text/Dimensions/Leaders/All] <All>: ");
    h.run({"D"});
    REQUIRE(h.last<DrawOrderCommand>()->op == DrawOrderCommand::Op::TextToFront);
    REQUIRE(h.last<DrawOrderCommand>()->kinds == 2);
    h.run({"HATCHTOBACK"});
    REQUIRE(h.last<DrawOrderCommand>()->op == DrawOrderCommand::Op::HatchToBack);

    h.run({"CETRANSPARENCY"});
    REQUIRE(h.out.prompt == "Enter new value for CETRANSPARENCY <\"ByLayer\">: ");
    h.run({"30"});
    REQUIRE(h.last<SetCurrentPropsCommand>()->current.transparency == 30);
    h.run({"CECOLOR", "BYBLOCK"});
    REQUIRE(h.last<SetCurrentPropsCommand>()->current.color_by_block());
}

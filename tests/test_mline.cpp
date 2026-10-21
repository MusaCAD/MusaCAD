// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// MLINE, MLSTYLE and MLEDIT (#45): the multiline's geometry from its style (offsets, mitres,
// gaps, caps), the entity in the engine (drawn, moved, mirrored, exploded, listed), the
// command line, MLEDIT's tools, and the drawing file and DXF.

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/mline.hpp"

using namespace musacad::core;
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
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
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

struct Out : musacad::command::CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
};

struct H {
    std::vector<Command> cmds;
    Out out;
    musacad::command::CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    template <class T>
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
    void run(std::initializer_list<const char*> lines) {
        for (const char* l : lines) {
            proc.submit_line(l);
        }
    }
};

AddMLineCommand wall(std::vector<Vec2> pts, double scale = 1.0, std::uint8_t just = 0, std::uint64_t group = 1) {
    AddMLineCommand c;
    c.points = std::move(pts);
    c.scale = scale;
    c.justification = just;
    c.group = group;
    return c;
}
} // namespace

TEST_CASE("A multiline's elements: offsets by justification and scale, mitred at a corner") {
    const MLineStyle st; // STANDARD: +0.5 and -0.5
    REQUIRE(mline::offsets(st, 20.0, 0) == std::vector<double>{0.0, -20.0}); // Top: along the upper line
    REQUIRE(mline::offsets(st, 20.0, 1) == std::vector<double>{10.0, -10.0}); // Zero
    REQUIRE(mline::offsets(st, 20.0, 2) == std::vector<double>{20.0, 0.0});   // Bottom
    // An L, Top, scale 1: the lower line runs outside the corner, mitred.
    MLineData m;
    m.vertices = {{0, 0}, {10, 0}, {10, 10}};
    const Vec2 start = mline::element_point(m, st, 0, 1);
    REQUIRE(start.x == Approx(0.0).margin(1e-12));
    REQUIRE(start.y == Approx(-1.0));
    const Vec2 corner = mline::element_point(m, st, 1, 1);
    REQUIRE(corner.x == Approx(11.0));
    REQUIRE(corner.y == Approx(-1.0));
    const Vec2 end = mline::element_point(m, st, 2, 1);
    REQUIRE(end.x == Approx(11.0));
    REQUIRE(end.y == Approx(10.0));
    const mline::Geometry g = mline::geometry(m, st);
    REQUIRE(g.pieces.size() == 4); // two elements, two segments
    REQUIRE(g.caps.empty());       // STANDARD has no caps
}

TEST_CASE("A multiline's gaps, caps, joints and fill") {
    MLineStyle st;
    st.flags = MLineStyle::kStartLine | MLineStyle::kEndOuterArc | MLineStyle::kJoints | MLineStyle::kFill;
    MLineData m;
    m.vertices = {{0, 0}, {10, 0}, {10, 10}};
    m.breaks = {{{2.0, 4.0}, {}}}; // the upper line of the first segment cut from 2 to 4
    const mline::Geometry g = mline::geometry(m, st);
    REQUIRE(g.pieces.size() == 5);
    REQUIRE(g.arcs.size() == 1);        // the outer arc at the end
    REQUIRE(g.lines.size() == 2 * 2);   // the start line and the joint at the corner
    REQUIRE(g.fill.size() == 2 * 6);    // two triangles a segment
    bool gap = false;
    for (const mline::Geometry::Piece& p : g.pieces) {
        gap = gap || (p.element == 0 && p.b.x == Approx(2.0) && p.b.y == Approx(0.0));
    }
    REQUIRE(gap);
}

TEST_CASE("A multiline in the drawing: drawn, listed, moved, mirrored, scaled and exploded") {
    GeometryEngine engine;
    engine.start();
    engine.submit(wall({{0, 0}, {100, 0}, {100, 50}}, 20.0));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() >= 8; }));
    REQUIRE(engine.snapshot().bounds_min.y == Approx(-20.0)); // Top: the wall below the path
    REQUIRE(engine.snapshot().bounds_max.x == Approx(120.0));
    io::Document doc = dump(engine, "musacad_mline_1.musa");
    REQUIRE(doc.mlines.size() == 1);
    REQUIRE(doc.mlines[0].scale == Approx(20.0));

    engine.submit(SelectAllCommand{});
    engine.submit(MoveSelectionCommand{{10, 5}, 2});
    doc = dump(engine, "musacad_mline_2.musa");
    REQUIRE(doc.mlines[0].vertices[0] == Vec2{10, 5});
    engine.submit(SelectAllCommand{});
    engine.submit(MirrorSelectionCommand{{0, 0}, {1, 0}, true, 3});
    doc = dump(engine, "musacad_mline_3.musa");
    REQUIRE(doc.mlines[0].vertices[0] == Vec2{10, -5});
    REQUIRE(doc.mlines[0].scale == Approx(-20.0)); // the wall mirrored with its path
    engine.submit(SelectAllCommand{});
    engine.submit(ScaleSelectionCommand{{10, -5}, 0.5, 4});
    doc = dump(engine, "musacad_mline_4.musa");
    REQUIRE(doc.mlines[0].scale == Approx(-10.0));

    engine.submit(SelectAllCommand{});
    engine.submit(ExplodeSelectionCommand{5});
    doc = dump(engine, "musacad_mline_5.musa");
    REQUIRE(doc.mlines.empty());
    REQUIRE(doc.lines.size() == 4);
    engine.stop();
}

TEST_CASE("MLINE asks for its points and options as AutoCAD does") {
    H h;
    h.run({"MLINE"});
    REQUIRE(h.out.lines.back() == "Current settings: Justification = Top, Scale = 20.00, Style = STANDARD");
    REQUIRE(h.out.prompt == "Specify start point or [Justification/Scale/STyle]: ");
    h.run({"J"});
    REQUIRE(h.out.prompt == "Enter justification type [Top/Zero/Bottom] <top>: ");
    h.run({"Z"});
    REQUIRE(h.out.lines.back() == "Current settings: Justification = Zero, Scale = 20.00, Style = STANDARD");
    REQUIRE(h.last<MLineStyleCommand>()->op == MLineStyleCommand::Op::Justification);
    h.run({"S"});
    REQUIRE(h.out.prompt == "Enter mline scale <20.00>: ");
    h.run({"10"});
    h.run({"0,0"});
    REQUIRE(h.out.prompt == "Specify next point: ");
    REQUIRE(h.proc.preview().mline_offsets == std::vector<double>{5.0, -5.0});
    h.run({"100,0"});
    REQUIRE(h.out.prompt == "Specify next point or [Undo]: ");
    h.run({"100,50"});
    REQUIRE(h.out.prompt == "Specify next point or [Close/Undo]: ");
    h.run({"U"});
    REQUIRE(h.out.prompt == "Specify next point or [Undo]: ");
    h.run({"100,60", "0,60", "C"});
    const auto* c = h.last<AddMLineCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->closed);
    REQUIRE(c->points.size() == 4);
    REQUIRE(c->scale == Approx(10.0));
    REQUIRE(c->justification == 1);
    REQUIRE_FALSE(h.proc.has_active_command());
    // Enter after two points draws it; after one, nothing.
    h.cmds.clear();
    h.run({"ML", "0,0", ""});
    REQUIRE(h.last<AddMLineCommand>() == nullptr);
    h.run({"ML", "0,0", "10,0", ""});
    REQUIRE(h.last<AddMLineCommand>() != nullptr);
}

TEST_CASE("MLEDIT cuts, welds, adds and deletes vertices") {
    GeometryEngine engine;
    engine.start();
    engine.submit(wall({{0, 0}, {100, 0}}, 10.0, 1)); // Zero: lines at y 5 and -5
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    MLineEditCommand cut;
    cut.op = MLineEditCommand::Op::CutAll;
    cut.pick = {20, 5};
    cut.pick2 = {40, 5};
    cut.pick_radius = 1.0;
    cut.group = 2;
    engine.submit(cut);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Multiline cut."; }));
    io::Document doc = dump(engine, "musacad_mledit_1.musa");
    REQUIRE(doc.mlines[0].breaks.size() == 1);
    REQUIRE(doc.mlines[0].breaks[0][0] == std::vector<double>{20.0, 40.0});
    REQUIRE(doc.mlines[0].breaks[0][1] == std::vector<double>{20.0, 40.0});
    MLineEditCommand weld = cut;
    weld.op = MLineEditCommand::Op::WeldAll;
    weld.pick = {15, -5}; // on the multiline, then the point the weld runs to
    weld.pick2 = {30, -5};
    weld.group = 3;
    engine.submit(weld);
    doc = dump(engine, "musacad_mledit_2.musa");
    REQUIRE(doc.mlines[0].breaks[0][0] == std::vector<double>{30.0, 40.0});
    REQUIRE(doc.mlines[0].breaks[0][1] == std::vector<double>{30.0, 40.0});
    MLineEditCommand add;
    add.op = MLineEditCommand::Op::AddVertex;
    add.pick = {50, 5};
    add.pick_radius = 1.0;
    add.group = 4;
    engine.submit(add);
    doc = dump(engine, "musacad_mledit_3.musa");
    REQUIRE(doc.mlines[0].vertices.size() == 3);
    REQUIRE(doc.mlines[0].vertices[1] == Vec2{50, 0});
    REQUIRE(doc.mlines[0].breaks.size() == 1); // the gaps stay on the first part, the second has none
    REQUIRE(doc.mlines[0].breaks[0][0] == std::vector<double>{30.0, 40.0});
    MLineEditCommand del = add;
    del.op = MLineEditCommand::Op::DeleteVertex;
    del.pick = {51, 5};
    del.group = 5;
    engine.submit(del);
    doc = dump(engine, "musacad_mledit_4.musa");
    REQUIRE(doc.mlines[0].vertices.size() == 2);
    engine.stop();
}

TEST_CASE("MLEDIT's crosses and tees open the multilines where they meet") {
    GeometryEngine engine;
    engine.start();
    engine.submit(wall({{0, 0}, {100, 0}}, 10.0, 1, 1));    // across, y 5 and -5
    engine.submit(wall({{50, -50}, {50, 50}}, 10.0, 1, 2)); // up, x 45 and 55
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));
    MLineEditCommand cross;
    cross.op = MLineEditCommand::Op::ClosedCross;
    cross.pick = {20, 5};   // the first: cut
    cross.pick2 = {45, 30}; // the second: stays
    cross.pick_radius = 1.0;
    cross.group = 3;
    engine.submit(cross);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Closed cross made."; }));
    io::Document doc = dump(engine, "musacad_mledit_5.musa");
    const auto across = [&]() -> const io::DocMLine& {
        return doc.mlines[0].vertices[0] == Vec2{0, 0} ? doc.mlines[0] : doc.mlines[1];
    };
    const auto up = [&]() -> const io::DocMLine& {
        return doc.mlines[0].vertices[0] == Vec2{0, 0} ? doc.mlines[1] : doc.mlines[0];
    };
    REQUIRE(across().breaks.size() == 1);
    for (const std::vector<double>& g : across().breaks[0]) { // both lines open across the other
        REQUIRE(g.size() == 2);
        REQUIRE(g[0] == Approx(45.0));
        REQUIRE(g[1] == Approx(55.0));
    }
    REQUIRE(across().breaks[0].size() == 2);
    REQUIRE(up().breaks.empty());
    engine.stop();

    // A tee: the stem runs from the left up to the bar's near line, and with Open the bar's
    // near line opens where the stem meets it.
    GeometryEngine e2;
    e2.start();
    e2.submit(wall({{0, 0}, {40, 0}}, 10.0, 1, 1));     // the stem, short of the bar
    e2.submit(wall({{60, -50}, {60, 50}}, 10.0, 1, 2)); // the bar, x 55 and 65
    REQUIRE(wait_until(e2, [](const auto& s) { return s.line_vertices.size() == 8; }));
    MLineEditCommand tee;
    tee.op = MLineEditCommand::Op::OpenTee;
    tee.pick = {20, 5};
    tee.pick2 = {55, 30};
    tee.pick_radius = 1.0;
    tee.group = 3;
    e2.submit(tee);
    REQUIRE(wait_until(e2, [](const auto& s) { return s.status == "Open tee made."; }));
    doc = dump(e2, "musacad_mledit_6.musa");
    const io::DocMLine& stem = doc.mlines[0].vertices.size() == 2 && doc.mlines[0].vertices[0] == Vec2{0, 0}
                                   ? doc.mlines[0]
                                   : doc.mlines[1];
    const io::DocMLine& bar = &stem == &doc.mlines[0] ? doc.mlines[1] : doc.mlines[0];
    REQUIRE(stem.vertices[1].x == Approx(55.0)); // extended to the bar's near line
    REQUIRE(bar.breaks.size() == 1);
    bool opened = false;
    for (const std::vector<double>& g : bar.breaks[0]) {
        opened = opened || (g.size() == 2 && g[0] == Approx(45.0) && g[1] == Approx(55.0));
    }
    REQUIRE(opened);
    e2.stop();
}

TEST_CASE("-MLEDIT asks for its tool and the multilines, Enter ending it") {
    H h;
    h.run({"-MLEDIT"});
    REQUIRE(h.out.prompt == "Enter mline editing option [CC/OC/MC/CT/OT/MT/CJ/AV/DV/CS/CA/WA]: ");
    h.run({"CS"});
    REQUIRE(h.out.prompt == "Select mline: ");
    h.run({"10,0"});
    REQUIRE(h.out.prompt == "Select second point: ");
    h.run({"20,0"});
    const auto* c = h.last<MLineEditCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->op == MLineEditCommand::Op::CutSingle);
    REQUIRE(c->pick == Vec2{10, 0});
    REQUIRE(c->pick2 == Vec2{20, 0});
    REQUIRE(h.out.prompt == "Select mline or [Undo]: ");
    h.run({"U"});
    REQUIRE(h.last<UndoLastGroupCommand>() != nullptr);
    h.run({""});
    REQUIRE_FALSE(h.proc.has_active_command());
    h.run({"-MLEDIT", "OT"});
    REQUIRE(h.out.prompt == "Select first mline: ");
    h.run({"0,0"});
    REQUIRE(h.out.prompt == "Select second mline: ");
}

TEST_CASE("Multilines and their styles kept in the drawing file and in DXF") {
    io::Document doc;
    MLineStyle wall_style;
    wall_style.name = "WALL";
    wall_style.description = "two leaves and a cavity";
    wall_style.flags = MLineStyle::kStartLine | MLineStyle::kEndLine;
    wall_style.elements = {MLineElement{0.5}, MLineElement{0.0, 0, Rgb{255, 0, 0}, 0, Linetype::Dashed},
                           MLineElement{-0.5}};
    doc.mline_styles.push_back(wall_style);
    doc.current_mline_style = 1;
    doc.cmljust = 1;
    doc.cmlscale = 250.0;
    io::DocMLine m;
    m.vertices = {{0, 0}, {1000, 0}, {1000, 500}};
    m.style = 1;
    m.scale = 250.0;
    m.justification = 1;
    m.breaks = {{{100.0, 200.0}}}; // the first element of the first segment
    doc.mlines.push_back(m);

    io::Document back;
    REQUIRE(io::parse_native(io::serialize_native(doc), back).ok);
    REQUIRE(back.mline_styles == doc.mline_styles);
    REQUIRE(back.mlines == doc.mlines);
    REQUIRE(back.current_mline_style == 1);
    REQUIRE(back.cmljust == 1);
    REQUIRE(back.cmlscale == Approx(250.0));

    io::Document dxf;
    REQUIRE(io::parse_dxf(io::serialize_dxf(doc), dxf).ok);
    REQUIRE(dxf.mline_styles.size() == 2);
    REQUIRE(dxf.mline_styles[1].name == "WALL");
    REQUIRE(dxf.mline_styles[1].flags == wall_style.flags);
    REQUIRE(dxf.mline_styles[1].elements.size() == 3);
    REQUIRE(dxf.mline_styles[1].elements[1].color_mode == 0);
    REQUIRE(dxf.mline_styles[1].elements[1].linetype == Linetype::Dashed);
    REQUIRE(dxf.mlines.size() == 1);
    REQUIRE(dxf.mlines[0].style == 1);
    REQUIRE(dxf.mlines[0].vertices == m.vertices);
    REQUIRE(dxf.mlines[0].scale == Approx(250.0));
    REQUIRE(dxf.mlines[0].justification == 1);
    REQUIRE(dxf.mlines[0].breaks.size() >= 1);
    REQUIRE(dxf.mlines[0].breaks[0][0] == std::vector<double>{100.0, 200.0});
    REQUIRE(dxf.current_mline_style == 1);
    REQUIRE(dxf.cmlscale == Approx(250.0));
}

TEST_CASE("MLSTYLE's styles and MLINE's settings reach the drawing") {
    GeometryEngine engine;
    engine.start();
    MLineStyle st;
    st.name = "Cavity";
    st.elements = {MLineElement{0.5}, MLineElement{0.1}, MLineElement{-0.5}};
    MLineStyleCommand define;
    define.op = MLineStyleCommand::Op::Define;
    define.style = st;
    engine.submit(define);
    MLineStyleCommand current;
    current.op = MLineStyleCommand::Op::SetCurrent;
    current.name = "cavity"; // any case
    engine.submit(current);
    MLineStyleCommand just;
    just.op = MLineStyleCommand::Op::Justification;
    just.justification = 2;
    engine.submit(just);
    MLineStyleCommand scale;
    scale.op = MLineStyleCommand::Op::Scale;
    scale.scale = 300.0;
    engine.submit(scale);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.mline.scale == 300.0; }));
    const MLineSettings& ms = engine.snapshot().mline;
    REQUIRE(ms.styles.size() == 2);
    REQUIRE(ms.current == 1);
    REQUIRE(ms.justification == 2);
    // STANDARD and the current style cannot be deleted; a style in no use can.
    MLineStyleCommand del;
    del.op = MLineStyleCommand::Op::Delete;
    del.name = "Cavity";
    engine.submit(del);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("cannot be deleted") != std::string::npos; }));
    engine.stop();
}

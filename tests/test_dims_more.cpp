// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Dimensions, the rest of #56 and #57: the text Angle option, DIMARC Partial and Leader,
// DIMCONTINUE / DIMBASELINE Select / Undo / Offset with angular and ordinate chains, and
// DIM with its options (aliGn, Distribute, Layer).

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"

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

/// The engine's drawing saved and read back: how a test sees the dimensions it made.
io::Document dump(GeometryEngine& engine, const char* name) {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("Saved", 0) == 0; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    std::filesystem::remove(p);
    return doc;
}

AddDimensionCommand linear_dim(Vec2 a, Vec2 b, Vec2 at, std::uint64_t group) {
    AddDimensionCommand d;
    d.type = static_cast<std::uint8_t>(DimType::Linear);
    d.a = a;
    d.b = b;
    d.line_pt = at;
    d.group = group;
    return d;
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
    template <class T>
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
    template <class T>
    [[nodiscard]] std::size_t count() const {
        std::size_t n = 0;
        for (const auto& c : cmds) {
            n += std::holds_alternative<T>(c) ? 1 : 0;
        }
        return n;
    }
};
} // namespace

// ---------------------------------------------------------------------------
// #56: the text angle, DIMARC Partial and Leader
// ---------------------------------------------------------------------------

TEST_CASE("The Angle option: a dimension's text at its own angle, kept in the drawing and in DXF") {
    DimStyle st;
    DimData d;
    d.type = DimType::Aligned;
    d.a = {0, 0};
    d.b = {10, 10};
    d.line_pt = {0, 10};
    const double along = compute_dim_geometry(d, st, Rgb{}).text_rotation;
    REQUIRE(along == Approx(std::atan2(1.0, 1.0)).margin(1e-9)); // along the dimension line
    d.text_angle = 0.3;
    REQUIRE(compute_dim_geometry(d, st, Rgb{}).text_rotation == Approx(0.3));

    io::Document doc;
    io::DocDim dd;
    dd.type = static_cast<std::uint8_t>(DimType::Linear);
    dd.a = {0, 0};
    dd.b = {10, 0};
    dd.line_pt = {5, 5};
    dd.text_angle = 0.7;
    doc.dims.push_back(dd);
    io::Document back;
    REQUIRE(io::parse_native(io::serialize_native(doc), back).ok);
    REQUIRE(back.format_version == 39);
    REQUIRE(back.dims.size() == 1);
    REQUIRE(back.dims[0].text_angle == Approx(0.7));

    io::Document from_dxf;
    REQUIRE(io::parse_dxf(io::serialize_dxf(doc), from_dxf).ok);
    REQUIRE(from_dxf.dims.size() == 1);
    REQUIRE(from_dxf.dims[0].text_angle == Approx(0.7).margin(1e-6));
}

TEST_CASE("DIMARC Partial dimensions the part of the arc between two points; Leader draws in to the arc") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddArcCommand{{0, 0}, 10.0, 0.0, kHalfPi, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 4; }));
    AddObjectDimensionCommand c;
    c.type = static_cast<std::uint8_t>(DimType::ArcLength);
    c.pick1 = {7.07, 7.07};
    c.pick2 = {14, 14};
    c.pick_radius = 1.0;
    c.group = 2;
    c.partial_from = Vec2{20.0 * std::cos(kPi / 3.0), 20.0 * std::sin(kPi / 3.0)}; // 60 degrees, off the arc
    c.partial_to = Vec2{10.0 * std::cos(kPi / 6.0), 10.0 * std::sin(kPi / 6.0)};   // 30 degrees
    c.arc_leader = true;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("Dimension created") != std::string::npos; }));
    const io::Document doc = dump(engine, "musacad_dims_more_partial.musa");
    REQUIRE(doc.dims.size() == 1);
    const io::DocDim& d = doc.dims[0];
    REQUIRE(d.arc_leader);
    DimData dd;
    dd.type = DimType::ArcLength;
    dd.a = d.a;
    dd.b = d.b;
    dd.aux = d.aux;
    dd.line_pt = d.line_pt;
    REQUIRE(dd.b.x == Approx(10.0 * std::cos(kPi / 6.0)).margin(1e-6)); // from 30 degrees ...
    REQUIRE(dd.aux == Approx(kPi / 3.0).margin(1e-9));                 // ... to 60
    REQUIRE(dim_measure(dd) == Approx(10.0 * kPi / 6.0).margin(1e-6));

    // The leader: a dimension line from the dimension arc in to the arc, at the middle.
    dd.arc_leader = true;
    const DimGeometry g = compute_dim_geometry(dd, DimStyle{}, Rgb{});
    const Vec2 tip{10.0 * std::cos(kPi / 4.0), 10.0 * std::sin(kPi / 4.0)};
    bool reaches = false;
    for (const Vec2& v : g.dim_lines) {
        reaches = reaches || distance(v, tip) < 1e-6;
    }
    REQUIRE(reaches);
    engine.stop();
}

TEST_CASE("The Angle option at the placement prompts, and DIMARC's Partial and Leader prompts") {
    H h;
    h.proc.submit_line("DIMLINEAR");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,0");
    h.proc.submit_line("A");
    REQUIRE(h.out.prompt == "Specify angle of dimension text: ");
    h.proc.submit_line("abc");
    REQUIRE(h.out.lines.back() == "Requires an angle in degrees.");
    h.proc.submit_line("45");
    h.proc.submit_line("5,5");
    REQUIRE(h.last<AddDimensionCommand>()->text_angle == Approx(kPi / 4.0));

    h.proc.submit_line("DIMRADIUS");
    h.proc.submit_line("10,0");
    h.proc.submit_line("A");
    h.proc.submit_line("30");
    h.proc.submit_line("20,0");
    REQUIRE(h.last<AddObjectDimensionCommand>()->text_angle == Approx(kPi / 6.0));

    h.proc.submit_line("DIMARC");
    h.proc.submit_line("7,7");
    REQUIRE(h.out.prompt == "Specify arc length dimension location, or [Mtext/Text/Angle/Partial/Leader]: ");
    h.proc.submit_line("L");
    REQUIRE(h.out.prompt == "Specify arc length dimension location, or [Mtext/Text/Angle/Partial/No leader]: ");
    h.proc.submit_line("P");
    REQUIRE(h.out.prompt == "Specify first point for arc length dimension: ");
    h.proc.submit_line("10,0");
    REQUIRE(h.out.prompt == "Specify second point for arc length dimension: ");
    h.proc.submit_line("0,10");
    h.proc.submit_line("12,12");
    const auto* arc = h.last<AddObjectDimensionCommand>();
    REQUIRE(arc->arc_leader);
    REQUIRE(arc->partial_from.has_value());
    REQUIRE(arc->partial_to.has_value());
    REQUIRE_FALSE(h.proc.has_active_command());
}

// ---------------------------------------------------------------------------
// #57: DIMCONTINUE / DIMBASELINE
// ---------------------------------------------------------------------------

TEST_CASE("DIMCONTINUE Select: go on from the extension line nearer the pick; Undo comes back to it") {
    GeometryEngine engine;
    engine.start();
    engine.submit(linear_dim({0, 0}, {10, 0}, {5, 5}, 1));
    engine.submit(linear_dim({20, 0}, {30, 0}, {25, 5}, 2)); // drawn last
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    ChainDimensionCommand start;
    start.op = ChainDimensionCommand::Op::Start;
    engine.submit(start);
    ChainDimensionCommand sel;
    sel.op = ChainDimensionCommand::Op::Select;
    sel.at = {0, 3}; // the first dimension's first extension line
    sel.pick_radius = 1.0;
    engine.submit(sel);
    engine.submit(ChainDimensionCommand{{-10, 0}, false, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Continued dimension added."; }));
    io::Document doc = dump(engine, "musacad_dims_more_chain1.musa");
    REQUIRE(doc.dims.size() == 3);
    REQUIRE(doc.dims[2].a == Vec2{0, 0});
    REQUIRE(doc.dims[2].b == Vec2{-10, 0});

    // Undo, and Back: the next one goes on from the selected extension line again.
    engine.submit(UndoLastGroupCommand{});
    ChainDimensionCommand back;
    back.op = ChainDimensionCommand::Op::Back;
    engine.submit(back);
    engine.submit(ChainDimensionCommand{{-20, 0}, false, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Continued dimension added."; }));
    doc = dump(engine, "musacad_dims_more_chain2.musa");
    REQUIRE(doc.dims.size() == 3);
    REQUIRE(doc.dims[2].a == Vec2{0, 0});
    REQUIRE(doc.dims[2].b == Vec2{-20, 0});
    engine.stop();
}

TEST_CASE("DIMBASELINE Offset spaces the stack; angular and ordinate dimensions chain too") {
    GeometryEngine engine;
    engine.start();
    engine.submit(linear_dim({0, 0}, {10, 0}, {5, 5}, 1));
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    ChainDimensionCommand b{{30, 0}, true, 2};
    b.spacing = 10.0;
    engine.submit(b);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Baseline dimension added."; }));
    io::Document doc = dump(engine, "musacad_dims_more_base.musa");
    REQUIRE(doc.dims.size() == 2);
    REQUIRE(doc.dims[1].a == Vec2{0, 0});
    REQUIRE(doc.dims[1].line_pt.y == Approx(15.0));

    // Angular: from the second ray on, the same arc; baseline: from the first ray, further out.
    AddDimensionCommand ang;
    ang.type = static_cast<std::uint8_t>(DimType::Angular);
    ang.a = {100, 0};
    ang.b = {110, 0};
    ang.line_pt = {100, 10};
    ang.aux = 5.0;
    ang.group = 3;
    engine.submit(ang);
    engine.submit(ChainDimensionCommand{{90, 0}, false, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Continued dimension added."; }));
    engine.submit(ChainDimensionCommand{{90, 10}, true, 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Baseline dimension added."; }));
    // Ordinate: the next feature, the leader ending level with the last.
    AddDimensionCommand ord;
    ord.type = static_cast<std::uint8_t>(DimType::Ordinate);
    ord.a = {200, 5};
    ord.b = {200, 20};
    ord.line_pt = {200, 20};
    ord.aux = 0.0; // X datum
    ord.group = 6;
    engine.submit(ord);
    engine.submit(ChainDimensionCommand{{215, 7}, false, 7});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Continued dimension added."; }));

    doc = dump(engine, "musacad_dims_more_kinds.musa");
    REQUIRE(doc.dims.size() == 7);
    const io::DocDim& cont = doc.dims[3];
    REQUIRE(cont.type == static_cast<std::uint8_t>(DimType::Angular));
    REQUIRE(cont.b == Vec2{100, 10});
    REQUIRE(cont.line_pt == Vec2{90, 0});
    REQUIRE(cont.aux == Approx(5.0));
    const io::DocDim& base = doc.dims[4];
    REQUIRE(base.aux > 5.0); // a spacing further out
    const io::DocDim& o2 = doc.dims[6];
    REQUIRE(o2.type == static_cast<std::uint8_t>(DimType::Ordinate));
    REQUIRE(o2.a == Vec2{215, 7});
    REQUIRE(o2.b == Vec2{215, 20});
    engine.stop();
}

TEST_CASE("DIMCONTINUE and DIMBASELINE: the prompts, Select, Offset and Undo") {
    H h;
    h.proc.submit_line("DIMBASELINE");
    REQUIRE(h.last<ChainDimensionCommand>()->op == ChainDimensionCommand::Op::Start);
    REQUIRE(h.out.prompt == "Specify a second extension line origin or [Select/Offset/Undo] <Select>: ");
    h.proc.submit_line("O");
    REQUIRE(h.out.prompt.rfind("Specify the baseline offset distance <", 0) == 0);
    h.proc.submit_line("8");
    h.proc.submit_line("50,0");
    REQUIRE(h.last<ChainDimensionCommand>()->spacing == Approx(8.0));
    h.proc.submit_line("U");
    REQUIRE(std::holds_alternative<ChainDimensionCommand>(h.cmds.back()));
    REQUIRE(h.last<ChainDimensionCommand>()->op == ChainDimensionCommand::Op::Back);
    REQUIRE(h.count<UndoLastGroupCommand>() == 1);
    h.proc.submit_line("U");
    REQUIRE(h.out.lines.back() == "Nothing to undo.");
    h.proc.submit_line("");
    REQUIRE(h.out.prompt == "Select base dimension: ");
    h.proc.submit_line("5,5");
    REQUIRE(h.last<ChainDimensionCommand>()->op == ChainDimensionCommand::Op::Select);
    h.proc.submit_line("");
    h.proc.submit_line("");
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.submit_line("DIMCONTINUE");
    REQUIRE(h.out.prompt == "Specify a second extension line origin or [Select/Undo] <Select>: ");
    h.proc.submit_line("S");
    REQUIRE(h.out.prompt == "Select continued dimension: ");
    h.proc.cancel();
}

// ---------------------------------------------------------------------------
// #57: DIM
// ---------------------------------------------------------------------------

TEST_CASE("DIM: a line, then a second line for the angle; Angular, Continue and Undo inside DIM") {
    H h;
    h.proc.submit_line("DIM");
    REQUIRE(h.out.prompt == "Select objects or specify first extension line origin or "
                            "[Angular/Baseline/Continue/Ordinate/aliGn/Distribute/Layer/Undo]: ");
    h.proc.set_hovered_kind(EntityKind::Line);
    h.proc.pick_point({5, 0}, std::nullopt);
    REQUIRE(h.out.prompt == "Specify dimension line location or second line for angle [Mtext/Text/Angle]: ");
    h.proc.pick_point({0, 5}, std::nullopt); // another line: the angle between them
    REQUIRE(h.out.prompt == "Specify dimension arc line location or [Mtext/Text/Angle/Quadrant]: ");
    h.proc.set_hovered_kind(std::nullopt);
    h.proc.pick_point({4, 4}, std::nullopt);
    const auto* ang = h.last<AddObjectDimensionCommand>();
    REQUIRE(ang != nullptr);
    REQUIRE(ang->type == static_cast<std::uint8_t>(DimType::Angular));
    REQUIRE(h.proc.has_active_command()); // back at DIM's prompt
    REQUIRE(h.out.prompt.rfind("Select objects or specify first extension line origin", 0) == 0);

    h.proc.submit_line("C"); // DIMCONTINUE inside DIM
    REQUIRE(h.out.prompt == "Specify a second extension line origin or [Select/Undo] <Select>: ");
    h.proc.submit_line("");
    h.proc.submit_line(""); // Enter at Select: back to DIM
    REQUIRE(h.out.prompt.rfind("Select objects or specify first extension line origin", 0) == 0);

    h.proc.submit_line("U");
    REQUIRE(h.count<UndoLastGroupCommand>() == 1);
    h.proc.submit_line("A");
    REQUIRE(h.out.prompt == "Select arc, circle, line, or <specify vertex>: ");
    h.proc.cancel();
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("DIM aliGn, Distribute and Layer") {
    H h;
    h.proc.submit_line("DIM");
    h.proc.submit_line("G");
    REQUIRE(h.out.prompt == "Select base dimension: ");
    h.proc.submit_line("5,5");
    REQUIRE(h.out.prompt == "Select dimensions to align: ");
    REQUIRE(h.proc.in_selection_phase());
    h.proc.set_selection_count(2);
    h.proc.submit_line("");
    const auto* al = h.last<DimArrangeCommand>();
    REQUIRE(al != nullptr);
    REQUIRE(al->op == DimArrangeCommand::Op::Align);
    REQUIRE(al->base_pick == Vec2{5, 5});

    h.proc.submit_line("D");
    REQUIRE(h.out.prompt == "Specify method of distribution [Equal/Offset] <Equal>: ");
    h.proc.submit_line("O");
    h.proc.submit_line("6");
    REQUIRE(h.out.prompt == "Select base dimension: ");
    h.proc.submit_line("1,1");
    h.proc.submit_line("");
    const auto* dist = h.last<DimArrangeCommand>();
    REQUIRE(dist->op == DimArrangeCommand::Op::DistributeOffset);
    REQUIRE(dist->offset == Approx(6.0));

    h.proc.submit_line("L");
    REQUIRE(h.out.prompt == "Enter layer name or select object <use current>: ");
    h.proc.submit_line("Dims");
    REQUIRE(h.last<SetDimLayerCommand>()->name == "Dims");
    h.proc.submit_line("");
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("Aligning and distributing dimensions moves their lines; DIMLAYER puts new dimensions on a layer") {
    GeometryEngine engine;
    engine.start();
    engine.submit(linear_dim({0, 0}, {10, 0}, {5, 5}, 1));    // the base
    engine.submit(linear_dim({20, 0}, {30, 0}, {25, 9}, 2));  // parallel, further out
    engine.submit(linear_dim({40, 0}, {50, 0}, {45, 30}, 3)); // parallel
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(SelectWindowCommand{{15, -1}, {55, 40}, true, false, false}); // the two others
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(DimArrangeCommand{DimArrangeCommand::Op::Align, {0, 3}, 1.0, 0.0, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("2 dimensions aligned") != std::string::npos; }));
    io::Document doc = dump(engine, "musacad_dims_more_align.musa");
    REQUIRE(doc.dims.size() == 3);
    for (const io::DocDim& d : doc.dims) {
        REQUIRE(d.line_pt.y == Approx(5.0));
    }
    engine.submit(UndoLastGroupCommand{});

    // Three stacked: Equal spaces the middle one halfway.
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 3; }));
    engine.submit(DimArrangeCommand{DimArrangeCommand::Op::DistributeEqual, {}, 0.0, 0.0, 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("1 dimension distributed") != std::string::npos; }));
    doc = dump(engine, "musacad_dims_more_dist.musa");
    bool middle = false;
    for (const io::DocDim& d : doc.dims) {
        middle = middle || std::abs(d.line_pt.y - 17.5) < 1e-6;
    }
    REQUIRE(middle);

    // DIMLAYER: new dimensions on layer "Dims"; "." back to the current layer.
    Layer dims;
    dims.name = "Dims";
    engine.submit(AddLayerCommand{dims});
    engine.submit(SetDimLayerCommand{"dims"});
    engine.submit(linear_dim({0, 50}, {10, 50}, {5, 55}, 6));
    engine.submit(SetDimLayerCommand{"."});
    engine.submit(linear_dim({0, 60}, {10, 60}, {5, 65}, 7));
    engine.submit(SetDimLayerCommand{"nope"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Layer \"nope\" not found."; }));
    doc = dump(engine, "musacad_dims_more_layer.musa");
    REQUIRE(doc.dims.size() == 5);
    REQUIRE(doc.dims[3].props.layer == 1);
    REQUIRE(doc.dims[4].props.layer == 0);
    engine.stop();
}

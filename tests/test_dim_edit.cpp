// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Editing dimensions already drawn (#60): DIMEDIT (Home / New / Rotate / Oblique),
// DIMTEDIT (move, Left / Right / Center / Home / Angle), DIMSPACE, DIMCENTER and
// DIMOVERRIDE.

#include <algorithm>
#include <chrono>
#include <cstdint>
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

AddDimensionCommand linear_dim(Vec2 a, Vec2 b, Vec2 at, std::uint64_t group) {
    AddDimensionCommand d;
    d.type = static_cast<std::uint8_t>(DimType::Linear);
    d.a = a;
    d.b = b;
    d.line_pt = at;
    d.group = group;
    return d;
}

bool status_has(const RenderSnapshot& s, const char* what) { return s.status.find(what) != std::string::npos; }

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
};
} // namespace

TEST_CASE("Oblique extension lines: the feet move along the line, the measurement stays") {
    DimStyle st;
    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {10, 0};
    d.line_pt = {5, 5};
    const DimGeometry square = compute_dim_geometry(d, st, Rgb{});
    d.oblique = kPi / 4.0;
    const DimGeometry slant = compute_dim_geometry(d, st, Rgb{});
    REQUIRE(slant.label == square.label); // still 10
    REQUIRE(slant.ext_lines.size() == 4);
    for (std::size_t i = 0; i < 4; i += 2) {
        const Vec2 v = slant.ext_lines[i + 1] - slant.ext_lines[i];
        REQUIRE(std::atan2(v.y, v.x) == Approx(kPi / 4.0).margin(1e-9));
    }
    // A slant along the dimension line itself cannot meet it: square again.
    d.oblique = kPi;
    REQUIRE(compute_dim_geometry(d, st, Rgb{}).ext_lines == square.ext_lines);

    io::Document doc;
    io::DocDim dd;
    dd.type = static_cast<std::uint8_t>(DimType::Aligned);
    dd.a = {0, 0};
    dd.b = {10, 0};
    dd.line_pt = {5, 5};
    dd.oblique = 1.1;
    dd.text_angle = 0.4;
    doc.dims.push_back(dd);
    io::Document back;
    REQUIRE(io::parse_native(io::serialize_native(doc), back).ok);
    REQUIRE(back.dims.size() == 1);
    REQUIRE(back.dims[0].oblique == Approx(1.1));
    REQUIRE(back.dims[0].text_angle == Approx(0.4));
    io::Document from_dxf;
    REQUIRE(io::parse_dxf(io::serialize_dxf(doc), from_dxf).ok);
    REQUIRE(from_dxf.dims.size() == 1);
    REQUIRE(from_dxf.dims[0].oblique == Approx(1.1).margin(1e-6));
}

TEST_CASE("DIMOVERRIDE's variables become a dimension's own; colours must be colours of their own") {
    DimOverrides o;
    REQUIRE(apply_dim_override(o, "dimtxt", 5.0));
    REQUIRE(o.has(DimOverrides::kTextHeight));
    REQUIRE(o.text_height == 5.0);
    REQUIRE(apply_dim_override(o, "DIMCLRD", 1.0));
    REQUIRE(o.has(DimOverrides::kDimColor));
    REQUIRE_FALSE(apply_dim_override(o, "DIMCLRE", 256.0)); // ByLayer is the style's to say
    REQUIRE_FALSE(apply_dim_override(o, "DIMEXO", 1.0));    // a style setting only
    REQUIRE_FALSE(apply_dim_override(o, "DIMDEC", 12.0));   // out of range
    REQUIRE_FALSE(o.has(DimOverrides::kExtColor));
}

TEST_CASE("DIMEDIT and DIMTEDIT edit dimensions in the drawing, one undo step each") {
    GeometryEngine engine;
    engine.start();
    engine.submit(linear_dim({0, 0}, {100, 0}, {50, 10}, 1));
    engine.submit(linear_dim({0, 50}, {100, 50}, {50, 60}, 2));
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    // DIMEDIT on the selection: New text, Rotate, Oblique; Home puts the text back.
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(DimEditCommand{DimEditCommand::Op::New, "<> TYP", 0.0, std::nullopt, 0.0, {}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return status_has(s, "2 dimensions edited."); }));
    engine.submit(DimEditCommand{DimEditCommand::Op::Rotate, "", 0.5, std::nullopt, 0.0, {}, 4});
    engine.submit(DimEditCommand{DimEditCommand::Op::Oblique, "", kPi / 3.0, std::nullopt, 0.0, {}, 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    io::Document doc = dump(engine, "musacad_dim_edit_dimedit.musa");
    REQUIRE(doc.dims.size() == 2);
    for (const io::DocDim& d : doc.dims) {
        REQUIRE(d.text_override == "<> TYP");
        REQUIRE(d.text_angle == Approx(0.5));
        REQUIRE(d.oblique == Approx(kPi / 3.0));
    }
    engine.submit(UndoLastGroupCommand{}); // the oblique angle alone
    doc = dump(engine, "musacad_dim_edit_undo.musa");
    REQUIRE(doc.dims[0].oblique == 0.0);
    REQUIRE(doc.dims[0].text_angle == Approx(0.5));

    // DIMTEDIT on the one picked: moved, then Left, then Home.
    engine.submit(DimEditCommand{DimEditCommand::Op::Move, "", 0.0, Vec2{50, 10}, 1.0, {70, 10}, 6});
    REQUIRE(wait_until(engine, [](const auto& s) { return status_has(s, "1 dimension edited."); }));
    doc = dump(engine, "musacad_dim_edit_move.musa");
    bool moved = false;
    for (const io::DocDim& d : doc.dims) {
        moved = moved || d.text_offset.x > 15.0;
    }
    REQUIRE(moved);
    // The dimension line followed the text down a little: picked within 5.
    engine.submit(DimEditCommand{DimEditCommand::Op::Left, "", 0.0, Vec2{50, 10}, 5.0, {}, 7});
    doc = dump(engine, "musacad_dim_edit_left.musa");
    bool left = false;
    for (const io::DocDim& d : doc.dims) {
        left = left || d.text_offset.x < -30.0;
    }
    REQUIRE(left);
    engine.submit(DimEditCommand{DimEditCommand::Op::Home, "", 0.0, Vec2{50, 10}, 5.0, {}, 8});
    doc = dump(engine, "musacad_dim_edit_home.musa");
    for (const io::DocDim& d : doc.dims) {
        REQUIRE(d.text_offset.x == 0.0);
    }
    engine.submit(DimEditCommand{DimEditCommand::Op::Home, "", 0.0, Vec2{50, 30}, 1.0, {}, 9});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "No dimension found."; }));
    engine.stop();
}

TEST_CASE("DIMSPACE Auto spaces at twice the text height; DIMCENTER marks a circle; DIMOVERRIDE and Clear") {
    GeometryEngine engine;
    engine.start();
    engine.submit(linear_dim({0, 0}, {10, 0}, {5, 5}, 1));    // the base
    engine.submit(linear_dim({20, 0}, {30, 0}, {25, 9}, 2));
    engine.submit(linear_dim({40, 0}, {50, 0}, {45, 30}, 3));
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(SelectWindowCommand{{15, -1}, {55, 40}, true, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(DimArrangeCommand{DimArrangeCommand::Op::DistributeOffset, {0, 5}, 1.0, -1.0, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return status_has(s, "2 dimensions distributed"); }));
    io::Document doc = dump(engine, "musacad_dim_edit_space.musa");
    std::vector<double> ys;
    for (const io::DocDim& d : doc.dims) {
        ys.push_back(d.line_pt.y);
    }
    std::sort(ys.begin(), ys.end());
    REQUIRE(ys == std::vector<double>{5.0, 10.0, 15.0}); // 2 x 2.5 apart

    // DIMOVERRIDE on the two spaced ones, then Clear.
    engine.submit(SetDimOverrideCommand{{{"DIMTXT", 4.0}, {"DIMDEC", 0.0}}, false, 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return status_has(s, "Overrides set on 2 dimensions."); }));
    doc = dump(engine, "musacad_dim_edit_override.musa");
    int overridden = 0;
    for (const io::DocDim& d : doc.dims) {
        if (d.overrides.has(DimOverrides::kTextHeight) && d.overrides.text_height == 4.0 &&
            d.overrides.has(DimOverrides::kPrecision) && d.overrides.precision == 0) {
            ++overridden;
        }
    }
    REQUIRE(overridden == 2);
    engine.submit(SetDimOverrideCommand{{{"DIMEXO", 1.0}}, false, 6});
    REQUIRE(wait_until(engine, [](const auto& s) { return status_has(s, "DIMEXO is not"); }));
    engine.submit(SetDimOverrideCommand{{}, true, 7});
    REQUIRE(wait_until(engine, [](const auto& s) { return status_has(s, "Overrides cleared on 2"); }));
    doc = dump(engine, "musacad_dim_edit_clear.musa");
    for (const io::DocDim& d : doc.dims) {
        REQUIRE(d.overrides.mask == 0);
    }

    // DIMCENTER: a cross of two lines, and with Lines four centre lines as well.
    engine.submit(AddCircleCommand{{100, 100}, 20.0, 8});
    engine.submit(AddCenterMarkCommand{{120, 100}, 1.0, 0.0, false, 9});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Center mark added."; }));
    doc = dump(engine, "musacad_dim_edit_center.musa");
    REQUIRE(doc.lines.size() == 2);
    REQUIRE(doc.lines[0].a.x == Approx(98.75)); // half the arrow size out from the centre
    engine.submit(AddCenterMarkCommand{{120, 100}, 1.0, 2.0, true, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Center mark and center lines added."; }));
    doc = dump(engine, "musacad_dim_edit_lines.musa");
    REQUIRE(doc.lines.size() == 8);
    REQUIRE(doc.lines[4].a.x == Approx(104.0)); // a gap past the mark ...
    REQUIRE(doc.lines[4].b.x == Approx(122.0)); // ... to the size beyond the circle
    engine.submit(AddCenterMarkCommand{{0, 0}, 1.0, 0.0, false, 11});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Select an arc or a circle."; }));
    engine.stop();
}

TEST_CASE("The dimension editing prompts") {
    H h;
    h.proc.submit_line("DIMEDIT");
    REQUIRE(h.out.prompt == "Enter type of dimension editing [Home/New/Rotate/Oblique] <Home>: ");
    h.proc.submit_line("O");
    REQUIRE(h.out.prompt == "Enter obliquing angle (press ENTER for none): ");
    h.proc.submit_line("60");
    REQUIRE(h.out.prompt == "Select objects: ");
    h.proc.set_selection_count(1);
    h.proc.submit_line("");
    const auto* ed = h.last<DimEditCommand>();
    REQUIRE(ed != nullptr);
    REQUIRE(ed->op == DimEditCommand::Op::Oblique);
    REQUIRE(ed->angle == Approx(kPi / 3.0));
    REQUIRE_FALSE(ed->pick.has_value());
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.submit_line("DED"); // a selection already there: edited at once
    h.proc.submit_line("N");
    h.proc.submit_line("<> MAX");
    REQUIRE(h.last<DimEditCommand>()->text == "<> MAX");
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.set_selection_count(0);
    h.proc.submit_line("DIMTEDIT");
    REQUIRE(h.out.prompt == "Select dimension: ");
    h.proc.submit_line("50,10");
    REQUIRE(h.out.prompt == "Specify new location for dimension text or [Left/Right/Center/Home/Angle]: ");
    h.proc.submit_line("A");
    h.proc.submit_line("45");
    const auto* te = h.last<DimEditCommand>();
    REQUIRE(te->op == DimEditCommand::Op::Rotate);
    REQUIRE(te->pick == Vec2{50, 10});
    REQUIRE(te->angle == Approx(kPi / 4.0));
    h.proc.submit_line("DIMTEDIT");
    h.proc.submit_line("50,10");
    h.proc.submit_line("70,12");
    REQUIRE(h.last<DimEditCommand>()->op == DimEditCommand::Op::Move);
    REQUIRE(h.last<DimEditCommand>()->to == Vec2{70, 12});

    h.proc.submit_line("DIMSPACE");
    REQUIRE(h.out.prompt == "Select base dimension: ");
    h.proc.submit_line("5,5");
    REQUIRE(h.out.prompt == "Select dimensions to space: ");
    h.proc.set_selection_count(2);
    h.proc.submit_line("");
    REQUIRE(h.out.prompt == "Enter value or [Auto] <Auto>: ");
    h.proc.submit_line("0");
    REQUIRE(h.last<DimArrangeCommand>()->op == DimArrangeCommand::Op::Align);
    h.proc.submit_line("DIMSPACE");
    h.proc.submit_line("5,5");
    h.proc.submit_line("");
    h.proc.submit_line("");
    REQUIRE(h.last<DimArrangeCommand>()->op == DimArrangeCommand::Op::DistributeOffset);
    REQUIRE(h.last<DimArrangeCommand>()->offset < 0.0); // Auto

    h.proc.submit_line("DIMCENTER");
    REQUIRE(h.out.prompt == "Select arc or circle or [Lines] <mark only>: ");
    h.proc.submit_line("L");
    REQUIRE(h.out.prompt == "Select arc or circle or [Lines] <with center lines>: ");
    h.proc.submit_line("120,100");
    REQUIRE(h.last<AddCenterMarkCommand>()->lines);
    h.proc.submit_line("DCE");
    h.proc.submit_line("L"); // back off for the next run
    h.proc.submit_line("");

    h.proc.submit_line("DIMOVERRIDE");
    REQUIRE(h.out.prompt == "Enter dimension variable name to override or [Clear overrides]: ");
    h.proc.submit_line("dimexo");
    REQUIRE(h.out.lines.back() == "DIMEXO is a style setting; a dimension cannot have its own.");
    h.proc.submit_line("DIMTXT");
    REQUIRE(h.out.prompt == "Enter new value for dimension variable <2.5000>: ");
    h.proc.submit_line("5");
    REQUIRE(h.out.prompt == "Enter dimension variable name to override: ");
    h.proc.submit_line("DIMCLRT");
    h.proc.submit_line("256");
    REQUIRE(h.out.lines.back().find("an ACI colour, 1 to 255") != std::string::npos);
    h.proc.submit_line("3");
    h.proc.submit_line("");
    const auto* ov = h.last<SetDimOverrideCommand>(); // the two still selected
    REQUIRE(ov != nullptr);
    REQUIRE(ov->vars.size() == 2);
    REQUIRE(ov->vars[0].first == "DIMTXT");
    REQUIRE_FALSE(ov->clear);
    h.proc.submit_line("DOV");
    h.proc.submit_line("C");
    REQUIRE(h.last<SetDimOverrideCommand>()->clear);
    REQUIRE_FALSE(h.proc.has_active_command());
}

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// CHANGE and HATCHEDIT (#55), MOCORO, FLATTEN, TCOUNT, TEXTFIT, GATTE and VPSCALE (#77), -MTEXT (#43),
// DIMINSPECT (#60), TABLESTYLE (#44), GRADIENT (#41).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
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
#include "musacad/core/gdt.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/spline_eval.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/io/xlsx.hpp"
#include "musacad/core/text/text_codes.hpp"
#include "musacad/core/text/text_frame.hpp"

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
    void run(std::initializer_list<const char*> lines) {
        for (const char* l : lines) {
            proc.submit_line(l);
        }
    }
};

constexpr std::uint32_t bit(EntityKind k) { return 1u << static_cast<unsigned>(k); }
} // namespace

TEST_CASE("CHANGE: a line's nearer end and a circle through the point; texts and blocks by their prompts") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddCircleCommand{{50, 0}, 5, 2});
    AddTextCommand t;
    t.pos = {0, 20};
    t.content = "OLD";
    t.group = 3;
    engine.submit(t);
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 3; }));
    ChangeCommand ch;
    ch.point = Vec2{8, 6};
    ch.text_height = 5.0;
    ch.text_content = std::string("NEW");
    ch.group = 4;
    engine.submit(ch);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "3 objects changed."; }));
    const io::Document doc = dump(engine, "musacad_change.musa");
    REQUIRE(doc.lines[0].a == Vec2{0, 0});
    REQUIRE(doc.lines[0].b == Vec2{8, 6}); // the nearer end
    REQUIRE(doc.circles[0].radius == Approx(std::hypot(42.0, 6.0)));
    REQUIRE(doc.texts[0].content == "NEW");
    REQUIRE(doc.texts[0].height == Approx(5.0));
    engine.stop();

    H h;
    h.proc.set_selection_count(2);
    h.proc.set_selection_kinds(bit(EntityKind::Line) | bit(EntityKind::Text));
    h.run({"CHANGE"});
    REQUIRE(h.out.prompt == "Specify change point or [Properties]: ");
    h.run({"8,6"});
    REQUIRE(h.out.prompt == "Specify new text insertion point <no change>: ");
    h.run({"", ""});
    REQUIRE(h.out.prompt == "Specify new height <no change>: ");
    h.run({"3", "45"});
    REQUIRE(h.out.prompt == "Enter new text <no change>: ");
    REQUIRE(h.proc.wants_free_text());
    h.run({"Two words"});
    const auto* c = h.last<ChangeCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(*c->point == Vec2{8, 6});
    REQUIRE_FALSE(c->text_pos.has_value());
    REQUIRE(*c->text_height == 3.0);
    REQUIRE(*c->text_rotation == Approx(kPi / 4));
    REQUIRE(*c->text_content == "Two words");
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.set_selection_kinds(bit(EntityKind::Line)); // lines only: the point and done
    h.run({"CHANGE", "1,1"});
    REQUIRE_FALSE(h.proc.has_active_command());
    h.proc.set_selection_kinds(bit(EntityKind::Insert));
    h.run({"CHANGE", ""});
    REQUIRE(h.out.prompt == "Specify new block insertion point <no change>: ");
    h.run({"5,5", "90"});
    REQUIRE(*h.last<ChangeCommand>()->block_pos == Vec2{5, 5});
    REQUIRE(*h.last<ChangeCommand>()->block_rotation == Approx(kHalfPi));
}

TEST_CASE("MOCORO moves, rotates and scales the selection in one go; FLATTEN zeroes elevation") {
    H h;
    h.proc.set_selection_count(1);
    h.run({"MOCORO"});
    REQUIRE(h.out.prompt == "Base point: ");
    h.run({"0,0", "10,0"});
    REQUIRE(h.last<MoveSelectionCommand>()->delta == Vec2{10, 0});
    REQUIRE(h.out.prompt == "Second point or [Copy/Base/Rotate/Scale/Undo] <eXit>: ");
    h.run({"R"});
    REQUIRE(h.out.prompt == "Rotation angle or [Copy/Base/Move/Scale/Undo] <eXit>: ");
    h.run({"90"});
    REQUIRE(h.last<RotateSelectionCommand>()->base == Vec2{10, 0}); // the base went with the move
    REQUIRE(h.last<RotateSelectionCommand>()->angle == Approx(kHalfPi));
    h.run({"S", "2"});
    REQUIRE(h.last<ScaleSelectionCommand>()->factor == 2.0);
    h.run({"C", "M", "20,0"});
    REQUIRE(h.last<CopySelectionCommand>()->delta == Vec2{10, 0});
    h.run({"U"});
    REQUIRE(std::holds_alternative<UndoLastGroupCommand>(h.cmds.back()));
    h.run({""});
    REQUIRE_FALSE(h.proc.has_active_command());

    GeometryEngine engine;
    engine.start();
    AddPolylineCommand pl;
    pl.points = {{0, 0}, {10, 0}};
    pl.elevation = 5.0;
    pl.thickness = 2.0;
    pl.group = 1;
    engine.submit(pl);
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(FlattenCommand{2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 object flattened."; }));
    const io::Document doc = dump(engine, "musacad_flatten.musa");
    REQUIRE(doc.polylines[0].elevation == 0.0);
    REQUIRE(doc.polylines[0].thickness == 0.0);
    engine.stop();

    h.run({"FLATTEN"});
    REQUIRE(h.out.prompt == "Remove hidden lines? <No>: ");
    h.run({""});
    REQUIRE(h.last<FlattenCommand>() != nullptr);
}

TEST_CASE("HATCHEDIT: Properties set the pattern, scale and angle; recreate Boundary draws the loops") {
    H h;
    h.run({"HE", "5,5"});
    REQUIRE(h.last<SelectPickCommand>()->world == Vec2{5, 5});
    REQUIRE(h.out.prompt.rfind("Enter hatch option [Disassociate/Style/Properties/", 0) == 0);
    h.run({"", "ANSI37", "2", "30"});
    int props = 0;
    for (const Command& c : h.cmds) {
        if (const auto* p = std::get_if<SetPropertyCommand>(&c)) {
            ++props;
            if (p->id == PropertyId::HatchPattern) {
                REQUIRE(p->value.text == "ANSI37");
            } else if (p->id == PropertyId::HatchScale) {
                REQUIRE(p->value.num == 2.0);
            } else {
                REQUIRE(p->id == PropertyId::HatchAngle);
                REQUIRE(p->value.num == 30.0);
            }
        }
    }
    REQUIRE(props == 3);
    REQUIRE_FALSE(h.proc.has_active_command());
    h.run({"HATCHEDIT", "5,5", "B", "", ""});
    REQUIRE(h.last<HatchBoundaryCommand>() != nullptr);

    GeometryEngine engine;
    engine.start();
    AddHatchCommand hatch;
    hatch.loops = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{3, 3}, {6, 3}, {6, 6}}};
    hatch.pattern_name = "SOLID";
    hatch.group = 1;
    engine.submit(hatch);
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(HatchBoundaryCommand{2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "2 boundaries created."; }));
    const io::Document doc = dump(engine, "musacad_hatchedit.musa");
    REQUIRE(doc.polylines.size() == 2);
    REQUIRE(doc.polylines[0].closed);
    engine.stop();
}

TEST_CASE("-MTEXT takes the text line by line until an empty line") {
    H h;
    h.run({"-MTEXT", "0,0", "40,-20"});
    REQUIRE(h.out.prompt == "MText: ");
    REQUIRE(h.proc.wants_free_text());
    h.run({"First line", "Second line"});
    REQUIRE(h.last<AddMTextCommand>() == nullptr);
    h.run({""});
    REQUIRE(h.last<AddMTextCommand>()->content == "First line\nSecond line");
    REQUIRE_FALSE(h.proc.has_active_command());
}

namespace {
struct ViewOnly : musacad::command::ViewControl {
    void zoom_extents() override {}
    void zoom_scale(double /*f*/) override {}
    bool current_view(Vec2& c, double& s) override {
        c = {12, 34};
        s = 2.5;
        return true;
    }
};
} // namespace

TEST_CASE("NEWVIEW saves the view on screen; VIEW without its window asks at the command line") {
    ViewOnly view;
    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[&cmds](Command c) { cmds.push_back(std::move(c)); }, &view, out};
    proc.submit_line("NEWVIEW");
    REQUIRE(out.prompt == "Enter view name: ");
    proc.submit_line("Front elevation");
    const auto* v = std::get_if<SaveNamedViewCommand>(&cmds.back());
    REQUIRE(v != nullptr);
    REQUIRE(v->view.name == "Front elevation");
    REQUIRE(v->view.center == Vec2{12, 34});
    REQUIRE(v->view.scale == 2.5);
    proc.submit_line("VIEW");
    REQUIRE(out.prompt == "Enter an option [?/Delete/Orthographic/Restore/Save/Ucs/Window]: ");
    proc.submit_line("");
    proc.submit_line("LAYERSTATE");
    REQUIRE(out.prompt == "Enter an option [?/Save/Restore/Delete/Name]: ");
    proc.submit_line("");

    GeometryEngine engine;
    engine.start();
    engine.submit(LayerStateCommand{LayerStateCommand::Op::Save, "Plan", {}});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.layer_state_names.size() == 1 && s.layer_state_names[0] == "Plan";
    }));
    engine.stop();
}

TEST_CASE("DIMJOGLINE puts a jog in a dimension's line where asked, kept with the drawing; Remove takes it away") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear), {0, 0}, {20, 0}, {10, 10}, 0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(DimJogCommand{{5, 10}, 0.5, Vec2{15, 12}, false, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Jog added."; }));
    io::Document doc = dump(engine, "musacad_dimjog.musa");
    REQUIRE(doc.dims[0].jog == Approx(0.75));
    io::Document again;
    REQUIRE(io::parse_native(io::serialize_native(doc), again).ok);
    REQUIRE(again.dims[0].jog == Approx(0.75));

    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {20, 0};
    d.line_pt = {10, 10};
    REQUIRE(compute_dim_geometry(d, DimStyle{}, Rgb{}).dim_lines.size() == 2);
    d.jog = 0.75;
    REQUIRE(compute_dim_geometry(d, DimStyle{}, Rgb{}).dim_lines.size() == 10); // the line and its zigzag

    engine.submit(DimJogCommand{{5, 10}, 0.5, std::nullopt, true, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Jog removed."; }));
    doc = dump(engine, "musacad_dimjog_removed.musa");
    REQUIRE(doc.dims[0].jog < 0.0);
    engine.stop();

    H h;
    h.run({"DJL"});
    REQUIRE(h.out.prompt == "Select dimension to add jog or [Remove]: ");
    h.run({"5,10"});
    REQUIRE(h.out.prompt == "Specify jog location (or press ENTER): ");
    h.run({"15,10"});
    REQUIRE(*h.last<DimJogCommand>()->at == Vec2{15, 10});
    h.run({"DJL", "R", "5,10"});
    REQUIRE(h.last<DimJogCommand>()->remove);
}

TEST_CASE("SPLINEDIT adds, deletes and moves control points, reverses, and makes a polyline") {
    using Op = SplineEditCommand::Op;
    GeometryEngine engine;
    engine.start();
    engine.submit(AddSplineCommand{{{0, 0}, {10, 10}, {20, 0}, {30, 10}}, 3, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    io::Document doc = dump(engine, "musacad_splinedit0.musa");
    // A point on the curve to pick it by: its start.
    const Vec2 on = doc.splines[0].control_points[0];
    engine.submit(SplineEditCommand{Op::Begin, on, 0.5, {}, {}, 10, 0});
    engine.submit(SplineEditCommand{Op::AddVertex, {}, 0.0, {15, 6}, {}, 10, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Control point added."; }));
    engine.submit(SplineEditCommand{Op::MoveVertex, {}, 0.0, {30, 10}, {30, 20}, 10, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Control point moved."; }));
    engine.submit(SplineEditCommand{Op::DeleteVertex, {}, 0.0, {15, 6}, {}, 10, 4});
    engine.submit(SplineEditCommand{Op::Reverse, {}, 0.0, {}, {}, 10, 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Spline reversed."; }));
    doc = dump(engine, "musacad_splinedit1.musa");
    REQUIRE(doc.splines[0].control_points.size() == 4);
    REQUIRE(doc.splines[0].control_points[0] == Vec2{30, 20}); // moved, then reversed to the front
    engine.submit(SplineEditCommand{Op::DeleteVertex, {}, 0.0, {0, 0}, {}, 10, 6});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("keeps 4 control points") != std::string::npos; }));
    engine.submit(SplineEditCommand{Op::ToPolyline, {}, 0.0, {}, {}, 10, 7});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Converted to a polyline."; }));
    doc = dump(engine, "musacad_splinedit2.musa");
    REQUIRE(doc.splines.empty());
    REQUIRE(doc.polylines.size() == 1);
    REQUIRE(doc.polylines[0].points.size() > 10);
    engine.stop();

    H h;
    h.run({"SPE", "1,1"});
    REQUIRE(h.last<SplineEditCommand>()->op == Op::Begin);
    REQUIRE(h.out.prompt ==
            "Enter an option [Close/Join/Fit data/Edit vertex/convert to Polyline/Reverse/Undo/eXit] <eXit>: ");
    h.run({"E", "M", "10,10", "12,14"});
    REQUIRE(h.last<SplineEditCommand>()->op == Op::MoveVertex);
    REQUIRE(h.last<SplineEditCommand>()->p2 == Vec2{12, 14});
    h.run({"A", "5,5", ""});
    REQUIRE(h.out.prompt == "Enter a vertex editing option [Add/Delete/Elevate order/Move/Weight/eXit] <eXit>: ");
    h.run({"", "P", "20"});
    REQUIRE(h.last<SplineEditCommand>()->op == Op::End);
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("BURST keeps the attribute values as text; TEXTMASK puts a wipeout behind text") {
    GeometryEngine engine;
    engine.start();
    // A block with an attribute, inserted with the value "R-12".
    AddAttDefCommand ad;
    ad.text.pos = {0, 0};
    ad.text.height = 2.5;
    ad.text.content = "TAG"; // the tag
    ad.def = "DEFAULT";
    ad.group = 1;
    engine.submit(ad);
    engine.submit(AddLineCommand{{0, -1}, {10, -1}, 2});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(DefineBlockCommand{"B", {0, 0}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.block_names.size() == 1; }));
    engine.submit(SelectAllCommand{});
    engine.submit(EraseSelectionCommand{4});
    InsertBlockCommand ins;
    ins.name = "B";
    ins.pos = {100, 100};
    ins.group = 5;
    ins.attribs = {"R-12"};
    engine.submit(ins);
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(ExplodeSelectionCommand{6, true});
    io::Document doc = dump(engine, "musacad_burst.musa");
    REQUIRE(doc.inserts.empty());
    REQUIRE(doc.lines.size() == 1);
    REQUIRE(doc.texts.size() == 1);
    REQUIRE(doc.texts[0].content == "R-12");
    REQUIRE(doc.attdefs.empty());

    engine.submit(SelectAllCommand{});
    engine.submit(TextMaskCommand{0.5, 7});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 text masked."; }));
    doc = dump(engine, "musacad_textmask.musa");
    REQUIRE(doc.hatches.size() == 1);
    REQUIRE(doc.hatches[0].pattern_name == "WIPEOUT");
    engine.stop();

    H h;
    h.run({"TEXTMASK"});
    REQUIRE(h.out.prompt == "Select text objects to mask or [Masktype/Offset]: ");
    h.run({"O", "0.2"});
    h.proc.set_selection_count(1);
    h.run({""});
    REQUIRE(h.last<TextMaskCommand>()->offset == 0.2);
    h.run({"TEXTMASK"}); // preselected: masked at once
    REQUIRE_FALSE(h.proc.has_active_command());
    h.run({"BURST"});
    REQUIRE(h.last<ExplodeSelectionCommand>()->burst);
}

TEST_CASE("DIMINSPECT frames a dimension's value with a label and an inspection rate, kept with the drawing") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear), {0, 0}, {100, 0}, {50, 10}, 0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(SelectAllCommand{});
    engine.submit(DimInspectCommand{false, 2, "A", "50%", 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 inspection dimension."; }));
    io::Document doc = dump(engine, "musacad_diminspect.musa");
    REQUIRE(doc.dims[0].inspect == 2);
    REQUIRE(doc.dims[0].inspect_label == "A");
    REQUIRE(doc.dims[0].inspect_rate == "50%");
    io::Document again;
    REQUIRE(io::parse_native(io::serialize_native(doc), again).ok);
    REQUIRE(again.dims[0] == doc.dims[0]);
    engine.submit(SelectAllCommand{});
    engine.submit(DimInspectCommand{true, 1, "", "", 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Inspection removed from 1 dimension."; }));
    REQUIRE(dump(engine, "musacad_diminspect_removed.musa").dims[0].inspect == 0);
    engine.stop();

    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {100, 0};
    d.line_pt = {50, 10};
    const DimGeometry plain = compute_dim_geometry(d, DimStyle{}, Rgb{});
    d.inspect = 2; // Angular: top, bottom, two segments at each end, a divider each side of the value
    const DimGeometry angular = compute_dim_geometry(d, DimStyle{}, Rgb{}, DimTextParts{{}, {}, {}, "A", "50%"});
    REQUIRE(angular.label == "A  " + plain.label + "  50%");
    REQUIRE(angular.dim_lines.size() == plain.dim_lines.size() + 2 * (2 + 4 + 2));
    d.inspect = 1; // Round: half circles of twelve segments, one divider (no label)
    const DimGeometry round = compute_dim_geometry(d, DimStyle{}, Rgb{}, DimTextParts{{}, {}, {}, "", "100%"});
    REQUIRE(round.label == plain.label + "  100%");
    REQUIRE(round.dim_lines.size() == plain.dim_lines.size() + 2 * (2 + 24 + 1));
    d.inspect = 3; // None: side by side, unframed
    const DimGeometry bare = compute_dim_geometry(d, DimStyle{}, Rgb{}, DimTextParts{{}, {}, {}, "A", ""});
    REQUIRE(bare.label == "A  " + plain.label);
    REQUIRE(bare.dim_lines.size() == plain.dim_lines.size());

    H h;
    h.run({"-DIMINSPECT"});
    REQUIRE(h.out.prompt == "Select dimensions: ");
    h.proc.set_selection_count(1);
    h.run({""});
    REQUIRE(h.out.prompt == "Add inspection data or [Remove] <Add>: ");
    h.run({""});
    REQUIRE(h.out.prompt == "Enter shape option [Round/Angular/None] <Round>: ");
    h.run({"A"});
    REQUIRE(h.out.prompt == "Enter label data or <None>: ");
    h.run({"B2"});
    REQUIRE(h.out.prompt == "Enter inspection rate <100%>: ");
    h.run({"75%"});
    const DimInspectCommand* c = h.last<DimInspectCommand>();
    REQUIRE(c != nullptr);
    REQUIRE_FALSE(c->remove);
    REQUIRE(c->shape == 2);
    REQUIRE(c->label == "B2");
    REQUIRE(c->rate == "75%");
    h.run({"DIMINSPECT", ""}); // preselected; no window here, so the command line asks
    REQUIRE(h.out.prompt == "Enter shape option [Round/Angular/None] <Angular>: ");
    h.run({"", "", "."});
    REQUIRE(h.last<DimInspectCommand>()->label == "B2");
    REQUIRE(h.last<DimInspectCommand>()->rate.empty());
    h.run({"DIMINSPECT", "R"});
    REQUIRE(h.last<DimInspectCommand>()->remove);
}

TEST_CASE("TXTEXP asks for objects and explodes the texts among them") {
    H h;
    h.run({"TXTEXP"});
    REQUIRE(h.out.prompt == "Select objects: ");
    h.proc.set_selection_count(1);
    h.run({""});
    REQUIRE(h.last<TextExplodeCommand>() != nullptr);
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("TABLESTYLE adds, changes, makes current and deletes table styles; TABLE draws in the current one") {
    using Op = TableStyleCommand::Op;
    GeometryEngine engine;
    engine.start();
    TableStyle big;
    big.name = "Big";
    big.data_height = 5.0;
    engine.submit(TableStyleCommand{Op::Add, {}, big});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Table style \"Big\" added."; }));
    engine.submit(TableStyleCommand{Op::Add, {}, big});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Table style \"Big\" already exists."; }));
    engine.submit(TableStyleCommand{Op::SetCurrent, "big", {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "\"Big\" is now the current table style."; }));
    REQUIRE(engine.snapshot().current_table_style == 1);
    engine.submit(TableStyleCommand{Op::Delete, "Big", {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.ends_with("it is Standard, the current style or in use."); }));
    big.data_height = 6.0;
    engine.submit(TableStyleCommand{Op::Modify, "Big", big});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Table style \"Big\" changed."; }));
    io::Document doc = dump(engine, "musacad_tablestyle.musa");
    REQUIRE(doc.current_table_style == 1);
    REQUIRE(doc.table_styles[1].data_height == 6.0);
    io::Document again;
    REQUIRE(io::parse_native(io::serialize_native(doc), again).ok);
    REQUIRE(again.current_table_style == 1);
    engine.submit(TableStyleCommand{Op::SetCurrent, "Standard", {}});
    engine.submit(TableStyleCommand{Op::Delete, "Big", {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Table style \"Big\" deleted."; }));
    REQUIRE(engine.snapshot().table_styles.size() == 1);
    engine.stop();

    H h;
    TableStyle standard;
    standard.name = "Standard";
    h.proc.set_table_styles({standard, big}, 0);
    h.run({"TABLESTYLE"}); // no window here: the command line asks
    REQUIRE(h.out.prompt == "Enter table style name to make current or [?] <Standard>: ");
    const auto said = [&h](const std::string& prefix) {
        return std::any_of(h.out.lines.begin(), h.out.lines.end(),
                           [&](const std::string& l) { return l.starts_with(prefix); });
    };
    h.run({"?"});
    REQUIRE(said("Table styles: Standard, Big"));
    h.run({"Big"});
    REQUIRE(h.last<TableStyleCommand>()->op == Op::SetCurrent);
    REQUIRE(h.last<TableStyleCommand>()->name == "Big");
    h.proc.set_table_styles({standard, big}, 1);
    h.run({"TB", "2", "1"});
    REQUIRE(said("Current table style: Big"));
    h.run({"S"});
    REQUIRE(h.out.prompt == "Enter a table style name or [?] <Big>: ");
    h.run({"standard", "0,0"});
    REQUIRE(h.last<AddTableCommand>()->style == 0);
    h.run({"TB", "2", "1", "0,0"});
    const AddTableCommand* t = h.last<AddTableCommand>();
    REQUIRE(t->style == 1);
    REQUIRE(t->row_heights[0] == Approx(9.0 * 6.0 / 2.5)); // a line of the style's data text
}

TEST_CASE("TCOUNT numbers texts in order; TEXTFIT stretches a text to a point by its width factor") {
    using Sort = TextCountCommand::Sort;
    using Place = TextCountCommand::Place;
    GeometryEngine engine;
    engine.start();
    const auto add_text = [&](Vec2 at, const char* content, std::uint64_t g) {
        AddTextCommand t;
        t.pos = at;
        t.height = 5.0;
        t.content = content;
        t.group = g;
        engine.submit(t);
    };
    add_text({30, 0}, "A", 1);
    add_text({10, 0}, "B", 2);
    add_text({20, 0}, "C", 3);
    engine.submit(SelectAllCommand{});
    engine.submit(TextCountCommand{Sort::X, 5, 2, Place::Prefix, "", 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "3 texts numbered."; }));
    io::Document doc = dump(engine, "musacad_tcount.musa");
    std::vector<std::string> got;
    for (const io::DocText& t : doc.texts) {
        got.push_back(t.content);
    }
    std::sort(got.begin(), got.end());
    REQUIRE(got == std::vector<std::string>{"5 B", "7 C", "9 A"});
    engine.submit(SelectAllCommand{});
    engine.submit(TextCountCommand{Sort::Y, 1, 1, Place::Replace, " ", 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "3 texts numbered."; }));
    engine.submit(SelectAllCommand{});
    engine.submit(EraseSelectionCommand{6});

    add_text({0, 0}, "ABC", 7);
    const double w = text::frame_of("ABC", 0, {0, 0}, {}, 5.0, 0.0, 1.0).width();
    engine.submit(TextFitCommand{{1, 1}, 3.0, {2 * w, 0}, std::nullopt, 8});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Text width factor 2."; }));
    engine.submit(TextFitCommand{{1, 1}, 3.0, {10 + w, 0}, Vec2{10, 0}, 9});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Text width factor 1."; }));
    doc = dump(engine, "musacad_textfit.musa");
    REQUIRE(doc.texts.size() == 1);
    REQUIRE(doc.texts[0].pos.x == Approx(10.0));
    REQUIRE(doc.texts[0].width_factor == Approx(1.0));
    engine.submit(TextFitCommand{{50, 50}, 1.0, {60, 50}, std::nullopt, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Select a single-line text."; }));
    engine.stop();

    H h;
    h.run({"TCOUNT"});
    REQUIRE(h.out.prompt == "Select objects: ");
    h.proc.set_selection_count(2);
    h.run({""});
    REQUIRE(h.out.prompt == "Sort selected objects by [X/Y/Select-order] <Select-order>: ");
    h.run({"Y"});
    REQUIRE(h.out.prompt == "Specify starting number and increment (Start,increment) <1,1>: ");
    h.run({"10,5"});
    REQUIRE(h.out.prompt == "Placement of numbers in text [Overwrite/Prefix/Suffix/Find&replace..] <Prefix>: ");
    h.run({"F", "#"});
    const TextCountCommand* c = h.last<TextCountCommand>();
    REQUIRE(c->sort == Sort::Y);
    REQUIRE(c->start == 10);
    REQUIRE(c->step == 5);
    REQUIRE(c->place == Place::Replace);
    REQUIRE(c->find == "#");
    h.run({"TEXTFIT", "1,1", "S", "5,0", "40,0"});
    REQUIRE(h.last<TextFitCommand>()->start == Vec2{5, 0});
    REQUIRE(h.last<TextFitCommand>()->end == Vec2{40, 0});
}

TEST_CASE("GATTE changes an attribute on every reference of a block; VPSCALE needs a layout") {
    GeometryEngine engine;
    engine.start();
    AddAttDefCommand ad;
    ad.text.pos = {0, 0};
    ad.text.height = 2.5;
    ad.text.content = "TAG";
    ad.def = "DEFAULT";
    ad.group = 1;
    engine.submit(ad);
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(DefineBlockCommand{"B", {0, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.block_names.size() == 1; }));
    engine.submit(SelectAllCommand{});
    engine.submit(EraseSelectionCommand{3});
    for (int i = 0; i < 3; ++i) {
        InsertBlockCommand ins;
        ins.name = "B";
        ins.pos = {100.0 * i, 0};
        ins.group = 4 + static_cast<std::uint64_t>(i);
        ins.attribs = {"OLD"};
        engine.submit(ins);
    }
    engine.submit(GlobalAttEditCommand{std::nullopt, 0.0, "b", "tag", "NEW", true, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "3 attributes changed."; }));
    engine.submit(GlobalAttEditCommand{Vec2{100, 0}, 2.0, "", "TAG", "ONE", false, 11});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 attribute changed."; }));
    const io::Document doc = dump(engine, "musacad_gatte.musa");
    std::vector<std::string> values;
    for (const io::DocInsert& in : doc.inserts) {
        values.push_back(in.attribs.at(0).value);
    }
    std::sort(values.begin(), values.end());
    REQUIRE(values == std::vector<std::string>{"NEW", "NEW", "ONE"});
    engine.submit(GlobalAttEditCommand{std::nullopt, 0.0, "B", "NOPE", "X", true, 12});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Block \"B\" has no attribute tagged \"NOPE\"."; }));
    engine.submit(VpScaleQueryCommand{{0, 0}, 1.0});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "VPSCALE works on a layout: select a viewport there."; }));
    engine.stop();

    H h;
    h.run({"GATTE", "B"});
    REQUIRE(h.out.prompt == "Enter block name: ");
    h.run({"X"}); // no blocks known here
    REQUIRE(h.out.lines.back() == "Block \"X\" not found.");
    h.run({""});
    h.run({"GATTE", "100,0", "TAG", "V2", "N"});
    const GlobalAttEditCommand* g = h.last<GlobalAttEditCommand>();
    REQUIRE(g != nullptr);
    REQUIRE(g->pick.has_value());
    REQUIRE(g->tag == "TAG");
    REQUIRE(g->value == "V2");
    REQUIRE_FALSE(g->all);
    h.run({"VPSCALE", "5,5"});
    REQUIRE(h.last<VpScaleQueryCommand>()->pick == Vec2{5, 5});
    h.run({"GRADIENT"});
    REQUIRE(h.out.prompt.starts_with("Pick internal point"));
}

TEST_CASE("EXPORTPDF, PUBLISH and PAGESETUP say so where there is no window; a page setup is deleted by name") {
    H h;
    h.run({"EXPORTPDF"});
    REQUIRE(h.out.lines.back() == "EXPORTPDF needs the drawing window.");
    REQUIRE_FALSE(h.proc.has_active_command());
    h.run({"PUBLISH"});
    REQUIRE(h.out.lines.back() == "PUBLISH needs the drawing window.");
    h.run({"PAGESETUP"});
    REQUIRE(h.out.lines.back() == "PAGESETUP needs the drawing window.");

    GeometryEngine engine;
    engine.start();
    PageSetup ps;
    ps.name = "A3 sheet";
    engine.submit(AddPageSetupCommand{ps});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.page_setups.size() == 1; }));
    engine.submit(RemovePageSetupCommand{"A3 sheet"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Page setup \"A3 sheet\" deleted."; }));
    REQUIRE(engine.snapshot().page_setups.empty());
    engine.submit(RemovePageSetupCommand{"A3 sheet"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "No page setup is called \"A3 sheet\"."; }));
    engine.stop();
}

TEST_CASE("REVCLOUD Freehand: a drag draws the cloud, closed when it comes back to its start") {
    H h;
    h.run({"REVCLOUD", "F"});
    REQUIRE(h.proc.wants_freehand());
    std::vector<Vec2> loop;
    for (int i = 0; i <= 64; ++i) {
        const double a = 2.0 * 3.141592653589793 * i / 64.0;
        loop.push_back({10.0 * std::cos(a), 10.0 * std::sin(a)});
    }
    h.proc.submit_freehand(loop);
    const AddPolylineCommand* c = h.last<AddPolylineCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->closed);
    REQUIRE(c->points.size() > 20);
    REQUIRE_FALSE(h.proc.has_active_command());

    h.run({"REVCLOUD"});
    std::vector<Vec2> line;
    for (int i = 0; i <= 40; ++i) {
        line.push_back({static_cast<double>(i), 0.0});
    }
    h.proc.submit_freehand(line);
    REQUIRE_FALSE(h.last<AddPolylineCommand>()->closed);
}

TEST_CASE("TABLEEDIT edits a cell's text; TABLE without a window asks on the command line") {
    H h;
    h.run({"TABLEEDIT"});
    REQUIRE(h.out.prompt == "Pick a table cell: ");
    h.run({"5,5"});
    REQUIRE(h.out.prompt == "Enter new text: ");
    h.run({"QTY 4"});
    const EditTextContentCommand* e = h.last<EditTextContentCommand>();
    REQUIRE(e != nullptr);
    REQUIRE(e->at == Vec2{5, 5});
    REQUIRE(e->content == "QTY 4");
    h.run({"TABLE"});
    REQUIRE(h.out.prompt == "Enter number of columns or [Auto] <5>: ");
    h.run({"2", "1", "0,0"});
    REQUIRE(h.last<AddTableCommand>()->rows == 3); // a title and a header row with the data row
}

TEST_CASE("TFRAMES turns wipeout and image frames off and on together; UPDATEFIELD redraws the fields") {
    GeometryEngine engine;
    engine.start();
    engine.submit(ToggleFramesCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "IMAGE/WIPEOUT frames are toggled OFF."; }));
    engine.submit(ToggleFramesCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "IMAGE/WIPEOUT frames are toggled ON."; }));
    AddTextCommand t;
    t.pos = {0, 0};
    t.content = "Plotted %<\\AcVar Date>% by %<\\AcVar Login>%";
    t.group = 1;
    engine.submit(t);
    engine.submit(SelectAllCommand{});
    engine.submit(UpdateFieldsCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "2 field(s) found. 2 field(s) updated."; }));
    engine.stop();

    H h;
    h.run({"TFRAMES"});
    REQUIRE(h.last<ToggleFramesCommand>() != nullptr);
    h.run({"UPDATEFIELD"});
    REQUIRE(h.out.prompt == "Select objects: ");
    h.proc.set_selection_count(1);
    h.run({""});
    REQUIRE(h.last<UpdateFieldsCommand>() != nullptr);
}

TEST_CASE("HATCH seTtings User defined: the angle, the spacing and Double") {
    H h;
    h.run({"H", "T", "U"});
    REQUIRE(h.out.prompt.starts_with("Specify angle for crosshatch lines <"));
    h.run({"45", "2"});
    REQUIRE(h.out.prompt == "Double hatch area? [Yes/No] <N>: ");
    h.run({"Y", "5,5"});
    const HatchPickPointCommand* c = h.last<HatchPickPointCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->pattern_name == "_USERDOUBLE");
    REQUIRE(c->pattern_scale == Approx(2.0));
    REQUIRE(c->pattern_angle == Approx(3.141592653589793 / 4.0));
}

TEST_CASE("DIMEX writes dimension styles to a file and DIMIM takes them in, keeping or overwriting") {
    const std::string path = (std::filesystem::temp_directory_path() / "musacad_dimex_test.dim").string();
    {
        GeometryEngine engine;
        engine.start();
        DimStyle arch;
        arch.name = "Arch";
        arch.text_height = 3.5;
        engine.submit(AddDimStyleCommand{arch});
        engine.submit(ExportDimStylesCommand{path, "ar*"});
        REQUIRE(wait_until(engine, [&](const auto& s) { return s.status == "1 dimension style exported to " + path + "."; }));
        engine.submit(ImportDimStylesCommand{path, false});
        REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "0 added, 0 overwritten, 1 kept as they were."; }));
        engine.stop();
    }
    {
        GeometryEngine engine;
        engine.start();
        engine.submit(ImportDimStylesCommand{path, true});
        REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 added, 0 overwritten, 0 kept as they were."; }));
        const auto& styles = engine.snapshot().dimstyles;
        REQUIRE(std::any_of(styles.begin(), styles.end(),
                            [](const DimStyle& d) { return d.name == "Arch" && d.text_height == 3.5; }));
        engine.stop();
    }
    std::filesystem::remove(path);

    H h;
    h.run({"DIMEX"});
    REQUIRE(h.out.prompt == "Dimension styles to export <*>: ");
    h.run({""});
    REQUIRE(h.out.prompt == "Export filename: "); // no window here
    h.run({"/tmp/house"});
    REQUIRE(h.last<ExportDimStylesCommand>()->path == "/tmp/house.dim");
    REQUIRE(h.last<ExportDimStylesCommand>()->names.empty());
    h.run({"DIMIM", "/tmp/house.dim"});
    REQUIRE(h.out.prompt == "Existing styles of the same name [Keep/Overwrite] <Keep>: ");
    h.run({"O"});
    REQUIRE(h.last<ImportDimStylesCommand>()->overwrite);
}

TEST_CASE("GRIPS, GRIPSIZE, GRIPCOLOR and GRIPHOT ask their value; without a window it is out of reach") {
    H h;
    h.run({"GRIPSIZE"});
    REQUIRE(h.out.prompt == "Enter new value for GRIPSIZE <0>: ");
    h.run({"6"});
    REQUIRE(h.out.lines.back() == "Value out of range for GRIPSIZE.");
    h.run({""});
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("NAVBAR asks ON or OFF and needs the window") {
    H h;
    h.run({"NAVBAR"});
    REQUIRE(h.out.prompt == "Enter an option [ON/OFF] <ON>: ");
    h.run({"maybe"});
    REQUIRE(h.out.lines.back() == "Enter ON or OFF.");
    h.run({"OFF"});
    REQUIRE(h.out.lines.back() == "NAVBAR needs the drawing window.");
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("-LAYER TRansparency: the value, then the layers") {
    H h;
    h.run({"-LAYER", "TR"});
    REQUIRE(h.out.prompt == "Enter transparency value (0-90): ");
    h.run({"95"});
    REQUIRE(h.out.lines.back() == "Requires a whole number from 0 to 90.");
    h.run({"40"});
    REQUIRE(h.out.prompt == "Enter name list of layer(s) for transparency 40% <current>: ");
    h.run({"Glass"});
    const LayerEditCommand* c = h.last<LayerEditCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->op == LayerEditCommand::Op::Transparency);
    REQUIRE(c->transparency == 40);
    REQUIRE(c->names == "Glass");
}

TEST_CASE("Isometric drafting: the snap lattice, ORTHO on the isoplane's axes, ISOPLANE, ISODRAFT and isocircles") {
    H h;
    h.run({"SNAP", "S"});
    REQUIRE(h.out.prompt == "Enter snap grid style [Standard/Isometric] <S>: ");
    h.run({"I"});
    REQUIRE(h.out.prompt.starts_with("Specify vertical spacing <"));
    h.run({"10"});
    REQUIRE(h.proc.grid_settings().isometric);
    // The lattice: x a multiple of 10*cos30, y of 5, the two of the same parity.
    h.proc.set_grid_snap(true);
    const double sx = 10.0 * std::sqrt(3.0) * 0.5;
    const Vec2 p = h.proc.resolve_pick({sx * 1.1, 4.0}, std::nullopt);
    REQUIRE(p.x == Approx(sx));
    REQUIRE(p.y == Approx(5.0));
    h.proc.set_grid_snap(false);

    // ORTHO in the Left isoplane: 90 and 150 degrees from the last point.
    h.proc.set_ortho(true);
    h.proc.set_last_point({0, 0});
    const Vec2 up = h.proc.resolve_pick({1, 10}, std::nullopt);
    REQUIRE(up.x == Approx(0.0).margin(1e-9));
    REQUIRE(up.y == Approx(10.0));
    const Vec2 along = h.proc.resolve_pick({-10, 6}, std::nullopt);
    REQUIRE(along.y / along.x == Approx(std::tan(150.0 * 3.141592653589793 / 180.0)));
    h.proc.set_ortho(false);

    h.run({"ISOPLANE"});
    REQUIRE(h.out.prompt == "Enter isometric plane setting [Left/Top/Right] <Top>: ");
    h.run({""});
    REQUIRE(h.proc.grid_settings().isoplane == 1);
    REQUIRE(h.out.lines.back() == "Current isoplane: Top");

    // An isocircle in the Top plane: the long axis level, r*sqrt(3/2) by r*sqrt(1/2).
    h.run({"ELLIPSE"});
    REQUIRE(h.out.prompt == "Specify axis endpoint of ellipse or [Arc/Center/Isocircle]: ");
    h.run({"I"});
    REQUIRE(h.out.prompt == "Specify center of isocircle: ");
    h.run({"0,0"});
    REQUIRE(h.out.prompt == "Specify radius of isocircle or [Diameter]: ");
    h.run({"10"});
    const AddEllipseCommand* e = h.last<AddEllipseCommand>();
    REQUIRE(e != nullptr);
    REQUIRE(e->major.x == Approx(10.0 * std::sqrt(1.5)));
    REQUIRE(e->major.y == Approx(0.0).margin(1e-9));
    REQUIRE(e->ratio == Approx(1.0 / std::sqrt(3.0)));

    h.run({"ISODRAFT"});
    REQUIRE(h.out.prompt == "Enter isometric drafting option [Orthographic/isoplane Left/isoplane Top/isoplane Right] <Orthographic>: ");
    h.run({"O"});
    REQUIRE_FALSE(h.proc.grid_settings().isometric);
    h.run({"ELLIPSE"});
    REQUIRE(h.out.prompt == "Specify axis endpoint of ellipse or [Arc/Center]: ");
}

TEST_CASE("MLEADERALIGN Options: Distribute between two points, Parallel last segments, a Spacing") {
    using Mode = MLeaderAlignCommand::Mode;
    const auto two_leaders = [](GeometryEngine& engine) {
        MTextBlock a;
        a.pos = {16, 10};
        a.attach = 3;
        engine.submit(AddMLeaderCommand{{{0, 0}, {10, 10}, {15, 10}}, 0, a, "A", 1});
        MTextBlock b;
        b.pos = {18, -5};
        b.attach = 3;
        engine.submit(AddMLeaderCommand{{{0, -20}, {12, -5}, {17, -5}}, 0, b, "B", 2});
        engine.submit(SelectAllCommand{});
        REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    };
    const auto find = [](const io::Document& doc, const char* text) {
        for (const auto& m : doc.mleaders) {
            if (m.content == text) {
                return m;
            }
        }
        FAIL("multileader not found");
        return doc.mleaders.front();
    };
    {
        GeometryEngine engine;
        engine.start();
        two_leaders(engine);
        MLeaderAlignCommand c{};
        c.mode = Mode::Distribute;
        c.from = {20, 0};
        c.to = {20, 40};
        c.group = 3;
        engine.submit(c);
        REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "2 multileaders aligned."; }));
        const io::Document doc = dump(engine, "musacad_mla_distribute.musa");
        REQUIRE(find(doc, "B").vertices.back() == Vec2{20, 0}); // lowest first
        REQUIRE(find(doc, "A").vertices.back() == Vec2{20, 40});
        REQUIRE(find(doc, "A").vertices.front() == Vec2{0, 0}); // the arrowhead stays
        engine.stop();
    }
    {
        GeometryEngine engine;
        engine.start();
        two_leaders(engine);
        MLeaderAlignCommand c{{12, 10}, 0.5, {}, 3};
        c.mode = Mode::Parallel;
        engine.submit(c);
        REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 multileader aligned."; }));
        const auto b = find(dump(engine, "musacad_mla_parallel.musa"), "B");
        const Vec2 seg = b.vertices[1] - b.vertices[0];
        REQUIRE(seg.y / seg.x == Approx(1.0)); // as A's (0,0)-(10,10)
        engine.stop();
    }
    {
        GeometryEngine engine;
        engine.start();
        two_leaders(engine);
        MLeaderAlignCommand c{{12, 10}, 0.5, {15, -30}, 3};
        c.mode = Mode::Spacing;
        c.spacing = 4.0;
        engine.submit(c);
        REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 multileader aligned."; }));
        const auto b = find(dump(engine, "musacad_mla_spacing.musa"), "B");
        REQUIRE(b.vertices.back().x == Approx(15.0));
        REQUIRE(b.vertices.back().y < 10.0 - 4.0);
        engine.stop();
    }

    H h;
    h.proc.set_selection_count(2);
    h.run({"MLEADERALIGN", "O"});
    REQUIRE(h.out.prompt == "Enter an option [Distribute/make leader segments Parallel/specify Spacing/Use current] <Use current>: ");
    h.run({"D"});
    REQUIRE(h.out.prompt == "Specify first point or [Options]: ");
    h.run({"20,0", "20,40"});
    REQUIRE(h.last<MLeaderAlignCommand>()->mode == Mode::Distribute);
    REQUIRE(h.last<MLeaderAlignCommand>()->to == Vec2{20, 40});
    h.run({"MLEADERALIGN", "O", "S", "4"});
    REQUIRE(h.out.prompt == "Select multileader to align to or [Options]: ");
    h.run({"12,10", "15,-30"});
    REQUIRE(h.last<MLeaderAlignCommand>()->mode == Mode::Spacing);
    REQUIRE(h.last<MLeaderAlignCommand>()->spacing == 4.0);
    h.run({"MLEADERALIGN", "O", "U"});
}

TEST_CASE("QLEADER asks as AutoCAD does: the points up to the maximum, the text width, the lines") {
    H h;
    h.run({"LE"});
    REQUIRE(h.out.prompt == "Specify first leader point, or [Settings] <Settings>: ");
    h.run({""}); // Settings: no window here
    REQUIRE(h.out.lines.back() == "Leader Settings need the drawing window.");
    REQUIRE(h.out.prompt == "Specify first leader point, or [Settings] <Settings>: ");
    h.run({"0,0"});
    REQUIRE(h.out.prompt == "Specify next point: ");
    h.run({"10,10", "20,10"}); // the third point: as many as the settings take
    REQUIRE(h.out.prompt == "Specify text width <0.0000>: ");
    h.run({"30"});
    REQUIRE(h.out.prompt == "Enter first line of annotation text <Mtext>: ");
    REQUIRE(h.proc.wants_free_text());
    h.run({"PART 4"});
    REQUIRE(h.out.prompt == "Enter next line of annotation text: ");
    h.run({"SEE NOTE 2", ""});
    const AddMLeaderCommand* m = h.last<AddMLeaderCommand>();
    REQUIRE(m != nullptr);
    REQUIRE(m->vertices.size() == 3);
    REQUIRE(m->content == "PART 4\nSEE NOTE 2");
    REQUIRE(m->block.width == 30.0);
    REQUIRE(m->block.pos == Vec2{20, 10});
}

TEST_CASE("-COLOR, CECOLOR, -LWEIGHT, CELWEIGHT, -LINETYPE and CELTYPE set what new objects take") {
    H h;
    h.run({"-COLOR"});
    REQUIRE(h.out.prompt == "Enter default object color [Truecolor/COlorbook] <BYLAYER>: ");
    h.run({"red"});
    const SetCurrentPropsCommand* c = h.last<SetCurrentPropsCommand>();
    REQUIRE(c != nullptr);
    REQUIRE_FALSE(c->current.color_by_layer());
    REQUIRE(c->current.color == Rgb{255, 0, 0});
    h.run({"CECOLOR", "T", "10,20,30"});
    REQUIRE(h.last<SetCurrentPropsCommand>()->current.color == Rgb{10, 20, 30});
    h.run({"COLOR", "BYLAYER"}); // no window here: the command line asks
    REQUIRE(h.last<SetCurrentPropsCommand>()->current.color_by_layer());

    h.run({"-LWEIGHT"});
    REQUIRE(h.out.lines.back() == "Current lineweight: BYLAYER");
    REQUIRE(h.out.prompt == "Enter default lineweight for new objects or [?]: ");
    h.run({"0.33"});
    REQUIRE(h.last<SetCurrentPropsCommand>()->current.lineweight == 35); // the nearest of AutoCAD's
    h.run({"CELWEIGHT", "50"});
    REQUIRE(h.last<SetCurrentPropsCommand>()->current.lineweight == 50);

    h.run({"-LINETYPE"});
    REQUIRE(h.out.prompt == "Enter an option [?/Create/Load/Set]: ");
    h.run({"S"});
    REQUIRE(h.out.prompt == "Specify linetype name or [?] <BYLAYER>: ");
    h.run({"hidden"});
    REQUIRE(h.last<SetCurrentPropsCommand>()->current.linetype == Linetype::Hidden);
    h.run({"CELTYPE", "Dashed"});
    REQUIRE(h.last<SetCurrentPropsCommand>()->current.linetype == Linetype::Dashed);
    h.run({"LWDISPLAY"});
    REQUIRE(h.out.prompt == "Enter new value for LWDISPLAY <ON>: ");
    h.run({"OFF"});
    REQUIRE_FALSE(h.last<SetLineweightDisplayCommand>()->on);
}

TEST_CASE("RENAME renames layers, styles, blocks and views; SETVAR runs a variable; the file commands need the window") {
    using K = RenameObjectCommand::Kind;
    GeometryEngine engine;
    engine.start();
    Layer walls;
    walls.name = "Walls";
    engine.submit(AddLayerCommand{walls});
    engine.submit(RenameObjectCommand{K::Layer, "Walls", "A-WALL"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Layer \"Walls\" renamed to \"A-WALL\"."; }));
    engine.submit(RenameObjectCommand{K::Layer, "0", "Zero"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.starts_with("Layer \"0\" was not renamed"); }));
    TextStyle notes;
    notes.name = "Notes";
    engine.submit(SetTextStyleCommand{notes, false});
    engine.submit(RenameObjectCommand{K::TextStyle, "Notes", "Callouts"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Text style \"Notes\" renamed to \"Callouts\"."; }));
    const io::Document doc = dump(engine, "musacad_rename.musa");
    REQUIRE(std::any_of(doc.layers.begin(), doc.layers.end(), [](const Layer& l) { return l.name == "A-WALL"; }));
    REQUIRE(std::any_of(doc.text_styles.begin(), doc.text_styles.end(), [](const TextStyle& t) { return t.name == "Callouts"; }));
    engine.stop();

    H h;
    h.run({"REN"});
    REQUIRE(h.out.prompt.starts_with("Enter object type to rename [Block/Dimstyle/LAyer/"));
    h.run({"LA"});
    REQUIRE(h.out.prompt == "Enter old layer name: ");
    h.run({"Walls"});
    REQUIRE(h.out.prompt == "Enter new layer name: ");
    h.run({"A-WALL"});
    REQUIRE(h.last<RenameObjectCommand>()->to == "A-WALL");

    h.run({"SETVAR"});
    REQUIRE(h.out.prompt == "Enter variable name or [?]: ");
    h.run({"PDMODE"});
    REQUIRE(h.out.prompt.starts_with("Enter new value for PDMODE"));
    h.run({"3"});
    REQUIRE_FALSE(h.proc.has_active_command());
    h.run({"SETVAR", "LINE"});
    REQUIRE(h.out.lines.back() == "Unknown variable name.  Type SETVAR ? for a list of variables.");
    h.run({""});

    for (const char* name : {"QSAVE", "OPEN", "CLOSE"}) {
        h.run({name});
        REQUIRE(h.out.lines.back() == std::string(name) + " needs the drawing window.");
    }
    h.run({"REDRAW"});
    REQUIRE(h.last<RegenCommand>() != nullptr);
}

TEST_CASE("DBLIST lists every object; PLAN takes the World view") {
    GeometryEngine engine;
    engine.start();
    engine.submit(ListQueryCommand{{}, 0.0, false, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "DBLIST: the drawing is empty."; }));
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddCircleCommand{{20, 0}, 5, 2});
    engine.submit(ListQueryCommand{{}, 0.0, false, true});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("LINE") != std::string::npos && s.status.find("CIRCLE") != std::string::npos;
    }));
    engine.stop();

    H h;
    h.run({"DBLIST"});
    REQUIRE(h.last<ListQueryCommand>()->everything);
    h.run({"PLAN"});
    REQUIRE(h.out.prompt == "Enter an option [Current ucs/Ucs/World] <Current>: ");
    h.run({"W"});
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("DATAEXTRACTION counts block references by name and attribute values, into CSV and a table") {
    GeometryEngine engine;
    engine.start();
    AddAttDefCommand ad;
    ad.text.pos = {0, 0};
    ad.text.height = 2.5;
    ad.text.content = "SIZE";
    ad.def = "M6";
    ad.group = 1;
    engine.submit(ad);
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(DefineBlockCommand{"BOLT", {0, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.block_names.size() == 1; }));
    engine.submit(SelectAllCommand{});
    engine.submit(EraseSelectionCommand{3});
    const char* sizes[] = {"M6", "M8", "M6"};
    for (int i = 0; i < 3; ++i) {
        InsertBlockCommand ins;
        ins.name = "BOLT";
        ins.pos = {50.0 * i, 0};
        ins.group = 4 + static_cast<std::uint64_t>(i);
        ins.attribs = {sizes[i]};
        engine.submit(ins);
    }
    const std::string path = (std::filesystem::temp_directory_path() / "musacad_extract.csv").string();
    engine.submit(DataExtractCommand{true, path, Vec2{0, -50}, 10});
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.status == "2 rows extracted to " + path + ", and into a table."; }));
    std::ifstream f(path);
    std::string csv((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    REQUIRE(csv == "Count,Name,SIZE\r\n2,BOLT,M6\r\n1,BOLT,M8\r\n");
    std::filesystem::remove(path);
    const io::Document doc = dump(engine, "musacad_extract.musa");
    REQUIRE(doc.tables.size() == 1);
    REQUIRE(doc.tables[0].rows == 3);
    REQUIRE(doc.tables[0].cols == 3);
    engine.stop();

    H h;
    h.run({"DX"});
    REQUIRE(h.out.prompt == "Extract from [Drawing/Selection] <Drawing>: ");
    h.run({""});
    REQUIRE(h.out.prompt == "Output to [Table/File/Both] <Table>: ");
    h.run({"F"});
    REQUIRE(h.out.prompt == "Enter file name: ");
    h.run({"/tmp/parts"});
    REQUIRE(h.last<DataExtractCommand>()->path == "/tmp/parts.csv");
    REQUIRE_FALSE(h.last<DataExtractCommand>()->table_at.has_value());
}

TEST_CASE("NCOPY copies an object out of a block reference; the reference stays") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddCircleCommand{{5, 20}, 3, 2});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(DefineBlockCommand{"PART", {0, 0}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.block_names.size() == 1; }));
    engine.submit(SelectAllCommand{});
    engine.submit(EraseSelectionCommand{4});
    InsertBlockCommand ins;
    ins.name = "PART";
    ins.pos = {100, 100};
    ins.group = 5;
    engine.submit(ins);
    engine.submit(NestedCopyCommand{{108, 120}, 1.0, {0, -50}, 6}); // on the circle (100+5+3, 100+20)
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 object copied out of the block reference."; }));
    const io::Document doc = dump(engine, "musacad_ncopy.musa");
    REQUIRE(doc.inserts.size() == 1);
    REQUIRE(doc.circles.size() == 1);
    REQUIRE(doc.circles[0].center.x == Approx(105.0));
    REQUIRE(doc.circles[0].center.y == Approx(70.0));
    REQUIRE(doc.lines.empty());
    engine.submit(NestedCopyCommand{{0, 0}, 1.0, {}, 7});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Select an object inside a block reference."; }));
    engine.stop();

    H h;
    h.run({"NCOPY"});
    REQUIRE(h.out.prompt == "Select nested objects to copy: ");
    h.run({"108,120", ""});
    REQUIRE(h.out.prompt == "Specify base point or displacement, or [Multiple]: ");
    h.run({""}); // in place
    REQUIRE(h.last<NestedCopyCommand>()->offset == Vec2{0, 0});
}

TEST_CASE("A PGP file's aliases are read as acad.pgp writes them", "[alias][pgp]") {
    const auto pgp = musacad::command::parse_pgp("; comment\r\n"
                                                 "EXPLORER, START EXPLORER, 1,,\n"
                                                 "c,  *copy\n"
                                                 "  ZZ ,*ZOOM ; zoom\n"
                                                 "BAD ALIAS, *LINE\n"
                                                 "NOSTAR, LINE\n");
    REQUIRE(pgp.size() == 2);
    REQUIRE(pgp[0] == std::pair<std::string, std::string>{"C", "COPY"});
    REQUIRE(pgp[1] == std::pair<std::string, std::string>{"ZZ", "ZOOM"});
}

TEST_CASE("A user alias starts its command, in place of a built-in one", "[alias][pgp]") {
    using musacad::command::CommandRegistry;
    CommandRegistry r = CommandRegistry::make_default();
    REQUIRE(r.create("C")->name() == "CIRCLE");
    REQUIRE(r.add_alias("C", "COPY"));
    REQUIRE(r.create("C")->name() == "COPY");
    REQUIRE(r.add_alias("QQ", "L")); // an alias of an alias
    REQUIRE(r.create("qq")->name() == "LINE");
    REQUIRE(r.find("QQ")->name == "LINE");
    const auto s = r.suggest("QQ");
    REQUIRE(!s.empty());
    REQUIRE(s.front().name == "LINE");
    REQUIRE_FALSE(r.add_alias("XX", "NOSUCHCOMMAND"));
    REQUIRE_FALSE(r.contains("XX"));
    REQUIRE(r.contains("ALIASEDIT"));
    REQUIRE(r.contains("REINIT"));
}

namespace {
struct WorkspaceView : musacad::command::ViewControl {
    std::vector<std::string> names{"Drafting & Annotation", "Mine"};
    std::string current = "Drafting & Annotation";
    std::vector<std::string> ops;
    void zoom_extents() override {}
    void zoom_scale(double /*f*/) override {}
    std::vector<std::string> workspaces() const override { return names; }
    std::string current_workspace() const override { return current; }
    bool workspace_op(const std::string& op, const std::string& name, const std::string& to) override {
        ops.push_back(op + ":" + name + ":" + to);
        if (op == "current") {
            current = name;
        } else if (op == "saveas") {
            if (name == names.front()) {
                return false;
            }
            names.push_back(name);
            current = name;
        } else if (op == "delete" && name == current) {
            return false;
        }
        return true;
    }
};
} // namespace

TEST_CASE("WORKSPACE makes a workspace current, saves, renames and deletes one; WSCURRENT asks for it") {
    WorkspaceView view;
    Out out;
    CommandProcessor proc{[](Command) {}, &view, out};
    proc.submit_line("WORKSPACE");
    REQUIRE(out.prompt == "Enter workspace option [setCurrent/SAveas/Rename/Delete/?] <setCurrent>: ");
    proc.submit_line("");
    REQUIRE(out.prompt == "Enter name of workspace to make current [?] <Drafting & Annotation>: ");
    proc.submit_line("mine"); // any case
    REQUIRE(view.current == "Mine");
    REQUIRE_FALSE(proc.has_active_command());

    proc.submit_line("WORKSPACE");
    proc.submit_line("SA");
    REQUIRE(out.prompt == "Enter name of workspace to save [?] <Mine>: ");
    proc.submit_line("Plan view"); // a name with a space
    REQUIRE(view.ops.back() == "saveas:Plan view:");
    REQUIRE(view.current == "Plan view");

    proc.submit_line("WORKSPACE");
    proc.submit_line("SA");
    proc.submit_line("Mine");
    REQUIRE(out.prompt == "Workspace \"Mine\" already exists. Replace it? [Yes/No] <N>: ");
    proc.submit_line("");
    REQUIRE(view.ops.back() == "saveas:Plan view:"); // nothing replaced

    proc.submit_line("WORKSPACE");
    proc.submit_line("R");
    REQUIRE(out.prompt == "Enter name of workspace to rename [?]: ");
    proc.submit_line("Drafting & Annotation");
    REQUIRE(out.lines.back() == "\"Drafting & Annotation\" is built in: it cannot be renamed or deleted.");
    proc.submit_line("WORKSPACE");
    proc.submit_line("R");
    proc.submit_line("Mine");
    REQUIRE(out.prompt == "Enter new name for workspace \"Mine\": ");
    proc.submit_line("Sections");
    REQUIRE(view.ops.back() == "rename:Mine:Sections");

    proc.submit_line("WORKSPACE");
    proc.submit_line("D");
    proc.submit_line("Plan view");
    REQUIRE(out.lines.back() == "The current workspace cannot be deleted.");

    proc.submit_line("WORKSPACE");
    proc.submit_line("?");
    REQUIRE(out.lines.back() == "  * Plan view");
    REQUIRE(out.prompt == "Enter workspace option [setCurrent/SAveas/Rename/Delete/?] <setCurrent>: ");
    proc.submit_line("C");
    proc.submit_line("Nowhere");
    REQUIRE(out.lines.back() == "Workspace \"Nowhere\" not found.");
    proc.submit_line("");
    REQUIRE_FALSE(proc.has_active_command());

    proc.submit_line("WSCURRENT");
    REQUIRE(out.prompt == "Enter new value for WSCURRENT <\"Plan view\">: ");
    proc.submit_line("Drafting & Annotation");
    REQUIRE(view.current == "Drafting & Annotation");
    proc.submit_line("WSSAVE");
    REQUIRE(view.ops.back() == "dialog::");
}

namespace {
struct UcsView : musacad::command::ViewControl {
    int ucsicon = 3;
    void zoom_extents() override {}
    void zoom_scale(double /*f*/) override {}
    int selection_setting(const std::string& name) const override { return name == "UCSICON" ? ucsicon : 0; }
    bool set_selection_setting(const std::string& name, int value) override {
        if (name != "UCSICON") {
            return false;
        }
        ucsicon = value;
        return true;
    }
};
} // namespace

TEST_CASE("UCSICON turns the icon off and on, and keeps it at the origin or in the corner") {
    UcsView view;
    Out out;
    CommandProcessor proc{[](Command) {}, &view, out};
    proc.submit_line("UCSICON");
    REQUIRE(out.prompt == "Enter an option [ON/OFF/All/Noorigin/ORigin] <ON>: ");
    proc.submit_line("OFF");
    REQUIRE(view.ucsicon == 2);
    proc.submit_line("UCSICON");
    REQUIRE(out.prompt == "Enter an option [ON/OFF/All/Noorigin/ORigin] <OFF>: ");
    proc.submit_line("A");
    REQUIRE(out.prompt == "Enter an option [ON/OFF/Noorigin/ORigin] <OFF>: ");
    proc.submit_line("N");
    REQUIRE(view.ucsicon == 0);
    proc.submit_line("UCSICON");
    proc.submit_line("ON");
    proc.submit_line("UCSICON");
    proc.submit_line("OR");
    REQUIRE(view.ucsicon == 3);
    REQUIRE_FALSE(proc.has_active_command());
}

namespace {
struct ToleranceView : musacad::command::ViewControl {
    int answer = 1;
    void zoom_extents() override {}
    void zoom_scale(double /*f*/) override {}
    int tolerance_dialog(std::vector<std::string>& cells) override {
        cells = {"\\U+2316", "%%c0.05 \\U+24C2", "A", "B\\U+24C2"};
        return answer;
    }
};
} // namespace

TEST_CASE("TOLERANCE takes its frame from the Geometric Tolerance window, then asks where") {
    ToleranceView view;
    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[&cmds](Command c) { cmds.push_back(std::move(c)); }, &view, out};
    proc.submit_line("TOL");
    REQUIRE(out.prompt == "Enter tolerance location: ");
    proc.submit_line("10,20");
    const auto* f = std::get_if<AddFcfCommand>(&cmds.back());
    REQUIRE(f != nullptr);
    REQUIRE(f->cells == std::vector<std::string>{"\\U+2316", "%%c0.05 \\U+24C2", "A", "B\\U+24C2"});
    REQUIRE(f->pos == Vec2{10, 20});
    view.answer = 0; // Cancel: nothing placed
    const std::size_t before = cmds.size();
    proc.submit_line("TOL");
    REQUIRE_FALSE(proc.has_active_command());
    REQUIRE(cmds.size() == before);
}

TEST_CASE("Fields in AutoCAD's form: date formats, the drawing's properties, text case") {
    namespace text = musacad::core::text;
    text::FieldContext fc;
    fc.date = "2026-10-17";
    fc.time = "01:30";
    fc.filename = "site plan.musa";
    fc.path = "/home/x/site plan.musa";
    fc.login = "pranay";
    std::tm t{};
    t.tm_year = 2026 - 1900;
    t.tm_mon = 9;
    t.tm_mday = 7;
    t.tm_hour = 14;
    t.tm_min = 5;
    t.tm_isdst = -1;
    fc.now = static_cast<std::int64_t>(std::mktime(&t));
    fc.created = fc.now;
    fc.title = "Site plan";
    fc.author = "";
    fc.custom = {{"Client", "Musa Works"}};
    text::set_field_context(fc);
    CHECK(text::expand_fields("%<\\AcVar Date \\f \"M/d/yyyy\">%") == "10/7/2026");
    CHECK(text::expand_fields("%<\\AcVar Date \\f \"dddd, MMMM dd, yy h:mm tt\">%") == "Wednesday, October 07, 26 2:05 PM");
    CHECK(text::expand_fields("%<\\AcVar Date \\f \"yyyy-MM-dd HH:mm\">%") == "2026-10-07 14:05");
    CHECK(text::expand_fields("%<\\AcVar CreateDate \\f \"MMM d\">%") == "Oct 7");
    CHECK(text::expand_fields("%<\\AcVar SaveDate>%") == "----"); // never saved
    CHECK(text::expand_fields("%<Date>%") == "2026-10-17");        // the short form as before
    CHECK(text::expand_fields("%<\\AcVar Title \\f \"%tc1\">%") == "SITE PLAN");
    CHECK(text::expand_fields("%<\\AcVar Title \\f \"%tc4\">%") == "Site Plan");
    CHECK(text::expand_fields("%<\\AcVar Author>%") == "----");
    CHECK(text::expand_fields("%<\\AcVar CustomDP.client>%") == "Musa Works");
    CHECK(text::expand_fields("%<\\AcVar Filename \\f \"%fn1\">%") == "site plan");
    CHECK(text::expand_fields("%<\\AcVar Filename \\f \"%fn6\">%") == "/home/x/site plan.musa");
    CHECK(text::expand_fields("%<\\AcVar Nothing>%") == "####");
    text::set_field_context(text::make_field_context(""));
}

namespace {
struct FieldView : musacad::command::ViewControl {
    void zoom_extents() override {}
    void zoom_scale(double /*f*/) override {}
    int field_dialog(std::string& code) override {
        code = "%<\\AcVar Title \\f \"%tc1\">%";
        return 1;
    }
};
} // namespace

TEST_CASE("FIELD takes its field from the Field window, then places the text") {
    FieldView view;
    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[&cmds](Command c) { cmds.push_back(std::move(c)); }, &view, out};
    proc.submit_line("FIELD");
    REQUIRE(out.prompt == "Specify start point: ");
    proc.submit_line("5,5");
    proc.submit_line("");
    proc.submit_line("");
    const auto* t = std::get_if<AddTextCommand>(&cmds.back());
    REQUIRE(t != nullptr);
    REQUIRE(t->content == "%<\\AcVar Title \\f \"%tc1\">%");
    REQUIRE(t->pos == Vec2{5, 5});
}

TEST_CASE("BASE asks for the base point; INSERT takes a drawing file in place of a block name") {
    H h;
    h.run({"BASE"});
    REQUIRE(h.out.prompt == "Enter base point <0.0000,0.0000,0.0000>: ");
    h.run({"5,6"});
    REQUIRE(h.last<SetInsBaseCommand>()->base == Vec2{5, 6});
    h.run({"-INSERT", "/tmp/parts/bracket.dxf"});
    REQUIRE(h.last<LoadBlockFileCommand>()->path == "/tmp/parts/bracket.dxf");
    REQUIRE(h.last<LoadBlockFileCommand>()->name == "bracket");
    REQUIRE_FALSE(h.last<LoadBlockFileCommand>()->redefine);
    REQUIRE(h.out.prompt == "Specify insertion point or [Scale/Rotate]: ");
    h.run({"0,0", "", "", ""});
    REQUIRE(h.last<InsertBlockCommand>()->name == "bracket");
}

TEST_CASE("INSBASE is kept in the drawing and in DXF, and a drawing inserted as a block sits on it") {
    namespace fs = std::filesystem;
    const std::string part = (fs::temp_directory_path() / "musacad_part_insbase.musa").string();
    {
        GeometryEngine engine;
        engine.start();
        engine.submit(AddCircleCommand{{10, 10}, 5, 1});
        engine.submit(SetInsBaseCommand{{10, 10}});
        engine.consume_snapshot();
        const std::uint64_t before = engine.snapshot().document_version;
        engine.submit(SaveDocumentCommand{part, false});
        REQUIRE(wait_until(engine, [before](const auto& s) { return s.document_version > before; }));
        REQUIRE(wait_until(engine, [](const auto& s) { return s.insbase == Vec2{10, 10}; }));
        engine.stop();
    }
    io::Document saved;
    REQUIRE(io::load_native(part, saved).ok);
    REQUIRE(saved.insbase == Vec2{10, 10});
    const std::string dxf = (fs::temp_directory_path() / "musacad_insbase.dxf").string();
    REQUIRE(io::save_dxf(saved, dxf).ok);
    io::Document back;
    REQUIRE(io::load_dxf(dxf, back).ok);
    REQUIRE(back.insbase == Vec2{10, 10});
    fs::remove(dxf);

    GeometryEngine host;
    host.start();
    host.submit(LoadBlockFileCommand{part, "musacad_part_insbase", false});
    const io::Document doc = dump(host, "musacad_host_insbase.musa");
    const auto it = std::find_if(doc.block_defs.begin(), doc.block_defs.end(),
                                 [](const io::DocBlockDef& b) { return b.name == "musacad_part_insbase"; });
    REQUIRE(it != doc.block_defs.end());
    REQUIRE(it->base == Vec2{10, 10});
    REQUIRE(it->circles.size() == 1);
    REQUIRE(it->xref_path.empty());
    host.stop();
    fs::remove(part);
}

TEST_CASE("TEXTSIZE sets the height TEXT offers") {
    H h;
    const double keep = h.proc.text_size();
    h.proc.set_text_size(2.5);
    h.run({"TEXTSIZE"});
    REQUIRE(h.out.prompt == "Enter new value for TEXTSIZE <2.5000>: ");
    h.run({"-1"});
    REQUIRE(h.out.lines.back() == "Requires a positive value.");
    h.run({"3.5"});
    REQUIRE(h.proc.text_size() == 3.5);
    h.run({"TEXT", "0,0"});
    REQUIRE(h.out.prompt == "Specify height <3.5000>: ");
    h.proc.cancel();
    h.proc.set_text_size(keep);
}

TEST_CASE("A table from a CSV data link, read again and written back by DATALINKUPDATE") {
    namespace fs = std::filesystem;
    const std::string csv = (fs::temp_directory_path() / "musacad_link.csv").string();
    {
        std::ofstream f(csv, std::ios::binary);
        f << "Tag,Qty\r\nM8 bolt,\"1,200\"\r\n\"Say \"\"hi\"\"\",3\r\n";
    }
    GeometryEngine engine;
    engine.start();
    engine.submit(SetDataLinkCommand{"Parts", csv, false});
    engine.submit(InsertLinkedTableCommand{"parts", {0, 0}, 1});
    io::Document doc = dump(engine, "musacad_link1.musa");
    REQUIRE(doc.data_links.size() == 1);
    REQUIRE(doc.data_links[0].name == "Parts");
    REQUIRE(doc.tables.size() == 1);
    REQUIRE(doc.tables[0].link == 0);
    REQUIRE(doc.tables[0].rows == 3);
    REQUIRE(doc.tables[0].cols == 2);
    REQUIRE(doc.tables[0].cells[3].text == "1,200");
    REQUIRE(doc.tables[0].cells[4].text == "Say \"hi\"");

    { // the file changes: the same grid, new values
        std::ofstream f(csv, std::ios::binary | std::ios::trunc);
        f << "Tag,Qty\nM8 bolt,1300\nWasher,9\n";
    }
    DataLinkUpdateCommand all;
    all.group = 2;
    engine.submit(all);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 table updated."; }));
    doc = dump(engine, "musacad_link2.musa");
    REQUIRE(doc.tables[0].cells[3].text == "1300");
    REQUIRE(doc.tables[0].cells[4].text == "Washer");
    REQUIRE(doc.tables[0].link == 0);

    // Undo brings the old values back; the update was one step.
    engine.submit(UndoLastGroupCommand{});
    doc = dump(engine, "musacad_link3.musa");
    REQUIRE(doc.tables[0].cells[3].text == "1,200");
    engine.submit(RedoLastGroupCommand{});

    DataLinkUpdateCommand write;
    write.write = true;
    write.link = "Parts";
    engine.submit(write);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 data link written."; }));
    {
        std::ifstream f(csv, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        REQUIRE(text == "Tag,Qty\r\nM8 bolt,1300\r\nWasher,9\r\n");
    }
    engine.submit(SetDataLinkCommand{"PARTS", {}, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Data link \"PARTS\" deleted."; }));
    engine.submit(DataLinkUpdateCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "No tables with a data link."; }));
    engine.stop();
    fs::remove(csv);
}

TEST_CASE("DATALINKUPDATE asks Update or Write, then the tables, a link or all of them") {
    H h;
    h.proc.set_data_link_names({"Parts"});
    h.run({"DATALINKUPDATE"});
    REQUIRE(h.out.prompt == "Select an option [Update data link/Write data link] <Update data link>: ");
    h.run({""});
    REQUIRE(h.out.prompt == "Select objects or [Data link/all data linKs]: ");
    h.run({"D"});
    REQUIRE(h.out.prompt == "Enter data link name or [?]: ");
    h.run({"Nope"});
    REQUIRE(h.out.lines.back() == "Data link \"Nope\" not found.");
    h.run({"parts"});
    REQUIRE(h.last<DataLinkUpdateCommand>()->link == "parts");
    REQUIRE_FALSE(h.last<DataLinkUpdateCommand>()->write);
    h.run({"DLU", "", "K"});
    REQUIRE(h.last<DataLinkUpdateCommand>()->link.empty());
    REQUIRE_FALSE(h.last<DataLinkUpdateCommand>()->selection);
    h.run({"DLU", "W"});
    REQUIRE(h.out.prompt == "Select objects: ");
    REQUIRE(h.proc.has_active_command());
    h.proc.cancel();
}

TEST_CASE("DIMBREAK breaks a dimension's lines where a line crosses them, by hand, and takes the breaks away") {
    GeometryEngine engine;
    engine.start();
    // A horizontal dimension 0..100 with its line at y=20; a vertical line crosses it at x=50.
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear), {0, 0}, {100, 0}, {50, 20}, 0, 1});
    engine.submit(AddLineCommand{{50, -10}, {50, 40}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    DimBreakCommand brk;
    brk.pick = {30, 20};
    brk.pick_radius = 1.0;
    brk.group = 2;
    engine.submit(brk);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 object modified."; }));
    io::Document doc = dump(engine, "musacad_dimbreak1.musa");
    REQUIRE(doc.dims.size() == 1);
    REQUIRE(doc.dims[0].breaks.size() == 2);
    REQUIRE(doc.dims[0].breaks[0].x == Approx(50.0));
    REQUIRE(doc.dims[0].breaks[0].y == Approx(20.0));

    // The dimension line now stops short of x=50 on both sides, by half the break (3.75).
    GeometryStore store;
    io::populate_store(store, doc);
    const DimData* d = nullptr;
    for (std::uint32_t i = 0; i < store.dimensions().slot_count(); ++i) {
        if (store.dimensions().alive(i)) {
            d = &store.dimensions().data()[i];
        }
    }
    REQUIRE(d != nullptr);
    const DimGeometry g = compute_dim_geometry(*d, *store.dimstyle(d->style), Rgb{}, store.dim_text_parts(*d));
    bool gap = true;
    for (std::size_t i = 0; i + 1 < g.dim_lines.size(); i += 2) {
        const double x0 = std::min(g.dim_lines[i].x, g.dim_lines[i + 1].x);
        const double x1 = std::max(g.dim_lines[i].x, g.dim_lines[i + 1].x);
        if (std::abs(g.dim_lines[i].y - 20.0) < 1e-9 && x0 < 50.0 - 1.8 && x1 > 50.0 + 1.8) {
            gap = false; // one piece runs through the crossing
        }
    }
    REQUIRE(gap);

    // Undo takes the breaks away again; Remove does too.
    engine.submit(UndoLastGroupCommand{});
    doc = dump(engine, "musacad_dimbreak2.musa");
    REQUIRE(doc.dims[0].breaks.empty());
    DimBreakCommand manual;
    manual.mode = DimBreakCommand::Mode::Manual;
    manual.pick = {30, 20};
    manual.pick_radius = 1.0;
    manual.p1 = {10, 21};
    manual.p2 = {20, 19};
    manual.group = 3;
    engine.submit(manual);
    doc = dump(engine, "musacad_dimbreak3.musa");
    REQUIRE(doc.dims[0].breaks.size() == 2);
    REQUIRE(doc.dims[0].breaks[0].x == Approx(10.0));
    REQUIRE(doc.dims[0].breaks[0].y == Approx(20.0)); // onto the line
    REQUIRE(doc.dims[0].breaks[1].x == Approx(20.0));
    DimBreakCommand remove;
    remove.mode = DimBreakCommand::Mode::Remove;
    remove.pick = {30, 20};
    remove.pick_radius = 1.0;
    engine.submit(remove);
    doc = dump(engine, "musacad_dimbreak4.musa");
    REQUIRE(doc.dims[0].breaks.empty());
    engine.stop();
}

TEST_CASE("DIMBREAK asks for the dimension, then the object or Auto, Manual or Remove") {
    H h;
    h.run({"DIMBREAK"});
    REQUIRE(h.out.prompt == "Select dimension to add/remove break or [Multiple]: ");
    h.run({"30,20"});
    REQUIRE(h.out.prompt == "Select object to break dimension or [Auto/Manual/Remove] <Auto>: ");
    h.run({""});
    REQUIRE(h.last<DimBreakCommand>()->mode == DimBreakCommand::Mode::Auto);
    REQUIRE(h.last<DimBreakCommand>()->pick == Vec2{30, 20});
    h.run({"DIMBREAK", "30,20", "M"});
    REQUIRE(h.out.prompt == "Specify first break point: ");
    h.run({"10,20", "20,20"});
    REQUIRE(h.last<DimBreakCommand>()->mode == DimBreakCommand::Mode::Manual);
    REQUIRE(h.last<DimBreakCommand>()->p2 == Vec2{20, 20});
    h.run({"DIMBREAK", "30,20", "50,0"});
    REQUIRE(h.last<DimBreakCommand>()->mode == DimBreakCommand::Mode::Object);
    REQUIRE(h.out.prompt == "Select object to break dimension: ");
    h.run({""});
    REQUIRE_FALSE(h.proc.has_active_command());
    h.run({"DIMBREAK", "30,20", "R"});
    REQUIRE(h.last<DimBreakCommand>()->mode == DimBreakCommand::Mode::Remove);
}

TEST_CASE("A block's units scale its inserts, and travel with it in the drawing and in DXF") {
    io::Document doc;
    io::DocBlockDef b;
    b.name = "MM_PART";
    b.units = 4; // millimetres
    b.lines.push_back(io::DocLine{{0, 0}, {25.4, 0}, {}});
    doc.block_defs.push_back(b);
    const std::string native = io::serialize_native(doc);
    io::Document back;
    REQUIRE(io::parse_native(native, back).ok);
    REQUIRE(back.block_defs.size() == 1);
    REQUIRE(back.block_defs[0].units == 4);
    const std::string dxf = (std::filesystem::temp_directory_path() / "musacad_block_units.dxf").string();
    REQUIRE(io::save_dxf(doc, dxf).ok);
    io::Document dback;
    REQUIRE(io::load_dxf(dxf, dback).ok);
    std::filesystem::remove(dxf);
    const auto it = std::find_if(dback.block_defs.begin(), dback.block_defs.end(),
                                 [](const io::DocBlockDef& d) { return d.name == "MM_PART"; });
    REQUIRE(it != dback.block_defs.end());
    REQUIRE(it->units == 4);

    // Into a drawing in inches: the millimetre block comes in at 1/25.4.
    GeometryEngine engine;
    engine.start();
    io::Document host;
    host.display_units.insunits = 1; // inches
    host.block_defs.push_back(b);
    const std::string hp = (std::filesystem::temp_directory_path() / "musacad_block_units_host.musa").string();
    REQUIRE(io::save_native(host, hp).ok);
    engine.submit(OpenDocumentCommand{hp});
    engine.submit(InsertBlockCommand{"MM_PART", {0, 0}, 2.0, 2.0, 0.0, 1, {}});
    const io::Document out = dump(engine, "musacad_block_units_out.musa");
    REQUIRE(out.inserts.size() == 1);
    REQUIRE(out.inserts[0].scale_x == Approx(2.0 / 25.4));
    REQUIRE(out.inserts[0].scale_y == Approx(2.0 / 25.4));
    engine.stop();
    std::filesystem::remove(hp);
}

TEST_CASE("INSUNITSDEFSOURCE and INSUNITSDEFTARGET are asked for and handed to the engine") {
    H h;
    h.run({"INSUNITSDEFSOURCE"});
    REQUIRE(h.out.prompt == "Enter new value for INSUNITSDEFSOURCE <4>: ");
    h.run({"25"});
    REQUIRE(h.out.lines.back() == "Requires an integer between 0 and 24.");
    h.run({"1"});
    REQUIRE(h.last<SetUnitsDefaultsCommand>()->source == 1);
    REQUIRE(h.last<SetUnitsDefaultsCommand>()->target == 4);
    h.run({"INSUNITSDEFSOURCE", "4"}); // as it was, for the other tests
    REQUIRE(h.last<SetUnitsDefaultsCommand>()->source == 4);
}

TEST_CASE("The command list CUI shows has every command once, by name, in order") {
    const auto all = musacad::command::CommandRegistry::make_default().commands();
    REQUIRE(all.size() > 100);
    REQUIRE(std::is_sorted(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.name < b.name; }));
    REQUIRE(std::count_if(all.begin(), all.end(), [](const auto& c) { return c.name == "LINE"; }) == 1);
    REQUIRE(std::none_of(all.begin(), all.end(), [](const auto& c) { return c.name == "L"; }));
}

TEST_CASE("STRETCH's Copy leaves the original where it is and adds the stretched one") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(SelectPickCommand{{5, 0}, 1.0, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.grips.empty(); }));
    GripInfo gb{};
    for (const GripInfo& g : engine.snapshot().grips) {
        if (g.index == 1) {
            gb = g;
        }
    }
    engine.submit(GripDragCommand{GripDragCommand::Phase::Begin, gb.handle, gb.index, {}, 0});
    engine.submit(GripDragCommand{GripDragCommand::Phase::Commit, {}, 0, {10, 10}, 7, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Copied."; }));
    engine.submit(GripDragCommand{GripDragCommand::Phase::Begin, gb.handle, gb.index, {}, 0});
    engine.submit(GripDragCommand{GripDragCommand::Phase::Commit, {}, 0, {10, -10}, 8, true});
    const io::Document doc = dump(engine, "musacad_grip_copy.musa");
    REQUIRE(doc.lines.size() == 3);
    REQUIRE(doc.lines[0].b == Vec2{10, 0}); // the original
    engine.stop();
}

TEST_CASE("MPEDIT starts at PEDIT's Multiple; DIMREASSOC takes dimension text overrides away") {
    H h;
    h.run({"MPEDIT"});
    REQUIRE(h.out.prompt == "Select objects: ");
    h.proc.cancel();
    h.proc.set_selection_count(2);
    h.run({"DIMREASSOC"});
    const auto* c = h.last<DimEditCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->op == DimEditCommand::Op::New);
    REQUIRE(c->text.empty());
    REQUIRE_FALSE(c->pick.has_value());
}

TEST_CASE("ATTOUT writes attribute values to a tab-separated file; ATTIN reads them back") {
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() / "musacad_attout.txt").string();
    GeometryEngine engine;
    engine.start();
    // A block with two attributes, and a reference of it.
    io::Document doc;
    io::DocBlockDef b;
    b.name = "TAGBOX";
    io::DocAttDef a1;
    a1.text.content = "NUM";
    a1.def = "1";
    io::DocAttDef a2;
    a2.text.content = "NOTE";
    a2.def = "";
    b.attdefs = {a1, a2};
    b.lines.push_back(io::DocLine{{0, 0}, {5, 0}, {}});
    doc.block_defs.push_back(b);
    io::DocInsert ins;
    ins.block_name = "TAGBOX";
    ins.attribs = {io::DocAttrib{"NUM", "7"}, io::DocAttrib{"NOTE", "old"}};
    doc.inserts.push_back(ins);
    const std::string dp = (fs::temp_directory_path() / "musacad_attout_src.musa").string();
    REQUIRE(io::save_native(doc, dp).ok);
    engine.submit(OpenDocumentCommand{dp});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(AttOutCommand{path});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("1 block reference written to ", 0) == 0; }));
    std::string text;
    {
        std::ifstream f(path, std::ios::binary);
        text.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
    REQUIRE(text.rfind("HANDLE\tBLOCKNAME\tNUM\tNOTE\r\n'", 0) == 0);
    REQUIRE(text.find("\tTAGBOX\t7\told\r\n") != std::string::npos);
    { // the spreadsheet changes NOTE
        const std::size_t at = text.find("\told\r\n");
        text.replace(at, 6, "\tnew\r\n");
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << text;
    }
    engine.submit(AttInCommand{path, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 block reference updated."; }));
    const io::Document out = dump(engine, "musacad_attin.musa");
    REQUIRE(out.inserts.size() == 1);
    REQUIRE(out.inserts[0].attribs.size() == 2);
    REQUIRE(out.inserts[0].attribs[1].value == "new");
    REQUIRE(out.inserts[0].attribs[0].value == "7");
    engine.stop();
    fs::remove(path);
    fs::remove(dp);
}

TEST_CASE("XLIST says what an object inside a block reference is, and its properties") {
    namespace fs = std::filesystem;
    io::Document doc;
    io::DocBlockDef b;
    b.name = "PLATE";
    b.lines.push_back(io::DocLine{{0, 0}, {10, 0}, {}});
    doc.block_defs.push_back(b);
    io::DocInsert ins;
    ins.block_name = "PLATE";
    ins.pos = {100, 100};
    doc.inserts.push_back(ins);
    const std::string dp = (fs::temp_directory_path() / "musacad_xlist.musa").string();
    REQUIRE(io::save_native(doc, dp).ok);
    GeometryEngine engine;
    engine.start();
    engine.submit(OpenDocumentCommand{dp});
    NestedCopyCommand x{{105, 100}, 1.0, {}, 0};
    x.list = true;
    engine.submit(x);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status == "Object: Line  Block name: PLATE  Layer: 0  Color: BYLAYER  Linetype: BYLAYER";
    }));
    const io::Document out = dump(engine, "musacad_xlist_out.musa");
    REQUIRE(out.lines.empty()); // nothing copied
    engine.stop();
    fs::remove(dp);
}

TEST_CASE("VPSYNC asks for the master viewport, then the viewports to line up with it") {
    H h;
    h.run({"VPSYNC"});
    REQUIRE(h.out.prompt == "Select master viewport: ");
    h.run({"10,10"});
    REQUIRE(h.out.prompt == "Select viewports to be aligned to master viewport: ");
    h.proc.set_selection_count(2);
    h.run({""});
    REQUIRE(h.last<VpSyncCommand>() != nullptr);
    REQUIRE(h.last<VpSyncCommand>()->master == Vec2{10, 10});
}

TEST_CASE("EXTRIM trims everything crossing a cutting edge on the side picked") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 10, 1});                // the cutting edge
    engine.submit(AddLineCommand{{-20, 0}, {20, 0}, 1});           // crosses it twice
    engine.submit(AddLineCommand{{0, -20}, {0, 5}, 1});            // crosses it once
    engine.submit(AddLineCommand{{30, 30}, {40, 40}, 1});          // nowhere near
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(ExtrimCommand{{7.0710678, 7.0710678}, {0, 1}, 0.5, 2}); // the circle; inside goes
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "2 objects trimmed."; }));
    const io::Document doc = dump(engine, "musacad_extrim.musa");
    REQUIRE(doc.circles.size() == 1);
    REQUIRE(doc.lines.size() == 4); // the long line in two, the short one cut back, the far one
    for (const auto& l : doc.lines) {
        const double mx = (l.a.x + l.b.x) * 0.5;
        const double my = (l.a.y + l.b.y) * 0.5;
        REQUIRE(std::hypot(mx, my) > 9.9); // nothing left inside
    }
    engine.stop();
}

TEST_CASE("EXTRIM asks for the cutting edge, then the side") {
    H h;
    h.run({"EXTRIM"});
    REQUIRE(h.out.prompt == "Pick a POLYLINE, LINE, CIRCLE, ARC, ELLIPSE, IMAGE or TEXT for cutting edge..");
    h.run({"10,0"});
    REQUIRE(h.out.prompt == "Specify the side to trim on:");
    h.run({"0,1"});
    REQUIRE(h.last<ExtrimCommand>()->edge == Vec2{10, 0});
    REQUIRE(h.last<ExtrimCommand>()->side == Vec2{0, 1});
}

TEST_CASE("Alternate units below the primary value when DIMAPOST starts with \\X") {
    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {50, 0};
    d.line_pt = {25, 10};
    DimStyle st;
    st.alt = true;
    st.alt_factor = 1.0 / 25.4;
    st.alt_precision = 2;
    DimLabel beside = compose_dim_label(d, st, {});
    REQUIRE(beside.line1.find(" [") != std::string::npos);
    REQUIRE(beside.line2.empty());
    st.alt_post = "\\X";
    DimLabel below = compose_dim_label(d, st, {});
    REQUIRE(below.line1.find('[') == std::string::npos);
    REQUIRE(below.line2.front() == '[');
    REQUIRE(below.line2.back() == ']');
    // A stacked tolerance keeps the line below: the alternate value goes beside the value.
    st.tol_method = 3; // Limits
    st.tol_upper = 0.1;
    st.tol_lower = 0.05;
    DimLabel lim = compose_dim_label(d, st, {});
    REQUIRE(lim.line1.find('\x1f') == std::string::npos);
    REQUIRE(lim.line2.find('\x1f') == std::string::npos);
}

TEST_CASE("Feet and inches drop zero feet and zero inches as DIMZIN says") {
    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.line_pt = {3, 10};
    DimStyle st;
    st.linear_unit = 4; // Architectural
    st.precision = 4;
    const auto text = [&](double inches, int mode) {
        d.b = {inches, 0};
        st.zero_suppression = static_cast<std::uint8_t>(mode);
        return compose_dim_label(d, st, {}).line1;
    };
    REQUIRE(text(6, 1) == "0'-6\"");
    REQUIRE(text(12, 1) == "1'-0\"");
    REQUIRE(text(6, 0) == "6\"");
    REQUIRE(text(12, 0) == "1'");
    REQUIRE(text(18, 0) == "1'-6\"");
    REQUIRE(text(6, 2) == "0'-6\"");
    REQUIRE(text(12, 2) == "1'");
    REQUIRE(text(6, 3) == "6\"");
    REQUIRE(text(12, 3) == "1'-0\"");

    // A drawing saved before v40 keeps showing both.
    io::Document doc;
    DimStyle old;
    old.name = "Imperial";
    old.zero_suppression = 0;
    doc.dimstyles.push_back(old);
    doc.format_version = 39;
    io::Document back;
    REQUIRE(io::parse_native(io::serialize_native(doc), back).ok);
    REQUIRE((back.dimstyles.back().zero_suppression & kDimZinFeetInches) == 1);
    doc.format_version = 40;
    io::Document now;
    REQUIRE(io::parse_native(io::serialize_native(doc), now).ok);
    REQUIRE((now.dimstyles.back().zero_suppression & kDimZinFeetInches) == 0);
}

TEST_CASE("Multileader styles: added, made current, kept in the drawing; MLEADER draws with the current one") {
    GeometryEngine engine;
    engine.start();
    MLeaderStyle big;
    big.name = "Big notes";
    big.text_height = 5.0;
    big.landing_distance = 10.0;
    big.landing_gap = 2.0;
    big.arrow = 3; // dot
    big.arrow_size = 4.0;
    engine.submit(MLeaderStyleCommand{MLeaderStyleCommand::Op::Add, {}, big});
    engine.submit(MLeaderStyleCommand{MLeaderStyleCommand::Op::SetCurrent, "big notes", {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.current_mleader_style == 1; }));
    io::Document doc = dump(engine, "musacad_mls.musa");
    REQUIRE(doc.mleader_styles.size() == 2);
    REQUIRE(doc.mleader_styles[1] == big);
    REQUIRE(doc.current_mleader_style == 1);
    engine.submit(MLeaderStyleCommand{MLeaderStyleCommand::Op::Delete, "Big notes", {}});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status == "Multileader style \"Big notes\" cannot be deleted: it is Standard or the current style.";
    }));
    engine.stop();

    H h;
    h.proc.set_mleader_styles({MLeaderStyle{}, big}, 1);
    h.run({"MLD", "0,0", "10,10", "NOTE"});
    const auto* m = h.last<AddMLeaderCommand>();
    REQUIRE(m != nullptr);
    REQUIRE(m->block.height == 5.0);
    REQUIRE(m->vertices[2].x == Approx(20.0));      // a 10 landing
    REQUIRE(m->block.pos.x == Approx(22.0));         // and a 2 gap
    REQUIRE(m->overrides.arrow_type == 3);
    REQUIRE(m->overrides.has(DimOverrides::kArrowType));
    REQUIRE(m->overrides.arrow_size == 4.0);
}

TEST_CASE("PAGESETUP's Set Current in a layout gives the layout that page setup") {
    GeometryEngine engine;
    engine.start();
    PageSetup a3;
    a3.name = "A3 landscape";
    a3.paper = "ISO A3";
    a3.paper_w_mm = 420.0;
    a3.paper_h_mm = 297.0;
    a3.landscape = true;
    engine.submit(AddPageSetupCommand{a3});
    engine.submit(SetLayoutPageCommand{"A3 landscape"});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status == "A page setup is given to a layout: make a layout current first.";
    }));
    engine.submit(SetActiveSpaceCommand{0xFF, {}});
    engine.submit(SetLayoutPageCommand{"A3 landscape"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("Page setup \"A3 landscape\" is current for layout", 0) == 0; }));
    const io::Document doc = dump(engine, "musacad_layout_page.musa");
    REQUIRE(!doc.layouts.empty());
    REQUIRE(doc.layouts.front().page.name == "A3 landscape");
    REQUIRE(doc.layouts.front().page.paper_w_mm == 420.0);
    engine.stop();
}

TEST_CASE("A feature control frame's second row and datum identifier stand under it, and travel through DXF") {
    DimStyle s;
    s.text_height = 2.5;
    const std::vector<std::string> cells = {"\\U+2316", "0.1", "A", "^J", "6 \\U+24C5", "^J", "-B-"};
    std::vector<std::string_view> views(cells.begin(), cells.end());
    FcfData f;
    f.pos = {0.0, 0.0};
    f.cell_count = static_cast<std::uint32_t>(cells.size());
    const FcfGeometry g = compute_fcf_geometry(f, views, s, Rgb{255, 255, 255});
    REQUIRE(g.cell_quads.size() == cells.size());
    REQUIRE(g.text_pos.size() == cells.size());
    const double box = 2.0 * s.text_height;
    REQUIRE(g.cell_quads[0][0].y == Approx(0.0));        // the first row
    REQUIRE(g.cell_quads[4][0].y == Approx(-box));       // the projected zone, under it
    REQUIRE(g.cell_quads[6][0].y == Approx(-2.0 * box)); // the datum identifier, under that
    REQUIRE(g.cell_text[3].empty());

    io::Document doc;
    io::DocFcf d;
    d.cells = cells;
    doc.fcfs.push_back(d);
    const std::string path = (std::filesystem::temp_directory_path() / "musacad_fcf_rows.dxf").string();
    REQUIRE(io::save_dxf(doc, path).ok);
    io::Document back;
    REQUIRE(io::load_dxf(path, back).ok);
    std::filesystem::remove(path);
    REQUIRE(!back.fcfs.empty());
    REQUIRE(back.fcfs[0].cells == cells);
}

TEST_CASE("DONUT carries its ring at the cursor while the centre is asked for") {
    H h;
    h.run({"DONUT", "2", "4"});
    REQUIRE(h.out.prompt == "Specify center of donut or <exit>: ");
    REQUIRE(h.proc.preview().kind == musacad::command::PreviewKind::Outline);
    REQUIRE(h.proc.preview().points.size() == 2 * 2 * 48); // both edges
    REQUIRE(h.proc.preview().points[0].x == Approx(1.0));  // the inner edge, radius 1
    h.run({""});
    REQUIRE(h.proc.preview().kind == musacad::command::PreviewKind::None);
}

TEST_CASE("SPLINE's Object makes the polylines picked into splines") {
    H h;
    h.run({"SPLINE", "O"});
    REQUIRE(h.out.prompt == "Select spline-fit polylines: ");
    h.proc.set_selection_count(1);
    h.run({""});
    const auto* c = h.last<PeditCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->op == 4);
    REQUIRE(c->selection);
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("The Block Editor: a block's objects to edit, the rest hidden; BCLOSE saves what is shown as the block") {
    namespace fs = std::filesystem;
    io::Document doc;
    io::DocBlockDef b;
    b.name = "BOX";
    b.base = {5, 5};
    b.lines.push_back(io::DocLine{{0, 0}, {10, 0}, {}});
    doc.block_defs.push_back(b);
    io::DocInsert ins;
    ins.block_name = "BOX";
    ins.pos = {100, 100};
    doc.inserts.push_back(ins);
    doc.circles.push_back(io::DocCircle{{50, 50}, 3.0, {}});
    const std::string dp = (fs::temp_directory_path() / "musacad_bedit.musa").string();
    REQUIRE(io::save_native(doc, dp).ok);
    GeometryEngine engine;
    engine.start();
    engine.submit(OpenDocumentCommand{dp});
    engine.submit(BlockEditCommand{BlockEditCommand::Op::Open, "box", true, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("Block Editor: \"BOX\" is open", 0) == 0; }));
    REQUIRE(engine.snapshot().block_editor); // the Block Editor tab shows
    engine.submit(SaveDocumentCommand{dp, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("Close the Block Editor", 0) == 0; }));
    engine.submit(AddLineCommand{{0, 10}, {10, 10}, 3}); // drawn in the Block Editor
    engine.submit(BlockEditCommand{BlockEditCommand::Op::Close, {}, true, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Block \"BOX\" saved."; }));
    REQUIRE_FALSE(engine.snapshot().block_editor);
    const io::Document out = dump(engine, "musacad_bedit_out.musa");
    const auto it = std::find_if(out.block_defs.begin(), out.block_defs.end(), [](const auto& d) { return d.name == "BOX"; });
    REQUIRE(it != out.block_defs.end());
    REQUIRE(it->lines.size() == 2);       // the new line is in the block
    REQUIRE(it->base == Vec2{5, 5});
    REQUIRE(out.inserts.size() == 1);      // the drawing's reference, and no other
    REQUIRE(out.circles.size() == 1);      // shown again
    REQUIRE(out.lines.empty());            // nothing left loose
    engine.stop();
    fs::remove(dp);
}

TEST_CASE("-BEDIT asks for the block; BCLOSE asks whether to save") {
    H h;
    h.run({"-BEDIT"});
    REQUIRE(h.out.prompt == "Enter block name or [?]: ");
    h.run({"Door A"});
    REQUIRE(h.last<BlockEditCommand>()->name == "Door A");
    REQUIRE(h.last<BlockEditCommand>()->op == BlockEditCommand::Op::Open);
    h.run({"BCLOSE"});
    REQUIRE(h.out.prompt == "Save changes to block? [Yes/No] <Yes>: ");
    h.run({"N"});
    REQUIRE_FALSE(h.last<BlockEditCommand>()->save);
    h.run({"BSAVE"});
    REQUIRE(h.last<BlockEditCommand>()->op == BlockEditCommand::Op::Save);
}

TEST_CASE("-TABLE's Auto fits the columns and the data rows to a window") {
    H h;
    h.run({"-TABLE", "A"});
    REQUIRE(h.out.prompt == "Specify first corner: ");
    h.run({"0,100", "200,0"});
    const auto* t = h.last<AddTableCommand>();
    REQUIRE(t != nullptr);
    REQUIRE(t->cols == 3);                       // 200 / 63.5, rounded
    REQUIRE(t->col_widths[0] == Approx(200.0 / 3.0));
    REQUIRE(t->pos == Vec2{0, 100});             // the top-left corner
    REQUIRE(t->rows == 11);                      // 100 / 9 lines, title and header among them
}

TEST_CASE("A view saved with a layer snapshot keeps the layers' state; deleting the view deletes it") {
    GeometryEngine engine;
    engine.start();
    engine.submit(SaveNamedViewCommand{NamedView{"Plan", {0, 0}, 1.0, "*VIEW|Plan"}});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.layer_state_names.size() == 1 && s.layer_state_names[0] == "*VIEW|Plan";
    }));
    const io::Document doc = dump(engine, "musacad_view_snapshot.musa");
    REQUIRE(doc.views.size() == 1);
    REQUIRE(doc.views[0].layer_state == "*VIEW|Plan");
    engine.submit(DeleteNamedViewCommand{"Plan"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layer_state_names.empty() && s.named_views.empty(); }));
    engine.stop();
}

TEST_CASE("A hatch pattern Musa CAD does not have comes with the drawing, from DXF and back") {
    io::Document doc;
    hatch::Pattern pat;
    pat.name = "MYPAT";
    hatch::PatternLine l;
    l.angle = 45.0;
    l.origin = {1.0, 0.0};
    l.delta = {0.0, 0.5};
    l.dashes = {0.25, -0.1};
    pat.lines.push_back(l);
    doc.patterns.push_back(pat);
    io::DocHatch h;
    h.loops = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    h.pattern_name = "MYPAT";
    h.pattern_scale = 2.0;
    h.pattern_angle = 0.5235987755982988; // 30 degrees
    doc.hatches.push_back(h);
    const std::string path = (std::filesystem::temp_directory_path() / "musacad_custom_pat.dxf").string();
    REQUIRE(io::save_dxf(doc, path).ok);
    io::Document back;
    REQUIRE(io::load_dxf(path, back).ok);
    std::filesystem::remove(path);
    REQUIRE(back.patterns.size() == 1);
    const hatch::PatternLine& b = back.patterns[0].lines.at(0);
    REQUIRE(back.patterns[0].name == "MYPAT");
    REQUIRE(b.angle == Approx(45.0));
    REQUIRE(b.origin.x == Approx(1.0));
    REQUIRE(b.origin.y == Approx(0.0).margin(1e-9));
    REQUIRE(b.delta.x == Approx(0.0).margin(1e-9));
    REQUIRE(b.delta.y == Approx(0.5));
    REQUIRE(b.dashes.size() == 2);
    REQUIRE(b.dashes[0] == Approx(0.25));

    // Kept in the drawing and drawn: the store finds it by name.
    GeometryStore store;
    io::populate_store(store, back);
    REQUIRE(store.pattern("mypat") != nullptr);
    io::Document again;
    REQUIRE(io::parse_native(io::serialize_native(io::document_from_store(store)), again).ok);
    REQUIRE(again.patterns.size() == 1);
    REQUIRE(again.patterns[0] == back.patterns[0]);
}

TEST_CASE("LAYMRG moves a layer's objects to another; LAYDEL erases them; one undo step each") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLayerCommand{Layer{"Walls"}});
    engine.submit(AddLayerCommand{Layer{"Doors"}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layers.size() == 3; }));
    AddLineCommand a{{0, 0}, {10, 0}, 1};
    a.props = EntityProps{};
    a.props->layer = 1; // Walls
    engine.submit(a);
    AddLineCommand b{{0, 5}, {10, 5}, 1};
    b.props = EntityProps{};
    b.props->layer = 2; // Doors
    engine.submit(b);
    LayerMergeCommand merge;
    merge.sources = {"doors"};
    merge.target = "Walls";
    merge.group = 2;
    engine.submit(merge);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("1 object merged from \"Doors\" into \"Walls\"", 0) == 0; }));
    io::Document doc = dump(engine, "musacad_laymrg.musa");
    REQUIRE(doc.lines.size() == 2);
    REQUIRE(doc.lines[0].props.layer == 1);
    REQUIRE(doc.lines[1].props.layer == 1);
    LayerMergeCommand del;
    del.sources = {"Walls"};
    del.erase = true;
    del.group = 3;
    engine.submit(del);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("2 objects deleted from \"Walls\"", 0) == 0; }));
    doc = dump(engine, "musacad_laydel.musa");
    REQUIRE(doc.lines.empty());
    REQUIRE(doc.layers.size() == 3); // the layers stay for PURGE
    engine.submit(UndoLastGroupCommand{});
    doc = dump(engine, "musacad_laydel_undo.musa");
    REQUIRE(doc.lines.size() == 2);
    engine.stop();
}

TEST_CASE("LAYMRG and LAYDEL ask for the layers, then warn") {
    H h;
    h.run({"LAYMRG"});
    REQUIRE(h.out.prompt == "Select object on layer to merge or [Name]: ");
    h.run({"N", "Doors"});
    REQUIRE(h.out.prompt == "Select object on layer to merge or [Name/Undo]: ");
    h.run({""});
    REQUIRE(h.out.prompt == "Select object on target layer or [Name]: ");
    h.run({"N", "Walls"});
    REQUIRE(h.out.prompt == "Do you wish to continue? [Yes/No] <No>: ");
    REQUIRE(h.out.lines.back() == "You are about to merge \"Doors\" into layer \"Walls\".");
    h.run({"Y"});
    REQUIRE(h.last<LayerMergeCommand>()->sources == std::vector<std::string>{"Doors"});
    REQUIRE(h.last<LayerMergeCommand>()->target == "Walls");
    h.run({"LAYDEL", "N", "Old", ""});
    REQUIRE(h.out.lines.back() == "You are about to delete layer \"Old\" from this drawing.");
    h.run({""}); // No
    REQUIRE(h.last<LayerMergeCommand>()->target == "Walls"); // nothing new submitted
}

TEST_CASE("-HATCH's Origin, hatch COlor and LAyer go with the hatches made") {
    H h;
    h.run({"-HATCH", "O"});
    REQUIRE(h.out.prompt == "[Use current origin/Set new origin/Default to boundary extents] <Use current origin>: ");
    h.run({"S", "5,7"});
    h.run({"CO"});
    REQUIRE(h.out.prompt == "Enter new color for new hatch objects or [. (use current)] <use current>: ");
    h.run({"1", "LA", "Hatching"});
    h.run({"10,10"});
    const auto* c = h.last<HatchPickPointCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->extras.origin == Vec2{5, 7});
    REQUIRE(c->extras.color == Rgb{255, 0, 0});
    REQUIRE(c->extras.layer == "Hatching");
    h.proc.cancel();
    // Back as they were, for the other tests.
    h.run({"-HATCH", "O", "U", "CO", ".", "LA", "."});
    h.proc.cancel();

    // The engine: a pick inside a square makes the hatch with them.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLayerCommand{Layer{"Hatching"}});
    engine.submit(AddPolylineCommand{{{0, 0}, {20, 0}, {20, 20}, {0, 20}}, true, 1});
    HatchPickPointCommand pick;
    pick.point = {10, 10};
    pick.pattern_name = "ANSI31";
    pick.group = 2;
    pick.extras.origin_corner = 4; // top right
    pick.extras.layer = "Hatching";
    engine.submit(pick);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Hatch created."; }));
    const io::Document doc = dump(engine, "musacad_hatch_extras.musa");
    REQUIRE(doc.hatches.size() == 1);
    REQUIRE(doc.hatches[0].pattern_origin == Vec2{20, 20});
    REQUIRE(doc.layers[doc.hatches[0].props.layer].name == "Hatching");
    engine.stop();
}

TEST_CASE("Associative dimensions follow MOVE, SCALE and grip edits of what they measure; DIMDISASSOCIATE stops that") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {20, 0}, 1});
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Aligned), {0, 0}, {20, 0}, {10, 5}, 0, 2});
    engine.submit(AddCircleCommand{{50, 0}, 5, 3});
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Radius), {50, 0}, {55, 0}, {58, 0}, 0, 4});
    // MOVE the line: its dimension goes along, whole.
    engine.submit(SelectPickCommand{{10, 0}, 0.5, false});
    engine.submit(MoveSelectionCommand{{5, 3}, 5});
    io::Document doc = dump(engine, "musacad_assoc_1.musa");
    REQUIRE(doc.dims.size() == 2);
    const auto aligned = [&]() -> const io::DocDim& {
        return doc.dims[0].type == static_cast<std::uint8_t>(DimType::Aligned) ? doc.dims[0] : doc.dims[1];
    };
    const auto radius = [&]() -> const io::DocDim& {
        return doc.dims[0].type == static_cast<std::uint8_t>(DimType::Radius) ? doc.dims[0] : doc.dims[1];
    };
    REQUIRE(aligned().a == Vec2{5, 3});
    REQUIRE(aligned().b == Vec2{25, 3});
    REQUIRE(aligned().line_pt == Vec2{15, 8});
    REQUIRE(radius().a == Vec2{50, 0}); // the circle stayed
    // One undo puts the line and its dimension back.
    engine.submit(UndoLastGroupCommand{});
    doc = dump(engine, "musacad_assoc_2.musa");
    REQUIRE(aligned().a == Vec2{0, 0});
    REQUIRE(doc.lines[0].a == Vec2{0, 0});
    engine.submit(RedoLastGroupCommand{});
    // SCALE the circle about its centre: the radius dimension's point on it goes out with it.
    engine.submit(SelectPickCommand{{50, 5}, 0.5, false});
    engine.submit(ScaleSelectionCommand{{50, 0}, 2.0, 6});
    doc = dump(engine, "musacad_assoc_3.musa");
    REQUIRE(radius().a == Vec2{50, 0});
    REQUIRE(std::abs(radius().b.x - 60.0) < 1e-9);
    // A grip edit of the line's end: only the point there follows.
    engine.submit(SelectPickCommand{{15, 3}, 0.5, false});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return std::any_of(s.grips.begin(), s.grips.end(),
                           [](const GripInfo& g) { return g.handle.kind == EntityKind::Line; });
    }));
    GripInfo end{};
    for (const GripInfo& g : engine.snapshot().grips) {
        if (g.handle.kind == EntityKind::Line && g.index == 1) {
            end = g;
        }
    }
    engine.submit(GripDragCommand{GripDragCommand::Phase::Begin, end.handle, end.index, {}, 0});
    engine.submit(GripDragCommand{GripDragCommand::Phase::Commit, {}, 0, {35, 3}, 7});
    doc = dump(engine, "musacad_assoc_4.musa");
    REQUIRE(aligned().a == Vec2{5, 3});
    REQUIRE(aligned().b == Vec2{35, 3});
    REQUIRE(aligned().line_pt == Vec2{15, 8});
    // DIMDISASSOCIATE: the dimensions stay where they are when the line moves again.
    engine.submit(SelectAllCommand{});
    engine.submit(DimAssocCommand{DimAssocCommand::Op::Disassociate, 2, 8});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "2 disassociated."; }));
    engine.submit(SelectPickCommand{{20, 3}, 0.5, false});
    engine.submit(MoveSelectionCommand{{0, 10}, 9});
    doc = dump(engine, "musacad_assoc_5.musa");
    REQUIRE(aligned().a == Vec2{5, 3});
    REQUIRE(aligned().unassoc);
    REQUIRE(radius().unassoc);
    REQUIRE(doc.lines[0].a == Vec2{5, 13});
    // DIMREASSOCIATE: they follow again.
    engine.submit(SelectAllCommand{});
    engine.submit(DimAssocCommand{DimAssocCommand::Op::Reassociate, 2, 10});
    engine.submit(SelectPickCommand{{50, 10}, 0.5, false});
    engine.submit(MoveSelectionCommand{{0, 10}, 11});
    doc = dump(engine, "musacad_assoc_6.musa");
    REQUIRE_FALSE(radius().unassoc);
    REQUIRE(radius().a == Vec2{50, 10});
    engine.stop();
}

TEST_CASE("A dimension point where another object stays does not follow; DIMASSOC 1 draws them not associative") {
    GeometryEngine engine;
    engine.start();
    // Two lines meeting at the origin; the dimension is to the corner and the first line's end.
    engine.submit(AddLineCommand{{0, 0}, {20, 0}, 1});
    engine.submit(AddLineCommand{{0, 0}, {0, 20}, 2});
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Aligned), {0, 0}, {20, 0}, {10, -5}, 0, 3});
    engine.submit(SelectPickCommand{{10, 0}, 0.5, false});
    engine.submit(MoveSelectionCommand{{0, -2}, 4});
    io::Document doc = dump(engine, "musacad_assoc_7.musa");
    REQUIRE(doc.dims[0].a == Vec2{0, 0});  // the corner: the other line is still there
    REQUIRE(doc.dims[0].b == Vec2{20, -2}); // the line's own end
    // DIMASSOC 1: a dimension drawn now does not follow.
    engine.submit(DimAssocCommand{DimAssocCommand::Op::Variable, 1, 0});
    engine.submit(AddCircleCommand{{50, 0}, 5, 5});
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Radius), {50, 0}, {55, 0}, {58, 0}, 0, 6});
    engine.submit(SelectPickCommand{{50, 5}, 0.5, false});
    engine.submit(MoveSelectionCommand{{0, 10}, 7});
    doc = dump(engine, "musacad_assoc_8.musa");
    const io::DocDim& r = doc.dims[0].type == static_cast<std::uint8_t>(DimType::Radius) ? doc.dims[0] : doc.dims[1];
    REQUIRE(r.unassoc);
    REQUIRE(r.a == Vec2{50, 0});
    engine.stop();
}

TEST_CASE("DIMASSOC asks for 0 to 2; DIMDISASSOCIATE and DIMREASSOCIATE take the dimensions selected") {
    H h;
    h.run({"DIMASSOC"});
    REQUIRE(h.out.prompt == "Enter new value for DIMASSOC <2>: ");
    h.run({"3"});
    REQUIRE(h.out.lines.back() == "Requires an integer between 0 and 2.");
    h.run({"1"});
    REQUIRE(h.last<DimAssocCommand>()->value == 1);
    h.run({"DIMASSOC", "2"});
    REQUIRE(h.last<DimAssocCommand>()->value == 2);
    h.run({"DDA"});
    REQUIRE(h.out.prompt == "Select dimensions to disassociate: ");
    h.proc.cancel();
    h.proc.set_selection_count(1);
    h.run({"DIMDISASSOCIATE"});
    REQUIRE(h.last<DimAssocCommand>()->op == DimAssocCommand::Op::Disassociate);
    h.run({"DRE"});
    REQUIRE(h.last<DimAssocCommand>()->op == DimAssocCommand::Op::Reassociate);
}

TEST_CASE("The dimension grip menu: Flip Arrow, Above Dim Line, Center Vertically, Reset Text Position, Move Text Only") {
    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {20, 0};
    d.line_pt = {10, 10};
    REQUIRE(compute_dim_geometry(d, DimStyle{}, Rgb{}).dim_lines[0].x == Approx(0.0));
    d.flip = 1; // the first head outside: the line goes on past its extension line
    const DimGeometry flipped = compute_dim_geometry(d, DimStyle{}, Rgb{});
    REQUIRE(flipped.dim_lines[0].x < -1.0);
    REQUIRE(flipped.dim_lines[1].x == Approx(20.0));

    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear), {0, 0}, {20, 0}, {10, 10}, 0, 1});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    const auto dim = [&] { return engine.snapshot().selection.at(0); };
    engine.submit(DimGripMenuCommand{DimGripMenuCommand::Op::FlipArrow, dim(), {20, 10}, 2});
    io::Document doc = dump(engine, "musacad_dimgrip_1.musa");
    REQUIRE(doc.dims[0].flip == 2); // the head nearer the grip
    engine.submit(DimGripMenuCommand{DimGripMenuCommand::Op::CenterVertically, dim(), {10, 10}, 3});
    doc = dump(engine, "musacad_dimgrip_2.musa");
    REQUIRE(doc.dims[0].overrides.has(DimOverrides::kTextAbove));
    REQUIRE_FALSE(doc.dims[0].overrides.text_above);
    // Move Text Only: the text grip dragged off the line; the dimension keeps DIMTMOVE 2.
    GripDragCommand begin{GripDragCommand::Phase::Begin, dim(), DimData::kTextGripIndex, {}, 0};
    begin.text_move = 2;
    engine.submit(begin);
    engine.submit(GripDragCommand{GripDragCommand::Phase::Commit, {}, 0, {14, 25}, 4});
    doc = dump(engine, "musacad_dimgrip_3.musa");
    REQUIRE(doc.dims[0].overrides.has(DimOverrides::kTextMove));
    REQUIRE(doc.dims[0].overrides.text_move == 2);
    REQUIRE(doc.dims[0].line_pt == Vec2{10, 10}); // the line stayed
    REQUIRE(doc.dims[0].text_offset != Vec2{});
    REQUIRE(doc.dims[0].flip == 2);
    // The file keeps them.
    io::Document again;
    REQUIRE(io::parse_native(io::serialize_native(doc), again).ok);
    REQUIRE(again.dims[0].flip == 2);
    REQUIRE(again.dims[0].overrides.text_move == 2);
    REQUIRE(again.dims[0] == doc.dims[0]);
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(DimGripMenuCommand{DimGripMenuCommand::Op::ResetText, dim(), {14, 25}, 5});
    doc = dump(engine, "musacad_dimgrip_4.musa");
    REQUIRE(doc.dims[0].text_offset == Vec2{});
    engine.stop();
}

TEST_CASE("A grip the selected objects share drags them all; grips made hot with Shift move together") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{10, 0}, {10, 10}, 2});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grips.size() >= 6; }));
    const auto grip_at = [&](Vec2 p, Vec2 other_end) {
        // The grip at `p` of the line whose other end is `other_end`.
        GripInfo found{};
        std::vector<GripInfo> all = engine.snapshot().grips;
        for (const GripInfo& g : all) {
            if (g.pos == p && g.handle.kind == EntityKind::Line) {
                for (const GripInfo& o : all) {
                    if (o.handle == g.handle && o.pos == other_end) {
                        found = g;
                    }
                }
            }
        }
        return found;
    };
    // The corner both lines have: one drag moves both ends.
    const GripInfo corner = grip_at({10, 0}, {0, 0});
    engine.submit(GripDragCommand{GripDragCommand::Phase::Begin, corner.handle, corner.index, {}, 0});
    engine.submit(GripDragCommand{GripDragCommand::Phase::Commit, {}, 0, {12, 2}, 3});
    io::Document doc = dump(engine, "musacad_grips_shared.musa");
    REQUIRE(doc.lines.size() == 2);
    for (const io::DocLine& l : doc.lines) {
        REQUIRE((l.a == Vec2{12, 2} || l.b == Vec2{12, 2}));
    }
    // Shift's hot grips: the first line's start and the second line's end go with the drag.
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        return std::any_of(s.grips.begin(), s.grips.end(), [](const GripInfo& g) { return g.pos == Vec2{12, 2}; });
    }));
    const GripInfo start = grip_at({0, 0}, {12, 2});
    const GripInfo top = grip_at({10, 10}, {12, 2});
    GripDragCommand begin{GripDragCommand::Phase::Begin, start.handle, start.index, {}, 0};
    begin.also = {{top.handle, top.index}};
    engine.submit(begin);
    engine.submit(GripDragCommand{GripDragCommand::Phase::Commit, {}, 0, {1, 1}, 4});
    doc = dump(engine, "musacad_grips_hot.musa");
    const bool first_is_bottom = doc.lines[0].a == Vec2{1, 1} || doc.lines[0].b == Vec2{1, 1};
    const io::DocLine& bottom = first_is_bottom ? doc.lines[0] : doc.lines[1];
    const io::DocLine& side = first_is_bottom ? doc.lines[1] : doc.lines[0];
    REQUIRE((bottom.a == Vec2{1, 1} && bottom.b == Vec2{12, 2}));
    REQUIRE((side.a == Vec2{12, 2} && side.b == Vec2{11, 11}));
    // Both ends of one line hot: it moves whole.
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        return std::any_of(s.grips.begin(), s.grips.end(), [](const GripInfo& g) { return g.pos == Vec2{11, 11}; });
    }));
    const GripInfo s0 = grip_at({1, 1}, {12, 2});
    const GripInfo s1 = grip_at({12, 2}, {1, 1});
    engine.submit(ClearSelectionCommand{});
    engine.submit(SelectPickCommand{{6, 1.5}, 0.5, false}); // the bottom line alone
    GripDragCommand both{GripDragCommand::Phase::Begin, s0.handle, s0.index, {}, 0};
    both.also = {{s1.handle, s1.index}};
    engine.submit(both);
    engine.submit(GripDragCommand{GripDragCommand::Phase::Commit, {}, 0, {1, -9}, 5});
    doc = dump(engine, "musacad_grips_whole.musa");
    const bool moved_first = doc.lines[0].a == Vec2{1, -9};
    const io::DocLine& moved = moved_first ? doc.lines[0] : doc.lines[1];
    REQUIRE(moved.a == Vec2{1, -9});
    REQUIRE(moved.b == Vec2{12, -8});
    engine.stop();
}

TEST_CASE("The grip menus' Lengthen keeps a line's direction and an arc's circle; Radius keeps an arc's angles") {
    GeometryStore st;
    const EntityHandle ln = st.add_line({0, 0}, {10, 0});
    const Command longer = edit_for_grip_drag(st, ln, 1, {15, 5}, -1, 1);
    REQUIRE(std::get<AddLineCommand>(longer).b == Vec2{15, 0});
    REQUIRE(std::get<AddLineCommand>(longer).a == Vec2{0, 0});
    const Command stretched = edit_for_grip_drag(st, ln, 1, {15, 5});
    REQUIRE(std::get<AddLineCommand>(stretched).b == Vec2{15, 5});
    const EntityHandle ah = st.add_arc({0, 0}, 5, 0.0, kHalfPi);
    const Command around = edit_for_grip_drag(st, ah, 2, {-3, 3}, -1, 1);
    REQUIRE(std::get<AddArcCommand>(around).end_angle == Approx(0.75 * kPi));
    REQUIRE(std::get<AddArcCommand>(around).radius == Approx(5.0));
    const Command wider = edit_for_grip_drag(st, ah, 3, {6, 6}, -1, 2);
    REQUIRE(std::get<AddArcCommand>(wider).radius == Approx(std::sqrt(72.0)));
    REQUIRE(std::get<AddArcCommand>(wider).start_angle == Approx(0.0));
    REQUIRE(std::get<AddArcCommand>(wider).end_angle == Approx(kHalfPi));
}

TEST_CASE("Ctrl at a hot grip's pick copies, again for each pick; the grip modes then say (multiple)") {
    using P = GripDragCommand::Phase;
    H h;
    h.proc.set_selection_count(2);
    GripDragCommand begin{P::Begin, EntityHandle{0, 1, EntityKind::Line}, 1, {}, 0};
    begin.also = {{EntityHandle{1, 1, EntityKind::Line}, 0}};
    h.proc.start_grip_mode(begin, {10, 0});
    REQUIRE(h.out.lines.back() == "** STRETCH **");
    h.proc.set_ctrl_held(true);
    h.proc.pick_point({20, 5}, std::nullopt);
    h.proc.set_ctrl_held(false);
    const GripDragCommand* commit = nullptr;
    for (const Command& c : h.cmds) {
        if (const auto* g = std::get_if<GripDragCommand>(&c); g != nullptr && g->phase == P::Commit) {
            commit = g;
        }
    }
    REQUIRE(commit != nullptr);
    REQUIRE(commit->copy);
    REQUIRE(commit->pos == Vec2{20, 5});
    REQUIRE(h.last<GripDragCommand>()->phase == P::Begin); // the next copy's drag ...
    REQUIRE(h.last<GripDragCommand>()->also.size() == 1);  // ... with the other hot grip
    REQUIRE(h.out.lines.back() == "** STRETCH (multiple) **");
    REQUIRE(h.proc.has_active_command());
    h.proc.cancel();
}

TEST_CASE("An associative hatch follows its boundary moved or reshaped; HPASSOC 0 makes them not") {
    GeometryEngine engine;
    engine.start();
    AddPolylineCommand rect;
    rect.points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    rect.closed = true;
    rect.group = 1;
    engine.submit(rect);
    AddHatchCommand fill;
    fill.loops = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    fill.group = 2;
    engine.submit(fill);
    const auto boundary = [&] {
        engine.submit(SelectAllCommand{});
        REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
        EntityHandle p{};
        for (const EntityHandle h : engine.snapshot().selection) {
            p = h.kind == EntityKind::Polyline ? h : p;
        }
        engine.submit(SelectHandleCommand{p});
        return p;
    };
    boundary();
    engine.submit(MoveSelectionCommand{{5, 5}, 3});
    io::Document doc = dump(engine, "musacad_hatch_assoc_1.musa");
    REQUIRE(doc.hatches[0].loops[0][0] == Vec2{5, 5});
    REQUIRE(doc.hatches[0].loops[0][2] == Vec2{15, 15});
    REQUIRE(doc.hatches[0].pattern_origin == Vec2{5, 5}); // the pattern went with it
    // A grip edit of a corner: the hatch's corner goes there.
    const EntityHandle p = boundary();
    engine.submit(GripDragCommand{GripDragCommand::Phase::Begin, p, 2, {}, 0});
    engine.submit(GripDragCommand{GripDragCommand::Phase::Commit, {}, 0, {25, 30}, 4});
    doc = dump(engine, "musacad_hatch_assoc_2.musa");
    REQUIRE(doc.hatches[0].loops[0][2] == Vec2{25, 30});
    REQUIRE(doc.hatches[0].loops[0][1] == Vec2{15, 5});
    // One undo takes the boundary and the hatch back together.
    engine.submit(UndoLastGroupCommand{});
    doc = dump(engine, "musacad_hatch_assoc_3.musa");
    REQUIRE(doc.hatches[0].loops[0][2] == Vec2{15, 15});
    engine.stop();
}

TEST_CASE("HPASSOC asks 0 or 1; a hatch made with it 0 does not follow, and the file keeps that") {
    H h;
    h.run({"HPASSOC"});
    REQUIRE(h.out.prompt == "Enter new value for HPASSOC <1>: ");
    h.run({"0"});
    REQUIRE(h.last<DimAssocCommand>()->op == DimAssocCommand::Op::HatchVariable);
    REQUIRE(h.last<DimAssocCommand>()->value == 0);
    h.run({"HPASSOC", "1"});
    REQUIRE(h.last<DimAssocCommand>()->value == 1);

    GeometryEngine engine;
    engine.start();
    engine.submit(DimAssocCommand{DimAssocCommand::Op::HatchVariable, 0, 0});
    engine.submit(AddCircleCommand{{0, 0}, 5, 1});
    AddHatchCommand fill;
    fill.loops = {{{5, 0}, {0, 5}, {-5, 0}, {0, -5}}};
    fill.group = 2;
    engine.submit(fill);
    engine.submit(SelectPickCommand{{3.5355339, 3.5355339}, 0.01, false});
    engine.submit(MoveSelectionCommand{{0, 20}, 3});
    const io::Document doc = dump(engine, "musacad_hatch_noassoc.musa");
    REQUIRE(doc.circles[0].center == Vec2{0, 20});
    REQUIRE(doc.hatches[0].loops[0][0] == Vec2{5, 0});
    REQUIRE(doc.hatches[0].unassoc);
    io::Document again;
    REQUIRE(io::parse_native(io::serialize_native(doc), again).ok);
    REQUIRE(again.hatches[0].unassoc);
    engine.stop();
}

TEST_CASE("A view keeps its View category in the file, with or without a layer snapshot") {
    io::Document doc;
    doc.views.push_back(NamedView{"Plan", {1, 2}, 3.0, "", "Sheets A"});
    doc.views.push_back(NamedView{"Side", {4, 5}, 6.0, "*VIEW|Side", "Details"});
    doc.views.push_back(NamedView{"Top", {7, 8}, 9.0});
    io::Document again;
    REQUIRE(io::parse_native(io::serialize_native(doc), again).ok);
    REQUIRE(again.views == doc.views);
}

TEST_CASE("TRIM takes an associative dimension's point at the trimmed end to the new end") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{6, -5}, {6, 5}, 2});
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Aligned), {0, 0}, {10, 0}, {5, -8}, 0, 3});
    engine.submit(TrimPickCommand{{8, 0}, 0.5, 4});
    io::Document doc = dump(engine, "musacad_trim_assoc.musa");
    REQUIRE(doc.dims.size() == 1);
    REQUIRE(doc.dims[0].a == Vec2{0, 0});
    REQUIRE(doc.dims[0].b.x == Approx(6.0));
    REQUIRE(doc.dims[0].b.y == Approx(0.0).margin(1e-9));
    // One undo: the line and its dimension as they were.
    engine.submit(UndoLastGroupCommand{});
    doc = dump(engine, "musacad_trim_assoc_2.musa");
    REQUIRE(doc.dims[0].b == Vec2{10, 0});
    engine.stop();
}

TEST_CASE("ARRAYRECT draws 4 columns and 3 rows at once and changes them option by option") {
    H h;
    h.proc.set_selection_count(1);
    h.proc.set_selection_size({10, 4});
    h.run({"ARRAYRECT"});
    REQUIRE(std::find(h.out.lines.begin(), h.out.lines.end(), "Type = Rectangular  Associative = No") != h.out.lines.end());
    const auto* pv = h.last<ArrayPreviewCommand>();
    REQUIRE(pv != nullptr);
    REQUIRE(pv->type == 1);
    REQUIRE(pv->rect.cols == 4);
    REQUIRE(pv->rect.rows == 3);
    REQUIRE(pv->rect.dx == Approx(15.0));
    REQUIRE(pv->rect.dy == Approx(6.0));
    REQUIRE(h.out.prompt ==
            "Select grip to edit array or [ASsociative/Base point/COUnt/Spacing/COLumns/Rows/Levels/eXit]<eXit>: ");
    h.run({"COU"});
    REQUIRE(h.out.prompt == "Enter the number of columns or [Expression] <4>: ");
    h.run({"5", "2"});
    REQUIRE(h.last<ArrayPreviewCommand>()->rect.cols == 5);
    REQUIRE(h.last<ArrayPreviewCommand>()->rect.rows == 2);
    h.run({"S"});
    REQUIRE(h.out.prompt.rfind("Specify the distance between columns or [Unit cell] <", 0) == 0);
    h.run({"20", "8"});
    REQUIRE(h.last<ArrayPreviewCommand>()->rect.dx == Approx(20.0));
    REQUIRE(h.last<ArrayPreviewCommand>()->rect.dy == Approx(8.0));
    h.run({"COL", "3", "T", "50"}); // three columns 50 from first to last
    REQUIRE(h.last<ArrayPreviewCommand>()->rect.dx == Approx(25.0));
    h.run({"COU", "E", "2*3", ""}); // an expression for the columns
    REQUIRE(h.last<ArrayPreviewCommand>()->rect.cols == 6);
    REQUIRE(h.proc.has_active_command());
    h.run({""});
    const auto* ar = h.last<ArrayRectCommand>();
    REQUIRE(ar != nullptr);
    REQUIRE(ar->cols == 6);
    REQUIRE(ar->rows == 2);
    REQUIRE(ar->dx == Approx(25.0));
    REQUIRE(ar->dy == Approx(8.0));
    REQUIRE_FALSE(ar->auto_spacing);
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("ARRAYPOLAR asks the centre, then Items, Angle between, Fill angle, ROWs and ROTate items") {
    H h;
    h.proc.set_selection_count(1);
    h.proc.set_selection_size({2, 2});
    h.run({"ARRAYPOLAR"});
    REQUIRE(h.out.prompt == "Specify center point of array or [Base point/Axis of rotation]: ");
    h.run({"0,0"});
    REQUIRE(h.last<ArrayPreviewCommand>()->type == 2);
    REQUIRE(h.last<ArrayPreviewCommand>()->polar.count == 6);
    REQUIRE(h.last<ArrayPreviewCommand>()->polar.total_angle == Approx(kTwoPi));
    h.run({"I", "8", "F", "180"});
    REQUIRE(h.last<ArrayPreviewCommand>()->polar.count == 8);
    REQUIRE(h.last<ArrayPreviewCommand>()->polar.total_angle == Approx(kPi));
    h.run({"A", "30"}); // 30 degrees apart: 210 for the eight
    REQUIRE(h.last<ArrayPreviewCommand>()->polar.total_angle == Approx(to_radians(210.0)));
    h.run({"ROT", "N", "ROW", "2", "5", ""});
    REQUIRE_FALSE(h.last<ArrayPreviewCommand>()->polar.rotate_items);
    REQUIRE(h.last<ArrayPreviewCommand>()->polar.rows == 2);
    REQUIRE(h.last<ArrayPreviewCommand>()->polar.row_spacing == Approx(5.0));
    h.run({"X"});
    const auto* ap = h.last<ArrayPolarCommand>();
    REQUIRE(ap != nullptr);
    REQUIRE(ap->count == 8);
    REQUIRE(ap->rows == 2);
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("ARRAYPATH fills the path at one and a half times the item's size; Method Divide asks how many") {
    H h;
    h.proc.set_selection_count(1);
    h.proc.set_selection_size({4, 2});
    h.run({"ARRAYPATH"});
    REQUIRE(h.out.prompt == "Select path curve: ");
    h.run({"50,0"});
    REQUIRE(h.last<ArrayPreviewCommand>()->type == 3);
    REQUIRE(h.last<ArrayPreviewCommand>()->path.spacing == Approx(6.0));
    REQUIRE(h.last<ArrayPreviewCommand>()->path.count == 0); // as many as fit
    h.run({"M", "D", "I", "4"});
    REQUIRE(h.last<ArrayPreviewCommand>()->path.count == 4);
    REQUIRE(h.last<ArrayPreviewCommand>()->path.spacing == Approx(0.0));
    h.run({"A", "N", ""});
    const auto* ap = h.last<ArrayPathCommand>();
    REQUIRE(ap != nullptr);
    REQUIRE(ap->count == 4);
    REQUIRE_FALSE(ap->align);
}

TEST_CASE("ARRAY asks the type; Esc takes the preview away") {
    H h;
    h.proc.set_selection_count(1);
    h.run({"AR"});
    REQUIRE(h.out.prompt == "Enter array type [Rectangular/PAth/POlar] <Rectangular>: ");
    h.run({""});
    REQUIRE(h.last<ArrayPreviewCommand>()->type == 1);
    REQUIRE(h.last<ArrayPreviewCommand>()->rect.auto_spacing); // the selection's size not known here
    h.proc.cancel();
    REQUIRE(h.last<ArrayPreviewCommand>()->type == 0);
    REQUIRE(h.last<ArrayRectCommand>() == nullptr);
}

TEST_CASE("The array preview draws the copies the array would make, and the array clears it") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 1, 1});
    engine.submit(SelectAllCommand{});
    ArrayPreviewCommand pv;
    pv.type = 1;
    pv.rect.rows = 3;
    pv.rect.cols = 4;
    pv.rect.auto_spacing = true;
    engine.submit(pv);
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.grip_preview_segments.empty(); }));
    // The far corner's copy: 3 x 1.5 x 2 across, 2 x 1.5 x 2 up.
    const auto& segs = engine.snapshot().grip_preview_segments;
    REQUIRE(std::any_of(segs.begin(), segs.end(), [](const Vec2& v) { return distance(v, Vec2{9, 6}) <= 1.01; }));
    ArrayRectCommand make;
    make.rows = 3;
    make.cols = 4;
    make.auto_spacing = true;
    make.group = 2;
    engine.submit(make);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grip_preview_segments.empty(); }));
    const io::Document doc = dump(engine, "musacad_array_preview.musa");
    REQUIRE(doc.circles.size() == 12);
    engine.stop();
}

TEST_CASE("SPLINE's start and end Tangency: the fitted curve leaves and arrives in those directions") {
    // The fit with end directions: through the points, its first and last legs along them.
    const std::vector<Vec2> pts{{0, 0}, {10, 5}, {20, 0}};
    const std::vector<Vec2> ctrl = spline::fit_or_fallback(pts, 3, spline::FitParam::Chord, Vec2{0, 1}, Vec2{0, -1});
    REQUIRE(ctrl.size() == 5);
    REQUIRE(ctrl.front() == Vec2{0, 0});
    REQUIRE(ctrl[1].x == Approx(0.0).margin(1e-9)); // straight up from the start
    REQUIRE(ctrl[1].y > 0.0);
    REQUIRE(ctrl[3].x == Approx(20.0).margin(1e-9)); // and straight down into the end
    REQUIRE(ctrl.back().x == Approx(20.0));
    std::vector<Vec2> curve;
    spline::tessellate(ctrl, 3, curve);
    REQUIRE(std::any_of(curve.begin(), curve.end(), [](const Vec2& v) { return distance(v, Vec2{10, 5}) < 0.05; }));

    H h;
    h.run({"SPLINE", "M", "F", "0,0", "T"}); // Fit, whatever an earlier test left
    REQUIRE(h.out.prompt == "Specify start tangent: ");
    h.run({"0,10", "10,5", "20,0", "T"});
    REQUIRE(h.out.prompt == "Specify end tangent: ");
    h.run({"20,-10"});
    const auto* sp = h.last<AddSplineCommand>();
    REQUIRE(sp != nullptr);
    REQUIRE(sp->control_points.size() == 5);
    REQUIRE(sp->control_points[1].x == Approx(0.0).margin(1e-9));
    REQUIRE_FALSE(h.proc.has_active_command()); // the end tangent ends it
}

TEST_CASE("The Text tab's Vertical position: Below, and Outside on the side away from what is measured") {
    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {20, 0};
    d.line_pt = {10, 10}; // the line above the points
    DimStyle st;
    REQUIRE(dim_text_vpos(st) == 1);
    const double above = compute_dim_geometry(d, st, Rgb{}).text_pos.y;
    REQUIRE(above > 10.0);
    REQUIRE(apply_dim_var(st, "DIMTAD", 4));
    REQUIRE(dim_text_vpos(st) == 4);
    REQUIRE(compute_dim_geometry(d, st, Rgb{}).text_pos.y < 10.0 - st.text_height);
    REQUIRE(apply_dim_var(st, "DIMTAD", 2)); // Outside: away from the points, so above here
    REQUIRE(compute_dim_geometry(d, st, Rgb{}).text_pos.y == Approx(above));
    d.line_pt = {10, -10}; // the line below the points: outside is below it
    REQUIRE(compute_dim_geometry(d, st, Rgb{}).text_pos.y < -10.0 - st.text_height);
    REQUIRE(apply_dim_var(st, "DIMTAD", 0));
    REQUIRE(dim_text_vpos(st) == 0);
    // The file and DXF keep all five.
    io::Document doc;
    DimStyle below;
    below.name = "B";
    REQUIRE(apply_dim_var(below, "DIMTAD", 4));
    doc.dimstyles.push_back(below);
    io::Document again;
    REQUIRE(io::parse_native(io::serialize_native(doc), again).ok);
    REQUIRE(dim_text_vpos(again.dimstyles.back()) == 4);
}

TEST_CASE("A layer state goes out to an AutoCAD .las file and comes back") {
    LayerState st;
    st.name = "Plot set";
    st.current = "WALLS";
    Layer walls;
    walls.name = "WALLS";
    walls.color = Rgb{255, 0, 0};
    walls.linetype = Linetype::Dashed;
    walls.lineweight = 35;
    walls.locked = true;
    Layer notes;
    notes.name = "NOTES";
    notes.color = Rgb{12, 34, 56}; // not a colour index: a true colour too
    notes.on = false;
    notes.plot = false;
    notes.transparency = 40;
    st.layers = {walls, notes};
    const std::string las = io::serialize_las(st);
    REQUIRE(las.rfind("  0\r\nLAYERSTATEDICTIONARY\r\n  0\r\nLAYERSTATE\r\n  1\r\nPlot set\r\n 91\r\n2047\r\n", 0) == 0);
    REQUIRE(las.find("  8\r\nWALLS\r\n 90\r\n12\r\n 62\r\n1\r\n370\r\n35\r\n  6\r\nDASHED\r\n") != std::string::npos);
    LayerState back;
    std::string error;
    REQUIRE(io::parse_las(las, back, error));
    REQUIRE(back.name == "Plot set");
    REQUIRE(back.current == "WALLS");
    REQUIRE(back.layers.size() == 2);
    REQUIRE(back.layers[0].color == Rgb{255, 0, 0});
    REQUIRE(back.layers[0].locked);
    REQUIRE(back.layers[0].linetype == Linetype::Dashed);
    REQUIRE(back.layers[1].color == Rgb{12, 34, 56});
    REQUIRE_FALSE(back.layers[1].on);
    REQUIRE_FALSE(back.layers[1].plot);
    REQUIRE(back.layers[1].transparency == 40);
    // As AutoCAD writes one (LF endings here, a byte order mark, the "Default" lineweight).
    const std::string acad = "\xEF\xBB\xBF  0\nLAYERSTATEDICTIONARY\n  0\nLAYERSTATE\n  1\nAULA\n 91\n2047\n301\n\n290\n0\n302\n"
                             "CENTRO\n  8\nCENTRO\n 90\n24\n 62\n7\n370\n-3\n  6\nCENTER\n  2\nCor_7\n440\n0\n";
    REQUIRE(io::parse_las(acad, back, error));
    REQUIRE(back.name == "AULA");
    REQUIRE(back.layers.size() == 1);
    REQUIRE(back.layers[0].vp_freeze_new);
    REQUIRE(back.layers[0].plot);
    REQUIRE(back.layers[0].lineweight == 25);
    REQUIRE_FALSE(io::parse_las("  0\nSECTION\n", back, error));
}

TEST_CASE("-LAYERSTATE's Import and EXport name the file") {
    H h;
    h.run({"-LAYERSTATE", "EX", "Plot set", ""});
    const auto* c = h.last<LayerStateCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->op == LayerStateCommand::Op::Export);
    REQUIRE(c->name == "Plot set");
    REQUIRE(c->to == "Plot set.las");
    h.run({"I", "/tmp/x.las"});
    REQUIRE(h.last<LayerStateCommand>()->op == LayerStateCommand::Op::Import);
    REQUIRE(h.last<LayerStateCommand>()->to == "/tmp/x.las");
}

TEST_CASE("HATCH finds a pattern it does not have in a .pat file on the support path and brings it in") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "musacad_pat_support";
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "mine.PAT", std::ios::binary);
        f << "*MYDIAG, my diagonal lines\r\n45, 0,0, 0,3.175\r\n*MYGRID, a grid\r\n0, 0,0, 0,5\r\n90, 0,0, 0,5\r\n";
    }
    H h;
    h.proc.set_support_paths({dir.string()});
    h.run({"-HATCH", "P", "?"});
    REQUIRE(h.out.lines.back().find(" MYDIAG") != std::string::npos);
    REQUIRE(h.out.lines.back().find(" MYGRID") != std::string::npos);
    h.run({"mygrid"});
    const auto* ap = h.last<AddPatternCommand>();
    REQUIRE(ap != nullptr);
    REQUIRE(ap->pattern.name == "MYGRID");
    REQUIRE(ap->pattern.lines.size() == 2);
    REQUIRE(h.out.prompt.rfind("Specify a scale for the pattern <", 0) == 0);
    h.proc.cancel();
    h.run({"-HATCH", "P", "NOSUCH"});
    REQUIRE(h.out.lines.back() == "Unknown pattern \"NOSUCH\"; ? lists them.");
    h.proc.cancel();
    fs::remove_all(dir);

    // The engine keeps it with the drawing.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPatternCommand{ap->pattern});
    AddHatchCommand fill;
    fill.loops = {{{0, 0}, {20, 0}, {20, 20}, {0, 20}}};
    fill.pattern_name = "MYGRID";
    fill.group = 1;
    engine.submit(fill);
    const io::Document doc = dump(engine, "musacad_pat_support.musa");
    REQUIRE(std::any_of(doc.patterns.begin(), doc.patterns.end(), [](const auto& p) { return p.name == "MYGRID"; }));
    engine.stop();
}

TEST_CASE("Hatching a selection takes circles, whole ellipses, closed splines and polylines with arcs") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 5, 1});
    AddPolylineCommand slot; // a slot: two straight sides, two half-circle ends
    slot.points = {{20, 0}, {30, 0}, {30, 10}, {20, 10}};
    slot.bulges = {0.0, 1.0, 0.0, 1.0};
    slot.closed = true;
    slot.group = 2;
    engine.submit(slot);
    engine.submit(SelectAllCommand{});
    engine.submit(HatchFromSelectionCommand{"SOLID", 1.0, 0.0, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Solid hatch created."; }));
    const io::Document doc = dump(engine, "musacad_hatch_curves.musa");
    REQUIRE(doc.hatches.size() == 1);
    REQUIRE(doc.hatches[0].loops.size() == 2);
    for (const auto& loop : doc.hatches[0].loops) {
        REQUIRE(loop.size() > 8); // the curves followed, not their chords
    }
    // The circle's loop is on the circle.
    const auto& circle = doc.hatches[0].loops[0].front().x < 10.0 ? doc.hatches[0].loops[0] : doc.hatches[0].loops[1];
    for (const Vec2& v : circle) {
        REQUIRE(length(v) == Approx(5.0).margin(1e-6));
    }
    engine.stop();
}

TEST_CASE("HATCHEDIT separates a hatch's areas, disassociates it, and sets its colour and layer") {
    GeometryEngine engine;
    engine.start();
    AddHatchCommand fill;
    fill.loops = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}},      // an area ...
                  {{3, 3}, {7, 3}, {7, 7}, {3, 7}},          // ... with an island
                  {{20, 0}, {30, 0}, {30, 10}, {20, 10}}};   // and another area
    fill.group = 1;
    engine.submit(fill);
    engine.submit(SelectAllCommand{});
    engine.submit(HatchEditCommand{HatchEditCommand::Op::Separate, {}, true, {}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 hatch separated."; }));
    io::Document doc = dump(engine, "musacad_hatchedit_1.musa");
    REQUIRE(doc.hatches.size() == 2);
    const auto& holed = doc.hatches[0].loops.size() == 2 ? doc.hatches[0] : doc.hatches[1];
    REQUIRE(holed.loops.size() == 2); // the island stays with its area
    engine.submit(SelectAllCommand{});
    engine.submit(HatchEditCommand{HatchEditCommand::Op::Disassociate, {}, true, {}, 3});
    engine.submit(HatchEditCommand{HatchEditCommand::Op::Color, Rgb{255, 0, 0}, false, {}, 4});
    doc = dump(engine, "musacad_hatchedit_2.musa");
    for (const auto& h : doc.hatches) {
        REQUIRE(h.unassoc);
        REQUIRE_FALSE(h.props.color_by_layer());
        REQUIRE(h.props.color == Rgb{255, 0, 0});
    }
    engine.submit(HatchEditCommand{HatchEditCommand::Op::Layer, {}, true, "NOSUCH", 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Cannot find layer \"NOSUCH\"."; }));
    engine.stop();
}

TEST_CASE("HATCHEDIT's Disassociate, separate Hatches, hatch COlor and LAyer") {
    H h;
    h.run({"HATCHEDIT", "5,5", "CO", "1"});
    const auto* c = h.last<HatchEditCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->op == HatchEditCommand::Op::Color);
    REQUIRE_FALSE(c->by_layer);
    h.run({"HATCHEDIT", "5,5", "LA", "Hatch fill"});
    REQUIRE(h.last<HatchEditCommand>()->layer == "Hatch fill");
    h.run({"HATCHEDIT", "5,5", "H"});
    REQUIRE(h.last<HatchEditCommand>()->op == HatchEditCommand::Op::Separate);
    h.run({"HATCHEDIT", "5,5", "D"});
    REQUIRE(h.last<HatchEditCommand>()->op == HatchEditCommand::Op::Disassociate);
}

TEST_CASE("TEXTFIT fits Aligned and Fit text by moving its two points") {
    GeometryEngine engine;
    engine.start();
    AddTextCommand t;
    t.pos = {0, 0};
    t.align = {10, 0};
    t.justify = 5; // Fit
    t.content = "FIT";
    t.group = 1;
    engine.submit(t);
    engine.submit(TextFitCommand{{5, 1}, 2.0, {25, 0}, Vec2{5, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Text fitted between its two points."; }));
    const io::Document doc = dump(engine, "musacad_textfit_fit.musa");
    REQUIRE(doc.texts.size() == 1);
    REQUIRE(doc.texts[0].pos == Vec2{5, 0});
    REQUIRE(doc.texts[0].align == Vec2{25, 0});
    engine.stop();
}

TEST_CASE("DIMASSOC 0 draws dimensions exploded, in the same undo step") {
    GeometryEngine engine;
    engine.start();
    engine.submit(DimAssocCommand{DimAssocCommand::Op::Variable, 0, 0});
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear), {0, 0}, {20, 0}, {10, 10}, 0, 1});
    io::Document doc = dump(engine, "musacad_dimassoc0.musa");
    REQUIRE(doc.dims.empty());
    REQUIRE(doc.lines.size() >= 3); // the dimension line and the two extension lines
    REQUIRE_FALSE(doc.texts.empty());
    engine.submit(UndoLastGroupCommand{});
    doc = dump(engine, "musacad_dimassoc0_undo.musa");
    REQUIRE(doc.dims.empty());
    REQUIRE(doc.lines.empty());
    engine.stop();
}

TEST_CASE("SPLINE's toLerance fits fewer control points within the tolerance, ends on the end points") {
    std::vector<Vec2> pts;
    for (int i = 0; i <= 20; ++i) {
        const double x = i;
        pts.push_back({x, 0.02 * std::sin(x * 7.0)}); // a wobbly straight run
    }
    const std::vector<Vec2> near = spline::fit_within(pts, 3, spline::FitParam::Chord, 0.1);
    REQUIRE_FALSE(near.empty());
    REQUIRE(near.size() < pts.size());
    REQUIRE(near.front() == pts.front());
    REQUIRE(near.back() == pts.back());
    REQUIRE(spline::fit_within(pts, 3, spline::FitParam::Chord, 0.0).empty()); // 0: through them

    H h;
    h.run({"SPLINE", "M", "F", "0,0", "L"});
    REQUIRE(h.out.prompt == "Specify fit tolerance <0.0000>: ");
    h.run({"0.5", "5,0.1", "10,-0.1", "15,0.1", "20,0", "25,-0.1", ""});
    const auto* sp = h.last<AddSplineCommand>();
    REQUIRE(sp != nullptr);
    REQUIRE(sp->control_points.size() < 6);
}

TEST_CASE("SPLINE's Close makes the fitted spline smooth across its seam") {
    H h;
    h.run({"SPLINE", "M", "F", "0,0", "10,0", "10,10", "0,10", "C"});
    const auto* sp = h.last<AddSplineCommand>();
    REQUIRE(sp != nullptr);
    const std::vector<Vec2>& c = sp->control_points;
    REQUIRE(c.front() == c.back()); // closed
    // The legs either side of the seam lie on one line (the same direction at the start and end).
    const Vec2 out = c[1] - c.front();
    const Vec2 in = c.back() - c[c.size() - 2];
    REQUIRE(std::abs(out.x * in.y - out.y * in.x) < 1e-9);
    REQUIRE(out.x * in.x + out.y * in.y > 0.0);
}

TEST_CASE("The Text tab's Horizontal placement: beside or over an extension line") {
    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {100, 0};
    d.line_pt = {50, 10};
    DimStyle st;
    const DimGeometry centred = compute_dim_geometry(d, st, Rgb{});
    REQUIRE(centred.text_pos.x == Approx(50.0));
    REQUIRE(apply_dim_var(st, "DIMJUST", 1));
    REQUIRE(compute_dim_geometry(d, st, Rgb{}).text_pos.x < 20.0); // by the first extension line
    REQUIRE(apply_dim_var(st, "DIMJUST", 2));
    REQUIRE(compute_dim_geometry(d, st, Rgb{}).text_pos.x > 80.0); // by the second
    REQUIRE(apply_dim_var(st, "DIMJUST", 3));
    const DimGeometry over = compute_dim_geometry(d, st, Rgb{});
    REQUIRE(over.text_pos.y > 10.0);                                // past the dimension line
    REQUIRE(std::abs(std::cos(over.text_rotation)) < 1e-9);         // along the extension line
    REQUIRE(std::abs(over.text_pos.x) < st.text_height * 1.5);      // over the first one
    io::Document doc;
    doc.dimstyles.push_back(st);
    io::Document again;
    REQUIRE(io::parse_native(io::serialize_native(doc), again).ok);
    REQUIRE(again.dimstyles.back().text_just == 3);
}

TEST_CASE("BLOCKREPLACE makes every reference of one block a reference of another, values kept by tag") {
    namespace fs = std::filesystem;
    io::Document doc;
    io::DocBlockDef a;
    a.name = "OLD";
    a.lines.push_back(io::DocLine{{0, 0}, {5, 0}, {}});
    io::DocAttDef tag_a;
    tag_a.text.content = "NUM";
    tag_a.def = "0";
    a.attdefs = {tag_a};
    io::DocBlockDef b;
    b.name = "NEW";
    b.circles.push_back(io::DocCircle{{0, 0}, 2.0, {}});
    io::DocAttDef note;
    note.text.content = "NOTE";
    note.def = "n/a";
    io::DocAttDef tag_b;
    tag_b.text.content = "NUM";
    tag_b.def = "0";
    b.attdefs = {note, tag_b};
    doc.block_defs = {a, b};
    io::DocInsert in;
    in.block_name = "OLD";
    in.pos = {10, 10};
    in.rotation = 0.5;
    in.attribs = {io::DocAttrib{"NUM", "42"}};
    doc.inserts = {in};
    const std::string dp = (fs::temp_directory_path() / "musacad_blockreplace_in.musa").string();
    REQUIRE(io::save_native(doc, dp).ok);
    GeometryEngine engine;
    engine.start();
    engine.submit(OpenDocumentCommand{dp});
    engine.submit(ReplaceBlockCommand{"old", "New", 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 reference of \"OLD\" replaced by \"NEW\"."; }));
    const io::Document out = dump(engine, "musacad_blockreplace_out.musa");
    REQUIRE(out.inserts.size() == 1);
    REQUIRE(out.inserts[0].block_name == "NEW");
    REQUIRE(out.inserts[0].pos == Vec2{10, 10});
    REQUIRE(out.inserts[0].rotation == Approx(0.5));
    const auto num = std::find_if(out.inserts[0].attribs.begin(), out.inserts[0].attribs.end(),
                                  [](const io::DocAttrib& x) { return x.tag == "NUM"; });
    REQUIRE(num != out.inserts[0].attribs.end());
    REQUIRE(num->value == "42"); // the value went with its tag
    engine.stop();
    fs::remove(dp);

    H h;
    h.run({"-BLOCKREPLACE"});
    REQUIRE(h.out.lines.back() == "The drawing needs two blocks for one to replace the other.");
}

TEST_CASE("A block redefined with its attributes in another order keeps each reference's values by tag") {
    namespace fs = std::filesystem;
    io::Document doc;
    io::DocBlockDef blk;
    blk.name = "TAG2";
    io::DocAttDef a;
    a.text.content = "A";
    io::DocAttDef b;
    b.text.content = "B";
    blk.attdefs = {a, b};
    blk.lines.push_back(io::DocLine{{0, 0}, {1, 0}, {}});
    doc.block_defs = {blk};
    io::DocInsert in;
    in.block_name = "TAG2";
    in.attribs = {io::DocAttrib{"A", "one"}, io::DocAttrib{"B", "two"}};
    doc.inserts = {in};
    const std::string dp = (fs::temp_directory_path() / "musacad_redefine_tags.musa").string();
    REQUIRE(io::save_native(doc, dp).ok);
    GeometryEngine engine;
    engine.start();
    engine.submit(OpenDocumentCommand{dp});
    // The new definition: B first, then A, far from the reference.
    AddAttDefCommand nb;
    nb.text.pos = {100, 0};
    nb.text.content = "B";
    nb.group = 2;
    AddAttDefCommand na;
    na.text.pos = {100, 10};
    na.text.content = "A";
    na.group = 3;
    engine.submit(nb);
    engine.submit(na);
    engine.submit(SelectPickCommand{{100.5, 0.5}, 1.0, false});
    engine.submit(SelectPickCommand{{100.5, 10.5}, 1.0, true});
    engine.submit(DefineBlockCommand{"TAG2", {100, 0}, 4});
    const io::Document out = dump(engine, "musacad_redefine_tags_out.musa");
    // The reference that was there (BLOCK put a second one where the attributes were).
    const auto first = std::find_if(out.inserts.begin(), out.inserts.end(),
                                    [](const io::DocInsert& x) { return x.pos == Vec2{0, 0}; });
    REQUIRE(first != out.inserts.end());
    const auto value = [&](const char* tag) {
        for (const io::DocAttrib& x : first->attribs) {
            if (x.tag == tag) {
                return x.value;
            }
        }
        return std::string("?");
    };
    REQUIRE(value("A") == "one");
    REQUIRE(value("B") == "two");
    engine.stop();
    fs::remove(dp);
}

TEST_CASE("Spline leaders, leaders with no line and no arrowhead -- drawn so and kept in the file") {
    const auto drawn = [](std::uint8_t flags) {
        GeometryEngine engine;
        engine.start();
        AddMLeaderCommand ml;
        ml.vertices = {{0, 0}, {10, 10}, {20, 5}, {25, 5}};
        ml.content = "";
        ml.leader_flags = flags;
        ml.group = 1;
        engine.submit(ml);
        REQUIRE(wait_until(engine, [](const auto& s) { return s.document_version > 0 || !s.line_vertices.empty(); }));
        engine.consume_snapshot();
        const std::size_t n = engine.snapshot().line_vertices.size();
        engine.stop();
        return n;
    };
    const std::size_t straight = drawn(0);
    REQUIRE(drawn(LeaderForm::kSpline | LeaderForm::kLanding) > straight); // the curve's many chords
    REQUIRE(drawn(LeaderForm::kNoLine) < straight);                          // no leader at all

    io::Document doc;
    io::DocMLeader m;
    m.vertices = {{0, 0}, {10, 10}};
    m.leader_flags = LeaderForm::kSpline | LeaderForm::kNoArrow;
    doc.mleaders.push_back(m);
    io::Document again;
    REQUIRE(io::parse_native(io::serialize_native(doc), again).ok);
    REQUIRE(again.mleaders[0].leader_flags == (LeaderForm::kSpline | LeaderForm::kNoArrow));
    const std::string dxf = io::serialize_dxf(doc);
    REQUIRE(dxf.find("\n72\n1\n") != std::string::npos); // DXF's spline path type
}

TEST_CASE("LEADER's Format Spline and None; MLEADER's Leader type sPline") {
    H h;
    h.run({"LEADER", "0,0", "10,10", "F", "S", "F", "N", "20,10", "", "NOTE", ""});
    const auto* ml = h.last<AddMLeaderCommand>();
    REQUIRE(ml != nullptr);
    REQUIRE(ml->leader_flags == (LeaderForm::kSpline | LeaderForm::kNoArrow));
}


namespace {
// A workbook as Excel lays one out (shared strings with a rich-text run, an inline string, a
// number, a boolean, a gap at C2), deflated with dynamic Huffman codes.
const unsigned char kWorkbook[] = {
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x04, 0x1f, 0x51, 0x5d, 0x7d, 0xb8,
    0x36, 0xc6, 0x6e, 0x00, 0x00, 0x00, 0x7a, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x5b, 0x43,
    0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x5f, 0x54, 0x79, 0x70, 0x65, 0x73, 0x5d, 0x2e, 0x78, 0x6d,
    0x6c, 0x1d, 0x8c, 0x41, 0x0e, 0xc2, 0x20, 0x10, 0x00, 0xbf, 0x42, 0xf6, 0xde, 0x2e, 0x7a, 0x68,
    0x8c, 0x01, 0x7a, 0xeb, 0x0b, 0xea, 0x03, 0x08, 0xae, 0xb4, 0x51, 0x16, 0x02, 0x1b, 0xa3, 0xbf,
    0x97, 0x7a, 0x9c, 0xcc, 0x64, 0xcc, 0xfc, 0x49, 0x2f, 0xf5, 0xa6, 0xda, 0xf6, 0xcc, 0x16, 0x4e,
    0xa3, 0x06, 0x45, 0x1c, 0xf2, 0x7d, 0xe7, 0x68, 0xe1, 0xb6, 0x2e, 0xc3, 0x05, 0x66, 0x67, 0xd6,
    0x6f, 0xa1, 0xa6, 0x7a, 0xca, 0xcd, 0xc2, 0x26, 0x52, 0xae, 0x88, 0x2d, 0x6c, 0x94, 0x7c, 0x1b,
    0x73, 0x21, 0xee, 0xe6, 0x91, 0x6b, 0xf2, 0xd2, 0xb1, 0x46, 0x2c, 0x3e, 0x3c, 0x7d, 0x24, 0x3c,
    0x6b, 0x3d, 0x61, 0xc8, 0x2c, 0xc4, 0x32, 0xc8, 0xf1, 0x00, 0x67, 0xf0, 0x3f, 0x73, 0x3f, 0x50,
    0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x04, 0x1f, 0x51, 0x5d, 0x2f, 0x65, 0xe1,
    0x1f, 0xbc, 0x00, 0x00, 0x00, 0x1a, 0x01, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x78, 0x6c, 0x2f,
    0x77, 0x6f, 0x72, 0x6b, 0x62, 0x6f, 0x6f, 0x6b, 0x2e, 0x78, 0x6d, 0x6c, 0x8d, 0x8f, 0xcb, 0x6e,
    0xc2, 0x40, 0x0c, 0x45, 0x7f, 0x65, 0xe4, 0x7d, 0x99, 0xc0, 0x02, 0xa1, 0x28, 0x09, 0x9b, 0xaa,
    0x12, 0x3b, 0x16, 0xe5, 0x03, 0xdc, 0x8c, 0x43, 0x46, 0x64, 0xec, 0xc8, 0x1e, 0x0a, 0xfc, 0x3d,
    0x53, 0x1e, 0xfb, 0xae, 0xfc, 0xd2, 0x3d, 0xbe, 0xb7, 0xd9, 0x5e, 0xd3, 0xe4, 0x7e, 0x49, 0x2d,
    0x0a, 0xb7, 0xb0, 0x5c, 0x54, 0xe0, 0x88, 0x7b, 0x09, 0x91, 0x8f, 0x2d, 0x1c, 0xbe, 0xbf, 0x3e,
    0x36, 0xe0, 0x2c, 0x23, 0x07, 0x9c, 0x84, 0xa9, 0x85, 0x1b, 0x19, 0x6c, 0xbb, 0xe6, 0x22, 0x7a,
    0xfa, 0x11, 0x39, 0xb9, 0x22, 0x67, 0x6b, 0x61, 0xcc, 0x79, 0xae, 0xbd, 0xb7, 0x7e, 0xa4, 0x84,
    0xb6, 0x90, 0x99, 0xb8, 0x5c, 0x06, 0xd1, 0x84, 0xb9, 0x8c, 0x7a, 0xf4, 0x36, 0x2b, 0x61, 0xb0,
    0x91, 0x28, 0xa7, 0xc9, 0xaf, 0xaa, 0x6a, 0xed, 0x13, 0x46, 0x86, 0x27, 0xa1, 0xd6, 0xff, 0x30,
    0x64, 0x18, 0x62, 0x4f, 0x9f, 0xd2, 0x9f, 0x13, 0x71, 0x7e, 0x42, 0x94, 0x26, 0xcc, 0xc5, 0xbc,
    0x8d, 0x71, 0x36, 0xe8, 0x9a, 0xc7, 0x07, 0x7b, 0x55, 0xc7, 0x98, 0x8a, 0xe9, 0x3d, 0x6a, 0xb6,
    0x92, 0xe3, 0x6f, 0xb5, 0x0b, 0x25, 0x26, 0x38, 0xad, 0x63, 0x69, 0x74, 0x17, 0x96, 0xe0, 0xbb,
    0xc6, 0xbf, 0x55, 0xfe, 0x1d, 0xac, 0xbb, 0x03, 0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x1f, 0x51, 0x5d, 0x5a, 0xfd, 0x82, 0x6b, 0xb1, 0x00, 0x00, 0x00, 0x28, 0x01,
    0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x78, 0x6c, 0x2f, 0x5f, 0x72, 0x65, 0x6c, 0x73, 0x2f, 0x77,
    0x6f, 0x72, 0x6b, 0x62, 0x6f, 0x6f, 0x6b, 0x2e, 0x78, 0x6d, 0x6c, 0x2e, 0x72, 0x65, 0x6c, 0x73,
    0x8d, 0xcf, 0xc9, 0x0a, 0xc2, 0x40, 0x0c, 0x06, 0xe0, 0x57, 0x19, 0x72, 0xb7, 0x69, 0x3d, 0x88,
    0x48, 0xa7, 0x5e, 0x44, 0xe8, 0x55, 0xea, 0x03, 0x0c, 0xd3, 0x74, 0xa1, 0x9d, 0x85, 0xc9, 0xb8,
    0xf4, 0xed, 0x1d, 0x3c, 0x88, 0x05, 0x0f, 0x9e, 0x42, 0xf2, 0x93, 0x2f, 0xa4, 0x3c, 0x3e, 0xcd,
    0x2c, 0xee, 0x14, 0x78, 0x74, 0x56, 0x42, 0x91, 0xe5, 0x20, 0xc8, 0x6a, 0xd7, 0x8e, 0xb6, 0x97,
    0x70, 0x6d, 0xce, 0x9b, 0x3d, 0x08, 0x8e, 0xca, 0xb6, 0x6a, 0x76, 0x96, 0x24, 0x2c, 0xc4, 0x70,
    0xac, 0xca, 0x0b, 0xcd, 0x2a, 0xa6, 0x15, 0x1e, 0x46, 0xcf, 0x22, 0x19, 0x96, 0x25, 0x0c, 0x31,
    0xfa, 0x03, 0x22, 0xeb, 0x81, 0x8c, 0xe2, 0xcc, 0x79, 0xb2, 0x29, 0xe9, 0x5c, 0x30, 0x2a, 0xa6,
    0x36, 0xf4, 0xe8, 0x95, 0x9e, 0x54, 0x4f, 0xb8, 0xcd, 0xf3, 0x1d, 0x86, 0x6f, 0x03, 0xd6, 0xa6,
    0xa8, 0x5b, 0x09, 0xa1, 0x6e, 0x0b, 0x10, 0xcd, 0xe2, 0xe9, 0x1f, 0xdb, 0x75, 0xdd, 0xa8, 0xe9,
    0xe4, 0xf4, 0xcd, 0x90, 0x8d, 0x3f, 0x4e, 0xe0, 0xc3, 0x85, 0x89, 0x07, 0xa2, 0x98, 0x50, 0x15,
    0x7a, 0x8a, 0x12, 0x3e, 0x23, 0xc6, 0x77, 0x29, 0xb2, 0xa4, 0x02, 0x56, 0x25, 0xae, 0x3e, 0xac,
    0x5e, 0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x04, 0x1f, 0x51, 0x5d, 0x6f,
    0xde, 0xf8, 0xf2, 0xc7, 0x00, 0x00, 0x00, 0x20, 0x01, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x78,
    0x6c, 0x2f, 0x73, 0x68, 0x61, 0x72, 0x65, 0x64, 0x53, 0x74, 0x72, 0x69, 0x6e, 0x67, 0x73, 0x2e,
    0x78, 0x6d, 0x6c, 0x55, 0x8f, 0xdd, 0x4e, 0xc3, 0x30, 0x0c, 0x85, 0x5f, 0xc5, 0xf2, 0x05, 0x77,
    0x5b, 0x0a, 0x42, 0x68, 0x82, 0x24, 0x93, 0x98, 0xc4, 0x1d, 0xa8, 0x48, 0xf0, 0x00, 0xa1, 0x35,
    0x6b, 0xa4, 0xc6, 0x29, 0xb1, 0x0b, 0xec, 0xed, 0x49, 0x37, 0xf1, 0x77, 0x63, 0xe9, 0x7c, 0xc7,
    0x3e, 0xb6, 0xed, 0xf6, 0x33, 0x8d, 0xf0, 0x4e, 0x45, 0x62, 0x66, 0x87, 0xe7, 0xeb, 0x06, 0x81,
    0xb8, 0xcb, 0x7d, 0xe4, 0xbd, 0xc3, 0xe7, 0xa7, 0xbb, 0xd5, 0x06, 0x41, 0x34, 0x70, 0x1f, 0xc6,
    0xcc, 0xe4, 0xf0, 0x40, 0x82, 0x5b, 0x6f, 0x45, 0x14, 0xea, 0x24, 0x8b, 0xc3, 0x41, 0x75, 0xba,
    0x36, 0x46, 0xba, 0x81, 0x52, 0x90, 0x75, 0x9e, 0x88, 0xab, 0xf3, 0x9a, 0x4b, 0x0a, 0x5a, 0x65,
    0xd9, 0x1b, 0x99, 0x0a, 0x85, 0x5e, 0x06, 0x22, 0x4d, 0xa3, 0xb9, 0x68, 0x9a, 0x2b, 0x93, 0x42,
    0x64, 0x84, 0x2e, 0xcf, 0xac, 0x0e, 0x2f, 0x11, 0x66, 0x8e, 0x6f, 0x33, 0xed, 0xbe, 0x75, 0x5d,
    0x10, 0xbd, 0x55, 0xdf, 0x86, 0xa2, 0xd6, 0xa8, 0xb7, 0x66, 0xd1, 0x27, 0xf6, 0xa8, 0x87, 0xff,
    0xa8, 0x2c, 0xf4, 0x36, 0x8f, 0x0a, 0x27, 0x5e, 0x8e, 0xa8, 0xb4, 0xb5, 0xbc, 0x98, 0x45, 0xb7,
    0xc7, 0x8e, 0xfb, 0xcd, 0x8f, 0xfd, 0x27, 0xed, 0x61, 0x56, 0x38, 0x0b, 0x69, 0xba, 0x81, 0x8f,
    0x50, 0x2f, 0x2c, 0xbf, 0xd1, 0xa6, 0xfe, 0xe8, 0xbf, 0x00, 0x50, 0x4b, 0x03, 0x04, 0x14, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x04, 0x1f, 0x51, 0x5d, 0x81, 0x87, 0x50, 0xb1, 0xe4, 0x00, 0x00, 0x00,
    0xbf, 0x01, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x78, 0x6c, 0x2f, 0x77, 0x6f, 0x72, 0x6b, 0x73,
    0x68, 0x65, 0x65, 0x74, 0x73, 0x2f, 0x73, 0x68, 0x65, 0x65, 0x74, 0x31, 0x2e, 0x78, 0x6d, 0x6c,
    0x6d, 0xd0, 0xe1, 0x4e, 0xc3, 0x20, 0x10, 0x07, 0xf0, 0x57, 0x21, 0x7c, 0x77, 0xd7, 0xd2, 0xc5,
    0x18, 0x43, 0x59, 0x9c, 0xc6, 0x17, 0x50, 0x1f, 0x00, 0xdb, 0x73, 0x25, 0x96, 0xa3, 0xe1, 0x48,
    0xa7, 0x6f, 0x2f, 0xeb, 0x94, 0x54, 0xdd, 0x37, 0xee, 0xfe, 0xc7, 0x2f, 0x70, 0x7a, 0xf7, 0xe1,
    0x47, 0x31, 0x63, 0x64, 0x17, 0xa8, 0x95, 0xf5, 0xa6, 0x92, 0x02, 0xa9, 0x0b, 0xbd, 0xa3, 0x43,
    0x2b, 0x5f, 0x9e, 0x1f, 0xaf, 0x6e, 0xa4, 0xe0, 0x64, 0xa9, 0xb7, 0x63, 0x20, 0x6c, 0xe5, 0x27,
    0xb2, 0xdc, 0x19, 0x7d, 0x0c, 0xf1, 0x9d, 0x07, 0xc4, 0x24, 0xf2, 0x7d, 0xe2, 0x56, 0x0e, 0x29,
    0x4d, 0xb7, 0x00, 0xdc, 0x0d, 0xe8, 0x2d, 0x6f, 0xc2, 0x84, 0x94, 0x93, 0xb7, 0x10, 0xbd, 0x4d,
    0xb9, 0x8c, 0x07, 0xe0, 0x29, 0xa2, 0xed, 0x97, 0x4b, 0x7e, 0x04, 0x55, 0x55, 0xd7, 0xe0, 0xad,
    0x23, 0x69, 0xf4, 0xd2, 0x7b, 0xb0, 0xc9, 0x1a, 0x1d, 0xc3, 0x51, 0xc4, 0xfc, 0x8e, 0xdc, 0xed,
    0x4e, 0x87, 0xbb, 0x5a, 0x8a, 0xd4, 0x4a, 0xce, 0xf5, 0x6c, 0x2a, 0x0d, 0xb3, 0xd1, 0xd0, 0x7d,
    0x67, 0xfb, 0x75, 0x56, 0x97, 0x0c, 0xb2, 0x51, 0x20, 0x55, 0x20, 0xb5, 0x1a, 0x56, 0x7f, 0x20,
    0x75, 0x26, 0xd4, 0x65, 0xa3, 0x29, 0x46, 0xb3, 0x32, 0x9a, 0xdf, 0xc6, 0xfd, 0x39, 0x73, 0x34,
    0x3a, 0xc2, 0xa7, 0x14, 0xf3, 0x8c, 0x63, 0xa3, 0x93, 0xe1, 0xc9, 0x46, 0xd4, 0x90, 0xf2, 0xec,
    0xa9, 0xf1, 0x4f, 0xdf, 0xfe, 0xe8, 0xfb, 0xed, 0x22, 0xbc, 0x5e, 0xfc, 0x0e, 0xac, 0x76, 0x04,
    0x65, 0xf9, 0xe6, 0x0b, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00,
    0x04, 0x1f, 0x51, 0x5d, 0x7d, 0xb8, 0x36, 0xc6, 0x6e, 0x00, 0x00, 0x00, 0x7a, 0x00, 0x00, 0x00,
    0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x5b, 0x43, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x5f, 0x54, 0x79, 0x70, 0x65, 0x73,
    0x5d, 0x2e, 0x78, 0x6d, 0x6c, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x08,
    0x00, 0x04, 0x1f, 0x51, 0x5d, 0x2f, 0x65, 0xe1, 0x1f, 0xbc, 0x00, 0x00, 0x00, 0x1a, 0x01, 0x00,
    0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x9f,
    0x00, 0x00, 0x00, 0x78, 0x6c, 0x2f, 0x77, 0x6f, 0x72, 0x6b, 0x62, 0x6f, 0x6f, 0x6b, 0x2e, 0x78,
    0x6d, 0x6c, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x04, 0x1f,
    0x51, 0x5d, 0x5a, 0xfd, 0x82, 0x6b, 0xb1, 0x00, 0x00, 0x00, 0x28, 0x01, 0x00, 0x00, 0x1a, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x88, 0x01, 0x00, 0x00,
    0x78, 0x6c, 0x2f, 0x5f, 0x72, 0x65, 0x6c, 0x73, 0x2f, 0x77, 0x6f, 0x72, 0x6b, 0x62, 0x6f, 0x6f,
    0x6b, 0x2e, 0x78, 0x6d, 0x6c, 0x2e, 0x72, 0x65, 0x6c, 0x73, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03,
    0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x04, 0x1f, 0x51, 0x5d, 0x6f, 0xde, 0xf8, 0xf2, 0xc7, 0x00,
    0x00, 0x00, 0x20, 0x01, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x01, 0x71, 0x02, 0x00, 0x00, 0x78, 0x6c, 0x2f, 0x73, 0x68, 0x61, 0x72, 0x65,
    0x64, 0x53, 0x74, 0x72, 0x69, 0x6e, 0x67, 0x73, 0x2e, 0x78, 0x6d, 0x6c, 0x50, 0x4b, 0x01, 0x02,
    0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x04, 0x1f, 0x51, 0x5d, 0x81, 0x87, 0x50, 0xb1,
    0xe4, 0x00, 0x00, 0x00, 0xbf, 0x01, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x6a, 0x03, 0x00, 0x00, 0x78, 0x6c, 0x2f, 0x77, 0x6f, 0x72,
    0x6b, 0x73, 0x68, 0x65, 0x65, 0x74, 0x73, 0x2f, 0x73, 0x68, 0x65, 0x65, 0x74, 0x31, 0x2e, 0x78,
    0x6d, 0x6c, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00, 0x4e, 0x01,
    0x00, 0x00, 0x84, 0x04, 0x00, 0x00, 0x00, 0x00,
};
// A zip with a stored entry (a.txt) and one deflated with the fixed codes (b.txt).
const unsigned char kSmallZip[] = {
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x1f, 0x51, 0x5d, 0xb5, 0x01,
    0xaf, 0x0f, 0x0b, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x61, 0x2e,
    0x74, 0x78, 0x74, 0x73, 0x74, 0x6f, 0x72, 0x65, 0x64, 0x20, 0x74, 0x65, 0x78, 0x74, 0x50, 0x4b,
    0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x21, 0x00, 0x38, 0x2f, 0x00, 0x52,
    0x0f, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x62, 0x2e, 0x74, 0x78,
    0x74, 0x4b, 0x4c, 0x4a, 0x4e, 0x84, 0x21, 0x85, 0x8c, 0xd4, 0x9c, 0x9c, 0x7c, 0x08, 0x09, 0x00,
    0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x1f, 0x51, 0x5d,
    0xb5, 0x01, 0xaf, 0x0f, 0x0b, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x61, 0x2e,
    0x74, 0x78, 0x74, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00,
    0x00, 0x21, 0x00, 0x38, 0x2f, 0x00, 0x52, 0x0f, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x2e, 0x00, 0x00,
    0x00, 0x62, 0x2e, 0x74, 0x78, 0x74, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x02, 0x00, 0x66, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00,
};
} // namespace

TEST_CASE("An Excel workbook's first sheet as rows of cells, for a data link") {
    const std::string_view wb(reinterpret_cast<const char*>(kWorkbook), sizeof(kWorkbook));
    std::vector<std::vector<std::string>> rows;
    std::string error;
    REQUIRE(io::parse_xlsx(wb, rows, error));
    REQUIRE(rows.size() == 4);
    REQUIRE(rows[0] == std::vector<std::string>{"Part", "Qty", ""});
    REQUIRE(rows[1] == std::vector<std::string>{"Bolt M8", "12", ""});
    REQUIRE(rows[2] == std::vector<std::string>{"Nut & washer", "", "spare"});
    REQUIRE(rows[3] == std::vector<std::string>{"", "TRUE", ""});
    const std::string_view zip(reinterpret_cast<const char*>(kSmallZip), sizeof(kSmallZip));
    std::string out;
    REQUIRE(io::zip_entry(zip, "a.txt", out));
    REQUIRE(out == "stored text");
    REQUIRE(io::zip_entry(zip, "b.txt", out));
    REQUIRE(out == "abcabcabcabc hello hello");
    REQUIRE_FALSE(io::zip_entry(zip, "c.txt", out));
    REQUIRE_FALSE(io::parse_xlsx(zip, rows, error)); // a zip, but no workbook in it
    REQUIRE(error == "not an Excel workbook (.xlsx)");
}

TEST_CASE("A multileader style's Leader type is kept, and MLEADER starts from it") {
    io::Document doc;
    MLeaderStyle st;
    st.name = "Curvy";
    st.leader_type = 1;
    doc.mleader_styles = {MLeaderStyle{}, st};
    io::Document again;
    REQUIRE(io::parse_native(io::serialize_native(doc), again).ok);
    REQUIRE(again.mleader_styles.size() == 2);
    REQUIRE(again.mleader_styles[1].leader_type == 1);
    REQUIRE(again.mleader_styles[1].name == "Curvy");
}

TEST_CASE("Layer group filters are kept with the drawing") {
    io::Document doc;
    doc.layer_filters = {LayerGroupFilter{"Walls and doors", {"A-WALL", "A DOOR"}}, LayerGroupFilter{"Empty", {}}};
    io::Document again;
    REQUIRE(io::parse_native(io::serialize_native(doc), again).ok);
    REQUIRE(again.layer_filters == doc.layer_filters);

    GeometryEngine engine;
    engine.start();
    engine.submit(SetLayerFiltersCommand{doc.layer_filters});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layer_filters.size() == 2; }));
    REQUIRE(engine.snapshot().layer_filters[0].layers[1] == "A DOOR");
    const io::Document out = dump(engine, "musacad_layer_filters.musa");
    REQUIRE(out.layer_filters == doc.layer_filters);
    engine.stop();
}

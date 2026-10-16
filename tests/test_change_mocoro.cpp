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
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
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

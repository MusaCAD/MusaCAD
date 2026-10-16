// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// CHANGE and HATCHEDIT (#55), MOCORO and FLATTEN (#77), -MTEXT (#43), DIMINSPECT (#60).

#include <chrono>
#include <cmath>
#include <cstdint>
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
    REQUIRE(h.out.prompt == "Select text or [Masktype/Offset]: ");
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
    REQUIRE(h.out.prompt == "Enter label value or <None>: ");
    h.run({"B2"});
    REQUIRE(h.out.prompt == "Enter inspection rate, . for none <100%>: ");
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

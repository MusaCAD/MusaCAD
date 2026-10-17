// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Layers that are not plotted and layer descriptions (#69), WIPEOUT's Display but not plot
// (#40), PASTEBLOCK (#55); and DIMCONTINUE on DIMLAYER, LAYISO per drawing, PEDIT
// Multiple's Undo, FILLET joining wide polylines, EXTEND of an elliptical arc to an
// edge's extension.

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
};

AddLineCommand line_on(std::uint16_t layer, Vec2 a, Vec2 b, std::uint64_t group) {
    EntityProps p;
    p.layer = layer;
    return AddLineCommand{a, b, group, p};
}

bool plot_ready(GeometryEngine& e, std::uint64_t before) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (e.plot_snapshot_version() > before) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

std::uint64_t g = 100;
} // namespace

TEST_CASE("Layers keep Plot and Description in the drawing and in DXF") {
    io::Document doc;
    Layer notes;
    notes.name = "Notes";
    notes.plot = false;
    notes.description = "Not for  the print";
    doc.layers.push_back(notes);
    Layer walls;
    walls.name = "Walls";
    doc.layers.push_back(walls);

    io::Document native;
    REQUIRE(io::parse_native(io::serialize_native(doc), native).ok);
    REQUIRE(native.layers.size() == 3);
    REQUIRE_FALSE(native.layers[1].plot);
    REQUIRE(native.layers[1].description == "Not for  the print");
    REQUIRE(native.layers[2].plot);
    REQUIRE(native.layers[2].description.empty());

    const std::string dxf = io::serialize_dxf(doc);
    REQUIRE(dxf.find("AcAecLayerStandard") != std::string::npos);
    io::Document back;
    REQUIRE(io::parse_dxf(dxf, back).ok);
    REQUIRE(back.layers.size() == 3);
    REQUIRE_FALSE(back.layers[1].plot);
    REQUIRE(back.layers[1].description == "Not for  the print");
    REQUIRE(back.layers[2].plot);
}

TEST_CASE("A plot leaves out the layers not plotted, and wipeout frames shown but not plotted") {
    GeometryEngine engine;
    engine.start();
    Layer np;
    np.name = "NoPlot";
    np.plot = false;
    engine.submit(AddLayerCommand{np});
    engine.submit(line_on(0, {0, 0}, {10, 0}, 1));
    engine.submit(line_on(1, {0, 5}, {10, 5}, 2));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; })); // both on screen
    std::uint64_t before = engine.plot_snapshot_version();
    engine.submit(BuildPlotSnapshotCommand{0.01});
    REQUIRE(plot_ready(engine, before));
    REQUIRE(engine.plot_snapshot().line_vertices.size() == 2); // the plotted layer only

    AddHatchCommand w;
    w.loops = {{{20, 0}, {30, 0}, {30, 10}}};
    w.pattern_name = "WIPEOUT";
    w.group = 3;
    engine.submit(w);
    engine.submit(SetWipeoutFramesCommand{true, false}); // Display but not plot
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 10; })); // the frame shows
    before = engine.plot_snapshot_version();
    engine.submit(BuildPlotSnapshotCommand{0.01});
    REQUIRE(plot_ready(engine, before));
    REQUIRE(engine.plot_snapshot().line_vertices.size() == 2); // no frame on paper
    REQUIRE_FALSE(engine.plot_snapshot().wipeout_vertices.empty()); // the mask still plots
    const io::Document doc = dump(engine, "musacad_layer_plot.musa");
    REQUIRE(doc.wipeout_frames);
    REQUIRE_FALSE(doc.wipeout_frames_plot);
    REQUIRE_FALSE(doc.layers[1].plot);
    engine.stop();
}

TEST_CASE("The -LAYER Plot and Description options; WIPEOUT Frames Display but not plot") {
    using Op = LayerEditCommand::Op;
    H h;
    h.proc.submit_line("-LAYER");
    REQUIRE(h.out.prompt ==
            "Enter an option [?/Make/Set/New/Rename/ON/OFF/Color/Ltype/LWeight/TRansparency/MATerial/Plot/Freeze/Thaw/"
            "LOck/Unlock/stAte/Description/rEconcile/Xref]: ");
    h.proc.submit_line("P");
    REQUIRE(h.out.prompt == "Enter a plotting preference [Plot/No plot] <Plot>: ");
    h.proc.submit_line("N");
    REQUIRE(h.out.prompt == "Enter layer name(s) for this plot preference <current>: ");
    h.proc.submit_line("Notes");
    REQUIRE(h.last<LayerEditCommand>()->op == Op::Plot);
    REQUIRE_FALSE(h.last<LayerEditCommand>()->plot);
    REQUIRE(h.last<LayerEditCommand>()->names == "Notes");
    h.proc.submit_line("D");
    REQUIRE(h.out.prompt == "Enter layer description: ");
    REQUIRE(h.proc.wants_free_text());
    h.proc.submit_line("Walls of the  ground floor");
    REQUIRE(h.out.prompt == "Enter name list of layer(s) to apply the description to <current>: ");
    h.proc.submit_line("");
    REQUIRE(h.last<LayerEditCommand>()->op == Op::Description);
    REQUIRE(h.last<LayerEditCommand>()->description == "Walls of the  ground floor");
    REQUIRE(h.last<LayerEditCommand>()->names.empty());
    h.proc.submit_line("");
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.submit_line("WIPEOUT");
    h.proc.submit_line("F");
    REQUIRE(h.out.prompt == "Enter mode [ON/OFF/Display but not plot] <ON>: ");
    h.proc.submit_line("D");
    REQUIRE(h.last<SetWipeoutFramesCommand>()->on);
    REQUIRE_FALSE(h.last<SetWipeoutFramesCommand>()->plot);

    GeometryEngine engine;
    engine.start();
    engine.submit(LayerEditCommand{Op::New, "Notes"});
    LayerEditCommand np{Op::Plot, "notes"};
    np.plot = false;
    engine.submit(np);
    LayerEditCommand desc{Op::Description, "Notes"};
    desc.description = "Remarks";
    engine.submit(desc);
    engine.submit(LayerEditCommand{Op::List, "N*"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("Layer name  State", 0) == 0; }));
    REQUIRE(engine.snapshot().status.find("\"Notes\"  On No-plot  255,255,255  Continuous  0.25mm  Remarks") !=
            std::string::npos);
    engine.stop();
}

TEST_CASE("PASTEBLOCK pastes the copied objects as one block at the point") {
    H h;
    h.proc.submit_line("PASTEBLOCK");
    REQUIRE(h.out.prompt == "Specify insertion point: ");
    h.proc.submit_line("50,50");
    const auto* def = h.last<DefineBlockCommand>();
    REQUIRE(def != nullptr);
    REQUIRE(def->name.rfind("A$C", 0) == 0);
    REQUIRE(def->name.size() == 11);
    REQUIRE(def->base == Vec2{50, 50});
    REQUIRE(h.last<PasteClipboardCommand>()->group == def->group);

    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(CopyClipboardCommand{});
    for (const Command& c : h.cmds) {
        engine.submit(c);
    }
    const io::Document doc = dump(engine, "musacad_pasteblock.musa");
    REQUIRE(doc.lines.size() == 1); // the original stays
    REQUIRE(doc.inserts.size() == 1);
    REQUIRE(doc.block_defs.size() == 1);
    REQUIRE(doc.block_defs[0].name == def->name);
    REQUIRE(doc.inserts[0].pos == Vec2{50, 50});
    engine.stop();
}

TEST_CASE("DIMCONTINUE and DIMBASELINE put the new dimension on DIMLAYER") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear), {0, 0}, {20, 0}, {10, -12}, 0, 1});
    Layer dims;
    dims.name = "Dims";
    engine.submit(AddLayerCommand{dims});
    engine.submit(SetDimLayerCommand{"Dims"});
    engine.submit(ChainDimensionCommand{{50, 0}, false, 2});
    engine.submit(ChainDimensionCommand{{70, 0}, true, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Baseline dimension added."; }));
    const io::Document doc = dump(engine, "musacad_dimlayer_chain.musa");
    REQUIRE(doc.dims.size() == 3);
    REQUIRE(doc.dims[0].props.layer == 0);
    REQUIRE(doc.dims[1].props.layer == 1);
    REQUIRE(doc.dims[2].props.layer == 1);
    engine.stop();
}

TEST_CASE("LAYUNISO in a drawing after a switch to another tab and back") {
    GeometryEngine engine;
    engine.start();
    Layer a;
    a.name = "A";
    engine.submit(AddLayerCommand{a});
    engine.submit(line_on(1, {0, 0}, {10, 0}, 1));
    engine.submit(line_on(0, {0, 5}, {10, 5}, 2));
    engine.submit(SelectPickCommand{{5, 0}, 0.5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(LayerToolCommand{LayerToolCommand::Op::Isolate, {}, 0.0, 9});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layers.size() == 2 && !s.layers[0].on; }));
    const std::uint64_t d1 = engine.snapshot().active_document_id;
    engine.submit(CreateDocumentCommand{});
    REQUIRE(wait_until(engine, [d1](const auto& s) { return s.documents.size() == 2 && s.active_document_id != d1; }));
    engine.submit(SwitchDocumentCommand{d1});
    engine.submit(LayerToolCommand{LayerToolCommand::Op::Unisolate});
    REQUIRE(wait_until(engine, [d1](const auto& s) {
        return s.active_document_id == d1 && s.layers.size() == 2 && s.layers[0].on &&
               s.status == "Layers isolated by LAYISO have been restored.";
    }));
    engine.stop();
}

TEST_CASE("PEDIT Multiple's Undo leaves the polylines as they were selected") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {10, 0}, {10, 10}}, false, 1});
    engine.submit(AddPolylineCommand{{{20, 0}, {30, 0}, {30, 10}}, false, 2});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    PeditCommand close;
    close.op = 0;
    close.group = 5;
    close.selection = true;
    engine.submit(close);
    engine.submit(UndoLastGroupCommand{5, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    PeditCommand open = close; // the next option works on them
    open.op = 10;              // Fit
    open.group = 6;
    engine.submit(open);
    const io::Document doc = dump(engine, "musacad_pedit_multiple_undo.musa");
    REQUIRE(doc.polylines.size() == 2);
    for (const io::DocPolyline& p : doc.polylines) {
        REQUIRE_FALSE(p.closed);
        REQUIRE(p.points.size() > 3); // fitted
    }
    engine.stop();
}

TEST_CASE("FILLET joining a line and a wide polyline keeps the polyline's width") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    AddPolylineCommand pl;
    pl.points = {{20, 20}, {12, 20}, {12, 8}};
    pl.widths = {1, 1, 1, 1, 1, 1};
    pl.group = 2;
    engine.submit(pl);
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty() || !s.fill_vertices.empty(); }));
    engine.submit(FilletPickCommand{{5, 0}, {12, 14}, 2.0, 0.5, ++g});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Filleted."; }));
    const io::Document doc = dump(engine, "musacad_fillet_join_widths.musa");
    REQUIRE(doc.polylines.size() == 1);
    const io::DocPolyline& p = doc.polylines[0];
    REQUIRE(p.points.size() == 5);
    REQUIRE(p.widths.size() == 10);
    REQUIRE(p.widths[0] == Approx(0.0)); // the line's part
    REQUIRE(p.widths[1] == Approx(0.0));
    REQUIRE(p.widths[2] == Approx(1.0)); // the arc takes the polyline's
    REQUIRE(p.widths[3] == Approx(1.0));
    REQUIRE(p.widths[4] == Approx(1.0));
    REQUIRE(p.widths[7] == Approx(1.0));
    engine.stop();
}

TEST_CASE("EXTEND an elliptical arc with Edge Extend to where a short edge would reach") {
    GeometryEngine engine;
    engine.start();
    // The elliptical arc from (10, 100) round to (0, 105); a short edge at x = -5 well below it.
    engine.submit(AddEllipseCommand{{0, 100}, {10, 0}, 0.5, 0.0, kHalfPi, 1});
    engine.submit(AddLineCommand{{-5, 80}, {-5, 85}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(ExtendPickCommand{{0.3, 105}, 0.5, ++g});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Extend: no boundary ahead of that end."; }));
    io::Document doc = dump(engine, "musacad_extend_ellipse_edge0.musa");
    REQUIRE(doc.ellipses[0].end == Approx(kHalfPi)); // the edge as drawn does not reach
    engine.submit(SetTrimEdgesCommand{SetTrimEdgesCommand::Op::EdgeMode, true});
    engine.submit(ExtendPickCommand{{0.3, 105}, 0.5, ++g});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Extended."; }));
    doc = dump(engine, "musacad_extend_ellipse_edge1.musa");
    REQUIRE(doc.ellipses[0].end == Approx(2.0 * kPi / 3.0).margin(1e-2));
    engine.stop();
}

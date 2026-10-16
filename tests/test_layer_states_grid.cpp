// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// LAYERP, LAYERPMODE and LAYERSTATE (#69); GRID's spacing and options (#65); pastes and
// references scaled by the two drawings' units (#81).

#include <chrono>
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
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/units.hpp"
#include "musacad/render/camera.hpp"
#include "musacad/render/grid.hpp"

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

void set_insunits(GeometryEngine& e, std::uint8_t u) {
    DrawingUnits du;
    du.insunits = u;
    e.submit(SetUnitsCommand{du});
}
} // namespace

TEST_CASE("LAYERP takes back layer changes one at a time; LAYERPMODE off stops it") {
    using Op = LayerEditCommand::Op;
    GeometryEngine engine;
    engine.start();
    engine.submit(LayerEditCommand{Op::New, "A,B"});
    LayerEditCommand red{Op::Color, "A"};
    red.color = Rgb{255, 0, 0};
    engine.submit(red);
    engine.submit(LayerEditCommand{Op::Off, "B"});
    engine.submit(SetCurrentLayerCommand{1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.current_layer == 1 && s.layers.size() == 3; }));
    engine.submit(LayerPreviousCommand{}); // the current layer back
    REQUIRE(wait_until(engine, [](const auto& s) { return s.current_layer == 0; }));
    engine.submit(LayerPreviousCommand{}); // B on again
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layers[2].on; }));
    engine.submit(LayerPreviousCommand{}); // A white again
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layers[1].color == Rgb{255, 255, 255}; }));
    REQUIRE(engine.snapshot().status == "Restored previous layer states.");
    engine.submit(LayerPreviousCommand{}); // the layers made stay
    engine.submit(LayerPreviousCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "*No previous layer state*"; }));
    REQUIRE(engine.snapshot().layers.size() == 3);

    engine.submit(SetLayerpModeCommand{false});
    engine.submit(LayerEditCommand{Op::Off, "A"});
    engine.submit(LayerPreviousCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status == "LAYERP is not tracking layer changes (LAYERPMODE is off).";
    }));
    REQUIRE_FALSE(engine.snapshot().layers[1].on);
    engine.stop();
}

TEST_CASE("LAYERSTATE saves, restores, renames and deletes named layer states, kept with the drawing") {
    using Op = LayerEditCommand::Op;
    using LS = LayerStateCommand::Op;
    GeometryEngine engine;
    engine.start();
    engine.submit(LayerEditCommand{Op::New, "Walls,Notes"});
    engine.submit(LayerEditCommand{Op::Freeze, "Notes"});
    engine.submit(LayerStateCommand{LS::Save, "Plan", {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Layer state \"Plan\" saved."; }));
    engine.submit(LayerEditCommand{Op::Thaw, "Notes"});
    engine.submit(LayerEditCommand{Op::Lock, "Walls"});
    engine.submit(SetCurrentLayerCommand{2});
    engine.submit(LayerStateCommand{LS::Restore, "plan", {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Layer state \"Plan\" restored."; }));
    REQUIRE(engine.snapshot().current_layer == 0);
    REQUIRE(engine.snapshot().layers[2].frozen);
    REQUIRE_FALSE(engine.snapshot().layers[1].locked);
    engine.submit(LayerStateCommand{LS::Rename, "Plan", "Ground floor"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Layer state renamed to \"Ground floor\"."; }));
    engine.submit(LayerStateCommand{LS::Save, "Second", {}});
    engine.submit(LayerStateCommand{LS::Restore, "Nope", {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Cannot find layer state \"Nope\"."; }));
    io::Document doc = dump(engine, "musacad_layer_states.musa");
    REQUIRE(doc.layer_states.size() == 2);
    REQUIRE(doc.layer_states[0].name == "Ground floor");
    REQUIRE(doc.layer_states[0].current == "0");
    REQUIRE(doc.layer_states[0].layers.size() == 3);
    REQUIRE(doc.layer_states[0].layers[2].frozen);
    io::Document again;
    REQUIRE(io::parse_native(io::serialize_native(doc), again).ok);
    REQUIRE(again.layer_states == doc.layer_states);
    engine.submit(LayerStateCommand{LS::Delete, "second", {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Layer state \"Second\" deleted."; }));
    engine.stop();

    H h;
    h.proc.submit_line("LAYERSTATE");
    REQUIRE(h.out.prompt == "Enter an option [?/Save/Restore/Delete/Name]: ");
    h.proc.submit_line("S");
    REQUIRE(h.out.prompt == "Enter new layer state name: ");
    REQUIRE(h.proc.wants_free_text());
    h.proc.submit_line("Ground floor");
    REQUIRE(h.last<LayerStateCommand>()->op == LS::Save);
    REQUIRE(h.last<LayerStateCommand>()->name == "Ground floor");
    h.proc.submit_line("N");
    h.proc.submit_line("Ground floor");
    REQUIRE(h.out.prompt == "Enter new layer state name: ");
    h.proc.submit_line("First");
    REQUIRE(h.last<LayerStateCommand>()->to == "First");
    h.proc.submit_line("");
    REQUIRE_FALSE(h.proc.has_active_command());
    h.proc.submit_line("LAYERPMODE");
    REQUIRE(h.out.prompt == "Enter LAYERP mode [ON/OFF]: ");
    h.proc.submit_line("OFF");
    REQUIRE(h.last<SetLayerpModeCommand>()->on == false);
    h.proc.submit_line("LAYERP");
    REQUIRE(h.last<LayerPreviousCommand>() != nullptr);
}

TEST_CASE("GRID: a spacing of its own, the major lines, adaptive, limits and aspect") {
    H h;
    h.proc.set_grid_spacing(2.5); // the snap spacing
    h.proc.submit_line("GRID");
    h.proc.submit_line("2x");
    REQUIRE(h.proc.grid_settings().x == 5.0);
    h.proc.submit_line("GRID");
    h.proc.submit_line("M");
    REQUIRE(h.out.prompt == "Specify the number of grid divisions per major grid line <5>: ");
    h.proc.submit_line("4");
    REQUIRE(h.proc.grid_settings().major == 4);
    h.proc.submit_line("GRID");
    h.proc.submit_line("D");
    REQUIRE(h.out.prompt == "Turn adaptive behavior on [Yes/No] <Yes>: ");
    h.proc.submit_line("Y");
    REQUIRE(h.out.prompt == "Allow subdivision below grid spacing [Yes/No] <Yes>: ");
    h.proc.submit_line("N");
    REQUIRE_FALSE(h.proc.grid_settings().subdivide);
    h.proc.submit_line("GRID");
    h.proc.submit_line("L");
    h.proc.submit_line("N");
    REQUIRE_FALSE(h.proc.grid_settings().beyond_limits);
    h.proc.submit_line("GRID");
    h.proc.submit_line("A");
    REQUIRE(h.out.prompt == "Specify the horizontal spacing(X) <5.0000>: ");
    h.proc.submit_line("10");
    h.proc.submit_line("20");
    REQUIRE(h.proc.grid_settings().x == 10.0);
    REQUIRE(h.proc.grid_settings().y == 20.0);
    h.proc.submit_line("GRID");
    h.proc.submit_line("S");
    REQUIRE(h.proc.grid_settings().x == 2.5);

    // The lines: 10 apart in a 100-unit view at 4 pixels a unit, a major every 4th.
    musacad::render::Camera2D cam;
    cam.set_viewport(400, 400);
    cam.set_center({50.0, 50.0});
    cam.set_scale(4.0);
    GridSettings g;
    g.x = 10.0;
    g.y = 10.0;
    g.major = 4;
    musacad::render::GridResult r = musacad::render::build_grid(cam, g, {0, 0}, {420, 297});
    REQUIRE(r.minor_spacing == Approx(10.0));
    REQUIRE(r.major.size() + r.minor.size() == 2 * 22); // x and y 0..100, both ways
    REQUIRE(r.major.size() == 2 * 6);                  // 0, 40, 80 each way
    g.beyond_limits = false;
    r = musacad::render::build_grid(cam, g, {0, 0}, {30, 30});
    REQUIRE(r.major.size() + r.minor.size() == 2 * 8); // 0..30 each way
    cam.set_scale(0.1);                                 // zoomed far out: fewer lines
    g.beyond_limits = true;
    r = musacad::render::build_grid(cam, g, {0, 0}, {0, 0});
    REQUIRE(r.minor_spacing == Approx(160.0));
    g.adaptive = false;
    r = musacad::render::build_grid(cam, g, {0, 0}, {0, 0});
    REQUIRE(r.minor.empty()); // too fine to show
    REQUIRE(r.major.empty());
}

TEST_CASE("A paste from a drawing in other units is scaled into this one's") {
    REQUIRE(units::insunits_factor(4, 1) == Approx(1.0 / 25.4)); // millimetres into inches
    REQUIRE(units::insunits_factor(6, 4) == Approx(1000.0));
    REQUIRE(units::insunits_factor(0, 4) == 1.0);
    REQUIRE(units::insunits_factor(2, 1) == Approx(12.0));

    GeometryEngine engine;
    engine.start();
    set_insunits(engine, 6); // metres
    engine.submit(AddLineCommand{{0, 0}, {2, 0}, 1});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(CopyClipboardCommand{});
    engine.submit(CreateDocumentCommand{});
    set_insunits(engine, 4); // millimetres
    engine.submit(PasteClipboardCommand{{100, 100}, 2, true});
    const io::Document doc = dump(engine, "musacad_paste_units.musa");
    REQUIRE(doc.lines.size() == 1);
    REQUIRE(doc.lines[0].a == Vec2{100, 100});
    REQUIRE(doc.lines[0].b.x == Approx(2100.0)); // two metres
    engine.stop();
}

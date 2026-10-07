// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// -LAYER at the command line (#69), and ORTHO / SNAP / GRID (#65).

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad::core;
using musacad::command::CommandOutput;
using musacad::command::CommandProcessor;

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

LayerEditCommand edit(LayerEditCommand::Op op, std::string names, std::string to = {}) {
    LayerEditCommand c;
    c.op = op;
    c.names = std::move(names);
    c.to = std::move(to);
    return c;
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
} // namespace

TEST_CASE("The -LAYER options: New, Make, Set, Rename, the switches by name and wild card, Color, Ltype, LWeight") {
    using Op = LayerEditCommand::Op;
    GeometryEngine engine;
    engine.start();
    engine.submit(edit(Op::New, "Walls, Doors ,Windows"));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "3 layers added."; }));
    engine.submit(edit(Op::Make, "Dims"));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Layer \"Dims\" is now the current layer."; }));
    engine.submit(edit(Op::Set, "walls"));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Layer \"Walls\" is now the current layer."; }));
    engine.submit(edit(Op::Freeze, "W*"));
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status == "1 layer changed. Cannot freeze layer \"Walls\". It is the CURRENT layer.";
    }));
    engine.submit(edit(Op::Rename, "Doors", "Openings"));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Layer renamed to \"Openings\"."; }));
    engine.submit(edit(Op::Rename, "0", "Base"));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Layer 0 cannot be renamed."; }));
    engine.submit(edit(Op::Rename, "Openings", "Walls"));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "A layer named \"Walls\" already exists."; }));
    LayerEditCommand red = edit(Op::Color, "openings,dims");
    red.color = Rgb{255, 0, 0};
    engine.submit(red);
    LayerEditCommand dashed = edit(Op::Ltype, "");
    dashed.linetype = Linetype::Dashed; // the current layer, Walls
    engine.submit(dashed);
    LayerEditCommand thick = edit(Op::LWeight, "Dims");
    thick.lineweight = 50;
    engine.submit(thick);
    engine.submit(edit(Op::Off, "Openings"));
    engine.submit(edit(Op::Lock, "Dims"));
    engine.submit(edit(Op::List, "*"));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("Layer name  State", 0) == 0; }));
    const std::string listing = engine.snapshot().status;
    CHECK(listing.find("\"Walls\"  On Current") != std::string::npos);
    CHECK(listing.find("\"Openings\"  Off  255,0,0") != std::string::npos);
    CHECK(listing.find("\"Windows\"  On Frozen") != std::string::npos);
    engine.submit(edit(Op::Set, "Nowhere"));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Cannot find layer \"Nowhere\"."; }));

    const io::Document doc = dump(engine, "musacad_layer_command.musa");
    REQUIRE(doc.layers.size() == 5);
    REQUIRE(doc.layers[2].name == "Openings");
    REQUIRE(doc.layers[2].color == Rgb{255, 0, 0});
    REQUIRE_FALSE(doc.layers[2].on);
    REQUIRE(doc.layers[1].linetype == Linetype::Dashed);
    REQUIRE(doc.layers[3].frozen); // Windows
    REQUIRE(doc.layers[4].lineweight == 50);
    REQUIRE(doc.layers[4].locked);
    engine.stop();
}

TEST_CASE("The -LAYER prompts; LAYER without a window works the same way") {
    using Op = LayerEditCommand::Op;
    H h;
    h.proc.submit_line("-LAYER");
    REQUIRE(h.out.prompt ==
            "Enter an option [?/Make/Set/New/Rename/ON/OFF/Color/Ltype/LWeight/Freeze/Thaw/LOck/Unlock]: ");
    h.proc.submit_line("N");
    REQUIRE(h.out.prompt == "Enter name list for new layer(s): ");
    h.proc.submit_line("A,B");
    REQUIRE(h.last<LayerEditCommand>()->op == Op::New);
    REQUIRE(h.last<LayerEditCommand>()->names == "A,B");
    h.proc.submit_line("C");
    REQUIRE(h.out.prompt == "New color [Truecolor]: ");
    h.proc.submit_line("blue");
    REQUIRE(h.out.prompt == "Enter name list of layer(s) for color 5 <current>: ");
    h.proc.submit_line("A");
    REQUIRE(h.last<LayerEditCommand>()->op == Op::Color);
    REQUIRE(h.last<LayerEditCommand>()->color == Rgb{0, 0, 255});
    h.proc.submit_line("LW");
    h.proc.submit_line("0.35");
    REQUIRE(h.out.prompt == "Enter name list of layer(s) for lineweight 0.35mm <current>: ");
    h.proc.submit_line("");
    REQUIRE(h.last<LayerEditCommand>()->lineweight == 35);
    REQUIRE(h.last<LayerEditCommand>()->names.empty());
    h.proc.submit_line("R");
    h.proc.submit_line("A");
    REQUIRE(h.out.prompt == "Enter new layer name: ");
    h.proc.submit_line("Axes");
    REQUIRE(h.last<LayerEditCommand>()->to == "Axes");
    h.proc.submit_line("S");
    h.proc.submit_line("");
    REQUIRE(h.out.prompt == "Select object: ");
    h.proc.submit_line("5,5");
    REQUIRE(h.last<LayerToolCommand>()->op == LayerToolCommand::Op::MakeCurrent);
    h.proc.submit_line("bogus");
    REQUIRE(h.out.lines.back() == "Invalid option keyword.");
    h.proc.submit_line("");
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.submit_line("LA"); // no window to open the manager in: the command line
    REQUIRE(h.out.prompt ==
            "Enter an option [?/Make/Set/New/Rename/ON/OFF/Color/Ltype/LWeight/Freeze/Thaw/LOck/Unlock]: ");
    h.proc.submit_line("?");
    REQUIRE(h.out.prompt == "Enter layer name(s) to list <*>: ");
    h.proc.submit_line("");
    REQUIRE(h.last<LayerEditCommand>()->op == Op::List);
    h.proc.submit_line("");
}

TEST_CASE("ORTHO, SNAP and GRID at the command line") {
    H h;
    h.proc.submit_line("ORTHO");
    REQUIRE(h.out.prompt == "Enter mode [ON/OFF] <OFF>: ");
    h.proc.submit_line("ON");
    REQUIRE(h.proc.ortho_mode());
    h.proc.submit_line("ORTHO");
    REQUIRE(h.out.prompt == "Enter mode [ON/OFF] <ON>: ");
    h.proc.submit_line("off");
    REQUIRE_FALSE(h.proc.ortho_mode());

    h.proc.set_grid_spacing(10.0);
    h.proc.submit_line("SNAP");
    REQUIRE(h.out.prompt == "Specify snap spacing or [ON/OFF] <10.0000>: ");
    h.proc.submit_line("2.5");
    REQUIRE(h.proc.snap_spacing() == 2.5);
    REQUIRE(h.proc.snap_mode()); // a spacing turns it on
    h.proc.submit_line("SN");
    h.proc.submit_line("OFF");
    REQUIRE_FALSE(h.proc.snap_mode());
    h.proc.submit_line("SNAP");
    h.proc.submit_line("-1");
    REQUIRE(h.out.lines.back() == "Requires a positive spacing, ON or OFF.");
    h.proc.submit_line("");

    h.proc.submit_line("GRID");
    REQUIRE(h.out.prompt == "Enter mode [ON/OFF] <OFF>: ");
    h.proc.submit_line("10");
    REQUIRE(h.out.lines.back() == "The grid's spacing follows the zoom here; enter ON or OFF.");
    h.proc.submit_line("ON");
    REQUIRE_FALSE(h.proc.has_active_command());
}

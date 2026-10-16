// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// -DWGUNITS: a drawing taken to another unit, its objects scaled to match (#81).

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
} // namespace

TEST_CASE("DWGUNITS takes the drawing to another unit and scales every object, locked ones too") {
    GeometryEngine engine;
    engine.start();
    Layer locked;
    locked.name = "Locked";
    locked.locked = true;
    engine.submit(AddLayerCommand{locked});
    EntityProps on_locked;
    on_locked.layer = 1;
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1, on_locked});
    engine.submit(AddCircleCommand{{50, 50}, 10, 2});
    engine.submit(ScaleDrawingCommand{0.1, 3}); // millimetres to centimetres
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "2 objects scaled by 0.1."; }));
    io::Document doc = dump(engine, "musacad_dwgunits.musa");
    REQUIRE(doc.lines[0].b.x == Approx(10.0));
    REQUIRE(doc.circles[0].center == Vec2{5, 5});
    REQUIRE(doc.circles[0].radius == Approx(1.0));
    REQUIRE(doc.limits_max.x == Approx(42.0));
    engine.submit(UndoLastGroupCommand{});
    doc = dump(engine, "musacad_dwgunits_undo.musa");
    REQUIRE(doc.circles[0].radius == Approx(10.0));
    engine.stop();

    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[&cmds](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    DrawingUnits mm;
    mm.insunits = 4;
    proc.set_units(mm);
    proc.submit_line("-DWGUNITS");
    REQUIRE(out.prompt == "Unit for length <3>: ");
    proc.submit_line("1"); // inches
    REQUIRE(out.prompt == "Linear display format <2>: ");
    proc.submit_line("4");
    REQUIRE(out.prompt == "Linear display precision <4>: ");
    proc.submit_line("3");
    proc.submit_line("");
    proc.submit_line("");
    REQUIRE(out.prompt == "Scale objects in current drawing to reflect change in units? [Yes/No] <Yes>: ");
    proc.submit_line("");
    const auto* scale = std::get_if<ScaleDrawingCommand>(&cmds[cmds.size() - 2]);
    REQUIRE(scale != nullptr);
    REQUIRE(scale->factor == Approx(1.0 / 25.4));
    const auto* units = std::get_if<SetUnitsCommand>(&cmds.back());
    REQUIRE(units != nullptr);
    REQUIRE(units->units.insunits == 1);
    REQUIRE(units->units.linear == LinearFormat::Architectural);
    REQUIRE(units->units.linear_precision == 3);
    REQUIRE_FALSE(proc.has_active_command());
}

TEST_CASE("MLEADERALIGN lines up the multileaders' text along a direction; the arrowheads stay") {
    GeometryEngine engine;
    engine.start();
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
    engine.submit(MLeaderAlignCommand{{12, 10}, 0.5, {15, 30}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 multileader aligned."; }));
    const io::Document doc = dump(engine, "musacad_mleaderalign.musa");
    bool found = false;
    for (const auto& m : doc.mleaders) {
        if (m.content == "B") {
            found = true;
            REQUIRE(m.vertices[0] == Vec2{0, -20}); // the arrowhead stays
            REQUIRE(m.vertices[1] == Vec2{10, -5});
            REQUIRE(m.vertices[2] == Vec2{15, -5});
            REQUIRE(m.block.pos == Vec2{16, -5});
        }
    }
    REQUIRE(found);
    engine.stop();

    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[&cmds](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    proc.set_selection_count(2);
    proc.submit_line("MLA");
    REQUIRE(out.prompt == "Select multileader to align to or [Options]: ");
    proc.submit_line("12,10");
    REQUIRE(out.prompt == "Specify direction: ");
    proc.submit_line("15,30");
    const auto* al = std::get_if<MLeaderAlignCommand>(&cmds.back());
    REQUIRE(al != nullptr);
    REQUIRE(al->base_pick == Vec2{12, 10});
    REQUIRE(al->toward == Vec2{15, 30});
}

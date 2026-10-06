// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// The layer tools (issue #55): LAYOFF, LAYFRZ, LAYLCK, LAYULK, LAYMCUR, LAYCUR, LAYISO,
// LAYUNISO, LAYON and LAYTHW.

#include <chrono>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"

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

using Op = LayerToolCommand::Op;

/// Layers 0, A (index 1) and B (index 2), a line on each: on 0 at y = 0, on A at y = 10,
/// on B at y = 20.
void three_layers(GeometryEngine& engine) {
    Layer a;
    a.name = "A";
    Layer b;
    b.name = "B";
    engine.submit(AddLayerCommand{a});
    engine.submit(AddLayerCommand{b});
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    EntityProps on_a;
    on_a.layer = 1;
    engine.submit(AddLineCommand{{0, 10}, {10, 10}, 2, on_a});
    EntityProps on_b;
    on_b.layer = 2;
    engine.submit(AddLineCommand{{0, 20}, {10, 20}, 3, on_b});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layers.size() == 3 && s.line_vertices.size() == 6; }));
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
    [[nodiscard]] std::vector<LayerToolCommand> tools() const {
        std::vector<LayerToolCommand> v;
        for (const auto& c : cmds) {
            if (const auto* t = std::get_if<LayerToolCommand>(&c)) {
                v.push_back(*t);
            }
        }
        return v;
    }
};
} // namespace

TEST_CASE("LAYOFF turns off the picked object's layer; its Undo turns it back on") {
    GeometryEngine engine;
    engine.start();
    three_layers(engine);
    engine.submit(LayerToolCommand{Op::Off, {5, 10}, 0.5, 9});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.layers[1].on; }));
    REQUIRE(engine.snapshot().status == "Layer \"A\" has been turned off.");
    engine.submit(LayerToolCommand{Op::Off, {5, 0}, 0.5, 9});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return !s.layers[0].on && s.status == "Layer \"0\" (the current layer) has been turned off.";
    }));
    engine.submit(LayerToolCommand{Op::UndoLast});
    engine.submit(LayerToolCommand{Op::UndoLast});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layers[0].on && s.layers[1].on; }));
    engine.submit(LayerToolCommand{Op::UndoLast});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Nothing to undo."; }));
    engine.submit(LayerToolCommand{Op::Off, {50, 50}, 0.5, 9});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "No object found."; }));
    engine.stop();
}

TEST_CASE("LAYFRZ freezes the picked object's layer but never the current one; LAYTHW thaws them all") {
    GeometryEngine engine;
    engine.start();
    three_layers(engine);
    engine.submit(LayerToolCommand{Op::Freeze, {5, 0}, 0.5, 9});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status == "Cannot freeze layer \"0\". It is the current layer.";
    }));
    REQUIRE_FALSE(engine.snapshot().layers[0].frozen);
    engine.submit(LayerToolCommand{Op::Freeze, {5, 20}, 0.5, 9});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layers[2].frozen && s.line_vertices.size() == 4; }));
    engine.submit(LayerToolCommand{Op::AllThaw});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.layers[2].frozen && s.line_vertices.size() == 6; }));
    engine.stop();
}

TEST_CASE("LAYLCK locks the picked object's layer and LAYULK unlocks it, picked on the locked layer") {
    GeometryEngine engine;
    engine.start();
    three_layers(engine);
    engine.submit(LayerToolCommand{Op::Lock, {5, 10}, 0.5, 9});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layers[1].locked; }));
    engine.submit(LayerToolCommand{Op::Unlock, {5, 10}, 0.5, 9});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return !s.layers[1].locked && s.status == "Layer \"A\" has been unlocked.";
    }));
    engine.stop();
}

TEST_CASE("LAYMCUR makes the picked object's layer current; LAYCUR moves the selection to it") {
    GeometryEngine engine;
    engine.start();
    three_layers(engine);
    engine.submit(LayerToolCommand{Op::MakeCurrent, {5, 20}, 0.5, 9});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.current_layer == 2; }));
    REQUIRE(engine.snapshot().status == "B is now the current layer.");

    engine.submit(SelectPickCommand{{5, 10}, 0.5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(LayerToolCommand{Op::ToCurrent, {}, 0.0, 10});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status == "1 object changed to layer \"B\" (the current layer).";
    }));
    // Layer A is empty now: turning B off hides both lines that were on A and B.
    engine.submit(LayerToolCommand{Op::Off, {5, 20}, 0.5, 9});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.stop();
}

TEST_CASE("LAYISO turns off every other layer and LAYUNISO turns them back on; LAYON turns on all") {
    GeometryEngine engine;
    engine.start();
    three_layers(engine);
    engine.submit(SelectPickCommand{{5, 10}, 0.5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(LayerToolCommand{Op::Isolate, {}, 0.0, 9});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.layers[1].on && !s.layers[0].on && !s.layers[2].on && s.current_layer == 1 &&
               s.selection.empty();
    }));
    REQUIRE(engine.snapshot().status == "Layer \"A\" has been isolated.");
    engine.submit(LayerToolCommand{Op::Unisolate});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layers[0].on && s.layers[2].on; }));
    engine.submit(LayerToolCommand{Op::Unisolate});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "No layers were isolated by LAYISO."; }));

    engine.submit(LayerToolCommand{Op::Off, {5, 20}, 0.5, 9});
    engine.submit(LayerToolCommand{Op::Off, {5, 0}, 0.5, 9});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.layers[0].on && !s.layers[2].on; }));
    engine.submit(LayerToolCommand{Op::AllOn});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.layers[0].on && s.layers[1].on && s.layers[2].on && s.status == "All layers have been turned on.";
    }));
    engine.stop();
}

TEST_CASE("LAYOFF takes one object after another until Enter, with Undo; LAYLCK takes one") {
    H h;
    h.proc.submit_line("LAYOFF");
    REQUIRE(h.out.prompt == "Select an object on the layer to be turned off: ");
    h.proc.submit_line("U");
    REQUIRE(h.out.lines.back() == "Nothing to undo.");
    h.proc.submit_line("1,1");
    REQUIRE(h.out.prompt == "Select an object on the layer to be turned off or [Undo]: ");
    h.proc.submit_line("2,2");
    h.proc.submit_line("U");
    auto tools = h.tools();
    REQUIRE(tools.size() == 3);
    REQUIRE(tools[0].op == Op::Off);
    REQUIRE(tools[2].op == Op::UndoLast);
    h.proc.submit_line("");
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.submit_line("LAYLCK");
    REQUIRE(h.out.prompt == "Select an object on the layer to be locked: ");
    h.proc.submit_line("3,3");
    REQUIRE(h.tools().back().op == Op::Lock);
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("LAYISO and LAYCUR ask for objects, or take those selected beforehand; LAYON and LAYTHW act at once") {
    H h;
    h.proc.submit_line("LAYISO");
    REQUIRE(h.out.prompt == "Select objects on the layer(s) to be isolated: ");
    REQUIRE(h.proc.in_selection_phase());
    h.proc.set_selection_count(1);
    h.proc.submit_line("");
    REQUIRE(h.tools().back().op == Op::Isolate);
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.submit_line("LAYCUR"); // one selected already
    REQUIRE(h.tools().back().op == Op::ToCurrent);
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.submit_line("LAYON");
    REQUIRE(h.tools().back().op == Op::AllOn);
    h.proc.submit_line("LAYTHW");
    REQUIRE(h.tools().back().op == Op::AllThaw);
    h.proc.submit_line("LAYUNISO");
    REQUIRE(h.tools().back().op == Op::Unisolate);
    REQUIRE_FALSE(h.proc.has_active_command());
}

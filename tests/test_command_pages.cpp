// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

// What the command pages of the wiki turned up (issues #89-#92): the defaults a prompt
// shows are what Enter does, the commands say what happened once, and the smaller fixes.

#include <chrono>
#include <cmath>
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
#include "musacad/core/properties_palette.hpp"
#include "musacad/core/properties_registry.hpp"

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

struct Out : CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
    [[nodiscard]] bool said(const std::string& s) const {
        for (const std::string& l : lines) {
            if (l.find(s) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
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

TEST_CASE("FILLET: Enter at the Trim mode keeps the mode shown") {
    H h;
    h.run({"F", "T", "N"}); // No trim
    h.run({"T"});
    REQUIRE(h.out.prompt == "Enter Trim mode option [Trim/No trim] <No trim>: ");
    h.run({""}); // Enter keeps it
    h.run({"T"});
    REQUIRE(h.out.prompt == "Enter Trim mode option [Trim/No trim] <No trim>: ");
    h.run({"T"}); // back to Trim for the other tests
    h.proc.cancel();
}

TEST_CASE("POLYGON shows the last choice, Inscribed or Circumscribed") {
    H h;
    h.run({"POL", "6", "0,0", "I", "5"}); // Inscribed, whatever was last
    h.run({"POL", "6", "0,0"});
    REQUIRE(h.out.prompt == "Enter an option [Inscribed in circle/Circumscribed about circle] <I>: ");
    h.run({"C", "5"});
    h.run({"POL", "6", "0,0"});
    REQUIRE(h.out.prompt == "Enter an option [Inscribed in circle/Circumscribed about circle] <C>: ");
    h.run({"I", "5"}); // back to Inscribed
}

TEST_CASE("MLEADER: the leader type shown is the current one, and 0 maximum points is no limit") {
    H h;
    h.run({"MLD", "O", "L"});
    REQUIRE(h.out.prompt == "Select a leader type [Straight/sPline/None] <Straight>: ");
    h.run({"P", "L"});
    REQUIRE(h.out.prompt == "Select a leader type [Straight/sPline/None] <sPline>: ");
    h.run({"S", "M"});
    REQUIRE(h.out.prompt == "Enter the maximum points for leader line (0 for no limit) <2>: ");
    h.run({"0", "X", "0,0", "5,5", "10,5"});
    REQUIRE(h.out.prompt == "Specify next point: "); // no limit: points until Enter
    h.run({""});
    REQUIRE(h.out.prompt == "Enter annotation text: ");
    h.proc.cancel();
}

TEST_CASE("DIMLINEAR: Enter at a second Text keeps the text shown") {
    H h;
    h.run({"DLI", "0,0", "10,0", "T", "ABC", "T"});
    REQUIRE(h.out.prompt == "Enter dimension text <ABC>: ");
    h.run({"", "5,5"});
    const auto* d = h.last<AddDimensionCommand>();
    REQUIRE(d != nullptr);
    REQUIRE(d->text_override == "ABC");
}

TEST_CASE("ARRAY: the angle to fill is an amount in degrees, a full circle 360") {
    H h;
    h.proc.set_selection_count(1);
    h.run({"AR", "PO", "0,0", "F"});
    REQUIRE(h.out.prompt == "Specify the angle to fill (+=ccw, -=cw) or [EXpression] <360>: ");
    h.proc.cancel();
}

TEST_CASE("STRETCH's displacement is the last one, in the drawing's units") {
    H h;
    h.proc.set_selection_count(1);
    h.run({"STRETCH", "D", "5,0"});
    REQUIRE(h.last<StretchSelectionCommand>()->delta.x == Approx(5.0));
    h.cmds.clear();
    h.run({"STRETCH", "D"});
    REQUIRE(h.out.prompt == "Specify displacement <5.0000, 0.0000, 0.0000>: ");
    h.run({""});
    const auto* s = h.last<StretchSelectionCommand>();
    REQUIRE(s != nullptr);
    REQUIRE(s->delta.x == Approx(5.0));
}

TEST_CASE("ROTATE: a typed angle goes the way ANGDIR says") {
    H h;
    DrawingUnits u;
    u.clockwise = true;
    h.proc.set_units(u);
    h.proc.set_selection_count(1);
    h.run({"RO", "0,0", "90"});
    const auto* r = h.last<RotateSelectionCommand>();
    REQUIRE(r != nullptr);
    REQUIRE(r->angle == Approx(-kPi / 2.0)); // clockwise
}

TEST_CASE("PLINE closes from the first segment, where Close is offered") {
    // One straight segment: the closing segment runs back over it.
    H h;
    h.run({"PL", "0,0", "10,0"});
    REQUIRE(h.out.prompt == "Specify next point or [Arc/Close/Halfwidth/Length/Undo/Width]: ");
    h.run({"C"});
    const auto* s = h.last<AddPolylineCommand>();
    REQUIRE(s != nullptr);
    REQUIRE(s->closed);
    REQUIRE(s->points.size() == 2);
    // An arc first: a straight segment back closes it into a shape.
    H a;
    a.run({"PL", "0,0", "A", "10,10", "L"}); // a quarter circle (from the east), then Line
    REQUIRE(a.out.prompt == "Specify next point or [Arc/Close/Halfwidth/Length/Undo/Width]: ");
    a.run({"C"});
    const auto* p = a.last<AddPolylineCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->closed);
    REQUIRE(p->points.size() == 2);
    REQUIRE(p->bulges.size() == 2);
}

TEST_CASE("HATCH lets go of a selection of hatches; HATCHEDIT takes the one selected") {
    H h;
    h.proc.set_selection_count(1);
    h.proc.set_selection_kinds(bit(EntityKind::Hatch));
    h.run({"H"});
    REQUIRE(h.last<ClearSelectionCommand>() != nullptr);
    REQUIRE(h.last<HatchFromSelectionCommand>() == nullptr);
    REQUIRE(h.out.prompt.rfind("Pick internal point", 0) == 0);
    h.proc.cancel();
    H e;
    e.proc.set_selection_count(1);
    e.proc.set_selection_kinds(bit(EntityKind::Hatch));
    e.run({"HATCHEDIT"});
    REQUIRE(e.out.prompt.rfind("Enter hatch option [", 0) == 0);
    e.proc.cancel();
}

TEST_CASE("TEXTEDIT leaves saying what happened to the engine") {
    H h;
    h.run({"TEXTEDIT", "5,5", "NEW"});
    REQUIRE(h.last<EditTextContentCommand>() != nullptr);
    REQUIRE_FALSE(h.out.said("Text edited."));
    h.proc.cancel();
}

TEST_CASE("CHAMMODE and OFFSETDIST are variables") {
    H h;
    h.run({"CHAMMODE"});
    REQUIRE(h.out.prompt == "Enter new value for CHAMMODE <0>: ");
    h.run({"1"});
    REQUIRE(musacad::command::ChamferCommand::chammode());
    h.run({"CHAMMODE", "0"});
    REQUIRE_FALSE(musacad::command::ChamferCommand::chammode());
    const double before = musacad::command::OffsetCommand::offsetdist();
    h.run({"OFFSETDIST", "7"});
    REQUIRE(musacad::command::OffsetCommand::offsetdist() == Approx(7.0));
    h.run({"OFFSETDIST"});
    REQUIRE(h.out.prompt == "Enter new value for OFFSETDIST <7.0000>: ");
    h.run({"-1"});
    REQUIRE(musacad::command::OffsetCommand::offsetdist() == Approx(0.0)); // Through
    musacad::command::OffsetCommand::offsetdist() = before;
}

TEST_CASE("DIMZIN keeps the feet-and-inches bits") {
    DimStyle s;
    REQUIRE(apply_dim_var(s, "DIMZIN", 3));
    REQUIRE(s.zero_suppression == 3);
    REQUIRE(apply_dim_var(s, "DIMZIN", 15));
    REQUIRE(s.zero_suppression == 15);
}

TEST_CASE("MATCHPROP copies no dimension's text override, prefix or tolerances") {
    AddDimensionCommand src;
    src.text_override = "SPECIAL";
    AddDimensionCommand dst;
    Command source = src;
    Command target = dst;
    (void)match_properties(source, target, MatchPropFilter{});
    REQUIRE(std::get<AddDimensionCommand>(target).text_override.empty());
}

TEST_CASE("Block names are found whatever their case") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(DefineBlockCommand{"Box", {0, 0}, 2});
    InsertBlockCommand ins;
    ins.name = "BOX";
    ins.pos = {50, 50};
    ins.group = 3;
    engine.submit(ins);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Inserted \"BOX\"."; }));
    engine.stop();
}

TEST_CASE("A layer cannot take another's name, and the current layer cannot be frozen") {
    GeometryEngine engine;
    engine.start();
    Layer a;
    a.name = "Walls";
    engine.submit(AddLayerCommand{a});
    Layer b;
    b.name = "Doors";
    engine.submit(AddLayerCommand{b});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layers.size() == 3; }));
    Layer renamed = engine.snapshot().layers[2];
    renamed.name = "WALLS";
    engine.submit(SetLayerCommand{2, renamed});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "A layer named \"WALLS\" already exists."; }));
    REQUIRE(engine.snapshot().layers[2].name == "Doors");
    Layer zero = engine.snapshot().layers[0]; // 0 is current
    zero.frozen = true;
    engine.submit(SetLayerCommand{0, zero});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Cannot freeze the current layer."; }));
    REQUIRE_FALSE(engine.snapshot().layers[0].frozen);
    engine.stop();
}

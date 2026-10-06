// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// LENGTHEN and DIVIDE / MEASURE, the rest of #52: a pick reports the length, DYnamic, Angle
// for arcs, one object after another with Undo, polylines and elliptical arcs; [Block] for
// DIVIDE and MEASURE, and MEASURE from the end nearer the pick.

#include <chrono>
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
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("Saved", 0) == 0; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    std::filesystem::remove(p);
    return doc;
}

LengthenCommand len(Vec2 pick, LengthenCommand::Mode mode, double value, std::uint64_t group) {
    LengthenCommand c;
    c.pick = pick;
    c.pick_radius = 0.5;
    c.mode = mode;
    c.value = value;
    c.group = group;
    return c;
}

struct Out : CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
};
} // namespace

TEST_CASE("LENGTHEN: a pick reports the length (and an arc's angle); DYnamic; Angle; polylines and elliptical arcs") {
    using Mode = LengthenCommand::Mode;
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddArcCommand{{0, 50}, 10.0, 0.0, kHalfPi, 2});
    AddPolylineCommand pl;
    pl.points = {{0, 100}, {10, 100}, {10, 110}};
    pl.group = 3;
    engine.submit(pl);
    engine.submit(AddEllipseCommand{{0, 200}, {10, 0}, 0.5, 0.0, kHalfPi, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 10; }));

    engine.submit(len({5, 0}, Mode::Measure, 0.0, 0));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Current length: 10.0000"; }));
    engine.submit(len({7.07, 57.07}, Mode::Measure, 0.0, 0));
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.rfind("Current length: 15.7080, included angle: ", 0) == 0;
    }));

    LengthenCommand dyn = len({9, 0}, Mode::Dynamic, 0.0, 10);
    dyn.to = {25, 3};
    engine.submit(dyn);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Length changed from 10.0000 to 25.0000."; }));
    engine.submit(len({0.5, 59.9}, Mode::TotalAngle, kPi, 11)); // the arc's (0, 60) end
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("to 31.4159") != std::string::npos; }));
    engine.submit(len({10, 109}, Mode::Total, 30.0, 12)); // 20 long: the end segment takes 10 more
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Length changed from 20.0000 to 30.0000."; }));
    engine.submit(len({0.3, 205}, Mode::Delta, 2.0, 13));
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("Length changed from 12.11", 0) == 0; })); // a quarter of the 10 x 5 ellipse

    const io::Document doc = dump(engine, "musacad_lengthen_more.musa");
    bool line25 = false;
    for (const auto& l : doc.lines) {
        line25 = line25 || (std::abs(l.b.x - 25.0) < 1e-9 && std::abs(l.b.y) < 1e-9);
    }
    REQUIRE(line25);
    REQUIRE(doc.arcs.size() == 1);
    REQUIRE(doc.arcs[0].end_angle - doc.arcs[0].start_angle == Approx(kPi));
    REQUIRE(doc.polylines.size() == 1);
    REQUIRE(doc.polylines[0].points.back().y == Approx(120.0));
    REQUIRE(doc.ellipses.size() == 1);
    REQUIRE(doc.ellipses[0].end > kHalfPi); // longer by 2
    engine.stop();
}

TEST_CASE("LENGTHEN's prompts: measure, DYnamic, Total Angle, one after another with Undo") {
    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[&](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    proc.submit_line("LENGTHEN");
    REQUIRE(out.prompt.rfind("Select an object to measure or [DElta/Percent/Total/DYnamic] <", 0) == 0);
    proc.submit_line("5,0");
    REQUIRE(std::get<LengthenCommand>(cmds.back()).mode == LengthenCommand::Mode::Measure);
    proc.submit_line("DY");
    REQUIRE(out.prompt == "Select an object to change: ");
    proc.submit_line("9,0");
    REQUIRE(out.prompt == "Specify new end point: ");
    proc.submit_line("20,0");
    const auto& d = std::get<LengthenCommand>(cmds.back());
    REQUIRE(d.mode == LengthenCommand::Mode::Dynamic);
    REQUIRE(d.to == Vec2{20, 0});
    REQUIRE(out.prompt == "Select an object to change or [Undo]: ");
    proc.submit_line("U");
    REQUIRE(std::holds_alternative<UndoLastGroupCommand>(cmds.back()));
    proc.submit_line("");
    REQUIRE_FALSE(proc.has_active_command());

    proc.submit_line("LEN");
    proc.submit_line("T");
    REQUIRE(out.prompt.rfind("Specify total length or [Angle] <", 0) == 0);
    proc.submit_line("A");
    REQUIRE(out.prompt.rfind("Specify total angle <", 0) == 0);
    proc.submit_line("180");
    proc.submit_line("5,0");
    const auto& t = std::get<LengthenCommand>(cmds.back());
    REQUIRE(t.mode == LengthenCommand::Mode::TotalAngle);
    REQUIRE(t.value == Approx(kPi));
    proc.cancel();
}

TEST_CASE("DIVIDE and MEASURE [Block]: block references at the marks, turned to the curve; MEASURE from the nearer end") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {1, 0}, 1});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(DefineBlockCommand{"MARK", {0, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.block_names.size() == 1; }));
    engine.submit(AddLineCommand{{0, 0}, {0, 30}, 3}); // up the y axis
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    DividePathCommand dv;
    dv.pick = {0, 15};
    dv.pick_radius = 0.5;
    dv.segments = 3;
    dv.group = 4;
    dv.block = "mark";
    engine.submit(dv);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Divided: 2 blocks placed."; }));

    DividePathCommand me;
    me.pick = {0, 29}; // near the top end: measured from there
    me.pick_radius = 0.5;
    me.distance = 12.0;
    me.group = 5;
    engine.submit(me);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Measured: 2 points placed."; }));

    dv.block = "NOPE";
    dv.group = 6;
    engine.submit(dv);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Block \"NOPE\" not found."; }));

    const io::Document doc = dump(engine, "musacad_divide_block.musa");
    REQUIRE(doc.inserts.size() == 3); // BLOCK's own reference, and the two marks
    int along = 0;
    for (const auto& in : doc.inserts) {
        along += std::abs(in.rotation - kHalfPi) < 1e-9 ? 1 : 0; // turned to the line
    }
    REQUIRE(along == 2);
    REQUIRE(doc.points.size() == 2);
    bool from_top = false;
    for (const auto& p : doc.points) {
        from_top = from_top || std::abs(p.p.y - 18.0) < 1e-6; // 30 - 12
    }
    REQUIRE(from_top);
    engine.stop();

    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[&](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    proc.submit_line("DIVIDE");
    proc.submit_line("0,15");
    REQUIRE(out.prompt == "Enter the number of segments or [Block]: ");
    proc.submit_line("B");
    REQUIRE(out.prompt == "Enter name of block to insert: ");
    proc.submit_line("MARK");
    REQUIRE(out.prompt == "Align block with object? [Yes/No] <Y>: ");
    proc.submit_line("N");
    REQUIRE(out.prompt == "Enter the number of segments: ");
    proc.submit_line("4");
    const auto& c = std::get<DividePathCommand>(cmds.back());
    REQUIRE(c.block == "MARK");
    REQUIRE_FALSE(c.align);
    REQUIRE(c.segments == 4);
}

TEST_CASE("JOIN: lines along one line into one line, arcs on one circle into one arc or a circle, cLose") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{20, 0}, {30, 0}, 2}); // a gap between
    engine.submit(AddArcCommand{{0, 50}, 10.0, 0.0, kHalfPi, 3});
    engine.submit(AddArcCommand{{0, 50}, 10.0, kPi, 1.5 * kPi, 4});
    engine.submit(AddArcCommand{{0, 100}, 5.0, 0.0, kPi, 5});
    engine.submit(AddArcCommand{{0, 100}, 5.0, kPi, 2.0 * kPi, 6});
    engine.submit(AddArcCommand{{0, 150}, 5.0, 0.0, kHalfPi, 7});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 10; }));

    engine.submit(JoinPickCommand{{{5, 0}, {25, 0}}, 0.5, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Joined 2 lines into one line."; }));
    engine.submit(JoinPickCommand{{{7.07, 57.07}, {-7.07, 42.93}}, 0.5, 11});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Joined 2 arcs into one arc."; }));
    engine.submit(JoinPickCommand{{{0, 105}, {0, 95}}, 0.5, 12});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Joined 2 arcs into a circle."; }));
    JoinPickCommand close{{{3.54, 153.54}}, 0.5, 13};
    close.close = true;
    engine.submit(close);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Arc converted to a circle."; }));

    const io::Document doc = dump(engine, "musacad_join_kinds.musa");
    REQUIRE(doc.lines.size() == 1);
    REQUIRE(doc.lines[0].a == Vec2{0, 0});
    REQUIRE(doc.lines[0].b == Vec2{30, 0});
    REQUIRE(doc.arcs.size() == 1);
    REQUIRE(doc.arcs[0].end_angle - doc.arcs[0].start_angle == Approx(1.5 * kPi)); // from 0 round to 270
    REQUIRE(doc.circles.size() == 2);
    REQUIRE(doc.polylines.empty());
    engine.stop();

    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[&](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    proc.submit_line("JOIN");
    REQUIRE(out.prompt == "Select source object or multiple objects to join at once: ");
    proc.set_hovered_kind(EntityKind::Arc);
    proc.pick_point({7.07, 7.07}, std::nullopt);
    REQUIRE(out.prompt == "Select arcs to join to source or [cLose]: ");
    REQUIRE(proc.in_selection_phase());
    proc.submit_line("L");
    REQUIRE(std::get<JoinPickCommand>(cmds.back()).close);
    REQUIRE_FALSE(proc.has_active_command());

    proc.submit_line("J");
    proc.set_hovered_kind(EntityKind::Line);
    proc.pick_point({5, 0}, std::nullopt);
    REQUIRE(out.prompt == "Select objects to join: ");
    REQUIRE(std::holds_alternative<SelectPickCommand>(cmds.back())); // the source goes first
    proc.set_selection_count(2);
    proc.submit_line("");
    REQUIRE(std::holds_alternative<JoinSelectionCommand>(cmds.back()));
}

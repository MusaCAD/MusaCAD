// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Inquiry (issue #63): AREA by points with Arc / Undo and the Add / Subtract totals,
// DIST Multiple points and the full readout, ID with Z, LIST's Select objects and its
// block, MEASUREGEOM, MASSPROP, TIME, STATUS, CAL and DWGPROPS.

#include <chrono>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/calc.hpp"
#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad::core;
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
bool settle(GeometryEngine& e, std::uint64_t& seen) {
    return wait_until(e, [&](const auto& s) {
        if (s.status_version != seen) {
            seen = s.status_version;
            return true;
        }
        return false;
    });
}
struct Out : musacad::command::CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
    [[nodiscard]] bool any_contains(const std::string& sub) const {
        for (const auto& l : lines) {
            if (l.find(sub) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};
struct H {
    std::vector<Command> cmds;
    Out out;
    musacad::command::CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
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

TEST_CASE("#63 AREA by points: a rectangle, Undo, an arc through a second point, and the Add / Subtract totals") {
    H h;
    h.proc.submit_line("AA");
    REQUIRE(h.out.prompt == "Specify first corner point or [Object/Add area/Subtract area] <Object>: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompt == "Specify next point or [Arc/Length/Undo]: ");
    h.proc.submit_line("10,0");
    h.proc.submit_line("10,5"); // a wrong point ...
    h.proc.submit_line("U");    // ... taken back
    h.proc.submit_line("10,10");
    REQUIRE(h.out.prompt == "Specify next point or [Arc/Length/Undo/Total] <Total>: ");
    h.proc.submit_line("0,10");
    h.proc.submit_line(""); // Total
    const auto* q = h.last<AreaQueryCommand>();
    REQUIRE(q != nullptr);
    REQUIRE(q->from_points);
    REQUIRE(q->points_area == Approx(100.0));
    REQUIRE(q->points_perimeter == Approx(40.0));
    REQUIRE(q->mode == 0);
    REQUIRE(!h.proc.has_active_command());

    // A semicircular bulge on top of a 10 x 10 square through a second point.
    h.proc.submit_line("AREA");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,0");
    h.proc.submit_line("10,10");
    h.proc.submit_line("A");
    REQUIRE(h.out.prompt == "Specify endpoint of arc or [Second pt/Line/Undo]: ");
    h.proc.submit_line("S");
    h.proc.submit_line("5,15"); // the top of the semicircle
    h.proc.submit_line("0,10");
    h.proc.submit_line("L"); // back to lines
    h.proc.submit_line("T");
    const auto* a = h.last<AreaQueryCommand>();
    REQUIRE(a->points_area == Approx(100.0 + 3.14159265 * 25.0 / 2.0).epsilon(0.01));
    REQUIRE(a->points_perimeter == Approx(30.0 + 3.14159265 * 5.0).epsilon(0.01));

    // Add mode: the first measurement resets the total, the next adds to it, eXit ends.
    h.proc.submit_line("AREA");
    h.proc.submit_line("A");
    REQUIRE(h.out.prompt == "Specify first corner point or [Object/Subtract area/eXit] <Object>: ");
    h.proc.submit_line("0,0");
    h.proc.submit_line("4,0");
    h.proc.submit_line("4,4");
    h.proc.submit_line("");
    const auto* add1 = h.last<AreaQueryCommand>();
    REQUIRE(add1->mode == 1);
    REQUIRE(add1->reset);
    REQUIRE(add1->points_area == Approx(8.0));
    REQUIRE(h.proc.has_active_command()); // Add mode keeps going
    h.proc.submit_line("O");             // an object, still adding
    h.proc.submit_line("50,50");
    const auto* add2 = h.last<AreaQueryCommand>();
    REQUIRE(!add2->from_points);
    REQUIRE(add2->mode == 1);
    REQUIRE(!add2->reset);
    h.proc.submit_line("S"); // switch to subtracting
    REQUIRE(h.out.prompt == "Specify first corner point or [Object/Add area/eXit] <Object>: ");
    h.proc.submit_line("X");
    REQUIRE(!h.proc.has_active_command());
}

TEST_CASE("#63 DIST: the full readout, Multiple points with Undo and Total; ID reports Z") {
    H h;
    h.proc.submit_line("DI");
    REQUIRE(h.out.prompt == "Specify first point or [Multiple points]: ");
    h.proc.submit_line("0,0");
    h.proc.submit_line("3,4");
    REQUIRE(h.out.any_contains("Distance = 5.0000,  Angle in XY Plane = 53,  Angle from XY Plane = 0"));
    REQUIRE(h.out.any_contains("Delta X = 3.0000,  Delta Y = 4.0000,  Delta Z = 0.0000"));

    h.proc.submit_line("DIST");
    h.proc.submit_line("M");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompt == "Specify next point or [Length/Undo/Total] <Total>: ");
    h.proc.submit_line("10,0");
    h.proc.submit_line("10,10");
    h.proc.submit_line("10,30"); // ...
    h.proc.submit_line("U");     // taken back
    h.proc.submit_line("T");
    REQUIRE(h.out.lines.back() == "Distance = 20.0000");
    REQUIRE(!h.proc.has_active_command());

    h.proc.submit_line("ID");
    h.proc.submit_line("1.5,2");
    REQUIRE(h.out.lines.back() == "X = 1.5000     Y = 2.0000     Z = 0.0000");
}

TEST_CASE("#63 LIST gathers a set; MEASUREGEOM's options; MASSPROP, TIME, STATUS and CAL through the processor") {
    H h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("LI");
    REQUIRE(h.proc.in_selection_phase());
    h.proc.submit_line("ALL");
    h.proc.set_selection_count(3);
    h.proc.submit_line("");
    REQUIRE(h.last<ListQueryCommand>() != nullptr);
    REQUIRE(h.last<ListQueryCommand>()->selection);

    h.proc.submit_line("MEA");
    REQUIRE(h.out.prompt == "Enter an option [Distance/Radius/Angle/ARea/Volume/Quick/Mode] <Distance>: ");
    h.proc.submit_line("R");
    h.proc.submit_line("5,5");
    REQUIRE(h.last<MeasureQueryCommand>()->what == 0);
    REQUIRE(h.out.prompt == "Enter an option [Distance/Radius/Angle/ARea/Volume/Quick/Mode/eXit] <eXit>: ");
    h.proc.submit_line("A");
    h.proc.submit_line("1,1");
    REQUIRE(h.out.prompt == "Select second line: "); // no arc under the cursor here
    h.proc.submit_line("2,2");
    REQUIRE(h.last<MeasureQueryCommand>()->what == 1);
    REQUIRE(h.last<MeasureQueryCommand>()->at2 == Vec2{2, 2});
    h.proc.submit_line("D");
    h.proc.submit_line("0,0");
    h.proc.submit_line("0,7");
    REQUIRE(h.out.any_contains("Distance = 7.0000"));
    h.proc.submit_line("V");
    h.proc.submit_line("0,0");
    h.proc.submit_line("2,0");
    h.proc.submit_line("2,3");
    h.proc.submit_line("");
    REQUIRE(h.out.prompt == "Specify height: ");
    h.proc.submit_line("10");
    REQUIRE(h.last<AreaQueryCommand>()->height == Approx(10.0));
    REQUIRE(h.last<AreaQueryCommand>()->points_area == Approx(3.0));
    h.proc.submit_line("Q");
    REQUIRE(h.out.prompt.rfind("Click an object to measure", 0) == 0);
    h.proc.submit_line("4,4");
    REQUIRE(h.last<MeasureQueryCommand>()->what == 2);
    h.proc.submit_line("");
    REQUIRE(!h.proc.has_active_command());

    h.proc.set_selection_count(1);
    h.proc.submit_line("MASSPROP");
    REQUIRE(h.last<MassPropQueryCommand>() != nullptr);
    h.proc.submit_line("TIME");
    REQUIRE(h.last<TimeCommand>()->op == 0);
    h.proc.submit_line("OFF");
    REQUIRE(h.last<TimeCommand>()->op == 2);
    h.proc.submit_line("R");
    REQUIRE(h.last<TimeCommand>()->op == 3);
    h.proc.submit_line("");
    h.proc.submit_line("STATUS");
    REQUIRE(h.last<StatusQueryCommand>() != nullptr);
    REQUIRE(h.last<StatusQueryCommand>()->modes.find("Ortho off") != std::string::npos);
    h.proc.submit_line("CAL");
    REQUIRE(h.out.prompt == ">> Expression: ");
    h.proc.submit_line("2*(3+4)");
    REQUIRE(h.out.lines.back() == "14");
    h.proc.submit_line("CAL");
    h.proc.submit_line("foo(");
    REQUIRE(h.out.lines.back().rfind("CAL: ", 0) == 0);
    REQUIRE(h.proc.has_active_command()); // asks again
    h.proc.cancel();
}

TEST_CASE("#63 CAL evaluates arithmetic, functions in degrees, points and unit conversion") {
    using musacad::command::calc::evaluate;
    using musacad::command::calc::format;
    std::string err;
    REQUIRE(evaluate("2+3*4", err)->x == Approx(14.0));
    REQUIRE(evaluate("(2+3)*4", err)->x == Approx(20.0));
    REQUIRE(evaluate("2^10", err)->x == Approx(1024.0));
    REQUIRE(evaluate("-3+5", err)->x == Approx(2.0));
    REQUIRE(evaluate("sin(30)", err)->x == Approx(0.5));
    REQUIRE(evaluate("atan(1)", err)->x == Approx(45.0));
    REQUIRE(evaluate("sqrt(2)*sqrt(2)", err)->x == Approx(2.0));
    REQUIRE(evaluate("dist([0,0],[3,4])", err)->x == Approx(5.0));
    REQUIRE(evaluate("ang([0,0],[0,1])", err)->x == Approx(90.0));
    const auto v = evaluate("[1,2]+[3,4]*2", err);
    REQUIRE(v->vector);
    REQUIRE(v->x == Approx(7.0));
    REQUIRE(v->y == Approx(10.0));
    REQUIRE(format(*v) == "[7,10]");
    REQUIRE(evaluate("cvunit(1,inch,mm)", err)->x == Approx(25.4));
    REQUIRE(evaluate("cvunit(1000, mm, m)", err)->x == Approx(1.0));
    REQUIRE(evaluate("abs([3,4])", err)->x == Approx(5.0));
    REQUIRE(evaluate("pi*2", err)->x == Approx(6.2831853));
    REQUIRE(format(*evaluate("1/3", err), 4) == "0.3333");
    REQUIRE(!evaluate("2+", err));
    REQUIRE(!err.empty());
    REQUIRE(!evaluate("1/0", err));
    REQUIRE(!evaluate("[1,2]+3", err));
    REQUIRE(!evaluate("nope(1)", err));
}

TEST_CASE("#63 engine: AREA's totals and open objects, LIST's block, MEASUREGEOM, MASSPROP, TIME, STATUS") {
    GeometryEngine e;
    e.start();
    e.submit(AddPolylineCommand{{{0, 0}, {10, 0}, {10, 20}, {0, 20}}, true, 1});
    e.submit(AddCircleCommand{{50, 50}, 10.0, 2});
    e.submit(AddLineCommand{{100, 0}, {100, 30}, 3});
    e.submit(AddArcCommand{{200, 0}, 10.0, 0.0, 3.14159265358979, 4});
    REQUIRE(wait_until(e, [](const auto& s) { return s.line_vertices.size() > 10; }));
    std::uint64_t seen = 0;

    AreaQueryCommand add;
    add.at = {5, 10};
    add.pick_radius = 20.0;
    add.mode = 1;
    add.reset = true;
    e.submit(add);
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status == "Area = 200.0000,  Perimeter = 60.0000\nTotal area = 200.0000");
    AreaQueryCommand pts;
    pts.from_points = true;
    pts.points_area = 50.0;
    pts.points_perimeter = 30.0;
    pts.mode = -1;
    e.submit(pts);
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status == "Area = 50.0000,  Perimeter = 30.0000\nTotal area = 150.0000");
    AreaQueryCommand line;
    line.at = {100, 15};
    line.pick_radius = 2.0;
    e.submit(line); // an open object: area as if closed (none), its length
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status == "Area = 0.0000,  Length = 30.0000");
    AreaQueryCommand vol;
    vol.at = {50, 60};
    vol.pick_radius = 2.0;
    vol.height = 2.0;
    e.submit(vol);
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status.find("Circumference = 62.8319") != std::string::npos);
    REQUIRE(e.snapshot().status.find("Volume = 628.3185") != std::string::npos);

    e.submit(SelectWindowCommand{{-1, -1}, {11, 21}, false, false, true});
    REQUIRE(settle(e, seen));
    ListQueryCommand lq;
    lq.selection = true;
    e.submit(lq);
    REQUIRE(settle(e, seen));
    const std::string block = e.snapshot().status;
    REQUIRE(block.find("POLYLINE      Layer: \"0\"") != std::string::npos);
    REQUIRE(block.find("Space: Model space") != std::string::npos);
    REQUIRE(block.find("Handle = ") != std::string::npos);
    REQUIRE(block.find("Color: BYLAYER") != std::string::npos);
    REQUIRE(block.find("area 200.0000,  perimeter 60.0000") != std::string::npos);

    MeasureQueryCommand r;
    r.at = {50, 60};
    r.pick_radius = 2.0;
    r.what = 0;
    e.submit(r);
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status == "Radius = 10.0000\nDiameter = 20.0000");
    MeasureQueryCommand ang;
    ang.at = {100, 15};
    ang.at2 = {5, 0};
    ang.pick_radius = 2.0;
    ang.what = 1; // a line and a polyline: only lines pair up
    e.submit(ang);
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status == "Select a second line.");
    MeasureQueryCommand arc_ang;
    arc_ang.at = {210, 0};
    arc_ang.pick_radius = 2.0;
    arc_ang.what = 1;
    e.submit(arc_ang);
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status == "Angle = 180");
    MeasureQueryCommand quick;
    quick.at = {100, 15};
    quick.pick_radius = 2.0;
    quick.what = 2;
    e.submit(quick);
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status == "Length = 30.0000,  Angle = 90");

    e.submit(MassPropQueryCommand{}); // the 10 x 20 rectangle is selected
    REQUIRE(settle(e, seen));
    const std::string mp = e.snapshot().status;
    REQUIRE(mp.find("Area:                    200.0000") != std::string::npos);
    REQUIRE(mp.find("Centroid:             X: 5.0000") != std::string::npos);
    REQUIRE(mp.find("                      Y: 10.0000") != std::string::npos);
    REQUIRE(mp.find("Moments of inertia:   X: 26666.6667") != std::string::npos);
    REQUIRE(mp.find("                      Y: 6666.6667") != std::string::npos);
    REQUIRE(mp.find("Product of inertia:  XY: 10000.0000") != std::string::npos);

    e.submit(TimeCommand{0});
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status.find("Times for this drawing:") != std::string::npos);
    REQUIRE(e.snapshot().status.find("Elapsed timer (on)") != std::string::npos);
    e.submit(TimeCommand{2});
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status.find("Elapsed timer (off)") != std::string::npos);
    REQUIRE(e.snapshot().times.created > 0);

    e.submit(StatusQueryCommand{"Ortho off"});
    REQUIRE(settle(e, seen));
    const std::string st = e.snapshot().status;
    REQUIRE(st.find("4 objects in") != std::string::npos);
    REQUIRE(st.find("Ortho off") != std::string::npos);
    REQUIRE(st.find("Current layer:            \"0\"") != std::string::npos);
    REQUIRE(st.find("Drawing changed since last save: yes") != std::string::npos);
    e.stop();
}

TEST_CASE("#63 DWGPROPS and the drawing's times are published and saved with the drawing") {
    GeometryEngine e;
    e.start();
    DrawingProps p;
    p.title = "Bracket";
    p.author = "Musa";
    p.comments = "two words";
    p.custom.emplace_back("Project", "X 1");
    e.submit(SetDrawingPropsCommand{p});
    REQUIRE(wait_until(e, [&](const auto& s) { return s.drawing_props == p; }));
    e.stop();

    io::Document doc;
    doc.props = p;
    doc.times.created = 1700000000;
    doc.times.updated = 1700003600;
    doc.times.edit_seconds = 12.5;
    const std::string text = io::serialize_native(doc);
    REQUIRE(text.find("DWGPROP title Bracket") != std::string::npos);
    REQUIRE(text.find("DWGPROP comments two\x1fwords") != std::string::npos);
    REQUIRE(text.find("DWGPROP c:Project X\x1f") != std::string::npos);
    REQUIRE(text.find("TIMES 1700000000 1700003600 12.5") != std::string::npos);
    io::Document back;
    REQUIRE(io::parse_native(text, back).ok);
    REQUIRE(back.props == p);
    REQUIRE(back.times == doc.times);
    REQUIRE(back.format_version == io::kFormatVersion);
}

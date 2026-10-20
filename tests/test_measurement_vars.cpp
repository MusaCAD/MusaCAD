// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// MEASUREMENT and MEASUREINIT (acad.pat / acadiso.pat, acad.lin / acadiso.lin), a drawing's own
// Dashed, Center and Hidden, HPSEPARATE and HATCH's picks after an Undo, Dynamic Input's pointer
// input (DYNPICOORDS), FILLETRAD / CHAMFERA-D / TRIMMODE / PEDITACCEPT, QDIM's datumPoint, Edit
// and seTtings with the ordinate origin, JOIN of splines and of ends that meet, PEDIT's
// Jointype (#41, #68, #81, #65, #49, #60, #52).

#include <algorithm>
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
#include "musacad/command/coordinate.hpp"
#include "musacad/command/dyn_fields.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/hatch_pattern.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/linetype.hpp"

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

void square(GeometryEngine& engine, Vec2 lo, double side, std::uint64_t group) {
    engine.submit(AddPolylineCommand{{lo, lo + Vec2{side, 0}, lo + Vec2{side, side}, lo + Vec2{0, side}}, true, group});
}
} // namespace

TEST_CASE("The linetype library as acad.lin and acadiso.lin have it, and the built-in three a drawing keeps") {
    const std::vector<LinetypeDef>& inch = linetype_library(false);
    const std::vector<LinetypeDef>& mm = linetype_library(true);
    REQUIRE(inch.size() == 38);
    REQUIRE(mm.size() == 38);
    const auto find = [](const std::vector<LinetypeDef>& lib, const char* name) {
        for (const LinetypeDef& d : lib) {
            if (d.name == name) {
                return d.pattern;
            }
        }
        return std::vector<double>{};
    };
    REQUIRE(find(inch, "DASHED") == std::vector<double>{0.5, -0.25});
    REQUIRE(find(mm, "DASHED") == std::vector<double>{0.5 * 25.4, -0.25 * 25.4});
    REQUIRE(find(mm, "CENTER2") == std::vector<double>{0.75 * 25.4, -0.125 * 25.4, 0.125 * 25.4, -0.125 * 25.4});
    REQUIRE(find(inch, "ACAD_ISO02W100") == std::vector<double>{12.0, -3.0}); // millimetres in both
    REQUIRE(find(mm, "ACAD_ISO02W100") == std::vector<double>{12.0, -3.0});

    REQUIRE(standard_builtin_linetypes(true)[0] == std::vector<double>{12.7, -6.35});
    REQUIRE(standard_builtin_linetypes(false)[2] == std::vector<double>{0.25, -0.125});
    REQUIRE(legacy_builtin_linetypes()[0] == std::vector<double>{5.0, -2.5});

    // Dashed drawn by the drawing's own definition.
    const std::vector<Vec2> line{{0, 0}, {100, 0}};
    std::vector<Vec2> out;
    dash_polyline(line, Linetype::Dashed, standard_builtin_linetypes(true), {}, 1.0, out);
    REQUIRE(out.size() >= 4);
    REQUIRE(out[1].x == Approx(12.7));
    REQUIRE(out[2].x == Approx(19.05)); // after the 6.35 gap

    // A store starts with the former definitions; loading DASHED redefines its Dashed.
    GeometryStore store;
    REQUIRE(store.builtin_linetypes() == legacy_builtin_linetypes());
    REQUIRE(store.add_linetype(LinetypeDef{"DASHED", "", {1.0, -1.0}}) == Linetype::Dashed);
    REQUIRE(store.builtin_linetypes()[0] == std::vector<double>{1.0, -1.0});
    REQUIRE(store.linetypes().empty());
}

TEST_CASE("MEASUREMENT 1: a new hatch's standard pattern is acadiso.pat's size, kept through files") {
    GeometryEngine engine;
    engine.start();
    square(engine, {0, 0}, 10, 1);
    DrawingUnits u;
    u.measurement = 1;
    engine.submit(SetUnitsCommand{u});
    engine.submit(HatchPickPointCommand{{5, 5}, "ANSI31", 1.0, 0.0, 2, {}, {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Hatch created."; }));
    io::Document doc = dump(engine, "musacad_measure1.musa");
    REQUIRE(doc.display_units.measurement == 1);
    REQUIRE(doc.hatches.size() == 1);
    REQUIRE(doc.hatches[0].iso);
    engine.stop();

    GeometryStore store;
    io::populate_store(store, doc);
    const HatchData* hd = nullptr;
    for (std::uint32_t i = 0; i < store.hatches().slot_count(); ++i) {
        if (store.hatches().alive(i)) {
            hd = &store.hatches().data()[i];
        }
    }
    REQUIRE(hd != nullptr);
    REQUIRE(store.hatch_pattern_scale(*hd) == Approx(25.4));

    // DXF: written with its line families at that size, and read back as such.
    const std::string dxf = io::serialize_dxf(doc);
    REQUIRE(dxf.find("$MEASUREMENT") != std::string::npos);
    io::Document back;
    REQUIRE(io::parse_dxf(dxf, back).ok);
    REQUIRE(back.display_units.measurement == 1);
    REQUIRE(back.hatches.size() == 1);
    REQUIRE(back.hatches[0].iso);
    doc.hatches[0].iso = false; // the same hatch at acad.pat's size
    io::Document inch;
    REQUIRE(io::parse_dxf(io::serialize_dxf(doc), inch).ok);
    REQUIRE_FALSE(inch.hatches[0].iso);

    // Native: MEASUREMENT, the built-in linetypes and the hatch's size.
    io::Document nd = back;
    nd.builtin_linetypes = standard_builtin_linetypes(true);
    io::Document nd2;
    REQUIRE(io::parse_native(io::serialize_native(nd), nd2).ok);
    REQUIRE(nd2.display_units.measurement == 1);
    REQUIRE(nd2.builtin_linetypes == standard_builtin_linetypes(true));
    REQUIRE(nd2.hatches[0].iso);
}

TEST_CASE("MEASUREINIT starts a new drawing; a DXF's Dashed is the drawing's own") {
    GeometryEngine engine;
    engine.start();
    DimAssocCommand init;
    init.op = DimAssocCommand::Op::MeasureInitNow;
    init.value = 1;
    engine.submit(init);
    const io::Document fresh = dump(engine, "musacad_measureinit.musa");
    REQUIRE(fresh.display_units.measurement == 1);
    REQUIRE(fresh.display_units.insunits == 4); // millimetres
    REQUIRE(fresh.builtin_linetypes == standard_builtin_linetypes(true));
    engine.stop();

    // A drawing already drawn in keeps what it has.
    GeometryEngine drawn;
    drawn.start();
    drawn.submit(AddLineCommand{{0, 0}, {1, 0}, 1});
    drawn.submit(init);
    const io::Document kept = dump(drawn, "musacad_measureinit2.musa");
    REQUIRE(kept.display_units.measurement == 0);
    REQUIRE(kept.builtin_linetypes == legacy_builtin_linetypes());
    drawn.stop();

    // A DXF with neither $MEASUREMENT nor the linetypes: acad.lin's. With $MEASUREMENT 1: acadiso.lin's.
    io::Document plain;
    REQUIRE(io::parse_dxf("0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n", plain).ok);
    REQUIRE(plain.builtin_linetypes == standard_builtin_linetypes(false));
    io::Document metric;
    REQUIRE(io::parse_dxf("0\nSECTION\n2\nHEADER\n9\n$MEASUREMENT\n70\n1\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n",
                          metric)
                .ok);
    REQUIRE(metric.display_units.measurement == 1);
    REQUIRE(metric.builtin_linetypes == standard_builtin_linetypes(true));
    // A drawing saved before: Musa CAD's former Dashed, written out, comes back the same.
    io::Document old;
    REQUIRE(io::parse_dxf(io::serialize_dxf(io::Document{}), old).ok);
    REQUIRE(old.builtin_linetypes == legacy_builtin_linetypes());
}

TEST_CASE("HATCH: the next area picked after Undo joins the hatch Undo brought back; HPSEPARATE for selections") {
    GeometryEngine engine;
    engine.start();
    square(engine, {0, 0}, 10, 1);
    square(engine, {20, 0}, 10, 2);
    HatchExtras first;
    HatchExtras next;
    next.merge = true;
    engine.submit(HatchPickPointCommand{{5, 5}, "ANSI31", 1.0, 0.0, 10, {}, first});
    engine.submit(HatchPickPointCommand{{25, 5}, "ANSI31", 1.0, 0.0, 11, {}, next});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Hatch created."; }));
    engine.submit(UndoLastGroupCommand{});
    engine.submit(HatchPickPointCommand{{25, 5}, "ANSI31", 1.0, 0.0, 12, {}, next});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Hatch created."; }));
    io::Document doc = dump(engine, "musacad_hatch_undo.musa");
    REQUIRE(doc.hatches.size() == 1);
    REQUIRE(doc.hatches[0].loops.size() == 2);

    // Select objects with HPSEPARATE 1: a hatch for each area.
    engine.submit(SelectAllCommand{});
    engine.submit(EraseSelectionCommand{});
    square(engine, {0, 0}, 10, 20);
    square(engine, {20, 0}, 10, 21);
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    HatchExtras separate;
    separate.separate = true;
    engine.submit(HatchFromSelectionCommand{"ANSI31", 1.0, 0.0, 22, {}, separate});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2 && s.status == "Hatch created."; }));
    doc = dump(engine, "musacad_hpseparate.musa");
    REQUIRE(doc.hatches.size() == 2);
    engine.stop();
}

TEST_CASE("Dynamic Input's pointer input: a next point relative, # absolute, @ relative; absolute polar") {
    using musacad::command::dyn_pointer_input;
    using musacad::command::dynpicoords;
    REQUIRE(dyn_pointer_input("10,5", true) == std::optional<std::string>("@10,5"));
    REQUIRE(dyn_pointer_input("10<45", true) == std::optional<std::string>("@10<45"));
    REQUIRE(dyn_pointer_input("#10,5", true) == std::optional<std::string>("10,5"));
    REQUIRE(dyn_pointer_input("@3,4", true) == std::optional<std::string>("@3,4"));
    REQUIRE(dyn_pointer_input("10,5", false) == std::optional<std::string>("10,5")); // a first point
    REQUIRE_FALSE(dyn_pointer_input("10", true));                                       // a distance
    REQUIRE_FALSE(dyn_pointer_input("C", true));
    dynpicoords() = 1;
    REQUIRE(dyn_pointer_input("10,5", true) == std::optional<std::string>("10,5"));
    dynpicoords() = 0;

    using musacad::command::parse_coordinate;
    const auto abs = parse_coordinate("#10,5", Vec2{3, 3});
    REQUIRE(abs.ok);
    REQUIRE(abs.point == Vec2{10, 5});
    const auto polar = parse_coordinate("10<90", std::nullopt);
    REQUIRE(polar.ok);
    REQUIRE(polar.point.x == Approx(0.0).margin(1e-12));
    REQUIRE(polar.point.y == Approx(10.0));
    const auto rel = parse_coordinate("@10<90", Vec2{1, 1});
    REQUIRE(rel.point.y == Approx(11.0));
}

TEST_CASE("MEASUREMENT, HPSEPARATE, TRIMMODE, FILLETRAD, CHAMFERA to D and PEDITACCEPT at the command line") {
    H h;
    h.run({"MEASUREMENT"});
    REQUIRE(h.out.prompt == "Enter new value for MEASUREMENT <0>: ");
    h.run({"2"});
    REQUIRE(h.out.lines.back() == "Requires an integer between 0 and 1.");
    h.run({"1"});
    REQUIRE(h.last<SetUnitsCommand>() != nullptr);
    REQUIRE(h.last<SetUnitsCommand>()->units.measurement == 1);

    h.run({"MEASUREINIT", "0"});
    REQUIRE(musacad::command::measureinit() == 0);
    REQUIRE(h.last<DimAssocCommand>()->op == DimAssocCommand::Op::MeasureInit);
    h.run({"MEASUREINIT", "1"});

    h.run({"HPSEPARATE"});
    REQUIRE(h.out.prompt == "Enter new value for HPSEPARATE <0>: ");
    h.run({"1"});
    REQUIRE(musacad::command::hpseparate());
    h.run({"HPSEPARATE", "0"});

    h.run({"FILLETRAD"});
    REQUIRE(h.out.prompt == "Enter new value for FILLETRAD <0.0000>: ");
    h.run({"5"});
    h.run({"TRIMMODE", "0"});
    h.run({"FILLET"});
    REQUIRE(h.out.lines.back() == "Current settings: Mode = NOTRIM, Radius = 5.0000");
    h.proc.cancel();
    h.run({"CHAMFER"});
    REQUIRE(h.out.lines.back().find("(NOTRIM mode)") != std::string::npos); // TRIMMODE is shared
    h.proc.cancel();
    h.run({"CHAMFERD"});
    REQUIRE(h.out.prompt == "Enter new value for CHAMFERD <0>: ");
    h.run({"30"});
    REQUIRE(musacad::command::ChamferCommand::chamferd() == Approx(30.0));
    h.run({"CHAMFERD", "0", "TRIMMODE", "1", "FILLETRAD", "0"});

    h.run({"PEDITACCEPT"});
    REQUIRE(h.out.prompt == "Enter new value for PEDITACCEPT <0>: ");
    h.proc.cancel();
    h.run({"SETVAR", "?"});
    REQUIRE(h.out.lines.back().find("MEASUREMENT, MIRRTEXT") != std::string::npos); // listed in order
}

TEST_CASE("-LINETYPE Load: the library at the drawing's MEASUREMENT, and DASHED reloaded") {
    H h;
    h.run({"-LINETYPE", "L", "DASHED", ""});
    REQUIRE(h.out.prompt == "Linetype DASHED is already loaded.  Reload it? <Y>: ");
    h.run({""});
    const auto* loaded = h.last<LoadLinetypesCommand>();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->defs.size() == 1);
    REQUIRE(loaded->defs[0].pattern == std::vector<double>{0.5, -0.25}); // MEASUREMENT 0: acad.lin
    h.run({"L", "HIDDEN2", "acadiso.lin"});
    REQUIRE(h.last<LoadLinetypesCommand>()->defs[0].pattern == std::vector<double>{0.125 * 25.4, -0.0625 * 25.4});
    h.proc.cancel();
}

TEST_CASE("QDIM datumPoint, Edit and seTtings; an ordinate measures from its origin") {
    H h;
    h.proc.set_selection_count(1);
    h.run({"QDIM"});
    REQUIRE(h.out.prompt ==
            "Specify dimension line position, or [Continuous/Staggered/Baseline/Ordinate/Radius/Diameter/datumPoint/Edit/"
            "seTtings] <Continuous>: ");
    h.run({"P"});
    REQUIRE(h.out.prompt == "Select new datum point: ");
    h.run({"0,0", "E"});
    REQUIRE(h.out.prompt == "Indicate dimension point to remove, or [Add/eXit] <eXit>: ");
    h.run({"10,0", "A"});
    REQUIRE(h.out.prompt == "Indicate dimension point to add, or [Remove/eXit] <eXit>: ");
    h.run({"5,5", "X", "T"});
    REQUIRE(h.out.prompt == "Associative dimension priority [Endpoint/Intersection] <Endpoint>: ");
    h.run({"I", "15,20"});
    const auto* q = h.last<QuickDimCommand>();
    REQUIRE(q != nullptr);
    REQUIRE(q->datum == std::optional<Vec2>(Vec2{0, 0}));
    REQUIRE(q->removed == std::vector<Vec2>{{10, 0}});
    REQUIRE(q->added == std::vector<Vec2>{{5, 5}});
    REQUIRE(q->intersection_priority);
    h.run({"QDIM", "T", "E"}); // back to Endpoint for the session
    h.proc.cancel();

    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{10, 0}, {10, 10}, 1});
    engine.submit(AddLineCommand{{20, 0}, {20, 10}, 2});
    engine.submit(AddLineCommand{{30, 0}, {30, 10}, 3});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 3; }));
    QuickDimCommand base;
    base.mode = QuickDimCommand::Mode::Baseline;
    base.at = {15, 20};
    base.datum = Vec2{0, 10};
    base.removed = {{20, 10}, {20, 0}};
    base.pick_radius = 0.5;
    base.group = 4;
    engine.submit(base);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "2 dimensions created."; }));
    io::Document doc = dump(engine, "musacad_qdim_datum.musa");
    REQUIRE(doc.dims.size() == 2); // to 10 and 30, both from the datum
    for (const io::DocDim& d : doc.dims) {
        REQUIRE(d.a == Vec2{0, 10});
    }

    engine.submit(SelectAllCommand{});
    QuickDimCommand ord;
    ord.mode = QuickDimCommand::Mode::Ordinate;
    ord.at = {15, 30};
    ord.datum = Vec2{5, 0};
    ord.group = 5;
    engine.submit(ord);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("dimensions created.") != std::string::npos; }));
    doc = dump(engine, "musacad_qdim_ord.musa");
    std::vector<double> values;
    for (const io::DocDim& d : doc.dims) {
        if (d.type == static_cast<std::uint8_t>(DimType::Ordinate)) {
            REQUIRE(d.origin == Vec2{5, 0});
            DimData dd;
            dd.type = DimType::Ordinate;
            dd.a = d.a;
            dd.b = d.b;
            dd.aux = d.aux;
            dd.origin = d.origin;
            values.push_back(dim_measure(dd));
        }
    }
    std::sort(values.begin(), values.end());
    REQUIRE(values == std::vector<double>{5.0, 15.0, 25.0});
    engine.stop();

    // DXF: an ordinate's 10 is its origin; one Musa CAD wrote before (10 at the leader's end)
    // measures from 0,0.
    io::Document od;
    io::DocDim o;
    o.type = static_cast<std::uint8_t>(DimType::Ordinate);
    o.a = {12, 3};
    o.b = {12, 20};
    o.line_pt = o.b;
    o.origin = {2, 0};
    od.dims.push_back(o);
    io::Document ob;
    REQUIRE(io::parse_dxf(io::serialize_dxf(od), ob).ok);
    REQUIRE(ob.dims.size() == 1);
    REQUIRE(ob.dims[0].origin == Vec2{2, 0});
    io::Document legacy; // X-type ordinate (70 = 6 + 64), 10 the same as 14
    REQUIRE(io::parse_dxf("0\nSECTION\n2\nENTITIES\n0\nDIMENSION\n8\n0\n70\n70\n10\n12\n20\n20\n"
                          "13\n12\n23\n3\n14\n12\n24\n20\n0\nENDSEC\n0\nEOF\n",
                          legacy)
                .ok);
    REQUIRE(legacy.dims.size() == 1);
    REQUIRE(legacy.dims[0].origin == Vec2{});
    REQUIRE(legacy.dims[0].line_pt == Vec2{12, 20});
    io::Document nat;
    od.dims[0].origin = {2, 0};
    REQUIRE(io::parse_native(io::serialize_native(od), nat).ok);
    REQUIRE(nat.dims[0].origin == Vec2{2, 0});
}

TEST_CASE("JOIN: splines end to end make one spline; only ends that meet chain; PEDIT's Jointype") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddSplineCommand{{{0, 0}, {1, 2}, {2, 2}, {3, 0}}, 3, 1});
    engine.submit(AddSplineCommand{{{6, 0}, {5, -2}, {4, -2}, {3, 0}}, 3, 2}); // runs back to the join
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(JoinSelectionCommand{0.0, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Joined 2 splines into one spline."; }));
    io::Document doc = dump(engine, "musacad_join_spline.musa");
    REQUIRE(doc.splines.size() == 1);
    REQUIRE(doc.splines[0].control_points.size() == 7);
    REQUIRE(doc.splines[0].control_points.front() == Vec2{0, 0});
    REQUIRE(doc.splines[0].control_points.back() == Vec2{6, 0});

    // Ends a hair apart do not chain into a polyline.
    engine.submit(SelectAllCommand{});
    engine.submit(EraseSelectionCommand{});
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 4});
    engine.submit(AddLineCommand{{10, 0.01}, {10, 10}, 5});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(JoinSelectionCommand{0.0, 6});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("JOIN") != std::string::npos; }));
    doc = dump(engine, "musacad_join_exact.musa");
    REQUIRE(doc.lines.size() == 2);
    REQUIRE(doc.polylines.empty());

    // PEDIT Multiple Join within a fuzz distance: Extend meets the lines; Add bridges the gap.
    for (const std::uint8_t type : {std::uint8_t{0}, std::uint8_t{1}}) {
        engine.submit(SelectAllCommand{});
        engine.submit(EraseSelectionCommand{});
        engine.submit(AddLineCommand{{0, 0}, {10, 0}, 7});
        engine.submit(AddLineCommand{{11, 1}, {11, 10}, 8});
        engine.submit(SelectAllCommand{});
        REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
        JoinSelectionCommand j{2.0, 9, true};
        j.jointype = type;
        engine.submit(j);
        REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
        doc = dump(engine, "musacad_jointype.musa");
        REQUIRE(doc.polylines.size() == 1);
        // Either way round: which line the chain starts from is the selection's order.
        std::vector<Vec2> v = doc.polylines[0].points;
        if (!v.empty() && v.front() != Vec2{0, 0}) {
            std::reverse(v.begin(), v.end());
        }
        if (type == 0) {
            REQUIRE(v == std::vector<Vec2>{{0, 0}, {11, 0}, {11, 10}});
        } else {
            REQUIRE(v == std::vector<Vec2>{{0, 0}, {10, 0}, {11, 1}, {11, 10}});
        }
    }
    engine.stop();

    H h;
    h.proc.set_selection_count(2);
    h.run({"PEDIT", "M", "J"});
    REQUIRE(h.out.prompt == "Enter fuzz distance or [Jointype] <0.0000>: ");
    h.run({"J"});
    REQUIRE(h.out.prompt == "Enter join type [Extend/Add/Both] <Extend>: ");
    h.run({"B", "1"});
    REQUIRE(h.last<JoinSelectionCommand>()->jointype == 2);
    h.run({"J", "J", "E", "1"}); // back to Extend for the session
    h.proc.cancel();
}

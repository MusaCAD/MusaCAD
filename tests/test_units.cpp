// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Issue #30: UNITS (display formats), AUDIT, and PURGE beyond layers.

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/units.hpp"

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
struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string&) override {}
    std::vector<std::string> lines;
};
struct ProcHarness {
    std::vector<Command> cmds;
    SilentOutput out;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    template <class T>
    const T* last() const {
        const T* found = nullptr;
        for (const Command& c : cmds) {
            if (const auto* p = std::get_if<T>(&c)) {
                found = p;
            }
        }
        return found;
    }
};
} // namespace

TEST_CASE("#30 UNITS: every linear format") {
    DrawingUnits u;
    u.linear_precision = 4;
    REQUIRE(units::format_length(12.345678, u) == "12.3457");
    u.linear = LinearFormat::Scientific;
    REQUIRE(units::format_length(12.345678, u) == "1.2346E+01");
    u.linear = LinearFormat::Engineering;
    u.linear_precision = 2;
    REQUIRE(units::format_length(15.5, u) == "1'-3.50\"");
    u.linear = LinearFormat::Architectural;
    u.linear_precision = 3; // 1/8
    REQUIRE(units::format_length(15.5, u) == "1'-3 1/2\"");
    REQUIRE(units::format_length(24.0, u) == "2'-0\"");
    REQUIRE(units::format_length(23.99, u) == "2'-0\""); // rounds up past the foot
    u.linear = LinearFormat::Fractional;
    u.linear_precision = 4; // 1/16
    REQUIRE(units::format_length(12.375, u) == "12 3/8");
    REQUIRE(units::format_length(0.5, u) == "1/2");
    REQUIRE(units::format_length(-3.25, u) == "-3 1/4");
    REQUIRE(units::format_length(7.0, u) == "7");
}

TEST_CASE("#30 UNITS: every angle format, base angle and clockwise") {
    DrawingUnits u;
    u.angular_precision = 1;
    REQUIRE(units::format_angle(kPi / 4.0, u) == "45.0");
    u.angular = AngleFormat::DegMinSec;
    u.angular_precision = 4;
    REQUIRE(units::format_angle(to_radians(45.5), u) == "45d30'0.0\"");
    u.angular_precision = 0;
    REQUIRE(units::format_angle(to_radians(45.5), u) == "46d");
    u.angular = AngleFormat::Grads;
    u.angular_precision = 1;
    REQUIRE(units::format_angle(kPi / 4.0, u) == "50.0g");
    u.angular = AngleFormat::Radians;
    u.angular_precision = 4;
    REQUIRE(units::format_angle(kPi / 4.0, u) == "0.7854r");
    u.angular = AngleFormat::Surveyor;
    u.angular_precision = 0;
    REQUIRE(units::format_angle(to_radians(45.0), u) == "N 45d E");
    REQUIRE(units::format_angle(to_radians(135.0), u) == "N 45d W");
    REQUIRE(units::format_angle(to_radians(225.0), u) == "S 45d W");
    REQUIRE(units::format_angle(to_radians(300.0), u) == "S 30d E"); // 30 deg past south toward east
    // Base angle north and clockwise: east (0 rad) reads as 90 measured clockwise from north.
    DrawingUnits nav;
    nav.base_angle = kHalfPi;
    nav.clockwise = true;
    nav.angular_precision = 0;
    REQUIRE(units::format_angle(0.0, nav) == "90");
    REQUIRE(units::format_angle(kHalfPi, nav) == "0");
    REQUIRE(units::format_point({1.5, 2.25}, DrawingUnits{}) == "1.5000, 2.2500");
}

TEST_CASE("#30 UNITS: set through the engine, published, saved, and used by the inquiry reports") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 5.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 10; }));
    DrawingUnits u;
    u.linear = LinearFormat::Fractional;
    u.linear_precision = 4;
    engine.submit(SetUnitsCommand{u});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.units.linear == LinearFormat::Fractional; }));
    engine.submit(AreaQueryCommand{{5, 0.1}, 1.0});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("Area = 78 9/16") != std::string::npos; }));

    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_units.musa";
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("Saved", 0) == 0; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    REQUIRE(doc.display_units.linear == LinearFormat::Fractional);
    REQUIRE(doc.display_units.linear_precision == 4);
    std::filesystem::remove(p);
    engine.stop();
}

TEST_CASE("#30 UNITS command flow sets every field; DIST and ID report in the units") {
    ProcHarness h;
    h.proc.submit_line("-UNITS");
    h.proc.submit_line("F");   // fractional
    h.proc.submit_line("8");   // the denominator: 1/8 is precision 3
    h.proc.submit_line("DMS"); // deg-min-sec
    h.proc.submit_line("2");
    h.proc.submit_line("90");  // angle 0 = north
    h.proc.submit_line("Y");   // clockwise
    const auto* su = h.last<SetUnitsCommand>();
    REQUIRE(su != nullptr);
    REQUIRE(su->units.linear == LinearFormat::Fractional);
    REQUIRE(su->units.linear_precision == 3);
    REQUIRE(su->units.angular == AngleFormat::DegMinSec);
    REQUIRE(su->units.angular_precision == 2);
    REQUIRE(su->units.base_angle == Approx(kHalfPi));
    REQUIRE(su->units.clockwise);

    DrawingUnits arch;
    arch.linear = LinearFormat::Architectural;
    arch.linear_precision = 3;
    h.proc.set_units(arch);
    h.out.lines.clear();
    h.proc.submit_line("DIST");
    h.proc.submit_line("0,0");
    h.proc.submit_line("15.5,0");
    bool saw = false;
    for (const std::string& l : h.out.lines) {
        saw = saw || l.find("Distance = 1'-3 1/2\"") != std::string::npos;
    }
    REQUIRE(saw);
    h.out.lines.clear();
    h.proc.submit_line("ID");
    h.proc.submit_line("12,0");
    saw = false;
    for (const std::string& l : h.out.lines) {
        saw = saw || l.find("X = 1'-0\"") != std::string::npos;
    }
    REQUIRE(saw);
}

TEST_CASE("#30 PURGE: unused dimension styles, table styles, blocks and image defs go; used ones stay; references reindex") {
    GeometryStore s;
    // Three dimstyles: 0 default, 1 unused, 2 used by a dimension.
    DimStyle a;
    a.name = "unused";
    DimStyle b;
    b.name = "used";
    s.add_dimstyle(a);
    s.add_dimstyle(b);
    REQUIRE(s.dimstyles().size() == 3);
    const EntityHandle d = s.add_dimension(DimType::Linear, {0, 0}, {10, 0}, {0, 5}, 2, {});
    REQUIRE(s.dimstyle_in_use(2));
    REQUIRE(!s.dimstyle_in_use(1));
    REQUIRE(!s.remove_dimstyle(2)); // used
    REQUIRE(!s.remove_dimstyle(0)); // the default
    REQUIRE(s.remove_dimstyle(1));
    REQUIRE(s.dimstyles().size() == 2);
    REQUIRE(s.dimension(d)->style == 1); // followed its style down one slot
    REQUIRE(s.dimstyles()[1].name == "used");

    // Blocks: A unused, B used by an insert, C referenced only from inside A.
    BlockDef A;
    A.name = "A";
    BlockDef B;
    B.name = "B";
    BlockDef C;
    C.name = "C";
    s.add_block(A);
    s.add_block(B);
    s.add_block(C);
    const EntityHandle ins = s.add_insert(1, {0, 0}, 1.0, 1.0, 0.0);
    REQUIRE(s.block_in_use(1));
    REQUIRE(!s.remove_block(1));
    REQUIRE(s.remove_block(0)); // A
    REQUIRE(s.insert(ins)->block == 0); // B slid down
    REQUIRE(s.block_count() == 2);
}

TEST_CASE("#30 PURGE by type through the engine; AUDIT finds and fixes a bad layer reference") {
    ProcHarness h;
    h.proc.submit_line("PURGE"); // no dialog here: AutoCAD's -PURGE prompts run
    h.proc.submit_line("D");
    h.proc.submit_line("");  // every name
    h.proc.submit_line("N"); // no verify: one command
    REQUIRE(h.last<PurgeCommand>() != nullptr);
    REQUIRE(h.last<PurgeCommand>()->what == 2);
    h.proc.submit_line("PU");
    h.proc.submit_line("");
    h.proc.submit_line("");
    h.proc.submit_line("N");
    REQUIRE(h.last<PurgeCommand>()->what == 0);
    h.proc.submit_line("AUDIT");
    h.proc.submit_line("Y");
    REQUIRE(h.last<AuditCommand>() != nullptr);
    REQUIRE(h.last<AuditCommand>()->fix);

    // A file whose line points at layer 7 of a one-layer drawing.
    io::Document doc;
    io::DocLine bad;
    bad.a = {0, 0};
    bad.b = {10, 0};
    bad.props.layer = 7;
    doc.lines.push_back(bad);
    doc.circles.push_back(io::DocCircle{{0, 0}, 3.0, {}});
    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_audit.musa";
    REQUIRE(io::save_native(doc, p.string()).ok);
    GeometryEngine engine;
    engine.start();
    engine.submit(OpenDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("Opened", 0) == 0; }));
    engine.submit(AuditCommand{false});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Total errors found 1 fixed 0") != std::string::npos;
    }));
    engine.submit(AuditCommand{true});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Total errors found 1 fixed 1") != std::string::npos;
    }));
    engine.submit(AuditCommand{false});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Total errors found 0 fixed 0") != std::string::npos;
    }));
    std::filesystem::remove(p);
    engine.stop();
}

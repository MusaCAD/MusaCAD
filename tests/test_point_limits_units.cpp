// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Feet-and-inch and fraction input and the DXF unit headers (#81); PDMODE / PDSIZE / PTYPE
// and the point markers (#40); LIMITS and its checking, SNAP Aspect / Type, typed POINT and
// MULTIPLE (#65, #40).

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
#include "musacad/command/coordinate.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/units.hpp"

using namespace musacad::core;
using musacad::command::CommandOutput;
using musacad::command::CommandProcessor;
using musacad::command::CoordParse;
using musacad::command::parse_coordinate;
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
    template <class T>
    [[nodiscard]] std::size_t count() const {
        std::size_t n = 0;
        for (const auto& c : cmds) {
            n += std::holds_alternative<T>(c) ? 1 : 0;
        }
        return n;
    }
};
} // namespace

TEST_CASE("Lengths as typed: decimals, fractions, feet and inches") {
    using units::parse_length_text;
    REQUIRE(parse_length_text("12.5") == Approx(12.5));
    REQUIRE(parse_length_text(" -3 ") == Approx(-3.0));
    REQUIRE(parse_length_text("1e3") == Approx(1000.0));
    REQUIRE(parse_length_text("3/8") == Approx(0.375));
    REQUIRE(parse_length_text("12 3/8") == Approx(12.375));
    REQUIRE(parse_length_text("12-3/8") == Approx(12.375));
    REQUIRE(parse_length_text("1'-6\"") == Approx(18.0));
    REQUIRE(parse_length_text("1'6") == Approx(18.0));
    REQUIRE(parse_length_text("1'") == Approx(12.0));
    REQUIRE(parse_length_text("6\"") == Approx(6.0));
    REQUIRE(parse_length_text("1'-6 1/2\"") == Approx(18.5));
    REQUIRE(parse_length_text("-2'3") == Approx(-27.0));
    REQUIRE(parse_length_text("2.5'") == Approx(30.0));
    REQUIRE_FALSE(parse_length_text("").has_value());
    REQUIRE_FALSE(parse_length_text("abc").has_value());
    REQUIRE_FALSE(parse_length_text("'").has_value());
    REQUIRE_FALSE(parse_length_text("\"").has_value());
    REQUIRE_FALSE(parse_length_text("1/0").has_value());
    REQUIRE_FALSE(parse_length_text("1'x").has_value());

    const CoordParse p = parse_coordinate("1'-6\",2'", std::nullopt);
    REQUIRE(p.ok);
    REQUIRE(p.point == Vec2{18.0, 24.0});
    const CoordParse q = parse_coordinate("@6 1/2<90", Vec2{0, 0});
    REQUIRE(q.ok);
    REQUIRE(q.point.x == Approx(0.0).margin(1e-12));
    REQUIRE(q.point.y == Approx(6.5));
}

TEST_CASE("The DXF header carries the units, the point display and the limits both ways") {
    io::Document doc;
    doc.display_units.insunits = 1; // inches
    doc.display_units.linear = LinearFormat::Architectural;
    doc.display_units.linear_precision = 3;
    doc.display_units.angular = AngleFormat::DegMinSec;
    doc.display_units.angular_precision = 2;
    doc.display_units.clockwise = true;
    doc.display_units.base_angle = kHalfPi;
    doc.pdmode = 35;
    doc.pdsize = 2.5;
    doc.limits_min = {-1, -2};
    doc.limits_max = {300, 200};
    doc.limits_on = true;
    doc.fillmode = false;
    const std::string dxf = io::serialize_dxf(doc);
    REQUIRE(dxf.find("$INSUNITS") != std::string::npos);
    REQUIRE(dxf.find("$MEASUREMENT\n70\n0\n") != std::string::npos); // inches: imperial
    io::Document back;
    REQUIRE(io::parse_dxf(dxf, back).ok);
    REQUIRE(back.display_units.insunits == 1);
    REQUIRE(back.display_units.linear == LinearFormat::Architectural);
    REQUIRE(back.display_units.linear_precision == 3);
    REQUIRE(back.display_units.angular == AngleFormat::DegMinSec);
    REQUIRE(back.display_units.angular_precision == 2);
    REQUIRE(back.display_units.clockwise);
    REQUIRE(back.display_units.base_angle == Approx(kHalfPi));
    REQUIRE(back.pdmode == 35);
    REQUIRE(back.pdsize == Approx(2.5));
    REQUIRE(back.limits_min == Vec2{-1, -2});
    REQUIRE(back.limits_max == Vec2{300, 200});
    REQUIRE(back.limits_on);
    REQUIRE_FALSE(back.fillmode);

    io::Document native;
    REQUIRE(io::parse_native(io::serialize_native(doc), native).ok);
    REQUIRE(native.format_version == io::kFormatVersion);
    REQUIRE(native.pdmode == 35);
    REQUIRE(native.pdsize == Approx(2.5));
    REQUIRE(native.limits_max == Vec2{300, 200});
    REQUIRE(native.limits_on);
}

TEST_CASE("PDMODE draws points as markers; LIMITS is kept with the drawing") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPointCommand{{10, 10}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.points.size() == 1; })); // mode 0: a dot
    REQUIRE(engine.snapshot().line_vertices.empty());
    engine.submit(SetPointDisplayCommand{3, 4.0}); // a cross 4 wide
    REQUIRE(wait_until(engine, [](const auto& s) { return s.pdmode == 3 && s.line_vertices.size() == 4; }));
    REQUIRE(engine.snapshot().points.empty());
    REQUIRE(engine.snapshot().line_vertices[0] == Vec2{8, 8});
    REQUIRE(engine.snapshot().line_vertices[1] == Vec2{12, 12});
    engine.submit(SetPointDisplayCommand{66, 4.0}); // a plus in a square: 2 + 4 segments
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 12; }));
    engine.submit(SetPointDisplayCommand{1, 4.0}); // nothing
    REQUIRE(wait_until(engine, [](const auto& s) { return s.pdmode == 1 && s.line_vertices.empty() && s.points.empty(); }));
    engine.submit(SetLimitsCommand{{0, 0}, {100, 50}, true, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.limits_on && s.limits_max == Vec2{100, 50}; }));
    engine.submit(SetLimitsCommand{{}, {}, false, false}); // OFF keeps the corners
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.limits_on; }));
    REQUIRE(engine.snapshot().limits_max == Vec2{100, 50});
    const io::Document doc = dump(engine, "musacad_point_limits.musa");
    REQUIRE(doc.pdmode == 1);
    REQUIRE(doc.pdsize == Approx(4.0));
    REQUIRE(doc.limits_max == Vec2{100, 50});
    REQUIRE_FALSE(doc.limits_on);
    engine.stop();
}

TEST_CASE("PDMODE, PDSIZE, PTYPE and LIMITS at the command line; POINT places one; MULTIPLE repeats") {
    H h;
    h.proc.set_point_display(0, 0.0);
    h.proc.submit_line("PDMODE");
    REQUIRE(h.out.prompt == "Enter new value for PDMODE <0>: ");
    h.proc.submit_line("5");
    REQUIRE(h.out.lines.back().rfind("Requires 0, 1, 2, 3 or 4", 0) == 0);
    h.proc.submit_line("35");
    REQUIRE(h.last<SetPointDisplayCommand>()->mode == 35);
    h.proc.submit_line("PTYPE");
    REQUIRE(h.out.prompt == "Enter point display mode (PDMODE) <0>: ");
    h.proc.submit_line("2");
    REQUIRE(h.out.prompt == "Enter point size (PDSIZE) <0.0000>: ");
    h.proc.submit_line("1'");
    REQUIRE(h.last<SetPointDisplayCommand>()->mode == 2);
    REQUIRE(h.last<SetPointDisplayCommand>()->size == Approx(12.0));
    h.proc.submit_line("PDSIZE");
    h.proc.submit_line("-3");
    REQUIRE(h.last<SetPointDisplayCommand>()->size == Approx(-3.0));

    h.proc.set_limits({0, 0}, {420, 297}, false);
    h.proc.submit_line("LIMITS");
    REQUIRE(h.out.lines.back() == "Reset Model space limits (limits checking off):");
    REQUIRE(h.out.prompt == "Specify lower left corner or [ON/OFF] <0.0000,0.0000>: ");
    h.proc.submit_line("");
    REQUIRE(h.out.prompt == "Specify upper right corner <420.0000,297.0000>: ");
    h.proc.submit_line("100,50");
    const auto* lim = h.last<SetLimitsCommand>();
    REQUIRE(lim->corners);
    REQUIRE(lim->max == Vec2{100, 50});
    h.proc.submit_line("LIMITS");
    h.proc.submit_line("ON");
    REQUIRE(h.last<SetLimitsCommand>()->on);
    REQUIRE_FALSE(h.last<SetLimitsCommand>()->corners);

    // Checking on: a point outside is refused at any point prompt.
    h.proc.set_limits({0, 0}, {100, 50}, true);
    h.proc.submit_line("LINE");
    h.proc.submit_line("500,500");
    REQUIRE(h.out.lines.back() == "**Outside limits");
    h.proc.submit_line("10,10");
    h.proc.submit_line("20,20");
    h.proc.submit_line("");
    REQUIRE(h.count<AddLineCommand>() == 1);
    h.proc.set_limits({0, 0}, {100, 50}, false);

    h.proc.submit_line("POINT");
    REQUIRE(h.out.lines.back() == "Current point modes:  PDMODE=0  PDSIZE=0.0000");
    h.proc.submit_line("1,1");
    REQUIRE(h.count<AddPointCommand>() == 1);
    REQUIRE_FALSE(h.proc.has_active_command()); // one point
    h.proc.submit_line("MULTIPLE");
    REQUIRE(h.out.prompt == "Enter command name to repeat: ");
    h.proc.submit_line("NOSUCH");
    REQUIRE(h.out.lines.back() == "Unknown command \"NOSUCH\".");
    h.proc.submit_line("POINT");
    h.proc.submit_line("2,2");
    h.proc.submit_line("3,3");
    REQUIRE(h.count<AddPointCommand>() == 3);
    REQUIRE(h.proc.has_active_command());
    h.proc.cancel();
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.set_grid_spacing(10.0);
    h.proc.submit_line("SNAP");
    REQUIRE(h.out.prompt == "Specify snap spacing or [ON/OFF/Aspect/Legacy/Style/Type] <10.0000>: ");
    h.proc.submit_line("A");
    REQUIRE(h.out.prompt == "Specify horizontal spacing <10.0000>: ");
    h.proc.submit_line("5");
    REQUIRE(h.out.prompt == "Specify vertical spacing <10.0000>: ");
    h.proc.submit_line("2.5");
    REQUIRE(h.proc.snap_spacing() == 5.0);
    REQUIRE(h.proc.snap_spacing_y() == 2.5);
    REQUIRE(h.proc.snap_mode());
    h.proc.submit_line("SNAP");
    h.proc.submit_line("T");
    REQUIRE(h.out.prompt == "Enter snap type [Polar/Grid] <Grid>: ");
    h.proc.submit_line("P");
    REQUIRE(h.proc.snap_type() == 1);
    h.proc.submit_line("SNAP");
    h.proc.submit_line("S");
    h.proc.submit_line("I");
    REQUIRE(h.out.prompt.rfind("Specify vertical spacing <", 0) == 0); // Isometric asks its spacing
    h.proc.cancel();
}

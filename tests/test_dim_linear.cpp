// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Issue #56: DIMLINEAR as AutoCAD has it. The dimension line's angle is the dimension's
// own (DimData::aux), chosen from where the line is placed -- above or below the points
// it measures across, beside them it measures up -- or fixed with Horizontal, Vertical
// or Rotated. Enter at the first prompt selects the object; Text types the text.

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
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

struct CaptureOutput : musacad::command::CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
};

struct Harness {
    std::vector<Command> cmds;
    CaptureOutput out;
    musacad::command::CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, nullptr,
                                            out};
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
    void run(std::initializer_list<const char*> lines) {
        for (const char* l : lines) {
            proc.submit_line(l);
        }
    }
};

DimData linear(Vec2 a, Vec2 b, Vec2 at, double aux = 0.0) {
    DimData d;
    d.type = DimType::Linear;
    d.a = a;
    d.b = b;
    d.line_pt = at;
    d.aux = aux;
    return d;
}

/// Waits until the engine has done everything submitted so far: a query that always
/// reports, answered after them (the queue is first in, first out).
void sync(GeometryEngine& engine) {
    engine.submit(AreaQueryCommand{{1e9, 1e9}, 1e-6});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("AREA:", 0) == 0; }));
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

} // namespace

TEST_CASE("#56 linear orientation: above or below measures across, beside measures up") {
    const Vec2 a{0, 0};
    const Vec2 b{100, 20};
    REQUIRE(linear_dim_auto_angle(a, b, {50, 60}) == Approx(0.0));     // above
    REQUIRE(linear_dim_auto_angle(a, b, {50, -40}) == Approx(0.0));    // below
    REQUIRE(linear_dim_auto_angle(a, b, {150, 10}) == Approx(kHalfPi)); // right
    REQUIRE(linear_dim_auto_angle(a, b, {-30, 10}) == Approx(kHalfPi)); // left
    // Off a corner, the side it stands further out on: 30 right but 80 up is across...
    REQUIRE(linear_dim_auto_angle(a, b, {130, 100}) == Approx(0.0));
    // ... 100 right and 10 up is beside.
    REQUIRE(linear_dim_auto_angle(a, b, {200, 30}) == Approx(kHalfPi));
    // Inside the box: its longer side.
    REQUIRE(linear_dim_auto_angle(a, b, {50, 10}) == Approx(0.0));
    REQUIRE(linear_dim_auto_angle({0, 0}, {10, 80}, {5, 40}) == Approx(kHalfPi));

    // What used to be impossible: a VERTICAL dimension of a mostly horizontal pair.
    DimData d = linear(a, b, {150, 10});
    orient_linear_dim(d, std::nullopt);
    REQUIRE(dim_measure(d) == Approx(20.0));
    d.line_pt = {50, 60};
    orient_linear_dim(d, std::nullopt);
    REQUIRE(dim_measure(d) == Approx(100.0));
    // Fixed: Vertical wherever the line goes; Rotated 30 degrees.
    orient_linear_dim(d, kHalfPi);
    REQUIRE(dim_measure(d) == Approx(20.0));
    orient_linear_dim(d, to_radians(30.0));
    REQUIRE(dim_measure(d) == Approx(100.0 * std::cos(to_radians(30.0)) + 20.0 * 0.5));
}

TEST_CASE("#56 the stored angle: 0 to pi, 0 meaning a dimension from before it was stored") {
    REQUIRE(linear_dim_aux(0.0) == Approx(kPi));
    REQUIRE(linear_dim_aux(kHalfPi) == Approx(kHalfPi));
    REQUIRE(linear_dim_aux(-kHalfPi) == Approx(kHalfPi));
    REQUIRE(linear_dim_aux(3.0 * kHalfPi) == Approx(kHalfPi));
    REQUIRE(linear_dim_aux(kPi + 0.25) == Approx(0.25));
    // Horizontal (pi) reads left to right; 120 degrees is turned to read so too.
    const Vec2 h = dim_line_direction(linear({0, 0}, {10, 50}, {0, 0}, kPi));
    REQUIRE(h.x == Approx(1.0));
    REQUIRE(h.y == Approx(0.0).margin(1e-12));
    const Vec2 r = dim_line_direction(linear({0, 0}, {10, 50}, {0, 0}, to_radians(120.0)));
    REQUIRE(r.x == Approx(0.5));
    REQUIRE(r.y == Approx(-std::sqrt(3.0) / 2.0));
    // aux 0: the axis the points differ most on, exactly as before.
    REQUIRE(dim_measure(linear({0, 0}, {10, 50}, {40, 0})) == Approx(50.0));
    REQUIRE(dim_measure(linear({0, 0}, {50, 10}, {0, 40})) == Approx(50.0));
    // ... the same points under a stored horizontal angle measure across.
    REQUIRE(dim_measure(linear({0, 0}, {10, 50}, {5, 70}, kPi)) == Approx(10.0));
}

TEST_CASE("#56 a circle's linear dimension: the diameter the angle measures, between quadrants") {
    DimData d = linear({-10, 0}, {10, 0}, {30, 5}); // the horizontal diameter, cursor beside
    orient_linear_dim(d, std::nullopt, /*circle=*/true);
    REQUIRE(d.aux == Approx(kHalfPi));
    REQUIRE(d.a.x == Approx(0.0).margin(1e-9));
    REQUIRE(d.a.y == Approx(-10.0));
    REQUIRE(d.b.y == Approx(10.0));
    REQUIRE(dim_measure(d) == Approx(20.0));
    DimData above = linear({-10, 0}, {10, 0}, {3, 25});
    orient_linear_dim(above, std::nullopt, true);
    REQUIRE(above.aux == Approx(kPi));
    REQUIRE(above.a.x == Approx(-10.0));
    REQUIRE(dim_measure(above) == Approx(20.0));
}

TEST_CASE("#56 DIMLINEAR: the prompts, and the angle follows the placement") {
    Harness h;
    h.run({"DLI"});
    REQUIRE(h.out.prompt == "Specify first extension line origin or <select object>: ");
    h.run({"0,0", "100,20"});
    REQUIRE(h.out.prompt ==
            "Specify dimension line location or [Mtext/Text/Angle/Horizontal/Vertical/Rotated]: ");
    h.run({"150,10"}); // beside: vertical
    const auto* d = h.last<AddDimensionCommand>();
    REQUIRE(d != nullptr);
    REQUIRE(d->aux == Approx(kHalfPi));
    REQUIRE(d->line_pt == Vec2{150, 10});
    REQUIRE_FALSE(h.proc.has_active_command());

    Harness above;
    above.run({"DLI", "0,0", "100,20", "50,60"});
    REQUIRE(above.last<AddDimensionCommand>()->aux == Approx(kPi)); // horizontal
}

TEST_CASE("#56 DIMLINEAR Horizontal / Vertical / Rotated fix the angle") {
    Harness v;
    v.run({"DLI", "0,0", "100,20", "V"});
    REQUIRE(v.out.prompt == "Specify dimension line location or [Mtext/Text/Angle]: ");
    REQUIRE(v.proc.preview().dim_angle_fixed);
    v.run({"50,60"}); // above, but Vertical was asked for
    REQUIRE(v.last<AddDimensionCommand>()->aux == Approx(kHalfPi));

    Harness hz;
    hz.run({"DLI", "0,0", "20,100", "horizontal", "150,50"});
    REQUIRE(hz.last<AddDimensionCommand>()->aux == Approx(kPi));

    Harness r;
    r.run({"DLI", "0,0", "100,20", "R"});
    REQUIRE(r.out.prompt == "Specify angle of dimension line <0>: ");
    r.run({"30", "50,60"});
    REQUIRE(r.last<AddDimensionCommand>()->aux == Approx(to_radians(30.0)));

    // Rotated by two points.
    Harness two;
    two.run({"DLI", "0,0", "100,20", "R", "0,0"});
    REQUIRE(two.out.prompt == "Specify second point: ");
    two.run({"10,10", "50,60"});
    REQUIRE(two.last<AddDimensionCommand>()->aux == Approx(kPi / 4.0));
}

TEST_CASE("#56 DIMLINEAR Text: the typed text, <> standing for the measurement") {
    Harness h;
    h.run({"DLI", "0,0", "100,0", "T"});
    REQUIRE(h.out.prompt.rfind("Enter dimension text <100", 0) == 0); // the measurement it keeps
    h.run({"<> TYP", "50,10"});
    const auto* d = h.last<AddDimensionCommand>();
    REQUIRE(d != nullptr);
    REQUIRE(d->text_override == "<> TYP");
    // Enter at the text prompt keeps the measurement.
    Harness keep;
    keep.run({"DLI", "0,0", "100,0", "M", "", "50,10"});
    REQUIRE(keep.last<AddDimensionCommand>()->text_override.empty());
    // Angle is not there yet: said so, and the command goes on.
    Harness ang;
    ang.run({"DLI", "0,0", "100,0", "A"});
    REQUIRE(ang.proc.has_active_command());
    ang.run({"50,10"});
    REQUIRE(ang.last<AddDimensionCommand>() != nullptr);
}

TEST_CASE("#56 Enter at the first prompt selects the object; Horizontal carries to the engine") {
    Harness h;
    h.run({"DLI", ""});
    REQUIRE(h.out.prompt == "Select object to dimension: ");
    h.run({"5,0"});
    REQUIRE(h.last<ResolveDimObjectCommand>() != nullptr);
    REQUIRE(h.proc.preview().kind == musacad::command::PreviewKind::Dimension);
    REQUIRE(h.proc.preview().points.empty()); // the object's points come from the engine
    h.run({"H", "T", "%%c<>", "5,5"});
    const auto* od = h.last<AddObjectDimensionCommand>();
    REQUIRE(od != nullptr);
    REQUIRE(od->type == static_cast<std::uint8_t>(DimType::Linear));
    REQUIRE(od->line_angle.has_value());
    REQUIRE(*od->line_angle == Approx(0.0));
    REQUIRE(od->text_override == "%%c<>");
    // Without an option the angle is left to the placement.
    Harness auto_angle;
    auto_angle.run({"DLI", "", "5,0", "5,5"});
    REQUIRE_FALSE(auto_angle.last<AddObjectDimensionCommand>()->line_angle.has_value());
}

TEST_CASE("#56 DIMALIGNED: <select object>, and [Mtext/Text/Angle] without the linear options") {
    Harness h;
    h.run({"DAL"});
    REQUIRE(h.out.prompt == "Specify first extension line origin or <select object>: ");
    h.run({"0,0", "30,40"});
    REQUIRE(h.out.prompt == "Specify dimension line location or [Mtext/Text/Angle]: ");
    h.run({"T", "<> MAX", "0,20"});
    const auto* d = h.last<AddDimensionCommand>();
    REQUIRE(d != nullptr);
    REQUIRE(d->type == static_cast<std::uint8_t>(DimType::Aligned));
    REQUIRE(d->aux == 0.0);
    REQUIRE(d->text_override == "<> MAX");
}

TEST_CASE("#56 engine: a circle, an arc, and the chain that follows a rotated dimension") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 10.0, 1});
    engine.submit(AddArcCommand{{100, 0}, 10.0, 0.0, kHalfPi, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 10; }));

    AddObjectDimensionCommand c; // the circle, placed beside it: its vertical diameter
    c.type = static_cast<std::uint8_t>(DimType::Linear);
    c.pick1 = {10, 0.1};
    c.pick2 = {30, 5};
    c.pick_radius = 1.0;
    c.group = 3;
    engine.submit(c);
    AddObjectDimensionCommand fixed = c; // ... and Horizontal, wherever it is placed
    fixed.pick2 = {30, -5};
    fixed.line_angle = 0.0;
    fixed.text_override = "<> TYP";
    fixed.group = 4;
    engine.submit(fixed);
    AddObjectDimensionCommand arc = c; // the arc's two ends
    arc.pick1 = {100 + 10 * std::cos(0.7), 10 * std::sin(0.7)};
    arc.pick2 = {105, 30};
    arc.group = 5;
    engine.submit(arc);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("Dimension created") != std::string::npos; }));

    io::Document doc = dump(engine, "musacad_dim_linear_obj.musa");
    REQUIRE(doc.dims.size() == 3);
    REQUIRE(doc.dims[0].aux == Approx(kHalfPi));
    REQUIRE(doc.dims[0].a.y == Approx(-10.0));
    REQUIRE(doc.dims[0].b.y == Approx(10.0));
    REQUIRE(doc.dims[1].aux == Approx(kPi));
    REQUIRE(doc.dims[1].a.x == Approx(-10.0));
    REQUIRE(doc.dims[1].text_override == "<> TYP");
    REQUIRE(doc.dims[2].a.x == Approx(110.0)); // the arc's start (0 degrees) ...
    REQUIRE(doc.dims[2].b.y == Approx(10.0));  // ... and its end (90 degrees)
    REQUIRE(doc.dims[2].aux == Approx(kPi));   // placed above: horizontal

    // A horizontal dimension of a mostly vertical pair, then DIMCONTINUE: the next one is
    // horizontal too, whatever its own points differ most on.
    engine.submit(NewDocumentCommand{});
    engine.submit(AddDimensionCommand{.type = static_cast<std::uint8_t>(DimType::Linear),
                                      .a = {0, 0},
                                      .b = {10, 50},
                                      .line_pt = {5, 70},
                                      .group = 6,
                                      .aux = kPi});
    engine.submit(ChainDimensionCommand{{30, 80}, false, 7});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("Continued dimension") != std::string::npos; }));
    doc = dump(engine, "musacad_dim_linear_chain.musa");
    REQUIRE(doc.dims.size() == 2);
    DimData next;
    next.type = DimType::Linear;
    next.a = doc.dims[1].a;
    next.b = doc.dims[1].b;
    next.line_pt = doc.dims[1].line_pt;
    next.aux = doc.dims[1].aux;
    REQUIRE(next.aux == Approx(kPi));
    REQUIRE(dim_measure(next) == Approx(20.0)); // across, not the 30 it rises
    engine.stop();
}

TEST_CASE("#56 ROTATE and MIRROR turn a linear dimension's line, and an arc length's arc") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{.type = static_cast<std::uint8_t>(DimType::Linear),
                                      .a = {0, 0},
                                      .b = {40, 0},
                                      .line_pt = {20, 10},
                                      .group = 1,
                                      .aux = kPi});
    engine.submit(SelectAllCommand{});
    engine.submit(RotateSelectionCommand{{0, 0}, to_radians(30.0), 2});
    sync(engine);
    io::Document doc = dump(engine, "musacad_dim_rotate.musa");
    REQUIRE(doc.dims.size() == 1);
    REQUIRE(doc.dims[0].aux == Approx(to_radians(30.0)));
    DimData d = linear(doc.dims[0].a, doc.dims[0].b, doc.dims[0].line_pt, doc.dims[0].aux);
    REQUIRE(dim_measure(d) == Approx(40.0)); // still the 40 it measured

    // Mirrored in the x axis: the 30-degree line becomes a 150-degree one (-30).
    engine.submit(SelectAllCommand{});
    engine.submit(MirrorSelectionCommand{{0, 0}, {10, 0}, true, 3});
    sync(engine);
    doc = dump(engine, "musacad_dim_mirror.musa");
    REQUIRE(doc.dims.size() == 1);
    REQUIRE(doc.dims[0].aux == Approx(to_radians(150.0)));
    d = linear(doc.dims[0].a, doc.dims[0].b, doc.dims[0].line_pt, doc.dims[0].aux);
    REQUIRE(dim_measure(d) == Approx(40.0));

    // An arc length dimension of the quarter from 0 to 90 degrees, mirrored in the x
    // axis: the quarter from -90 to 0.
    engine.submit(NewDocumentCommand{});
    engine.submit(AddDimensionCommand{.type = static_cast<std::uint8_t>(DimType::ArcLength),
                                      .a = {0, 0},
                                      .b = {10, 0},
                                      .line_pt = {12, 12},
                                      .group = 4,
                                      .aux = kHalfPi});
    engine.submit(SelectAllCommand{});
    engine.submit(MirrorSelectionCommand{{0, 0}, {10, 0}, true, 5});
    sync(engine);
    doc = dump(engine, "musacad_dimarc_mirror.musa");
    REQUIRE(doc.dims.size() == 1);
    REQUIRE(doc.dims[0].b.x == Approx(0.0).margin(1e-9));
    REQUIRE(doc.dims[0].b.y == Approx(-10.0));
    REQUIRE(doc.dims[0].aux == Approx(0.0).margin(1e-9));
    DimData arc;
    arc.type = DimType::ArcLength;
    arc.a = doc.dims[0].a;
    arc.b = doc.dims[0].b;
    arc.aux = doc.dims[0].aux;
    REQUIRE(dim_measure(arc) == Approx(10.0 * kHalfPi));
    engine.stop();
}

TEST_CASE("#56 DXF 50: the angle out and back; a dimension without one keeps the old rule") {
    io::Document doc;
    io::DocDim across;
    across.type = static_cast<std::uint8_t>(DimType::Linear);
    across.a = {0, 0};
    across.b = {10, 50};
    across.line_pt = {5, 70};
    across.aux = kPi; // horizontal
    io::DocDim rotated = across;
    rotated.aux = to_radians(30.0);
    io::DocDim legacy = across;
    legacy.aux = 0.0; // the axis they differ most on: vertical
    legacy.line_pt = {40, 25};
    doc.dims = {across, rotated, legacy};
    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_dim_linear.dxf";
    REQUIRE(io::save_dxf(doc, p.string()).ok);
    io::Document back;
    REQUIRE(io::load_dxf(p.string(), back).ok);
    REQUIRE(back.dims.size() == 3);
    REQUIRE(back.dims[0].aux == Approx(kPi));
    REQUIRE(back.dims[1].aux == Approx(to_radians(30.0)));
    REQUIRE(back.dims[2].aux == Approx(kHalfPi)); // written as the 90 it was drawn at

    // A DIMENSION from elsewhere with no 50 reads as it always did.
    const std::filesystem::path q = std::filesystem::temp_directory_path() / "musacad_dim_linear_no50.dxf";
    {
        std::FILE* f = std::fopen(q.string().c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fputs("0\nSECTION\n2\nENTITIES\n0\nDIMENSION\n8\n0\n70\n0\n10\n40\n20\n25\n"
                   "13\n0\n23\n0\n14\n10\n24\n50\n0\nENDSEC\n0\nEOF\n",
                   f);
        std::fclose(f);
    }
    io::Document old;
    REQUIRE(io::load_dxf(q.string(), old).ok);
    REQUIRE(old.dims.size() == 1);
    REQUIRE(old.dims[0].aux == 0.0);
    std::filesystem::remove(p);
    std::filesystem::remove(q);
}

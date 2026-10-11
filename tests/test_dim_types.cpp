// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Issue #28: DIMORDINATE, DIMJOGGED and DIMARC. The value is always measured from the
// def points (plus the one extra datum each type needs), the geometry comes from the
// single compute_dim_geometry, and the extra datum survives the native format and DXF.

#include <chrono>
#include <cstdint>
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
#include "musacad/core/geometry_store.hpp"
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
struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string&) override {}
    void set_prompt(const std::string&) override {}
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
// Saves the engine's drawing and reads it back: the one way a test can inspect the
// dimension the engine created, and an end-to-end check of the aux datum in v23.
io::Document dump(GeometryEngine& engine, const char* name) {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
    { // wait for this save itself, not a "Saved" left by an earlier one
        engine.consume_snapshot();
        const std::uint64_t saved_before = engine.snapshot().document_version;
        engine.submit(SaveDocumentCommand{p.string(), false});
        REQUIRE(wait_until(engine, [saved_before](const auto& s) { return s.document_version > saved_before; }));
    }
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    std::filesystem::remove(p);
    return doc;
}
} // namespace

TEST_CASE("#28 dim types: measured values and geometry for ordinate, jogged and arc length") {
    DimStyle s;
    s.precision = 1;

    DimData ordx;
    ordx.type = DimType::Ordinate;
    ordx.a = {12.34, 5.6}; // the feature
    ordx.b = {12.34, 40};  // a vertical leader -> X datum
    ordx.aux = 0.0;
    REQUIRE(dim_measure(ordx) == Approx(12.34));
    const DimGeometry gx = compute_dim_geometry(ordx, s, Rgb{});
    REQUIRE(gx.label == "12.3");
    REQUIRE(gx.dim_lines.size() >= 2);           // the leader
    REQUIRE(gx.text_rotation == Approx(kHalfPi)); // reads along the leader
    REQUIRE(gx.arrow_fills.empty());              // ordinates have no arrowhead
    DimData ordy = ordx;
    ordy.aux = 1.0;
    ordy.b = {60, 5.6 + 8}; // off-axis endpoint: a dogleg
    REQUIRE(dim_measure(ordy) == Approx(5.6));
    const DimGeometry gy = compute_dim_geometry(ordy, s, Rgb{});
    REQUIRE(gy.dim_lines.size() == 6); // three leader segments
    REQUIRE(gy.text_rotation == Approx(0.0));

    DimData jog;
    jog.type = DimType::Jogged;
    jog.a = {0, 0};      // the TRUE centre
    jog.b = {100, 0};    // point on the arc
    jog.line_pt = {60, 0}; // the centre override (where the line starts)
    jog.aux = 0.5;
    REQUIRE(dim_measure(jog) == Approx(100.0)); // the true radius, not from the override
    const DimGeometry gj = compute_dim_geometry(jog, s, Rgb{});
    REQUIRE(gj.label == "R100.0");
    REQUIRE(gj.dim_lines.size() == 10); // five segments: line, jog (3), line
    REQUIRE(!gj.arrow_fills.empty());
    REQUIRE(gj.dim_lines.front().x == Approx(60.0)); // starts at the override

    DimData arc;
    arc.type = DimType::ArcLength;
    arc.a = {0, 0};
    arc.b = {10, 0};      // start point at 0 deg
    arc.aux = kHalfPi;    // end angle: a quarter arc
    arc.line_pt = {15, 15};
    REQUIRE(dim_measure(arc) == Approx(10.0 * kHalfPi));
    const DimGeometry ga = compute_dim_geometry(arc, s, Rgb{});
    REQUIRE(ga.label == "15.7");
    REQUIRE(ga.ext_lines.size() == 4); // two radial extension lines
    REQUIRE(ga.dim_lines.size() >= 48); // the dimension arc
    REQUIRE(ga.arrow_fills.size() >= 6);
    // The dimension arc sits at the placement radius (|line_pt| = 21.2), outside the arc.
    for (const Vec2& p : ga.dim_lines) {
        REQUIRE(std::hypot(p.x, p.y) == Approx(std::hypot(15.0, 15.0)).margin(1e-6));
    }
}

TEST_CASE("#28 DIMJOGGED from a circle: true centre, edge on the override ray, jog position") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 50.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 10; }));
    AddObjectDimensionCommand c;
    c.type = static_cast<std::uint8_t>(DimType::Jogged);
    c.pick1 = {50, 0.1};  // on the circle
    c.pick2 = {30, 0};    // dimension line location
    c.pick3 = {10, 0};    // centre location override
    c.pick4 = {20, 0};    // jog location: a quarter of the way from the override
    c.pick_radius = 1.0;
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Dimension created") != std::string::npos;
    }));
    const io::Document doc = dump(engine, "musacad_dim_jogged.musa");
    REQUIRE(doc.dims.size() == 1);
    const io::DocDim& d = doc.dims[0];
    REQUIRE(d.type == static_cast<std::uint8_t>(DimType::Jogged));
    REQUIRE(d.a.x == Approx(0.0).margin(1e-9));      // true centre
    REQUIRE(d.b.x == Approx(50.0));                  // the edge along the override ray
    REQUIRE(d.b.y == Approx(0.0).margin(1e-9));
    REQUIRE(d.line_pt.x == Approx(10.0));            // the override
    REQUIRE(d.aux == Approx(0.25));                  // jog at 10 of the 40 units
    engine.stop();
}

TEST_CASE("#28 DIMARC from an arc entity and from a polyline arc segment") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddArcCommand{{0, 0}, 10.0, 0.0, kHalfPi, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 4; }));
    AddObjectDimensionCommand c;
    c.type = static_cast<std::uint8_t>(DimType::ArcLength);
    c.pick1 = {7.07, 7.07};
    c.pick2 = {12, 12};
    c.pick_radius = 1.0;
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Dimension created") != std::string::npos;
    }));
    {
        const io::Document doc = dump(engine, "musacad_dim_arc1.musa");
        REQUIRE(doc.dims.size() == 1);
        const io::DocDim& d = doc.dims[0];
        REQUIRE(d.type == static_cast<std::uint8_t>(DimType::ArcLength));
        REQUIRE(d.a == Vec2{0, 0});
        REQUIRE(d.b.x == Approx(10.0));
        REQUIRE(d.aux == Approx(kHalfPi));
        DimData dd;
        dd.type = DimType::ArcLength;
        dd.a = d.a;
        dd.b = d.b;
        dd.aux = d.aux;
        REQUIRE(dim_measure(dd) == Approx(10.0 * kHalfPi));
    }
    // A polyline with a bulged segment: (0,0)->(10,0), bulge 1 = the lower semicircle.
    engine.submit(NewDocumentCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    engine.submit(AddPolylineCommand{{{0, 0}, {10, 0}}, false, 3, {}, {1.0, 0.0}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 8; }));
    c.pick1 = {5, -4.9};
    c.pick2 = {5, -12};
    c.group = 4;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Dimension created") != std::string::npos;
    }));
    const io::Document doc = dump(engine, "musacad_dim_arc2.musa");
    REQUIRE(doc.dims.size() == 1);
    DimData dd;
    dd.type = DimType::ArcLength;
    dd.a = doc.dims[0].a;
    dd.b = doc.dims[0].b;
    dd.aux = doc.dims[0].aux;
    REQUIRE(dd.a.x == Approx(5.0));
    REQUIRE(dim_measure(dd) == Approx(5.0 * kPi)); // half the circumference of r 5
    engine.stop();
}

TEST_CASE("#28 commands: DIMORDINATE picks the datum from the leader (or Xdatum/Ydatum); DJO and DAR flows") {
    { // A vertical leader measures X.
        ProcHarness h;
        h.proc.submit_line("DOR");
        h.proc.submit_line("10,20");
        h.proc.submit_line("10,50");
        const auto* d = h.last<AddDimensionCommand>();
        REQUIRE(d != nullptr);
        REQUIRE(d->type == static_cast<std::uint8_t>(DimType::Ordinate));
        REQUIRE(d->aux == Approx(0.0));
        REQUIRE(d->a == Vec2{10, 20});
        REQUIRE(d->b == Vec2{10, 50});
    }
    { // A mostly horizontal leader measures Y.
        ProcHarness h;
        h.proc.submit_line("DIMORDINATE");
        h.proc.submit_line("10,20");
        h.proc.submit_line("40,21");
        REQUIRE(h.last<AddDimensionCommand>()->aux == Approx(1.0));
    }
    { // Forced Y with a vertical leader.
        ProcHarness h;
        h.proc.submit_line("DIMORDINATE");
        h.proc.submit_line("10,20");
        h.proc.submit_line("Y");
        h.proc.submit_line("10,50");
        REQUIRE(h.last<AddDimensionCommand>()->aux == Approx(1.0));
    }
    { // DIMJOGGED: four picks -> an object dimension with the override and jog points.
        ProcHarness h;
        h.proc.submit_line("DJO");
        h.proc.submit_line("50,0");
        h.proc.submit_line("10,0");
        h.proc.submit_line("30,0");
        h.proc.submit_line("20,0");
        const auto* o = h.last<AddObjectDimensionCommand>();
        REQUIRE(o != nullptr);
        REQUIRE(o->type == static_cast<std::uint8_t>(DimType::Jogged));
        REQUIRE(o->pick1 == Vec2{50, 0});
        REQUIRE(o->pick2 == Vec2{30, 0});
        REQUIRE(o->pick3 == Vec2{10, 0});
        REQUIRE(o->pick4 == Vec2{20, 0});
        REQUIRE(!h.proc.has_active_command());
    }
    { // DIMARC: select, then place.
        ProcHarness h;
        h.proc.submit_line("DAR");
        h.proc.submit_line("7,7");
        h.proc.submit_line("12,12");
        const auto* o = h.last<AddObjectDimensionCommand>();
        REQUIRE(o != nullptr);
        REQUIRE(o->type == static_cast<std::uint8_t>(DimType::ArcLength));
        REQUIRE(o->pick2 == Vec2{12, 12});
    }
}

TEST_CASE("#28 DXF: ordinate dimensions round-trip with their datum axis") {
    io::Document doc;
    io::DocDim x;
    x.type = static_cast<std::uint8_t>(DimType::Ordinate);
    x.a = {12, 5};
    x.b = {12, 40};
    x.line_pt = x.b;
    x.aux = 0.0;
    io::DocDim y = x;
    y.b = {60, 5};
    y.line_pt = y.b;
    y.aux = 1.0;
    doc.dims = {x, y};
    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_ordinate.dxf";
    REQUIRE(io::save_dxf(doc, p.string()).ok);
    io::Document back;
    REQUIRE(io::load_dxf(p.string(), back).ok);
    REQUIRE(back.dims.size() == 2);
    int xs = 0;
    int ys = 0;
    for (const io::DocDim& d : back.dims) {
        REQUIRE(d.type == static_cast<std::uint8_t>(DimType::Ordinate));
        REQUIRE(d.a == Vec2{12, 5});
        if (d.aux < 0.5) {
            ++xs;
        } else {
            ++ys;
        }
    }
    REQUIRE(xs == 1);
    REQUIRE(ys == 1);
    std::filesystem::remove(p);
}

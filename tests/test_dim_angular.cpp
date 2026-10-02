// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Issue #56: DIMANGULAR as AutoCAD has it -- `Select arc, circle, line, or <specify
// vertex>:`, the dimension arc where it is placed, and the angle that location stands in
// (two lines make four; three points or a circle the angle or the rest of the turn; an
// arc its own), Quadrant to choose it apart from the arc's place.

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
    std::string prompt;
    void append_line(const std::string&) override {}
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

DimData angular(Vec2 v, Vec2 r1, Vec2 r2) {
    DimData d;
    d.type = DimType::Angular;
    d.a = v;
    d.b = r1;
    d.line_pt = r2;
    return d;
}

} // namespace

TEST_CASE("#56 angular: the turn from ray 1 to ray 2, reflex angles included") {
    REQUIRE(to_degrees(ccw_sweep({1, 0}, {0, 1})) == Approx(90.0));
    REQUIRE(to_degrees(ccw_sweep({0, 1}, {1, 0})) == Approx(270.0));
    DimData d = angular({0, 0}, {10, 0}, {0, 10});
    REQUIRE(dim_measure(d) == Approx(90.0)); // aux 0: from before, the smaller angle
    d.aux = 5.0;
    REQUIRE(dim_measure(d) == Approx(90.0));
    std::swap(d.b, d.line_pt);
    REQUIRE(dim_measure(d) == Approx(270.0));
}

TEST_CASE("#56 two lines: the angle of the four the arc is placed in") {
    // Lines along +x and +y from the origin.
    const auto placed = [](Vec2 at, std::optional<Vec2> quadrant = std::nullopt) {
        DimData d = angular({0, 0}, {10, 0}, {0, 10});
        place_angular_dim(d, at, AngularFrom::Lines, quadrant);
        return d;
    };
    DimData d = placed({5, 5});
    REQUIRE(dim_measure(d) == Approx(90.0));
    REQUIRE(d.aux == Approx(std::sqrt(50.0)));
    REQUIRE(dim_measure(placed({-5, 5})) == Approx(90.0)); // the supplement, also 90 here
    // Lines at 30 degrees apart: inside gives 30, beside gives the 150 supplement.
    const auto at30 = [](Vec2 at) {
        DimData e = angular({0, 0}, {10, 0}, {10 * std::cos(0.5235987755982988), 10 * std::sin(0.5235987755982988)});
        place_angular_dim(e, at, AngularFrom::Lines);
        return dim_measure(e);
    };
    REQUIRE(at30({8, 2}) == Approx(30.0));
    REQUIRE(at30({-3, 6}) == Approx(150.0));
    REQUIRE(at30({-8, -2}) == Approx(30.0)); // the opposite angle
    REQUIRE(at30({3, -6}) == Approx(150.0));
    // Quadrant chooses the angle; the location only the radius.
    DimData q = placed({-20, 1}, Vec2{1, -1});
    REQUIRE(q.aux == Approx(std::sqrt(401.0)));
    const Vec2 mid = q.b - q.a + (q.line_pt - q.a); // between the two rays
    REQUIRE(mid.x > 0.0);
    REQUIRE(mid.y < 0.0);
}

TEST_CASE("#56 three points: the angle, or the rest of the turn where the arc is put") {
    DimData d = angular({0, 0}, {10, 0}, {0, 10});
    place_angular_dim(d, {3, 3}, AngularFrom::Points);
    REQUIRE(dim_measure(d) == Approx(90.0));
    DimData r = angular({0, 0}, {10, 0}, {0, 10});
    place_angular_dim(r, {-3, -3}, AngularFrom::Points);
    REQUIRE(dim_measure(r) == Approx(270.0));
    // The geometry: an arc at the placed radius, extension lines out to it.
    DimStyle style;
    DimData far = angular({0, 0}, {2, 0}, {0, 2});
    place_angular_dim(far, {10, 10}, AngularFrom::Points);
    const DimGeometry g = compute_dim_geometry(far, style, Rgb{});
    REQUIRE(g.ext_lines.size() == 4); // one for each ray
    double rmax = 0.0;
    for (const Vec2& p : g.dim_lines) {
        rmax = std::max(rmax, length(p));
    }
    REQUIRE(rmax == Approx(std::sqrt(200.0)).epsilon(1e-6));
    REQUIRE(g.label == "90.00°");
}

TEST_CASE("#56 DIMANGULAR prompts: <specify vertex>, Quadrant, Text") {
    Harness h;
    h.run({"DAN"});
    REQUIRE(h.out.prompt == "Select arc, circle, line, or <specify vertex>: ");
    h.run({""});
    REQUIRE(h.out.prompt == "Specify angle vertex: ");
    h.run({"0,0", "10,0"});
    REQUIRE(h.out.prompt == "Specify second angle endpoint: ");
    h.run({"0,10"});
    REQUIRE(h.out.prompt == "Specify dimension arc line location or [Mtext/Text/Angle/Quadrant]: ");
    REQUIRE(h.proc.preview().points.size() == 3);
    h.run({"T", "<> TYP", "-4,-4"});
    const auto* d = h.last<AddDimensionCommand>();
    REQUIRE(d != nullptr);
    REQUIRE(d->type == static_cast<std::uint8_t>(DimType::Angular));
    REQUIRE(d->aux == Approx(std::sqrt(32.0)));
    REQUIRE(d->text_override == "<> TYP");
    DimData m = angular(d->a, d->b, d->line_pt);
    m.aux = d->aux;
    REQUIRE(dim_measure(m) == Approx(270.0));

    // Two lines (nothing hovered: a line), Quadrant, then the arc.
    Harness lines;
    lines.run({"DAN", "5,0", "0,5"});
    REQUIRE(lines.last<ResolveDimObjectCommand>() != nullptr);
    lines.run({"Q", "-1,1", "8,8"});
    const auto* od = lines.last<AddObjectDimensionCommand>();
    REQUIRE(od != nullptr);
    REQUIRE(od->arc_at.has_value());
    REQUIRE(od->quadrant.has_value());
    REQUIRE(od->quadrant->x == -1.0);

    // An arc under the cursor: straight to the location.
    Harness arc;
    arc.run({"DAN"});
    arc.proc.set_hovered_kind(EntityKind::Arc);
    arc.run({"10,0"});
    REQUIRE(arc.out.prompt == "Specify dimension arc line location or [Mtext/Text/Angle/Quadrant]: ");
    // A circle: the second endpoint first.
    Harness circle;
    circle.run({"DAN"});
    circle.proc.set_hovered_kind(EntityKind::Circle);
    circle.run({"10,0"});
    REQUIRE(circle.out.prompt == "Specify second angle endpoint: ");
}

TEST_CASE("#56 engine: an arc's own angle, a circle's, two lines' -- where the arc is put") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddArcCommand{{0, 0}, 10.0, 0.0, kPi * 1.5, 1}); // three quarters
    engine.submit(AddCircleCommand{{100, 0}, 10.0, 2});
    engine.submit(AddLineCommand{{200, 0}, {220, 0}, 3});
    engine.submit(AddLineCommand{{200, 0}, {200, 20}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 20; }));
    AddObjectDimensionCommand a;
    a.type = static_cast<std::uint8_t>(DimType::Angular);
    a.pick1 = {0, 10.05}; // on the arc (at 90 degrees)
    a.pick2 = a.pick1;
    a.pick_radius = 1.0;
    a.arc_at = Vec2{-15, 0};
    a.group = 4;
    engine.submit(a);
    AddObjectDimensionCommand c = a; // the circle, from 0 to 90 degrees, arc in the rest
    c.pick1 = {110.05, 0};
    c.pick2 = {100, 30};
    c.arc_at = Vec2{80, -5};
    c.group = 5;
    engine.submit(c);
    AddObjectDimensionCommand l = a; // the two lines, arc beyond their ends
    l.pick1 = {215, 0.05};
    l.pick2 = {200.05, 15};
    l.arc_at = Vec2{230, 30};
    l.group = 6;
    engine.submit(l);
    engine.submit(SaveDocumentCommand{(std::filesystem::temp_directory_path() / "musacad_dim_angular.musa").string(), false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("Saved", 0) == 0; }));
    io::Document doc;
    REQUIRE(io::load_native((std::filesystem::temp_directory_path() / "musacad_dim_angular.musa").string(), doc).ok);
    REQUIRE(doc.dims.size() == 3);
    const auto measure = [](const io::DocDim& dd) {
        DimData d = angular(dd.a, dd.b, dd.line_pt);
        d.aux = dd.aux;
        return dim_measure(d);
    };
    REQUIRE(measure(doc.dims[0]) == Approx(270.0));
    REQUIRE(doc.dims[0].aux == Approx(15.0));
    REQUIRE(measure(doc.dims[1]) == Approx(270.0)); // the rest of the turn, where the arc went
    REQUIRE(measure(doc.dims[2]) == Approx(90.0));
    REQUIRE(doc.dims[2].aux == Approx(std::hypot(30.0, 30.0)));
    std::filesystem::remove(std::filesystem::temp_directory_path() / "musacad_dim_angular.musa");
    engine.stop();
}

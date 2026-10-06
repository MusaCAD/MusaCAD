// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// TRIM, EXTEND and FILLET beyond lines, arcs, circles and polylines (issues #48, #49):
// ellipses and elliptical arcs, construction lines and rays (as objects and as edges),
// a polyline's arc end, parallel lines filleted with a half circle -- and TRIM / EXTEND's
// hover preview of the part that would go or be added.

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

/// The engine's drawing saved and read back: how a test sees what it made.
io::Document dump(GeometryEngine& engine, const char* name) {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("Saved", 0) == 0; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    std::filesystem::remove(p);
    return doc;
}

bool has_line(const io::Document& doc, Vec2 a, Vec2 b, double eps = 1e-6) {
    const auto eq = [&](Vec2 p, Vec2 q) { return std::abs(p.x - q.x) < eps && std::abs(p.y - q.y) < eps; };
    for (const auto& l : doc.lines) {
        if ((eq(l.a, a) && eq(l.b, b)) || (eq(l.a, b) && eq(l.b, a))) {
            return true;
        }
    }
    return false;
}

std::uint64_t g = 100; // a fresh undo group per step
} // namespace

// ---------------------------------------------------------------------------
// TRIM
// ---------------------------------------------------------------------------

TEST_CASE("TRIM an ellipse between two edges leaves an elliptical arc") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddEllipseCommand{{0, 0}, {10, 0}, 0.5});
    engine.submit(AddLineCommand{{-5, -10}, {-5, 10}, 1});
    engine.submit(AddLineCommand{{5, -10}, {5, 10}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 6; }));
    engine.submit(TrimPickCommand{{0, 5}, 0.5, ++g, false}); // the top, between the edges
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Trimmed."; }));
    const io::Document doc = dump(engine, "musacad_trim_curves_ellipse.musa");
    REQUIRE(doc.ellipses.size() == 1);
    const io::DocEllipse& e = doc.ellipses[0];
    // What stays runs from the top-left crossing (t = 2pi/3) the long way round, under, to
    // the top-right one (t = pi/3).
    double sweep = std::fmod(e.end - e.start, 2.0 * kPi);
    if (sweep <= 0.0) {
        sweep += 2.0 * kPi;
    }
    REQUIRE(sweep == Approx(2.0 * kPi - (2.0 * kPi / 3.0 - kPi / 3.0)).margin(1e-2)); // crossings on the tessellation
    engine.stop();
}

TEST_CASE("TRIM a construction line leaves a ray; a ray is a cutting edge too") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddXlineCommand{{0, 0}, {1, 0}, false, 1});
    engine.submit(AddLineCommand{{5, -10}, {5, 10}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(TrimPickCommand{{20, 0}, 0.5, ++g, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Trimmed."; }));
    io::Document doc = dump(engine, "musacad_trim_curves_xline.musa");
    REQUIRE(doc.xlines.size() == 1);
    REQUIRE(doc.xlines[0].ray);
    REQUIRE(doc.xlines[0].base.x == Approx(5.0));
    REQUIRE(doc.xlines[0].dir.x == Approx(-1.0));

    // A ray straight up through (10, 0) cuts a line along the x axis.
    engine.submit(AddLineCommand{{0, 30}, {20, 30}, 3});
    engine.submit(AddXlineCommand{{10, 20}, {0, 1}, true, 4});
    engine.submit(TrimPickCommand{{15, 30}, 0.5, ++g, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Trimmed."; }));
    doc = dump(engine, "musacad_trim_curves_ray.musa");
    REQUIRE(has_line(doc, {0, 30}, {10, 30}));
    engine.stop();
}

TEST_CASE("TRIM by a path across a construction line") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddXlineCommand{{0, 0}, {1, 0}, false, 1});
    engine.submit(AddLineCommand{{0, -10}, {0, 10}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(TrimPathCommand{{{5, -1}, {5, 1}}, false, 0.5, ++g});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("Trimmed 1 object") != std::string::npos; }));
    const io::Document doc = dump(engine, "musacad_trim_curves_path.musa");
    REQUIRE(doc.xlines.size() == 1);
    REQUIRE(doc.xlines[0].ray);
    REQUIRE(doc.xlines[0].base.x == Approx(0.0).margin(1e-9));
    REQUIRE(doc.xlines[0].dir.x == Approx(-1.0));
    engine.stop();
}

// ---------------------------------------------------------------------------
// EXTEND
// ---------------------------------------------------------------------------

TEST_CASE("EXTEND to a construction line, an elliptical arc round its ellipse, a polyline's arc end round its circle") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {5, 0}, 1});
    engine.submit(AddXlineCommand{{20, 0}, {0, 1}, false, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(ExtendPickCommand{{4.5, 0}, 0.5, ++g});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Extended."; }));

    // The elliptical arc from (10, 0) round to (0, 5); a boundary at x = -5.
    engine.submit(AddEllipseCommand{{0, 100}, {10, 0}, 0.5, 0.0, kHalfPi, 3});
    engine.submit(AddLineCommand{{-5, 90}, {-5, 110}, 4});
    engine.submit(ExtendPickCommand{{0.3, 105}, 0.5, ++g});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Extended."; }));

    // The polyline's end is a quarter circle round (0, 200), radius 10; a boundary at x = -6.
    AddPolylineCommand pl;
    pl.points = {{10, 200}, {0, 210}};
    pl.bulges = {std::tan(kPi / 8.0), 0.0};
    pl.group = 5;
    engine.submit(pl);
    engine.submit(AddLineCommand{{-6, 180}, {-6, 220}, 6});
    engine.submit(ExtendPickCommand{{0.5, 210}, 0.5, ++g});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Extended."; }));

    const io::Document doc = dump(engine, "musacad_trim_curves_extend.musa");
    REQUIRE(has_line(doc, {0, 0}, {20, 0}));
    REQUIRE(doc.ellipses.size() == 1);
    REQUIRE(doc.ellipses[0].end == Approx(2.0 * kPi / 3.0).margin(1e-2)); // crossings on the tessellation
    REQUIRE(doc.polylines.size() == 1);
    REQUIRE(doc.polylines[0].points[1].x == Approx(-6.0).margin(1e-6));
    REQUIRE(doc.polylines[0].points[1].y == Approx(208.0).margin(1e-6));
    engine.stop();
}

// ---------------------------------------------------------------------------
// FILLET
// ---------------------------------------------------------------------------

TEST_CASE("FILLET parallel lines with a half circle; a construction line with a line") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{0, 4}, {8, 4}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    engine.submit(FilletPickCommand{{9, 0}, {7, 4}, 5.0, 0.5, ++g});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Filleted."; }));

    engine.submit(AddLineCommand{{0, 50}, {10, 50}, 3});
    engine.submit(AddXlineCommand{{5, 55}, {0, 1}, false, 4});
    engine.submit(FilletPickCommand{{2, 50}, {5, 58}, 0.0, 0.5, ++g});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Filleted."; }));

    const io::Document doc = dump(engine, "musacad_trim_curves_fillet.musa");
    REQUIRE(doc.arcs.size() == 1);
    REQUIRE(doc.arcs[0].center.x == Approx(10.0));
    REQUIRE(doc.arcs[0].center.y == Approx(2.0));
    REQUIRE(doc.arcs[0].radius == Approx(2.0));
    REQUIRE(has_line(doc, {0, 4}, {10, 4})); // extended to meet the half circle
    REQUIRE(has_line(doc, {0, 0}, {10, 0})); // the first line as it was
    REQUIRE(has_line(doc, {0, 50}, {5, 50}));
    REQUIRE(doc.xlines.size() == 1);
    REQUIRE(doc.xlines[0].ray);
    REQUIRE(doc.xlines[0].base.y == Approx(50.0));
    REQUIRE(doc.xlines[0].dir.y == Approx(1.0));
    engine.stop();
}

// ---------------------------------------------------------------------------
// The hover preview
// ---------------------------------------------------------------------------

TEST_CASE("TRIM's hover preview draws the part that would go; EXTEND's the part that would be added") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {30, 0}, 1});
    engine.submit(AddLineCommand{{10, -5}, {10, 5}, 2});
    engine.submit(AddLineCommand{{20, -5}, {20, 5}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 6; }));
    engine.submit(TrimPreviewCommand{{15, 0}, 0.5, false, true, true});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.grip_preview_segments.size() == 2 && std::abs(s.grip_preview_segments[0].x - 10.0) < 1e-9 &&
               std::abs(s.grip_preview_segments[1].x - 20.0) < 1e-9;
    }));
    engine.submit(TrimPreviewCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grip_preview_segments.empty(); }));

    // Quick mode: an object with nothing to trim it to goes whole.
    engine.submit(AddLineCommand{{0, 40}, {10, 40}, 4});
    engine.submit(TrimPreviewCommand{{5, 40}, 0.5, false, true, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grip_preview_segments.size() == 2; }));
    engine.submit(TrimPreviewCommand{{5, 40}, 0.5, false, false, true}); // Standard mode: nothing
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grip_preview_segments.empty(); }));

    // EXTEND: the line at y = 40 reaches the edge at x = 20 (from its end at 10).
    engine.submit(AddLineCommand{{20, 30}, {20, 50}, 5});
    engine.submit(TrimPreviewCommand{{9, 40}, 0.5, true, true, true});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.grip_preview_segments.size() == 2 && std::abs(s.grip_preview_segments[1].x - 20.0) < 1e-9;
    }));
    engine.stop();
}

TEST_CASE("TRIM and EXTEND ask the viewport for the hover preview at their object prompt") {
    struct Out : CommandOutput {
        std::string prompt;
        void append_line(const std::string&) override {}
        void set_prompt(const std::string& p) override { prompt = p; }
    } out;
    std::vector<Command> cmds;
    CommandProcessor proc{[&](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    proc.submit_line("TRIM");
    REQUIRE(proc.preview().trim_hover);
    REQUIRE_FALSE(proc.preview().trim_extend);
    proc.submit_line("C"); // Crossing: the window's band instead
    REQUIRE_FALSE(proc.preview().trim_hover);
    proc.cancel();
    proc.submit_line("EXTEND");
    REQUIRE(proc.preview().trim_hover);
    REQUIRE(proc.preview().trim_extend);
    proc.submit_line("");
    REQUIRE_FALSE(proc.preview().trim_hover);
}

TEST_CASE("FILLET and CHAMFER show what the second pick would make; nothing in the drawing changes") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{0, 0}, {0, 10}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    FilletPreviewCommand f;
    f.first = {8, 0};
    f.at = {0, 8};
    f.radius = 2.0;
    f.pick_radius = 0.5;
    f.active = true;
    engine.submit(f);
    // The two shortened lines and the arc, as ghosts; the lines themselves as they were.
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grip_preview_segments.size() > 4; }));
    REQUIRE(engine.snapshot().line_vertices.size() == 4);
    f.chamfer = true;
    f.d1 = 2.0;
    f.d2 = 2.0;
    engine.submit(f);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grip_preview_segments.size() == 6; })); // 2 lines + the bevel
    engine.submit(FilletPreviewCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grip_preview_segments.empty(); }));
    engine.stop();

    struct Out : CommandOutput {
        void append_line(const std::string&) override {}
        void set_prompt(const std::string&) override {}
    } out;
    std::vector<Command> cmds;
    CommandProcessor proc{[&](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    proc.submit_line("FILLET");
    REQUIRE_FALSE(proc.preview().fillet_hover);
    proc.submit_line("8,0");
    REQUIRE(proc.preview().fillet_hover);
    REQUIRE(proc.preview().fillet_first == Vec2{8, 0});
    proc.submit_line("0,8");
    REQUIRE_FALSE(proc.preview().fillet_hover);
    proc.submit_line("CHAMFER");
    proc.submit_line("8,0");
    REQUIRE(proc.preview().fillet_hover);
    REQUIRE(proc.preview().fillet_chamfer);
    proc.cancel();
}

TEST_CASE("FILLET a line with a polyline's end segment: one polyline, the corner rounded") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    AddPolylineCommand pl;
    pl.points = {{20, 20}, {12, 20}, {12, 8}};
    pl.group = 2;
    engine.submit(pl);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() >= 6; }));
    engine.submit(FilletPickCommand{{5, 0}, {12, 14}, 2.0, 0.5, ++g});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Filleted."; }));
    const io::Document doc = dump(engine, "musacad_trim_curves_join.musa");
    REQUIRE(doc.lines.empty());
    REQUIRE(doc.polylines.size() == 1);
    const io::DocPolyline& p = doc.polylines[0];
    REQUIRE(p.points.size() == 5);
    REQUIRE(p.points[0] == Vec2{0, 0});
    REQUIRE(p.points[1].x == Approx(10.0).margin(1e-9));
    REQUIRE(p.points[2].x == Approx(12.0).margin(1e-9));
    REQUIRE(p.points[2].y == Approx(2.0).margin(1e-9));
    REQUIRE(p.points[4] == Vec2{20, 20});
    REQUIRE(std::abs(p.bulges[1]) > 0.0); // the rounding arc
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() >= 6; }));
    engine.stop();
}

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// A spline drawn by fit points keeps them (#39): SPLINE's Fit method records them with the
// knots, tolerance, closure and end tangents; the grips are on them and a fit point dragged
// fits the spline again; MOVE, ROTATE and REVERSE take them along, an edit of the control
// vertices drops them; the drawing file and DXF keep them (AutoCAD's 11, 44, 12 and 13).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/spline_eval.hpp"

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

bool near(Vec2 p, Vec2 q, double eps = 1e-9) { return std::abs(p.x - q.x) < eps && std::abs(p.y - q.y) < eps; }

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
    void run(std::initializer_list<const char*> lines) {
        for (const char* l : lines) {
            proc.submit_line(l);
        }
    }
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

const std::vector<Vec2> kThrough{{0, 0}, {10, 10}, {20, 0}, {30, 10}};

AddSplineCommand fitted(std::uint64_t group, std::optional<Vec2> start = std::nullopt) {
    AddSplineCommand s;
    s.fit.points = kThrough;
    s.fit.start_tangent = start;
    s.control_points = spline::fit_spline(kThrough, 3, spline::FitParam::Chord, 0.0, false, start, std::nullopt);
    s.group = group;
    return s;
}

// Whether the curve of `ctrl` passes through `p`: near enough to its tessellation's segments.
bool passes(const std::vector<Vec2>& ctrl, Vec2 p) {
    std::vector<Vec2> curve;
    spline::tessellate(ctrl, 3, curve);
    double best = 1e30;
    for (std::size_t i = 0; i + 1 < curve.size(); ++i) {
        const Vec2 ab = curve[i + 1] - curve[i];
        const double l2 = dot(ab, ab);
        const double t = l2 > 0.0 ? std::clamp(dot(p - curve[i], ab) / l2, 0.0, 1.0) : 0.0;
        best = std::min(best, distance(curve[i] + ab * t, p));
    }
    return best < 0.05;
}
} // namespace

TEST_CASE("SPLINE's Fit method keeps the points it fits; by control vertices, none") {
    H h;
    h.run({"SPLINE", "M", "F", "0,0", "10,10", "20,0", "30,10", ""});
    const auto* sp = h.last<AddSplineCommand>();
    REQUIRE(sp != nullptr);
    REQUIRE(sp->fit.points == kThrough);
    REQUIRE_FALSE(sp->fit.closed);
    h.run({"SPLINE", "M", "F", "0,0", "10,0", "10,10", "0,10", "C"});
    sp = h.last<AddSplineCommand>();
    REQUIRE(sp->fit.closed);
    REQUIRE(sp->fit.points.size() == 4); // the first not repeated
    // The same rule draws it again: the control points from the fit data.
    REQUIRE(spline::fit_spline(sp->fit.points, 3, spline::FitParam::Chord, sp->fit.tolerance, true, std::nullopt,
                               std::nullopt) == sp->control_points);
    h.run({"SPLINE", "M", "CV", "0,0", "10,10", "20,0", ""});
    REQUIRE(h.last<AddSplineCommand>()->fit.empty());
    h.run({"SPLINE", "M", "F", ""}); // leave the method as the next test expects
}

TEST_CASE("A fitted spline's grips are its fit points; dragging one fits it again through the new place") {
    GeometryStore store;
    const AddSplineCommand s = fitted(0);
    const EntityHandle h = add_command_to_store(store, s, {});
    REQUIRE(store.fit_of(*store.spline(h)) != nullptr);
    std::vector<Grip> grips;
    grips_of(store, h, grips);
    REQUIRE(grips.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        REQUIRE(near(grips[i].pos, kThrough[i]));
    }
    const Command moved = edit_for_grip_drag(store, h, grips[1].index, {10, 20});
    const auto* sp = std::get_if<AddSplineCommand>(&moved);
    REQUIRE(sp != nullptr);
    REQUIRE(sp->fit.points[1] == Vec2{10, 20});
    REQUIRE(passes(sp->control_points, {10, 20}));
    REQUIRE(passes(sp->control_points, {30, 10}));
    REQUIRE(sp->knots.empty());
}

TEST_CASE("MOVE, ROTATE and REVERSE take a spline's fit data along; SPLINEDIT on its control vertices drops it") {
    GeometryEngine engine;
    engine.start();
    engine.submit(fitted(1, Vec2{0, 1}));
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    io::Document doc = dump(engine, "musacad_splinefit1.musa");
    REQUIRE(doc.splines.size() == 1);
    REQUIRE(doc.splines[0].fit.points == kThrough);
    REQUIRE(doc.splines[0].fit.start_tangent == std::optional<Vec2>(Vec2{0, 1}));

    engine.submit(SelectAllCommand{});
    engine.submit(MoveSelectionCommand{{5, 5}, 2});
    engine.submit(RotateSelectionCommand{{5, 5}, kPi / 2.0, 3});
    doc = dump(engine, "musacad_splinefit2.musa");
    const SplineFit f = doc.splines[0].fit; // a copy: `doc` is read again below
    REQUIRE(near(f.points[0], {5, 5}));
    REQUIRE(near(f.points[1], {-5, 15}, 1e-9)); // (15, 15) turned a quarter about (5, 5)
    REQUIRE(f.start_tangent);
    REQUIRE(f.start_tangent->x == Approx(-1.0));
    REQUIRE(f.start_tangent->y == Approx(0.0).margin(1e-12));

    engine.submit(ReverseSelectionCommand{4});
    doc = dump(engine, "musacad_splinefit3.musa");
    REQUIRE(near(doc.splines[0].fit.points.front(), f.points.back()));
    REQUIRE_FALSE(doc.splines[0].fit.start_tangent);
    REQUIRE(doc.splines[0].fit.end_tangent);
    REQUIRE(doc.splines[0].fit.end_tangent->x == Approx(1.0));

    // SPLINEDIT moves a control vertex: the spline is one by control vertices now.
    const Vec2 cv = doc.splines[0].control_points[1];
    engine.submit(SplineEditCommand{SplineEditCommand::Op::Begin, doc.splines[0].control_points[0], 1.0, {}, {}, 10, 5});
    engine.submit(SplineEditCommand{SplineEditCommand::Op::MoveVertex, {}, 0.0, cv, cv + Vec2{1, 1}, 10, 6});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Control point moved."; }));
    engine.submit(SplineEditCommand{SplineEditCommand::Op::End, {}, 0.0, {}, {}, 10, 0});
    doc = dump(engine, "musacad_splinefit4.musa");
    REQUIRE(doc.splines[0].fit.empty());
    engine.stop();
}

TEST_CASE("The drawing file and DXF keep a spline's fit data; AutoCAD's fit-point splines come in with theirs") {
    io::Document doc;
    io::DocSpline open;
    open.fit.points = kThrough;
    open.fit.knots = 1;
    open.fit.tolerance = 0.25;
    open.fit.start_tangent = Vec2{1, 0};
    open.fit.end_tangent = Vec2{0, -1};
    open.control_points = spline::fit_spline(kThrough, 3, spline::FitParam::SquareRoot, 0.25, false, Vec2{1, 0}, Vec2{0, -1});
    doc.splines.push_back(open);
    io::DocSpline round;
    round.fit.points = {{0, 50}, {10, 50}, {10, 60}, {0, 60}};
    round.fit.closed = true;
    round.control_points = spline::fit_spline(round.fit.points, 3, spline::FitParam::Chord, 0.0, true, std::nullopt, std::nullopt);
    doc.splines.push_back(round);

    io::Document native;
    REQUIRE(io::parse_native(io::serialize_native(doc), native).ok);
    REQUIRE(native.splines == doc.splines);

    const std::string dxf = io::serialize_dxf(doc);
    REQUIRE(dxf.find("\n74\n4\n") != std::string::npos);
    REQUIRE(dxf.find("\n74\n5\n") != std::string::npos); // closed: the first point again
    REQUIRE(dxf.find("\n44\n0.25\n") != std::string::npos);
    io::Document back;
    REQUIRE(io::parse_dxf(dxf, back).ok);
    REQUIRE(back.splines.size() == 2);
    REQUIRE(back.splines[0].fit.points == kThrough);
    REQUIRE(back.splines[0].fit.tolerance == Approx(0.25));
    REQUIRE(back.splines[0].fit.start_tangent == std::optional<Vec2>(Vec2{1, 0}));
    REQUIRE(back.splines[0].fit.end_tangent == std::optional<Vec2>(Vec2{0, -1}));
    REQUIRE(back.splines[1].fit.closed);
    REQUIRE(back.splines[1].fit.points.size() == 4);

    // AutoCAD's: fit points only, closed by its flag, a tolerance of 1e-10 (through them).
    const std::string acad =
        "0\nSECTION\n2\nENTITIES\n0\nSPLINE\n8\n0\n70\n9\n71\n3\n72\n0\n73\n0\n74\n4\n44\n0.0000000001\n"
        "11\n0\n21\n0\n31\n0\n11\n10\n21\n0\n31\n0\n11\n10\n21\n10\n31\n0\n11\n0\n21\n10\n31\n0\n"
        "0\nENDSEC\n0\nEOF\n";
    io::Document a;
    REQUIRE(io::parse_dxf(acad, a).ok);
    REQUIRE(a.splines.size() == 1);
    REQUIRE(a.splines[0].fit.closed);
    REQUIRE(a.splines[0].fit.tolerance == 0.0);
    REQUIRE(a.splines[0].control_points.front() == a.splines[0].control_points.back());
    REQUIRE(passes(a.splines[0].control_points, {10, 10}));
}

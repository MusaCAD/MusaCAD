// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// SPLINE (issue #23): the shared B-spline evaluation, fit-point interpolation, the
// entity's editing machinery (it was capture-less before: moving one produced an empty
// line), DXF in both directions, and the command's Fit/CV/Knots/Degree/Undo/Close paths.

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
#include "musacad/core/grips.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/spline_eval.hpp"

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
    const AddSplineCommand* only() const {
        const AddSplineCommand* found = nullptr;
        int n = 0;
        for (const Command& c : cmds) {
            if (const auto* p = std::get_if<AddSplineCommand>(&c)) {
                found = p;
                ++n;
            }
        }
        return n == 1 ? found : nullptr;
    }
};
bool near(Vec2 a, Vec2 b, double tol = 1e-6) {
    return std::abs(a.x - b.x) <= tol && std::abs(a.y - b.y) <= tol;
}
// Does the spline through `ctrl` pass through every fit point at the parameters the
// given mode assigns them? (The definition of interpolation.)
bool passes_through(const std::vector<Vec2>& ctrl, const std::vector<Vec2>& fit, int degree,
                    spline::FitParam mode) {
    const std::vector<double> u = spline::fit_parameters(fit, mode);
    for (std::size_t i = 0; i < fit.size(); ++i) {
        if (!near(spline::evaluate(ctrl, degree, u[i]), fit[i], 1e-6)) {
            return false;
        }
    }
    return true;
}
} // namespace

TEST_CASE("#23 SPLINE eval: clamped ends, effective degree, and exact fit-point interpolation") {
    const std::vector<Vec2> ctrl{{0, 0}, {1, 2}, {3, 2}, {4, 0}};
    REQUIRE(near(spline::evaluate(ctrl, 3, 0.0), {0, 0}));
    REQUIRE(near(spline::evaluate(ctrl, 3, 1.0), {4, 0}));
    std::vector<Vec2> tess;
    spline::tessellate(ctrl, 3, tess);
    REQUIRE(tess.size() >= 17);
    REQUIRE(near(tess.front(), {0, 0}));
    REQUIRE(near(tess.back(), {4, 0}));
    REQUIRE(spline::effective_degree(2, 3) == 1); // too few points: degree drops
    REQUIRE(spline::effective_degree(6, 3) == 3);

    // Interpolation: through 5 points, every parameterisation, degree 3 and 2.
    const std::vector<Vec2> fit{{0, 0}, {10, 8}, {20, -3}, {35, 12}, {50, 0}};
    for (const spline::FitParam mode :
         {spline::FitParam::Chord, spline::FitParam::SquareRoot, spline::FitParam::Uniform}) {
        std::vector<Vec2> c;
        REQUIRE(spline::fit_control_points(fit, 3, mode, c));
        REQUIRE(c.size() == fit.size());
        REQUIRE(passes_through(c, fit, 3, mode));
        REQUIRE(spline::fit_control_points(fit, 2, mode, c));
        REQUIRE(passes_through(c, fit, 2, mode));
    }
    // Two points: a straight segment (degree 1) through both.
    std::vector<Vec2> two;
    REQUIRE(spline::fit_control_points(std::vector<Vec2>{{0, 0}, {5, 5}}, 3, spline::FitParam::Chord, two));
    REQUIRE(near(spline::evaluate(two, 3, 0.5), {2.5, 2.5}));
    // Coincident points: chord parameterisation degenerates and falls back to uniform.
    const std::vector<Vec2> same{{1, 1}, {1, 1}, {1, 1}};
    const std::vector<Vec2> fb = spline::fit_or_fallback(same, 3, spline::FitParam::Chord);
    REQUIRE(fb.size() == 3);
}

TEST_CASE("#23 SPLINE entity: created, drawn, moved (stays a spline), gripped") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddSplineCommand{{{0, 0}, {10, 20}, {20, -20}, {30, 0}}, 3, 1, {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() >= 32; }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().bounds_max.x == Approx(30.0).margin(1e-6));

    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    REQUIRE(engine.snapshot().selection[0].kind == EntityKind::Spline);
    engine.submit(MoveSelectionCommand{{100, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.bounds_min.x > 99.9; }));
    // Still one spline (the capture bug used to turn it into an empty line).
    REQUIRE(engine.snapshot().selection.size() == 1);
    REQUIRE(engine.snapshot().selection[0].kind == EntityKind::Spline);
    REQUIRE(engine.snapshot().bounds_max.x == Approx(130.0).margin(1e-6));
    engine.stop();

    GeometryStore s;
    const std::vector<Vec2> cp{{0, 0}, {10, 20}, {20, -20}, {30, 0}};
    const EntityHandle h = s.add_spline(cp, 3);
    std::vector<Grip> g;
    grips_of(s, h, g);
    REQUIRE(g.size() == 4);
    REQUIRE(g[1].kind == GripKind::Vertex);
    const Command c = edit_for_grip_drag(s, h, 1, {10, 50});
    const auto* sp = std::get_if<AddSplineCommand>(&c);
    REQUIRE(sp != nullptr);
    REQUIRE(near(sp->control_points[1], {10, 50}));
    REQUIRE(sp->degree == 3);
}

TEST_CASE("#23 SPLINE DXF: export writes our knots; import keeps a uniform spline, fits fit points, keeps a non-uniform one with its knots") {
    GeometryStore store;
    store.add_spline(std::vector<Vec2>{{0, 0}, {10, 20}, {20, -20}, {30, 0}, {40, 5}}, 3);
    io::Document doc = io::document_from_store(store);
    REQUIRE(doc.splines.size() == 1);
    const std::filesystem::path d = std::filesystem::temp_directory_path() / "musacad_spline.dxf";
    REQUIRE(io::save_dxf(doc, d.string()).ok);
    io::Document back;
    REQUIRE(io::load_dxf(d.string(), back).ok);
    REQUIRE(back.polylines.empty());
    REQUIRE(back.splines.size() == 1);
    REQUIRE(back.splines[0].control_points.size() == 5);
    REQUIRE(near(back.splines[0].control_points[2], {20, -20}));
    std::filesystem::remove(d);

    // Fit points only (codes 11/21): interpolated into a real spline through them.
    {
        const std::string dxf =
            "0\nSECTION\n2\nENTITIES\n0\nSPLINE\n8\n0\n71\n3\n"
            "11\n0.0\n21\n0.0\n31\n0.0\n11\n10.0\n21\n8.0\n31\n0.0\n"
            "11\n20.0\n21\n-3.0\n31\n0.0\n11\n30.0\n21\n0.0\n31\n0.0\n"
            "0\nENDSEC\n0\nEOF\n";
        const std::filesystem::path f = std::filesystem::temp_directory_path() / "musacad_fit.dxf";
        {
            std::ofstream out(f);
            out << dxf;
        }
        io::Document fd;
        REQUIRE(io::load_dxf(f.string(), fd).ok);
        REQUIRE(fd.splines.size() == 1);
        const std::vector<Vec2> fit{{0, 0}, {10, 8}, {20, -3}, {30, 0}};
        REQUIRE(passes_through(fd.splines[0].control_points, fit, 3, spline::FitParam::Chord));
        std::filesystem::remove(f);
    }
    // A non-uniform knot vector: a real spline with those knots, written back as they were.
    {
        const std::string dxf =
            "0\nSECTION\n2\nENTITIES\n0\nSPLINE\n8\n0\n71\n3\n72\n8\n73\n4\n"
            "40\n0\n40\n0\n40\n0\n40\n0\n40\n0.2\n40\n1\n40\n1\n40\n1\n"
            "10\n0.0\n20\n0.0\n30\n0.0\n10\n1.0\n20\n2.0\n30\n0.0\n"
            "10\n3.0\n20\n2.0\n30\n0.0\n10\n4.0\n20\n0.0\n30\n0.0\n"
            "0\nENDSEC\n0\nEOF\n";
        const std::filesystem::path f = std::filesystem::temp_directory_path() / "musacad_nonuni.dxf";
        {
            std::ofstream out(f);
            out << dxf;
        }
        io::Document nd;
        REQUIRE(io::load_dxf(f.string(), nd).ok);
        REQUIRE(nd.polylines.empty());
        REQUIRE(nd.splines.size() == 1);
        REQUIRE(nd.splines[0].knots == std::vector<double>{0, 0, 0, 0, 0.2, 1, 1, 1});
        REQUIRE(io::serialize_dxf(nd).find("\n40\n0.2\n") != std::string::npos);
        std::filesystem::remove(f);
    }
}

TEST_CASE("#23 SPLINE command: Fit passes through the points; CV takes them as vertices; Knots, Degree, Undo, Close") {
    const std::vector<Vec2> pts{{0, 0}, {10, 10}, {20, 0}, {30, 10}};
    { // Fit (explicitly set, since the method persists for the session), chord knots.
        ProcHarness h;
        h.proc.submit_line("SPL");
        h.proc.submit_line("M");
        h.proc.submit_line("F");
        h.proc.submit_line("K");
        h.proc.submit_line("C");
        for (const Vec2& p : pts) {
            h.proc.submit_line(std::to_string(p.x) + "," + std::to_string(p.y));
        }
        h.proc.submit_line("");
        const auto* sp = h.only();
        REQUIRE(sp != nullptr);
        REQUIRE(sp->degree == 3);
        REQUIRE(sp->control_points.size() == 4);
        REQUIRE(passes_through(sp->control_points, pts, 3, spline::FitParam::Chord));
        REQUIRE(!near(sp->control_points[1], pts[1])); // interpolated, not the raw point
    }
    { // Uniform knots: interpolation at uniform parameters.
        ProcHarness h;
        h.proc.submit_line("SPLINE");
        h.proc.submit_line("K");
        h.proc.submit_line("U");
        for (const Vec2& p : pts) {
            h.proc.submit_line(std::to_string(p.x) + "," + std::to_string(p.y));
        }
        h.proc.submit_line("");
        const auto* sp = h.only();
        REQUIRE(sp != nullptr);
        REQUIRE(passes_through(sp->control_points, pts, 3, spline::FitParam::Uniform));
    }
    { // Close: the curve returns to the first point.
        ProcHarness h;
        h.proc.submit_line("SPLINE");
        h.proc.submit_line("K");
        h.proc.submit_line("C");
        for (const Vec2& p : pts) {
            h.proc.submit_line(std::to_string(p.x) + "," + std::to_string(p.y));
        }
        h.proc.submit_line("C");
        const auto* sp = h.only();
        REQUIRE(sp != nullptr);
        REQUIRE(sp->control_points.size() == 7); // five points passed through, two for the seam's direction
        REQUIRE(near(spline::evaluate(sp->control_points, 3, 1.0), {0, 0}));
    }
    { // Undo drops the last point; then finishing uses the remaining three.
        ProcHarness h;
        h.proc.submit_line("SPLINE");
        for (const Vec2& p : pts) {
            h.proc.submit_line(std::to_string(p.x) + "," + std::to_string(p.y));
        }
        h.proc.submit_line("U");
        h.proc.submit_line("");
        const auto* sp = h.only();
        REQUIRE(sp != nullptr);
        REQUIRE(sp->control_points.size() == 3);
        REQUIRE(near(spline::evaluate(sp->control_points, 3, 1.0), {20, 0}));
    }
    { // CV with Degree 2: the picked points ARE the control vertices.
        ProcHarness h;
        h.proc.submit_line("SPLINE");
        h.proc.submit_line("M");
        h.proc.submit_line("CV");
        h.proc.submit_line("D");
        h.proc.submit_line("2");
        for (const Vec2& p : pts) {
            h.proc.submit_line(std::to_string(p.x) + "," + std::to_string(p.y));
        }
        h.proc.submit_line("");
        const auto* sp = h.only();
        REQUIRE(sp != nullptr);
        REQUIRE(sp->degree == 2);
        REQUIRE(sp->control_points.size() == 4);
        REQUIRE(near(sp->control_points[1], pts[1]));
    }
    { // The method persists for the session: the next SPLINE starts in CV.
        ProcHarness h;
        h.proc.submit_line("SPLINE");
        for (const Vec2& p : pts) {
            h.proc.submit_line(std::to_string(p.x) + "," + std::to_string(p.y));
        }
        h.proc.submit_line("");
        const auto* sp = h.only();
        REQUIRE(sp != nullptr);
        REQUIRE(near(sp->control_points[2], pts[2]));
    }
    { // Back to Fit (leave the session default as AutoCAD's), degree back to 3.
        ProcHarness h;
        h.proc.submit_line("SPLINE");
        h.proc.submit_line("M");
        h.proc.submit_line("F");
        h.proc.submit_line("0,0");
        h.proc.submit_line("5,5");
        h.proc.submit_line("");
        REQUIRE(h.only() != nullptr);
    }
    { // A single point then Enter: nothing is created and the command ends.
        ProcHarness h;
        h.proc.submit_line("SPLINE");
        h.proc.submit_line("0,0");
        h.proc.submit_line("");
        REQUIRE(h.only() == nullptr);
        REQUIRE(!h.proc.has_active_command());
    }
}

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// PEDIT Spline and the frame a fitted polyline keeps (#52): the B-spline on the vertices
// placed (SPLINETYPE, SPLINESEGS), Decurve back to them, grips and the vertex editor on the
// frame, the vertex Tangent a Fit follows, SPLINE Object, MOVE / ROTATE / MIRROR / SCALE / REVERSE,
// and the frame kept in the drawing file and in DXF (a POLYLINE with its frame vertices).

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
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
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/polyline_ops.hpp"

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

Vec2 turned(Vec2 v, double a) { return {v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a)}; }

/// The direction an arc segment leaves its start.
Vec2 leaving(Vec2 p, Vec2 q, double bulge) { return turned(normalized(q - p), -2.0 * std::atan(bulge)); }

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
    [[nodiscard]] std::vector<PeditCommand> pedits() const {
        std::vector<PeditCommand> out_cmds;
        for (const Command& c : cmds) {
            if (const auto* p = std::get_if<PeditCommand>(&c)) {
                out_cmds.push_back(*p);
            }
        }
        return out_cmds;
    }
};

const std::vector<Vec2> kZigzag{{0, 0}, {10, 10}, {20, 0}, {30, 10}};

void zigzag_spline(GeometryEngine& engine, double type = 6.0, double segs = 8.0) {
    engine.submit(AddPolylineCommand{kZigzag, false, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(PeditCommand{{5, 5}, 5.0, 4, {type, segs}, {}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Spline fit."; }));
}
} // namespace

TEST_CASE("Spline fit: from the first vertex placed to the last; closed, round on the frame") {
    const std::vector<Vec2> open = polyline_ops::spline_fit(kZigzag, false, 3, 8);
    REQUIRE(open.size() == 25); // 8 pieces a frame segment
    REQUIRE(near(open.front(), {0, 0}));
    REQUIRE(near(open.back(), {30, 10}));
    // Within the frame's box, and not through the middle vertices (an approximation).
    for (const Vec2& p : open) {
        REQUIRE(p.x >= -1e-9);
        REQUIRE(p.x <= 30 + 1e-9);
        REQUIRE(p.y >= -1e-9);
        REQUIRE(p.y <= 10 + 1e-9);
    }
    REQUIRE_FALSE(near(open[8], {10, 10}, 1e-3));
    const std::vector<Vec2> quad = polyline_ops::spline_fit(kZigzag, false, 2, 4);
    REQUIRE(quad.size() == 13);
    REQUIRE(near(quad.back(), {30, 10}));

    // Closed on a square: a periodic cubic, at each vertex's knot (P- + 4P + P+) / 6.
    const std::vector<Vec2> square{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    const std::vector<Vec2> round = polyline_ops::spline_fit(square, true, 3, 8);
    REQUIRE(round.size() == 32);
    for (std::size_t i = 0; i < 4; ++i) {
        const Vec2 expect = (square[(i + 3) % 4] + square[i] * 4.0 + square[(i + 1) % 4]) * (1.0 / 6.0);
        bool found = false;
        for (const Vec2& p : round) {
            found = found || near(p, expect, 1e-9);
        }
        REQUIRE(found);
    }
}

TEST_CASE("PEDIT Spline keeps the frame; Decurve goes back to it; Fit and Decurve the same") {
    GeometryEngine engine;
    engine.start();
    zigzag_spline(engine);
    io::Document doc = dump(engine, "musacad_spline_fit1.musa");
    REQUIRE(doc.polylines.size() == 1);
    REQUIRE(doc.polylines[0].fit == 6);
    REQUIRE(doc.polylines[0].frame == kZigzag);
    REQUIRE(doc.polylines[0].points.size() == 25);
    REQUIRE(near(doc.polylines[0].points.back(), {30, 10}));

    engine.submit(PeditCommand{{5, 5}, 5.0, 3, {}, {}, 3}); // Decurve
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Decurved."; }));
    doc = dump(engine, "musacad_spline_fit2.musa");
    REQUIRE(doc.polylines[0].fit == 0);
    REQUIRE(doc.polylines[0].frame.empty());
    REQUIRE(doc.polylines[0].points == kZigzag);

    // Quadratic, 4 pieces a segment; undone, the cubic is back.
    engine.submit(UndoLastGroupCommand{});
    engine.submit(PeditCommand{{5, 5}, 5.0, 4, {5.0, 4.0}, {}, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Spline fit."; }));
    doc = dump(engine, "musacad_spline_fit3.musa");
    REQUIRE(doc.polylines[0].fit == 5);
    REQUIRE(doc.polylines[0].frame == kZigzag); // splined again from the frame, not the curve
    REQUIRE(doc.polylines[0].points.size() == 13);

    // Fit on the spline: the arcs through the vertices placed; Decurve, those vertices again.
    engine.submit(PeditCommand{{5, 5}, 5.0, 10, {}, {}, 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Fit."; }));
    doc = dump(engine, "musacad_spline_fit4.musa");
    REQUIRE(doc.polylines[0].fit == 1);
    REQUIRE(doc.polylines[0].frame == kZigzag);
    REQUIRE(doc.polylines[0].points.size() == 7);
    REQUIRE(near(doc.polylines[0].points[2], {10, 10}));
    engine.submit(PeditCommand{{5, 5}, 5.0, 3, {}, {}, 6});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Decurved."; }));
    doc = dump(engine, "musacad_spline_fit5.musa");
    REQUIRE(doc.polylines[0].points == kZigzag);
    for (const double b : doc.polylines[0].bulges) {
        REQUIRE(b == 0.0);
    }
    engine.stop();
}

TEST_CASE("A spline-fit polyline: Close and Reverse on the frame, the vertex editor moves a frame vertex") {
    GeometryEngine engine;
    engine.start();
    zigzag_spline(engine);
    engine.submit(PeditCommand{{5, 5}, 5.0, 0, {}, {}, 3}); // Close
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("Closed") != std::string::npos; }));
    io::Document doc = dump(engine, "musacad_spline_close.musa");
    REQUIRE(doc.polylines[0].closed);
    REQUIRE(doc.polylines[0].fit == 6);
    REQUIRE(doc.polylines[0].frame == kZigzag);
    REQUIRE(doc.polylines[0].points.size() == 32); // round: 8 a frame segment, 4 of them
    engine.submit(PeditCommand{{5, 5}, 5.0, 1, {}, {}, 4}); // Open
    engine.submit(PeditCommand{{5, 5}, 5.0, 2, {}, {}, 5}); // Reverse
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Reversed."; }));
    doc = dump(engine, "musacad_spline_reverse.musa");
    REQUIRE_FALSE(doc.polylines[0].closed);
    REQUIRE(doc.polylines[0].frame == std::vector<Vec2>(kZigzag.rbegin(), kZigzag.rend()));
    REQUIRE(near(doc.polylines[0].points.front(), {30, 10}));

    // The vertex editor walks the frame: the second vertex placed (20, 0) moved to (20, -30).
    engine.submit(PeditCommand{{5, 5}, 5.0, 11, {}, {}, 0});
    engine.submit(PeditCommand{{5, 5}, 5.0, 12, {}, {}, 0});
    engine.submit(PeditCommand{{5, 5}, 5.0, 19, {20, -30}, {}, 6});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Vertex moved."; }));
    doc = dump(engine, "musacad_spline_vertex.musa");
    REQUIRE(doc.polylines[0].fit == 6);
    REQUIRE(doc.polylines[0].frame.size() == 4);
    REQUIRE(doc.polylines[0].frame[1] == Vec2{20, -30});
    REQUIRE(doc.polylines[0].points.size() == 25);
    double lowest = 0.0;
    for (const Vec2& p : doc.polylines[0].points) {
        lowest = std::min(lowest, p.y);
    }
    REQUIRE(lowest < -5.0); // the curve follows the frame down (about -7.9)
    engine.stop();
}

TEST_CASE("A fitted polyline's grips are its frame; dragging one fits it again") {
    GeometryStore store;
    std::vector<Vec2> pts = polyline_ops::spline_fit(kZigzag, false, 3, 8);
    const EntityHandle h = store.add_polyline(pts, false);
    REQUIRE(store.set_polyline_fit(h, 6, kZigzag));
    std::vector<Grip> grips;
    grips_of(store, h, grips);
    REQUIRE(grips.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        REQUIRE(near(grips[i].pos, kZigzag[i]));
    }
    const Command moved = edit_for_grip_drag(store, h, grips[1].index, {10, 20});
    const auto* pl = std::get_if<AddPolylineCommand>(&moved);
    REQUIRE(pl != nullptr);
    REQUIRE(pl->fit == 6);
    REQUIRE(pl->frame[1] == Vec2{10, 20});
    REQUIRE(pl->points.size() == 25);
    REQUIRE(near(pl->points.front(), {0, 0}));
    REQUIRE(near(pl->points.back(), {30, 10}));
}

TEST_CASE("The vertex Tangent: a Fit leaves the vertex along it, and it is kept") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {10, 0}, {20, 0}}, false, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(PeditCommand{{0, 0}, 1.0, 11, {}, {}, 0});
    engine.submit(PeditCommand{{0, 0}, 1.0, 12, {}, {}, 0});
    engine.submit(PeditCommand{{0, 0}, 1.0, 23, {kPi / 2.0, 0.0}, {}, 2}); // straight up
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Tangent set."; }));
    engine.submit(PeditCommand{{0, 0}, 1.0, 17, {}, {}, 0}); // eXit
    io::Document doc = dump(engine, "musacad_tangent1.musa");
    REQUIRE(doc.polylines[0].tangents.size() == 3);
    REQUIRE(std::isnan(doc.polylines[0].tangents[0]));
    REQUIRE(doc.polylines[0].tangents[1] == Approx(kPi / 2.0));
    REQUIRE(doc.polylines[0].points.size() == 3); // not fitted yet

    engine.submit(PeditCommand{{0, 0}, 1.0, 10, {}, {}, 3}); // Fit
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Fit."; }));
    doc = dump(engine, "musacad_tangent2.musa");
    const io::DocPolyline& p = doc.polylines[0];
    REQUIRE(p.points.size() == 5);
    REQUIRE(near(p.points[2], {10, 0}));
    const Vec2 out = leaving(p.points[2], p.points[3], p.bulges[2]);
    REQUIRE(out.x == Approx(0.0).margin(1e-9));
    REQUIRE(out.y == Approx(1.0));

    // A tangent set on a fitted polyline (a point given: towards it) is followed at once.
    engine.submit(PeditCommand{{0, 0}, 1.0, 11, {}, {}, 0});
    engine.submit(PeditCommand{{0, 0}, 1.0, 12, {}, {}, 0});
    engine.submit(PeditCommand{{0, 0}, 1.0, 23, {0.0, 1.0}, {20, 10}, 4}); // 45 degrees
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Tangent set."; }));
    engine.submit(PeditCommand{{0, 0}, 1.0, 17, {}, {}, 0});
    doc = dump(engine, "musacad_tangent3.musa");
    const io::DocPolyline& q = doc.polylines[0];
    REQUIRE(q.fit == 1);
    REQUIRE(q.tangents[1] == Approx(kPi / 4.0));
    const Vec2 out45 = leaving(q.points[2], q.points[3], q.bulges[2]);
    REQUIRE(out45.x == Approx(std::sqrt(0.5)));
    REQUIRE(out45.y == Approx(std::sqrt(0.5)));

    // ROTATE turns the tangent with it; MIRROR reflects it.
    engine.submit(SelectAllCommand{});
    engine.submit(RotateSelectionCommand{{0, 0}, kPi / 2.0, 5});
    doc = dump(engine, "musacad_tangent4.musa");
    REQUIRE(doc.polylines[0].frame[1].x == Approx(0.0).margin(1e-9));
    REQUIRE(doc.polylines[0].frame[1].y == Approx(10.0));
    REQUIRE(doc.polylines[0].tangents[1] == Approx(3.0 * kPi / 4.0));
    engine.submit(MirrorSelectionCommand{{0, 0}, {0, 10}, true, 6}); // about the y axis
    doc = dump(engine, "musacad_tangent5.musa");
    REQUIRE(doc.polylines.size() == 1);
    const Vec2 dir{std::cos(doc.polylines[0].tangents[1]), std::sin(doc.polylines[0].tangents[1])};
    REQUIRE(dir.x == Approx(std::sqrt(0.5)));
    REQUIRE(dir.y == Approx(std::sqrt(0.5)));
    // REVERSE: the frame the other way, the tangent turned round.
    const std::vector<Vec2> frame = doc.polylines[0].frame;
    engine.submit(SelectAllCommand{});
    engine.submit(ReverseSelectionCommand{7});
    doc = dump(engine, "musacad_tangent6.musa");
    REQUIRE(doc.polylines[0].frame == std::vector<Vec2>(frame.rbegin(), frame.rend()));
    REQUIRE(std::cos(doc.polylines[0].tangents[1]) == Approx(-std::sqrt(0.5)));
    REQUIRE(std::sin(doc.polylines[0].tangents[1]) == Approx(-std::sqrt(0.5)));
    engine.stop();
}

TEST_CASE("MOVE and SCALE take the frame along; SPLINE Object makes the same curve a spline") {
    GeometryEngine engine;
    engine.start();
    zigzag_spline(engine);
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(MoveSelectionCommand{{5, 5}, 3});
    engine.submit(ScaleSelectionCommand{{5, 5}, 2.0, 4});
    io::Document doc = dump(engine, "musacad_spline_xform.musa");
    REQUIRE(doc.polylines[0].fit == 6);
    REQUIRE(doc.polylines[0].frame[0] == Vec2{5, 5});
    REQUIRE(doc.polylines[0].frame[3].x == Approx(65.0));
    REQUIRE(doc.polylines[0].frame[3].y == Approx(25.0));
    REQUIRE(near(doc.polylines[0].points.back(), {65, 25}, 1e-9));

    engine.submit(PeditCommand{{10, 10}, 50.0, 25, {}, {}, 5}); // SPLINE Object
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Converted to a spline."; }));
    doc = dump(engine, "musacad_spline_object.musa");
    REQUIRE(doc.polylines.empty());
    REQUIRE(doc.splines.size() == 1);
    REQUIRE(doc.splines[0].degree == 3);
    REQUIRE(doc.splines[0].control_points.size() == 4);
    REQUIRE(doc.splines[0].control_points[0] == Vec2{5, 5});
    engine.stop();
}

TEST_CASE("DXF: a fitted polyline goes out as a POLYLINE with its frame, and comes back") {
    io::Document doc;
    io::DocPolyline spline;
    spline.frame = kZigzag;
    spline.fit = 6;
    spline.points = polyline_ops::spline_fit(kZigzag, false, 3, 8);
    doc.polylines.push_back(spline);
    io::DocPolyline fit;
    fit.frame = {{0, 50}, {10, 50}, {20, 50}};
    fit.fit = 1;
    fit.tangents = {std::numeric_limits<double>::quiet_NaN(), kPi / 2.0, std::numeric_limits<double>::quiet_NaN()};
    fit.points = fit.frame;
    fit.bulges.assign(3, 0.0);
    std::vector<double> widths;
    REQUIRE(polyline_ops::fit_arcs(fit.points, fit.bulges, widths, false, fit.tangents));
    doc.polylines.push_back(fit);

    const std::string dxf = io::serialize_dxf(doc);
    REQUIRE(dxf.find("LWPOLYLINE") == std::string::npos);
    REQUIRE(dxf.find("\n75\n6\n") != std::string::npos);
    REQUIRE(dxf.find("\n50\n") != std::string::npos); // the tangent, in degrees
    io::Document back;
    REQUIRE(io::parse_dxf(dxf, back).ok);
    REQUIRE(back.polylines.size() == 2);
    REQUIRE(back.polylines[0].fit == 6);
    REQUIRE(back.polylines[0].frame == kZigzag);
    REQUIRE(back.polylines[0].points.size() == 25);
    REQUIRE(back.polylines[1].fit == 1);
    REQUIRE(back.polylines[1].frame == fit.frame);
    REQUIRE(back.polylines[1].points.size() == 5);
    REQUIRE(back.polylines[1].tangents.size() == 3);
    REQUIRE(back.polylines[1].tangents[1] == Approx(kPi / 2.0));
    REQUIRE(std::isnan(back.polylines[1].tangents[2]));

    // The drawing file keeps them too.
    io::Document native;
    REQUIRE(io::parse_native(io::serialize_native(doc), native).ok);
    REQUIRE(native.polylines == doc.polylines);
}

TEST_CASE("DXF: AutoCAD's spline-fit and curve-fit POLYLINEs come in with their frames") {
    // Spline-fit (70 4, 75 5): the frame vertices (16), then the curve (8). Curve-fit (70 2):
    // the vertices placed carry the tangent (2, 50); the ones Fit added are flagged 1.
    const std::string dxf =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nPOLYLINE\n8\n0\n66\n1\n70\n4\n75\n5\n"
        "0\nVERTEX\n8\n0\n10\n0\n20\n0\n70\n16\n"
        "0\nVERTEX\n8\n0\n10\n10\n20\n10\n70\n16\n"
        "0\nVERTEX\n8\n0\n10\n20\n20\n0\n70\n16\n"
        "0\nVERTEX\n8\n0\n10\n0\n20\n0\n70\n8\n"
        "0\nVERTEX\n8\n0\n10\n10\n20\n5\n70\n8\n"
        "0\nVERTEX\n8\n0\n10\n20\n20\n0\n70\n8\n"
        "0\nSEQEND\n"
        "0\nPOLYLINE\n8\n0\n66\n1\n70\n2\n"
        "0\nVERTEX\n8\n0\n10\n0\n20\n0\n42\n0.2\n70\n2\n50\n30\n"
        "0\nVERTEX\n8\n0\n10\n5\n20\n1\n42\n0.2\n70\n1\n"
        "0\nVERTEX\n8\n0\n10\n10\n20\n0\n70\n2\n50\n-30\n"
        "0\nSEQEND\n0\nENDSEC\n0\nEOF\n";
    io::Document doc;
    REQUIRE(io::parse_dxf(dxf, doc).ok);
    REQUIRE(doc.polylines.size() == 2);
    const io::DocPolyline& s = doc.polylines[0];
    REQUIRE(s.fit == 5);
    REQUIRE(s.frame == std::vector<Vec2>{{0, 0}, {10, 10}, {20, 0}});
    REQUIRE(s.points == std::vector<Vec2>{{0, 0}, {10, 5}, {20, 0}});
    const io::DocPolyline& c = doc.polylines[1];
    REQUIRE(c.fit == 1);
    REQUIRE(c.frame == std::vector<Vec2>{{0, 0}, {10, 0}});
    REQUIRE(c.points.size() == 3);
    REQUIRE(c.tangents.size() == 2);
    REQUIRE(c.tangents[0] == Approx(kPi / 6.0));
    REQUIRE(c.tangents[1] == Approx(-kPi / 6.0));
}

TEST_CASE("PEDIT's Spline takes SPLINETYPE and SPLINESEGS; the vertex Tangent takes an angle or a point") {
    H h;
    h.run({"SPLINETYPE", "5"});
    h.run({"SPLINESEGS", "12"});
    h.run({"PE", "5,0", "S"});
    std::vector<PeditCommand> ps = h.pedits();
    REQUIRE(!ps.empty());
    REQUIRE(ps.back().op == 4);
    REQUIRE(ps.back().p1 == Vec2{5, 12});
    REQUIRE(h.out.prompt.rfind("Enter an option [", 0) == 0); // still in PEDIT
    h.run({"E", "T"});
    REQUIRE(h.out.prompt == "Specify direction of vertex tangent: ");
    h.run({"45"});
    ps = h.pedits();
    REQUIRE(ps.back().op == 23);
    REQUIRE(ps.back().p1.x == Approx(kPi / 4.0));
    REQUIRE(ps.back().p1.y == 0.0);
    h.run({"T", "3,4"});
    ps = h.pedits();
    REQUIRE(ps.back().op == 23);
    REQUIRE(ps.back().p1.y == 1.0);
    REQUIRE(ps.back().p2 == Vec2{3, 4});
    h.run({"X", ""});
    h.run({"SPLINETYPE", "6"});
    h.run({"SPLINESEGS", "8"});
}

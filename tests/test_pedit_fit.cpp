// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// PEDIT Fit and Multiple (#52), the turn-it-into-a-polyline question, PELLIPSE (#39), and
// REVCLOUD's Calligraphy style and remembered type (#40).

#include <chrono>
#include <cstdint>
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
    // Wait for THIS save: the status alone can be a stale "Saved" from the dump before,
    // when the command in between (an undo, say) reported nothing.
    engine.consume_snapshot();
    const std::uint64_t before = engine.snapshot().document_version;
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [before](const auto& s) { return s.document_version > before; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    std::filesystem::remove(p);
    return doc;
}

Vec2 turned(Vec2 v, double a) { return {v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a)}; }

/// The direction an arc segment leaves its start and arrives at its end.
std::pair<Vec2, Vec2> arc_tangents(Vec2 p, Vec2 q, double bulge) {
    const Vec2 chord = normalized(q - p);
    const double half = 2.0 * std::atan(bulge); // half the sweep
    return {turned(chord, -half), turned(chord, half)};
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
};
} // namespace

TEST_CASE("A biarc: two arcs meeting tangent, leaving and arriving along the tangents given") {
    // Up from the origin, down into (2,0): a half circle over the top, two quarter arcs.
    const polyline_ops::Biarc a = polyline_ops::biarc({0, 0}, {0, 1}, {2, 0}, {0, -1});
    REQUIRE(a.joint.x == Approx(1.0));
    REQUIRE(a.joint.y == Approx(1.0));
    REQUIRE(a.bulge0 == Approx(-std::tan(kPi / 8.0)));
    REQUIRE(a.bulge1 == Approx(-std::tan(kPi / 8.0)));
    // Parallel tangents: an S through the midpoint.
    const polyline_ops::Biarc s = polyline_ops::biarc({0, 0}, {1, 0}, {4, 2}, {1, 0});
    REQUIRE(s.joint == Vec2{2, 1});
    REQUIRE(s.bulge0 * s.bulge1 < 0.0);
}

TEST_CASE("PEDIT Fit passes every vertex, smooth at each one and at the joints between") {
    std::vector<Vec2> pts{{0, 0}, {10, 0}, {10, 10}, {20, 10}};
    std::vector<double> bulges;
    std::vector<double> widths{1, 1, 1, 3, 3, 3, 0, 0};
    REQUIRE(polyline_ops::fit_arcs(pts, bulges, widths, false));
    REQUIRE(pts.size() == 7); // a joint on each of the three segments
    REQUIRE(pts[0] == Vec2{0, 0});
    REQUIRE(pts[2] == Vec2{10, 0});
    REQUIRE(pts[4] == Vec2{10, 10});
    REQUIRE(pts[6] == Vec2{20, 10});
    REQUIRE(widths.size() == 14);
    REQUIRE(widths[2] == Approx(1.0)); // the first segment's halves
    REQUIRE(widths[4] == Approx(1.0));
    REQUIRE(widths[5] == Approx(2.0)); // the taper meets half way
    for (std::size_t i = 0; i + 2 < pts.size(); ++i) {
        const Vec2 in = arc_tangents(pts[i], pts[i + 1], bulges[i]).second;
        const Vec2 out = arc_tangents(pts[i + 1], pts[i + 2], bulges[i + 1]).first;
        REQUIRE(in.x == Approx(out.x).margin(1e-9));
        REQUIRE(in.y == Approx(out.y).margin(1e-9));
    }
    std::vector<Vec2> two{{0, 0}, {1, 0}};
    REQUIRE_FALSE(polyline_ops::fit_arcs(two, bulges, widths, false));
}

TEST_CASE("PEDIT Fit and Multiple on the drawing, one undo step each") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {10, 0}, {10, 10}, {20, 10}}, false, 1});
    engine.submit(AddPolylineCommand{{{0, 50}, {10, 50}, {10, 60}}, true, 2});
    engine.submit(AddLineCommand{{0, 100}, {10, 100}, 3});
    engine.submit(AddCircleCommand{{50, 50}, 5.0, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(PeditCommand{{5, 0}, 1.0, 10, {}, {}, 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Fit."; }));
    io::Document doc = dump(engine, "musacad_pedit_fit.musa");
    REQUIRE(doc.polylines[0].points.size() == 7);
    REQUIRE(doc.polylines[0].bulges.size() == 7);

    // Multiple: every polyline, the line made one; the circle stays.
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 4; }));
    PeditCommand w{{}, 0.0, 8, {2.0, 0.0}, {}, 6};
    w.selection = true;
    engine.submit(w);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "3 objects changed."; }));
    doc = dump(engine, "musacad_pedit_multiple.musa");
    REQUIRE(doc.polylines.size() == 3);
    REQUIRE(doc.lines.empty());
    REQUIRE(doc.circles.size() == 1);
    for (const io::DocPolyline& p : doc.polylines) {
        REQUIRE(p.widths.size() == 2 * p.points.size());
        REQUIRE(p.widths[0] == Approx(2.0));
    }
    engine.submit(UndoLastGroupCommand{});
    doc = dump(engine, "musacad_pedit_multiple_undo.musa");
    REQUIRE(doc.lines.size() == 1);
    REQUIRE(doc.polylines.size() == 2);
    engine.stop();
}

TEST_CASE("PEDIT's prompts: Multiple, Fit, the fuzz distance, and turning a line into a polyline") {
    H h;
    h.proc.submit_line("PEDIT");
    h.proc.submit_line("M");
    REQUIRE(h.out.prompt == "Select objects: ");
    h.proc.set_selection_count(2);
    h.proc.submit_line("");
    REQUIRE(h.out.prompt == "Enter an option [Close/Open/Join/Width/Fit/Spline/Decurve/Ltype gen/Reverse/Undo]: ");
    h.proc.submit_line("F");
    REQUIRE(h.last<PeditCommand>()->op == 10);
    REQUIRE(h.last<PeditCommand>()->selection);
    h.proc.submit_line("J");
    REQUIRE(h.out.prompt == "Enter fuzz distance <0.0000>: ");
    h.proc.submit_line("0.5");
    REQUIRE(h.last<JoinSelectionCommand>()->radius == Approx(0.5));
    h.proc.submit_line("E"); // no vertex editing on several
    REQUIRE(h.out.lines.back() ==
            "Enter Close, Open, Join, Width, Fit, Spline, Decurve, Reverse, Undo or Enter to finish.");
    h.proc.submit_line("");
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.set_selection_count(0);
    h.proc.submit_line("PEDIT");
    h.proc.set_hovered_kind(EntityKind::Line);
    h.proc.pick_point({5, 0}, std::nullopt);
    REQUIRE(h.out.lines.back() == "Object selected is not a polyline");
    REQUIRE(h.out.prompt == "Do you want to turn it into one? <Y> ");
    h.proc.submit_line("N");
    REQUIRE(h.out.prompt == "Select polyline or [Multiple]: ");
    h.proc.pick_point({5, 0}, std::nullopt);
    h.proc.submit_line("");
    REQUIRE(h.out.prompt ==
            "Enter an option [Close/Open/Join/Width/Edit vertex/Fit/Spline/Decurve/Ltype gen/Reverse/Undo]: ");
    h.proc.submit_line("F");
    REQUIRE(h.last<PeditCommand>()->op == 10);
    REQUIRE_FALSE(h.last<PeditCommand>()->selection);
    h.proc.submit_line("");
    h.proc.set_hovered_kind(std::nullopt);
}

TEST_CASE("PELLIPSE 1: ELLIPSE draws a polyline of arcs on the ellipse") {
    H h;
    h.proc.submit_line("PELLIPSE");
    REQUIRE(h.out.prompt == "Enter new value for PELLIPSE <0>: ");
    h.proc.submit_line("1");
    h.proc.submit_line("ELLIPSE");
    h.proc.submit_line("0,0");
    h.proc.submit_line("20,0");
    h.proc.submit_line("5");
    const auto* p = h.last<AddPolylineCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->closed);
    REQUIRE(p->points.size() == 32);
    for (std::size_t i = 0; i < p->points.size(); ++i) {
        const Vec2 q = p->points[i];
        const double on = std::pow((q.x - 10.0) / 10.0, 2) + std::pow(q.y / 5.0, 2);
        REQUIRE(on == Approx(1.0).margin(i % 2 == 0 ? 1e-9 : 0.01)); // samples on it, joints near
    }
    REQUIRE(h.last<AddEllipseCommand>() == nullptr);
    h.proc.submit_line("PELLIPSE");
    h.proc.submit_line("0");
    h.proc.submit_line("ELLIPSE");
    h.proc.submit_line("0,0");
    h.proc.submit_line("20,0");
    h.proc.submit_line("5");
    REQUIRE(h.last<AddEllipseCommand>() != nullptr);

    h.proc.submit_line("SPLINE");
    h.proc.submit_line("M");
    h.proc.submit_line("CV");
    h.proc.submit_line("D");
    REQUIRE(h.out.prompt == "Enter degree of spline <3>: ");
    h.proc.submit_line("");
    h.proc.cancel();
}

TEST_CASE("REVCLOUD: Calligraphy lobes taper; the type is remembered and the prompt follows it") {
    H h;
    h.proc.submit_line("REVCLOUD");
    h.proc.submit_line("F");
    REQUIRE(h.out.prompt ==
            "Specify first point or [Arc length/Object/Rectangular/Polygonal/Freehand/Style] <Object>: ");
    h.proc.submit_line("S");
    REQUIRE(h.out.prompt == "Select arc style [Normal/Calligraphy] <Normal>: ");
    h.proc.submit_line("C");
    h.proc.submit_line("A");
    h.proc.submit_line("25");
    h.proc.submit_line("25");
    h.proc.submit_line("R");
    REQUIRE(h.out.prompt ==
            "Specify first corner point or [Arc length/Object/Rectangular/Polygonal/Freehand/Style] <Object>: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompt == "Specify opposite corner: ");
    h.proc.submit_line("100,50");
    const auto* p = h.last<AddPolylineCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->widths.size() == 2 * p->points.size());
    REQUIRE(p->widths[0] == 0.0);               // thin where a lobe starts
    REQUIRE(p->widths[1] == Approx(0.2 * 25.0)); // a fifth of its chord where it ends

    h.proc.submit_line("REVCLOUD");
    REQUIRE(h.out.lines.back().find("Style: Calligraphy   Type: Rectangular") != std::string::npos);
    h.proc.submit_line("O");
    h.proc.submit_line("50,0");
    REQUIRE(h.last<RevcloudObjectCommand>()->calligraphy);
    h.proc.submit_line("");
    h.proc.submit_line("REVCLOUD"); // back as they were for whatever runs next
    h.proc.submit_line("S");
    h.proc.submit_line("N");
    h.proc.submit_line("F");
    h.proc.cancel();
}

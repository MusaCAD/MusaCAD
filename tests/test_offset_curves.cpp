// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// OFFSET (issue #50): construction lines and rays, ellipses and splines (as splines),
// OFFSETGAPTYPE, the offset's properties, and the preview at the side prompt.

#include <chrono>
#include <cmath>
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
#include "musacad/core/ellipse.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/spline_eval.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {
constexpr double kPiT = 3.14159265358979323846;

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
bool eq(Vec2 p, Vec2 q, double eps = 1e-6) {
    return std::abs(p.x - q.x) < eps && std::abs(p.y - q.y) < eps;
}
bool has_segment(const std::vector<Vec2>& lines, Vec2 a, Vec2 b) {
    for (std::size_t i = 0; i + 1 < lines.size(); i += 2) {
        if ((eq(lines[i], a) && eq(lines[i + 1], b)) || (eq(lines[i], b) && eq(lines[i + 1], a))) {
            return true;
        }
    }
    return false;
}
/// How far `p` is from a densely sampled curve.
double distance_to(const std::vector<Vec2>& curve, Vec2 p) {
    double best = std::numeric_limits<double>::max();
    for (const Vec2& q : curve) {
        best = std::min(best, length(p - q));
    }
    return best;
}
std::vector<Vec2> sampled(const AddSplineCommand& sp, int n = 200) {
    std::vector<Vec2> out;
    for (int i = 0; i <= n; ++i) {
        out.push_back(spline::evaluate(sp.control_points, static_cast<int>(sp.degree),
                                       static_cast<double>(i) / n));
    }
    return out;
}

struct Out : musacad::command::CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
    [[nodiscard]] bool any_contains(const std::string& sub) const {
        for (const auto& l : lines) {
            if (l.find(sub) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};
struct H {
    std::vector<Command> cmds;
    Out out;
    musacad::command::CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
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
    [[nodiscard]] int count() const {
        int n = 0;
        for (const Command& c : cmds) {
            n += std::holds_alternative<T>(c) ? 1 : 0;
        }
        return n;
    }
    void run(std::initializer_list<const char*> lines_in) {
        for (const char* l : lines_in) {
            proc.submit_line(l);
        }
    }
};
} // namespace

TEST_CASE("#50 a construction line and a ray offset to a parallel one of their own kind") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle x = store.add_xline({0, 0}, {1, 0}, false);
    Command out;
    REQUIRE(kernel.offset(store, x, 3.0, {7, 9}, out));
    const auto& up = std::get<AddXlineCommand>(out);
    REQUIRE(up.base == Vec2{0, 3});
    REQUIRE(up.dir == Vec2{1, 0});
    REQUIRE(!up.ray);
    REQUIRE(kernel.offset(store, x, 3.0, {7, -9}, out));
    REQUIRE(std::get<AddXlineCommand>(out).base == Vec2{0, -3});

    const double s = std::sqrt(0.5);
    const EntityHandle r = store.add_xline({1, 1}, {s, s}, true);
    REQUIRE(kernel.offset(store, r, std::sqrt(2.0), {0, 5}, out));
    const auto& ray = std::get<AddXlineCommand>(out);
    REQUIRE(ray.ray);
    REQUIRE(ray.base.x == Approx(0.0).margin(1e-12));
    REQUIRE(ray.base.y == Approx(2.0));
    REQUIRE(ray.dir.x == Approx(s));
}

TEST_CASE("#50 an ellipse offsets to a spline a constant distance away, outside and inside") {
    GeometryStore store;
    NativeKernel2D kernel;
    EntityProps props;
    props.layer = 0;
    const EntityHandle h = store.add_ellipse({10, 5}, {8, 0}, 0.5, 0.0, 2.0 * kPiT, props);
    std::vector<Vec2> source;
    const EllipseData* e = store.ellipse(h);
    for (int i = 0; i < 2000; ++i) {
        source.push_back(ellipse::point_at(*e, 2.0 * kPiT * i / 2000.0));
    }

    Command out;
    REQUIRE(kernel.offset(store, h, 1.0, {30, 5}, out)); // a point outside
    const auto& outer = std::get<AddSplineCommand>(out);
    REQUIRE(outer.degree == 3);
    for (const Vec2& p : sampled(outer)) {
        REQUIRE(distance_to(source, p) == Approx(1.0).margin(0.02));
        // outside: farther from the centre than the ellipse is in that direction
        const Vec2 d = p - Vec2{10, 5};
        REQUIRE((d.x * d.x) / 64.0 + (d.y * d.y) / 16.0 > 1.0);
    }
    // The loop closes on itself.
    REQUIRE(length(sampled(outer).front() - sampled(outer).back()) < 1e-6);

    REQUIRE(kernel.offset(store, h, 0.5, {10, 5}, out)); // the centre: inside
    for (const Vec2& p : sampled(std::get<AddSplineCommand>(out))) {
        REQUIRE(distance_to(source, p) == Approx(0.5).margin(0.02));
        const Vec2 d = p - Vec2{10, 5};
        REQUIRE((d.x * d.x) / 64.0 + (d.y * d.y) / 16.0 < 1.0);
    }
    // Inside by more than the ellipse's tightest radius (b * b / a = 2) the offset would
    // cross itself: refused.
    REQUIRE_FALSE(kernel.offset(store, h, 3.0, {10, 5}, out));

    // An elliptical arc gives an open spline between the moved ends.
    const EntityHandle arc = store.add_ellipse({0, 0}, {8, 0}, 0.5, 0.0, kPiT / 2.0);
    REQUIRE(kernel.offset(store, arc, 1.0, {20, 20}, out));
    const std::vector<Vec2> open = sampled(std::get<AddSplineCommand>(out));
    REQUIRE(open.front().x == Approx(9.0).margin(0.02));
    REQUIRE(open.front().y == Approx(0.0).margin(0.05));
    REQUIRE(open.back().x == Approx(0.0).margin(0.05));
    REQUIRE(open.back().y == Approx(5.0).margin(0.02));
}

TEST_CASE("#50 a spline offsets to a spline on the side asked for") {
    GeometryStore store;
    NativeKernel2D kernel;
    const std::vector<Vec2> fit{{0, 0}, {5, 3}, {10, 0}, {15, -3}, {20, 0}};
    const std::vector<Vec2> ctrl = spline::fit_or_fallback(fit, 3, spline::FitParam::Chord);
    const EntityHandle h = store.add_spline(ctrl, 3);
    std::vector<Vec2> source;
    for (int i = 0; i <= 4000; ++i) {
        source.push_back(spline::evaluate(ctrl, 3, i / 4000.0));
    }
    Command out;
    REQUIRE(kernel.offset(store, h, 0.5, {10, 10}, out)); // above
    const std::vector<Vec2> above = sampled(std::get<AddSplineCommand>(out));
    for (const Vec2& p : above) {
        REQUIRE(distance_to(source, p) == Approx(0.5).margin(0.03));
    }
    REQUIRE(above.front().y > 0.0); // the left of travel at the start
    REQUIRE(kernel.offset(store, h, 0.5, {10, -10}, out)); // below
    REQUIRE(sampled(std::get<AddSplineCommand>(out)).front().y < 0.0);
}

TEST_CASE("#50 OFFSETGAPTYPE: the outside of a corner carried on, rounded or bevelled") {
    GeometryStore store;
    NativeKernel2D kernel;
    const std::vector<Vec2> ell{{0, 0}, {10, 0}, {10, 10}};
    const EntityHandle h = store.add_polyline(ell, false);
    Command out;

    REQUIRE(kernel.offset(store, h, 2.0, {5, -5}, out, 0));
    const auto extend = std::get<AddPolylineCommand>(out);
    REQUIRE(extend.points.size() == 3);
    REQUIRE(eq(extend.points[1], {12, -2}));
    REQUIRE(extend.bulges.empty());

    REQUIRE(kernel.offset(store, h, 2.0, {5, -5}, out, 1));
    const auto fillet = std::get<AddPolylineCommand>(out);
    REQUIRE(fillet.points.size() == 4);
    REQUIRE(eq(fillet.points[0], {0, -2}));
    REQUIRE(eq(fillet.points[1], {10, -2}));
    REQUIRE(eq(fillet.points[2], {12, 0}));
    REQUIRE(eq(fillet.points[3], {12, 10}));
    REQUIRE(fillet.bulges.size() == 4);
    REQUIRE(fillet.bulges[1] == Approx(std::tan(kPiT / 8.0))); // a quarter circle about the vertex
    REQUIRE(fillet.bulges[0] == 0.0);
    REQUIRE(fillet.bulges[2] == 0.0);

    REQUIRE(kernel.offset(store, h, 2.0, {5, -5}, out, 2));
    const auto bevel = std::get<AddPolylineCommand>(out);
    REQUIRE(bevel.points.size() == 4);
    REQUIRE(eq(bevel.points[1], {10, -2}));
    REQUIRE(eq(bevel.points[2], {12, 0}));
    REQUIRE(bevel.bulges.empty());

    // The inside of the corner has no gap to fill: the same for every setting.
    for (const int gap : {0, 1, 2}) {
        REQUIRE(kernel.offset(store, h, 2.0, {5, 5}, out, gap));
        const auto inner = std::get<AddPolylineCommand>(out);
        REQUIRE(inner.points.size() == 3);
        REQUIRE(eq(inner.points[1], {8, 2}));
    }

    // A closed square, offset outwards with arcs: every corner rounded.
    const std::vector<Vec2> sq{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    const EntityHandle box = store.add_polyline(sq, true);
    REQUIRE(kernel.offset(store, box, 1.0, {20, 5}, out, 1));
    const auto round = std::get<AddPolylineCommand>(out);
    REQUIRE(round.closed);
    REQUIRE(round.points.size() == 8);
    int arcs = 0;
    for (const double b : round.bulges) {
        arcs += std::abs(b) > 1e-9 ? 1 : 0;
    }
    REQUIRE(arcs == 4);
}

TEST_CASE("#50 the offset is the source's double: its layer, colour and linetype scale") {
    GeometryEngine e;
    e.start();
    Layer walls;
    walls.name = "walls";
    e.submit(AddLayerCommand{walls});
    e.submit(SetCurrentLayerCommand{1});
    REQUIRE(wait_until(e, [](const auto& s) { return s.current_layer == 1; }));
    e.submit(AddLineCommand{{0, 0}, {10, 0}, 1}); // on "walls"
    e.submit(AddCircleCommand{{50, 50}, 5.0, 2});
    e.submit(SetCurrentLayerCommand{0});
    REQUIRE(wait_until(e, [](const auto& s) { return s.current_layer == 0; }));

    // Layer = Source (the default): the offsets stay on "walls" though layer 0 is current.
    e.submit(OffsetPickCommand{{5, 0}, 0.5, 2.0, {5, 5}, 3});
    e.submit(OffsetPickCommand{{55, 50}, 0.5, 1.0, {70, 50}, 4});
    REQUIRE(wait_until(e, [](const auto& s) { return has_segment(s.line_vertices, {0, 2}, {10, 2}); }));
    Layer off = walls;
    off.on = false;
    e.submit(SetLayerCommand{1, off});
    REQUIRE(wait_until(e, [](const auto& s) { return s.line_vertices.empty(); }));
    e.submit(SetLayerCommand{1, walls});
    REQUIRE(wait_until(e, [](const auto& s) { return has_segment(s.line_vertices, {0, 2}, {10, 2}); }));

    // Layer = Current: that one lands on layer 0 and stays when "walls" goes off.
    OffsetPickCommand cur{{5, 2}, 0.5, 2.0, {5, 9}, 5};
    cur.to_current_layer = true;
    e.submit(cur);
    REQUIRE(wait_until(e, [](const auto& s) { return has_segment(s.line_vertices, {0, 4}, {10, 4}); }));
    e.submit(SetLayerCommand{1, off});
    REQUIRE(wait_until(e, [](const auto& s) {
        return has_segment(s.line_vertices, {0, 4}, {10, 4}) && !has_segment(s.line_vertices, {0, 2}, {10, 2});
    }));
    e.stop();
}

TEST_CASE("#50 the side prompt shows the offset under the cursor until the pick") {
    GeometryEngine e;
    e.start();
    e.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(e, [](const auto& s) { return !s.line_vertices.empty(); }));
    OffsetPreviewCommand p;
    p.pick = {5, 0};
    p.radius = 0.5;
    p.distance = 2.0;
    e.submit(p);
    e.submit(SetCursorCommand{{4, 7}, 0.5, false, 0, {}, false});
    REQUIRE(wait_until(e, [](const auto& s) { return has_segment(s.grip_preview_segments, {0, 2}, {10, 2}); }));
    e.submit(SetCursorCommand{{4, -7}, 0.5, false, 0, {}, false});
    REQUIRE(wait_until(e, [](const auto& s) { return has_segment(s.grip_preview_segments, {0, -2}, {10, -2}); }));
    // The drawing itself is untouched by the band.
    REQUIRE(e.snapshot().line_vertices.size() == 2);

    // Through: the band passes through the cursor.
    p.distance = 0.0;
    p.through = true;
    e.submit(p);
    e.submit(SetCursorCommand{{4, 3.5}, 0.5, false, 0, {}, false});
    REQUIRE(wait_until(e, [](const auto& s) { return has_segment(s.grip_preview_segments, {0, 3.5}, {10, 3.5}); }));

    p.active = false;
    e.submit(p);
    REQUIRE(wait_until(e, [](const auto& s) { return s.grip_preview_segments.empty(); }));
    e.stop();
}

TEST_CASE("#50 OFFSET raises and drops its preview; OFFSETGAPTYPE is asked, kept and sent") {
    H h;
    h.run({"OFFSETGAPTYPE"});
    REQUIRE(h.out.prompt == "Enter new value for OFFSETGAPTYPE <0>: ");
    h.proc.submit_line("3");
    REQUIRE(h.out.any_contains("Requires an integer between 0 and 2."));
    REQUIRE(h.proc.has_active_command());
    h.proc.submit_line("1");
    REQUIRE(!h.proc.has_active_command());
    REQUIRE(musacad::command::OffsetCommand::s_gap_type_ == 1);

    h.out.lines.clear();
    h.run({"O", "2", "5,0"});
    REQUIRE(h.out.any_contains("OFFSETGAPTYPE=1"));
    const auto* up = h.last<OffsetPreviewCommand>();
    REQUIRE(up != nullptr);
    REQUIRE(up->active);
    REQUIRE(up->pick == Vec2{5, 0});
    REQUIRE(up->distance == 2.0);
    REQUIRE(!up->through);
    REQUIRE(up->gap_type == 1);
    h.proc.submit_line("5,5");
    REQUIRE(h.last<OffsetPickCommand>()->gap_type == 1);
    REQUIRE(!h.last<OffsetPreviewCommand>()->active); // back at the object prompt
    // Multiple: the band steps out from the offset just made.
    h.run({"5,0", "M", "5,5"});
    REQUIRE(h.last<OffsetPreviewCommand>()->active);
    REQUIRE(h.last<OffsetPreviewCommand>()->from_last);
    h.proc.cancel();
    REQUIRE(!h.last<OffsetPreviewCommand>()->active);

    H t;
    t.run({"O", "T", "5,0"});
    REQUIRE(t.last<OffsetPreviewCommand>()->through);
    t.run({""});
    REQUIRE(!t.last<OffsetPreviewCommand>()->active);
    REQUIRE(!t.proc.has_active_command());
    // put the session defaults back
    t.run({"OFFSETGAPTYPE", "0"});
    t.run({"O", "1", ""});
}

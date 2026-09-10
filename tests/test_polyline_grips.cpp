// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Polyline multi-functional grips (#32): segment midpoint grips (on the arc for arc
// segments), dragging them (a straight segment moves, an arc reshapes through the point),
// and the grip menu's operations (add / remove a vertex, convert to arc / line).

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/grips.hpp"

using namespace musacad::core;

namespace {
template <class Pred>
bool wait_until(GeometryEngine& e, Pred pred) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        e.consume_snapshot();
        if (pred(e.snapshot())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    e.consume_snapshot();
    return pred(e.snapshot());
}
bool near(Vec2 a, Vec2 b, double tol = 1e-9) {
    return std::abs(a.x - b.x) <= tol && std::abs(a.y - b.y) <= tol;
}
} // namespace

TEST_CASE("#32 bulge_through: the arc from a to b through p") {
    // A semicircle below the chord (0,0)-(10,0): counter-clockwise, bulge 1.
    CHECK(std::abs(bulge_through({0, 0}, {5, -5}, {10, 0}) - 1.0) < 1e-9);
    // Above the chord: clockwise, bulge -1.
    CHECK(std::abs(bulge_through({0, 0}, {5, 5}, {10, 0}) + 1.0) < 1e-9);
    // A point on the chord: straight.
    CHECK(bulge_through({0, 0}, {3, 0}, {10, 0}) == 0.0);
    // A quarter circle (90 degrees): sagitta = r(1 - cos 45), bulge = tan(22.5 deg).
    const double r = 5.0 / std::sin(kPi / 4.0);
    const double sag = r * (1.0 - std::cos(kPi / 4.0));
    CHECK(std::abs(bulge_through({0, 0}, {5, -sag}, {10, 0}) - std::tan(kPi / 8.0)) < 1e-9);
    // The "long way round" (p beyond the diameter): included angle over 180 degrees.
    CHECK(bulge_through({0, 0}, {5, -20}, {10, 0}) > 1.0);
}

TEST_CASE("#32 polyline grips: vertex grips plus a midpoint grip per segment, on the arc for arcs") {
    GeometryStore s;
    std::vector<Grip> g;
    const std::vector<Vec2> pts{{0, 0}, {10, 0}, {10, 10}};
    const std::vector<double> bulges{1.0, 0.0, 0.0};
    const EntityHandle open = s.add_polyline(pts, bulges, false);
    grips_of(s, open, g);
    REQUIRE(g.size() == 5);
    int segs = 0;
    for (const Grip& gr : g) {
        if (gr.kind == GripKind::Segment) {
            ++segs;
            if (gr.index == kSegmentGripBase + 0) {
                CHECK(near(gr.pos, {5, -5})); // the semicircle's midpoint
            } else {
                CHECK(gr.index == kSegmentGripBase + 1);
                CHECK(near(gr.pos, {10, 5}));
            }
        }
    }
    CHECK(segs == 2);
    g.clear();
    grips_of(s, s.add_polyline(pts, true), g);
    CHECK(g.size() == 6); // closed: three segments

    // Dragging a straight segment's grip moves the segment parallel to itself.
    Command c = edit_for_grip_drag(s, open, kSegmentGripBase + 1, {12, 7});
    const auto& pl = std::get<AddPolylineCommand>(c);
    CHECK(near(pl.points[1], {12, 2}));
    CHECK(near(pl.points[2], {12, 12}));
    CHECK(near(pl.points[0], {0, 0}));
    // Dragging the arc's grip reshapes the arc through the point: a quarter circle.
    const double r = 5.0 / std::sin(kPi / 4.0);
    const double sag = r * (1.0 - std::cos(kPi / 4.0));
    Command c2 = edit_for_grip_drag(s, open, kSegmentGripBase + 0, {5, -sag});
    const auto& pl2 = std::get<AddPolylineCommand>(c2);
    REQUIRE(pl2.bulges.size() == 3);
    CHECK(std::abs(pl2.bulges[0] - std::tan(kPi / 8.0)) < 1e-9);
    CHECK(near(pl2.points[0], {0, 0}));
    CHECK(near(pl2.points[1], {10, 0}));
}

TEST_CASE("#32 grip menu: add / remove a vertex, convert a segment to an arc and back; one undo step each") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {10, 0}, {10, 10}}, false, 1});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grips.size() == 5; }));
    const EntityHandle h = engine.snapshot().selection[0];

    // Add a vertex on the first segment: four vertices, three segments -> 7 grips.
    engine.submit(PolylineVertexCommand{h, kSegmentGripBase + 0, PolylineVertexCommand::Op::AddVertex, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grips.size() == 7; }));
    bool has_mid = false;
    for (const GripInfo& g : engine.snapshot().grips) {
        has_mid = has_mid || (g.kind == static_cast<std::uint8_t>(GripKind::Vertex) && near(g.pos, {5, 0}));
    }
    CHECK(has_mid);
    const EntityHandle h2 = engine.snapshot().selection[0];

    // Convert the last segment to an arc: its midpoint grip leaves the chord.
    engine.submit(PolylineVertexCommand{h2, kSegmentGripBase + 2, PolylineVertexCommand::Op::ToArc, 3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        for (const GripInfo& g : s.grips) {
            if (g.index == kSegmentGripBase + 2 && std::abs(g.pos.x - 10.0) > 0.5) {
                return true;
            }
        }
        return false;
    }));
    const EntityHandle h3 = engine.snapshot().selection[0];
    engine.submit(PolylineVertexCommand{h3, kSegmentGripBase + 2, PolylineVertexCommand::Op::ToLine, 4});
    REQUIRE(wait_until(engine, [](const auto& s) {
        for (const GripInfo& g : s.grips) {
            if (g.index == kSegmentGripBase + 2 && near(g.pos, {10, 5}, 1e-6)) {
                return true;
            }
        }
        return false;
    }));
    const EntityHandle h4 = engine.snapshot().selection[0];

    // Remove the added vertex (index 1): back to three vertices.
    engine.submit(PolylineVertexCommand{h4, 1, PolylineVertexCommand::Op::RemoveVertex, 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grips.size() == 5; }));
    // Undo the removal: the restored polyline (re-created, so no longer selected) has
    // its four vertices back -- seven grips once selected again.
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grips.empty(); }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grips.size() == 7; }));
    // A two-vertex polyline keeps its vertices.
    engine.submit(ClearSelectionCommand{});
    engine.submit(AddPolylineCommand{{{50, 0}, {60, 0}}, false, 6});
    engine.submit(SelectPickCommand{{55, 0}, 0.5, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grips.size() == 3; }));
    const EntityHandle two = engine.snapshot().selection[0];
    engine.submit(PolylineVertexCommand{two, 0, PolylineVertexCommand::Op::RemoveVertex, 7});
    engine.submit(AddLineCommand{{100, 0}, {110, 0}, 8});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() >= 2 && s.grips.size() == 3; }));
    engine.stop();
}

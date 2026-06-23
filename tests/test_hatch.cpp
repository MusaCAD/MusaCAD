// HATCH (Part A): the core triangulation + point-in-polygon helpers, and the engine
// path that turns a selected closed polyline into a SOLID-filled, pickable hatch entity.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/hatch.hpp"
#include "musacad/core/hatch_pattern.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {
double triangle_area_sum(const std::vector<Vec2>& tris) {
    double a = 0.0;
    for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
        const Vec2 p = tris[i];
        const Vec2 q = tris[i + 1];
        const Vec2 r = tris[i + 2];
        a += std::abs((q.x - p.x) * (r.y - p.y) - (r.x - p.x) * (q.y - p.y)) * 0.5;
    }
    return a;
}

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
} // namespace

TEST_CASE("hatch::triangulate_filled covers the exact filled area (outer minus holes)") {
    std::vector<Vec2> tris;
    hatch::triangulate_filled({{{0, 0}, {10, 0}, {10, 10}, {0, 10}}}, tris);
    CHECK(triangle_area_sum(tris) == Approx(100.0)); // a 10x10 square

    tris.clear();
    hatch::triangulate_filled(
        {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{3, 3}, {7, 3}, {7, 7}, {3, 7}}}, tris);
    CHECK(triangle_area_sum(tris) == Approx(100.0 - 16.0)); // square minus a 4x4 hole

    // A slanted triangle is exact (no stair-stepping): area 0.5*8*8 = 32.
    tris.clear();
    hatch::triangulate_filled({{{0, 0}, {8, 0}, {0, 8}}}, tris);
    CHECK(triangle_area_sum(tris) == Approx(32.0));
}

TEST_CASE("hatch::point_in_loops respects islands (even-odd)") {
    const std::vector<std::vector<Vec2>> loops = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
                                                  {{3, 3}, {7, 3}, {7, 7}, {3, 7}}};
    CHECK(hatch::point_in_loops(loops, {1, 1}));        // inside outer, outside hole
    CHECK_FALSE(hatch::point_in_loops(loops, {5, 5}));  // inside the hole
    CHECK_FALSE(hatch::point_in_loops(loops, {20, 20})); // outside everything
}

TEST_CASE("HATCH from a selected closed polyline -> SOLID fill, pickable, undoable") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {40, 0}, {40, 40}, {0, 40}}, true, 1});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(HatchFromSelectionCommand{"SOLID", 1.0, 0.0, 2});
    // The hatch renders as filled triangles (the polygon area), and is the selection.
    REQUIRE(wait_until(engine, [](const auto& s) {
        return !s.fill_batches.empty() && s.selection.size() == 1 &&
               s.selection[0].kind == EntityKind::Hatch;
    }));

    // A click INSIDE the region selects the hatch (point-in-polygon pick).
    engine.submit(ClearSelectionCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.empty(); }));
    engine.submit(SelectPickCommand{{20, 20}, 0.5, false});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.selection.size() == 1 && s.selection[0].kind == EntityKind::Hatch;
    }));

    // Undo removes the hatch (the polyline remains).
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.fill_batches.empty(); }));
    engine.stop();
}

TEST_CASE("hatch::trace_boundary finds the loop enclosing a point (or fails cleanly)") {
    // Four separate segments forming a 10x10 square (corners meet at shared endpoints).
    const std::vector<hatch::Segment> sq = {{{0, 0}, {10, 0}},
                                            {{10, 0}, {10, 10}},
                                            {{10, 10}, {0, 10}},
                                            {{0, 10}, {0, 0}}};
    const auto loop = hatch::trace_boundary(sq, {5, 5}, 1e-6);
    REQUIRE(loop.has_value());
    CHECK(hatch::point_in_loops({*loop}, {5, 5}));
    CHECK_FALSE(hatch::trace_boundary(sq, {50, 50}, 1e-6).has_value()); // outside -> no loop
    // An open boundary (a gap larger than tolerance) does not enclose the point.
    const std::vector<hatch::Segment> open = {{{0, 0}, {10, 0}}, {{10, 0}, {10, 10}},
                                              {{10, 10}, {0, 10}}}; // left side missing
    CHECK_FALSE(hatch::trace_boundary(open, {5, 5}, 1e-6).has_value());
}

TEST_CASE("hatch::trace_boundary respects a partitioning chord (planar arrangement)") {
    // A 20x40 square (four edges) split by a vertical chord whose endpoints land in the
    // MIDDLE of the top and bottom edges. A pick in either half must trace just that half,
    // not the whole square (the regression: a chord that didn't split the edges was ignored).
    const std::vector<hatch::Segment> segs = {
        {{0, 0}, {40, 0}},     {{40, 0}, {40, 40}},  {{40, 40}, {0, 40}}, {{0, 40}, {0, 0}},
        {{20, 40}, {20, 0}}}; // the partitioning chord, endpoints mid-edge
    const auto left = hatch::trace_boundary(segs, {10, 20}, 1e-6);
    REQUIRE(left.has_value());
    CHECK(hatch::point_in_loops({*left}, {10, 20}));        // pick is inside the left half
    CHECK_FALSE(hatch::point_in_loops({*left}, {30, 20}));  // the right half is NOT included
    const auto right = hatch::trace_boundary(segs, {30, 20}, 1e-6);
    REQUIRE(right.has_value());
    CHECK(hatch::point_in_loops({*right}, {30, 20}));
    CHECK_FALSE(hatch::point_in_loops({*right}, {10, 20}));

    // The traced half encloses exactly half the area (800), not the full 1600.
    std::vector<Vec2> tris;
    hatch::triangulate_filled({*left}, tris);
    CHECK(triangle_area_sum(tris) == Approx(800.0));
}

TEST_CASE("HATCH pick-point traces a boundary of loose lines (with an island)") {
    GeometryEngine engine;
    engine.start();
    // Outer square from four LINEs (not a polyline).
    engine.submit(AddLineCommand{{0, 0}, {40, 0}, 1});
    engine.submit(AddLineCommand{{40, 0}, {40, 40}, 1});
    engine.submit(AddLineCommand{{40, 40}, {0, 40}, 1});
    engine.submit(AddLineCommand{{0, 40}, {0, 0}, 1});
    // A closed-polyline island fully inside.
    engine.submit(AddPolylineCommand{{{15, 15}, {25, 15}, {25, 25}, {15, 25}}, true, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() >= 8; }));

    // Click between the island and the outer boundary -> hatch the ring (island is a hole).
    engine.submit(HatchPickPointCommand{{5, 5}, "SOLID", 1.0, 0.0, 3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return !s.fill_batches.empty() && s.selection.size() == 1 &&
               s.selection[0].kind == EntityKind::Hatch;
    }));
    // The pick inside the ring selects the hatch; a pick inside the island hole does NOT
    // (the hole is unfilled).
    engine.submit(ClearSelectionCommand{});
    engine.submit(SelectPickCommand{{20, 20}, 0.5, false}); // inside the island hole
    REQUIRE(wait_until(engine, [](const auto& s) {
        for (const auto& h : s.selection) {
            if (h.kind == EntityKind::Hatch) {
                return false; // must NOT select the hatch through its hole
            }
        }
        return true;
    }));
    engine.stop();
}

TEST_CASE("A selected hatch exposes a grip at every boundary vertex (identification + drag)") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {30, 0}, {30, 30}, {0, 30}}, true, 1});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(HatchFromSelectionCommand{"SOLID", 1.0, 0.0, 2});
    // The selected hatch publishes a grip per boundary vertex (clear selection feedback), its
    // boundary in selected_line_vertices (the outline highlight), AND its SOLID fill triangles
    // in selected_fill_vertices so the renderer can tint the whole fill as selected (the fill
    // must NOT vanish on selection -- the regression we are guarding against).
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.selection.size() == 1 && s.selection[0].kind == EntityKind::Hatch &&
               s.grips.size() == 4 && !s.selected_line_vertices.empty() &&
               !s.selected_fill_vertices.empty();
    }));
    engine.stop();
}

TEST_CASE("hatch boundary grip drag moves the picked vertex (reshape)") {
    GeometryStore store;
    const EntityHandle h =
        store.add_hatch({{{0, 0}, {10, 0}, {10, 10}, {0, 10}}}, "SOLID", 1.0, 0.0, {0, 0});
    // Grip index 2 is the third boundary vertex (10,10). Drag it to (15,16).
    const Command edited = edit_for_grip_drag(store, h, 2, {15, 16});
    const auto* c = std::get_if<AddHatchCommand>(&edited);
    REQUIRE(c != nullptr);
    REQUIRE(c->loops.size() == 1);
    REQUIRE(c->loops[0].size() == 4);
    CHECK(length(c->loops[0][2] - Vec2{15, 16}) < 1e-9); // moved
    CHECK(length(c->loops[0][0] - Vec2{0, 0}) < 1e-9);    // others unchanged
}

// ---- Part B: line patterns ---------------------------------------------------------------

TEST_CASE("hatch::parse_pat reads .PAT families (angle/origin/delta + dashes)") {
    const auto pats = hatch::parse_pat(
        "*ANSI31, test\n45, 0,0, 0,0.125\n;a comment\n*DASHED, test2\n0, 0,0, 0,0.25, 0.5,-0.25\n");
    REQUIRE(pats.size() == 2);
    CHECK(pats[0].name == "ANSI31");
    REQUIRE(pats[0].lines.size() == 1);
    CHECK(pats[0].lines[0].angle == Approx(45.0));
    CHECK(pats[0].lines[0].delta.y == Approx(0.125));
    CHECK(pats[0].lines[0].dashes.empty()); // solid family
    REQUIRE(pats[1].lines.size() == 1);
    REQUIRE(pats[1].lines[0].dashes.size() == 2);
    CHECK(pats[1].lines[0].dashes[0] == Approx(0.5));
    CHECK(pats[1].lines[0].dashes[1] == Approx(-0.25));
}

TEST_CASE("hatch::builtin_pattern resolves stock names (and not SOLID)") {
    CHECK(hatch::builtin_pattern("ANSI31") != nullptr);
    CHECK(hatch::builtin_pattern("ansi31") != nullptr);   // case-insensitive
    CHECK(hatch::builtin_pattern("NET") != nullptr);
    CHECK(hatch::builtin_pattern("SOLID") == nullptr);    // SOLID is the fill special-case
    CHECK(hatch::builtin_pattern("NOTAPATTERN") == nullptr);
    CHECK(hatch::builtin_pattern_names().size() >= 20);   // a real stock library

    // The PR Pattern dropdown offers SOLID first, then every line pattern.
    const auto& choices = hatch::pattern_choice_list();
    REQUIRE(!choices.empty());
    CHECK(choices.front() == "SOLID");
    CHECK(choices.size() == hatch::builtin_pattern_names().size() + 1);
    CHECK(std::find(choices.begin(), choices.end(), "ANSI31") != choices.end());
}

TEST_CASE("hatch::generate_pattern_segments clips ANSI31 to the region (and islands)") {
    const std::vector<std::vector<Vec2>> square = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    const hatch::Pattern* ansi31 = hatch::builtin_pattern("ANSI31");
    REQUIRE(ansi31 != nullptr);

    std::vector<hatch::Segment> segs;
    hatch::generate_pattern_segments(square, *ansi31, 1.0, 0.0, {0, 0}, segs);
    REQUIRE(!segs.empty());
    // Every emitted dash lies inside the region (midpoint test) and runs at 45 degrees.
    for (const hatch::Segment& s : segs) {
        const Vec2 mid{(s.a.x + s.b.x) * 0.5, (s.a.y + s.b.y) * 0.5};
        CHECK(hatch::point_in_loops(square, mid));
        const Vec2 d = s.b - s.a;
        CHECK(std::abs(std::abs(d.x) - std::abs(d.y)) < 1e-6); // |dx| == |dy| => 45 degrees
    }

    // A bigger scale spreads the lines further apart -> strictly fewer segments.
    std::vector<hatch::Segment> coarse;
    hatch::generate_pattern_segments(square, *ansi31, 4.0, 0.0, {0, 0}, coarse);
    CHECK(coarse.size() < segs.size());

    // With a central island hole, no dash falls inside the hole.
    const std::vector<std::vector<Vec2>> holed = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
                                                  {{4, 4}, {6, 4}, {6, 6}, {4, 6}}};
    std::vector<hatch::Segment> ring;
    hatch::generate_pattern_segments(holed, *ansi31, 1.0, 0.0, {0, 0}, ring);
    REQUIRE(!ring.empty());
    for (const hatch::Segment& s : ring) {
        const Vec2 mid{(s.a.x + s.b.x) * 0.5, (s.a.y + s.b.y) * 0.5};
        CHECK_FALSE((mid.x > 4 && mid.x < 6 && mid.y > 4 && mid.y < 6)); // never inside the hole
    }
}

TEST_CASE("A pattern hatch renders as clipped lines (not a fill) through the snapshot") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {40, 0}, {40, 40}, {0, 40}}, true, 1});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    // ANSI31 (a line pattern) -> the hatch contributes LINES, not fill triangles.
    engine.submit(HatchFromSelectionCommand{"ANSI31", 1.0, 0.0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.selection.size() == 1 && s.selection[0].kind == EntityKind::Hatch &&
               s.fill_batches.empty() && !s.line_batches.empty();
    }));
    engine.stop();
}

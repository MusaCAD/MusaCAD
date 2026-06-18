#include <array>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/native_kernel_2d.hpp"

using namespace musacad::core;
using Catch::Approx;

TEST_CASE("Kernel offset: line offsets perpendicular toward the side point") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle h = store.add_line({0, 0}, {10, 0});

    Command out;
    REQUIRE(kernel.offset(store, h, 2.0, {5, 5}, out)); // toward +y
    const auto& up = std::get<AddLineCommand>(out);
    REQUIRE(up.a.y == Approx(2.0));
    REQUIRE(up.b.y == Approx(2.0));

    REQUIRE(kernel.offset(store, h, 2.0, {5, -5}, out)); // toward -y
    REQUIRE(std::get<AddLineCommand>(out).a.y == Approx(-2.0));
}

TEST_CASE("Kernel offset: circle grows outward / shrinks inward") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle h = store.add_circle({0, 0}, 5.0);

    Command out;
    REQUIRE(kernel.offset(store, h, 2.0, {10, 0}, out)); // side outside -> larger
    REQUIRE(std::get<AddCircleCommand>(out).radius == Approx(7.0));

    REQUIRE(kernel.offset(store, h, 2.0, {0, 0}, out)); // side inside -> smaller
    REQUIRE(std::get<AddCircleCommand>(out).radius == Approx(3.0));

    // Offset larger than radius from inside fails (non-positive radius).
    REQUIRE_FALSE(kernel.offset(store, h, 6.0, {0, 0}, out));
}

TEST_CASE("Kernel offset: arc keeps angles, offsets radius") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle h = store.add_arc({0, 0}, 5.0, 0.0, kHalfPi);

    Command out;
    REQUIRE(kernel.offset(store, h, 1.0, {10, 0}, out));
    const auto& a = std::get<AddArcCommand>(out);
    REQUIRE(a.radius == Approx(6.0));
    REQUIRE(a.start_angle == Approx(0.0));
    REQUIRE(a.end_angle == Approx(kHalfPi));
}

TEST_CASE("Kernel offset: polyline offsets to a parallel polyline") {
    GeometryStore store;
    NativeKernel2D kernel;
    const std::array<Vec2, 3> pts{{{0, 0}, {10, 0}, {10, 10}}};
    const EntityHandle h = store.add_polyline(pts, false);

    Command out;
    REQUIRE(kernel.offset(store, h, 1.0, {5, 5}, out)); // toward the inside
    const auto& pl = std::get<AddPolylineCommand>(out);
    REQUIRE(pl.points.size() == 3);
    // First segment was horizontal at y=0; offset upward -> y ~ 1.
    REQUIRE(pl.points[0].y == Approx(1.0));
    // Corner re-mitered to the intersection of the two offset edges (not averaged).
    REQUIRE(pl.points[1].x == Approx(9.0));
    REQUIRE(pl.points[1].y == Approx(1.0));
}

// The trapezoid-bug regression: a closed rectangle offset inward must produce a
// uniformly-spaced inner rectangle -- four edges each exactly `d` from the original
// AND four right-angle corners. The old averaged-normal miter failed this.
TEST_CASE("Kernel offset: closed rectangle re-miters to a uniform inner rectangle") {
    GeometryStore store;
    NativeKernel2D kernel;
    const std::array<Vec2, 4> rect{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    const EntityHandle h = store.add_polyline(rect, /*closed=*/true);

    Command out;
    REQUIRE(kernel.offset(store, h, 3.0, {5, 5}, out)); // inward
    const auto& pl = std::get<AddPolylineCommand>(out);
    REQUIRE(pl.closed);
    REQUIRE(pl.points.size() == 4);

    // Exact inner rectangle: every corner moved (3,3) inward.
    REQUIRE(pl.points[0].x == Approx(3.0));
    REQUIRE(pl.points[0].y == Approx(3.0));
    REQUIRE(pl.points[1].x == Approx(7.0));
    REQUIRE(pl.points[1].y == Approx(3.0));
    REQUIRE(pl.points[2].x == Approx(7.0));
    REQUIRE(pl.points[2].y == Approx(7.0));
    REQUIRE(pl.points[3].x == Approx(3.0));
    REQUIRE(pl.points[3].y == Approx(7.0));

    // Each edge is exactly distance d from the corresponding original edge.
    REQUIRE(pl.points[0].y == Approx(3.0)); // bottom edge at y=3 (orig y=0)
    REQUIRE(pl.points[1].x == Approx(7.0)); // right edge at x=7 (orig x=10)

    // Four right-angle corners: consecutive edge directions are perpendicular.
    const auto dir = [&](std::size_t i, std::size_t j) {
        const Vec2 d{pl.points[j].x - pl.points[i].x, pl.points[j].y - pl.points[i].y};
        const double len = std::sqrt(d.x * d.x + d.y * d.y);
        return Vec2{d.x / len, d.y / len};
    };
    const Vec2 e0 = dir(0, 1); // bottom
    const Vec2 e1 = dir(1, 2); // right
    const Vec2 e2 = dir(2, 3); // top
    const Vec2 e3 = dir(3, 0); // left
    REQUIRE(e0.x * e1.x + e0.y * e1.y == Approx(0.0).margin(1e-9));
    REQUIRE(e1.x * e2.x + e1.y * e2.y == Approx(0.0).margin(1e-9));
    REQUIRE(e2.x * e3.x + e2.y * e3.y == Approx(0.0).margin(1e-9));
    REQUIRE(e3.x * e0.x + e3.y * e0.y == Approx(0.0).margin(1e-9));
}

TEST_CASE("Kernel offset: over-large inward offset fails gracefully, store unchanged") {
    GeometryStore store;
    NativeKernel2D kernel;
    const std::array<Vec2, 4> rect{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    const EntityHandle h = store.add_polyline(rect, /*closed=*/true);
    const std::size_t before = store.live_count();

    Command out;
    // Inward by 8 on a 10x10 rectangle would fold the opposite edges past each other.
    REQUIRE_FALSE(kernel.offset(store, h, 8.0, {5, 5}, out));
    // The kernel never mutates the store; offset() only builds a result Command.
    REQUIRE(store.live_count() == before);
}

TEST_CASE("Kernel offset: arc (bulge) segment offsets concentrically, bulge preserved") {
    GeometryStore store;
    NativeKernel2D kernel;
    // A semicircle as a 2-vertex polyline: bulge 1 = sweep pi, centre (5,0), radius 5.
    const std::array<Vec2, 2> pts{{{0, 0}, {10, 0}}};
    const std::array<double, 2> bulges{{1.0, 0.0}};
    const EntityHandle h = store.add_polyline(pts, bulges, /*closed=*/false);

    Command out;
    REQUIRE(kernel.offset(store, h, 2.0, {5, 1}, out)); // toward the centre -> shrink
    const auto& pl = std::get<AddPolylineCommand>(out);
    REQUIRE(pl.points.size() == 2);
    REQUIRE_FALSE(pl.bulges.empty());
    REQUIRE(pl.bulges[0] == Approx(1.0)); // concentric arc keeps its sweep -> same bulge
    // radius 5 -> 3: endpoints move radially inward to (2,0) and (8,0).
    REQUIRE(pl.points[0].x == Approx(2.0));
    REQUIRE(pl.points[0].y == Approx(0.0).margin(1e-9));
    REQUIRE(pl.points[1].x == Approx(8.0));
}

TEST_CASE("Kernel circle/circle intersection: two hits, tangent, and disjoint") {
    Vec2 p0{};
    Vec2 p1{};
    // Overlapping: centres 8 apart, radius 5 each -> x = 4, y = +/-3.
    REQUIRE(NativeKernel2D::circle_circle_intersection({0, 0}, 5.0, {8, 0}, 5.0, p0, p1) == 2);
    REQUIRE(p0.x == Approx(4.0));
    REQUIRE(p1.x == Approx(4.0));
    REQUIRE(std::abs(p0.y) == Approx(3.0));
    // Tangent (externally): centres 10 apart, radius 5 each -> single hit (5,0).
    REQUIRE(NativeKernel2D::circle_circle_intersection({0, 0}, 5.0, {10, 0}, 5.0, p0, p1) == 1);
    REQUIRE(p0.x == Approx(5.0));
    REQUIRE(p0.y == Approx(0.0).margin(1e-9));
    // Disjoint, and one-wholly-inside-the-other: no isolated intersection.
    REQUIRE(NativeKernel2D::circle_circle_intersection({0, 0}, 2.0, {10, 0}, 2.0, p0, p1) == 0);
    REQUIRE(NativeKernel2D::circle_circle_intersection({0, 0}, 10.0, {1, 0}, 2.0, p0, p1) == 0);
}

TEST_CASE("Kernel offset: adjacent arc/arc corner re-miters onto both offset circles") {
    GeometryStore store;
    NativeKernel2D kernel;
    // Two non-tangent arcs sharing the vertex (10,0) -- the case JOIN / DXF can produce.
    const std::array<Vec2, 3> pts{{{0, 0}, {10, 0}, {10, 10}}};
    const std::array<double, 3> bulges{{0.5, 0.5, 0.0}};
    const EntityHandle h = store.add_polyline(pts, bulges, false);

    Command out;
    REQUIRE(kernel.offset(store, h, 1.0, {5, 5}, out));
    const auto& pl = std::get<AddPolylineCommand>(out);
    REQUIRE(pl.points.size() == 3);
    REQUIRE_FALSE(pl.bulges.empty());
    REQUIRE(pl.bulges[0] == Approx(0.5)); // arcs stay arcs (concentric -> same sweep)
    REQUIRE(pl.bulges[1] == Approx(0.5));

    // The offset arcs are concentric with the originals, so the re-mitered corner must lie
    // on BOTH offset circles: equidistant from each centre with that arc's other endpoint.
    // (A naive midpoint would NOT satisfy this for non-tangent arcs.)
    const BulgeArc a0 = arc_from_bulge({0, 0}, {10, 0}, 0.5);
    const BulgeArc a1 = arc_from_bulge({10, 0}, {10, 10}, 0.5);
    const auto dist = [](Vec2 p, Vec2 q) {
        return std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y));
    };
    REQUIRE(dist(pl.points[1], a0.center) == Approx(dist(pl.points[0], a0.center)));
    REQUIRE(dist(pl.points[1], a1.center) == Approx(dist(pl.points[2], a1.center)));
}

TEST_CASE("Kernel offset: filleted corner (line-arc-line) re-miters tangentially") {
    GeometryStore store;
    NativeKernel2D kernel;
    // Horizontal edge, a 90-degree fillet of radius 2 (centre (8,2)), then a vertical edge.
    const double b = std::tan((kHalfPi) / 4.0); // bulge of a 90-degree arc
    const std::array<Vec2, 4> pts{{{0, 0}, {8, 0}, {10, 2}, {10, 10}}};
    const std::array<double, 4> bulges{{0.0, b, 0.0, 0.0}};
    const EntityHandle h = store.add_polyline(pts, bulges, /*closed=*/false);

    Command out;
    REQUIRE(kernel.offset(store, h, 1.0, {8, 2}, out)); // toward the fillet centre -> shrink
    const auto& pl = std::get<AddPolylineCommand>(out);
    REQUIRE(pl.points.size() == 4);
    REQUIRE_FALSE(pl.bulges.empty());
    REQUIRE(pl.bulges[1] == Approx(b)); // fillet arc stays a 90-degree arc
    // Tangency preserved: the offset line meets the offset arc at one point.
    REQUIRE(pl.points[1].x == Approx(8.0));
    REQUIRE(pl.points[1].y == Approx(1.0));
    REQUIRE(pl.points[2].x == Approx(9.0));
    REQUIRE(pl.points[2].y == Approx(2.0));
}

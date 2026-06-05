#include <array>

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
}

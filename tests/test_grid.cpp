#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/render/grid.hpp"

using namespace musacad::render;
using Catch::Approx;

TEST_CASE("Grid: decade minor spacing tracks zoom") {
    // raw = target/scale; spacing = 10^ceil(log10(raw)).
    REQUIRE(choose_minor_spacing(1.0, 14.0) == Approx(100.0));   // raw 14 -> 100
    REQUIRE(choose_minor_spacing(14.0, 14.0) == Approx(1.0));    // raw 1 -> 1
    REQUIRE(choose_minor_spacing(140.0, 14.0) == Approx(0.1));   // raw 0.1 -> 0.1
    REQUIRE(choose_minor_spacing(1400.0, 14.0) == Approx(0.01)); // raw 0.01 -> 0.01

    // As we zoom in (scale up), spacing only shrinks.
    REQUIRE(choose_minor_spacing(200.0, 14.0) <= choose_minor_spacing(20.0, 14.0));
}

TEST_CASE("Grid: build produces major every tenth minor line") {
    Camera2D cam;
    cam.set_viewport(1000, 800);
    cam.set_center({0.0, 0.0});
    cam.set_scale(20.0);

    const GridResult g = build_grid(cam);
    REQUIRE(g.major_spacing == Approx(g.minor_spacing * 10.0));
    REQUIRE_FALSE(g.minor.empty());
    REQUIRE_FALSE(g.major.empty());

    // Every major line lies on a multiple of major_spacing.
    for (const Vec2& p : g.major) {
        const double on_x = std::abs(std::remainder(p.x, g.major_spacing));
        const double on_y = std::abs(std::remainder(p.y, g.major_spacing));
        REQUIRE((on_x < 1e-6 || on_y < 1e-6));
    }
    // Segment endpoints come in pairs.
    REQUIRE(g.minor.size() % 2 == 0);
    REQUIRE(g.major.size() % 2 == 0);
}

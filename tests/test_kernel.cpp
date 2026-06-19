#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/native_kernel_2d.hpp"

using namespace musacad::core;
using Catch::Approx;

TEST_CASE("Kernel: tessellate line is its two endpoints") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle line = store.add_line({0.0, 0.0}, {10.0, 0.0});

    std::vector<Vec2> out;
    kernel.tessellate(store, line, kDefaultTessTolerance, out);
    REQUIRE(out.size() == 2);
    REQUIRE(out.front() == Vec2{0.0, 0.0});
    REQUIRE(out.back() == Vec2{10.0, 0.0});
}

TEST_CASE("Kernel: tessellate circle is a closed loop within tolerance") {
    GeometryStore store;
    NativeKernel2D kernel;
    const double r = 4.0;
    const double tol = 0.01;
    const EntityHandle circle = store.add_circle({1.0, 2.0}, r);

    std::vector<Vec2> out;
    kernel.tessellate(store, circle, tol, out);
    REQUIRE(out.size() >= 4);
    REQUIRE(out.front() == out.back()); // closed

    for (const Vec2& p : out) {
        REQUIRE(distance(p, Vec2{1.0, 2.0}) == Approx(r));
    }
    // Chord midpoints stay within tolerance of the true circle.
    for (std::size_t i = 1; i < out.size(); ++i) {
        const Vec2 mid = lerp(out[i - 1], out[i], 0.5);
        const double err = r - distance(mid, Vec2{1.0, 2.0});
        REQUIRE(err <= tol + 1e-9);
        REQUIRE(err >= 0.0);
    }
}

TEST_CASE("Kernel: closest point on line clamps to segment") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle line = store.add_line({0.0, 0.0}, {10.0, 0.0});

    Vec2 cp{};
    REQUIRE(kernel.closest_point(store, line, {5.0, 3.0}, cp));
    REQUIRE(cp == Vec2{5.0, 0.0});

    REQUIRE(kernel.closest_point(store, line, {-5.0, 1.0}, cp));
    REQUIRE(cp == Vec2{0.0, 0.0}); // clamped to start
}

TEST_CASE("Kernel: closest point on circle") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle circle = store.add_circle({0.0, 0.0}, 5.0);

    Vec2 cp{};
    REQUIRE(kernel.closest_point(store, circle, {10.0, 0.0}, cp));
    REQUIRE(cp.x == Approx(5.0));
    REQUIRE(cp.y == Approx(0.0).margin(1e-12));
}

TEST_CASE("Kernel: a dimension is pickable by its TEXT label, not just the dim line") {
    GeometryStore store;
    NativeKernel2D kernel;
    // Linear dim: measured points on y=0, the dimension line (and the text) up at y=15.
    const EntityHandle dim =
        store.add_dimension(DimType::Linear, {0.0, 0.0}, {40.0, 0.0}, {20.0, 15.0}, 0);
    const DimData* d = store.dimension(dim);
    REQUIRE(d != nullptr);
    const DimGeometry g = compute_dim_geometry(*d, DimStyle{}, Rgb{});
    REQUIRE_FALSE(g.label.empty());

    // Picking at the text origin resolves to the text (~0 away). Before the fix the nearest
    // dim/ext/arrow line was returned instead, so clicking the label missed the dimension.
    Vec2 cp{};
    REQUIRE(kernel.closest_point(store, dim, g.text_pos, cp));
    REQUIRE(distance(g.text_pos, cp) < 0.01);
}

TEST_CASE("Kernel: closest point invalid handle returns false") {
    GeometryStore store;
    NativeKernel2D kernel;
    EntityHandle stale = store.add_line({0.0, 0.0}, {1.0, 0.0});
    REQUIRE(store.remove(stale));
    Vec2 cp{99.0, 99.0};
    REQUIRE_FALSE(kernel.closest_point(store, stale, {0.0, 0.0}, cp));
    REQUIRE(cp == Vec2{99.0, 99.0}); // untouched
}

TEST_CASE("Kernel: intersect two crossing lines") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle h = store.add_line({-5.0, 0.0}, {5.0, 0.0});
    const EntityHandle v = store.add_line({0.0, -5.0}, {0.0, 5.0});

    std::vector<Vec2> out;
    kernel.intersect(store, h, v, out);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].x == Approx(0.0).margin(1e-12));
    REQUIRE(out[0].y == Approx(0.0).margin(1e-12));
}

TEST_CASE("Kernel: parallel lines do not intersect") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle a = store.add_line({0.0, 0.0}, {10.0, 0.0});
    const EntityHandle b = store.add_line({0.0, 1.0}, {10.0, 1.0});

    std::vector<Vec2> out;
    kernel.intersect(store, a, b, out);
    REQUIRE(out.empty());
}

TEST_CASE("Kernel: line crossing a circle yields two points") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle circle = store.add_circle({0.0, 0.0}, 5.0);
    const EntityHandle line = store.add_line({-10.0, 0.0}, {10.0, 0.0});

    std::vector<Vec2> out;
    kernel.intersect(store, circle, line, out);
    REQUIRE(out.size() == 2);
    // The two crossings are near (+/-5, 0) within tessellation tolerance.
    for (const Vec2& p : out) {
        REQUIRE(std::abs(std::abs(p.x) - 5.0) < 0.1);
        REQUIRE(std::abs(p.y) < 0.1);
    }
}

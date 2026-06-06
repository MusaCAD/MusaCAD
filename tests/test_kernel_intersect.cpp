// Shared analytic intersection primitives used by Extend/Trim/Fillet/Chamfer.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/native_kernel_2d.hpp"

using namespace musacad::core;
using Catch::Approx;

TEST_CASE("line_line_intersection: exact crossing; parallel returns false") {
    Vec2 out{};
    REQUIRE(NativeKernel2D::line_line_intersection({0, 0}, {10, 0}, {5, -5}, {5, 5}, out));
    REQUIRE(out.x == Approx(5.0));
    REQUIRE(out.y == Approx(0.0));
    // Infinite lines: cross even when the segments don't overlap.
    REQUIRE(NativeKernel2D::line_line_intersection({0, 0}, {2, 0}, {10, -5}, {10, 5}, out));
    REQUIRE(out.x == Approx(10.0));
    // Parallel.
    REQUIRE_FALSE(NativeKernel2D::line_line_intersection({0, 0}, {10, 0}, {0, 1}, {10, 1}, out));
}

TEST_CASE("line_circle_intersection: 0 / 1 (tangent) / 2 hits") {
    Vec2 p0{};
    Vec2 p1{};
    REQUIRE(NativeKernel2D::line_circle_intersection({-10, 0}, {10, 0}, {0, 0}, 5.0, p0, p1) == 2);
    REQUIRE(NativeKernel2D::line_circle_intersection({-10, 5}, {10, 5}, {0, 0}, 5.0, p0, p1) == 1);
    REQUIRE(p0.y == Approx(5.0));
    REQUIRE(NativeKernel2D::line_circle_intersection({-10, 9}, {10, 9}, {0, 0}, 5.0, p0, p1) == 0);
}

TEST_CASE("intersect(): analytic line/line, line/circle, line/arc (segment-bounded)") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle line = store.add_line({0, 0}, {10, 0});

    SECTION("line x line") {
        const EntityHandle other = store.add_line({5, -5}, {5, 5});
        std::vector<Vec2> hits;
        kernel.intersect(store, line, other, hits);
        REQUIRE(hits.size() == 1);
        REQUIRE(hits[0].x == Approx(5.0));
    }
    SECTION("line x circle (both crossings within the segment)") {
        const EntityHandle circle = store.add_circle({5, 0}, 2.0);
        std::vector<Vec2> hits;
        kernel.intersect(store, line, circle, hits);
        REQUIRE(hits.size() == 2);
    }
    SECTION("line x arc (sweep-filtered: only the upper half is on the arc)") {
        // Upper semicircle centred at (5,0): the line y=0 meets it only at its ends.
        const EntityHandle arc = store.add_arc({5, 0}, 2.0, 0.0, kPi);
        std::vector<Vec2> hits;
        kernel.intersect(store, line, arc, hits);
        REQUIRE(hits.size() == 2); // (3,0) and (7,0) -- the arc endpoints lie on y=0
    }
}

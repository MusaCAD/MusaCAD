#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/osnap.hpp"
#include "musacad/core/spatial_grid.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {

struct Scene {
    GeometryStore store;
    NativeKernel2D kernel;
    SpatialGrid grid{16.0};

    EntityHandle add_line(Vec2 a, Vec2 b) {
        const EntityHandle h = store.add_line(a, b);
        index(h);
        return h;
    }
    EntityHandle add_circle(Vec2 c, double r) {
        const EntityHandle h = store.add_circle(c, r);
        index(h);
        return h;
    }
    void index(EntityHandle h) {
        Vec2 lo;
        Vec2 hi;
        entity_aabb(store, h, lo, hi);
        grid.insert(h, lo, hi);
    }
    SnapResult snap(Vec2 cursor, double radius, std::uint32_t mask = kAllSnaps) {
        return compute_snap(store, kernel, grid, cursor, radius, mask);
    }
};

} // namespace

TEST_CASE("OSNAP: endpoint") {
    Scene s;
    s.add_line({0, 0}, {100, 0});
    const SnapResult r = s.snap({99.5, 0.4}, 2.0);
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Endpoint);
    REQUIRE(r.point == Vec2{100, 0});
}

TEST_CASE("OSNAP: midpoint") {
    Scene s;
    s.add_line({0, 0}, {100, 0});
    // Near the midpoint but away from endpoints; disable Nearest so Midpoint shows.
    const SnapResult r = s.snap({50.3, 0.3}, 2.0,
                                snap_bit(SnapType::Endpoint) | snap_bit(SnapType::Midpoint));
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Midpoint);
    REQUIRE(r.point == Vec2{50, 0});
}

TEST_CASE("OSNAP: center") {
    Scene s;
    s.add_circle({10, 10}, 5.0);
    const SnapResult r = s.snap({10.4, 9.7}, 2.0);
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Center);
    REQUIRE(r.point == Vec2{10, 10});
}

TEST_CASE("OSNAP: intersection of two crossing lines") {
    Scene s;
    // Cross at the origin, with endpoints and midpoints kept well away from it.
    s.add_line({-10, 0}, {30, 0});
    s.add_line({0, -10}, {0, 30});
    const SnapResult r = s.snap({0.3, 0.3}, 1.0);
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Intersection);
    REQUIRE(r.point.x == Approx(0.0).margin(1e-6));
    REQUIRE(r.point.y == Approx(0.0).margin(1e-6));
}

TEST_CASE("OSNAP: nearest on a line body") {
    Scene s;
    s.add_line({0, 0}, {100, 0});
    // Only Nearest enabled; cursor above the middle of the line.
    const SnapResult r = s.snap({37.0, 0.5}, 2.0, snap_bit(SnapType::Nearest));
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Nearest);
    REQUIRE(r.point.x == Approx(37.0));
    REQUIRE(r.point.y == Approx(0.0).margin(1e-9));
}

TEST_CASE("OSNAP: nothing within the aperture") {
    Scene s;
    s.add_line({0, 0}, {100, 0});
    REQUIRE_FALSE(s.snap({50.0, 50.0}, 2.0).found);
}

TEST_CASE("OSNAP: endpoint beats nearest within the aperture (priority)") {
    Scene s;
    s.add_line({0, 0}, {100, 0});
    const SnapResult r = s.snap({0.2, 0.2}, 5.0); // near the start endpoint
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Endpoint);
    REQUIRE(r.point == Vec2{0, 0});
}

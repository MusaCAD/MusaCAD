#include <array>
#include <optional>

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

    void index(EntityHandle h) {
        Vec2 lo;
        Vec2 hi;
        entity_aabb(store, h, lo, hi);
        grid.insert(h, lo, hi);
    }
    EntityHandle line(Vec2 a, Vec2 b) { const auto h = store.add_line(a, b); index(h); return h; }
    EntityHandle circle(Vec2 c, double r) { const auto h = store.add_circle(c, r); index(h); return h; }
    EntityHandle point(Vec2 p) { const auto h = store.add_point(p); index(h); return h; }
    EntityHandle poly(std::span<const Vec2> v, bool closed) {
        const auto h = store.add_polyline(v, closed); index(h); return h;
    }
    SnapResult snap(Vec2 cur, double r, std::uint32_t mask = kAllSnaps,
                    std::optional<Vec2> from = std::nullopt) {
        return compute_snap(store, kernel, grid, cur, r, mask, from);
    }
};
} // namespace

TEST_CASE("OSNAP Quadrant: N/E/S/W of a circle") {
    Scene s;
    s.circle({0, 0}, 5.0);
    const SnapResult r = s.snap({5.0, 0.3}, 1.0); // near the +X quadrant
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Quadrant);
    REQUIRE(r.point.x == Approx(5.0));
    REQUIRE(r.point.y == Approx(0.0).margin(1e-9));
}

TEST_CASE("OSNAP Node: standalone Point entity") {
    Scene s;
    s.point({3, 3});
    const SnapResult r = s.snap({3.1, 3.1}, 1.0);
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Node);
    REQUIRE(r.point == Vec2{3, 3});
}

TEST_CASE("OSNAP Perpendicular: foot from the previous point onto a line") {
    Scene s;
    s.line({0, 0}, {10, 0});
    // from (3,5): perpendicular foot is (3,0); cursor hovers there (not the midpoint).
    const SnapResult r = s.snap({3.0, 0.3}, 1.0, kAllSnaps, Vec2{3, 5});
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Perpendicular);
    REQUIRE(r.point.x == Approx(3.0));
    REQUIRE(r.point.y == Approx(0.0).margin(1e-9));
}

TEST_CASE("OSNAP Tangent: tangent point from an external point to a circle") {
    Scene s;
    s.circle({0, 0}, 5.0);
    // from (10,0): tangent points at +/-60deg -> (2.5, +/-4.330).
    const SnapResult r = s.snap({2.5, 4.0}, 1.0, kAllSnaps, Vec2{10, 0});
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Tangent);
    REQUIRE(r.point.x == Approx(2.5).margin(1e-3));
    REQUIRE(r.point.y == Approx(4.3301).margin(1e-3));
}

TEST_CASE("OSNAP Centroid (Musa extension): centre of a closed polyline") {
    Scene s;
    const std::array<Vec2, 4> sq{{{0, 0}, {4, 0}, {4, 4}, {0, 4}}};
    s.poly(sq, true);
    const SnapResult r = s.snap({2.1, 2.1}, 1.0);
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Centroid);
    REQUIRE(r.point.x == Approx(2.0));
    REQUIRE(r.point.y == Approx(2.0));
}

TEST_CASE("OSNAP mask toggles individual types") {
    Scene s;
    s.circle({0, 0}, 5.0);
    // With Quadrant disabled, the +X quadrant area falls back to Nearest.
    const std::uint32_t no_quadrant = kAllSnaps & ~snap_bit(SnapType::Quadrant);
    const SnapResult r = s.snap({5.0, 0.3}, 1.0, no_quadrant);
    REQUIRE(r.found);
    REQUIRE(r.type != SnapType::Quadrant);
    REQUIRE(r.type == SnapType::Nearest);
}

TEST_CASE("OSNAP precedence: endpoint beats nearest in the aperture") {
    Scene s;
    s.line({0, 0}, {10, 0});
    const SnapResult r = s.snap({0.2, 0.2}, 5.0);
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Endpoint);
    REQUIRE(r.point == Vec2{0, 0});
}

TEST_CASE("Deferred snaps need a from-point") {
    Scene s;
    s.line({0, 0}, {10, 0});
    // No from-point: perpendicular not offered; midpoint/nearest instead.
    const SnapResult r = s.snap({3.0, 0.3}, 1.0, snap_bit(SnapType::Perpendicular));
    REQUIRE_FALSE(r.found); // only Perpendicular enabled, but no from-point
}

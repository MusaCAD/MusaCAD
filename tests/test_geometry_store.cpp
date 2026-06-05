#include <array>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/geometry_store.hpp"

using namespace musacad::core;

TEST_CASE("Store: removal does not invalidate other handles") {
    GeometryStore store;
    const EntityHandle line = store.add_line({0.0, 0.0}, {1.0, 1.0});
    const EntityHandle circle = store.add_circle({0.0, 0.0}, 5.0);
    const EntityHandle point = store.add_point({2.0, 2.0});
    REQUIRE(store.live_count() == 3);

    REQUIRE(store.is_valid(line));
    REQUIRE(store.line(line) != nullptr);
    REQUIRE(store.line(line)->b == Vec2{1.0, 1.0});

    // Typed accessor with the wrong kind returns nullptr.
    REQUIRE(store.point(line) == nullptr);
    REQUIRE(store.circle(point) == nullptr);

    // Removing the circle leaves line and point valid.
    REQUIRE(store.remove(circle));
    REQUIRE_FALSE(store.is_valid(circle));
    REQUIRE(store.circle(circle) == nullptr);
    REQUIRE(store.is_valid(line));
    REQUIRE(store.is_valid(point));
    REQUIRE(store.live_count() == 2);

    // Double remove is a no-op.
    REQUIRE_FALSE(store.remove(circle));
    REQUIRE(store.live_count() == 2);
}

TEST_CASE("Store: churn keeps surviving handles stable") {
    GeometryStore store;
    std::vector<EntityHandle> keep;
    for (int i = 0; i < 100; ++i) {
        keep.push_back(store.add_line({static_cast<double>(i), 0.0}, {static_cast<double>(i), 1.0}));
    }
    // Remove every other line, then add new ones (reusing freed slots).
    for (std::size_t i = 0; i < keep.size(); i += 2) {
        REQUIRE(store.remove(keep[i]));
    }
    for (int i = 0; i < 50; ++i) {
        store.add_line({0.0, 0.0}, {1.0, 1.0});
    }
    // Odd-indexed originals must remain valid with correct data.
    for (std::size_t i = 1; i < keep.size(); i += 2) {
        REQUIRE(store.is_valid(keep[i]));
        REQUIRE(store.line(keep[i])->a.x == static_cast<double>(i));
    }
    // Even-indexed (removed) handles must be invalid even though slots were reused.
    for (std::size_t i = 0; i < keep.size(); i += 2) {
        REQUIRE_FALSE(store.is_valid(keep[i]));
    }
}

TEST_CASE("Store: polyline vertex pool view") {
    GeometryStore store;
    const std::array<Vec2, 3> verts{{{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}}};
    const EntityHandle pl = store.add_polyline(verts, true);

    const PolylineData* data = store.polyline(pl);
    REQUIRE(data != nullptr);
    REQUIRE(data->count == 3);
    REQUIRE(data->closed);

    const std::span<const Vec2> view = store.vertices_of(*data);
    REQUIRE(view.size() == 3);
    REQUIRE(view[1] == Vec2{1.0, 0.0});

    // A second polyline gets a disjoint slice of the pool.
    const std::array<Vec2, 2> verts2{{{5.0, 5.0}, {6.0, 6.0}}};
    const EntityHandle pl2 = store.add_polyline(verts2, false);
    const std::span<const Vec2> view2 = store.vertices_of(*store.polyline(pl2));
    REQUIRE(view2.size() == 2);
    REQUIRE(view2[0] == Vec2{5.0, 5.0});
    // First polyline's view is unaffected.
    REQUIRE(store.vertices_of(*store.polyline(pl))[0] == Vec2{0.0, 0.0});
}

TEST_CASE("Store: arc and spline creation") {
    GeometryStore store;
    const EntityHandle arc = store.add_arc({0.0, 0.0}, 2.0, 0.0, kHalfPi);
    REQUIRE(store.arc(arc) != nullptr);
    REQUIRE(store.arc(arc)->radius == 2.0);

    const std::array<Vec2, 4> ctrl{{{0.0, 0.0}, {1.0, 2.0}, {3.0, 2.0}, {4.0, 0.0}}};
    const EntityHandle spline = store.add_spline(ctrl, 3);
    REQUIRE(store.spline(spline) != nullptr);
    REQUIRE(store.spline(spline)->degree == 3);
    REQUIRE(store.control_points_of(*store.spline(spline)).size() == 4);
}

#include <algorithm>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/spatial_grid.hpp"

using namespace musacad::core;

namespace {
bool contains(const std::vector<EntityHandle>& v, EntityHandle h) {
    return std::find(v.begin(), v.end(), h) != v.end();
}
} // namespace

TEST_CASE("SpatialGrid: query returns overlapping entities only") {
    SpatialGrid grid(10.0);
    const EntityHandle a{0, 1, EntityKind::Line};
    const EntityHandle b{1, 1, EntityKind::Line};
    grid.insert(a, {0, 0}, {5, 5});
    grid.insert(b, {100, 100}, {105, 105});
    REQUIRE(grid.entity_count() == 2);

    std::vector<EntityHandle> out;
    grid.query({-1, -1}, {6, 6}, out);
    REQUIRE(contains(out, a));
    REQUIRE_FALSE(contains(out, b));

    out.clear();
    grid.query({99, 99}, {106, 106}, out);
    REQUIRE(contains(out, b));
    REQUIRE_FALSE(contains(out, a));
}

TEST_CASE("SpatialGrid: handles spanning many cells are deduplicated") {
    SpatialGrid grid(1.0);
    const EntityHandle big{7, 3, EntityKind::Polyline};
    grid.insert(big, {0, 0}, {10, 10}); // covers ~121 cells
    std::vector<EntityHandle> out;
    grid.query({0, 0}, {10, 10}, out);
    REQUIRE(std::count(out.begin(), out.end(), big) == 1);
}

TEST_CASE("SpatialGrid: remove takes the entity out of all its cells") {
    SpatialGrid grid(4.0);
    const EntityHandle a{2, 5, EntityKind::Circle};
    grid.insert(a, {0, 0}, {12, 12});
    grid.remove(a);
    REQUIRE(grid.entity_count() == 0);
    std::vector<EntityHandle> out;
    grid.query({0, 0}, {12, 12}, out);
    REQUIRE(out.empty());
}

TEST_CASE("SpatialGrid: reinsert after remove works (reused handle slot)") {
    SpatialGrid grid(10.0);
    const EntityHandle a{0, 1, EntityKind::Line};
    grid.insert(a, {0, 0}, {1, 1});
    grid.remove(a);
    const EntityHandle a2{0, 3, EntityKind::Line}; // same index, new generation
    grid.insert(a2, {0, 0}, {1, 1});
    std::vector<EntityHandle> out;
    grid.query({0, 0}, {1, 1}, out);
    REQUIRE(contains(out, a2));
    REQUIRE_FALSE(contains(out, a));
}

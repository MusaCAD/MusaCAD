// Insertion throughput benchmark: time to insert 1,000,000 lines into the SoA
// GeometryStore. Run under the `release` preset for a representative number.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/spatial_grid.hpp"

int main() {
    using namespace musacad::core;
    constexpr std::size_t kCount = 1'000'000;

    GeometryStore store;
    store.reserve_lines(kCount);

    const auto t0 = std::chrono::steady_clock::now();
    std::vector<EntityHandle> handles;
    handles.reserve(kCount);
    for (std::size_t i = 0; i < kCount; ++i) {
        const double f = static_cast<double>(i);
        handles.push_back(store.add_line(Vec2{f, 0.0}, Vec2{f, 1.0}));
    }
    const auto t1 = std::chrono::steady_clock::now();

    // Spatial-index maintenance cost (the engine does this per create).
    SpatialGrid grid(16.0);
    const auto t2 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kCount; ++i) {
        const double f = static_cast<double>(i);
        grid.insert(handles[i], Vec2{f, 0.0}, Vec2{f, 1.0});
    }
    const auto t3 = std::chrono::steady_clock::now();

    const double store_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double grid_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
    std::printf("Store insert : %zu lines in %.2f ms (%.2f ns/line, live=%zu)\n", kCount, store_ms,
                store_ms * 1e6 / static_cast<double>(kCount), store.live_count());
    std::printf("Index insert : %zu AABBs in %.2f ms (%.2f ns/entity, indexed=%zu)\n", kCount,
                grid_ms, grid_ms * 1e6 / static_cast<double>(kCount), grid.entity_count());
    return (store.live_count() == kCount && grid.entity_count() == kCount) ? 0 : 1;
}

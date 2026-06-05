// Insertion throughput benchmark: time to insert 1,000,000 lines into the SoA
// GeometryStore. Run under the `release` preset for a representative number.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/osnap.hpp"
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

    // OSNAP per-query cost (one query per frame) with ALL snap types on, on a
    // moderately dense local scene (~5000 entities in a 200x200 area).
    GeometryStore s2;
    NativeKernel2D kernel;
    SpatialGrid g2(8.0);
    constexpr int kSide = 70; // 70*70 = 4900 lines
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            const Vec2 a{static_cast<double>(x) * 3.0, static_cast<double>(y) * 3.0};
            const EntityHandle h = s2.add_line(a, a + Vec2{2.0, 1.0});
            Vec2 lo;
            Vec2 hi;
            entity_aabb(s2, h, lo, hi);
            g2.insert(h, lo, hi);
        }
    }
    constexpr int kQueries = 200'000;
    const double radius = 2.0;
    std::size_t hits = 0;
    const auto q0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kQueries; ++i) {
        const double t = static_cast<double>(i % 1000) * 0.2;
        const Vec2 cursor{50.0 + t, 50.0 + t};
        const SnapResult r =
            compute_snap(s2, kernel, g2, cursor, radius, kAllSnaps, Vec2{40.0, 60.0});
        hits += r.found ? 1 : 0;
    }
    const auto q1 = std::chrono::steady_clock::now();
    const double snap_us = std::chrono::duration<double, std::micro>(q1 - q0).count();
    std::printf("OSNAP query  : %d queries over %d entities, all snaps on -> %.3f us/query "
                "(found=%zu)\n",
                kQueries, kSide * kSide, snap_us / kQueries, hits);

    return (store.live_count() == kCount && grid.entity_count() == kCount) ? 0 : 1;
}

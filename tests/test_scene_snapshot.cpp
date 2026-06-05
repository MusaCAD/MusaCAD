#include <array>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/scene_snapshot.hpp"

using namespace musacad::core;

// Constraint A (core level): the snapshot reflects only LIVE geometry. The
// polyline/spline vertex pools are append-only, so this proves that deleting
// entities returns the snapshot's primitive count to baseline rather than the
// high-water mark -- no dead-pool residue is ever uploaded.
TEST_CASE("Snapshot reflects only live geometry; deletions return to baseline") {
    GeometryStore store;
    NativeKernel2D kernel;
    RenderSnapshot snap;

    build_render_snapshot(store, kernel, snap);
    REQUIRE(snap.points.empty());
    REQUIRE(snap.line_vertices.empty());

    // One persistent line as the baseline.
    store.add_line({0.0, 0.0}, {1.0, 0.0});
    build_render_snapshot(store, kernel, snap);
    const std::size_t baseline_verts = snap.line_vertices.size();
    REQUIRE(baseline_verts == 2);

    // Add many open polylines (each pushes vertices into the append-only pool).
    const std::array<Vec2, 5> verts{{{0, 0}, {1, 1}, {2, 0}, {3, 1}, {4, 0}}};
    std::vector<EntityHandle> handles;
    for (int i = 0; i < 1000; ++i) {
        handles.push_back(store.add_polyline(verts, false));
    }
    build_render_snapshot(store, kernel, snap);
    const std::size_t loaded_verts = snap.line_vertices.size();
    // 5 vertices -> 4 segments -> 8 line endpoints per polyline.
    REQUIRE(loaded_verts == baseline_verts + 1000 * 8);

    // Delete every polyline. The pool keeps their vertices, but the snapshot
    // must not.
    for (const EntityHandle h : handles) {
        REQUIRE(store.remove(h));
    }
    build_render_snapshot(store, kernel, snap);
    REQUIRE(snap.line_vertices.size() == baseline_verts); // back to baseline, not high-water
}

TEST_CASE("Snapshot points track live point entities") {
    GeometryStore store;
    NativeKernel2D kernel;
    RenderSnapshot snap;

    const EntityHandle a = store.add_point({1.0, 2.0});
    store.add_point({3.0, 4.0});
    build_render_snapshot(store, kernel, snap);
    REQUIRE(snap.points.size() == 2);

    REQUIRE(store.remove(a));
    build_render_snapshot(store, kernel, snap);
    REQUIRE(snap.points.size() == 1);
    REQUIRE(snap.points[0] == Vec2{3.0, 4.0});
}

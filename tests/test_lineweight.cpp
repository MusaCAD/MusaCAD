// Phase 14 Part B/C: lineweight flows into render batches; ByLayer + override
// both resolve; dimension arrowheads produce filled triangles.

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/scene_snapshot.hpp"

using namespace musacad::core;

namespace {
const ColorBatch* batch_with_weight(const RenderSnapshot& s, std::uint8_t lw) {
    for (const ColorBatch& b : s.line_batches) {
        if (b.lineweight == lw) {
            return &b;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE("ByLayer and override lineweights both reach the render batches") {
    GeometryStore store;
    NativeKernel2D kernel;

    // A heavy layer (ByLayer 0.60 mm) and a default layer (0.25 mm).
    Layer heavy;
    heavy.name = "heavy";
    heavy.lineweight = 60;
    const std::uint16_t heavy_idx = store.add_layer(heavy);

    store.add_line({0, 0}, {10, 0}, EntityProps{0}); // ByLayer on layer 0 (25)
    store.add_line({0, 1}, {10, 1}, EntityProps{heavy_idx}); // ByLayer on heavy (60)

    // An explicit per-entity override of 1.00 mm, regardless of layer.
    EntityProps ov{0};
    ov.lineweight = 100;
    ov.set_lineweight_by_layer(false);
    store.add_line({0, 2}, {10, 2}, ov);

    RenderSnapshot snap;
    build_render_snapshot(store, kernel, snap, 0.1);

    REQUIRE(batch_with_weight(snap, 25) != nullptr);  // ByLayer default
    REQUIRE(batch_with_weight(snap, 60) != nullptr);  // ByLayer heavy
    REQUIRE(batch_with_weight(snap, 100) != nullptr); // explicit override
}

TEST_CASE("Dimension arrowheads produce filled triangles in the fill channel") {
    GeometryStore store;
    NativeKernel2D kernel;
    store.add_dimension(DimType::Linear, {0, 0}, {20, 0}, {10, 5}, 0, {});

    RenderSnapshot snap;
    build_render_snapshot(store, kernel, snap, 0.1);

    REQUIRE(!snap.fill_vertices.empty());
    REQUIRE(snap.fill_vertices.size() % 3 == 0); // whole triangles
    REQUIRE(!snap.fill_batches.empty());
}

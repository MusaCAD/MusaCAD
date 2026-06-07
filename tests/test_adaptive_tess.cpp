// Phase 16 Part A: zoom-adaptive curve tessellation.
//  - the kernel samples a curve to a chord tolerance (finer tolerance -> more
//    segments), and the segment count is bounded (capped) at extreme zoom-in;
//  - the engine re-tessellates curves when the view scale crosses a zoom bucket,
//    but NOT on pan/cursor moves; stored geometry stays parametric.

#include <chrono>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/render_snapshot.hpp"

using namespace musacad::core;

TEST_CASE("Kernel: finer chord tolerance tessellates a circle into more segments") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle circle = store.add_circle({0, 0}, 100.0);

    std::vector<Vec2> coarse;
    std::vector<Vec2> fine;
    kernel.tessellate(store, circle, 1.0, coarse);  // ~loose
    kernel.tessellate(store, circle, 0.01, fine);   // ~tight
    REQUIRE(fine.size() > coarse.size());
    // Every chord stays within tolerance: the polyline has many short segments.
    REQUIRE(fine.size() > 50);
}

TEST_CASE("Kernel: segment count is bounded at extreme zoom-in (cap holds)") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle circle = store.add_circle({0, 0}, 1.0e6);
    std::vector<Vec2> out;
    // Absurdly tight tolerance would ask for millions of segments; the cap bounds it.
    kernel.tessellate(store, circle, 1.0e-9, out);
    REQUIRE(out.size() <= 8192 + 1); // kMaxArcSegments (+closing vertex)
    REQUIRE(out.size() > 1000);      // still finely sampled
}

namespace {
template <class Pred>
bool wait_until(GeometryEngine& e, Pred pred) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        e.consume_snapshot();
        if (pred(e.snapshot())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
} // namespace

TEST_CASE("Engine: zooming in re-tessellates a circle finer; the store stays parametric") {
    GeometryEngine engine;
    engine.start();
    // Start zoomed out (1 world unit per pixel) and draw a big circle.
    engine.submit(SetViewScaleCommand{1.0});
    engine.submit(AddCircleCommand{{0, 0}, 500.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    const std::size_t coarse = engine.snapshot().line_vertices.size();

    // Zoom in hard (0.001 world units per pixel): a new zoom bucket -> finer tess.
    engine.submit(SetViewScaleCommand{0.001});
    REQUIRE(wait_until(engine, [coarse](const auto& s) {
        return s.line_vertices.size() > coarse;
    }));
    REQUIRE(engine.snapshot().line_vertices.size() > coarse);
    engine.stop();
}

TEST_CASE("Engine: a same-bucket scale nudge does NOT re-tessellate (pan/zoom-jitter safe)") {
    GeometryEngine engine;
    engine.start();
    engine.submit(SetViewScaleCommand{1.0});
    engine.submit(AddCircleCommand{{0, 0}, 500.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    const std::uint64_t gv = engine.snapshot().geometry_version;
    const std::size_t n = engine.snapshot().line_vertices.size();

    // A tiny scale change within the same half-octave bucket: no re-tessellation.
    engine.submit(SetViewScaleCommand{1.02});
    // Also move the cursor a lot (simulating pan) -- must not re-tessellate either.
    for (int i = 0; i < 5; ++i) {
        engine.submit(SetCursorCommand{{static_cast<double>(i) * 10.0, 0.0}, 5.0, false,
                                       kAllSnaps, {}, false});
    }
    // Let the engine process; geometry_version must be unchanged (no rebuild).
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().geometry_version == gv);
    REQUIRE(engine.snapshot().line_vertices.size() == n);
    engine.stop();
}

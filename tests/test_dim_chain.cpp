// DIMCONTINUE / DIMBASELINE (issue #28). Placement helpers over the EXISTING
// Linear/Aligned types -- they only decide where the next dimension's def points and
// dimension line go, so there is no new DimType and no format change.

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad::core;
using Catch::Approx;

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
bool status_has(GeometryEngine& e, const char* n) {
    return wait_until(e, [n](const auto& s) { return s.status.find(n) != std::string::npos; });
}
/// Wait for the drawing's right-hand extent to reach `x`. Chained dimensions all report
/// the SAME status string, so waiting on the status would return instantly from the
/// PREVIOUS chain's message and read the snapshot before this one had been applied --
/// which is exactly how these tests first failed against correct code.
bool wait_extent(GeometryEngine& e, double x) {
    return wait_until(e, [x](const auto& s) { return s.has_bounds && s.bounds_max.x >= x - 3.0; });
}
} // namespace

TEST_CASE("#28: DIMCONTINUE chains from the previous second extension line") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear),
                                      {0, 0}, {20, 0}, {10, -12}, 0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    const std::size_t after_first = engine.snapshot().line_vertices.size();

    engine.submit(ChainDimensionCommand{{50, 0}, /*baseline=*/false, 2});
    REQUIRE(wait_extent(engine, 50.0));
    REQUIRE(status_has(engine, "Continued dimension added."));
    // A second dimension really was drawn, and the chain reaches the new origin.
    REQUIRE(engine.snapshot().line_vertices.size() > after_first);
    REQUIRE(engine.snapshot().bounds_max.x == Approx(50.0).margin(3.0));
}

TEST_CASE("#28: DIMBASELINE stacks, offsetting the dimension line") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear),
                                      {0, 0}, {20, 0}, {10, -12}, 0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.consume_snapshot();
    const double y_before = engine.snapshot().bounds_min.y;

    engine.submit(ChainDimensionCommand{{50, 0}, /*baseline=*/true, 2});
    // The stacked dimension's line sits FURTHER from the def points, so the drawing
    // extends further in the direction the first dimension was offset (-y here).
    REQUIRE(wait_until(engine, [y_before](const auto& s) { return s.bounds_min.y < y_before - 1.0; }));
    REQUIRE(status_has(engine, "Baseline dimension added."));
}

TEST_CASE("#28: chaining reports honestly when there is nothing to chain from") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    engine.submit(ChainDimensionCommand{{50, 0}, false, 2});
    REQUIRE(status_has(engine, "no previous dimension to continue from"));
    engine.submit(ChainDimensionCommand{{50, 0}, true, 3});
    REQUIRE(status_has(engine, "no previous dimension to stack from"));
}

TEST_CASE("#28: a radial dimension cannot be chained, and the engine says so") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Radius),
                                      {0, 0}, {10, 0}, {10, 0}, 0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(ChainDimensionCommand{{50, 0}, false, 2});
    REQUIRE(status_has(engine, "needs a linear, aligned, ordinate or angular dimension"));
}

TEST_CASE("#28: each chained dimension is its own undo group") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear),
                                      {0, 0}, {20, 0}, {10, -12}, 0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(ChainDimensionCommand{{40, 0}, false, 2});
    REQUIRE(wait_extent(engine, 40.0));
    engine.submit(ChainDimensionCommand{{60, 0}, false, 3});
    REQUIRE(wait_extent(engine, 60.0));
    REQUIRE(engine.snapshot().bounds_max.x == Approx(60.0).margin(3.0));

    // One undo removes only the LAST chained dimension.
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.bounds_max.x < 55.0; }));
    REQUIRE(engine.snapshot().bounds_max.x == Approx(40.0).margin(3.0));
}

TEST_CASE("#28: the chain follows UNDO -- it continues from whatever is now last") {
    // most_recent_dimension() walks the undo log rather than caching a handle, so undoing
    // a dimension makes the chain continue from the one before it with no state to resync.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear),
                                      {0, 0}, {20, 0}, {10, -12}, 0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(ChainDimensionCommand{{40, 0}, false, 2});
    REQUIRE(wait_extent(engine, 40.0));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.bounds_max.x < 35.0; }));

    // Chaining again now continues from the FIRST dimension (b = 20), not the undone one.
    engine.submit(ChainDimensionCommand{{35, 0}, false, 3});
    REQUIRE(wait_extent(engine, 35.0));
    REQUIRE(engine.snapshot().bounds_max.x == Approx(35.0).margin(3.0));
}

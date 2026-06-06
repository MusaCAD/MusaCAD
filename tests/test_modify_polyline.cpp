// Fillet/Chamfer on the corner between two adjacent segments of a polyline
// (the common rectangle case), plus the honest engine status channel.

#include <chrono>
#include <cmath>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad::core;

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
bool has_segment(const RenderSnapshot& s, Vec2 a, Vec2 b, double eps = 1e-6) {
    const auto eq = [&](Vec2 p, Vec2 q) {
        return std::abs(p.x - q.x) < eps && std::abs(p.y - q.y) < eps;
    };
    for (std::size_t i = 0; i + 1 < s.line_vertices.size(); i += 2) {
        if ((eq(s.line_vertices[i], a) && eq(s.line_vertices[i + 1], b)) ||
            (eq(s.line_vertices[i], b) && eq(s.line_vertices[i + 1], a))) {
            return true;
        }
    }
    return false;
}
// A closed rectangle polyline: corner (10,0) is shared by the bottom & right edges.
void add_rect(GeometryEngine& e) {
    e.submit(AddPolylineCommand{{{0, 0}, {10, 0}, {10, 5}, {0, 5}}, true, 1});
}
} // namespace

TEST_CASE("CHAMFER bevels a polyline (rectangle) corner; status is honest; undo restores") {
    GeometryEngine engine;
    engine.start();
    add_rect(engine);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));

    // Pick the bottom edge (5,0) and the right edge (10,2.5): corner (10,0).
    engine.submit(ChamferPickCommand{{5, 0}, {10, 2.5}, 2.0, 2.0, 1.0, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {8, 0}, {10, 2}); }));
    REQUIRE(engine.snapshot().status == "Chamfered.");

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {10, 0}, {10, 5}); }));
    engine.stop();
}

TEST_CASE("FILLET rounds a polyline (rectangle) corner with arc vertices; undo restores") {
    GeometryEngine engine;
    engine.start();
    add_rect(engine);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));

    engine.submit(FilletPickCommand{{5, 0}, {10, 2.5}, 2.0, 1.0, 10});
    // The corner is replaced by a tangent arc approximated with vertices -> the
    // polyline grows well beyond its original 4 corners.
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 8; }));
    REQUIRE(engine.snapshot().status == "Filleted.");

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));
    engine.stop();
}

TEST_CASE("Honest status: non-adjacent / impossible picks report failure, change nothing") {
    GeometryEngine engine;
    engine.start();
    add_rect(engine);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));

    // Bottom edge (5,0) and top edge (5,5): opposite sides, not adjacent.
    engine.submit(ChamferPickCommand{{5, 0}, {5, 5}, 2.0, 2.0, 1.0, 10});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status == "Chamfer: pick two adjacent edges of the polyline.";
    }));
    REQUIRE(engine.snapshot().line_vertices.size() == 8); // unchanged

    // Extending a closed polyline: nothing to extend.
    engine.submit(ExtendPickCommand{{5, 0}, 1.0, 11});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.rfind("Extend:", 0) == 0;
    }));
    REQUIRE(engine.snapshot().line_vertices.size() == 8);
    engine.stop();
}

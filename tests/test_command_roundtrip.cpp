// End-to-end: command-line text on the UI side -> MPSC queue -> geometry thread
// mutates the store -> snapshot -> (would render). The UI side never touches the
// GeometryStore; it only emits core::Command messages via the processor sink.

#include <chrono>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad::command;
using musacad::core::GeometryEngine;

namespace {

struct SilentOutput : CommandOutput {
    void append_line(const std::string&) override {}
    void set_prompt(const std::string&) override {}
};

// Consumes snapshots until the predicate holds or the timeout elapses.
template <class Pred>
bool wait_until(GeometryEngine& engine, Pred pred) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        engine.consume_snapshot();
        if (pred(engine.snapshot())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

} // namespace

TEST_CASE("Round-trip: L 0,0 @100,0 ESC produces a horizontal line in the snapshot") {
    GeometryEngine engine;
    engine.start();
    SilentOutput out;
    CommandProcessor proc([&engine](musacad::core::Command c) { engine.submit(std::move(c)); },
                          nullptr, out);

    proc.submit_line("L");
    proc.submit_line("0,0");
    proc.submit_line("@100,0");
    proc.cancel();

    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    const auto& s = engine.snapshot();
    REQUIRE(s.line_vertices[0] == musacad::core::Vec2{0.0, 0.0});
    REQUIRE(s.line_vertices[1] == musacad::core::Vec2{100.0, 0.0});

    // U undoes the whole LINE command -> back to empty.
    proc.submit_line("U");
    REQUIRE(wait_until(engine, [](const auto& s2) { return s2.line_vertices.empty(); }));

    engine.stop();
}

TEST_CASE("Round-trip: ERASE last then UNDO restores the entity") {
    GeometryEngine engine;
    engine.start();
    SilentOutput out;
    CommandProcessor proc([&engine](musacad::core::Command c) { engine.submit(std::move(c)); },
                          nullptr, out);

    // Create a circle (tessellates to many line vertices).
    proc.submit_line("C");
    proc.submit_line("0,0");
    proc.submit_line("10");
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 8; }));
    const std::size_t with_circle = engine.snapshot().line_vertices.size();

    // Erase last -> empty.
    proc.submit_line("ERASE");
    proc.submit_line("L");
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));

    // Undo the erase -> circle restored.
    proc.submit_line("U");
    REQUIRE(wait_until(engine, [with_circle](const auto& s) {
        return s.line_vertices.size() == with_circle;
    }));

    engine.stop();
}

// The actual interactive case the user hits: a RECTANGLE (closed 4-vertex
// polyline) created by the REC command, then FILLET driven through the full
// command path (radius prompt + two edge picks) -- not a raw FilletPickCommand.
// This is the end-to-end stack the synthetic engine test (test_modify_polyline)
// does not exercise: REC command -> queue -> store -> snapshot, then F command
// -> pick_nearest resolves both picks to the SAME polyline -> corner rounds.
TEST_CASE("Round-trip: FILLET rounds a RECTANGLE corner via the full command path") {
    GeometryEngine engine;
    engine.start();
    SilentOutput out;
    CommandProcessor proc([&engine](musacad::core::Command c) { engine.submit(std::move(c)); },
                          nullptr, out);
    proc.set_pick_radius(1.0);

    // Draw the rectangle exactly as a user would: REC, first corner, other corner.
    proc.submit_line("REC");
    proc.submit_line("0,0");
    proc.submit_line("10,5");
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));

    // FILLET corner (10,0): radius 2, pick the bottom edge then the right edge.
    proc.submit_line("F");
    proc.submit_line("2");      // Specify fillet radius
    proc.submit_line("5,0");    // first edge  (bottom)
    proc.submit_line("10,2.5"); // second edge (right) -> shared corner (10,0)
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Filleted."; }));
    // The rounding arc tessellates to MORE than the original 8 line vertices.
    REQUIRE(engine.snapshot().line_vertices.size() > 8);

    // Undo restores the pristine rectangle.
    proc.submit_line("U");
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));

    engine.stop();
}

// The corner the synthetic test never touches: vertex 0, where the CLOSING edge
// (vertex 3->0) meets the first edge (0->1). Exercises shared_vertex()'s closed
// wrap branch and the closing-segment tessellation -- the most likely place a
// "rectangle isn't a polyline corner" bug would have hidden.
TEST_CASE("Round-trip: FILLET rounds the RECTANGLE wrap corner (closing edge)") {
    GeometryEngine engine;
    engine.start();
    SilentOutput out;
    CommandProcessor proc([&engine](musacad::core::Command c) { engine.submit(std::move(c)); },
                          nullptr, out);
    proc.set_pick_radius(1.0);

    proc.submit_line("REC");
    proc.submit_line("0,0");
    proc.submit_line("10,5");
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));

    // Corner (0,0) = vertex 0: pick the left/closing edge then the bottom edge.
    proc.submit_line("F");
    proc.submit_line("2");
    proc.submit_line("0,2.5"); // left (closing) edge, vertex 3 -> 0
    proc.submit_line("5,0");   // bottom edge, vertex 0 -> 1
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Filleted."; }));
    REQUIRE(engine.snapshot().line_vertices.size() > 8);

    engine.stop();
}

// CHAMFER through the same full command path, for parity with FILLET.
TEST_CASE("Round-trip: CHAMFER bevels a RECTANGLE corner via the full command path") {
    GeometryEngine engine;
    engine.start();
    SilentOutput out;
    CommandProcessor proc([&engine](musacad::core::Command c) { engine.submit(std::move(c)); },
                          nullptr, out);
    proc.set_pick_radius(1.0);

    proc.submit_line("REC");
    proc.submit_line("0,0");
    proc.submit_line("10,5");
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));

    proc.submit_line("CHA");
    proc.submit_line("2");      // first chamfer distance
    proc.submit_line("2");      // second chamfer distance
    proc.submit_line("5,0");    // bottom edge
    proc.submit_line("10,2.5"); // right edge -> corner (10,0)
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Chamfered."; }));
    // Bevel: corner vertex -> two vertices => 5-vertex closed polyline = 10 line verts.
    REQUIRE(engine.snapshot().line_vertices.size() == 10);

    engine.stop();
}

TEST_CASE("Round-trip: snapshot carries world bounds for ZOOM extents") {
    GeometryEngine engine;
    engine.start();
    SilentOutput out;
    CommandProcessor proc([&engine](musacad::core::Command c) { engine.submit(std::move(c)); },
                          nullptr, out);

    proc.submit_line("L");
    proc.submit_line("0,0");
    proc.submit_line("100,50");
    proc.submit_line("");

    REQUIRE(wait_until(engine, [](const auto& s) { return s.has_bounds; }));
    const auto& s = engine.snapshot();
    REQUIRE(s.bounds_min == musacad::core::Vec2{0.0, 0.0});
    REQUIRE(s.bounds_max == musacad::core::Vec2{100.0, 50.0});

    engine.stop();
}

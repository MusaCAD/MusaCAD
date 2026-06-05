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

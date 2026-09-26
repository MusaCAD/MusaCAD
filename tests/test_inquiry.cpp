// Inquiry commands (issue #30): AREA and LIST. Read-only queries resolved on the
// geometry thread and reported through the existing status channel, so the UI never
// touches the store and nothing in the data model changes.
//
// (DIST and ID are answered from the picked points alone and never reach the engine,
// so they are exercised through the command processor rather than here.)

#include <chrono>
#include <string>
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
bool status_contains(GeometryEngine& e, const char* needle) {
    return wait_until(e, [needle](const auto& s) {
        return s.status.find(needle) != std::string::npos;
    });
}
} // namespace

TEST_CASE("#30: AREA reports a circle's exact area and circumference") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 10.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    engine.submit(AreaQueryCommand{{10.0, 0.0}, 2.0}); // pick on the circumference
    // pi r^2 = 314.159..., 2 pi r = 62.83...
    REQUIRE(status_contains(engine, "Area = 314.1593")); // UNITS default: 4 decimals
    REQUIRE(engine.snapshot().status.find("Circumference = 62.8319") != std::string::npos);
}

TEST_CASE("#30: AREA of a closed polyline uses the shoelace formula") {
    GeometryEngine engine;
    engine.start();
    // A 10 x 20 rectangle: area 200, perimeter 60.
    AddPolylineCommand pl;
    pl.points = {{0, 0}, {10, 0}, {10, 20}, {0, 20}};
    pl.closed = true;
    pl.group = 1;
    engine.submit(pl);
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    engine.submit(AreaQueryCommand{{5.0, 0.0}, 2.0}); // pick on the bottom edge
    REQUIRE(status_contains(engine, "Area = 200"));
    REQUIRE(engine.snapshot().status.find("Perimeter = 60") != std::string::npos);
}

TEST_CASE("#30: an OPEN object reports its length, and its area as if closed (AutoCAD's rule)") {
    // AutoCAD measures an open object as if a line closed it and reports its own length
    // as Length: a lone line encloses nothing.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {30, 40}, 1}); // 3-4-5 -> length 50
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    engine.submit(AreaQueryCommand{{15.0, 20.0}, 2.0});
    REQUIRE(status_contains(engine, "Length = 50"));
    REQUIRE(engine.snapshot().status.find("Area = 0.0000") != std::string::npos);
}

TEST_CASE("#30: AREA and LIST report honestly when nothing is under the pick") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    engine.submit(AreaQueryCommand{{500.0, 500.0}, 1.0});
    REQUIRE(status_contains(engine, "AREA: no object found"));
    engine.submit(ListQueryCommand{{500.0, 500.0}, 1.0});
    REQUIRE(status_contains(engine, "LIST: no object found"));
}

TEST_CASE("#30: LIST names the entity, its layer and its defining parameters") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {30, 40}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    engine.submit(ListQueryCommand{{15.0, 20.0}, 2.0});
    REQUIRE(status_contains(engine, "Line"));
    const std::string s = engine.snapshot().status;
    REQUIRE(s.find("Layer: \"0\"") != std::string::npos); // AutoCAD's LIST block
    REQUIRE(s.find("length 50") != std::string::npos);
}

TEST_CASE("#30: LIST reports a dimension's MEASURED value, which cannot have been authored") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear),
                                      {0, 0}, {75, 0}, {37.5, -10}, 0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    engine.submit(ListQueryCommand{{37.5, -10.0}, 3.0});
    REQUIRE(status_contains(engine, "measures 75"));
}

TEST_CASE("#30: LIST names a circle's centre and radius") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{5, 6}, 7.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    engine.submit(ListQueryCommand{{12.0, 6.0}, 2.0});
    REQUIRE(status_contains(engine, "CIRCLE"));
    const std::string s = engine.snapshot().status;
    REQUIRE(s.find("centre (5.0000,6.0000)") != std::string::npos); // UNITS default: 4 decimals
    REQUIRE(s.find("radius 7.0000") != std::string::npos);
}

TEST_CASE("#30: an inquiry never mutates the drawing") {
    // The point of routing these through the engine rather than the store is that they
    // stay read-only: no geometry version bump, no undo entry.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 10.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.consume_snapshot();
    const std::uint64_t gv = engine.snapshot().geometry_version;
    const std::size_t verts = engine.snapshot().line_vertices.size();

    engine.submit(AreaQueryCommand{{10.0, 0.0}, 2.0});
    REQUIRE(status_contains(engine, "Area ="));
    engine.submit(ListQueryCommand{{10.0, 0.0}, 2.0});
    REQUIRE(status_contains(engine, "CIRCLE"));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().geometry_version == gv);
    REQUIRE(engine.snapshot().line_vertices.size() == verts);
}

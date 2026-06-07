// Part C: the dimension model -- measured value computed from def points, the
// resolved geometry (ext/dim lines + arrows + label), and style propagation.

#include <chrono>
#include <thread>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"

using namespace musacad::core;
using Catch::Approx;

TEST_CASE("Measured value is computed from def points (never baked)") {
    DimData lin;
    lin.type = DimType::Linear;
    lin.a = {0, 0};
    lin.b = {10, 0};
    lin.line_pt = {5, 3};
    REQUIRE(dim_measure(lin) == Approx(10.0));

    // Move a def point -> the measurement follows.
    lin.b = {7, 0};
    REQUIRE(dim_measure(lin) == Approx(7.0));

    DimData ali;
    ali.type = DimType::Aligned;
    ali.a = {0, 0};
    ali.b = {3, 4};
    ali.line_pt = {0, 5};
    REQUIRE(dim_measure(ali) == Approx(5.0)); // 3-4-5
}

TEST_CASE("compute_dim_geometry builds ext/dim lines + arrows + label per style") {
    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {10, 0};
    d.line_pt = {5, 3};
    DimStyle s;
    s.precision = 2;

    const DimGeometry g = compute_dim_geometry(d, s, Rgb{255, 255, 255});
    REQUIRE(g.label == "10.00");
    REQUIRE(g.ext_lines.size() == 4);       // two extension lines
    REQUIRE(g.dim_lines.size() >= 2);       // the dimension line
    REQUIRE(!g.arrow_fills.empty());        // solid filled arrowheads (triangles)
    REQUIRE(g.arrow_fills.size() % 3 == 0);

    // Precision is a style property: fewer decimals -> different label.
    s.precision = 0;
    REQUIRE(compute_dim_geometry(d, s, Rgb{}).label == "10");
}

TEST_CASE("Radius / diameter / angular: computed value, prefix/suffix, geometry") {
    DimStyle s;
    s.precision = 1;

    DimData rad;
    rad.type = DimType::Radius;
    rad.a = {0, 0};   // centre
    rad.b = {10, 0};  // point on circle
    REQUIRE(dim_measure(rad) == Approx(10.0));
    const DimGeometry gr = compute_dim_geometry(rad, s, Rgb{});
    REQUIRE(gr.label == "R10.0");
    REQUIRE(!gr.dim_lines.empty());
    REQUIRE(!gr.arrow_fills.empty());

    DimData dia = rad;
    dia.type = DimType::Diameter;
    REQUIRE(dim_measure(dia) == Approx(20.0));
    REQUIRE(compute_dim_geometry(dia, s, Rgb{}).label == "⌀20.0"); // diameter prefix

    DimData ang;
    ang.type = DimType::Angular;
    ang.a = {0, 0};       // vertex
    ang.b = {10, 0};      // ray 1 (+x)
    ang.line_pt = {0, 10}; // ray 2 (+y)
    REQUIRE(dim_measure(ang) == Approx(90.0));
    const DimGeometry ga = compute_dim_geometry(ang, s, Rgb{});
    REQUIRE(ga.label == "90.0°"); // degree suffix
    REQUIRE(!ga.dim_lines.empty());     // the arc
}

TEST_CASE("DIMSTYLE per-element colours resolve (explicit wins, ByLayer uses base)") {
    DimStyle s;
    s.arrow_color = {false, Rgb{255, 0, 0}};   // explicit red
    s.text_color = {false, Rgb{255, 255, 0}};  // explicit yellow
    // dim_color stays ByLayer -> uses the base colour.
    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {10, 0};
    d.line_pt = {5, 3};
    const DimGeometry g = compute_dim_geometry(d, s, Rgb{0, 0, 255}); // base blue
    REQUIRE(g.arrow_color == Rgb{255, 0, 0});
    REQUIRE(g.text_color == Rgb{255, 255, 0});
    REQUIRE(g.dim_color == Rgb{0, 0, 255}); // ByLayer -> base
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

TEST_CASE("DIMLINEAR through the engine: renders, undoable, style change propagates") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear),
                                      {0, 0},
                                      {10, 0},
                                      {5, 3},
                                      0,
                                      1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    const std::size_t with_label = engine.snapshot().line_vertices.size();

    // Change the Standard style's precision -> the dimension re-renders (the
    // "10.00" label becomes "10", so the glyph segment count drops).
    DimStyle s;
    s.name = "Standard";
    s.precision = 0;
    engine.submit(SetDimStyleCommand{0, s});
    REQUIRE(wait_until(engine, [with_label](const auto& sn) {
        return !sn.line_vertices.empty() && sn.line_vertices.size() != with_label;
    }));

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    engine.stop();
}

namespace {
// Count line-segment vertices within `r` of point `p` in the snapshot.
std::size_t verts_near(const RenderSnapshot& s, Vec2 p, double r) {
    std::size_t n = 0;
    for (const Vec2& v : s.line_vertices) {
        if (length_squared(v - p) <= r * r) {
            ++n;
        }
    }
    return n;
}
} // namespace

TEST_CASE("Object radius dimension reads the circle's own centre + radius") {
    GeometryEngine engine;
    engine.start();
    // A circle r=10 at the origin: nothing of it comes near the centre.
    engine.submit(AddCircleCommand{{0, 0}, 10.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    REQUIRE(verts_near(engine.snapshot(), {0, 0}, 0.5) == 0);

    // Select the circle (pick on its rim) and place to the right: the radius dim
    // line runs centre -> edge, so a vertex appears AT the circle's centre, proving
    // the value came from the entity's geometry (not from raw picked points).
    engine.submit(AddObjectDimensionCommand{static_cast<std::uint8_t>(DimType::Radius),
                                            {10, 0}, {25, 0}, 2.0, 0, 2});
    REQUIRE(wait_until(engine,
                       [](const auto& s) { return verts_near(s, {0, 0}, 0.5) > 0; }));
    engine.stop();
}

TEST_CASE("Object linear dimension reads a selected line's endpoints") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(AddObjectDimensionCommand{static_cast<std::uint8_t>(DimType::Linear),
                                            {5, 0}, {5, 5}, 1.0, 0, 2});
    // Extension + dimension lines + arrows + label all add geometry.
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 2; }));
    engine.stop();
}

TEST_CASE("Object angular dimension measures the angle between two selected lines") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});  // +x
    engine.submit(AddLineCommand{{0, 0}, {0, 10}, 1});  // +y (90 deg)
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    engine.submit(AddObjectDimensionCommand{static_cast<std::uint8_t>(DimType::Angular),
                                            {5, 0}, {0, 5}, 1.0, 0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 4; }));
    engine.stop();
}

TEST_CASE("Dangling-ref policy: deleting the dimensioned circle leaves the dim intact") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 10.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(AddObjectDimensionCommand{static_cast<std::uint8_t>(DimType::Radius),
                                            {10, 0}, {25, 0}, 2.0, 0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return verts_near(s, {0, 0}, 0.5) > 0; }));

    // Erase the circle by picking its top rim (clear of the x-axis dim line). The
    // dimension captured def points at creation, so it must survive (no dangling
    // reference, no crash). Wait for the circle's rim vertices to vanish...
    engine.submit(ErasePickCommand{{0, 10}, 2.0, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return verts_near(s, {0, 10}, 0.5) == 0; }));
    // ...then the dimension's centre-endpoint vertex must still be present.
    REQUIRE(verts_near(engine.snapshot(), {0, 0}, 0.5) > 0);
    engine.stop();
}

TEST_CASE("Object dimension on empty space creates nothing (honest, no crash)") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddObjectDimensionCommand{static_cast<std::uint8_t>(DimType::Radius),
                                            {100, 100}, {120, 100}, 2.0, 0, 1});
    // Give the engine time to process; nothing should render.
    REQUIRE_FALSE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.stop();
}

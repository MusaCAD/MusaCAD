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

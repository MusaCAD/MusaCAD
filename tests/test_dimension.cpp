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

TEST_CASE("Object-dim placement preview resolve is non-mutating (no entity, no geom bump)") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{50, 0}, 10.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    const std::size_t base = engine.snapshot().line_vertices.size();
    const std::uint64_t gv = engine.snapshot().geometry_version;

    // What an object-dim command issues at the object pick (for the live preview).
    engine.submit(ResolveDimObjectCommand{static_cast<std::uint8_t>(DimType::Radius),
                                          {60, 0}, {60, 0}, 2.0});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.has_pending_dim; }));
    const RenderSnapshot& s = engine.snapshot();
    // Resolving created NOTHING and did not re-tessellate: it is preview-only.
    REQUIRE(s.line_vertices.size() == base);
    REQUIRE(s.geometry_version == gv);
    // The pending def points describe the circle (centre + radius), for the preview.
    REQUIRE(s.pending_dim_a.x == Approx(50.0));
    REQUIRE(s.pending_dim_a.y == Approx(0.0));
    REQUIRE(distance(s.pending_dim_a, s.pending_dim_b) == Approx(10.0).margin(1e-6));
    REQUIRE(s.pending_dim_type == static_cast<std::uint8_t>(DimType::Radius));
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

TEST_CASE("Per-dimension overrides resolve override-first in compute_dim_geometry") {
    using musacad::core::DimData;
    using musacad::core::DimOverrides;
    using musacad::core::DimStyle;
    using musacad::core::DimType;
    using musacad::core::Rgb;
    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {10, 0};
    d.line_pt = {5, 3};
    DimStyle s; // text_height 2.5, arrow 2.5, precision 2
    // No override -> follows style.
    REQUIRE(compute_dim_geometry(d, s, Rgb{255, 255, 255}).text_height == Catch::Approx(2.5));

    // Override text height + text colour on THIS dim.
    d.overrides.set(DimOverrides::kTextHeight, true);
    d.overrides.text_height = 7.0;
    d.overrides.set(DimOverrides::kTextColor, true);
    d.overrides.text_color = {255, 0, 0};
    const auto g1 = compute_dim_geometry(d, s, Rgb{255, 255, 255});
    REQUIRE(g1.text_height == Catch::Approx(7.0));
    REQUIRE(g1.text_color == Rgb{255, 0, 0});

    // Change the style: overridden field stays, ByStyle field (precision) follows.
    s.text_height = 4.0;
    s.precision = 4;
    const auto g2 = compute_dim_geometry(d, s, Rgb{255, 255, 255});
    REQUIRE(g2.text_height == Catch::Approx(7.0)); // override wins
    REQUIRE(g2.label == "10.0000");                // precision followed the style

    // Reset the override -> follows the style again.
    d.overrides.set(DimOverrides::kTextHeight, false);
    REQUIRE(compute_dim_geometry(d, s, Rgb{255, 255, 255}).text_height == Catch::Approx(4.0));
}

TEST_CASE("Dimension text placement is consistent for rotated (vertical) dimensions") {
    // Regression: Above/Centered offset along the geometric perp inverted for vertical
    // dims (the rotated glyphs grow toward the line, not away). Now both axes agree:
    // Centered straddles the dim line; Above clears it on the offset side.
    DimStyle above;
    above.text_above = true;
    DimStyle centered;
    centered.text_above = false;

    // Horizontal dim line (along +x at y=0).
    DimData h;
    h.type = DimType::Linear;
    h.a = {0, 0};
    h.b = {100, 0};
    h.line_pt = {50, 0};
    const double hi = compute_dim_geometry(h, above, Rgb{255, 255, 255}).text_pos.y;
    const double hc = compute_dim_geometry(h, centered, Rgb{255, 255, 255}).text_pos.y;
    REQUIRE(hi > 0.0);  // Above: baseline above the line
    REQUIRE(hc < 0.0);  // Centered: baseline dropped half a glyph so the body straddles

    // Vertical dim line (along +y at x=0). The text reads rotated; its "up" points -x,
    // so Above must sit on -x and Centered must straddle x=0 -- the mirror of horizontal.
    DimData v;
    v.type = DimType::Linear;
    v.a = {0, 0};
    v.b = {0, 100};
    v.line_pt = {0, 50};
    const double vi = compute_dim_geometry(v, above, Rgb{255, 255, 255}).text_pos.x;
    const double vc = compute_dim_geometry(v, centered, Rgb{255, 255, 255}).text_pos.x;
    REQUIRE(vi < 0.0);  // Above: baseline on the offset side
    REQUIRE(vc > 0.0);  // Centered: baseline shifted +x so the leftward body straddles
    REQUIRE(((vi < 0.0) != (vc < 0.0))); // the two placements land on opposite sides
}

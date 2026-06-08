// Polyline arc segments (bulges): the proper fix for dimensioning filleted
// rectangle corners. Covers the math, tessellation, the fillet->bulge pipeline,
// the headline (DIMRADIUS on a filleted polyline corner), and persistence.

#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/render_snapshot.hpp"

using namespace musacad::core;
using Catch::Approx;

TEST_CASE("arc_from_bulge recovers a fillet arc's radius") {
    // Quarter-circle from (5,0) to (0,5); bulge = tan(90deg / 4).
    const BulgeArc a = arc_from_bulge({5, 0}, {0, 5}, std::tan(kPi / 8.0));
    REQUIRE(a.radius == Approx(5.0));
    REQUIRE(std::abs(a.sweep) == Approx(kPi / 2.0));
}

TEST_CASE("A bulged polyline segment tessellates to a curved arc (not the chord)") {
    GeometryStore s;
    NativeKernel2D k;
    const std::array<Vec2, 2> v{{{0, 0}, {10, 0}}};
    const std::array<double, 2> b{{1.0, 0.0}}; // semicircle on the first segment
    const EntityHandle h = s.add_polyline(v, b, false);
    std::vector<Vec2> out;
    k.tessellate(s, h, 0.01, out);
    REQUIRE(out.size() > 8); // many samples, not a 2-point chord
    // The semicircle bows to a peak ~radius 5 off the chord midpoint.
    double max_off = 0.0;
    for (const Vec2& p : out) {
        max_off = std::max(max_off, std::abs(p.y));
    }
    REQUIRE(max_off == Approx(5.0).margin(0.1));
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

TEST_CASE("Filleting a rectangle corner makes a real arc, dimensionable by radius") {
    GeometryEngine engine;
    engine.start();
    // A closed rectangle polyline.
    engine.submit(AddPolylineCommand{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    const std::size_t before = engine.snapshot().line_vertices.size();

    // Fillet the (10,10) corner with R = 3 (picks on its two adjacent edges).
    engine.submit(FilletPickCommand{{10, 7}, {7, 10}, 3.0, 1.0, 2});
    // The corner becomes a smooth arc -> the tessellated polyline gains vertices.
    REQUIRE(wait_until(engine, [before](const auto& s) {
        return s.line_vertices.size() > before;
    }));

    // DIMRADIUS on the filleted corner: pick on the arc (~the corner region).
    // The fillet centre is (7,7), so a point on the arc is ~ (7+3/sqrt2, 7+3/sqrt2).
    const Vec2 on_arc{7.0 + 3.0 / std::sqrt(2.0), 7.0 + 3.0 / std::sqrt(2.0)};
    engine.submit(AddObjectDimensionCommand{static_cast<std::uint8_t>(DimType::Radius), on_arc,
                                            {12, 12}, 1.0, 0, 3});
    // A dimension renders only if the arc was found and measured (radius read from
    // the bulge). Its geometry adds a vertex near the fillet centre (7,7).
    const bool dimmed = wait_until(engine, [](const auto& s) {
        for (const Vec2& v : s.line_vertices) {
            if (length_squared(v - Vec2{7, 7}) < 0.25) {
                return true; // the radius dim line runs to the centre
            }
        }
        return false;
    });
    REQUIRE(dimmed);
    engine.stop();
}

TEST_CASE("Polyline bulges round-trip native + DXF; old (straight) files still load") {
    using namespace musacad::core::io;
    Document doc;
    DocPolyline pl;
    pl.points = {{0, 0}, {10, 0}, {10, 10}};
    pl.closed = true;
    pl.bulges = {0.5, 0.0, 0.0}; // an arc on the first segment
    doc.polylines.push_back(pl);

    Document nb;
    REQUIRE(parse_native(serialize_native(doc), nb).ok);
    REQUIRE(nb.polylines.size() == 1);
    REQUIRE(nb.polylines[0].bulges.size() == 3);
    REQUIRE(nb.polylines[0].bulges[0] == Approx(0.5));

    Document db;
    REQUIRE(parse_dxf(serialize_dxf(doc), db).ok);
    REQUIRE(db.polylines.size() == 1);
    REQUIRE(db.polylines[0].bulges.size() == 3);
    REQUIRE(db.polylines[0].bulges[0] == Approx(0.5));

    // A v4 file (no bulge tokens) loads as a straight polyline.
    const std::string v4 =
        "MUSACAD 4\nLAYER 255 255 255 0 25 1 0 0 0\nPOLYLINE 1 3 0 0 10 0 10 10 0 7 0 0 0 0 25\nEND\n";
    Document old;
    REQUIRE(parse_native(v4, old).ok);
    REQUIRE(old.polylines.size() == 1);
    REQUIRE(old.polylines[0].bulges.empty());
}

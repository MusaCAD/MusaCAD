// Phase 23: linetype dash-pattern rendering + global LTSCALE.
//  - dash_polyline walks any polyline by arc-length (lines AND tessellated curves
//    share one path; phase carries across vertices);
//  - dashing is DERIVED at snapshot from the stored linetype + LTSCALE (not baked);
//  - LTSCALE scales every pattern and round-trips in native + DXF;
//  - older files load with LTSCALE 1.0.

#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/linetype.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/scene_snapshot.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {
// Total drawn length of a list of endpoint pairs (a0,b0,a1,b1,...).
double drawn_length(const std::vector<Vec2>& segs) {
    double total = 0.0;
    for (std::size_t i = 1; i < segs.size(); i += 2) {
        total += length(segs[i] - segs[i - 1]);
    }
    return total;
}
} // namespace

TEST_CASE("dash_polyline: Continuous copies the segment unchanged") {
    const std::array<Vec2, 2> line{Vec2{0, 0}, Vec2{100, 0}};
    std::vector<Vec2> out;
    dash_polyline(line, Linetype::Continuous, 1.0, out);
    REQUIRE(out.size() == 2);
    REQUIRE(drawn_length(out) == Approx(100.0));
}

TEST_CASE("dash_polyline: Dashed breaks a line into multiple on-segments by arc-length") {
    const std::array<Vec2, 2> line{Vec2{0, 0}, Vec2{100, 0}};
    std::vector<Vec2> out;
    dash_polyline(line, Linetype::Dashed, 1.0, out);
    REQUIRE(out.size() > 2);                 // many dashes, not one solid run
    REQUIRE(drawn_length(out) < 100.0);      // gaps removed length
    REQUIRE(drawn_length(out) > 50.0);       // dash:gap = 5:2.5 -> ~2/3 drawn
    // Every "on" sub-segment stays within the line and is a real dash (length > 0).
    for (std::size_t i = 1; i < out.size(); i += 2) {
        REQUIRE(length(out[i] - out[i - 1]) > 0.0);
        REQUIRE(out[i - 1].x >= -1e-9);
        REQUIRE(out[i].x <= 100.0 + 1e-9);
    }
}

TEST_CASE("dash_polyline: LTSCALE scales the pattern (bigger scale -> fewer dashes)") {
    const std::array<Vec2, 2> line{Vec2{0, 0}, Vec2{100, 0}};
    std::vector<Vec2> small, big;
    dash_polyline(line, Linetype::Dashed, 1.0, small);
    dash_polyline(line, Linetype::Dashed, 4.0, big);
    REQUIRE(big.size() < small.size()); // longer dashes/gaps -> fewer of them
}

TEST_CASE("dash_polyline: phase carries continuously across polyline vertices") {
    // An L-shaped 2-segment polyline of total length 200. Dashing the whole thing
    // must equal dashing a straight 200-length line (arc-length parameterization),
    // i.e. the pattern does NOT restart at the corner.
    const std::array<Vec2, 3> bent{Vec2{0, 0}, Vec2{100, 0}, Vec2{100, 100}};
    const std::array<Vec2, 2> straight{Vec2{0, 0}, Vec2{200, 0}};
    std::vector<Vec2> a, b;
    dash_polyline(bent, Linetype::Dashed, 1.0, a);
    dash_polyline(straight, Linetype::Dashed, 1.0, b);
    REQUIRE(drawn_length(a) == Approx(drawn_length(b)).margin(1e-6));
}

TEST_CASE("Snapshot: a Dashed line emits more segments than a Continuous one (derived)") {
    GeometryStore store;
    NativeKernel2D kernel;
    EntityProps dashed;
    dashed.set_linetype_by_layer(false);
    dashed.linetype = Linetype::Dashed;
    store.add_line({0, 0}, {100, 0}, dashed);

    RenderSnapshot dash_snap;
    build_render_snapshot(store, kernel, dash_snap, kDefaultTessTolerance, 1.0);
    const std::size_t dashed_verts = dash_snap.line_vertices.size();
    REQUIRE(dashed_verts > 2); // dashing happened in the snapshot

    // Same geometry, Continuous -> a single segment. Proves dashing is derived from
    // the stored linetype, not baked into geometry.
    GeometryStore plain;
    plain.add_line({0, 0}, {100, 0});
    RenderSnapshot plain_snap;
    build_render_snapshot(plain, kernel, plain_snap, kDefaultTessTolerance, 1.0);
    REQUIRE(plain_snap.line_vertices.size() == 2);
}

TEST_CASE("Snapshot: dashed circle distributes dashes; stable across zoom (arc-length)") {
    GeometryStore store;
    NativeKernel2D kernel;
    EntityProps dashed;
    dashed.set_linetype_by_layer(false);
    dashed.linetype = Linetype::Dashed;
    store.add_circle({0, 0}, 40.0, dashed);

    // Two tessellation tolerances (coarse vs fine = two zoom buckets). Arc-length
    // dashing => the total drawn length is ~the same regardless of segment density.
    RenderSnapshot coarse, fine;
    build_render_snapshot(store, kernel, coarse, 0.5, 1.0);
    build_render_snapshot(store, kernel, fine, 0.02, 1.0);
    REQUIRE(coarse.line_vertices.size() > 2);
    REQUIRE(fine.line_vertices.size() > 2);
    const double lc = drawn_length(coarse.line_vertices);
    const double lf = drawn_length(fine.line_vertices);
    // Circumference ~251; dash duty ~2/3 -> ~167 drawn. Allow tessellation slack.
    REQUIRE(lc == Approx(lf).epsilon(0.05));
}

TEST_CASE("LTSCALE round-trips in native format; older files default to 1.0") {
    using namespace musacad::core::io;
    Document doc;
    doc.ltscale = 2.5;
    doc.lines.push_back(DocLine{{0, 0}, {10, 0}, {}});
    Document back;
    REQUIRE(parse_native(serialize_native(doc), back).ok);
    REQUIRE(back.ltscale == Approx(2.5));

    // A serialized doc with the LTSCALE line stripped (older file) loads as 1.0.
    std::string text = serialize_native(doc);
    const std::size_t pos = text.find("LTSCALE");
    REQUIRE(pos != std::string::npos);
    const std::size_t eol = text.find('\n', pos);
    text.erase(pos, eol - pos + 1);
    Document older;
    REQUIRE(parse_native(text, older).ok);
    REQUIRE(older.ltscale == Approx(1.0));
}

TEST_CASE("LTSCALE + per-entity linetype round-trip through DXF") {
    using namespace musacad::core::io;
    Document doc;
    doc.ltscale = 3.0;
    EntityProps p;
    p.set_linetype_by_layer(false);
    p.linetype = Linetype::Center;
    doc.lines.push_back(DocLine{{0, 0}, {50, 0}, p});
    Document back;
    REQUIRE(parse_dxf(serialize_dxf(doc), back).ok);
    REQUIRE(back.ltscale == Approx(3.0));
    REQUIRE(back.lines.size() == 1);
    REQUIRE(back.lines[0].props.linetype == Linetype::Center);
    REQUIRE_FALSE(back.lines[0].props.linetype_by_layer());
}

TEST_CASE("CELTSCALE: per-entity scale feeds the dash renderer (effective = LTSCALE x CELTSCALE)") {
    GeometryStore store;
    NativeKernel2D kernel;
    EntityProps center;
    center.set_linetype_by_layer(false);
    center.linetype = Linetype::Center;
    const EntityHandle h = store.add_line({0, 0}, {100, 0}, center);

    const auto segs = [&](double lts) {
        RenderSnapshot s;
        build_render_snapshot(store, kernel, s, kDefaultTessTolerance, lts);
        return s.line_vertices.size();
    };
    const std::size_t base = segs(1.0); // CELTSCALE 1.0
    store.set_celtscale(h, 0.25);
    REQUIRE(segs(1.0) > base); // a smaller per-entity scale -> finer dashes -> more segments

    // ONE multiplication: CELTSCALE 0.25 at LTSCALE 1.0 == CELTSCALE 1.0 at LTSCALE 0.25.
    const std::size_t via_celt = segs(1.0); // celtscale still 0.25
    store.set_celtscale(h, 1.0);
    const std::size_t via_lts = segs(0.25);
    REQUIRE(via_celt == via_lts);
}

TEST_CASE("CELTSCALE is sparse storage: hot structs are not fattened") {
    // A double celtscale field on EntityProps/LineData would add 8 bytes; the sparse
    // side-map keeps them unchanged. (< 16 / < 48 prove no double was inlined.)
    REQUIRE(sizeof(EntityProps) < 16);
    REQUIRE(sizeof(LineData) < 48);
    REQUIRE(sizeof(CircleData) < 48);
}

TEST_CASE("CELTSCALE per-entity round-trips native + DXF; older files default to 1.0") {
    using namespace musacad::core::io;
    Document doc;
    doc.lines.push_back(DocLine{{0, 0}, {50, 0}, {}, 0.25});
    doc.circles.push_back(DocCircle{{0, 0}, 10.0, {}, 2.0});
    DocPolyline pl;
    pl.points = {{0, 0}, {10, 0}, {10, 10}};
    pl.celtscale = 0.5;
    doc.polylines.push_back(pl);

    Document nb;
    REQUIRE(parse_native(serialize_native(doc), nb).ok);
    REQUIRE(nb.lines.at(0).celtscale == Approx(0.25));
    REQUIRE(nb.circles.at(0).celtscale == Approx(2.0));
    REQUIRE(nb.polylines.at(0).celtscale == Approx(0.5));

    Document db;
    REQUIRE(parse_dxf(serialize_dxf(doc), db).ok);
    REQUIRE(db.lines.at(0).celtscale == Approx(0.25));
    REQUIRE(db.circles.at(0).celtscale == Approx(2.0));
    REQUIRE(db.polylines.at(0).celtscale == Approx(0.5));

    // A serialized doc with the CELTSCALE records stripped (older file) loads as 1.0.
    std::string text = serialize_native(doc);
    for (std::size_t pos = text.find("CELTSCALE"); pos != std::string::npos;
         pos = text.find("CELTSCALE")) {
        const std::size_t eol = text.find('\n', pos);
        text.erase(pos, eol - pos + 1);
    }
    Document older;
    REQUIRE(parse_native(text, older).ok);
    REQUIRE(older.lines.at(0).celtscale == Approx(1.0));
    REQUIRE(older.circles.at(0).celtscale == Approx(1.0));
    REQUIRE(older.polylines.at(0).celtscale == Approx(1.0));
}

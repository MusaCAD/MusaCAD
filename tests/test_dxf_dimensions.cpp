// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Reading what AutoCAD writes (#93): each dimension type's own groups, the text override,
// the per-dimension style overrides, DIMSCALE and moved text; entities with a reversed
// extrusion; bent leaders; SOLID; dimension blocks and MINSERT. The values in the
// dimension cases come from a production drawing converted from DWG.

#include <cmath>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/scene_snapshot.hpp"

using namespace musacad::core;
using namespace musacad::core::io;
using Catch::Approx;

namespace {
/// A DXF of `entities` in ENTITIES, each record "code\nvalue\n..." as written here.
std::string dxf_of(const std::string& entities, const std::string& blocks = {}, const std::string& tables = {}) {
    std::string s;
    if (!tables.empty()) {
        s += "0\nSECTION\n2\nTABLES\n" + tables + "0\nENDSEC\n";
    }
    if (!blocks.empty()) {
        s += "0\nSECTION\n2\nBLOCKS\n" + blocks + "0\nENDSEC\n";
    }
    s += "0\nSECTION\n2\nENTITIES\n" + entities + "0\nENDSEC\n0\nEOF\n";
    return s;
}
Document parsed(const std::string& text) {
    Document doc;
    const IoResult r = parse_dxf(text, doc);
    REQUIRE(r.ok);
    return doc;
}
std::string p(int base, Vec2 v) {
    return std::to_string(base) + "\n" + std::to_string(v.x) + "\n" + std::to_string(base + 10) + "\n" +
           std::to_string(v.y) + "\n";
}
std::string dim(int type, const std::string& points, const std::string& extra = {}) {
    return "0\nDIMENSION\n8\n0\n2\n*D1\n3\nStandard\n70\n" + std::to_string(type) + "\n" + points + extra;
}
DimData data_of(const DocDim& d) {
    DimData x;
    x.type = static_cast<DimType>(d.type);
    x.a = d.a;
    x.b = d.b;
    x.line_pt = d.line_pt;
    x.aux = d.aux;
    return x;
}
void check_near(Vec2 a, Vec2 b) {
    CHECK(a.x == Approx(b.x).margin(1e-6));
    CHECK(a.y == Approx(b.y).margin(1e-6));
}
bool reaches_origin(const Document& doc) {
    GeometryStore store;
    populate_store(store, doc);
    NativeKernel2D kernel;
    RenderSnapshot snap;
    build_render_snapshot(store, kernel, snap);
    for (const Vec2& v : snap.line_vertices) {
        if (std::hypot(v.x, v.y) < 1e-6) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("#93 a radius dimension: 10 is the centre and 15 the arc, with no line to the origin") {
    const Vec2 c{-16218.74215651911, 662.6517454993107};
    const Vec2 e{-16217.67884804489, 663.7097507397073};
    const Vec2 t{-16213.86014989224, 667.5094031970455};
    const Document doc = parsed(dxf_of(dim(164, p(10, c) + p(11, t) + p(15, e))));
    REQUIRE(doc.dims.size() == 1);
    const DocDim& d = doc.dims[0];
    CHECK(static_cast<DimType>(d.type) == DimType::Radius);
    check_near(d.a, c);
    check_near(d.b, e);
    check_near(d.line_pt, t);
    CHECK(dim_measure(data_of(d)) == Approx(1.5).margin(1e-3));
    CHECK_FALSE(reaches_origin(doc));
}

TEST_CASE("#93 a diameter dimension: 10 and 15 are opposite points on the circle") {
    const Vec2 far_side{-17182.40285845269, 781.0427831513123};
    const Vec2 near_side{-17248.19650495803, 822.9378434750968};
    const Vec2 t{-17260.43350720354, 836.1156022778503};
    const Document doc = parsed(dxf_of(dim(35, p(10, far_side) + p(11, t) + p(15, near_side))));
    REQUIRE(doc.dims.size() == 1);
    const DocDim& d = doc.dims[0];
    CHECK(static_cast<DimType>(d.type) == DimType::Diameter);
    check_near(d.a, (far_side + near_side) * 0.5);
    check_near(d.b, near_side);
    check_near(d.line_pt, t);
    CHECK(dim_measure(data_of(d)) == Approx(78.0).margin(1e-3));
    CHECK_FALSE(reaches_origin(doc));
}

TEST_CASE("#93 a two-line angular dimension: the vertex is where the lines cross") {
    const Vec2 p13{-10604.65716729836, 775.1490288437906};
    const Vec2 p14{-10571.25333029524, 794.434743129508};
    const Vec2 p15{-10571.25333029524, 833.0061717009355};
    const Vec2 p10{-10571.25333029524, 759.6762735458485};
    const Vec2 p16{-10597.02700119868, 825.1506080230215};
    const Document doc = parsed(dxf_of(dim(34, p(10, p10) + p(11, Vec2{-10608.49, 815.93}) + p(13, p13) + p(14, p14) +
                                                   p(15, p15) + p(16, p16))));
    REQUIRE(doc.dims.size() == 1);
    const DocDim& d = doc.dims[0];
    CHECK(static_cast<DimType>(d.type) == DimType::Angular);
    check_near(d.a, p14); // the two lines meet at the end of the first
    CHECK(d.aux == Approx(length(p16 - p14)));
    // Group 42 of this dimension: 2.0944 radians, 120 degrees.
    CHECK(dim_measure(data_of(d)) == Approx(120.0).margin(1e-3));
}

TEST_CASE("#93 a two-line angular dimension picks the sides the arc lies between") {
    // Two lines crossing at the origin of their own, offset so the vertex is not (0,0).
    const Vec2 o{50.0, 20.0};
    const Vec2 a1 = o + Vec2{-10.0, 0.0};
    const Vec2 a2 = o + Vec2{10.0, 0.0};
    const Vec2 b1 = o + Vec2{0.0, -10.0};
    const Vec2 b2 = o + Vec2{0.0, 10.0};
    for (const Vec2 arc : {Vec2{5.0, 5.0}, Vec2{-5.0, 5.0}, Vec2{-5.0, -5.0}, Vec2{5.0, -5.0}}) {
        const Document doc = parsed(dxf_of(dim(2, p(10, b2) + p(13, a1) + p(14, a2) + p(15, b1) + p(16, o + arc))));
        REQUIRE(doc.dims.size() == 1);
        const DocDim& d = doc.dims[0];
        check_near(d.a, o);
        CHECK(dim_measure(data_of(d)) == Approx(90.0).margin(1e-6));
        // The arc's point lies inside the sweep from b to line_pt.
        CHECK(ccw_sweep(d.b - d.a, o + arc - d.a) <= ccw_sweep(d.b - d.a, d.line_pt - d.a));
    }
    // Parallel lines measure no angle: read as an aligned dimension, never a crash.
    const Document par = parsed(dxf_of(dim(2, p(10, Vec2{10, 5}) + p(13, Vec2{0, 0}) + p(14, Vec2{10, 0}) +
                                                  p(15, Vec2{0, 5}) + p(16, Vec2{5, 2}))));
    REQUIRE(par.dims.size() == 1);
    CHECK(static_cast<DimType>(par.dims[0].type) == DimType::Aligned);
}

TEST_CASE("#93 a three-point angular dimension: 15 is the vertex, 13 and 14 the rays, 10 on the arc") {
    const Vec2 v{100.0, 100.0};
    const Vec2 r1 = v + Vec2{10.0, 0.0};
    const Vec2 r2 = v + Vec2{0.0, 10.0};
    // The arc through the quarter between them: 90 degrees.
    Document doc = parsed(dxf_of(dim(5, p(10, v + Vec2{7.0, 7.0}) + p(13, r1) + p(14, r2) + p(15, v))));
    REQUIRE(doc.dims.size() == 1);
    check_near(doc.dims[0].a, v);
    CHECK(dim_measure(data_of(doc.dims[0])) == Approx(90.0).margin(1e-6));
    // The arc round the other way: the reflex 270.
    doc = parsed(dxf_of(dim(5, p(10, v + Vec2{-7.0, -7.0}) + p(13, r1) + p(14, r2) + p(15, v))));
    REQUIRE(doc.dims.size() == 1);
    CHECK(dim_measure(data_of(doc.dims[0])) == Approx(270.0).margin(1e-6));
}

TEST_CASE("#93 a DXF exported by this version reads back the same, every dimension type") {
    Document doc;
    DocDim r;
    r.type = static_cast<std::uint8_t>(DimType::Radius);
    r.a = {30, 40};
    r.b = {35, 40};
    r.line_pt = {50, 60};
    doc.dims.push_back(r);
    DocDim dia = r;
    dia.type = static_cast<std::uint8_t>(DimType::Diameter);
    doc.dims.push_back(dia);
    DocDim ang;
    ang.type = static_cast<std::uint8_t>(DimType::Angular);
    ang.a = {10, 10};
    ang.b = {20, 10};
    ang.line_pt = {10, 25};
    ang.aux = 12.0;
    doc.dims.push_back(ang);
    DocDim big = ang; // past half a turn
    big.b = {10, 25};
    big.line_pt = {20, 10};
    doc.dims.push_back(big);

    const std::string text = serialize_dxf(doc);
    Document back;
    REQUIRE(parse_dxf(text, back).ok);
    REQUIRE(back.dims.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(back.dims[i].type == doc.dims[i].type);
        check_near(back.dims[i].a, doc.dims[i].a);
        check_near(back.dims[i].b, doc.dims[i].b);
        check_near(back.dims[i].line_pt, doc.dims[i].line_pt);
    }
    CHECK(back.dims[2].aux == Approx(12.0));
    CHECK(dim_measure(data_of(back.dims[3])) == Approx(270.0).margin(1e-6));
    CHECK_FALSE(reaches_origin(back));
}

TEST_CASE("#93 a radial dimension is written the way AutoCAD reads it") {
    Document doc;
    DocDim r;
    r.type = static_cast<std::uint8_t>(DimType::Diameter);
    r.a = {30, 40};
    r.b = {35, 40};
    r.line_pt = {50, 60};
    doc.dims.push_back(r);
    const std::string text = serialize_dxf(doc);
    const std::size_t at = text.find("\nDIMENSION\n");
    REQUIRE(at != std::string::npos);
    const std::string rec = text.substr(at, text.find("ENDSEC", at) - at);
    CHECK(rec.find("\n13\n") == std::string::npos); // radial dimensions have no 13 or 14
    CHECK(rec.find("\n14\n") == std::string::npos);
    // 10 is the far side of the circle: the centre less the radius.
    CHECK(rec.find("\n10\n25") != std::string::npos);
    CHECK(rec.find("\n15\n35") != std::string::npos);
}

TEST_CASE("#93 files written by earlier versions keep their meaning") {
    // Their radial dimensions carried the centre in 13, the circle in 14, the text in 15.
    Document doc = parsed(dxf_of(dim(4, p(10, Vec2{50, 60}) + p(13, Vec2{30, 40}) + p(14, Vec2{35, 40}) +
                                            p(15, Vec2{50, 60}))));
    REQUIRE(doc.dims.size() == 1);
    check_near(doc.dims[0].a, Vec2{30, 40});
    check_near(doc.dims[0].b, Vec2{35, 40});
    check_near(doc.dims[0].line_pt, Vec2{50, 60});
    // Their angular ones the vertex in 13, ray 1 in 14, and ray 2 in both 10 and 15.
    doc = parsed(dxf_of(dim(5, p(10, Vec2{10, 25}) + p(13, Vec2{10, 10}) + p(14, Vec2{20, 10}) + p(15, Vec2{10, 25}))));
    REQUIRE(doc.dims.size() == 1);
    check_near(doc.dims[0].a, Vec2{10, 10});
    check_near(doc.dims[0].b, Vec2{20, 10});
    check_near(doc.dims[0].line_pt, Vec2{10, 25});
}

TEST_CASE("#93 the text override is read, and written back as typed") {
    const std::string pts = p(10, Vec2{0, 10}) + p(13, Vec2{1, 1}) + p(14, Vec2{11, 1});
    const auto text_of = [&](const std::string& value) {
        const Document doc = parsed(dxf_of(dim(0, pts, "1\n" + value + "\n")));
        REQUIRE(doc.dims.size() == 1);
        return doc.dims[0].text_override;
    };
    CHECK(text_of("<>").empty()); // the measurement
    CHECK(text_of("<> (TYP)") == "<> (TYP)");
    CHECK(text_of("%%C<>") == "%%C<>");
    CHECK(text_of("(%%C78)**") == "(%%C78)**");
    CHECK(text_of("AS PER PID") == "AS PER PID");
    CHECK(text_of("<>\\P(TYP)") == "<>\n(TYP)");

    Document doc = parsed(dxf_of(dim(0, pts, "1\n<>\\P(TYP)\n")));
    Document back;
    REQUIRE(parse_dxf(serialize_dxf(doc), back).ok);
    REQUIRE(back.dims.size() == 1);
    CHECK(back.dims[0].text_override == "<>\n(TYP)");

    // A second line goes under the first.
    DimData d;
    d.type = DimType::Aligned;
    d.a = {0, 0};
    d.b = {10, 0};
    const DimLabel lbl = compose_dim_label(d, DimStyle{}, {{}, {}, "<>\n(TYP)"});
    CHECK(lbl.line1.find("10") == 0);
    CHECK(lbl.line2 == "(TYP)");
}

TEST_CASE("#93 a dimension's own overrides of its style are read from its xdata") {
    const std::string pts = p(10, Vec2{0, 10}) + p(13, Vec2{1, 1}) + p(14, Vec2{11, 1});
    const std::string xdata = "1001\nACAD\n1000\nDSTYLE\n1002\n{\n1070\n140\n1040\n7.0\n1070\n41\n1040\n8.0\n"
                              "1070\n271\n1070\n1\n1070\n176\n1070\n1\n1002\n}\n";
    const Document doc = parsed(dxf_of(dim(0, pts, xdata)));
    REQUIRE(doc.dims.size() == 1);
    const DimOverrides& o = doc.dims[0].overrides;
    CHECK(o.has(DimOverrides::kTextHeight));
    CHECK(o.text_height == Approx(7.0));
    CHECK(o.has(DimOverrides::kArrowSize));
    CHECK(o.arrow_size == Approx(8.0));
    CHECK(o.has(DimOverrides::kPrecision));
    CHECK(o.precision == 1);
    CHECK(o.has(DimOverrides::kDimColor));
    // No xdata: the style decides.
    CHECK(parsed(dxf_of(dim(0, pts))).dims[0].overrides.mask == 0);
    // Tolerances set on the dimension itself.
    const std::string tol = "1001\nACAD\n1000\nDSTYLE\n1002\n{\n1070\n71\n1070\n1\n1070\n47\n1040\n0.2\n"
                            "1070\n48\n1040\n0.1\n1002\n}\n";
    const DocDim t = parsed(dxf_of(dim(0, pts, tol))).dims[0];
    CHECK(t.tol.mode == TolMode::Deviation);
    CHECK(t.tol.upper == Approx(0.2));
    CHECK(t.tol.lower == Approx(-0.1));
}

TEST_CASE("#93 DIMSCALE multiplies a style's sizes") {
    const std::string tables = "0\nTABLE\n2\nDIMSTYLE\n70\n1\n0\nDIMSTYLE\n2\nBIG\n40\n10.0\n140\n2.5\n41\n3.0\n42\n1.0\n44\n1.5\n"
                               "0\nENDTAB\n";
    const std::string xdata = "1001\nACAD\n1000\nDSTYLE\n1002\n{\n1070\n140\n1040\n4.0\n1002\n}\n";
    const std::string e = "0\nDIMENSION\n8\n0\n2\n*D1\n3\nBIG\n70\n0\n" + p(10, Vec2{0, 10}) + p(13, Vec2{1, 1}) +
                          p(14, Vec2{11, 1}) + xdata;
    const Document doc = parsed(dxf_of(e, {}, tables));
    REQUIRE(doc.dims.size() == 1);
    const DimStyle& s = doc.dimstyles[doc.dims[0].style];
    CHECK(s.name == "BIG");
    CHECK(s.text_height == Approx(25.0));
    CHECK(s.arrow_size == Approx(30.0));
    CHECK(s.ext_offset == Approx(10.0));
    CHECK(s.ext_extension == Approx(15.0));
    CHECK(doc.dims[0].overrides.text_height == Approx(40.0)); // an override scales too
}

TEST_CASE("#93 text the author moved stays where it was put") {
    const Vec2 moved{30.0, 25.0};
    const Document doc = parsed(dxf_of(dim(128, p(10, Vec2{0, 10}) + p(11, moved) + p(13, Vec2{0, 0}) + p(14, Vec2{20, 0}))));
    REQUIRE(doc.dims.size() == 1);
    const DocDim& d = doc.dims[0];
    CHECK((d.text_offset.x != 0.0 || d.text_offset.y != 0.0));
    DimData x = data_of(d);
    x.text_offset = d.text_offset;
    const DimGeometry g = compute_dim_geometry(x, doc.dimstyles[d.style], Rgb{}, {});
    Vec2 q[4];
    REQUIRE(dim_label_quad(g, false, q));
    check_near((q[0] + q[2]) * 0.5, moved);
    // Without the flag, 11 is only where the layout already puts it.
    const Document plain = parsed(dxf_of(dim(0, p(10, Vec2{0, 10}) + p(11, moved) + p(13, Vec2{0, 0}) + p(14, Vec2{20, 0}))));
    CHECK(plain.dims[0].text_offset.x == 0.0);
    CHECK(plain.dims[0].text_offset.y == 0.0);
}

TEST_CASE("#93 a reversed extrusion turns arcs, circles, polylines and references over") {
    const std::string z = "210\n0.0\n220\n0.0\n230\n-1.0\n";
    const std::string e =
        "0\nCIRCLE\n8\n0\n10\n10.0\n20\n5.0\n40\n2.0\n" + z +                              //
        "0\nARC\n8\n0\n10\n10.0\n20\n5.0\n40\n2.0\n50\n0.0\n51\n90.0\n" + z +              //
        "0\nLWPOLYLINE\n8\n0\n90\n2\n70\n0\n10\n1.0\n20\n2.0\n42\n0.5\n10\n3.0\n20\n2.0\n" + z + //
        "0\nELLIPSE\n8\n0\n10\n10.0\n20\n5.0\n11\n4.0\n21\n0.0\n40\n0.5\n41\n0.0\n42\n1.0\n" + z + //
        "0\nINSERT\n8\n0\n2\nB\n10\n10.0\n20\n5.0\n41\n2.0\n42\n3.0\n50\n30.0\n" + z;
    const Document doc = parsed(dxf_of(e, "0\nBLOCK\n2\nB\n10\n0\n20\n0\n0\nLINE\n8\n0\n10\n0\n20\n0\n11\n1\n21\n0\n0\nENDBLK\n"));
    REQUIRE(doc.circles.size() == 1);
    CHECK(doc.circles[0].center.x == Approx(-10.0));
    CHECK(doc.circles[0].center.y == Approx(5.0));
    REQUIRE(doc.arcs.size() == 1);
    CHECK(doc.arcs[0].center.x == Approx(-10.0));
    // The quarter from east to north, turned over, is the quarter from north to west.
    CHECK(doc.arcs[0].start_angle == Approx(to_radians(90.0)));
    CHECK(doc.arcs[0].end_angle == Approx(to_radians(180.0)));
    REQUIRE(doc.polylines.size() == 1);
    CHECK(doc.polylines[0].points[0].x == Approx(-1.0));
    CHECK(doc.polylines[0].bulges[0] == Approx(-0.5));
    REQUIRE(doc.ellipses.size() == 1);
    CHECK(doc.ellipses[0].center.x == Approx(10.0)); // an ellipse's points are in world coordinates
    CHECK(doc.ellipses[0].start == Approx(kTwoPi - 1.0));
    CHECK(doc.ellipses[0].end == Approx(kTwoPi));
    REQUIRE(doc.inserts.size() == 1);
    CHECK(doc.inserts[0].pos.x == Approx(-10.0));
    CHECK(doc.inserts[0].scale_x == Approx(-2.0));
    CHECK(doc.inserts[0].scale_y == Approx(3.0));
    CHECK(doc.inserts[0].rotation == Approx(to_radians(-30.0)));
}

TEST_CASE("#93 a bent LEADER keeps every vertex, and one vertex draws nothing") {
    const std::string two = "0\nLEADER\n8\n0\n76\n2\n10\n5.0\n20\n6.0\n30\n0\n10\n8.0\n20\n9.0\n30\n0\n";
    const std::string three = "0\nLEADER\n8\n0\n3\nStandard\n76\n3\n10\n1.0\n20\n2.0\n30\n0\n10\n3.0\n20\n4.0\n30\n0\n"
                              "10\n5.0\n20\n4.0\n30\n0\n";
    const std::string one = "0\nLEADER\n8\n0\n76\n1\n10\n7.0\n20\n8.0\n30\n0\n";
    const Document doc = parsed(dxf_of(two + three + one));
    REQUIRE(doc.leaders.size() == 1);
    check_near(doc.leaders[0].tip, Vec2{5, 6});
    check_near(doc.leaders[0].knee, Vec2{8, 9});
    REQUIRE(doc.mleaders.size() == 1);
    REQUIRE(doc.mleaders[0].vertices.size() == 3);
    check_near(doc.mleaders[0].vertices[1], Vec2{3, 4});
    CHECK_FALSE(reaches_origin(doc));
}

TEST_CASE("#93 SOLID draws filled, and as an outline inside a block") {
    const std::string solid = "0\nSOLID\n8\n0\n10\n0.0\n20\n0.0\n11\n10.0\n21\n0.0\n12\n0.0\n22\n5.0\n13\n10.0\n23\n5.0\n";
    const std::string tri = "0\nSOLID\n8\n0\n10\n1.0\n20\n1.0\n11\n4.0\n21\n1.0\n12\n2.0\n22\n3.0\n13\n2.0\n23\n3.0\n";
    const std::string blk = "0\nBLOCK\n2\nB\n10\n0\n20\n0\n" + tri + "0\nENDBLK\n";
    const Document doc = parsed(dxf_of(solid + tri + "0\nINSERT\n8\n0\n2\nB\n10\n0\n20\n0\n", blk));
    REQUIRE(doc.hatches.size() == 2);
    CHECK(doc.hatches[0].pattern_name == "SOLID");
    REQUIRE(doc.hatches[0].loops[0].size() == 4);
    check_near(doc.hatches[0].loops[0][2], Vec2{10, 5}); // corners 1 2 4 3 around the outline
    check_near(doc.hatches[0].loops[0][3], Vec2{0, 5});
    CHECK(doc.hatches[1].loops[0].size() == 3); // a triangle repeats its last corner
    REQUIRE(doc.block_defs.size() == 1);
    REQUIRE(doc.block_defs[0].polylines.size() == 1);
    CHECK(doc.block_defs[0].polylines[0].closed);
}

TEST_CASE("#93 dimension blocks stay out, unless a dimension inside a block draws with one") {
    const std::string line = "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n1\n21\n0\n";
    const std::string blocks = "0\nBLOCK\n2\n*D5\n10\n0\n20\n0\n" + line + "0\nENDBLK\n" +                 //
                               "0\nBLOCK\n2\n*D6\n10\n0\n20\n0\n" + line + "0\nENDBLK\n" +                 //
                               "0\nBLOCK\n2\nDETAIL\n10\n0\n20\n0\n0\nDIMENSION\n8\n0\n2\n*D6\n70\n0\n" + //
                               p(10, Vec2{0, 10}) + p(13, Vec2{1, 1}) + p(14, Vec2{11, 1}) + "0\nENDBLK\n" + //
                               "0\nBLOCK\n2\nGHOST\n10\n0\n20\n0\n0\nDIMENSION\n8\n0\n2\n*D9\n70\n0\n0\nENDBLK\n";
    Document doc;
    const IoResult r = parse_dxf(dxf_of(dim(0, p(10, Vec2{0, 10}) + p(13, Vec2{1, 1}) + p(14, Vec2{11, 1})), blocks), doc);
    REQUIRE(r.ok);
    std::vector<std::string> names;
    for (const DocBlockDef& b : doc.block_defs) {
        names.push_back(b.name);
    }
    CHECK(names == std::vector<std::string>{"*D6", "DETAIL", "GHOST"}); // *D5 draws a model dimension: not needed
    REQUIRE(doc.block_defs[1].inserts.size() == 1);
    CHECK(doc.block_defs[1].inserts[0].block_name == "*D6");
    CHECK(doc.block_defs[2].inserts.empty()); // *D9 is not in the file
    CHECK(r.message.find("DIMENSION") == std::string::npos);
}

TEST_CASE("#93 MULTILEADER: its line, landing, dogleg and text") {
    const std::string one =
        "0\nMULTILEADER\n8\nNotes\n100\nAcDbMLeader\n270\n2\n300\nCONTEXT_DATA{\n40\n1.0\n10\n319.0\n20\n553.0\n"
        "41\n3.0\n140\n9.0\n304\nEPDM ROOF:\\PMEMBRANE\n12\n323.5\n22\n555.6\n42\n0.0\n43\n110.5\n171\n1\n"
        "302\nLEADER{\n290\n1\n10\n453.0\n20\n553.0\n30\n0\n11\n-1.0\n21\n0.0\n31\n0\n40\n24.0\n"
        "304\nLEADER_LINE{\n10\n453.0\n20\n493.0\n30\n0\n91\n0\n305\n}\n"
        "304\nLEADER_LINE{\n10\n470.0\n20\n490.0\n30\n0\n91\n1\n305\n}\n303\n}\n272\n9\n301\n}\n"
        "340\n2A\n170\n1\n291\n1\n41\n0.36\n172\n2\n";
    const Document doc = parsed(dxf_of(one));
    REQUIRE(doc.mleaders.size() == 1);
    const DocMLeader& m = doc.mleaders[0];
    REQUIRE(m.vertices.size() == 3);
    check_near(m.vertices[0], Vec2{453, 493}); // the arrowhead
    check_near(m.vertices[1], Vec2{453, 553}); // the landing
    check_near(m.vertices[2], Vec2{429, 553}); // the dogleg's end
    REQUIRE(m.extra_tips.size() == 1);
    check_near(m.extra_tips[0], Vec2{470, 490});
    check_near(m.block.pos, Vec2{323.5, 555.6});
    CHECK(m.block.height == Approx(3.0));
    CHECK(m.content == "EPDM ROOF:\nMEMBRANE");
    CHECK(m.overrides.has(DimOverrides::kArrowSize));
    CHECK(m.overrides.arrow_size == Approx(9.0));
    CHECK_FALSE(reaches_origin(doc));

    // Inside a block it becomes the lines and the text.
    const Document inblock = parsed(dxf_of("0\nINSERT\n8\n0\n2\nB\n10\n0\n20\n0\n",
                                           "0\nBLOCK\n2\nB\n10\n0\n20\n0\n" + one + "0\nENDBLK\n"));
    REQUIRE(inblock.block_defs.size() == 1);
    CHECK(inblock.block_defs[0].polylines.size() == 2);
    CHECK(inblock.block_defs[0].mtexts.size() == 1);
}

TEST_CASE("#93 MINSERT places the whole grid") {
    const std::string blk = "0\nBLOCK\n2\nB\n10\n0\n20\n0\n0\nLINE\n8\n0\n10\n0\n20\n0\n11\n1\n21\n0\n0\nENDBLK\n";
    const std::string e = "0\nINSERT\n8\n0\n2\nB\n10\n100.0\n20\n50.0\n50\n90.0\n70\n3\n71\n2\n44\n10.0\n45\n5.0\n";
    const Document doc = parsed(dxf_of(e, blk));
    REQUIRE(doc.inserts.size() == 6);
    std::vector<Vec2> at;
    for (const DocInsert& i : doc.inserts) {
        at.push_back(i.pos);
    }
    // Turned 90 degrees: columns run up, rows run left.
    const auto has = [&](Vec2 q) {
        for (const Vec2& v : at) {
            if (length(v - q) < 1e-9) {
                return true;
            }
        }
        return false;
    };
    CHECK(has({100, 50}));
    CHECK(has({100, 70}));
    CHECK(has({95, 50}));
    CHECK(has({95, 70}));
    // A plain INSERT stays one.
    CHECK(parsed(dxf_of("0\nINSERT\n8\n0\n2\nB\n10\n1\n20\n2\n", blk)).inserts.size() == 1);
}

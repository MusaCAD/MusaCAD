// DXF interchange: export -> import preserves the supported entities, unsupported
// entities are skipped + summarized, and malformed input fails gracefully.

#include <cmath>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"

using namespace musacad::core;
using namespace musacad::core::io;
using Catch::Approx;

namespace {
Document sample() {
    Document d;
    d.points.push_back({{2.0, 3.0}});
    d.lines.push_back(DocLine{{0, 0}, {10, 5}});
    d.circles.push_back(DocCircle{{4, 4}, 2.5});
    d.arcs.push_back(DocArc{{1, 1}, 3.0, 0.5, 2.5});
    d.polylines.push_back(DocPolyline{{{0, 0}, {1, 0}, {1, 1}}, true});
    return d;
}
} // namespace

TEST_CASE("DXF export -> import preserves supported entity counts and parameters") {
    const Document src = sample();
    Document dst;
    const IoResult r = parse_dxf(serialize_dxf(src), dst);
    REQUIRE(r.ok);

    REQUIRE(dst.points.size() == src.points.size());
    REQUIRE(dst.lines.size() == src.lines.size());
    REQUIRE(dst.circles.size() == src.circles.size());
    REQUIRE(dst.arcs.size() == src.arcs.size());
    REQUIRE(dst.polylines.size() == src.polylines.size());

    REQUIRE(dst.lines[0].b.x == Approx(10.0));
    REQUIRE(dst.circles[0].radius == Approx(2.5));
    REQUIRE(dst.polylines[0].closed);
    REQUIRE(dst.polylines[0].points.size() == 3);
    // Arc angles survive the radians<->degrees round-trip within fp tolerance.
    REQUIRE(dst.arcs[0].start_angle == Approx(0.5).margin(1e-9));
    REQUIRE(dst.arcs[0].end_angle == Approx(2.5).margin(1e-9));
}

TEST_CASE("DXF import is valid R2000 (has the required sections)") {
    const std::string text = serialize_dxf(sample());
    REQUIRE(text.find("AC1015") != std::string::npos);
    REQUIRE(text.find("ENTITIES") != std::string::npos);
    REQUIRE(text.find("LWPOLYLINE") != std::string::npos);
    REQUIRE(text.find("EOF") != std::string::npos);
}

TEST_CASE("DXF import skips unsupported entities and reports a summary") {
    // A minimal DXF with one LINE and two unsupported entities (HATCH, SPLINE).
    const std::string dxf =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n5\n21\n5\n"
        "0\nHATCH\n8\n0\n"
        "0\nSPLINE\n8\n0\n"
        "0\nENDSEC\n0\nEOF\n";
    Document out;
    const IoResult r = parse_dxf(dxf, out);
    REQUIRE(r.ok);
    REQUIRE(out.lines.size() == 1);
    REQUIRE(out.entity_count() == 1);
    REQUIRE(r.message.find("skipped 2") != std::string::npos);
    REQUIRE(r.message.find("HATCH") != std::string::npos);
    REQUIRE(r.message.find("SPLINE") != std::string::npos);
}

TEST_CASE("DXF import treats negative/invalid lineweights as inherit, not fat lines") {
    // DXF code 370 < 0 is ByLayer(-1)/ByBlock(-2)/Default(-3); a naive uint8 cast turns
    // -2 into 254 (a 2.54mm line) and fattens the whole import. Entities with such codes
    // must stay ByLayer; only 0..211 are real widths.
    const std::string dxf =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nLINE\n8\n0\n370\n-2\n10\n0\n20\n0\n11\n5\n21\n5\n" // ByBlock -> inherit
        "0\nLINE\n8\n0\n370\n-1\n10\n0\n20\n0\n11\n5\n21\n5\n" // ByLayer -> inherit
        "0\nLINE\n8\n0\n370\n50\n10\n0\n20\n0\n11\n5\n21\n5\n"  // 0.50mm -> explicit
        "0\nLINE\n8\n0\n370\n330\n10\n0\n20\n0\n11\n5\n21\n5\n" // out of range -> inherit
        "0\nENDSEC\n0\nEOF\n";
    Document out;
    const IoResult r = parse_dxf(dxf, out);
    REQUIRE(r.ok);
    REQUIRE(out.lines.size() == 4);
    REQUIRE(out.lines[0].props.lineweight_by_layer()); // -2 stays ByLayer
    REQUIRE(out.lines[1].props.lineweight_by_layer()); // -1 stays ByLayer
    REQUIRE_FALSE(out.lines[2].props.lineweight_by_layer()); // 50 is an explicit override
    REQUIRE(out.lines[2].props.lineweight == 50);
    REQUIRE(out.lines[3].props.lineweight_by_layer()); // 330 (>211) stays ByLayer
}

TEST_CASE("MTEXT import strips inline formatting to plain, readable text") {
    // Real title-block cells come in as inline-formatted MTEXT; rendering the control
    // runs verbatim is the "\fCambria|b0|i0|c0|p18;REV" garbage seen on import.
    const std::string dxf =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nMTEXT\n8\n0\n10\n0\n20\n0\n40\n2.5\n"
        "1\n\\fCambria|b0|i0|c0|p18;REDUCE BOLT \\C1;SIZE\\PTO M6\n"
        "0\nENDSEC\n0\nEOF\n";
    Document out;
    const IoResult r = parse_dxf(dxf, out);
    REQUIRE(r.ok);
    REQUIRE(out.mtexts.size() == 1);
    REQUIRE(out.mtexts[0].content == "REDUCE BOLT SIZE\nTO M6");
}

TEST_CASE("TEXT import decodes %% overrides") {
    const std::string dxf =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nTEXT\n8\n0\n10\n0\n20\n0\n40\n2.5\n1\n%%c12 %%d slope %%p0.5\n"
        "0\nENDSEC\n0\nEOF\n";
    Document out;
    const IoResult r = parse_dxf(dxf, out);
    REQUIRE(r.ok);
    REQUIRE(out.texts.size() == 1);
    REQUIRE(out.texts[0].content == "Ø12 ° slope ±0.5");
}

TEST_CASE("DXF import resolves ACI colors (code 62) for layers and entities") {
    // Most DWG layers carry colour as an ACI index in code 62, not a true colour (420).
    // Without the palette they all collapse to white; this is the "colours not imported"
    // bug. 1=red, 5=blue; 256=ByLayer must stay inherited.
    const std::string dxf =
        "0\nSECTION\n2\nTABLES\n"
        "0\nTABLE\n2\nLAYER\n"
        "0\nLAYER\n2\nWALLS\n70\n0\n62\n1\n" // ACI 1 = red
        "0\nENDTAB\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n"
        "0\nLINE\n8\nWALLS\n62\n5\n10\n0\n20\n0\n11\n5\n21\n5\n"  // ACI 5 = blue override
        "0\nLINE\n8\nWALLS\n62\n256\n10\n0\n20\n0\n11\n5\n21\n5\n" // ByLayer -> inherit
        "0\nENDSEC\n0\nEOF\n";
    Document out;
    const IoResult r = parse_dxf(dxf, out);
    REQUIRE(r.ok);
    // Layer WALLS resolved to red.
    bool found = false;
    for (const auto& l : out.layers) {
        if (l.name == "WALLS") {
            REQUIRE(l.color == Rgb{255, 0, 0});
            found = true;
        }
    }
    REQUIRE(found);
    REQUIRE(out.lines.size() == 2);
    REQUIRE_FALSE(out.lines[0].props.color_by_layer()); // explicit ACI 5
    REQUIRE(out.lines[0].props.color == Rgb{0, 0, 255});
    REQUIRE(out.lines[1].props.color_by_layer()); // 256 = ByLayer
}

TEST_CASE("MTEXT import decodes caret tabs and reads the line-spacing factor") {
    const std::string dxf =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nMTEXT\n8\n0\n10\n0\n20\n0\n40\n2.5\n44\n1.5\n"
        "1\nLINE ONE\\P^I 2. TWO\n"
        "0\nENDSEC\n0\nEOF\n";
    Document out;
    const IoResult r = parse_dxf(dxf, out);
    REQUIRE(r.ok);
    REQUIRE(out.mtexts.size() == 1);
    REQUIRE(out.mtexts[0].content == "LINE ONE\n  2. TWO"); // ^I -> space, no stray 'I'
    REQUIRE(out.mtexts[0].block.line_spacing == Approx(1.5));
}

TEST_CASE("Malformed DXF fails gracefully, output untouched") {
    Document out;
    out.circles.push_back(DocCircle{{1, 1}, 1.0}); // sentinel preserved on failure

    SECTION("dangling group code (truncated)") {
        const IoResult r = parse_dxf("0\nSECTION\n2\n", out); // last code has no value
        REQUIRE_FALSE(r.ok);
    }
    SECTION("non-numeric group code") {
        const IoResult r = parse_dxf("notacode\nSECTION\n", out);
        REQUIRE_FALSE(r.ok);
    }
    SECTION("empty file") {
        const IoResult r = parse_dxf("", out);
        REQUIRE_FALSE(r.ok);
    }
    REQUIRE(out.circles.size() == 1); // unchanged
}

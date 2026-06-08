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

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Dimension units (#81, #58): DIMLUNIT, DIMFRAC, DIMRND, DIMLFAC, DIMPOST, DIMAUNIT, DIMADEC
// and the alternate units, in the text, at the command line and in both file formats.

#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad::core;

namespace {
std::string linear_label(double length, const DimStyle& s) {
    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {length, 0};
    d.line_pt = {length / 2.0, 5};
    return compute_dim_geometry(d, s, Rgb{}).label;
}

std::string radius_label(double r, const DimStyle& s) {
    DimData d;
    d.type = DimType::Radius;
    d.a = {0, 0};
    d.b = {r, 0};
    d.line_pt = {r * 1.5, 0};
    return compute_dim_geometry(d, s, Rgb{}).label;
}

std::string angle_label(double deg, const DimStyle& s) {
    DimData d;
    d.type = DimType::Angular;
    d.a = {0, 0};
    d.b = {10, 0};
    const double r = to_radians(deg);
    d.line_pt = {10 * std::cos(r), 10 * std::sin(r)};
    d.aux = 5.0;
    return compute_dim_geometry(d, s, Rgb{}).label;
}
} // namespace

TEST_CASE("Dimension text in architectural, fractional, scaled, rounded and with a prefix and suffix") {
    DimStyle s;
    REQUIRE(linear_label(10.0, s) == "10.00");
    s.linear_unit = 4; // architectural, to 1/16
    s.precision = 4;
    REQUIRE(linear_label(18.5, s) == "1'-6 1/2\"");
    s.linear_unit = 5; // fractional, to 1/8
    s.precision = 3;
    REQUIRE(linear_label(12.375, s) == "12 3/8");
    s = DimStyle{};
    s.linear_unit = 3; // engineering
    REQUIRE(linear_label(18.5, s) == "1'-6.50\"");
    s = DimStyle{};
    s.linear_factor = 2.0;
    REQUIRE(linear_label(10.0, s) == "20.00");
    s = DimStyle{};
    s.round_off = 0.5;
    REQUIRE(linear_label(10.3, s) == "10.50");
    s = DimStyle{};
    s.post = "<> mm";
    REQUIRE(linear_label(10.0, s) == "10.00 mm");
    REQUIRE(radius_label(5.0, s) == "R5.00 mm");
    s.post = "RAD <>";
    REQUIRE(radius_label(5.0, s) == "RAD 5.00"); // a prefix takes the place of R
    s.post = "x";
    REQUIRE(linear_label(10.0, s) == "10.00x"); // no <>: a suffix
}

TEST_CASE("Alternate units after the value, and angles in degrees, minutes and seconds, grads and radians") {
    DimStyle s;
    s.alt = true;
    REQUIRE(linear_label(1.0, s) == "1.00 [25.40]");
    s.alt_post = "[] mm";
    s.alt_precision = 0;
    REQUIRE(linear_label(2.0, s) == "2.00 [51 mm]");
    s.alt_round = 5.0;
    REQUIRE(linear_label(2.0, s) == "2.00 [50 mm]");

    DimStyle a;
    REQUIRE(angle_label(90.0, a) == "90.00°");
    a.angular_unit = 1;
    a.angular_precision = 3;
    REQUIRE(angle_label(45.5, a) == "45°30'0\"");
    a.angular_unit = 2;
    a.angular_precision = 1;
    REQUIRE(angle_label(90.0, a) == "100.0g");
    a.angular_unit = 3;
    a.angular_precision = 4;
    REQUIRE(angle_label(90.0, a) == "1.5708r");
    a.angular_unit = 0;
    a.angular_precision = 0;
    REQUIRE(angle_label(90.0, a) == "90°");
    a.alt = true; // alternate units are for lengths
    REQUIRE(angle_label(90.0, a) == "90°");
}

TEST_CASE("The unit variables at the command line, and the units in both file formats") {
    DimStyle s;
    REQUIRE(apply_dim_var(s, "DIMLUNIT", 4));
    REQUIRE(s.linear_unit == 4);
    REQUIRE(apply_dim_var(s, "dimadec", -1));
    REQUIRE(apply_dim_var(s, "DIMALT", 1));
    REQUIRE(s.alt);
    REQUIRE_FALSE(apply_dim_var(s, "DIMALTF", 0.0));
    REQUIRE_FALSE(apply_dim_var(s, "DIMLUNIT", 7));
    REQUIRE(dim_var_value(s, "DIMLUNIT") == Catch::Approx(4.0));
    REQUIRE(dim_var_value(s, "DIMADEC") == Catch::Approx(-1.0));

    io::Document doc;
    DimStyle u;
    u.name = "Imperial units";
    u.linear_unit = 4;
    u.fraction_format = 2;
    u.round_off = 0.25;
    u.linear_factor = 12.0;
    u.angular_unit = 1;
    u.angular_precision = 3;
    u.alt = true;
    u.alt_unit = 2;
    u.alt_precision = 1;
    u.alt_factor = 2.54;
    u.alt_round = 0.5;
    u.post = "<> in";
    u.alt_post = "[] cm";
    doc.dimstyles.push_back(u);
    io::Document native;
    REQUIRE(io::parse_native(io::serialize_native(doc), native).ok);
    REQUIRE(native.dimstyles.size() == 2);
    REQUIRE(native.dimstyles[1] == u);
    REQUIRE(native.dimstyles[0] == DimStyle{"Standard"});
    io::Document back;
    REQUIRE(io::parse_dxf(io::serialize_dxf(doc), back).ok);
    const auto it = std::find_if(back.dimstyles.begin(), back.dimstyles.end(),
                                 [](const DimStyle& d) { return d.name == "Imperial units"; });
    REQUIRE(it != back.dimstyles.end());
    REQUIRE(it->linear_unit == 4);
    REQUIRE(it->fraction_format == 2);
    REQUIRE(it->round_off == Catch::Approx(0.25));
    REQUIRE(it->linear_factor == Catch::Approx(12.0));
    REQUIRE(it->angular_unit == 1);
    REQUIRE(it->angular_precision == 3);
    REQUIRE(it->alt);
    REQUIRE(it->alt_precision == 1);
    REQUIRE(it->alt_factor == Catch::Approx(2.54));
    REQUIRE(it->post == "<> in");
    REQUIRE(it->alt_post == "[] cm");
}

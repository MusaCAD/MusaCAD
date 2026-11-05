// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Complex linetypes' text (#68): read from a .lin file, drawn along the line in the stroke
// font, kept in the drawing and in DXF.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/linetype.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/text/stroke_font.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {

const LinetypeDef& gas_line() {
    static const std::vector<LinetypeDef> lin = parse_lin("*GAS_LINE,Gas line ----GAS----GAS----GAS--\n"
                                                          "A,.5,-.2,[\"GAS\",STANDARD,S=.1,U=0.0,X=-0.1,Y=-.05],-.25\n");
    return lin.front();
}

constexpr auto kLoaded = static_cast<Linetype>(kFirstLoadedLinetype);

} // namespace

TEST_CASE("A .lin file's text elements are read; its shapes are left out, their gaps kept") {
    const LinetypeDef& gas = gas_line();
    REQUIRE(gas.name == "GAS_LINE");
    REQUIRE(gas.pattern == std::vector<double>{0.5, -0.2, -0.25});
    REQUIRE(gas.texts.size() == 1);
    const LinetypeText& t = gas.texts.front();
    REQUIRE(t.after == 2); // where the -.2 gap ends
    REQUIRE(t.text == "GAS");
    REQUIRE(t.style == "STANDARD");
    REQUIRE(t.scale == Approx(0.1));
    REQUIRE(t.upright);
    REQUIRE_FALSE(t.absolute);
    REQUIRE(t.rotation == Approx(0.0));
    REQUIRE(t.x == Approx(-0.1));
    REQUIRE(t.y == Approx(-0.05));

    const std::vector<LinetypeDef> zig =
        parse_lin("*ZIGZAG,Zig zag /\\/\\/\\/\n"
                  "A,.0001,-.2,[ZIG,ltypeshp.shx,x=-.2,s=.2],-.4,[ZIG,ltypeshp.shx,r=180,x=.2,s=.2],-.2\n");
    REQUIRE(zig.size() == 1);
    REQUIRE(zig[0].pattern == std::vector<double>{0.0001, -0.2, -0.4, -0.2});
    REQUIRE(zig[0].texts.empty());

    // Angles in degrees, or r radians, g grads; A absolute; a comma inside the quotes is text.
    const std::vector<LinetypeDef> angles = parse_lin("*A,a\nA,1,[\"X\",S,S=1,R=90],-1,[\"Y,Z\",S,A=100g],-1,[\"W\",S,R=1.5r],-1\n");
    REQUIRE(angles.size() == 1);
    REQUIRE(angles[0].texts.size() == 3);
    REQUIRE(angles[0].texts[0].rotation == Approx(kPi / 2.0));
    REQUIRE_FALSE(angles[0].texts[0].absolute);
    REQUIRE(angles[0].texts[1].text == "Y,Z");
    REQUIRE(angles[0].texts[1].rotation == Approx(kPi / 2.0));
    REQUIRE(angles[0].texts[1].absolute);
    REQUIRE(angles[0].texts[2].rotation == Approx(1.5));
    REQUIRE(angles[0].texts[2].after == 3);
}

TEST_CASE("The text is drawn where its element ends, reading upright on a line running left") {
    const std::vector<LinetypeDef> loaded{gas_line()};
    const double w = text::text_width("GAS", 0.1);
    const auto near = [](const std::vector<Vec2>& a, const std::vector<Vec2>& b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (length(a[i] - b[i]) > 1e-9) {
                return false;
            }
        }
        return true;
    };

    // A line 1 long, one repeat: the dash 0-0.5, the text after the -.2 gap (at 0.7, back 0.1
    // and down 0.05), the next repeat's dash 0.95-1.
    {
        const std::vector<Vec2> pts{{0.0, 0.0}, {1.0, 0.0}};
        std::vector<Vec2> out;
        dash_polyline(pts, kLoaded, loaded, 1.0, out);
        REQUIRE(out.size() > 4);
        REQUIRE(length(out[0] - Vec2{0.0, 0.0}) < 1e-9);
        REQUIRE(length(out[1] - Vec2{0.5, 0.0}) < 1e-9);
        REQUIRE(length(out[out.size() - 2] - Vec2{0.95, 0.0}) < 1e-9);
        const std::vector<Vec2> drawn(out.begin() + 2, out.end() - 2);
        std::vector<Vec2> want;
        text::append_text_segments("GAS", {0.6, -0.05}, 0.1, 0.0, text::Justify::Left, want);
        REQUIRE(near(drawn, want));
        // Inside the gaps around it, clear of the dashes.
        for (const Vec2 p : drawn) {
            REQUIRE(p.x > 0.5);
            REQUIRE(p.x < 0.95);
        }
    }
    // Running left: turned round, in the same place, reading from the left.
    {
        const std::vector<Vec2> pts{{1.0, 0.0}, {0.0, 0.0}};
        std::vector<Vec2> out;
        dash_polyline(pts, kLoaded, loaded, 1.0, out);
        const std::vector<Vec2> drawn(out.begin() + 2, out.end() - 2);
        std::vector<Vec2> want;
        text::append_text_segments("GAS", {0.4 - w, -0.05}, 0.1, 2.0 * kPi, text::Justify::Left, want);
        REQUIRE(near(drawn, want));
    }
    // Without U, it follows the line, upside down.
    {
        std::vector<LinetypeDef> plain = loaded;
        plain[0].texts[0].upright = false;
        const std::vector<Vec2> pts{{1.0, 0.0}, {0.0, 0.0}};
        std::vector<Vec2> out;
        dash_polyline(pts, kLoaded, plain, 1.0, out);
        const std::vector<Vec2> drawn(out.begin() + 2, out.end() - 2);
        std::vector<Vec2> want;
        text::append_text_segments("GAS", {0.4, 0.05}, 0.1, kPi, text::Justify::Left, want);
        REQUIRE(near(drawn, want));
    }
    // Scaled with the pattern.
    {
        const std::vector<Vec2> pts{{0.0, 0.0}, {10.0, 0.0}};
        std::vector<Vec2> out;
        dash_polyline(pts, kLoaded, loaded, 10.0, out);
        const std::vector<Vec2> drawn(out.begin() + 2, out.end() - 2);
        std::vector<Vec2> want;
        text::append_text_segments("GAS", {6.0, -0.5}, 1.0, 0.0, text::Justify::Left, want);
        REQUIRE(near(drawn, want));
    }
}

TEST_CASE("GAS_LINE and HOT_WATER_SUPPLY are in the linetype library, at acadiso.lin's size too") {
    for (const bool metric : {false, true}) {
        const double unit = metric ? 25.4 : 1.0;
        const std::vector<LinetypeDef>& lib = linetype_library(metric);
        const auto gas = std::find_if(lib.begin(), lib.end(), [](const LinetypeDef& d) { return d.name == "GAS_LINE"; });
        const auto hw =
            std::find_if(lib.begin(), lib.end(), [](const LinetypeDef& d) { return d.name == "HOT_WATER_SUPPLY"; });
        REQUIRE(gas != lib.end());
        REQUIRE(hw != lib.end());
        REQUIRE(gas->texts.size() == 1);
        REQUIRE(gas->texts[0].text == "GAS");
        REQUIRE(gas->texts[0].scale == Approx(0.1 * unit));
        REQUIRE(gas->texts[0].x == Approx(-0.1 * unit));
        REQUIRE(gas->pattern.back() == Approx(-0.25 * unit));
        REQUIRE(hw->texts[0].text == "HW");
        REQUIRE(hw->pattern.back() == Approx(-0.2 * unit));
    }
    // As the .lin file has it.
    LinetypeDef lib_gas = linetype_library(false)[38];
    lib_gas.description = gas_line().description;
    REQUIRE(lib_gas == gas_line());
}

TEST_CASE("A linetype's text is kept in the drawing and in DXF") {
    io::Document doc;
    doc.linetypes.push_back(gas_line());
    LinetypeDef odd;
    odd.name = "ODD";
    odd.pattern = {1.0, -1.0};
    LinetypeText t;
    t.after = 0; // where the pattern starts
    t.text = "two words";
    t.style = "Notes";
    t.scale = 0.25;
    t.rotation = 0.5;
    t.absolute = true;
    t.x = 0.1;
    t.y = -0.2;
    odd.texts.push_back(t);
    doc.linetypes.push_back(odd);
    TextStyle notes;
    notes.name = "Notes";
    doc.text_styles.push_back(TextStyle{});
    doc.text_styles.front().name = "Standard";
    doc.text_styles.push_back(notes);

    io::Document nat;
    REQUIRE(io::parse_native(io::serialize_native(doc), nat).ok);
    REQUIRE(nat.linetypes == doc.linetypes);

    const std::string dxf = io::serialize_dxf(doc);
    io::Document back;
    REQUIRE(io::parse_dxf(dxf, back).ok);
    const auto find = [&](const char* name) {
        return std::find_if(back.linetypes.begin(), back.linetypes.end(), [&](const LinetypeDef& d) { return d.name == name; });
    };
    const auto gas = find("GAS_LINE");
    REQUIRE(gas != back.linetypes.end());
    REQUIRE(gas->pattern == gas_line().pattern);
    REQUIRE(gas->texts.size() == 1);
    REQUIRE(gas->texts[0].after == 2);
    REQUIRE(gas->texts[0].text == "GAS");
    REQUIRE(gas->texts[0].style == "Standard"); // STANDARD is the drawing's Standard
    REQUIRE(gas->texts[0].scale == Approx(0.1));
    REQUIRE(gas->texts[0].x == Approx(-0.1));
    REQUIRE(gas->texts[0].y == Approx(-0.05));
    const auto o = find("ODD");
    REQUIRE(o != back.linetypes.end());
    REQUIRE(o->texts.size() == 1);
    // The one at the pattern's start goes with its last element: the same place.
    REQUIRE(o->texts[0].after == 2);
    REQUIRE(o->texts[0].text == "two words");
    REQUIRE(o->texts[0].style == "Notes");
    REQUIRE(o->texts[0].absolute);
    REQUIRE(o->texts[0].rotation == Approx(0.5));
    REQUIRE(o->texts[0].scale == Approx(0.25));

    // The style it names is a STYLE record's handle.
    const std::size_t at = dxf.find("\n9\ntwo words");
    REQUIRE(at != std::string::npos);
    const std::size_t h = dxf.rfind("340\n", at);
    REQUIRE(h != std::string::npos);
    const std::string handle = dxf.substr(h + 4, dxf.find('\n', h + 4) - h - 4);
    REQUIRE(dxf.find("STYLE\n5\n" + handle + "\n") != std::string::npos);
}

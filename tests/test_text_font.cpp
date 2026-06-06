// Part A: the single-stroke vector font covers the required character set and
// lays text out (width, justification, rotation) as world-space segments.

#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/text/stroke_font.hpp"

using namespace musacad::core;
using namespace musacad::core::text;
using Catch::Approx;

TEST_CASE("Every required glyph produces strokes") {
    const std::string charset =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.,-+/<>()";
    for (char c : charset) {
        std::vector<Vec2> segs;
        append_text_segments(std::string(1, c), {0, 0}, 1.0, 0.0, Justify::Left, segs);
        INFO("glyph: " << c);
        REQUIRE(segs.size() >= 2); // at least one segment (space is the only blank)
        REQUIRE(segs.size() % 2 == 0);
    }
}

TEST_CASE("CAD symbols (degree, plus-minus, diameter) render") {
    for (const char* sym : {"°", "±", "⌀"}) {
        std::vector<Vec2> segs;
        append_text_segments(sym, {0, 0}, 1.0, 0.0, Justify::Left, segs);
        REQUIRE(segs.size() >= 2);
    }
}

TEST_CASE("Width is monospace and scales with height; space advances") {
    REQUIRE(text_width("", 10.0) == Approx(0.0));
    const double one = text_width("A", 10.0);
    REQUIRE(one > 0.0);
    REQUIRE(text_width("AB", 10.0) == Approx(2.0 * one));
    REQUIRE(text_width("A", 20.0) == Approx(2.0 * one)); // scales with height
    REQUIRE(text_width(" ", 10.0) > 0.0);                // space advances
}

TEST_CASE("Justification shifts the run; rotation transforms it") {
    std::vector<Vec2> left;
    std::vector<Vec2> center;
    append_text_segments("123", {0, 0}, 1.0, 0.0, Justify::Left, left);
    append_text_segments("123", {0, 0}, 1.0, 0.0, Justify::Center, center);
    REQUIRE(left.size() == center.size());
    const double w = text_width("123", 1.0);
    // Center run is shifted left by half the width.
    REQUIRE(center[0].x == Approx(left[0].x - w / 2.0));

    std::vector<Vec2> rot;
    append_text_segments("1", {0, 0}, 1.0, 1.5707963267948966, Justify::Left, rot); // 90 deg
    std::vector<Vec2> flat;
    append_text_segments("1", {0, 0}, 1.0, 0.0, Justify::Left, flat);
    // 90-degree rotation maps local (x,y) -> (-y, x): x-extent becomes y-extent.
    double flat_max_x = 0.0;
    double rot_max_y = 0.0;
    for (const Vec2& p : flat) {
        flat_max_x = std::max(flat_max_x, p.x);
    }
    for (const Vec2& p : rot) {
        rot_max_y = std::max(rot_max_y, p.y);
    }
    REQUIRE(rot_max_y == Approx(flat_max_x).margin(1e-9));
}

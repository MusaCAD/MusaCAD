// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

// The pure Dynamic-Input field schema: positioned tooltips, the coordinate-string
// composition shared with the DYN box, and the render-side dimension lock. No Qt --
// this is the testable core; the widget shell is a thin presentation layer over it.

#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/dyn_fields.hpp"

using namespace musacad::command;
using musacad::core::Vec2;

namespace {
PreviewSpec rect_at(Vec2 first) {
    PreviewSpec pv;
    pv.kind = PreviewKind::Rectangle;
    pv.points = {first};
    return pv;
}
PreviewSpec seg_at(Vec2 first) {
    PreviewSpec pv;
    pv.kind = PreviewKind::Segment;
    pv.points = {first};
    return pv;
}
PreviewSpec circ_at(Vec2 first) {
    PreviewSpec pv;
    pv.kind = PreviewKind::Circle;
    pv.points = {first};
    return pv;
}
bool approx(double a, double b) {
    return std::abs(a - b) < 1e-9;
}
} // namespace

TEST_CASE("dyn_fields: RECTANGLE exposes Length + Width anchored on the two edges") {
    const auto f = dyn_fields(rect_at({0, 0}), {50, 30});
    REQUIRE(f.size() == 2);

    REQUIRE(f[0].label == "Length");
    REQUIRE(f[0].slot == 0);
    REQUIRE(approx(f[0].value, 50.0));
    // Length floats at the midpoint of the first (bottom) edge.
    REQUIRE(approx(f[0].anchor.x, 25.0));
    REQUIRE(approx(f[0].anchor.y, 0.0));

    REQUIRE(f[1].label == "Width");
    REQUIRE(f[1].slot == 1);
    REQUIRE(approx(f[1].value, 30.0));
    // Width floats at the midpoint of the side (right) edge.
    REQUIRE(approx(f[1].anchor.x, 50.0));
    REQUIRE(approx(f[1].anchor.y, 15.0));
}

TEST_CASE("dyn_fields: values are absolute extents regardless of quadrant") {
    const auto f = dyn_fields(rect_at({0, 0}), {-50, -30}); // dragging SW
    REQUIRE(approx(f[0].value, 50.0));
    REQUIRE(approx(f[1].value, 30.0));
}

TEST_CASE("dyn_fields: LINE exposes Length + Angle; CIRCLE exposes Radius") {
    const auto seg = dyn_fields(seg_at({0, 0}), {10, 10});
    REQUIRE(seg.size() == 2);
    REQUIRE(seg[0].label == "Length");
    REQUIRE(approx(seg[0].value, std::sqrt(200.0)));
    REQUIRE(seg[1].label == "Angle");
    REQUIRE(seg[1].is_angle);
    REQUIRE(approx(seg[1].value, 45.0));

    const auto c = dyn_fields(circ_at({0, 0}), {3, 4});
    REQUIRE(c.size() == 1);
    REQUIRE(c[0].label == "Radius");
    REQUIRE(approx(c[0].value, 5.0));
}

TEST_CASE("dyn_fields: no anchor yet -> no fields") {
    PreviewSpec empty;
    empty.kind = PreviewKind::Rectangle; // but points is empty
    REQUIRE(dyn_fields(empty, {1, 1}).empty());
}

TEST_CASE("compose_dyn_submit: RECTANGLE both typed -> @w,h with quadrant sign") {
    // Cursor in the NE quadrant -> positive signs.
    REQUIRE(compose_dyn_submit(rect_at({0, 0}), {1, 1}, 50.0, 30.0) == "@50.000000,30.000000");
    // Cursor in the SW quadrant -> the typed magnitudes pick up negative signs.
    REQUIRE(compose_dyn_submit(rect_at({0, 0}), {-1, -1}, 50.0, 30.0) == "@-50.000000,-30.000000");
}

TEST_CASE("compose_dyn_submit: typing ONE field locks it, the other follows the cursor") {
    // Only Length typed; width comes from the live cursor (which is at +30 in y).
    const std::string line = compose_dyn_submit(rect_at({0, 0}), {7, 30}, 50.0, std::nullopt);
    REQUIRE(line == "@50.000000,30.000000");
}

TEST_CASE("compose_dyn_submit: LINE -> @len<ang, CIRCLE -> @rad<ang") {
    REQUIRE(compose_dyn_submit(seg_at({0, 0}), {1, 0}, 100.0, 45.0) == "@100.000000<45.000000");
    // Circle: radius typed, angle taken from the cursor direction (0 deg here).
    REQUIRE(compose_dyn_submit(circ_at({0, 0}), {1, 0}, 25.0, std::nullopt) == "@25.000000<0.000000");
}

TEST_CASE("compose_dyn_submit: nothing typed where a value is required -> empty") {
    REQUIRE(compose_dyn_submit(rect_at({0, 0}), {1, 1}, std::nullopt, std::nullopt).empty());
    REQUIRE(compose_dyn_submit(circ_at({0, 0}), {1, 1}, std::nullopt, std::nullopt).empty());
}

TEST_CASE("apply_dyn_lock: RECTANGLE locks the typed dimension, cursor drives the other") {
    // Lock Length=50; the height still follows the cursor (here +30, NE quadrant).
    const Vec2 b = apply_dyn_lock(rect_at({0, 0}), {7, 30}, 50.0, std::nullopt);
    REQUIRE(approx(b.x, 50.0));
    REQUIRE(approx(b.y, 30.0));

    // SW quadrant: locked length picks up the negative sign.
    const Vec2 sw = apply_dyn_lock(rect_at({0, 0}), {-7, -30}, 50.0, std::nullopt);
    REQUIRE(approx(sw.x, -50.0));
    REQUIRE(approx(sw.y, -30.0));
}

TEST_CASE("apply_dyn_lock: LINE locks length along the live angle; CIRCLE locks radius") {
    // Length 10 locked, angle from cursor (45 deg) -> (10/sqrt2, 10/sqrt2).
    const Vec2 e = apply_dyn_lock(seg_at({0, 0}), {1, 1}, 10.0, std::nullopt);
    REQUIRE(approx(e.x, 10.0 / std::sqrt(2.0)));
    REQUIRE(approx(e.y, 10.0 / std::sqrt(2.0)));

    // Radius 5 locked toward a cursor on +x -> (5,0); the overlay reads dist = 5.
    const Vec2 c = apply_dyn_lock(circ_at({0, 0}), {2, 0}, 5.0, std::nullopt);
    REQUIRE(approx(musacad::core::distance(Vec2{0, 0}, c), 5.0));
}

TEST_CASE("apply_dyn_lock: nothing locked -> cursor unchanged") {
    const Vec2 c = apply_dyn_lock(rect_at({0, 0}), {12, 34}, std::nullopt, std::nullopt);
    REQUIRE(approx(c.x, 12.0));
    REQUIRE(approx(c.y, 34.0));
}

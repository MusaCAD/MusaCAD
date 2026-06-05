#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/coordinate.hpp"

using namespace musacad::command;
using musacad::core::Vec2;
using Catch::Approx;

TEST_CASE("Coordinate: absolute") {
    const CoordParse p = parse_coordinate("10,20", std::nullopt);
    REQUIRE(p.ok);
    REQUIRE(p.point.x == Approx(10.0));
    REQUIRE(p.point.y == Approx(20.0));
}

TEST_CASE("Coordinate: relative cartesian") {
    const CoordParse p = parse_coordinate("@100,0", Vec2{5.0, 5.0});
    REQUIRE(p.ok);
    REQUIRE(p.point.x == Approx(105.0));
    REQUIRE(p.point.y == Approx(5.0));
}

TEST_CASE("Coordinate: polar relative") {
    const CoordParse p = parse_coordinate("@10<90", Vec2{0.0, 0.0});
    REQUIRE(p.ok);
    REQUIRE(p.point.x == Approx(0.0).margin(1e-9));
    REQUIRE(p.point.y == Approx(10.0));

    const CoordParse q = parse_coordinate("@10<0", Vec2{1.0, 2.0});
    REQUIRE(q.ok);
    REQUIRE(q.point.x == Approx(11.0));
    REQUIRE(q.point.y == Approx(2.0));
}

TEST_CASE("Coordinate: relative needs a previous point") {
    const CoordParse p = parse_coordinate("@1,1", std::nullopt);
    REQUIRE_FALSE(p.ok);
    REQUIRE_FALSE(p.error.empty());
}

TEST_CASE("Coordinate: malformed is rejected") {
    REQUIRE_FALSE(parse_coordinate("abc", std::nullopt).ok);
    REQUIRE_FALSE(parse_coordinate("10,", std::nullopt).ok);
    REQUIRE_FALSE(parse_coordinate("10 20", std::nullopt).ok);
    REQUIRE_FALSE(parse_coordinate("@5", Vec2{0, 0}).ok);
}

TEST_CASE("Coordinate: bare number") {
    double v = 0.0;
    REQUIRE(parse_number("42.5", v));
    REQUIRE(v == Approx(42.5));
    REQUIRE_FALSE(parse_number("4,2", v));
    REQUIRE_FALSE(parse_number("", v));
}

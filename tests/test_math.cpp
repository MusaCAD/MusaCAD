#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/math/math.hpp"

using namespace musacad::core;
using Catch::Approx;

TEST_CASE("Vec2 arithmetic and products") {
    constexpr Vec2 a{3.0, 4.0};
    constexpr Vec2 b{1.0, 2.0};

    STATIC_REQUIRE(a + b == Vec2{4.0, 6.0});
    STATIC_REQUIRE(a - b == Vec2{2.0, 2.0});
    STATIC_REQUIRE(a * 2.0 == Vec2{6.0, 8.0});
    STATIC_REQUIRE(dot(a, b) == 11.0);
    STATIC_REQUIRE(cross(a, b) == 2.0);
    STATIC_REQUIRE(length_squared(a) == 25.0);

    REQUIRE(length(a) == Approx(5.0));
    REQUIRE(distance(a, b) == Approx(std::sqrt(8.0)));
    REQUIRE(length(normalized(a)) == Approx(1.0));
    REQUIRE(normalized(Vec2{0.0, 0.0}) == Vec2{0.0, 0.0});
    STATIC_REQUIRE(perpendicular(Vec2{1.0, 0.0}) == Vec2{0.0, 1.0});
}

TEST_CASE("Vec3 cross product") {
    constexpr Vec3 x{1.0, 0.0, 0.0};
    constexpr Vec3 y{0.0, 1.0, 0.0};
    STATIC_REQUIRE(cross(x, y) == Vec3{0.0, 0.0, 1.0});
    STATIC_REQUIRE(dot(x, y) == 0.0);
}

TEST_CASE("Mat3 transforms") {
    const Mat3 t = Mat3::translation({10.0, 20.0});
    REQUIRE(t.transform_point({1.0, 1.0}) == Vec2{11.0, 21.0});
    REQUIRE(t.transform_vector({1.0, 1.0}) == Vec2{1.0, 1.0}); // translation ignored

    const Mat3 r = Mat3::rotation(kHalfPi);
    const Vec2 p = r.transform_point({1.0, 0.0});
    REQUIRE(p.x == Approx(0.0).margin(1e-12));
    REQUIRE(p.y == Approx(1.0));

    // Compose: rotate then translate.
    const Mat3 m = t * r;
    const Vec2 q = m.transform_point({1.0, 0.0});
    REQUIRE(q.x == Approx(10.0).margin(1e-12));
    REQUIRE(q.y == Approx(21.0));
}

TEST_CASE("Mat3/Mat4 identity") {
    STATIC_REQUIRE(Mat3::identity().at(0, 0) == 1.0);
    STATIC_REQUIRE(Mat3::identity().at(1, 0) == 0.0);
    const Mat3 m = Mat3::identity() * Mat3::scale({2.0, 3.0});
    REQUIRE(m.transform_point({1.0, 1.0}) == Vec2{2.0, 3.0});

    const Mat4 t = Mat4::translation({1.0, 2.0, 3.0});
    REQUIRE(t.transform_point({0.0, 0.0, 0.0}) == Vec3{1.0, 2.0, 3.0});
    REQUIRE((Mat4::identity() * t).transform_point({1.0, 1.0, 1.0}) == Vec3{2.0, 3.0, 4.0});
}

TEST_CASE("Angle conversion") {
    REQUIRE(to_radians(180.0) == Approx(kPi));
    REQUIRE(to_degrees(kPi) == Approx(180.0));
}

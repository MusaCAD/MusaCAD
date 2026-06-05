#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/render/camera.hpp"

using namespace musacad::render;
using musacad::core::Vec2;
using Catch::Approx;

namespace {
bool approx_eq(Vec2 a, Vec2 b, double eps = 1e-9) {
    return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps;
}
} // namespace

TEST_CASE("Camera: world<->screen round trip and centering") {
    Camera2D cam;
    cam.set_viewport(800, 600);
    cam.set_center({10.0, 20.0});
    cam.set_scale(4.0);

    // Center projects to the middle of the viewport.
    REQUIRE(approx_eq(cam.world_to_screen({10.0, 20.0}), {400.0, 300.0}));

    for (const Vec2 w : {Vec2{0.0, 0.0}, Vec2{15.0, -7.0}, Vec2{-30.0, 50.0}}) {
        const Vec2 s = cam.world_to_screen(w);
        REQUIRE(approx_eq(cam.screen_to_world(s), w));
    }

    // World y-up maps to screen y-down.
    REQUIRE(cam.world_to_screen({10.0, 21.0}).y < 300.0);
}

TEST_CASE("Camera: zoom keeps the anchor world point fixed") {
    Camera2D cam;
    cam.set_viewport(1024, 768);
    cam.set_center({0.0, 0.0});
    cam.set_scale(2.0);

    const Vec2 anchor{700.0, 200.0};
    const Vec2 world_before = cam.screen_to_world(anchor);
    cam.zoom_about(anchor, 3.5);
    REQUIRE(cam.scale() == Approx(7.0));
    REQUIRE(approx_eq(cam.screen_to_world(anchor), world_before, 1e-7));

    cam.zoom_about(anchor, 1.0 / 3.5);
    REQUIRE(cam.scale() == Approx(2.0));
    REQUIRE(approx_eq(cam.screen_to_world(anchor), world_before, 1e-7));
}

TEST_CASE("Camera: pan keeps the grabbed point under the cursor") {
    Camera2D cam;
    cam.set_viewport(800, 600);
    cam.set_scale(5.0);

    const Vec2 grab{300.0, 400.0};
    const Vec2 world = cam.screen_to_world(grab);
    const Vec2 delta{40.0, -25.0};
    cam.pan_pixels(delta);
    REQUIRE(approx_eq(cam.screen_to_world(grab + delta), world, 1e-9));
}

TEST_CASE("Camera: view matrix maps to NDC") {
    Camera2D cam;
    cam.set_viewport(800, 600);
    cam.set_center({5.0, 5.0});
    cam.set_scale(10.0);
    const auto m = cam.view_matrix();

    // Center -> NDC origin.
    REQUIRE(approx_eq(m.transform_point({5.0, 5.0}), {0.0, 0.0}));
    // A point half a viewport-width to the right (in world) -> NDC x = 1.
    const double half_w_world = 400.0 / 10.0; // (width/2) / scale
    REQUIRE(m.transform_point({5.0 + half_w_world, 5.0}).x == Approx(1.0));
}

TEST_CASE("Camera: scale is clamped positive") {
    Camera2D cam;
    cam.set_scale(-5.0);
    REQUIRE(cam.scale() > 0.0);
}

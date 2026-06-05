#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/render/frame_stats.hpp"

using namespace musacad::render;
using Catch::Approx;

TEST_CASE("FrameStats: empty has zero fps") {
    FrameStats fs;
    REQUIRE(fs.fps() == 0.0);
    REQUIRE(fs.sample_count() == 0);
}

TEST_CASE("FrameStats: steady frames yield expected fps") {
    FrameStats fs;
    const double dt = 1.0 / 144.0;
    for (int i = 0; i < 300; ++i) {
        fs.add_frame(dt);
    }
    REQUIRE(fs.fps() == Approx(144.0).epsilon(0.01));
    REQUIRE(fs.average_frame_ms() == Approx(1000.0 / 144.0).epsilon(0.01));
    REQUIRE(fs.sample_count() == 120); // capped to window
}

TEST_CASE("FrameStats: rolling window forgets old frames") {
    FrameStats fs;
    for (int i = 0; i < 120; ++i) {
        fs.add_frame(1.0 / 30.0); // slow frames fill the window
    }
    REQUIRE(fs.fps() == Approx(30.0).epsilon(0.01));
    for (int i = 0; i < 120; ++i) {
        fs.add_frame(1.0 / 240.0); // then fast frames replace them all
    }
    REQUIRE(fs.fps() == Approx(240.0).epsilon(0.01));
}

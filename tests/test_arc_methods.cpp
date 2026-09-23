// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// ARC's construction methods (#34): the arc geometry from each set of constraints
// (arc_construct.hpp) and the command flow with AutoCAD's prompts -- Center and End
// branches, Angle / chord Length / Direction / Radius, Continue, and the Ctrl direction.
#include <cmath>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/math/arc_construct.hpp"

using namespace musacad::core;
using namespace musacad::command;
using Catch::Approx;

namespace {
// A point of the stored arc at parameter t in [0, 1] along its counter-clockwise sweep.
Vec2 at(const ConstructedArc& a, double t) {
    double sweep = a.end - a.start;
    while (sweep < 0.0) {
        sweep += kTwoPi;
    }
    const double ang = a.start + sweep * t;
    return {a.center.x + a.radius * std::cos(ang), a.center.y + a.radius * std::sin(ang)};
}
bool near(Vec2 p, Vec2 q, double eps = 1e-6) {
    return std::abs(p.x - q.x) < eps && std::abs(p.y - q.y) < eps;
}
double sweep_of(const ConstructedArc& a) {
    double s = a.end - a.start;
    while (s < 0.0) {
        s += kTwoPi;
    }
    return s;
}
struct PromptOutput : CommandOutput {
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompts.push_back(p); }
    std::vector<std::string> lines;
    std::vector<std::string> prompts;
};
struct Harness {
    std::vector<Command> cmds;
    PromptOutput out;
    CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    const AddArcCommand* last_arc() const {
        const AddArcCommand* found = nullptr;
        for (const auto& c : cmds) {
            if (const auto* p = std::get_if<AddArcCommand>(&c)) {
                found = p;
            }
        }
        return found;
    }
};
} // namespace

TEST_CASE("arc from three points: the sweep passes through the middle point either way round") {
    const auto ccw = arc_three_points({10, 0}, {0, 10}, {-10, 0});
    REQUIRE(ccw.has_value());
    CHECK(near(ccw->center, {0, 0}));
    CHECK(ccw->radius == Approx(10.0));
    CHECK(near(at(*ccw, 0.5), {0, 10}));
    CHECK(near(ccw->end_point, {-10, 0}));
    CHECK(ccw->end_tangent == Approx(kPi + kHalfPi).margin(1e-9)); // heading down at (-10, 0)

    const auto cw = arc_three_points({10, 0}, {0, -10}, {-10, 0});
    REQUIRE(cw.has_value());
    CHECK(near(at(*cw, 0.5), {0, -10}));
    CHECK(near(cw->end_point, {-10, 0}));
    CHECK(std::cos(cw->end_tangent) == Approx(0.0).margin(1e-9));
    CHECK(std::sin(cw->end_tangent) == Approx(1.0)); // heading up: it came round the bottom
    CHECK(!arc_three_points({0, 0}, {1, 0}, {2, 0}).has_value());
}

TEST_CASE("start, centre, end / angle / chord length; Ctrl draws the other way") {
    const auto sce = arc_start_center_end({10, 0}, {0, 0}, {0, 20}, false);
    REQUIRE(sce.has_value());
    CHECK(sweep_of(*sce) == Approx(kHalfPi));
    CHECK(near(sce->end_point, {0, 10})); // on the ray through the picked end
    const auto sce_cw = arc_start_center_end({10, 0}, {0, 0}, {0, 20}, true);
    REQUIRE(sce_cw.has_value());
    CHECK(sweep_of(*sce_cw) == Approx(3.0 * kHalfPi)); // the long way round, clockwise
    CHECK(near(sce_cw->end_point, {0, 10}));
    CHECK(std::cos(sce_cw->end_tangent) == Approx(1.0)); // heading right at the top, clockwise

    const auto sca = arc_start_center_angle({10, 0}, {0, 0}, to_radians(90.0), false);
    REQUIRE(sca.has_value());
    CHECK(near(sca->end_point, {0, 10}));
    const auto sca_neg = arc_start_center_angle({10, 0}, {0, 0}, to_radians(-90.0), false);
    REQUIRE(sca_neg.has_value());
    CHECK(near(sca_neg->end_point, {0, -10})); // a negative angle is clockwise

    const auto scl = arc_start_center_length({10, 0}, {0, 0}, 10.0 * std::sqrt(2.0), false);
    REQUIRE(scl.has_value());
    CHECK(sweep_of(*scl) == Approx(kHalfPi)); // chord r*sqrt(2) subtends 90 degrees
    const auto scl_major = arc_start_center_length({10, 0}, {0, 0}, -10.0 * std::sqrt(2.0), false);
    REQUIRE(scl_major.has_value());
    CHECK(sweep_of(*scl_major) == Approx(3.0 * kHalfPi));
    CHECK(!arc_start_center_length({10, 0}, {0, 0}, 30.0, false).has_value()); // longer than the diameter
}

TEST_CASE("start, end, angle / direction / radius") {
    const auto sea = arc_start_end_angle({10, 0}, {0, 10}, to_radians(90.0), false);
    REQUIRE(sea.has_value());
    CHECK(near(sea->center, {0, 0}));
    CHECK(sea->radius == Approx(10.0));
    CHECK(near(sea->end_point, {0, 10}));
    // 270 degrees counter-clockwise from (10, 0) to (0, 10) is the long way round the
    // circle centred at (10, 10) (about the origin the counter-clockwise way is a quarter).
    const auto sea_major = arc_start_end_angle({10, 0}, {0, 10}, to_radians(270.0), false);
    REQUIRE(sea_major.has_value());
    CHECK(near(sea_major->center, {10, 10}));
    CHECK(sweep_of(*sea_major) == Approx(3.0 * kHalfPi));
    const auto sea_cw = arc_start_end_angle({10, 0}, {0, 10}, to_radians(90.0), true);
    REQUIRE(sea_cw.has_value());
    CHECK(near(sea_cw->center, {10, 10})); // the mirror-image quarter, clockwise

    // Leaving (10, 0) heading straight up and reaching (0, 10): the quarter about the origin.
    const auto sed = arc_start_end_direction({10, 0}, {0, 10}, kHalfPi);
    REQUIRE(sed.has_value());
    CHECK(near(sed->center, {0, 0}));
    CHECK(near(sed->end_point, {0, 10}));
    CHECK(std::cos(sed->end_tangent) == Approx(-1.0)); // heading left at the top
    // Heading down instead: the arc bends the other way round, clockwise, three quarters.
    const auto sed_cw = arc_start_end_direction({10, 0}, {0, 10}, -kHalfPi);
    REQUIRE(sed_cw.has_value());
    CHECK(near(sed_cw->center, {0, 0}));
    CHECK(sweep_of(*sed_cw) == Approx(3.0 * kHalfPi));
    CHECK(!arc_start_end_direction({0, 0}, {10, 0}, 0.0).has_value()); // the end lies on the tangent

    const auto ser = arc_start_end_radius({10, 0}, {0, 10}, 10.0, false);
    REQUIRE(ser.has_value());
    CHECK(near(ser->center, {0, 0}));
    CHECK(sweep_of(*ser) == Approx(kHalfPi));
    const auto ser_major = arc_start_end_radius({10, 0}, {0, 10}, -10.0, false);
    REQUIRE(ser_major.has_value());
    CHECK(near(ser_major->center, {10, 10}));
    CHECK(sweep_of(*ser_major) == Approx(3.0 * kHalfPi));
    CHECK(!arc_start_end_radius({10, 0}, {0, 10}, 5.0, false).has_value()); // shorter than half the chord
}

namespace {
double cmd_sweep(const AddArcCommand& a) {
    double s = a.end_angle - a.start_angle;
    while (s < 0.0) {
        s += kTwoPi;
    }
    while (s >= kTwoPi) {
        s -= kTwoPi;
    }
    return s;
}
} // namespace

TEST_CASE("ARC: three points, and AutoCAD's prompts along the way") {
    Harness h;
    h.proc.submit_line("A");
    REQUIRE(h.out.prompts.back() == "Specify start point of arc or [Center]: ");
    h.proc.submit_line("10,0");
    REQUIRE(h.out.prompts.back() == "Specify second point of arc or [Center/End]: ");
    h.proc.submit_line("0,10");
    REQUIRE(h.out.prompts.back() == "Specify end point of arc: ");
    REQUIRE(h.proc.preview().kind == PreviewKind::Arc);
    REQUIRE(h.proc.preview().arc_mode == 1);
    h.proc.submit_line("-10,0");
    const AddArcCommand* a = h.last_arc();
    REQUIRE(a != nullptr);
    CHECK(a->center.x == Approx(0.0).margin(1e-9));
    CHECK(a->radius == Approx(10.0));
    CHECK(cmd_sweep(*a) == Approx(kPi));
    REQUIRE(!h.proc.has_active_command());
}

TEST_CASE("ARC Center branch: end, Angle and chord Length; the centre first") {
    Harness h;
    h.proc.submit_line("ARC");
    h.proc.submit_line("10,0");
    h.proc.submit_line("C");
    REQUIRE(h.out.prompts.back() == "Specify center point of arc: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompts.back() ==
            "Specify end point of arc (hold Ctrl to switch direction) or [Angle/chord Length]: ");
    REQUIRE(h.proc.preview().arc_mode == 2);
    h.proc.submit_line("0,20"); // the ray through the pick: the arc ends at (0, 10)
    REQUIRE(cmd_sweep(*h.last_arc()) == Approx(kHalfPi));
    CHECK(h.last_arc()->start_angle == Approx(0.0).margin(1e-9));

    h.proc.submit_line("A");
    h.proc.submit_line("10,0");
    h.proc.submit_line("CENTER");
    h.proc.submit_line("0,0");
    h.proc.submit_line("A");
    REQUIRE(h.out.prompts.back() == "Specify included angle (hold Ctrl to switch direction): ");
    REQUIRE(h.proc.preview().arc_mode == 3);
    h.proc.submit_line("-90"); // negative: clockwise, ending at (0, -10)
    REQUIRE(cmd_sweep(*h.last_arc()) == Approx(kHalfPi));
    CHECK(std::sin(h.last_arc()->start_angle) == Approx(-1.0)); // stored from (0, -10) round to (10, 0)

    h.proc.submit_line("A");
    h.proc.submit_line("10,0");
    h.proc.submit_line("C");
    h.proc.submit_line("0,0");
    h.proc.submit_line("L");
    REQUIRE(h.out.prompts.back() == "Specify length of chord (hold Ctrl to switch direction): ");
    h.proc.submit_line("14.142135624");
    REQUIRE(cmd_sweep(*h.last_arc()) == Approx(kHalfPi).epsilon(1e-6));
    const std::size_t n = h.cmds.size();
    h.proc.submit_line("A");
    h.proc.submit_line("10,0");
    h.proc.submit_line("C");
    h.proc.submit_line("0,0");
    h.proc.submit_line("L");
    h.proc.submit_line("30"); // longer than the diameter: no arc, the prompt stays
    REQUIRE(h.cmds.size() == n);
    REQUIRE(h.proc.has_active_command());
    h.proc.cancel();

    h.proc.submit_line("A");
    h.proc.submit_line("C"); // the centre first
    REQUIRE(h.out.prompts.back() == "Specify center point of arc: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompts.back() == "Specify start point of arc: ");
    h.proc.submit_line("10,0");
    REQUIRE(h.out.prompts.back() ==
            "Specify end point of arc (hold Ctrl to switch direction) or [Angle/chord Length]: ");
    h.proc.submit_line("0,20");
    REQUIRE(cmd_sweep(*h.last_arc()) == Approx(kHalfPi));
}

TEST_CASE("ARC End branch: centre, Angle, Direction and Radius") {
    Harness h;
    h.proc.submit_line("A");
    h.proc.submit_line("10,0");
    h.proc.submit_line("E");
    REQUIRE(h.out.prompts.back() == "Specify end point of arc: ");
    h.proc.submit_line("0,10");
    REQUIRE(h.out.prompts.back() ==
            "Specify center point of arc (hold Ctrl to switch direction) or [Angle/Direction/Radius]: ");
    REQUIRE(h.proc.preview().arc_mode == 5);
    h.proc.submit_line("0,0");
    REQUIRE(cmd_sweep(*h.last_arc()) == Approx(kHalfPi));
    CHECK(h.last_arc()->center.y == Approx(0.0).margin(1e-9));

    h.proc.submit_line("A");
    h.proc.submit_line("10,0");
    h.proc.submit_line("END");
    h.proc.submit_line("0,10");
    h.proc.submit_line("A");
    REQUIRE(h.proc.preview().arc_mode == 6);
    h.proc.submit_line("90");
    CHECK(h.last_arc()->center.x == Approx(0.0).margin(1e-9));
    CHECK(h.last_arc()->center.y == Approx(0.0).margin(1e-9));

    h.proc.submit_line("A");
    h.proc.submit_line("10,0");
    h.proc.submit_line("E");
    h.proc.submit_line("0,10");
    h.proc.submit_line("D");
    REQUIRE(h.out.prompts.back() ==
            "Specify tangent direction for the start point of arc (hold Ctrl to switch direction): ");
    REQUIRE(h.proc.preview().arc_mode == 7);
    h.proc.submit_line("90");
    CHECK(h.last_arc()->center.x == Approx(0.0).margin(1e-9));
    CHECK(h.last_arc()->radius == Approx(10.0));

    h.proc.submit_line("A");
    h.proc.submit_line("10,0");
    h.proc.submit_line("E");
    h.proc.submit_line("0,10");
    h.proc.submit_line("R");
    REQUIRE(h.out.prompts.back() == "Specify radius of arc (hold Ctrl to switch direction): ");
    REQUIRE(h.proc.preview().arc_mode == 8);
    h.proc.submit_line("-10"); // negative: the major arc, centred at (10, 10)
    CHECK(h.last_arc()->center.x == Approx(10.0));
    CHECK(h.last_arc()->center.y == Approx(10.0));
    CHECK(cmd_sweep(*h.last_arc()) == Approx(3.0 * kHalfPi));
}

TEST_CASE("ARC Continue: Enter at the first prompt starts tangent from the last arc or line; Ctrl flips") {
    Harness h;
    h.proc.submit_line("A");
    h.proc.submit_line("10,0");
    h.proc.submit_line("C");
    h.proc.submit_line("0,0");
    h.proc.submit_line("0,20"); // ends at (0, 10) heading left
    h.proc.submit_line("A");
    h.proc.submit_line(""); // continue
    REQUIRE(h.out.prompts.back() == "Specify end point of arc (hold Ctrl to switch direction): ");
    REQUIRE(h.proc.preview().arc_mode == 9);
    h.proc.submit_line("-10,0");
    const AddArcCommand* a = h.last_arc();
    REQUIRE(a != nullptr);
    CHECK(a->center.x == Approx(0.0).margin(1e-9));
    CHECK(a->center.y == Approx(0.0).margin(1e-9));
    CHECK(cmd_sweep(*a) == Approx(kHalfPi));

    // After a line, the arc leaves along the line.
    h.proc.submit_line("L");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,0");
    h.proc.submit_line("");
    h.proc.submit_line("A");
    h.proc.submit_line("");
    h.proc.submit_line("20,10"); // heading +x from (10, 0) and reaching (20, 10): centre (10, 10)
    CHECK(h.last_arc()->center.x == Approx(10.0));
    CHECK(h.last_arc()->center.y == Approx(10.0));

    // Nothing to continue from in a fresh session: Enter just repeats the prompt.
    Harness fresh;
    fresh.proc.submit_line("A");
    fresh.proc.submit_line("");
    REQUIRE(fresh.proc.has_active_command());
    REQUIRE(fresh.cmds.empty());

    // Ctrl held at the pick: the other way round.
    Harness c;
    c.proc.submit_line("A");
    c.proc.submit_line("10,0");
    c.proc.submit_line("C");
    c.proc.submit_line("0,0");
    c.proc.set_ctrl_held(true);
    c.proc.submit_line("0,20");
    REQUIRE(c.last_arc() != nullptr);
    CHECK(cmd_sweep(*c.last_arc()) == Approx(3.0 * kHalfPi));
}

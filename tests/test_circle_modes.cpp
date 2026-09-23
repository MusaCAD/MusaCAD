// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// CIRCLE's construction methods (#35): 3P, 2P, Ttr and Tan, Tan, Tan -- the geometry
// (tangent_circle.hpp), the command flow with AutoCAD's prompts and defaults, and the
// engine resolving the objects under the picks.
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/math/tangent_circle.hpp"

using namespace musacad::core;
using namespace musacad::command;
using Catch::Approx;

namespace {
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
    template <class T>
    const T* last() const {
        const T* found = nullptr;
        for (const auto& c : cmds) {
            if (const auto* p = std::get_if<T>(&c)) {
                found = p;
            }
        }
        return found;
    }
};
template <class Pred>
bool wait_until(GeometryEngine& e, Pred pred) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        e.consume_snapshot();
        if (pred(e.snapshot())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
} // namespace

TEST_CASE("Ttr: the circle of a given radius tangent to two lines, on the picked side") {
    const TangentObject x = TangentObject::line({0, 0}, {100, 0});
    const TangentObject y = TangentObject::line({0, 0}, {0, 100});
    const auto c = circle_tan_tan_radius(x, {30, 0}, y, {0, 30}, 10.0);
    REQUIRE(c.has_value());
    CHECK(c->center.x == Approx(10.0));
    CHECK(c->center.y == Approx(10.0));
    CHECK(c->radius == Approx(10.0));
    // Picks in the other quadrant pick the other branch.
    const auto d = circle_tan_tan_radius(x, {-30, 0}, y, {0, 30}, 10.0);
    REQUIRE(d.has_value());
    CHECK(d->center.x == Approx(-10.0));
    CHECK(d->center.y == Approx(10.0));
    // Two parallel lines 30 apart admit no circle of radius 10 tangent to both.
    const TangentObject far = TangentObject::line({0, 30}, {100, 30});
    CHECK(!circle_tan_tan_radius(x, {50, 0}, far, {50, 30}, 10.0).has_value());
    CHECK(circle_tan_tan_radius(x, {50, 0}, far, {50, 30}, 15.0).has_value());
}

TEST_CASE("Ttr: a line and a circle; two circles") {
    const TangentObject base = TangentObject::line({-100, 0}, {100, 0});
    const TangentObject ball = TangentObject::circle({0, 30}, 10.0);
    // Radius 12 between the line and the ball: the centre sits 12 above the line and 22
    // from the ball's centre -- two mirror solutions; the pick on the right picks x > 0.
    const auto c = circle_tan_tan_radius(base, {20, 0}, ball, {8, 24}, 12.0);
    REQUIRE(c.has_value());
    CHECK(c->center.y == Approx(12.0));
    CHECK(c->center.x > 0.0);
    CHECK(distance(c->center, {0, 30}) == Approx(22.0));
    // Radius 10 is the limiting case: one circle, squeezed under the ball at (0, 10).
    const auto one = circle_tan_tan_radius(base, {20, 0}, ball, {8, 24}, 10.0);
    REQUIRE(one.has_value());
    CHECK(one->center.x == Approx(0.0).margin(1e-9));
    CHECK(one->center.y == Approx(10.0));
    const TangentObject a = TangentObject::circle({0, 0}, 20.0);
    const TangentObject b = TangentObject::circle({60, 0}, 20.0);
    const auto m = circle_tan_tan_radius(a, {20, 5}, b, {40, 5}, 12.0);
    REQUIRE(m.has_value());
    CHECK(m->center.x == Approx(30.0));
    CHECK(m->center.y > 0.0);
    CHECK(distance(m->center, {0, 0}) == Approx(32.0));
}

TEST_CASE("Tan, Tan, Tan: the incircle of three lines; a circle tangent to two lines and a circle") {
    // The triangle (0,0) (60,0) (0,80): inradius r = area / s = 2400 / 120 = 10... with
    // area = 2400 and semi-perimeter (60 + 80 + 100) / 2 = 120 -> r = 20, centre (20, 20).
    const TangentObject ab = TangentObject::line({0, 0}, {60, 0});
    const TangentObject ac = TangentObject::line({0, 0}, {0, 80});
    const TangentObject bc = TangentObject::line({60, 0}, {0, 80});
    const auto in = circle_tan_tan_tan(ab, {20, 0}, ac, {0, 20}, bc, {36, 32});
    REQUIRE(in.has_value());
    CHECK(in->center.x == Approx(20.0));
    CHECK(in->center.y == Approx(20.0));
    CHECK(in->radius == Approx(20.0));
    // Picks on the two legs beyond the far vertices choose the excircle opposite the
    // right angle: radius area / (s - a) = 2400 / 20 = 120, centred at (120, 120).
    const auto ex = circle_tan_tan_tan(ab, {100, 0}, ac, {0, 100}, bc, {30, 40});
    REQUIRE(ex.has_value());
    CHECK(ex->center.x == Approx(120.0));
    CHECK(ex->center.y == Approx(120.0));
    CHECK(ex->radius == Approx(120.0));
    CHECK(std::abs(distance(ab.closest(ex->center), ex->center) - ex->radius) < 1e-6);
    CHECK(std::abs(distance(bc.closest(ex->center), ex->center) - ex->radius) < 1e-6);
    // Two axes and a circle sitting in the corner: the small circle tangent to all three.
    const TangentObject x = TangentObject::line({0, 0}, {100, 0});
    const TangentObject y = TangentObject::line({0, 0}, {0, 100});
    const TangentObject big = TangentObject::circle({40, 40}, 20.0);
    const auto s = circle_tan_tan_tan(x, {10, 0}, y, {0, 10}, big, {26, 26});
    REQUIRE(s.has_value());
    CHECK(s->center.x == Approx(s->center.y));
    CHECK(s->center.x == Approx(s->radius));
    CHECK(distance(s->center, {40, 40}) == Approx(20.0 + s->radius));
    CHECK(s->radius < 20.0);
}

TEST_CASE("CIRCLE: AutoCAD's first prompt, the remembered radius, and the diameter pick") {
    Harness h;
    h.proc.submit_line("C");
    REQUIRE(h.out.prompts.back() == "Specify center point for circle or [3P/2P/Ttr (tan tan radius)]: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.proc.preview().kind == PreviewKind::Circle);
    REQUIRE(h.proc.preview().circle_mode == 0);
    h.proc.submit_line("D");
    REQUIRE(h.proc.preview().circle_mode == 1);
    h.proc.submit_line("8,0"); // a pick at the diameter prompt IS the diameter: 8 -> radius 4
    const auto* c1 = h.last<AddCircleCommand>();
    REQUIRE(c1 != nullptr);
    REQUIRE(c1->radius == Approx(4.0));

    // The last radius is the next default (CIRCLERAD), shown and accepted by Enter.
    h.proc.submit_line("C");
    h.proc.submit_line("10,10");
    REQUIRE(h.out.prompts.back() == "Specify radius of circle or [Diameter] <4.0000>: ");
    h.proc.submit_line("");
    const auto* c2 = h.last<AddCircleCommand>();
    REQUIRE(c2 != nullptr);
    REQUIRE(c2->radius == Approx(4.0));
    REQUIRE(c2->center.x == Approx(10.0));
    h.proc.submit_line("C");
    h.proc.submit_line("0,0");
    h.proc.submit_line("DIAMETER");
    REQUIRE(h.out.prompts.back() == "Specify diameter of circle <8.0000>: ");
    h.proc.submit_line("3");
    REQUIRE(h.last<AddCircleCommand>()->radius == Approx(1.5));
}

TEST_CASE("CIRCLE 2P and 3P") {
    Harness h;
    h.proc.submit_line("CIRCLE");
    h.proc.submit_line("2P");
    REQUIRE(h.out.prompts.back() == "Specify first end point of circle's diameter: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.proc.preview().circle_mode == 2);
    REQUIRE(h.out.prompts.back() == "Specify second end point of circle's diameter: ");
    h.proc.submit_line("10,0");
    const auto* two = h.last<AddCircleCommand>();
    REQUIRE(two != nullptr);
    REQUIRE(two->center.x == Approx(5.0));
    REQUIRE(two->radius == Approx(5.0));

    h.proc.submit_line("C");
    h.proc.submit_line("3P");
    REQUIRE(h.out.prompts.back() == "Specify first point on circle: ");
    h.proc.submit_line("10,0");
    h.proc.submit_line("0,10");
    REQUIRE(h.proc.preview().circle_mode == 3);
    REQUIRE(h.out.prompts.back() == "Specify third point on circle: ");
    h.proc.submit_line("-10,0");
    const auto* three = h.last<AddCircleCommand>();
    REQUIRE(three != nullptr);
    REQUIRE(three->center.x == Approx(0.0).margin(1e-9));
    REQUIRE(three->center.y == Approx(0.0).margin(1e-9));
    REQUIRE(three->radius == Approx(10.0));
    // Collinear points: no circle, the prompt stays.
    const std::size_t n = h.cmds.size();
    h.proc.submit_line("C");
    h.proc.submit_line("3P");
    h.proc.submit_line("0,0");
    h.proc.submit_line("1,0");
    h.proc.submit_line("2,0");
    REQUIRE(h.cmds.size() == n);
    REQUIRE(h.out.lines.back() == "Circle does not exist.");
    REQUIRE(h.proc.has_active_command());
}

TEST_CASE("CIRCLE Ttr and TTT hand the picks to the engine; the engine builds the tangent circle") {
    Harness h;
    h.proc.set_pick_radius(1.0);
    h.proc.submit_line("C");
    h.proc.submit_line("T");
    REQUIRE(h.out.prompts.back() == "Specify point on object for first tangent of circle: ");
    h.proc.submit_line("30,0");
    h.proc.submit_line("0,30");
    REQUIRE(h.out.prompts.back().rfind("Specify radius of circle", 0) == 0); // <last> when one exists
    h.proc.submit_line("10");
    REQUIRE(h.last<AddCircleTangentCommand>() != nullptr);
    const AddCircleTangentCommand ttr = *h.last<AddCircleTangentCommand>(); // a copy: more commands follow
    REQUIRE(ttr.picks.size() == 2);
    REQUIRE(ttr.radius == Approx(10.0));
    REQUIRE(ttr.pick_radius == Approx(1.0));

    h.proc.submit_line("C");
    h.proc.submit_line("TTT");
    h.proc.submit_line("20,0");
    h.proc.submit_line("0,20");
    h.proc.submit_line("36,32");
    const auto* ttt = h.last<AddCircleTangentCommand>();
    REQUIRE(ttt != nullptr);
    REQUIRE(ttt->picks.size() == 3);
    REQUIRE(ttt->radius == Approx(0.0));

    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    engine.submit(AddLineCommand{{0, 0}, {0, 100}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    engine.submit(ttr);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("Circle: radius") != std::string::npos; }));
    // The circle of radius 10 in the corner: its tessellation reaches x = 20 at y = 10.
    const RenderSnapshot& s = engine.snapshot();
    bool rightmost = false;
    for (const Vec2& v : s.line_vertices) {
        if (std::abs(v.x - 20.0) < 1e-6 && std::abs(v.y - 10.0) < 1e-6) {
            rightmost = true;
        }
    }
    CHECK(rightmost);
    // No object under a pick: no circle, a message.
    AddCircleTangentCommand nowhere{{{500, 500}, {30, 0}}, 5.0, 1.0, 2};
    engine.submit(nowhere);
    REQUIRE(wait_until(engine, [](const auto& s2) { return s2.status.find("Circle does not exist.") != std::string::npos; }));
    engine.stop();
}

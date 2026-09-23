// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// PLINE's Arc mode (#37): arc segments tangent to the last one, the Angle / CEnter /
// CLose / Direction / Line / Radius / Second pt / Undo sub-options, Length in line mode,
// and the bulges the committed polyline carries.
#include <cmath>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/math/math.hpp"

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
    const AddPolylineCommand* poly() const {
        const AddPolylineCommand* found = nullptr;
        for (const auto& c : cmds) {
            if (const auto* p = std::get_if<AddPolylineCommand>(&c)) {
                found = p;
            }
        }
        return found;
    }
};
constexpr const char* kArcPrompt =
    "Specify endpoint of arc (hold Ctrl to switch direction) or "
    "[Angle/CEnter/CLose/Direction/Line/Radius/Second pt/Undo]: ";
} // namespace

TEST_CASE("PLINE: a tangent arc after a line, then Line mode again, then Close") {
    Harness h;
    h.proc.submit_line("PL");
    REQUIRE(h.out.lines.back().rfind("Current line-width is", 0) == 0);
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompts.back() == "Specify next point or [Arc/Length/Undo]: ");
    h.proc.submit_line("10,0");
    REQUIRE(h.out.prompts.back() == "Specify next point or [Arc/Close/Length/Undo]: ");
    h.proc.submit_line("A");
    REQUIRE(h.out.prompts.back() == kArcPrompt);
    REQUIRE(h.proc.preview().pline_arc_mode == 1);
    h.proc.submit_line("10,10"); // leaving east from (10, 0) and reaching (10, 10): a semicircle
    REQUIRE(h.out.prompts.back() == kArcPrompt);
    h.proc.submit_line("L");
    REQUIRE(h.out.prompts.back() == "Specify next point or [Arc/Close/Length/Undo]: ");
    h.proc.submit_line("0,10");
    h.proc.submit_line("CLOSE");
    const AddPolylineCommand* p = h.poly();
    REQUIRE(p != nullptr);
    REQUIRE(p->closed);
    REQUIRE(p->points.size() == 4);
    REQUIRE(p->bulges.size() == 4);
    CHECK(p->bulges[0] == Approx(0.0).margin(1e-12));
    CHECK(p->bulges[1] == Approx(1.0)); // a semicircle bulges tan(pi / 4) = 1
    CHECK(p->bulges[2] == Approx(0.0).margin(1e-12));
}

TEST_CASE("PLINE Arc: CEnter, Radius, Angle, Direction and Second pt each place the arc") {
    Harness h;
    h.proc.submit_line("PL");
    h.proc.submit_line("10,0");
    h.proc.submit_line("A");
    h.proc.submit_line("CE");
    REQUIRE(h.out.prompts.back() == "Specify center point of arc: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompts.back() ==
            "Specify endpoint of arc (hold Ctrl to switch direction) or [Angle/Length]: ");
    REQUIRE(h.proc.preview().pline_arc_mode == 3);
    h.proc.submit_line("0,20"); // the quarter from (10, 0) to (0, 10), counter-clockwise
    h.proc.submit_line("R");
    h.proc.submit_line("10");
    REQUIRE(h.out.prompts.back() == "Specify endpoint of arc (hold Ctrl to switch direction) or [Angle]: ");
    h.proc.submit_line("-10,0"); // the minor arc of radius 10 from (0, 10) to (-10, 0)
    h.proc.submit_line("A");
    REQUIRE(h.out.prompts.back() == "Specify included angle: ");
    h.proc.submit_line("90");
    REQUIRE(h.out.prompts.back() ==
            "Specify endpoint of arc (hold Ctrl to switch direction) or [CEnter/Radius]: ");
    h.proc.submit_line("0,-10");
    h.proc.submit_line("D");
    h.proc.submit_line("0"); // leave (0, -10) heading east
    REQUIRE(h.out.prompts.back() == "Specify endpoint of arc: ");
    h.proc.submit_line("10,0");
    h.proc.submit_line("S");
    h.proc.submit_line("20,10");
    REQUIRE(h.out.prompts.back() == "Specify end point of arc: ");
    h.proc.submit_line("30,0");
    h.proc.submit_line("");
    const AddPolylineCommand* p = h.poly();
    REQUIRE(p != nullptr);
    REQUIRE(!p->closed);
    REQUIRE(p->points.size() == 6);
    REQUIRE(p->bulges.size() == 6);
    // Every one of the first four is a quarter turn counter-clockwise: bulge tan(pi/8).
    for (int i = 0; i < 4; ++i) {
        CHECK(p->bulges[static_cast<std::size_t>(i)] == Approx(std::tan(kPi / 8.0)));
    }
    CHECK(p->points[1].x == Approx(0.0).margin(1e-9));
    CHECK(p->points[1].y == Approx(10.0));
    CHECK(p->points[2].x == Approx(-10.0));
    CHECK(p->points[4].x == Approx(10.0));
    // The three-point arc through (20, 10) to (30, 0) bulges clockwise.
    CHECK(p->bulges[4] == Approx(-1.0));
}

TEST_CASE("PLINE: Length continues along the last segment; Undo removes one; CLose closes with an arc") {
    Harness h;
    h.proc.submit_line("PL");
    h.proc.submit_line("0,0");
    h.proc.submit_line("3,4");
    h.proc.submit_line("L");
    REQUIRE(h.out.prompts.back() == "Specify length of line: ");
    h.proc.submit_line("5"); // along the 3-4-5 direction: (6, 8)
    REQUIRE(h.proc.preview().points.back().x == Approx(6.0));
    REQUIRE(h.proc.preview().points.back().y == Approx(8.0));
    h.proc.submit_line("U");
    REQUIRE(h.proc.preview().points.back().x == Approx(3.0));
    h.proc.submit_line("U");
    h.proc.submit_line("10,0");
    h.proc.submit_line("10,10"); // heading north
    h.proc.submit_line("A");
    h.proc.submit_line("CL"); // an arc from (10, 10) back to (0, 0), tangent to the last segment
    const AddPolylineCommand* p = h.poly();
    REQUIRE(p != nullptr);
    REQUIRE(p->closed);
    REQUIRE(p->points.size() == 3);
    REQUIRE(p->bulges.size() == 3);
    // Leaving (10, 10) northwards and reaching (0, 0): centre (0, 10), three quarters round.
    CHECK(p->bulges[2] == Approx(std::tan(3.0 * kPi / 8.0)));
    // The next PLINE + Arc leaves along the last drawn segment's heading.
    Harness g;
    g.proc.submit_line("PL");
    g.proc.submit_line("0,0");
    g.proc.submit_line("A");
    g.proc.submit_line("U");
    REQUIRE(g.out.prompts.back() == "Specify start point: ");
    g.proc.cancel();
}

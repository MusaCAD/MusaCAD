// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// RECTANGLE's corner options (issue #33): Chamfer and Fillet, with AutoCAD's prompts,
// session-persistent defaults, "last one set wins", and the square-corner fallback when
// the treatment does not fit. The corner math itself is core::polyline_ops, shared with
// FILLET/CHAMFER and tested there; these cases test the command's use of it.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {
struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string&) override {}
    void set_prompt(const std::string&) override {}
};
struct ProcHarness {
    std::vector<Command> cmds;
    SilentOutput out;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    [[nodiscard]] const AddPolylineCommand& poly() const {
        return std::get<AddPolylineCommand>(cmds.at(0));
    }
};
bool has_pt(const std::vector<Vec2>& v, Vec2 p) {
    return std::any_of(v.begin(), v.end(), [&](const Vec2& q) { return length(q - p) < 1e-9; });
}
/// Reset the session defaults so cases do not leak into each other.
void reset_defaults() {
    ProcHarness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("F");
    h.proc.submit_line("0");
    h.proc.cancel();
}
} // namespace

TEST_CASE("#33: RECTANGLE [Fillet] rounds all four corners") {
    reset_defaults();
    ProcHarness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("F");
    h.proc.submit_line("5");
    h.proc.submit_line("0,0");
    h.proc.submit_line("100,50");
    REQUIRE(h.cmds.size() == 1);
    const AddPolylineCommand& p = h.poly();
    REQUIRE(p.closed);
    REQUIRE(p.points.size() == 8);   // two tangent points per corner
    REQUIRE(p.bulges.size() == 8);
    int arcs = 0;
    for (double b : p.bulges) {
        if (b != 0.0) {
            REQUIRE(std::abs(b) == Approx(std::tan(kPi / 8.0))); // quarter circles
            ++arcs;
        }
    }
    REQUIRE(arcs == 4);
    REQUIRE(has_pt(p.points, {95, 0}));  // tangent points 5 back from each corner
    REQUIRE(has_pt(p.points, {100, 5}));
    REQUIRE(has_pt(p.points, {5, 50}));
    REQUIRE(has_pt(p.points, {0, 45}));
    reset_defaults();
}

TEST_CASE("#33: RECTANGLE [Chamfer] cuts all four corners at the two distances") {
    reset_defaults();
    ProcHarness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("C");
    h.proc.submit_line("5");
    h.proc.submit_line("10");
    h.proc.submit_line("0,0");
    h.proc.submit_line("100,50");
    REQUIRE(h.cmds.size() == 1);
    const AddPolylineCommand& p = h.poly();
    REQUIRE(p.points.size() == 8);
    REQUIRE(p.bulges.empty()); // straight cuts, no arcs
    REQUIRE(has_pt(p.points, {95, 0}));  // 5 back along the incoming edge at (100,0)
    REQUIRE(has_pt(p.points, {100, 10})); // 10 along the outgoing edge
    reset_defaults();
}

TEST_CASE("#33: an oversized fillet falls back to square corners") {
    reset_defaults();
    ProcHarness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("F");
    h.proc.submit_line("40"); // needs 80 of a 50-high side
    h.proc.submit_line("0,0");
    h.proc.submit_line("100,50");
    REQUIRE(h.cmds.size() == 1);
    REQUIRE(h.poly().points.size() == 4);
    REQUIRE(h.poly().bulges.empty());
    reset_defaults();
}

TEST_CASE("#33: the fillet radius persists for the next rectangle, and Enter keeps it") {
    reset_defaults();
    {
        ProcHarness h;
        h.proc.submit_line("REC");
        h.proc.submit_line("F");
        h.proc.submit_line("5");
        h.proc.submit_line("0,0");
        h.proc.submit_line("100,50");
        REQUIRE(h.poly().points.size() == 8);
    }
    {
        ProcHarness h; // a new command in the same session: no option given
        h.proc.submit_line("REC");
        h.proc.submit_line("0,0");
        h.proc.submit_line("100,50");
        REQUIRE(h.poly().points.size() == 8); // still filleted
    }
    {
        ProcHarness h; // Enter at the radius prompt keeps the default
        h.proc.submit_line("REC");
        h.proc.submit_line("F");
        h.proc.submit_line("");
        h.proc.submit_line("0,0");
        h.proc.submit_line("100,50");
        REQUIRE(h.poly().points.size() == 8);
    }
    reset_defaults();
}

TEST_CASE("#33: setting a chamfer clears the fillet and vice versa") {
    reset_defaults();
    {
        ProcHarness h;
        h.proc.submit_line("REC");
        h.proc.submit_line("F");
        h.proc.submit_line("5");
        h.proc.submit_line("C");
        h.proc.submit_line("5");
        h.proc.submit_line("5");
        h.proc.submit_line("0,0");
        h.proc.submit_line("100,50");
        REQUIRE(h.poly().points.size() == 8);
        REQUIRE(h.poly().bulges.empty()); // chamfered, not filleted
    }
    {
        ProcHarness h;
        h.proc.submit_line("REC");
        h.proc.submit_line("F");
        h.proc.submit_line("0"); // radius 0 = square corners again, and chamfer cleared
        h.proc.submit_line("0,0");
        h.proc.submit_line("100,50");
        REQUIRE(h.poly().points.size() == 4);
    }
    reset_defaults();
}

namespace {
struct PromptOutput : musacad::command::CommandOutput {
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompts.push_back(p); }
    std::vector<std::string> lines;
    std::vector<std::string> prompts;
};
struct PromptHarness {
    std::vector<Command> cmds;
    PromptOutput out;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
};
} // namespace

TEST_CASE("#36: RECTANG wording, remembered defaults, and Rotation by picked points") {
    reset_defaults();
    PromptHarness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompts.back() == "Specify other corner point or [Area/Dimensions/Rotation]: ");
    h.proc.submit_line("D");
    // The defaults are whatever the last rectangle used (earlier cases set their own).
    REQUIRE(h.out.prompts.back().rfind("Specify length for rectangles <", 0) == 0);
    h.proc.submit_line("40");
    REQUIRE(h.out.prompts.back().rfind("Specify width for rectangles <", 0) == 0);
    h.proc.submit_line("20");
    h.proc.submit_line("5,5");
    REQUIRE(h.cmds.size() == 1);
    // Next time the values are the defaults, and Enter takes them.
    h.proc.submit_line("REC");
    h.proc.submit_line("0,0");
    h.proc.submit_line("D");
    REQUIRE(h.out.prompts.back() == "Specify length for rectangles <40.0000>: ");
    h.proc.submit_line("");
    REQUIRE(h.out.prompts.back() == "Specify width for rectangles <20.0000>: ");
    h.proc.submit_line("");
    h.proc.submit_line("5,5");
    const auto& pl = std::get<AddPolylineCommand>(h.cmds.at(1));
    REQUIRE(has_pt(pl.points, {40, 20}));

    // Rotation by two picked points: 45 degrees, remembered for the next rectangle.
    h.proc.submit_line("REC");
    h.proc.submit_line("0,0");
    h.proc.submit_line("R");
    REQUIRE(h.out.prompts.back() == "Specify rotation angle or [Pick points] <0>: ");
    h.proc.submit_line("P");
    REQUIRE(h.out.prompts.back() == "Specify first point: ");
    h.proc.submit_line("100,100");
    REQUIRE(h.out.prompts.back() == "Specify second point: ");
    h.proc.submit_line("110,110");
    REQUIRE(h.out.prompts.back() == "Specify other corner point or [Area/Dimensions/Rotation]: ");
    h.proc.submit_line("10,0"); // a corner 10 along x, turned 45 degrees: (7.07, 7.07)
    const auto& rot = std::get<AddPolylineCommand>(h.cmds.at(2));
    REQUIRE(has_pt(rot.points, {10.0 / std::sqrt(2.0), 10.0 / std::sqrt(2.0)}));
    h.proc.submit_line("REC");
    h.proc.submit_line("0,0");
    h.proc.submit_line("R");
    REQUIRE(h.out.prompts.back() == "Specify rotation angle or [Pick points] <45>: ");
    h.proc.submit_line("0"); // back to square
    h.proc.cancel();
}

TEST_CASE("#36: the Area option means the finished shape's area, corner cut-outs included") {
    reset_defaults();
    PromptHarness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("F");
    h.proc.submit_line("2");
    h.proc.submit_line("0,0");
    h.proc.submit_line("A");
    REQUIRE(h.out.prompts.back().rfind("Enter area of rectangle in current units <", 0) == 0);
    h.proc.submit_line("100");
    h.proc.submit_line("L");
    h.proc.submit_line("10");
    h.proc.submit_line("5,5");
    // A 10 x W rectangle with four r=2 rounded corners has area 10 W - (4 - pi) 4;
    // asking for 100 gives W = (100 + (4 - pi) 4) / 10.
    const double w = (100.0 + (4.0 - kPi) * 4.0) / 10.0;
    const auto& pl = std::get<AddPolylineCommand>(h.cmds.at(0));
    double max_y = 0.0;
    for (const Vec2& p : pl.points) {
        max_y = std::max(max_y, p.y);
    }
    REQUIRE(max_y == Approx(w));
    // The mode echo names the treatment in force.
    PromptHarness e;
    e.proc.submit_line("REC");
    REQUIRE(std::any_of(e.out.lines.begin(), e.out.lines.end(), [](const std::string& l) {
        return l == "Current rectangle modes: Fillet=2.0000";
    }));
    e.proc.cancel();
    reset_defaults();
}

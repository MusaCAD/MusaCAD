#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"

using namespace musacad::command;
using musacad::core::Vec2;
using Catch::Approx;

namespace {
struct SilentOutput : CommandOutput {
    void append_line(const std::string&) override {}
    void set_prompt(const std::string&) override {}
};
struct Harness {
    std::vector<musacad::core::Command> cmds;
    SilentOutput out;
    CommandProcessor proc{[this](musacad::core::Command c) { cmds.push_back(std::move(c)); }, nullptr,
                          out};
};
} // namespace

TEST_CASE("Ortho constrains a free second point to horizontal/vertical") {
    Harness h;
    h.proc.set_ortho(true);
    h.proc.submit_line("L");
    h.proc.submit_line("0,0");                  // first point
    h.proc.pick_point(Vec2{50.0, 5.0}, std::nullopt); // free pick: dx>dy -> horizontal

    REQUIRE(h.cmds.size() == 1);
    const auto& line = std::get<musacad::core::AddLineCommand>(h.cmds[0]);
    REQUIRE(line.a == Vec2{0, 0});
    REQUIRE(line.b == Vec2{50.0, 0.0}); // y snapped to last point's y
}

TEST_CASE("Ortho picks the vertical when dy dominates") {
    Harness h;
    h.proc.set_ortho(true);
    h.proc.submit_line("L");
    h.proc.submit_line("0,0");
    h.proc.pick_point(Vec2{4.0, 80.0}, std::nullopt); // dy>dx -> vertical
    const auto& line = std::get<musacad::core::AddLineCommand>(h.cmds.back());
    REQUIRE(line.b == Vec2{0.0, 80.0});
}

TEST_CASE("OSNAP point overrides ortho constraint on a pick") {
    Harness h;
    h.proc.set_ortho(true);
    h.proc.submit_line("L");
    h.proc.submit_line("0,0");
    // A snap point is provided -> used verbatim, ortho not applied.
    h.proc.pick_point(Vec2{4.0, 80.0}, Vec2{33.0, 77.0});
    const auto& line = std::get<musacad::core::AddLineCommand>(h.cmds.back());
    REQUIRE(line.b == Vec2{33.0, 77.0});
}

TEST_CASE("Polar tracking snaps the direction to 45-degree increments") {
    Harness h;
    h.proc.set_polar(true);
    h.proc.submit_line("L");
    h.proc.submit_line("0,0");
    h.proc.pick_point(Vec2{10.0, 9.0}, std::nullopt); // ~42deg -> snaps to 45deg
    const auto& line = std::get<musacad::core::AddLineCommand>(h.cmds.back());
    const double dist = std::sqrt(10.0 * 10.0 + 9.0 * 9.0);
    const double expected = dist * std::cos(musacad::core::kPi / 4.0);
    REQUIRE(line.b.x == Approx(expected));
    REQUIRE(line.b.y == Approx(expected));
}

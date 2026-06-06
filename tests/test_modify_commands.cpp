// Command-flow tests: the Phase-10 Modify state machines, driven through the
// CommandProcessor exactly as the command line does, emit the right messages.

#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/math/math.hpp"

using namespace musacad::command;
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

TEST_CASE("ROTATE flow emits RotateSelectionCommand with the typed angle") {
    Harness h;
    h.proc.set_selection_count(1); // ROTATE requires a selection
    h.proc.submit_line("RO");
    h.proc.submit_line("0,0"); // base point
    h.proc.submit_line("90");  // degrees
    REQUIRE(h.cmds.size() == 1);
    const auto* rot = std::get_if<musacad::core::RotateSelectionCommand>(&h.cmds[0]);
    REQUIRE(rot != nullptr);
    REQUIRE(rot->angle == Approx(musacad::core::kHalfPi));
}

TEST_CASE("ROTATE with no selection does nothing") {
    Harness h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("RO");
    h.proc.submit_line("0,0");
    h.proc.submit_line("90");
    REQUIRE(h.cmds.empty());
}

TEST_CASE("SCALE flow emits ScaleSelectionCommand with the typed factor") {
    Harness h;
    h.proc.set_selection_count(2);
    h.proc.submit_line("SC");
    h.proc.submit_line("0,0");
    h.proc.submit_line("3");
    REQUIRE(h.cmds.size() == 1);
    const auto* sc = std::get_if<musacad::core::ScaleSelectionCommand>(&h.cmds[0]);
    REQUIRE(sc != nullptr);
    REQUIRE(sc->factor == Approx(3.0));
}

TEST_CASE("ARRAY rectangular flow emits ArrayRectCommand") {
    Harness h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("AR");
    h.proc.submit_line("R"); // rectangular
    h.proc.submit_line("2"); // rows
    h.proc.submit_line("3"); // cols
    h.proc.submit_line("10"); // row spacing
    h.proc.submit_line("15"); // col spacing
    REQUIRE(h.cmds.size() == 1);
    const auto* ar = std::get_if<musacad::core::ArrayRectCommand>(&h.cmds[0]);
    REQUIRE(ar != nullptr);
    REQUIRE(ar->rows == 2);
    REQUIRE(ar->cols == 3);
    REQUIRE(ar->dx == Approx(15.0));
    REQUIRE(ar->dy == Approx(10.0));
}

TEST_CASE("FILLET flow emits FilletPickCommand with radius and two picks") {
    Harness h;
    h.proc.submit_line("F");
    h.proc.submit_line("2");     // radius
    h.proc.submit_line("10,0");  // first line pick
    h.proc.submit_line("0,10");  // second line pick
    REQUIRE(h.cmds.size() == 1);
    const auto* f = std::get_if<musacad::core::FilletPickCommand>(&h.cmds[0]);
    REQUIRE(f != nullptr);
    REQUIRE(f->radius == Approx(2.0));
    REQUIRE(f->pick1.x == Approx(10.0));
    REQUIRE(f->pick2.y == Approx(10.0));
}

TEST_CASE("CHAMFER flow emits ChamferPickCommand with two distances") {
    Harness h;
    h.proc.submit_line("CHA");
    h.proc.submit_line("2");    // dist1
    h.proc.submit_line("3");    // dist2
    h.proc.submit_line("10,0"); // first line
    h.proc.submit_line("0,10"); // second line
    REQUIRE(h.cmds.size() == 1);
    const auto* c = std::get_if<musacad::core::ChamferPickCommand>(&h.cmds[0]);
    REQUIRE(c != nullptr);
    REQUIRE(c->dist1 == Approx(2.0));
    REQUIRE(c->dist2 == Approx(3.0));
}

TEST_CASE("CHAMFER Angle method (length + angle, default 45) emits equal distances") {
    Harness h;
    h.proc.submit_line("CHA");
    h.proc.submit_line("A");    // Angle method
    h.proc.submit_line("5");    // chamfer length on first line
    h.proc.submit_line("45");   // angle -> dist2 = 5 * tan(45) = 5
    h.proc.submit_line("10,0");
    h.proc.submit_line("0,10");
    REQUIRE(h.cmds.size() == 1);
    const auto* c = std::get_if<musacad::core::ChamferPickCommand>(&h.cmds[0]);
    REQUIRE(c != nullptr);
    REQUIRE(c->dist1 == Approx(5.0));
    REQUIRE(c->dist2 == Approx(5.0)); // 45 degrees => equal legs
}

TEST_CASE("EXTEND flow emits ExtendPickCommand per pick") {
    Harness h;
    h.proc.submit_line("EX");
    h.proc.submit_line("5,0");
    REQUIRE(h.cmds.size() == 1);
    REQUIRE(std::holds_alternative<musacad::core::ExtendPickCommand>(h.cmds[0]));
}

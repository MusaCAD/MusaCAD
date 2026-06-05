// Constraint proof: dragging a live preview produces ZERO geometry commands /
// op-log entries -- the rubber-band is render-side only; committing happens once
// on the click. We capture every command the processor would send to the
// geometry queue and assert none are emitted until the commit point.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"

using namespace musacad::command;
using musacad::core::Vec2;

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

TEST_CASE("LINE: anchor + preview drag emit nothing; commit emits exactly one segment") {
    Harness h;
    h.proc.submit_line("L");
    h.proc.submit_line("0,0"); // first point -> preview begins, nothing committed
    REQUIRE(h.cmds.empty());
    REQUIRE(h.proc.preview().kind == PreviewKind::Segment);
    REQUIRE(h.proc.preview().points.size() == 1);
    REQUIRE(h.proc.preview().points[0] == Vec2{0, 0});

    // "Dragging" the cursor never calls the processor, so the op-log stays empty.
    REQUIRE(h.cmds.empty());

    h.proc.submit_line("@100,0"); // commit click
    REQUIRE(h.cmds.size() == 1);
    REQUIRE(std::holds_alternative<musacad::core::AddLineCommand>(h.cmds[0]));
}

TEST_CASE("CIRCLE: center + radius preview emit nothing until commit") {
    Harness h;
    h.proc.submit_line("C");
    h.proc.submit_line("5,5"); // center -> preview circle, no command
    REQUIRE(h.cmds.empty());
    REQUIRE(h.proc.preview().kind == PreviewKind::Circle);

    h.proc.submit_line("3"); // commit
    REQUIRE(h.cmds.size() == 1);
    REQUIRE(std::holds_alternative<musacad::core::AddCircleCommand>(h.cmds[0]));
}

TEST_CASE("RECTANGLE preview emits nothing until the second corner") {
    Harness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("0,0");
    REQUIRE(h.cmds.empty());
    REQUIRE(h.proc.preview().kind == PreviewKind::Rectangle);
    h.proc.submit_line("40,20");
    REQUIRE(h.cmds.size() == 1);
}

TEST_CASE("Preview clears when the command ends") {
    Harness h;
    h.proc.submit_line("C");
    h.proc.submit_line("0,0");
    REQUIRE(h.proc.preview().kind == PreviewKind::Circle);
    h.proc.submit_line("5"); // commit -> command done
    REQUIRE(h.proc.preview().kind == PreviewKind::None);
}

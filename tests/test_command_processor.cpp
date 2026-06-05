#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"

using namespace musacad::command;
using musacad::core::Vec2;
using Catch::Approx;

namespace {

struct CaptureOutput : CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
    bool any_contains(const std::string& sub) const {
        for (const auto& l : lines) {
            if (l.find(sub) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

struct FakeView : ViewControl {
    int extents = 0;
    double scale = 0.0;
    void zoom_extents() override { ++extents; }
    void zoom_scale(double f) override { scale = f; }
};

struct Harness {
    std::vector<musacad::core::Command> cmds;
    CaptureOutput out;
    FakeView view;
    CommandProcessor proc{[this](musacad::core::Command c) { cmds.push_back(std::move(c)); }, &view,
                          out};
};

} // namespace

TEST_CASE("Processor: canonical LINE 0,0 @100,0 ESC draws a horizontal line") {
    Harness h;
    h.proc.submit_line("L");
    h.proc.submit_line("0,0");
    h.proc.submit_line("@100,0");
    h.proc.cancel(); // ESC

    REQUIRE(h.cmds.size() == 1);
    const auto* line = std::get_if<musacad::core::AddLineCommand>(&h.cmds[0]);
    REQUIRE(line != nullptr);
    REQUIRE(line->a == Vec2{0.0, 0.0});
    REQUIRE(line->b == Vec2{100.0, 0.0});
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("Processor: malformed coordinate re-prompts without aborting") {
    Harness h;
    h.proc.submit_line("L");
    h.proc.submit_line("0,0");
    h.proc.submit_line("not-a-point"); // rejected, command stays active
    REQUIRE(h.proc.has_active_command());
    REQUIRE(h.out.any_contains("Malformed"));
    REQUIRE(h.cmds.empty());
    h.proc.submit_line("@100,0"); // now valid
    REQUIRE(h.cmds.size() == 1);
}

TEST_CASE("Processor: CIRCLE center + radius") {
    Harness h;
    h.proc.submit_line("C");
    h.proc.submit_line("5,5");
    h.proc.submit_line("3");
    REQUIRE(h.cmds.size() == 1);
    const auto* c = std::get_if<musacad::core::AddCircleCommand>(&h.cmds[0]);
    REQUIRE(c != nullptr);
    REQUIRE(c->center == Vec2{5.0, 5.0});
    REQUIRE(c->radius == Approx(3.0));
}

TEST_CASE("Processor: RECTANGLE makes a closed 4-point polyline") {
    Harness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,5");
    REQUIRE(h.cmds.size() == 1);
    const auto* pl = std::get_if<musacad::core::AddPolylineCommand>(&h.cmds[0]);
    REQUIRE(pl != nullptr);
    REQUIRE(pl->closed);
    REQUIRE(pl->points.size() == 4);
    REQUIRE(pl->points[2] == Vec2{10.0, 5.0});
}

TEST_CASE("Processor: 3-point ARC") {
    Harness h;
    h.proc.submit_line("A");
    h.proc.submit_line("0,0");
    h.proc.submit_line("1,1");
    h.proc.submit_line("2,0");
    REQUIRE(h.cmds.size() == 1);
    const auto* a = std::get_if<musacad::core::AddArcCommand>(&h.cmds[0]);
    REQUIRE(a != nullptr);
    REQUIRE(a->center.x == Approx(1.0));
    REQUIRE(a->center.y == Approx(0.0).margin(1e-9));
    REQUIRE(a->radius == Approx(1.0));
}

TEST_CASE("Processor: PLINE with Close") {
    Harness h;
    h.proc.submit_line("PL");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,0");
    h.proc.submit_line("10,10");
    h.proc.submit_line("C");
    REQUIRE(h.cmds.size() == 1);
    const auto* pl = std::get_if<musacad::core::AddPolylineCommand>(&h.cmds[0]);
    REQUIRE(pl != nullptr);
    REQUIRE(pl->closed);
    REQUIRE(pl->points.size() == 3);
}

TEST_CASE("Processor: each LINE invocation gets its own undo group") {
    Harness h;
    h.proc.submit_line("L");
    h.proc.submit_line("0,0");
    h.proc.submit_line("1,0");
    h.proc.submit_line("2,0"); // two segments, same group
    h.proc.submit_line("");    // end LINE
    REQUIRE(h.cmds.size() == 2);
    // Copy group ids (pushing more commands may reallocate h.cmds).
    const std::uint64_t g1 = std::get<musacad::core::AddLineCommand>(h.cmds[0]).group;
    const std::uint64_t g2 = std::get<musacad::core::AddLineCommand>(h.cmds[1]).group;
    REQUIRE(g1 == g2); // grouped together

    h.proc.submit_line("L"); // a new invocation -> new group
    h.proc.submit_line("0,0");
    h.proc.submit_line("0,1");
    h.proc.submit_line("");
    const std::uint64_t g3 = std::get<musacad::core::AddLineCommand>(h.cmds.back()).group;
    REQUIRE(g3 != g1);
}

TEST_CASE("Processor: ENTER repeats the last command") {
    Harness h;
    h.proc.submit_line("C");
    h.proc.submit_line("0,0");
    h.proc.submit_line("1");
    REQUIRE_FALSE(h.proc.has_active_command());
    h.proc.submit_line(""); // ENTER on empty -> repeat CIRCLE
    REQUIRE(h.proc.has_active_command());
    REQUIRE(h.out.prompt.find("center") != std::string::npos);
}

TEST_CASE("Processor: ERASE and UNDO emit messages") {
    Harness h;
    h.proc.submit_line("ERASE");
    h.proc.submit_line("L");
    REQUIRE(std::holds_alternative<musacad::core::EraseCommand>(h.cmds.back()));
    REQUIRE(std::get<musacad::core::EraseCommand>(h.cmds.back()).scope ==
            musacad::core::EraseScope::Last);

    h.proc.submit_line("U");
    REQUIRE(std::holds_alternative<musacad::core::UndoLastGroupCommand>(h.cmds.back()));
}

TEST_CASE("Processor: ZOOM drives the view, not geometry") {
    Harness h;
    h.proc.submit_line("ZOOM");
    h.proc.submit_line("E");
    REQUIRE(h.view.extents == 1);
    REQUIRE(h.cmds.empty()); // no geometry message

    h.proc.submit_line("Z");
    h.proc.submit_line("2");
    REQUIRE(h.view.scale == Approx(2.0));
    REQUIRE(h.cmds.empty());
}

TEST_CASE("Processor: unknown command reports an error") {
    Harness h;
    h.proc.submit_line("FLOOP");
    REQUIRE(h.out.any_contains("Unknown command"));
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("Registry is data-driven and trivially extensible") {
    Harness h;
    REQUIRE(h.proc.registry().contains("LINE"));
    REQUIRE(h.proc.registry().contains("l"));
    REQUIRE_FALSE(h.proc.registry().contains("WOBBLE"));
    // Adding a command is a single row, no parser changes:
    h.proc.registry().register_command({"WOBBLE"},
                                       [] { return std::make_unique<LineCommand>(); });
    REQUIRE(h.proc.registry().contains("WOBBLE"));
}

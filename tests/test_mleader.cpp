// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// MLEADER and LEADER's options (#61).

#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"

using namespace musacad::core;
using musacad::command::CommandOutput;
using musacad::command::CommandProcessor;
using Catch::Approx;

namespace {
struct Out : CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
};

struct H {
    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    template <class T>
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
    void run(std::initializer_list<const char*> lines) {
        for (const char* l : lines) {
            proc.submit_line(l);
        }
    }
};
} // namespace

TEST_CASE("MLEADER: arrowhead, landing, text beside a horizontal landing") {
    H h;
    h.run({"MLD"});
    REQUIRE(h.out.prompt ==
            "Specify leader arrowhead location or [leader Landing first/Content first/Options] <Options>: ");
    h.run({"0,0"});
    REQUIRE(h.out.prompt == "Specify leader landing location: ");
    h.run({"10,10"});
    REQUIRE(h.out.prompt == "Enter annotation text: ");
    REQUIRE(h.proc.wants_free_text());
    h.run({"SEE DETAIL A"});
    const auto* m = h.last<AddMLeaderCommand>();
    REQUIRE(m != nullptr);
    REQUIRE(m->content == "SEE DETAIL A");
    REQUIRE(m->vertices.size() == 3); // tip, landing, the end of the landing
    REQUIRE(m->vertices[0] == Vec2{0, 0});
    REQUIRE(m->vertices[1] == Vec2{10, 10});
    REQUIRE(m->vertices[2].y == Approx(10.0));
    REQUIRE(m->vertices[2].x > 10.0);
    REQUIRE(m->block.attach == 3); // middle left, to the right of the landing
    REQUIRE(m->block.pos.x > m->vertices[2].x);
    REQUIRE_FALSE(h.proc.has_active_command());

    // Landing first, leftwards: the text to the left.
    h.run({"MLEADER", "L"});
    REQUIRE(h.out.prompt ==
            "Specify leader landing location or [leader arrowHead first/Content first/Options] <Options>: ");
    h.run({"0,0"});
    REQUIRE(h.out.prompt == "Specify leader arrowhead location: ");
    h.run({"10,-5", "NOTE"});
    m = h.last<AddMLeaderCommand>();
    REQUIRE(m->vertices[0] == Vec2{10, -5});
    REQUIRE(m->vertices[1] == Vec2{0, 0});
    REQUIRE(m->block.attach == 5);
    REQUIRE(m->vertices[2].x < 0.0);

    // Content first: the text, then the arrowhead.
    h.run({"MLEADER", "C"});
    h.run({"20,20", "LABEL", "0,0"});
    m = h.last<AddMLeaderCommand>();
    REQUIRE(m->content == "LABEL");
    REQUIRE(m->vertices[0] == Vec2{0, 0});

    // Options: three points, no landing, no text.
    h.run({"MLEADER", "O", "M", "3", "A", "N", "C", "N", "X"});
    REQUIRE(h.out.prompt ==
            "Specify leader arrowhead location or [leader Landing first/Content first/Options] <Options>: ");
    h.run({"0,0"});
    REQUIRE(h.out.prompt == "Specify next point: ");
    h.run({"5,5"});
    REQUIRE(h.out.prompt == "Specify leader landing location: ");
    h.run({"10,5"});
    m = h.last<AddMLeaderCommand>();
    REQUIRE(m->vertices.size() == 3);
    REQUIRE(m->content.empty());
    h.run({"MLEADER", "O", "M", "2", "A", "Y", "C", "M", "X"}); // back to the defaults
    h.proc.cancel();
}

TEST_CASE("LEADER: points until Enter, lines of annotation, Undo and Format") {
    H h;
    h.run({"LEADER"});
    REQUIRE(h.out.prompt == "Specify leader start point: ");
    h.run({"0,0"});
    REQUIRE(h.out.prompt == "Specify next point: ");
    h.run({"10,10"});
    REQUIRE(h.out.prompt == "Specify next point or [Annotation/Format/Undo] <Annotation>: ");
    h.run({""});
    REQUIRE(h.out.prompt == "Enter first line of annotation text or <options>: ");
    h.run({"SEE NOTE", ""});
    const auto* l = h.last<AddLeaderCommand>();
    REQUIRE(l != nullptr);
    REQUIRE(l->tip == Vec2{0, 0});
    REQUIRE(l->knee == Vec2{10, 10});
    REQUIRE(l->content == "SEE NOTE");

    h.run({"LEADER", "0,0", "5,5", "9,9", "U", "10,5", "F"});
    REQUIRE(h.out.prompt == "Enter leader format option [Spline/STraight/Arrow/None] <Exit>: ");
    h.run({"S"});
    REQUIRE(h.out.lines.back() == "Spline leaders are not available yet; the leader stays straight.");
    h.run({"20,5", "A", "FIRST", "SECOND", ""});
    const auto* m = h.last<AddMLeaderCommand>();
    REQUIRE(m != nullptr);
    REQUIRE(m->vertices.size() == 4);
    REQUIRE(m->vertices[2] == Vec2{10, 5});
    REQUIRE(m->content == "FIRST\nSECOND");

    h.run({"LEADER", "0,0", "5,5", "", ""});
    REQUIRE(h.out.prompt == "Enter an annotation option [Tolerance/Copy/Block/None/Mtext] <Mtext>: ");
    h.run({"N"});
    REQUIRE(h.last<AddLeaderCommand>()->content.empty());
    REQUIRE_FALSE(h.proc.has_active_command());
}

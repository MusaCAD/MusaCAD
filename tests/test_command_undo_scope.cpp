// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// A command's own [Undo] (TRIM, EXTEND, LENGTHEN, DIM, DIMCONTINUE, FILLET ...) takes back
// only what the command did: a step that changed nothing leaves no undo group, so the
// Undo must not reach past the command to the work before it.

#include <chrono>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad::core;
using musacad::command::CommandOutput;
using musacad::command::CommandProcessor;

namespace {
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

struct SilentOutput : CommandOutput {
    void append_line(const std::string&) override {}
    void set_prompt(const std::string&) override {}
};
} // namespace

TEST_CASE("UndoLastGroupCommand with `since` undoes only a group made since the command began") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 3}); // before the command
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(UndoLastGroupCommand{5}); // the command (from group 5) made nothing yet
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Nothing to undo."; }));
    REQUIRE(engine.snapshot().line_vertices.size() == 2); // the line before it stays
    engine.submit(AddLineCommand{{0, 5}, {10, 5}, 6});   // a step of the command
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    engine.submit(UndoLastGroupCommand{5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(UndoLastGroupCommand{}); // U / Ctrl+Z: the last group, whatever made it
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    engine.stop();
}

TEST_CASE("TRIM's Undo after a step that trimmed nothing leaves the drawing before TRIM alone") {
    GeometryEngine engine;
    engine.start();
    SilentOutput out;
    CommandProcessor proc{[&](Command c) { engine.submit(std::move(c)); }, nullptr, out};
    proc.submit_line("LINE");
    proc.submit_line("0,0");
    proc.submit_line("10,0");
    proc.submit_line("");
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    proc.submit_line("TRIM");
    proc.submit_line("F"); // a fence over empty space: nothing to trim
    proc.submit_line("50,50");
    proc.submit_line("60,60");
    proc.submit_line("");
    proc.submit_line("U");
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Nothing to undo."; }));
    REQUIRE(engine.snapshot().line_vertices.size() == 2); // the LINE drawn before TRIM stays
    proc.cancel();
    engine.stop();
}

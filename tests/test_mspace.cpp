// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// MSPACE / PSPACE through a viewport (#26): entering edits the model, leaving writes the
// view back into the viewport and restores the sheet; the command flows.

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad::core;

namespace {
struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompts.push_back(p); }
    std::vector<std::string> lines;
    std::vector<std::string> prompts;
};
struct StubView : musacad::command::ViewControl {
    void zoom_extents() override {}
    void zoom_scale(double) override {}
    bool current_view(Vec2& center, double& scale) override {
        center = {30, 40};
        scale = 4.0; // screen px per unit (or per sheet mm)
        return true;
    }
};
struct ProcHarness {
    std::vector<Command> cmds;
    SilentOutput out;
    StubView view;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, &view, out};
    template <class T>
    const T* last() const {
        const T* found = nullptr;
        for (const Command& c : cmds) {
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
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    e.consume_snapshot();
    return pred(e.snapshot());
}
} // namespace

TEST_CASE("#26 MSPACE: enter through a viewport, draw in the model, leave with the view written back") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    engine.submit(SetActiveSpaceCommand{1, std::string()});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.active_space == 1; }));
    engine.submit(CreateViewportCommand{{20, 20}, {120, 80}, false, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.viewport_rects.size() == 1; }));
    CHECK_FALSE(engine.snapshot().mspace.active);

    engine.submit(EnterMspaceCommand{{200, 200}, 1.0, 4.0}); // outside: refused
    engine.submit(EnterMspaceCommand{{70, 50}, 1.0, 4.0});   // inside the viewport
    REQUIRE(wait_until(engine, [](const auto& s) { return s.mspace.active; }));
    {
        const RenderSnapshot& s = engine.snapshot();
        CHECK(s.active_space == 0); // the model is what is drawn and edited
        CHECK(s.mspace.layout == 1);
        CHECK(s.mspace.paper_px_per_mm == 4.0);
        CHECK(s.line_vertices.size() == 2); // the model line, no sheet
        CHECK(s.viewport_rects.empty());
    }
    engine.submit(AddLineCommand{{0, 0}, {0, 50}, 3}); // lands in the model
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    engine.submit(EnterMspaceCommand{{70, 50}, 1.0, 4.0}); // already inside: refused
    // Leave: the camera at (10, 10), 2 px per unit -> the viewport shows (10, 10) at 0.5 mm/unit.
    engine.submit(LeaveMspaceCommand{{10, 10}, 2.0});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.mspace.active && s.active_space == 1; }));
    engine.submit(ClearSelectionCommand{});
    engine.submit(SelectPickCommand{{20, 50}, 1.0, false}); // the viewport's frame
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.selection.size() == 1 && s.selection[0].kind == EntityKind::Viewport;
    }));
    // Both model lines show through it at the new view: (0,0)->(100,0) maps to y = 50 - 5 = 45, x 65..115.
    bool has_line = false;
    for (std::size_t i = 0; i + 1 < engine.snapshot().line_vertices.size(); i += 2) {
        const Vec2 a = engine.snapshot().line_vertices[i];
        const Vec2 b = engine.snapshot().line_vertices[i + 1];
        if (std::abs(a.y - 45.0) < 1e-6 && std::abs(b.y - 45.0) < 1e-6 && std::abs(std::min(a.x, b.x) - 65.0) < 1e-6) {
            has_line = true;
        }
    }
    CHECK(has_line);
    // A leave without an edit in progress is refused; a tab switch drops the edit.
    engine.submit(LeaveMspaceCommand{{0, 0}, 1.0});
    engine.submit(EnterMspaceCommand{{70, 50}, 1.0, 4.0});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.mspace.active; }));
    engine.submit(SetActiveSpaceCommand{0, std::string()});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.mspace.active && s.active_space == 0; }));
    engine.stop();
}

TEST_CASE("#26 commands: MSPACE picks a viewport with the sheet scale; PSPACE and MODEL leave with the camera") {
    ProcHarness h;
    h.proc.set_layouts({"Layout1"}, 0);
    h.proc.submit_line("MSPACE");
    CHECK(h.last<EnterMspaceCommand>() == nullptr); // model space: refused
    h.proc.set_layouts({"Layout1"}, 1);
    h.proc.submit_line("MS");
    h.proc.submit_line("70,50");
    const auto* e = h.last<EnterMspaceCommand>();
    REQUIRE(e != nullptr);
    CHECK(e->pick == Vec2{70, 50});
    CHECK(e->paper_px_per_mm == 4.0); // the stub camera's scale
    h.proc.set_mspace_active(true);
    h.proc.submit_line("MSPACE"); // already inside
    h.proc.submit_line("PSPACE");
    const auto* l = h.last<LeaveMspaceCommand>();
    REQUIRE(l != nullptr);
    CHECK(l->view_center == Vec2{30, 40});
    CHECK(l->px_per_unit == 4.0);
    h.proc.submit_line("MODEL"); // leaves, then switches to model space
    CHECK(h.last<SetActiveSpaceCommand>()->space == 0);
}

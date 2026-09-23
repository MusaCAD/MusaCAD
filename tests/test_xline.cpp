// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// XLINE and RAY (issue #23): construction lines. They are infinite, so the interesting
// invariants are the ones that fall out of that: no finite bounds (excluded from ZOOM
// Extents and the spatial grid), yet still pickable and window-selectable, and clipped
// to the view at render time. Plus the ordinary edit/persist machinery.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/native_kernel_2d.hpp"

using namespace musacad::core;
using Catch::Approx;

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
struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string&) override {}
    void set_prompt(const std::string&) override {}
};
struct ProcHarness {
    std::vector<Command> cmds;
    SilentOutput out;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    template <class T>
    std::vector<const T*> all() const {
        std::vector<const T*> v;
        for (const Command& c : cmds) {
            if (const auto* p = std::get_if<T>(&c)) {
                v.push_back(p);
            }
        }
        return v;
    }
};
} // namespace

TEST_CASE("#23: a construction line has no finite bounds and is excluded from zoom extents") {
    GeometryStore store;
    const EntityHandle h = store.add_xline({10, 20}, {1, 1}, false);
    Vec2 lo;
    Vec2 hi;
    REQUIRE(!entity_aabb(store, h, lo, hi)); // no finite AABB -> not indexed, not in extents

    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {50, 0}, 1}); // a finite line sets the extents
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.consume_snapshot();
    const Vec2 bmax = engine.snapshot().bounds_max;
    engine.submit(AddXlineCommand{{0, 0}, {0, 1}, false, 2, {}}); // a vertical xline
    REQUIRE(wait_until(engine, [](const auto& s) { return s.construction_lines.size() == 1; }));
    engine.consume_snapshot();
    // The xline is published for the renderer but does NOT enlarge the extents.
    REQUIRE(engine.snapshot().bounds_max.x == Approx(bmax.x));
    REQUIRE(engine.snapshot().bounds_max.y == Approx(bmax.y));
    engine.stop();
}

TEST_CASE("#23: a construction line is publishable, pickable and unit-normalised") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddXlineCommand{{0, 0}, {10, 0}, false, 1, {}}); // dir not unit on input
    REQUIRE(wait_until(engine, [](const auto& s) { return s.construction_lines.size() == 1; }));
    engine.consume_snapshot();
    const ConstructionLineView& v = engine.snapshot().construction_lines[0];
    REQUIRE(std::hypot(v.dir.x, v.dir.y) == Approx(1.0)); // stored as a unit vector
    REQUIRE(!v.ray);

    // Pick a point far from the base but on the infinite line -> selects it (it is not
    // in the spatial grid, so this exercises the direct scan).
    engine.submit(SelectPickCommand{{900.0, 0.2}, 1.0, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    REQUIRE(engine.snapshot().selection[0].kind == EntityKind::Xline);
    engine.stop();
}

TEST_CASE("#23: a RAY does not extend behind its start point") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddXlineCommand{{0, 0}, {1, 0}, true, 1, {}}); // ray along +x from origin
    REQUIRE(wait_until(engine, [](const auto& s) { return s.construction_lines.size() == 1; }));
    // A point behind the start (negative x) is NOT on the ray -> no pick. Prove the pick
    // was processed with a fence command, then assert nothing was selected.
    engine.submit(SelectPickCommand{{-500.0, 0.1}, 1.0, false, false});
    engine.submit(AddPointCommand{{123, 456}, 9, {}}); // fence: processed after the pick
    REQUIRE(wait_until(engine, [](const auto& s) { return s.points.size() == 1; }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().selection.empty());
    // A point ahead of the start IS on the ray.
    engine.submit(SelectPickCommand{{500.0, 0.1}, 1.0, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.stop();
}

TEST_CASE("#23: a crossing window catches a construction line that passes through it") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddXlineCommand{{0, 0}, {1, 1}, false, 1, {}}); // the y=x line
    REQUIRE(wait_until(engine, [](const auto& s) { return s.construction_lines.size() == 1; }));
    // A window far up the diagonal still catches it (it is infinite).
    engine.submit(SelectWindowCommand{{490, 490}, {510, 510}, true, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    // A window off the diagonal does not.
    engine.submit(SelectWindowCommand{{490, 0}, {510, 20}, true, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.empty(); }));
    engine.stop();
}

TEST_CASE("#23: a construction line moves and rotates like any object; move is one undo group") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddXlineCommand{{0, 0}, {1, 0}, false, 1, {}}); // horizontal through origin
    REQUIRE(wait_until(engine, [](const auto& s) { return s.construction_lines.size() == 1; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(MoveSelectionCommand{{0, 50}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return !s.construction_lines.empty() && s.construction_lines[0].base.y == Approx(50.0);
    }));
    engine.submit(RotateSelectionCommand{{0, 50}, kPi / 2.0, 3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return !s.construction_lines.empty() &&
               std::abs(s.construction_lines[0].dir.x) < 1e-6; // now vertical
    }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return !s.construction_lines.empty() &&
               std::abs(s.construction_lines[0].dir.y) < 1e-6; // horizontal again
    }));
    engine.stop();
}

TEST_CASE("#23: the kernel's closest point is the perpendicular foot, clamped for a ray") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle x = store.add_xline({0, 0}, {1, 0}, false); // the x-axis
    Vec2 cp;
    REQUIRE(kernel.closest_point(store, x, {30, 40}, cp));
    REQUIRE(cp.x == Approx(30.0));
    REQUIRE(cp.y == Approx(0.0)); // foot of perpendicular

    const EntityHandle r = store.add_xline({0, 0}, {1, 0}, true); // ray along +x
    REQUIRE(kernel.closest_point(store, r, {-30, 40}, cp));
    REQUIRE(cp.x == Approx(0.0)); // clamped to the start, not projected behind it
}

TEST_CASE("#23: XLINE and RAY round-trip through the native format") {
    GeometryStore store;
    store.add_xline({1, 2}, {1, 0}, false);
    store.add_xline({3, 4}, {0, 1}, true);
    io::Document doc = io::document_from_store(store);
    REQUIRE(doc.xlines.size() == 2);
    const std::filesystem::path p =
        std::filesystem::temp_directory_path() / "musacad_xline_rt.musa";
    REQUIRE(io::save_native(doc, p.string()).ok);
    io::Document back;
    REQUIRE(io::load_native(p.string(), back).ok);
    REQUIRE(back.xlines.size() == 2);
    REQUIRE(back.xlines[0].base == Vec2{1, 2});
    REQUIRE(!back.xlines[0].ray);
    REQUIRE(back.xlines[1].ray);
    REQUIRE(back.xlines[1].dir == Vec2{0, 1});
    std::filesystem::remove(p);
}

TEST_CASE("#23: XLINE and RAY round-trip through DXF") {
    GeometryStore store;
    store.add_xline({5, 6}, {1, 0}, false);
    store.add_xline({7, 8}, {0, 1}, true);
    io::Document doc = io::document_from_store(store);
    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_xline.dxf";
    REQUIRE(io::save_dxf(doc, p.string()).ok);
    io::Document back;
    REQUIRE(io::load_dxf(p.string(), back).ok);
    REQUIRE(back.xlines.size() == 2);
    const bool has_ray = back.xlines[0].ray || back.xlines[1].ray;
    const bool has_xline = !back.xlines[0].ray || !back.xlines[1].ray;
    REQUIRE(has_ray);
    REQUIRE(has_xline);
    std::filesystem::remove(p);
}

TEST_CASE("#23: the XLINE command's options each emit the right direction") {
    { // two-point: through the root toward the second point
        ProcHarness h;
        h.proc.submit_line("XL");
        h.proc.submit_line("0,0");
        h.proc.submit_line("10,10");
        h.proc.submit_line(""); // Enter ends the family
        const auto xs = h.all<AddXlineCommand>();
        REQUIRE(xs.size() == 1);
        REQUIRE(xs[0]->base == Vec2{0, 0});
        REQUIRE(std::abs(xs[0]->dir.x - xs[0]->dir.y) < 1e-9); // 45 degrees
        REQUIRE(!xs[0]->ray);
    }
    { // horizontal
        ProcHarness h;
        h.proc.submit_line("XLINE");
        h.proc.submit_line("H");
        h.proc.submit_line("5,7");
        h.proc.submit_line("");
        const auto xs = h.all<AddXlineCommand>();
        REQUIRE(xs.size() == 1);
        REQUIRE(xs[0]->base == Vec2{5, 7});
        REQUIRE(xs[0]->dir.y == Approx(0.0));
    }
    { // vertical
        ProcHarness h;
        h.proc.submit_line("XLINE");
        h.proc.submit_line("V");
        h.proc.submit_line("5,7");
        h.proc.submit_line("");
        REQUIRE(h.all<AddXlineCommand>()[0]->dir.x == Approx(0.0));
    }
    { // angle
        ProcHarness h;
        h.proc.submit_line("XLINE");
        h.proc.submit_line("A");
        h.proc.submit_line("90");
        h.proc.submit_line("2,3");
        h.proc.submit_line("");
        const auto xs = h.all<AddXlineCommand>();
        REQUIRE(xs[0]->base == Vec2{2, 3});
        REQUIRE(xs[0]->dir.x == Approx(0.0).margin(1e-9));
        REQUIRE(xs[0]->dir.y == Approx(1.0));
    }
    { // bisect: vertex at origin, between +x and +y -> 45 degrees
        ProcHarness h;
        h.proc.submit_line("XLINE");
        h.proc.submit_line("B");
        h.proc.submit_line("0,0");
        h.proc.submit_line("10,0");
        h.proc.submit_line("0,10");
        const auto xs = h.all<AddXlineCommand>();
        REQUIRE(xs.size() == 1);
        REQUIRE(xs[0]->base == Vec2{0, 0});
        REQUIRE(std::abs(xs[0]->dir.x - xs[0]->dir.y) < 1e-9);
    }
}

TEST_CASE("#23: XLINE repeats through one root; RAY is semi-infinite from its start") {
    { // one root, several through points -> several xlines sharing the base
        ProcHarness h;
        h.proc.submit_line("XLINE");
        h.proc.submit_line("0,0");
        h.proc.submit_line("1,0");
        h.proc.submit_line("0,1");
        h.proc.submit_line("");
        const auto xs = h.all<AddXlineCommand>();
        REQUIRE(xs.size() == 2);
        REQUIRE(xs[0]->base == Vec2{0, 0});
        REQUIRE(xs[1]->base == Vec2{0, 0});
        REQUIRE(!xs[0]->ray);
    }
    { // RAY
        ProcHarness h;
        h.proc.submit_line("RAY");
        h.proc.submit_line("0,0");
        h.proc.submit_line("5,0");
        h.proc.submit_line("");
        const auto xs = h.all<AddXlineCommand>();
        REQUIRE(xs.size() == 1);
        REQUIRE(xs[0]->ray);
        REQUIRE(xs[0]->dir.x == Approx(1.0));
    }
}

namespace {
struct XlPromptOutput : musacad::command::CommandOutput {
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompts.push_back(p); }
    std::vector<std::string> lines;
    std::vector<std::string> prompts;
};
struct XlPromptHarness {
    std::vector<Command> cmds;
    XlPromptOutput out;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
};
} // namespace

TEST_CASE("#39: XLINE Offset and Ang Reference go to the engine; Bisect repeats; the bands exist") {
    XlPromptHarness h;
    h.proc.set_pick_radius(1.0);
    h.proc.submit_line("XL");
    REQUIRE(h.out.prompts.back() == "Specify a point or [Hor/Ver/Ang/Bisect/Offset]: ");
    h.proc.submit_line("O");
    REQUIRE(h.out.prompts.back() == "Specify offset distance or [Through] <Through>: ");
    h.proc.submit_line("5");
    REQUIRE(h.out.prompts.back() == "Select a line object: ");
    h.proc.submit_line("5,0");
    REQUIRE(h.out.prompts.back() == "Specify side to offset: ");
    h.proc.submit_line("5,9");
    std::optional<XlineOffsetCommand> off; // a copy: later commands reallocate the vector
    for (const auto& c : h.cmds) {
        if (const auto* p = std::get_if<XlineOffsetCommand>(&c)) {
            off = *p;
        }
    }
    REQUIRE(off.has_value());
    REQUIRE(off->distance == Approx(5.0));
    REQUIRE(!off->through);
    REQUIRE(h.out.prompts.back() == "Select a line object: "); // repeats
    h.proc.submit_line("");
    REQUIRE(!h.proc.has_active_command());

    h.proc.submit_line("XLINE");
    h.proc.submit_line("A");
    REQUIRE(h.out.prompts.back() == "Enter angle of xline (0) or [Reference]: ");
    h.proc.submit_line("R");
    h.proc.submit_line("5,0");
    REQUIRE(h.out.prompts.back() == "Enter angle of xline <0>: ");
    h.proc.submit_line("30");
    h.proc.submit_line("20,20");
    std::optional<XlineReferenceCommand> ref;
    for (const auto& c : h.cmds) {
        if (const auto* p = std::get_if<XlineReferenceCommand>(&c)) {
            ref = *p;
        }
    }
    REQUIRE(ref.has_value());
    REQUIRE(ref->angle == Approx(to_radians(30.0)));
    REQUIRE(ref->base.x == Approx(20.0));
    h.proc.submit_line("");

    h.proc.submit_line("XL");
    h.proc.submit_line("0,0");
    REQUIRE(h.proc.preview().kind == musacad::command::PreviewKind::Xline);
    REQUIRE(h.proc.preview().xline_mode == 0);
    h.proc.cancel();
    h.proc.submit_line("XL");
    h.proc.submit_line("B");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,0");
    REQUIRE(h.proc.preview().xline_mode == 4);
    h.proc.submit_line("0,10");
    h.proc.submit_line("0,-10"); // a second bisector from the same vertex and start
    int xlines = 0;
    for (const auto& c : h.cmds) {
        if (std::get_if<AddXlineCommand>(&c) != nullptr) {
            ++xlines;
        }
    }
    REQUIRE(xlines >= 2);
    h.proc.submit_line("");
    REQUIRE(!h.proc.has_active_command());

    // The engine: the offset xline and the reference-angle xline.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(*off);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.construction_lines.size() == 1; }));
    REQUIRE(engine.snapshot().construction_lines[0].base.y == Approx(5.0));
    REQUIRE(std::abs(engine.snapshot().construction_lines[0].dir.y) < 1e-9);
    engine.submit(*ref);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.construction_lines.size() == 2; }));
    REQUIRE(std::atan2(engine.snapshot().construction_lines[1].dir.y, engine.snapshot().construction_lines[1].dir.x) == Approx(to_radians(30.0)));
    engine.stop();
}

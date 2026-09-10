// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// MVIEW viewports (#26): a paper-space window onto model space -- the derived view
// (mapped into the sheet and clipped to the frame), MVIEW's operations, and the file forms.

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

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
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/scene_snapshot.hpp"

using namespace musacad::core;
using namespace musacad::core::io;

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
int segments_near(const RenderSnapshot& s, Vec2 a, Vec2 b, double tol = 1e-6) {
    int n = 0;
    for (std::size_t i = 0; i + 1 < s.line_vertices.size(); i += 2) {
        const Vec2 p = s.line_vertices[i];
        const Vec2 q = s.line_vertices[i + 1];
        const bool fwd = std::abs(p.x - a.x) < tol && std::abs(p.y - a.y) < tol && std::abs(q.x - b.x) < tol && std::abs(q.y - b.y) < tol;
        const bool rev = std::abs(p.x - b.x) < tol && std::abs(p.y - b.y) < tol && std::abs(q.x - a.x) < tol && std::abs(q.y - a.y) < tol;
        n += (fwd || rev) ? 1 : 0;
    }
    return n;
}
} // namespace

TEST_CASE("#26 viewport: the model seen through it is mapped into the sheet and clipped to the frame") {
    GeometryStore store;
    NativeKernel2D kernel;
    // Model: a 100-unit line and a line that runs past the view.
    store.add_line({0, 0}, {100, 0});
    store.add_line({-1000, 50}, {1000, 50});
    // Layout1: a 50 x 40 mm viewport at (100, 100) showing the model around (50, 25) at 0.5 mm/unit.
    EntityProps paper;
    paper.set_space(1);
    const EntityHandle vp = store.add_viewport({100, 100}, 50, 40, {50, 25}, 0.5, true, paper);
    store.set_active_space(1);
    RenderSnapshot snap;
    build_render_snapshot(store, kernel, snap, 0.01, 1.0);
    // The frame (4 segments) + the first line mapped: (0,0)->(75, 87.5), (100,0)->(125, 87.5).
    CHECK(segments_near(snap, {75, 80}, {125, 80}) == 1);   // frame bottom
    CHECK(segments_near(snap, {75, 120}, {125, 120}) == 1); // frame top
    CHECK(segments_near(snap, {75, 87.5}, {125, 87.5}) == 1);
    // The long line is clipped to the frame's x range at y = 100 + (50-25)*0.5 = 112.5.
    CHECK(segments_near(snap, {75, 112.5}, {125, 112.5}) == 1);
    CHECK(snap.line_vertices.size() == 12);
    // Off: only the frame.
    Vec2 lo;
    Vec2 hi;
    CHECK(entity_aabb(store, vp, lo, hi));
    CHECK(lo.x == 75.0);
    CHECK(hi.y == 120.0);
    Vec2 cp;
    CHECK(kernel.closest_point(store, vp, {75.5, 100}, cp)); // picked by the frame
    CHECK(std::abs(cp.x - 75.0) < 1e-9);

    // Model space itself is untouched by the viewport.
    store.set_active_space(0);
    RenderSnapshot model;
    build_render_snapshot(store, kernel, model, 0.01, 1.0);
    CHECK(model.line_vertices.size() == 4);
}

TEST_CASE("#26 MVIEW: Fit and corners create fitted viewports on a layout; ON/OFF, Scale, Center; undo") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {200, 0}, 1});
    engine.submit(AddLineCommand{{0, 0}, {0, 100}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    engine.submit(CreateViewportCommand{{}, {}, true, 2}); // refused in model space
    engine.submit(SetActiveSpaceCommand{1, std::string()});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.active_space == 1 && s.line_vertices.empty(); }));
    engine.submit(CreateViewportCommand{{20, 20}, {120, 70}, false, 3});
    // Frame (4) + the two model lines mapped (2) = 6 segments.
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 12; }));
    // Fitted: the 200 x 100 model into 100 x 50 mm at 95 % -> scale 0.475, centred at (70, 45).
    CHECK(segments_near(engine.snapshot(), {70 - 47.5, 45 - 23.75}, {70 + 47.5, 45 - 23.75}, 1e-6) == 1);
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1 && s.grips.size() == 5; }));

    engine.submit(SetViewportViewCommand{{20, 45}, 1.0, 0, 0.0, std::nullopt, 4}); // OFF: frame only
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));
    engine.submit(SetViewportViewCommand{{20, 45}, 1.0, 1, 0.0, std::nullopt, 5}); // ON
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 12; }));
    engine.submit(SetViewportViewCommand{{20, 45}, 1.0, -1, 1.0, Vec2{0, 0}, 6}); // 1:1, centred at the origin
    // At 1:1 centred on the model origin only the parts of the axes inside the frame show:
    // x from 70 to 120 (50 mm), y from 45 to 70.
    REQUIRE(wait_until(engine, [](const auto& s) {
        return segments_near(s, {70, 45}, {120, 45}) == 1 && segments_near(s, {70, 45}, {70, 70}) == 1;
    }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return segments_near(s, {70 - 47.5, 45 - 23.75}, {70 + 47.5, 45 - 23.75}, 1e-6) == 1;
    }));
    engine.submit(CreateViewportCommand{{}, {}, true, 7}); // Fit: the sheet inside a 10 mm margin
    REQUIRE(wait_until(engine, [](const auto& s) { return segments_near(s, {10, 10}, {287, 10}) == 1; }));
    engine.stop();
}

TEST_CASE("#26 native v31 and DXF VIEWPORT carry a viewport both ways") {
    Document doc;
    DocViewport v;
    v.center = {150, 100};
    v.width = 200;
    v.height = 120;
    v.view_center = {30, 40};
    v.scale = 0.25;
    v.on = false;
    v.props.set_space(1);
    doc.viewports.push_back(v);
    Document rt;
    REQUIRE(parse_native(serialize_native(doc), rt).ok);
    REQUIRE(rt.viewports.size() == 1);
    CHECK(rt.viewports[0] == v);
    GeometryStore store;
    populate_store(store, rt);
    CHECK(store.viewports().live_count() == 1);
    CHECK(document_from_store(store).viewports[0] == v);

    const std::string dxf = serialize_dxf(doc);
    CHECK(dxf.find("\nVIEWPORT\n") != std::string::npos);
    Document from_dxf;
    REQUIRE(parse_dxf(dxf, from_dxf).ok);
    REQUIRE(from_dxf.viewports.size() == 1);
    CHECK(from_dxf.viewports[0].center == v.center);
    CHECK(from_dxf.viewports[0].width == 200.0);
    CHECK(std::abs(from_dxf.viewports[0].scale - 0.25) < 1e-9);
    CHECK_FALSE(from_dxf.viewports[0].on);
    CHECK(from_dxf.viewports[0].props.space() == 1);
}

TEST_CASE("#26 commands: MVIEW corners / Fit / ON / Scale / Center; MSPACE says what it cannot do") {
    ProcHarness h;
    h.proc.set_layouts({"Layout1"}, 0);
    h.proc.submit_line("MVIEW"); // model space: refused
    CHECK(h.last<CreateViewportCommand>() == nullptr);
    h.proc.set_layouts({"Layout1"}, 1);
    h.proc.submit_line("MVIEW");
    h.proc.submit_line("10,10");
    h.proc.submit_line("110,80");
    const auto* c = h.last<CreateViewportCommand>();
    REQUIRE(c != nullptr);
    CHECK(c->a == Vec2{10, 10});
    CHECK(c->b == Vec2{110, 80});
    CHECK_FALSE(c->fit_sheet);
    h.proc.submit_line("MV");
    h.proc.submit_line("");
    CHECK(h.last<CreateViewportCommand>()->fit_sheet);
    h.proc.submit_line("MVIEW");
    h.proc.submit_line("OFF");
    h.proc.submit_line("50,50");
    const auto* off = h.last<SetViewportViewCommand>();
    REQUIRE(off != nullptr);
    CHECK(off->on == 0);
    h.proc.submit_line("MVIEW");
    h.proc.submit_line("S");
    h.proc.submit_line("50,50");
    h.proc.submit_line("0.5");
    CHECK(h.last<SetViewportViewCommand>()->scale == 0.5);
    h.proc.submit_line("MVIEW");
    h.proc.submit_line("C");
    h.proc.submit_line("50,50");
    h.proc.submit_line("300,200");
    REQUIRE(h.last<SetViewportViewCommand>()->view_center.has_value());
    CHECK(*h.last<SetViewportViewCommand>()->view_center == Vec2{300, 200});
    h.proc.submit_line("MSPACE");
    CHECK(h.out.lines.back().find("not available") != std::string::npos);
}

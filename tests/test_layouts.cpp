// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Layouts / paper space (#26): the layout table, per-entity space, what the active space
// shows and lets you pick, LAYOUT's operations, and the native / DXF forms.

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
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
} // namespace

TEST_CASE("#26 EntityProps::space rides in the flags; the store's layout table has stable ids") {
    EntityProps p;
    CHECK(p.space() == 0);
    p.set_space(3);
    CHECK(p.space() == 3);
    CHECK(p.color_by_layer()); // the ByLayer bits are untouched
    p.set_color_by_layer(false);
    CHECK(p.space() == 3);

    GeometryStore s;
    REQUIRE(s.layouts().size() == 2); // Layout1, Layout2 as a new drawing has
    CHECK(s.layouts()[0].id == 1);
    CHECK(s.layouts()[1].name == "Layout2");
    CHECK(s.add_layout("Sheet A") == 3);
    CHECK(s.remove_layout(2));
    CHECK(s.add_layout("Sheet B") == 2); // the lowest free id
    CHECK(s.layout_by_id(2)->name == "Sheet B");
    s.set_active_space(3);
    CHECK(s.active_space() == 3);
    s.set_active_space(9); // unknown -> model
    CHECK(s.active_space() == 0);
    s.set_active_space(2);
    CHECK(s.remove_layout(2));
    CHECK(s.active_space() == 0); // deleting the active layout drops to model
}

TEST_CASE("#26 only the active space is drawn, picked and edited; a new object lands in the active space") {
    GeometryStore store;
    NativeKernel2D kernel;
    store.add_line({0, 0}, {10, 0});
    EntityProps paper;
    paper.set_space(1);
    store.add_line({0, 0}, {50, 0}, paper);
    RenderSnapshot snap;
    build_render_snapshot(store, kernel, snap, 0.01, 1.0);
    CHECK(snap.line_vertices.size() == 2);
    CHECK(snap.active_space == 0);
    CHECK(snap.layouts.size() == 2);
    store.set_active_space(1);
    RenderSnapshot paper_snap;
    build_render_snapshot(store, kernel, paper_snap, 0.01, 1.0);
    CHECK(paper_snap.line_vertices.size() == 2);
    CHECK(paper_snap.line_vertices[1].x == 50.0);
    CHECK(paper_snap.active_space == 1);

    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SetActiveSpaceCommand{1, std::string()});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.active_space == 1 && s.line_vertices.empty(); }));
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 2}); // drawn in the layout
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    // The model-space line is not pickable from the layout.
    engine.submit(SelectPickCommand{{5, 0}, 1.0, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    CHECK(engine.snapshot().line_vertices[1].x == 100.0);
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(SetActiveSpaceCommand{0, "model"}); // by name too (case-insensitive)
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.active_space == 0 && s.line_vertices.size() == 2 && s.selection.empty();
    }));
    CHECK(engine.snapshot().line_vertices[1].x == 10.0);
    engine.submit(SetActiveSpaceCommand{0, "layout2"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.active_space == 2; }));
    engine.submit(SetActiveSpaceCommand{0xFF, std::string()}); // PSPACE: the first layout
    REQUIRE(wait_until(engine, [](const auto& s) { return s.active_space == 1; }));
    engine.stop();
}

TEST_CASE("#26 LAYOUT New / Rename / Copy (objects included) / Delete (empty only)") {
    GeometryEngine engine;
    engine.start();
    engine.submit(LayoutCommand{LayoutCommand::Op::New, "Sheet 1", std::string(), 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layouts.size() == 3; }));
    CHECK(engine.snapshot().layouts[2].name == "Sheet 1");
    CHECK(engine.snapshot().layouts[2].id == 3);
    engine.submit(LayoutCommand{LayoutCommand::Op::New, "Layout1", std::string(), 2}); // taken
    engine.submit(LayoutCommand{LayoutCommand::Op::Rename, "Sheet 1", "Cover", 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layouts.size() == 3 && s.layouts[2].name == "Cover"; }));
    // Draw on Cover, copy it: the copy has the object too.
    engine.submit(SetActiveSpaceCommand{3, std::string()});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.active_space == 3; }));
    engine.submit(AddLineCommand{{0, 0}, {20, 0}, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(LayoutCommand{LayoutCommand::Op::Copy, "Cover", "Cover (2)", 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layouts.size() == 4; }));
    engine.submit(SetActiveSpaceCommand{0, "Cover (2)"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.active_space == 4 && s.line_vertices.size() == 2; }));
    // Delete refuses a sheet with objects; the empty Layout2 goes.
    engine.submit(LayoutCommand{LayoutCommand::Op::Delete, "Cover", std::string(), 6});
    engine.submit(LayoutCommand{LayoutCommand::Op::Delete, "Layout2", std::string(), 7});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layouts.size() == 3; }));
    bool has_cover = false;
    for (const LayoutInfo& l : engine.snapshot().layouts) {
        has_cover = has_cover || l.name == "Cover";
    }
    CHECK(has_cover);
    // Undo the copy's objects: the copied line goes, the layout stays.
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    CHECK(engine.snapshot().layouts.size() == 3);
    engine.stop();
}

TEST_CASE("#26 native v30 and DXF carry the layouts, the active space and each entity's space") {
    Document doc;
    Layout l;
    l.id = 4;
    l.name = "Sheet A";
    l.page.paper = "ISO A3";
    l.page.paper_w_mm = 420.0;
    l.page.paper_h_mm = 297.0;
    l.page.landscape = false;
    doc.layouts.push_back(l);
    doc.active_space = 4;
    DocLine model_line{{0, 0}, {10, 0}};
    doc.lines.push_back(model_line);
    DocLine paper_line{{0, 0}, {50, 0}};
    paper_line.props.set_space(4);
    doc.lines.push_back(paper_line);

    Document rt;
    REQUIRE(parse_native(serialize_native(doc), rt).ok);
    CHECK(rt.layouts == doc.layouts);
    CHECK(rt.active_space == 4);
    REQUIRE(rt.lines.size() == 2);
    CHECK(rt.lines[1].props.space() == 4);
    CHECK(rt.lines[0].props.space() == 0);

    GeometryStore store;
    populate_store(store, rt);
    CHECK(store.active_space() == 4);
    REQUIRE(store.layouts().size() == 1);
    CHECK(store.layouts()[0].name == "Sheet A");
    const Document again = document_from_store(store);
    CHECK(again.layouts == doc.layouts);
    CHECK(again.active_space == 4);

    // DXF: paper-space entities carry 67 = 1 and come back in the first layout.
    const std::string dxf = serialize_dxf(doc);
    CHECK(dxf.find("\n67\n1\n") != std::string::npos);
    Document from_dxf;
    REQUIRE(parse_dxf(dxf, from_dxf).ok);
    REQUIRE(from_dxf.lines.size() == 2);
    CHECK(from_dxf.lines[0].props.space() == 0);
    CHECK(from_dxf.lines[1].props.space() == 1);
}

TEST_CASE("#26 commands: LAYOUT options, MODEL, PSPACE") {
    ProcHarness h;
    h.proc.set_layouts({"Layout1", "Cover"}, 0);
    h.proc.submit_line("LAYOUT");
    h.proc.submit_line("?");
    CHECK(h.out.lines.back() == "Layouts: Model, Layout1, Cover");
    h.proc.submit_line("N");
    h.proc.submit_line("Sheet 3");
    const auto* n = h.last<LayoutCommand>();
    REQUIRE(n != nullptr);
    CHECK(n->op == LayoutCommand::Op::New);
    CHECK(n->name == "Sheet 3");

    h.proc.submit_line("LO");
    h.proc.submit_line("R");
    h.proc.submit_line("Cover");
    h.proc.submit_line("Title");
    const auto* r = h.last<LayoutCommand>();
    REQUIRE(r != nullptr);
    CHECK(r->op == LayoutCommand::Op::Rename);
    CHECK(r->name == "Cover");
    CHECK(r->new_name == "Title");

    h.proc.submit_line("LAYOUT");
    h.proc.submit_line("C");
    h.proc.submit_line("Layout1");
    h.proc.submit_line("Layout1 copy");
    CHECK(h.last<LayoutCommand>()->op == LayoutCommand::Op::Copy);
    h.proc.submit_line("LAYOUT");
    h.proc.submit_line("D");
    h.proc.submit_line("Title");
    CHECK(h.last<LayoutCommand>()->op == LayoutCommand::Op::Delete);

    h.proc.submit_line("LAYOUT");
    h.proc.submit_line(""); // Set
    h.proc.submit_line("cover");
    const auto* s = h.last<SetActiveSpaceCommand>();
    REQUIRE(s != nullptr);
    CHECK(s->name == "cover");
    h.proc.submit_line("LAYOUT");
    h.proc.submit_line("S");
    h.proc.submit_line("Model");
    CHECK(h.last<SetActiveSpaceCommand>()->space == 0);
    CHECK(h.last<SetActiveSpaceCommand>()->name.empty());

    h.proc.submit_line("PSPACE");
    CHECK(h.last<SetActiveSpaceCommand>()->space == 0xFF);
    h.proc.set_layouts({"Layout1"}, 1);
    h.proc.submit_line("MODEL");
    CHECK(h.last<SetActiveSpaceCommand>()->space == 0);
}

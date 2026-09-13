// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// VPLAYER (#26): layers frozen per viewport -- the model seen through a viewport hides
// them (and MSPACE through it does too) while other viewports and model space itself do
// not; the command's Freeze / Thaw / Reset / Newfrz / Vpvisdflt / ? flows; the native and
// DXF forms; layer removal keeping the lists straight; AUDIT dropping a stale index.
#include <algorithm>
#include <chrono>
#include <cmath>
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
    return false;
}

bool has(const std::vector<std::uint16_t>& v, std::uint16_t x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

Layer named(const std::string& name) {
    Layer l;
    l.name = name;
    return l;
}

} // namespace

TEST_CASE("VPLAYER snapshot: a layer frozen in one viewport is hidden there only") {
    GeometryStore store;
    NativeKernel2D kernel;
    const std::uint16_t walls = store.add_layer(named("walls"));
    store.add_line({0, 0}, {100, 0});
    EntityProps on_walls;
    on_walls.layer = walls;
    store.add_line({0, 0}, {0, 100}, on_walls);
    EntityProps paper;
    paper.set_space(1);
    // Two viewports of the same model; the first freezes "walls".
    const EntityHandle va = store.add_viewport({100, 100}, 50, 40, {50, 50}, 0.3, true, paper, {walls});
    store.add_viewport({200, 100}, 50, 40, {50, 50}, 0.3, true, paper);
    store.set_active_space(1);
    RenderSnapshot sheet;
    build_render_snapshot(store, kernel, sheet, 0.01, 1.0);
    // Frames 2 x 4, the first viewport shows 1 line, the second 2.
    CHECK(sheet.line_vertices.size() == (8 + 1 + 2) * 2);

    // MSPACE through the first viewport: the model build hides "walls" too.
    store.set_mspace(va, 1, 1.0);
    store.set_active_space(0);
    RenderSnapshot through;
    build_render_snapshot(store, kernel, through, 0.01, 1.0);
    CHECK(through.line_vertices.size() == 2);
    // Back in plain model space every layer shows.
    store.clear_mspace();
    RenderSnapshot model;
    build_render_snapshot(store, kernel, model, 0.01, 1.0);
    CHECK(model.line_vertices.size() == 4);
}

TEST_CASE("VPLAYER engine: Freeze / Thaw / Reset / Vpvisdflt / Newfrz / ? on the current, all or a picked viewport") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLayerCommand{named("walls")});
    engine.submit(SetCurrentLayerCommand{1});
    engine.submit(AddLineCommand{{0, 0}, {200, 0}, 1});
    engine.submit(SetCurrentLayerCommand{0});
    engine.submit(AddLineCommand{{0, 0}, {0, 100}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4 && s.layers.size() == 2; }));
    engine.submit(SetActiveSpaceCommand{1, std::string()});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.active_space == 1; }));
    engine.submit(CreateViewportCommand{{20, 20}, {120, 70}, false, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 12; }));

    // Into the viewport: both lines, nothing frozen yet.
    engine.submit(EnterMspaceCommand{{70, 45}, 1.0, 1.0});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.mspace.active && s.line_vertices.size() == 4; }));

    SetViewportLayerFreezeCommand c;
    c.op = SetViewportLayerFreezeCommand::Op::Freeze;
    c.target = SetViewportLayerFreezeCommand::Target::Current;
    c.layer_names = {"WALLS"}; // case-insensitive
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    CHECK(has(engine.snapshot().mspace.frozen_layers, 1));
    CHECK(engine.snapshot().status.find("froze walls") != std::string::npos);

    c.op = SetViewportLayerFreezeCommand::Op::Thaw;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    CHECK(engine.snapshot().mspace.frozen_layers.empty());

    // The layers of the selection, in every viewport.
    engine.submit(SelectPickCommand{{100, 0}, 1.0, true, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    SetViewportLayerFreezeCommand sel;
    sel.op = SetViewportLayerFreezeCommand::Op::Freeze;
    sel.target = SetViewportLayerFreezeCommand::Target::All;
    sel.from_selection = true;
    engine.submit(sel);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    // Reset: back to the layer defaults (none frozen by default yet).
    SetViewportLayerFreezeCommand reset;
    reset.op = SetViewportLayerFreezeCommand::Op::Reset;
    engine.submit(reset);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));

    // Vpvisdflt: "walls" frozen by default -> Reset now freezes it.
    SetViewportLayerFreezeCommand dflt;
    dflt.op = SetViewportLayerFreezeCommand::Op::VisDefault;
    dflt.layer_names = {"walls"};
    dflt.value = true;
    engine.submit(dflt);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layers.size() == 2 && s.layers[1].vp_freeze_new; }));
    engine.submit(reset);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    dflt.value = false;
    engine.submit(dflt);
    engine.submit(reset);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4 && !s.layers[1].vp_freeze_new; }));

    // Newfrz: a new layer, frozen in every viewport and by default in new ones.
    SetViewportLayerFreezeCommand nf;
    nf.op = SetViewportLayerFreezeCommand::Op::Newfrz;
    nf.layer_names = {"temp"};
    engine.submit(nf);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.layers.size() == 3 && s.layers[2].vp_freeze_new && has(s.mspace.frozen_layers, 2);
    }));

    // ?: the report names the viewport and its frozen layers.
    SetViewportLayerFreezeCommand list;
    list.op = SetViewportLayerFreezeCommand::Op::List;
    engine.submit(list);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("temp") != std::string::npos; }));
    CHECK(engine.snapshot().status.find("viewport 1") != std::string::npos);

    // Back on the sheet: pick the viewport to freeze "walls" in it -> frame + 1 line.
    engine.submit(LeaveMspaceCommand{{100, 50}, 0.4}); // 0.4 mm/unit: both lines inside the frame
    REQUIRE(wait_until(engine, [](const auto& s) { return s.active_space == 1 && s.line_vertices.size() == 12; }));
    SetViewportLayerFreezeCommand pick;
    pick.op = SetViewportLayerFreezeCommand::Op::Freeze;
    pick.target = SetViewportLayerFreezeCommand::Target::Pick;
    pick.pick = {70, 45};
    pick.pick_radius = 1.0;
    pick.layer_names = {"walls"};
    engine.submit(pick);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 10; }));

    // Current with no current viewport: refused with a pointer to MSPACE.
    c.op = SetViewportLayerFreezeCommand::Op::Thaw;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("no current viewport") != std::string::npos; }));
    CHECK(engine.snapshot().line_vertices.size() == 10);

    // A new viewport starts with the default-frozen layers ("temp").
    engine.submit(CreateViewportCommand{{20, 100}, {120, 150}, false, 9});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 10 + 8 + 4; }));
    engine.submit(EnterMspaceCommand{{70, 125}, 1.0, 1.0});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.mspace.active; }));
    CHECK(engine.snapshot().mspace.frozen_layers == std::vector<std::uint16_t>{2});
    engine.stop();
}

TEST_CASE("VPLAYER command: the prompts collect layers, a selection, and the target viewports") {
    using Op = SetViewportLayerFreezeCommand::Op;
    using Target = SetViewportLayerFreezeCommand::Target;
    ProcHarness h;
    h.proc.submit_line("VPLAYER");
    REQUIRE(h.out.prompts.back().find("[?/Freeze/Thaw/Reset/Newfrz/Vpvisdflt]") != std::string::npos);
    h.proc.submit_line("F");
    REQUIRE(h.out.prompts.back().find("freeze or <select objects>") != std::string::npos);
    h.proc.submit_line("walls, doors");
    REQUIRE(h.out.prompts.back().find("[All/Select/Current] <Current>") != std::string::npos);
    h.proc.submit_line("");
    const auto* f = h.last<SetViewportLayerFreezeCommand>();
    REQUIRE(f != nullptr);
    CHECK(f->op == Op::Freeze);
    CHECK(f->target == Target::Current);
    CHECK(f->layer_names == std::vector<std::string>{"walls", "doors"});
    CHECK(!f->from_selection);
    CHECK(h.proc.has_active_command()); // back at the option prompt

    // Thaw by selecting objects, in every viewport.
    h.proc.submit_line("T");
    h.proc.submit_line(""); // <select objects>
    CHECK(h.proc.in_selection_phase());
    h.proc.set_selection_count(1);
    h.proc.submit_line("");
    h.proc.submit_line("A");
    const auto* t = h.last<SetViewportLayerFreezeCommand>();
    REQUIRE(t != nullptr);
    CHECK(t->op == Op::Thaw);
    CHECK(t->target == Target::All);
    CHECK(t->from_selection);
    CHECK(t->layer_names.empty());

    // Select: a picked viewport.
    h.proc.submit_line("R");
    h.proc.submit_line("S");
    REQUIRE(h.out.prompts.back().find("Select viewport") != std::string::npos);
    h.proc.submit_line("70,45");
    const auto* r = h.last<SetViewportLayerFreezeCommand>();
    REQUIRE(r != nullptr);
    CHECK(r->op == Op::Reset);
    CHECK(r->target == Target::Pick);
    CHECK(r->pick.x == 70.0);

    h.proc.submit_line("N");
    h.proc.submit_line("temp scratch");
    const auto* n = h.last<SetViewportLayerFreezeCommand>();
    REQUIRE(n != nullptr);
    CHECK(n->op == Op::Newfrz);
    CHECK(n->layer_names == std::vector<std::string>{"temp", "scratch"});

    h.proc.submit_line("V");
    h.proc.submit_line("walls");
    REQUIRE(h.out.prompts.back().find("[Frozen/Thawed] <Thawed>") != std::string::npos);
    h.proc.submit_line("F");
    const auto* v = h.last<SetViewportLayerFreezeCommand>();
    REQUIRE(v != nullptr);
    CHECK(v->op == Op::VisDefault);
    CHECK(v->value);

    h.proc.submit_line("?");
    CHECK(h.last<SetViewportLayerFreezeCommand>()->op == Op::List);
    h.proc.submit_line(""); // Enter at the option prompt ends the command
    CHECK(!h.proc.has_active_command());
}

TEST_CASE("VPLAYER files: frozen-per-viewport lists and the new-viewport default round-trip natively and through DXF") {
    Document doc;
    doc.layers.push_back(named("walls"));
    doc.layers.back().vp_freeze_new = true;
    doc.layers.push_back(named("doors"));
    DocViewport v;
    v.center = {100, 100};
    v.width = 50;
    v.height = 40;
    v.view_center = {10, 10};
    v.scale = 0.5;
    v.props.set_space(1);
    v.frozen_layers = {1, 2};
    doc.viewports.push_back(v);
    doc.viewports.push_back(DocViewport{}); // nothing frozen

    const std::string text = serialize_native(doc);
    CHECK(text.find("VPFREEZE 2 1 2\n") != std::string::npos);
    CHECK(text.find("VPFRZNEW 1\n") != std::string::npos);
    Document rt;
    REQUIRE(parse_native(text, rt).ok);
    REQUIRE(rt.viewports.size() == 2);
    CHECK(rt.viewports[0].frozen_layers == std::vector<std::uint16_t>{1, 2});
    CHECK(rt.viewports[1].frozen_layers.empty());
    CHECK(rt.layers[1].vp_freeze_new);
    CHECK(!rt.layers[2].vp_freeze_new);

    const std::string dxf = serialize_dxf(doc);
    CHECK(dxf.find("\n331\n") != std::string::npos); // frozen-layer handle on the VIEWPORT
    Document from_dxf;
    REQUIRE(parse_dxf(dxf, from_dxf).ok);
    REQUIRE(from_dxf.viewports.size() == 2);
    CHECK(from_dxf.viewports[0].frozen_layers == std::vector<std::uint16_t>{1, 2});
    CHECK(from_dxf.viewports[1].frozen_layers.empty());
    CHECK(from_dxf.layers[1].vp_freeze_new);  // LAYER 70 bit 2
    CHECK(!from_dxf.layers[2].vp_freeze_new);

    // Through the store and back: the lists survive populate/document.
    GeometryStore store;
    populate_store(store, rt);
    Document again = document_from_store(store);
    REQUIRE(again.viewports.size() == 2);
    CHECK(again.viewports[0].frozen_layers == std::vector<std::uint16_t>{1, 2});
    CHECK(again.layers[1].vp_freeze_new);
}

TEST_CASE("VPLAYER store: removing a layer keeps every viewport's frozen list pointing at the same layers") {
    GeometryStore store;
    store.add_layer(named("a")); // 1
    store.add_layer(named("b")); // 2
    store.add_layer(named("c")); // 3
    EntityProps paper;
    paper.set_space(1);
    paper.layer = 3;
    const EntityHandle vp = store.add_viewport({0, 0}, 10, 10, {0, 0}, 1.0, true, paper, {1, 3});
    REQUIRE(store.remove_layer(2)); // "b": unused
    CHECK(store.viewport(vp)->frozen_layers == std::vector<std::uint16_t>{1, 2}); // "c" moved down
    CHECK(store.viewport(vp)->props.layer == 2);
    REQUIRE(store.remove_layer(1)); // "a": frozen in the viewport, but no entity uses it
    CHECK(store.viewport(vp)->frozen_layers == std::vector<std::uint16_t>{1});
}

TEST_CASE("VPLAYER audit: a frozen-layer index past the layer table is dropped by AUDIT fix") {
    GeometryEngine engine;
    engine.start();
    engine.submit(SetActiveSpaceCommand{1, std::string()});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.active_space == 1; }));
    EntityProps paper;
    paper.set_space(1);
    AddViewportCommand vp{{70, 45}, 100, 50, {0, 0}, 1.0, true, 1, paper, {9}};
    engine.submit(vp);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));
    engine.submit(AuditCommand{true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("AUDIT") != std::string::npos || s.status.find("udit") != std::string::npos; }));
    engine.submit(EnterMspaceCommand{{70, 45}, 1.0, 1.0});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.mspace.active; }));
    CHECK(engine.snapshot().mspace.frozen_layers.empty());
    engine.stop();
}

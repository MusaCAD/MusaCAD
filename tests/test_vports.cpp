// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// VPORTS (#33): tiled model-space viewports -- the standard configurations and Join, the
// store's configuration and named configurations through the engine, the native and DXF
// forms, and the command's prompts against the live tiles a view reports.
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
#include "musacad/core/tiled_viewport.hpp"

using namespace musacad::core;
using namespace musacad::core::io;

namespace {

struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompts.push_back(p); }
    std::vector<std::string> lines;
    std::vector<std::string> prompts;
};
/// A view with two tiles side by side (the right one current, zoomed differently).
struct TiledView : musacad::command::ViewControl {
    void zoom_extents() override {}
    void zoom_scale(double) override {}
    std::vector<TiledViewport> tiled_viewports() const override { return tiles; }
    int active_tile() const override { return active; }
    std::vector<TiledViewport> tiles;
    int active = 0;
};
struct ProcHarness {
    std::vector<Command> cmds;
    SilentOutput out;
    TiledView view;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, &view, out};
    const VportsCommand* last() const {
        const VportsCommand* found = nullptr;
        for (const Command& c : cmds) {
            if (const auto* p = std::get_if<VportsCommand>(&c)) {
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

bool rect_is(const TiledViewport& t, double x0, double y0, double x1, double y1) {
    return std::abs(t.x0 - x0) < 1e-9 && std::abs(t.y0 - y0) < 1e-9 && std::abs(t.x1 - x1) < 1e-9 &&
           std::abs(t.y1 - y1) < 1e-9;
}

TiledViewport tile(double x0, double y0, double x1, double y1, Vec2 c = {}, double h = 0.0) {
    TiledViewport t;
    t.x0 = x0;
    t.y0 = y0;
    t.x1 = x1;
    t.y1 = y1;
    t.center = c;
    t.height = h;
    return t;
}

} // namespace

TEST_CASE("VPORTS layout: the standard configurations tile the window; every tile keeps the view") {
    TiledViewport whole;
    whole.center = {10, 20};
    whole.height = 50;
    const auto two_v = split_vport(whole, "2v");
    REQUIRE(two_v.size() == 2);
    CHECK(rect_is(two_v[0], 0, 0, 0.5, 1));
    CHECK(rect_is(two_v[1], 0.5, 0, 1, 1));
    CHECK(two_v[1].center == Vec2{10, 20});
    CHECK(two_v[1].height == 50.0);
    const auto two_h = split_vport(whole, "2h");
    CHECK(rect_is(two_h[0], 0, 0.5, 1, 1)); // the upper one first
    CHECK(rect_is(two_h[1], 0, 0, 1, 0.5));
    const auto three_r = split_vport(whole, "3r");
    REQUIRE(three_r.size() == 3);
    CHECK(rect_is(three_r[0], 0.5, 0, 1, 1)); // the large one on the right
    CHECK(rect_is(three_r[1], 0, 0.5, 0.5, 1));
    CHECK(rect_is(three_r[2], 0, 0, 0.5, 0.5));
    CHECK(rect_is(split_vport(whole, "3l")[0], 0, 0, 0.5, 1));
    CHECK(rect_is(split_vport(whole, "3a")[0], 0, 0.5, 1, 1));
    CHECK(rect_is(split_vport(whole, "3b")[0], 0, 0, 1, 0.5));
    const auto three_v = split_vport(whole, "3v");
    CHECK(std::abs(three_v[1].x0 - 1.0 / 3.0) < 1e-12);
    CHECK(std::abs(three_v[1].x1 - 2.0 / 3.0) < 1e-12);
    const auto three_h = split_vport(whole, "3h");
    CHECK(std::abs(three_h[0].y0 - 2.0 / 3.0) < 1e-12);
    const auto four = split_vport(whole, "4");
    REQUIRE(four.size() == 4);
    CHECK(rect_is(four[0], 0, 0.5, 0.5, 1));
    CHECK(rect_is(four[3], 0.5, 0, 1, 0.5));
    CHECK(split_vport(whole, "single").size() == 1);
    CHECK(split_vport(whole, "bogus").size() == 1);

    // A split of a tile splits that tile only.
    const auto nested = split_vport(two_v[1], "2h");
    CHECK(rect_is(nested[0], 0.5, 0.5, 1, 1));
    CHECK(rect_is(nested[1], 0.5, 0, 1, 0.5));
}

TEST_CASE("VPORTS join: two tiles sharing a full edge merge into one; others are refused") {
    std::vector<TiledViewport> tiles = split_vport(TiledViewport{}, "3r"); // right, upper-left, lower-left
    tiles[0].center = {5, 5};
    REQUIRE(join_vports(tiles, 1, 2)); // the two stacked left ones
    REQUIRE(tiles.size() == 2);
    CHECK(rect_is(tiles[1], 0, 0, 0.5, 1));
    REQUIRE(join_vports(tiles, 0, 1)); // now side by side
    REQUIRE(tiles.size() == 1);
    CHECK(rect_is(tiles[0], 0, 0, 1, 1));
    CHECK(tiles[0].center == Vec2{5, 5}); // the dominant's view

    std::vector<TiledViewport> four = split_vport(TiledViewport{}, "4");
    CHECK(!join_vports(four, 0, 3)); // diagonal: no shared edge
    CHECK(four.size() == 4);
    CHECK(!join_vports(four, 0, 0));
    CHECK(!join_vports(four, 0, 9));
    std::vector<TiledViewport> three = split_vport(TiledViewport{}, "3r");
    CHECK(!join_vports(three, 0, 1)); // the large right one and a small left one: not a rectangle
}

TEST_CASE("VPORTS engine: Set publishes the tiles with a version bump; Sync does not; Save / Restore / Delete / ?") {
    GeometryEngine engine;
    engine.start();
    REQUIRE(wait_until(engine, [](const auto& s) { return s.vports.empty(); }));
    const std::uint32_t v0 = engine.snapshot().vports_version;

    VportsCommand set;
    set.op = VportsCommand::Op::Set;
    set.tiles = split_vport(tile(0, 0, 1, 1, {10, 10}, 100), "2v");
    set.active = 1;
    engine.submit(set);
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.vports.size() == 2 && s.vports_version == v0 + 1; }));
    CHECK(engine.snapshot().vports_active == 1);
    CHECK(engine.snapshot().status == "2 tiled viewports.");

    // Sync: the live views arrive, no version change (the UI must not re-apply them).
    VportsCommand sync;
    sync.op = VportsCommand::Op::Sync;
    sync.tiles = set.tiles;
    sync.tiles[0].center = {99, 99};
    sync.active = 0;
    engine.submit(sync);
    engine.submit(RegenCommand{}); // any publish
    REQUIRE(wait_until(engine, [](const auto& s) { return s.vports.size() == 2 && s.vports[0].center.x == 99.0; }));
    CHECK(engine.snapshot().vports_version == v0 + 1);
    CHECK(engine.snapshot().vports_active == 0);

    VportsCommand save;
    save.op = VportsCommand::Op::Save;
    save.name = "two";
    engine.submit(save); // empty tiles = the current configuration
    REQUIRE(wait_until(engine, [](const auto& s) { return s.vport_config_names.size() == 1; }));
    CHECK(engine.snapshot().vport_config_names[0] == "two");
    CHECK(engine.snapshot().status == "Viewport configuration \"two\" saved.");

    VportsCommand single;
    single.op = VportsCommand::Op::Set;
    single.tiles = {tile(0, 0, 1, 1, {1, 2}, 30)};
    engine.submit(single);
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.vports.size() == 1 && s.vports_version == v0 + 2; }));
    CHECK(engine.snapshot().status == "Single viewport.");

    VportsCommand restore;
    restore.op = VportsCommand::Op::Restore;
    restore.name = "two";
    engine.submit(restore);
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.vports.size() == 2 && s.vports_version == v0 + 3; }));
    CHECK(engine.snapshot().vports[0].center.x == 99.0); // as synced before the save

    VportsCommand list;
    list.op = VportsCommand::Op::List;
    engine.submit(list);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("two (2)") != std::string::npos; }));

    restore.name = "nope";
    engine.submit(restore);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("No viewport configuration named") != std::string::npos; }));

    VportsCommand del;
    del.op = VportsCommand::Op::Delete;
    del.name = "two";
    engine.submit(del);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.vport_config_names.empty(); }));
    engine.submit(list);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "No saved viewport configurations."; }));

    save.name.clear();
    engine.submit(save);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("needs a name") != std::string::npos; }));
    engine.stop();
}

TEST_CASE("VPORTS files: the tiles, the active one and the saved configurations round-trip natively and through DXF") {
    Document doc;
    doc.vports = split_vport(tile(0, 0, 1, 1, {12.5, -3}, 80), "3r");
    doc.vports[1].center = {1, 1};
    doc.vports_active = 2;
    doc.saved_vports.push_back(VportConfig{"my two", split_vport(tile(0, 0, 1, 1, {0, 0}, 40), "2h")});
    doc.saved_vports.push_back(VportConfig{"quad", split_vport(TiledViewport{}, "4")});

    const std::string text = serialize_native(doc);
    CHECK(text.find("VPORT 0.5 0 1 1 12.5 -3 80\n") != std::string::npos);
    CHECK(text.find("VPORTACTIVE 2\n") != std::string::npos);
    CHECK(text.find("VPORTSAVE 2 my\x1ftwo\n") != std::string::npos);
    CHECK(text.find("VPORTCFG ") != std::string::npos);
    Document rt;
    REQUIRE(parse_native(text, rt).ok);
    CHECK(rt.vports == doc.vports);
    CHECK(rt.vports_active == 2);
    REQUIRE(rt.saved_vports.size() == 2);
    CHECK(rt.saved_vports[0].name == "my two");
    CHECK(rt.saved_vports[0].tiles == doc.saved_vports[0].tiles);
    CHECK(rt.saved_vports[1].tiles.size() == 4);

    // DXF: a VPORT table of *Active entries carries the tiles (not the saved sets).
    const std::string dxf = serialize_dxf(doc);
    CHECK(dxf.find("\n2\nVPORT\n") != std::string::npos);
    CHECK(dxf.find("\n2\n*Active\n") != std::string::npos);
    Document from_dxf;
    REQUIRE(parse_dxf(dxf, from_dxf).ok);
    REQUIRE(from_dxf.vports.size() == 3);
    CHECK(rect_is(from_dxf.vports[0], 0.5, 0, 1, 1));
    CHECK(from_dxf.vports[0].center == Vec2{12.5, -3});
    CHECK(from_dxf.vports[0].height == 80.0);
    CHECK(from_dxf.vports[1].center == Vec2{1, 1});

    // A drawing without tiles writes one whole-window *Active entry, read back as one tile.
    Document plain;
    Document plain_rt;
    REQUIRE(parse_dxf(serialize_dxf(plain), plain_rt).ok);
    REQUIRE(plain_rt.vports.size() == 1);
    CHECK(rect_is(plain_rt.vports[0], 0, 0, 1, 1));

    // Through the store and back.
    GeometryStore store;
    populate_store(store, rt);
    CHECK(store.vports().size() == 3);
    CHECK(store.vports_active() == 2);
    CHECK(store.saved_vport("quad") != nullptr);
    Document again = document_from_store(store);
    CHECK(again.vports == doc.vports);
    CHECK(again.saved_vports == doc.saved_vports);
}

TEST_CASE("VPORTS command: splits the current tile, SIngle keeps its view, Join, Save / Restore / Delete / ?") {
    using Op = VportsCommand::Op;
    ProcHarness h;
    h.view.tiles = split_vport(tile(0, 0, 1, 1, {0, 0}, 100), "2v");
    h.view.tiles[1].center = {50, 50};
    h.view.tiles[1].height = 20;
    h.view.active = 1;

    // "-VPORTS" -> 2 -> Horizontal: the right tile becomes two stacked; the left stays.
    h.proc.submit_line("-VPORTS");
    REQUIRE(h.out.prompts.back().find("[Save/Restore/Delete/Join/SIngle/?/2/3/4] <3>") != std::string::npos);
    h.proc.submit_line("2");
    REQUIRE(h.out.prompts.back().find("[Horizontal/Vertical] <Vertical>") != std::string::npos);
    h.proc.submit_line("H");
    const VportsCommand* c = h.last();
    REQUIRE(c != nullptr);
    CHECK(c->op == Op::Set);
    REQUIRE(c->tiles.size() == 3);
    CHECK(rect_is(c->tiles[0], 0, 0, 0.5, 1));
    CHECK(rect_is(c->tiles[1], 0.5, 0.5, 1, 1));
    CHECK(rect_is(c->tiles[2], 0.5, 0, 1, 0.5));
    CHECK(c->tiles[2].center == Vec2{50, 50}); // the split tile's view, in both halves
    CHECK(c->active == 1);
    CHECK(!h.proc.has_active_command());

    // Enter = 3, Enter = Right.
    h.proc.submit_line("VPORTS");
    h.proc.submit_line("");
    REQUIRE(h.out.prompts.back().find("[Horizontal/Vertical/Above/Below/Left/Right] <Right>") != std::string::npos);
    h.proc.submit_line("");
    c = h.last();
    REQUIRE(c->tiles.size() == 4);
    CHECK(rect_is(c->tiles[1], 0.75, 0, 1, 1)); // the large one on the right of the split tile

    h.proc.submit_line("VPORTS");
    h.proc.submit_line("4");
    CHECK(h.last()->tiles.size() == 5);

    // SIngle: one whole-window tile with the current tile's view.
    h.proc.submit_line("VPORTS");
    h.proc.submit_line("SI");
    c = h.last();
    REQUIRE(c->tiles.size() == 1);
    CHECK(rect_is(c->tiles[0], 0, 0, 1, 1));
    CHECK(c->tiles[0].center == Vec2{50, 50});
    CHECK(c->tiles[0].height == 20.0);

    // Join: Enter takes the current (right) tile as dominant; a point names the other.
    h.proc.submit_line("VPORTS");
    h.proc.submit_line("J");
    REQUIRE(h.out.prompts.back().find("Select dominant viewport <current>") != std::string::npos);
    h.proc.submit_line("");
    REQUIRE(h.out.prompts.back().find("Select viewport to join") != std::string::npos);
    h.proc.submit_line("0.25,0.5");
    c = h.last();
    REQUIRE(c->tiles.size() == 1);
    CHECK(rect_is(c->tiles[0], 0, 0, 1, 1));
    CHECK(c->tiles[0].center == Vec2{50, 50}); // the dominant's view
    CHECK(c->active == 0);

    // Save takes the live tiles; Restore and Delete pass the name; ? lists.
    h.proc.submit_line("VPORTS");
    h.proc.submit_line("S");
    h.proc.submit_line("pair");
    c = h.last();
    CHECK(c->op == Op::Save);
    CHECK(c->name == "pair");
    CHECK(c->tiles.size() == 2);
    h.proc.submit_line("VPORTS");
    h.proc.submit_line("R");
    h.proc.submit_line("pair");
    CHECK(h.last()->op == Op::Restore);
    h.proc.submit_line("VPORTS");
    h.proc.submit_line("D");
    h.proc.submit_line("pair");
    CHECK(h.last()->op == Op::Delete);
    h.proc.submit_line("VPORTS");
    h.proc.submit_line("?");
    CHECK(h.last()->op == Op::List);
    CHECK(h.proc.has_active_command()); // ? returns to the option prompt
    h.proc.cancel();

    // With one tile, Join has nothing to do; a view that reports nothing counts as one tile.
    h.view.tiles = {tile(0, 0, 1, 1, {3, 4}, 10)};
    h.view.active = 0;
    h.proc.submit_line("VPORTS");
    h.proc.submit_line("J");
    CHECK(h.out.lines.back().find("nothing to join") != std::string::npos);
    h.proc.cancel();
    h.view.tiles.clear();
    h.proc.submit_line("VPORTS");
    h.proc.submit_line("2");
    h.proc.submit_line("V");
    c = h.last();
    REQUIRE(c->tiles.size() == 2);
    CHECK(rect_is(c->tiles[0], 0, 0, 0.5, 1));
}

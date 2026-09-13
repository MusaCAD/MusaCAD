// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// EATTEDIT and BATTMAN (#25): the attribute-editor targets the snapshot publishes for a
// block reference, editing every value of one reference by handle, and rewriting a
// block's attribute definitions (reorder / rename / remove / add) with the references
// synced by tag -- plus the command-layer hooks that open the two dialogs.
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/scene_snapshot.hpp"

using namespace musacad::core;

namespace {

struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompts.push_back(p); }
    std::vector<std::string> lines;
    std::vector<std::string> prompts;
};
/// A view that records the two dialog hooks instead of opening anything.
struct RecordingView : musacad::command::ViewControl {
    void zoom_extents() override {}
    void zoom_scale(double) override {}
    void attribute_editor_at(Vec2 pick, double radius) override {
        editor_picks.push_back(pick);
        last_radius = radius;
    }
    void block_attribute_manager() override { ++manager_opened; }
    std::vector<Vec2> editor_picks;
    double last_radius = 0.0;
    int manager_opened = 0;
};
struct ProcHarness {
    std::vector<Command> cmds;
    SilentOutput out;
    RecordingView view;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, &view, out};
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

AddAttDefCommand attdef(Vec2 pos, const char* tag, const char* prompt, const char* def,
                        std::uint8_t flags, std::uint64_t group) {
    AddAttDefCommand c;
    c.text.pos = pos;
    c.text.height = 2.5;
    c.text.content = tag;
    c.prompt = prompt;
    c.def = def;
    c.flags = flags;
    c.group = group;
    return c;
}

/// A store with block "TB" (a 60-unit line + attributes NO / REV) and one reference of
/// it at `pos` carrying `values`.
EntityHandle make_reference(GeometryStore& s, Vec2 pos, const std::vector<std::string>& values) {
    std::vector<BlockDef> blocks(1);
    blocks[0].name = "TB";
    LineData line;
    line.a = {0, 0};
    line.b = {60, 0};
    blocks[0].content.lines.push_back(line);
    BlockAttDef no;
    no.tag = "NO";
    no.prompt = "Drawing number";
    no.def = "D-000";
    no.text.pos = {2, 2};
    no.text.height = 2.5;
    BlockAttDef rev;
    rev.tag = "REV";
    rev.prompt = "Revision";
    rev.def = "A";
    rev.text.pos = {2, 8};
    rev.text.height = 2.5;
    blocks[0].content.attdefs = {no, rev};
    s.set_block_table(std::move(blocks));
    return s.add_insert(0, pos, 1, 1, 0, {}, values);
}

} // namespace

TEST_CASE("EATTEDIT snapshot: a reference with attributes is an editor target with its values, defaults filled in") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle ref = make_reference(store, {100, 50}, {"D-100"}); // REV left at its default
    RenderSnapshot snap;
    build_render_snapshot(store, kernel, snap, 0.01, 1.0);
    REQUIRE(snap.attrib_edit_targets.size() == 1);
    const AttribEditTarget& t = snap.attrib_edit_targets[0];
    CHECK(t.handle == ref);
    CHECK(t.block == 0);
    CHECK(t.values == std::vector<std::string>{"D-100", "A"});
    // The extent covers the block's line at the reference's place.
    CHECK(t.min.x <= 100.0);
    CHECK(t.max.x >= 160.0);
    CHECK(t.min.y <= 50.0);

    // A locked layer is not an editor target; a block without attributes never is.
    Layer locked;
    locked.name = "locked";
    locked.locked = true;
    const std::uint16_t li = store.add_layer(locked);
    EntityProps on_locked;
    on_locked.layer = li;
    store.add_insert(0, {300, 0}, 1, 1, 0, on_locked, {"x"});
    std::vector<BlockDef> two = store.blocks();
    two.push_back(BlockDef{});
    two.back().name = "PLAIN";
    two.back().content.lines = two[0].content.lines;
    store.set_block_table(std::move(two));
    store.add_insert(1, {500, 0}, 1, 1, 0);
    RenderSnapshot again;
    build_render_snapshot(store, kernel, again, 0.01, 1.0);
    CHECK(again.attrib_edit_targets.size() == 1);
}

TEST_CASE("EATTEDIT engine: every value of one reference by handle, as one undo step") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {60, 0}, 1});
    engine.submit(attdef({2, 2}, "NO", "Drawing number", "D-000", 0, 1));
    engine.submit(attdef({2, 8}, "REV", "Revision", "A", 0, 1));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 3; }));
    engine.submit(DefineBlockCommand{"TB", {0, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.attrib_edit_targets.size() == 1; }));
    const std::size_t before = engine.snapshot().line_vertices.size();
    const EntityHandle ref = engine.snapshot().attrib_edit_targets[0].handle;
    CHECK(engine.snapshot().attrib_edit_targets[0].values == std::vector<std::string>{"D-000", "A"});

    engine.submit(SetInsertAttribsCommand{ref, {"D-100-A-MUCH-LONGER-NUMBER", "B"}, 3});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        return s.attrib_edit_targets.size() == 1 && s.attrib_edit_targets[0].values[0] == "D-100-A-MUCH-LONGER-NUMBER";
    }));
    CHECK(engine.snapshot().attrib_edit_targets[0].values[1] == "B");
    CHECK(engine.snapshot().line_vertices.size() > before); // more glyph strokes
    CHECK(engine.snapshot().status == "Attributes updated.");
    // The handle changed (erase + create); the same values again are a no-op.
    const EntityHandle ref2 = engine.snapshot().attrib_edit_targets[0].handle;
    CHECK(!(ref2 == ref));
    engine.submit(SetInsertAttribsCommand{ref2, {"D-100-A-MUCH-LONGER-NUMBER", "B"}, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Attributes unchanged."; }));
    // A stale handle is refused.
    engine.submit(SetInsertAttribsCommand{ref, {"x", "y"}, 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("select a block reference") != std::string::npos; }));
    // Undo takes the edit back as one step.
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.line_vertices.size() == before; }));
    CHECK(engine.snapshot().attrib_edit_targets[0].values == std::vector<std::string>{"D-000", "A"});
    engine.stop();
}

TEST_CASE("BATTMAN engine: definitions reordered, renamed, removed and added; references synced by tag; undo") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {60, 0}, 1});
    engine.submit(attdef({2, 2}, "NO", "Drawing number", "D-000", 0, 1));
    engine.submit(attdef({2, 8}, "REV", "Revision", "A", 0, 1));
    engine.submit(attdef({2, 14}, "BY", "Drawn by", "", 0, 1));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 4; }));
    engine.submit(DefineBlockCommand{"TB", {0, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.attrib_edit_targets.size() == 1; }));
    InsertBlockCommand ins;
    ins.name = "TB";
    ins.pos = {200, 0};
    ins.group = 3;
    ins.attribs = {"D-200", "C", "PK"};
    engine.submit(ins);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.attrib_edit_targets.size() == 2; }));
    const std::size_t before = engine.snapshot().line_vertices.size();

    // Reorder (REV first), rename NO -> DWG keeping its values? No: a rename is a new tag
    // (defaults), as in AutoCAD. Remove BY, add SHEET with a default, make REV Constant.
    SetBlockAttDefsCommand c;
    c.name = "tb"; // case-insensitive
    c.attdefs = {BlockAttDefInfo{"REV", "Revision", "A", kAttConstant, 2.5},
                 BlockAttDefInfo{"NO", "Drawing number", "D-000", 0, 3.5},
                 BlockAttDefInfo{"SHEET", "Sheet", "1 of 1", 0, 2.5}};
    c.sync = true;
    c.group = 4;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.block_attdefs.size() == 1 && s.block_attdefs[0].size() == 3 && s.block_attdefs[0][0].tag == "REV";
    }));
    const auto& defs = engine.snapshot().block_attdefs[0];
    CHECK(defs[0].flags == kAttConstant);
    CHECK(defs[1].tag == "NO");
    CHECK(defs[1].height == 3.5);
    CHECK(defs[2].tag == "SHEET");
    CHECK(defs[2].def == "1 of 1");
    CHECK(engine.snapshot().status.find("3 attribute(s), 2 reference(s) synced") != std::string::npos);
    // Both references followed by tag: the second keeps D-200 / C and gains the SHEET default.
    REQUIRE(engine.snapshot().attrib_edit_targets.size() == 2);
    std::vector<std::string> second;
    for (const AttribEditTarget& t : engine.snapshot().attrib_edit_targets) {
        if (t.min.x >= 150.0) {
            second = t.values;
        }
    }
    CHECK(second == std::vector<std::string>{"C", "D-200", "1 of 1"});

    // Undo restores the references' values (the definition itself is not an undo step).
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        for (const AttribEditTarget& t : s.attrib_edit_targets) {
            if (t.min.x >= 150.0 && t.values.size() == 3 && t.values[0] == "D-200") {
                return true;
            }
        }
        return false;
    }));
    (void)before;

    // Without sync the references keep their values positionally.
    SetBlockAttDefsCommand plain;
    plain.name = "TB";
    plain.attdefs = {BlockAttDefInfo{"ONLY", "", "x", 0, 2.5}};
    plain.sync = false;
    plain.group = 5;
    engine.submit(plain);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.block_attdefs[0].size() == 1; }));
    CHECK(engine.snapshot().status.find("1 attribute(s).") != std::string::npos);

    // A missing block and an empty tag are refused.
    SetBlockAttDefsCommand bad;
    bad.name = "NOPE";
    engine.submit(bad);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("No block named") != std::string::npos; }));
    bad.name = "TB";
    bad.attdefs = {BlockAttDefInfo{"", "", "", 0, 2.5}};
    engine.submit(bad);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("needs a tag") != std::string::npos; }));
    engine.stop();
}

TEST_CASE("EATTEDIT / BATTMAN commands: a pick opens the editor through the view; BATTMAN opens the manager") {
    ProcHarness h;
    h.proc.submit_line("EATTEDIT");
    REQUIRE(h.out.prompts.back().find("Select a block reference") != std::string::npos);
    h.proc.submit_line("100,50");
    REQUIRE(h.view.editor_picks.size() == 1);
    CHECK(h.view.editor_picks[0].x == 100.0);
    CHECK(h.view.editor_picks[0].y == 50.0);
    CHECK(!h.proc.has_active_command());

    h.proc.submit_line("BATTMAN");
    CHECK(h.view.manager_opened == 1);
    CHECK(!h.proc.has_active_command());

    // Enter at the pick prompt just ends the command.
    h.proc.submit_line("EATTEDIT");
    h.proc.submit_line("");
    CHECK(!h.proc.has_active_command());
    CHECK(h.view.editor_picks.size() == 1);
}

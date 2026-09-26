// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// The engine side of "Select objects:" (issue #46): fence, polygon, Last, Previous,
// Undo, Remove / Shift-toggle, the drag preview, selection cycling, SELECTSIMILAR,
// QSELECT's filter, ISOLATEOBJECTS; and OOPS, -GROUP's edits, PURGE by name and the
// linetype-scale modes (issues #53, #67).

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"

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
bool settle(GeometryEngine& e, std::uint64_t& seen) {
    // Wait for a NEW status message (every selection command reports one).
    return wait_until(e, [&](const auto& s) {
        if (s.status_version != seen) {
            seen = s.status_version;
            return true;
        }
        return false;
    });
}
std::size_t sel(GeometryEngine& e) {
    e.consume_snapshot();
    return e.snapshot().selection.size();
}
// Three horizontal lines at y = 0, 50, 100 (x 0..100) and a circle at (200, 50).
void scene(GeometryEngine& e) {
    e.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    e.submit(AddLineCommand{{0, 50}, {100, 50}, 2});
    e.submit(AddLineCommand{{0, 100}, {100, 100}, 3});
    e.submit(AddCircleCommand{{200, 50}, 10.0, 4});
    REQUIRE(wait_until(e, [](const auto& s) { return s.line_vertices.size() > 6; }));
}
} // namespace

TEST_CASE("#46 Fence selects what it crosses; WPolygon what it encloses; CPolygon both") {
    GeometryEngine e;
    e.start();
    scene(e);
    std::uint64_t seen = 0;
    e.submit(SelectFenceCommand{{{50, -10}, {50, 60}}}); // crosses y=0 and y=50
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 2);
    REQUIRE(e.snapshot().status == "2 found.");

    e.submit(ClearSelectionCommand{});
    e.submit(SelectPolygonCommand{{{-10, -10}, {110, -10}, {110, 10}, {-10, 10}}, false}); // encloses y=0 only
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 1);

    e.submit(SelectPolygonCommand{{{-10, 40}, {50, 40}, {50, 110}, {-10, 110}}, true}); // crosses y=50 and y=100
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 3);
    REQUIRE(e.snapshot().status == "2 found, 3 total.");
    e.stop();
}

TEST_CASE("#46 Last, Previous and Undo; Remove and the Shift toggle take objects out") {
    GeometryEngine e;
    e.start();
    scene(e);
    std::uint64_t seen = 0;
    e.submit(SelectLastCommand{}); // the circle
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 1);
    REQUIRE(e.snapshot().selection[0].kind == EntityKind::Circle);

    // An edit consumes the set: Previous recalls it afterwards.
    e.submit(MoveSelectionCommand{{1, 0}, 10});
    e.submit(ClearSelectionCommand{});
    e.submit(SelectPreviousCommand{});
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 1);

    e.submit(SelectPickCommand{{50, 0}, 2.0, true, true}); // + the y=0 line
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 2);
    e.submit(SelectUndoCommand{}); // back to the circle alone
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 1);

    e.submit(SelectAllCommand{true});
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 4);
    SelectPickCommand rm{{50, 50}, 2.0, true, true};
    rm.remove = true;
    e.submit(rm);
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 3);
    REQUIRE(e.snapshot().status == "1 found, 1 removed, 3 total.");
    SelectPickCommand tg{{50, 100}, 2.0, true, true};
    tg.toggle = true; // Shift + click on a selected object
    e.submit(tg);
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 2);
    e.submit(tg); // ... and again puts it back
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 3);
    SelectWindowCommand rw{{-10, -10}, {110, 10}, false, true, true};
    rw.remove = true;
    e.submit(rw);
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 2);
    e.stop();
}

TEST_CASE("#46 the drag preview publishes the would-be set; selection cycling lists the objects under a pick") {
    GeometryEngine e;
    e.start();
    scene(e);
    e.submit(SelectPreviewCommand{{-10, -10}, {110, 60}, {}, false}); // a window round y=0 and y=50
    REQUIRE(wait_until(e, [](const auto& s) { return s.preview_line_vertices.size() == 4; }));
    e.submit(SelectPreviewCommand{}); // cleared
    REQUIRE(wait_until(e, [](const auto& s) { return s.preview_line_vertices.empty(); }));

    e.submit(AddLineCommand{{0, 0}, {100, 0}, 5}); // a second line on top of the first
    SelectPickCommand pk{{50, 0}, 2.0, false, false};
    pk.cycle = true;
    e.submit(pk);
    REQUIRE(wait_until(e, [](const auto& s) { return s.pick_candidates.size() == 2; }));
    REQUIRE(e.snapshot().pick_candidates[0].name.rfind("Line", 0) == 0);
    const EntityHandle other = e.snapshot().pick_candidates[1].handle;
    e.submit(SelectHandleCommand{other});
    REQUIRE(wait_until(e, [&](const auto& s) { return s.selection.size() == 1 && s.selection[0] == other; }));
    e.stop();
}

TEST_CASE("#46 SELECTSIMILAR by kind and layer; QSELECT by type, property and operator") {
    GeometryEngine e;
    e.start();
    Layer l2;
    l2.name = "WALLS";
    e.submit(AddLayerCommand{l2});
    EntityProps on_walls;
    on_walls.layer = 1;
    e.submit(AddLineCommand{{0, 0}, {100, 0}, 1, on_walls});
    e.submit(AddLineCommand{{0, 50}, {100, 50}, 2, on_walls});
    e.submit(AddLineCommand{{0, 100}, {10, 100}, 3});
    e.submit(AddCircleCommand{{200, 50}, 10.0, 4, on_walls});
    e.submit(AddCircleCommand{{300, 50}, 30.0, 5});
    REQUIRE(wait_until(e, [](const auto& s) { return s.layers.size() == 2 && s.line_vertices.size() > 6; }));
    std::uint64_t seen = 0;
    e.submit(SelectPickCommand{{50, 0}, 2.0, false, true});
    REQUIRE(settle(e, seen));
    e.submit(SelectSimilarCommand{130}); // layer + name: the other WALLS line, not the layer-0 one
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 2);
    for (const EntityHandle h : e.snapshot().selection) {
        REQUIRE(h.kind == EntityKind::Line);
    }

    SelectFilterCommand f;
    f.kind = static_cast<int>(EntityKind::Circle);
    f.property = 6; // radius
    f.op = 2;       // greater than
    f.number = 20.0;
    e.submit(f);
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 1);
    REQUIRE(e.snapshot().selection[0].kind == EntityKind::Circle);

    SelectFilterCommand g;
    g.property = 2; // layer name, any kind
    g.text = "walls";
    e.submit(g);
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 3);

    SelectFilterCommand x = g;
    x.include = false; // everything but WALLS
    e.submit(x);
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 2);

    SelectFilterCommand a;
    a.property = 7; // length
    a.op = 3;       // less than
    a.number = 20.0;
    a.append = true;
    e.submit(a); // the short line, already in the set
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 2);
    e.stop();
}

TEST_CASE("#46 ISOLATEOBJECTS hides the rest (not drawn, not pickable) and UNISOLATEOBJECTS shows them; the flag survives the file") {
    GeometryEngine e;
    e.start();
    scene(e);
    std::uint64_t seen = 0;
    e.submit(SelectPickCommand{{50, 0}, 2.0, false, true});
    REQUIRE(settle(e, seen));
    e.submit(IsolateObjectsCommand{0});
    REQUIRE(settle(e, seen));
    REQUIRE(wait_until(e, [](const auto& s) { return s.object_isolation && s.line_vertices.size() == 2; }));
    e.submit(SelectPickCommand{{50, 50}, 2.0, false, true}); // hidden: nothing to pick
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 0);

    io::Document doc;
    e.submit(SaveDocumentCommand{});
    // The store carries the flag: a document made from it keeps it.
    e.submit(IsolateObjectsCommand{2});
    REQUIRE(settle(e, seen));
    REQUIRE(wait_until(e, [](const auto& s) { return !s.object_isolation && s.line_vertices.size() > 6; }));

    e.submit(SelectPickCommand{{50, 50}, 2.0, false, true});
    REQUIRE(settle(e, seen));
    e.submit(IsolateObjectsCommand{1}); // HIDEOBJECTS on the selection
    REQUIRE(settle(e, seen));
    REQUIRE(wait_until(e, [](const auto& s) { return s.object_isolation; }));
    e.stop();
}

TEST_CASE("#53 OOPS brings back the last erased set, keeping later work; U undoes the OOPS") {
    GeometryEngine e;
    e.start();
    // Three lines only (a circle's tessellation would muddy the vertex counts).
    e.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    e.submit(AddLineCommand{{0, 50}, {100, 50}, 2});
    e.submit(AddLineCommand{{0, 100}, {100, 100}, 3});
    REQUIRE(wait_until(e, [](const auto& s) { return s.line_vertices.size() == 6; }));
    std::uint64_t seen = 0;
    e.submit(SelectWindowCommand{{-10, -10}, {110, 60}, false, false, true}); // y=0 and y=50
    REQUIRE(settle(e, seen));
    e.submit(EraseSelectionCommand{10});
    REQUIRE(wait_until(e, [](const auto& s) { return s.line_vertices.size() == 2; }));
    e.submit(AddLineCommand{{0, 200}, {100, 200}, 11}); // later work
    REQUIRE(wait_until(e, [](const auto& s) { return s.line_vertices.size() == 4; }));
    e.submit(OopsCommand{12});
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status == "2 object(s) restored.");
    REQUIRE(wait_until(e, [](const auto& s) { return s.line_vertices.size() == 8; }));
    e.submit(OopsCommand{13}); // nothing twice
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status == "OOPS: nothing to restore.");
    e.submit(UndoLastGroupCommand{}); // the OOPS itself
    REQUIRE(wait_until(e, [](const auto& s) { return s.line_vertices.size() == 4; }));
    e.submit(ErasePickCommand{{50, 200}, 2.0, 14});
    REQUIRE(wait_until(e, [](const auto& s) { return s.line_vertices.size() == 2; }));
    e.submit(OopsCommand{15});
    REQUIRE(wait_until(e, [](const auto& s) { return s.line_vertices.size() == 4; }));
    e.stop();
}

TEST_CASE("#53 -GROUP's edits: add, remove, rename, describe, selectable, reorder, explode; the listing") {
    GeometryEngine e;
    e.start();
    scene(e);
    std::uint64_t seen = 0;
    e.submit(SelectWindowCommand{{-10, -10}, {110, 60}, false, false, true});
    REQUIRE(settle(e, seen));
    e.submit(CreateGroupCommand{"FRAME", "two lines"});
    REQUIRE(settle(e, seen));
    e.submit(ClearSelectionCommand{});
    e.submit(SelectPickCommand{{50, 100}, 2.0, false, true}); // the y=100 line
    REQUIRE(settle(e, seen));
    GroupEditCommand add;
    add.name = "FRAME";
    add.op = 0;
    e.submit(add);
    REQUIRE(settle(e, seen));
    REQUIRE(wait_until(e, [](const auto& s) { return !s.group_sizes.empty() && s.group_sizes[0] == 3; }));

    GroupEditCommand ren;
    ren.name = "FRAME";
    ren.op = 2;
    ren.text = "BOX";
    e.submit(ren);
    REQUIRE(wait_until(e, [](const auto& s) { return !s.group_names.empty() && s.group_names[0] == "BOX"; }));
    GroupEditCommand desc;
    desc.name = "BOX";
    desc.op = 3;
    desc.text = "three lines";
    e.submit(desc);
    REQUIRE(wait_until(e, [](const auto& s) { return !s.group_descriptions.empty() && s.group_descriptions[0] == "three lines"; }));

    GroupEditCommand order;
    order.name = "BOX";
    order.op = 5;
    order.from = 2;
    order.to = 0;
    seen = e.snapshot().status_version; // the rename and description reported meanwhile
    e.submit(order);
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status.find("moved to 0") != std::string::npos);

    e.submit(ListGroupsCommand{});
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status.find("BOX") != std::string::npos);
    REQUIRE(e.snapshot().status.find("three lines") != std::string::npos);

    GroupEditCommand unsel;
    unsel.name = "BOX";
    unsel.op = 4;
    unsel.flag = false;
    e.submit(unsel);
    REQUIRE(settle(e, seen));
    e.submit(ClearSelectionCommand{});
    e.submit(SelectPickCommand{{50, 0}, 2.0, false, true}); // not selectable: the member alone
    REQUIRE(settle(e, seen));
    REQUIRE(sel(e) == 1);

    GroupEditCommand rm;
    rm.name = "BOX";
    rm.op = 1; // the selected member leaves the group
    e.submit(rm);
    REQUIRE(wait_until(e, [](const auto& s) { return !s.group_sizes.empty() && s.group_sizes[0] == 2; }));

    GroupEditCommand by_pick;
    by_pick.by_pick = true;
    by_pick.pick = {50, 50};
    by_pick.pick_radius = 2.0;
    by_pick.op = 6; // explode
    e.submit(by_pick);
    REQUIRE(wait_until(e, [](const auto& s) { return s.group_names.empty(); }));
    e.stop();
}

TEST_CASE("#53 PURGE by name, the listing, and zero-length / empty text objects; the candidates are published") {
    GeometryEngine e;
    e.start();
    for (const char* n : {"A", "B", "C"}) {
        Layer l;
        l.name = n;
        e.submit(AddLayerCommand{l});
    }
    e.submit(AddLineCommand{{0, 0}, {0, 0}, 1});      // zero length
    e.submit(AddLineCommand{{0, 0}, {10, 0}, 2});
    e.submit(AddTextCommand{{0, 5}, 2.5, 0.0, 0, "   ", 3}); // blank text
    e.submit(AddTextCommand{{0, 9}, 2.5, 0.0, 0, "keep", 4});
    REQUIRE(wait_until(e, [](const auto& s) {
        return s.layers.size() == 4 && s.purge.layers.size() == 3 && s.purge.zero_length == 1 && s.purge.empty_text == 1;
    }));
    std::uint64_t seen = e.snapshot().status_version; // the layer adds reported meanwhile
    PurgeCommand list;
    list.list_only = true;
    e.submit(list);
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status.find("Unused layers: A, B, C") != std::string::npos);
    REQUIRE(e.snapshot().status.find("Zero-length geometry: 1") != std::string::npos);

    PurgeCommand one;
    one.what = 4;
    one.name = "B";
    e.submit(one);
    REQUIRE(wait_until(e, [](const auto& s) { return s.layers.size() == 3; }));
    REQUIRE(e.snapshot().layers[1].name == "A");
    REQUIRE(e.snapshot().layers[2].name == "C");

    PurgeCommand pat;
    pat.what = 4;
    pat.name = "?"; // one character: A and C
    e.submit(pat);
    REQUIRE(wait_until(e, [](const auto& s) { return s.layers.size() == 1; }));

    PurgeCommand zero;
    zero.what = 8;
    zero.group = 20;
    seen = e.snapshot().status_version; // the layer purges reported meanwhile
    e.submit(zero);
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status == "Purged 1 zero-length object.");
    PurgeCommand empty;
    empty.what = 9;
    empty.group = 21;
    e.submit(empty);
    REQUIRE(settle(e, seen));
    REQUIRE(e.snapshot().status == "Purged 1 empty text object.");
    REQUIRE(wait_until(e, [](const auto& s) { return s.purge.zero_length == 0 && s.purge.empty_text == 0; }));
    e.submit(UndoLastGroupCommand{}); // the empty text comes back
    REQUIRE(wait_until(e, [](const auto& s) { return s.purge.empty_text == 1; }));
    e.stop();
}

TEST_CASE("#67 PSLTSCALE / MSLTSCALE and INSUNITS are set, published and saved with the drawing") {
    GeometryEngine e;
    e.start();
    e.submit(SetLtscaleModesCommand{false, false});
    DrawingUnits u;
    u.insunits = 4;
    e.submit(SetUnitsCommand{u});
    e.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(e, [](const auto& s) { return !s.psltscale && !s.msltscale && s.units.insunits == 4; }));
    e.stop();

    io::Document doc;
    doc.psltscale = false;
    doc.msltscale = true;
    doc.display_units.insunits = 6;
    const std::string text = io::serialize_native(doc);
    REQUIRE(text.find("LTSCALEMODE 0 1") != std::string::npos);
    io::Document back;
    REQUIRE(io::parse_native(text, back).ok);
    REQUIRE(!back.psltscale);
    REQUIRE(back.msltscale);
    REQUIRE(back.display_units.insunits == 6);
    REQUIRE(units::insunits_name(6) == std::string("Meters"));
}

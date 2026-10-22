// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// A wipeout as an object of its own (#40): AutoCAD's WIPEOUT read from and written to DXF,
// Quick Select's Wipeout type, Select Similar, the Properties palette, LIST, EXPLODE and
// HATCHEDIT. HATCHEDIT's Origin, ADd boundaries and Remove boundaries (#41). ATTSYNC (#75).
// 3DOSMODE and UCSDETECT, F4 and F6 (#65).

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/sysvars.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/properties_registry.hpp"

using namespace musacad::core;
using musacad::command::CommandOutput;
using musacad::command::CommandProcessor;
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

io::Document dump(GeometryEngine& engine, const char* name) {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
    engine.consume_snapshot();
    const std::uint64_t before = engine.snapshot().document_version;
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [before](const auto& s) { return s.document_version > before; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    std::filesystem::remove(p);
    return doc;
}

bool near(Vec2 p, Vec2 q, double eps = 1e-9) { return std::abs(p.x - q.x) < eps && std::abs(p.y - q.y) < eps; }

AddHatchCommand wipeout(std::vector<Vec2> loop, std::uint64_t group) {
    AddHatchCommand w;
    w.loops = {std::move(loop)};
    w.pattern_name = "WIPEOUT";
    w.group = group;
    return w;
}

const io::DocInsert* insert_at(const io::Document& doc, double x) {
    for (const io::DocInsert& in : doc.inserts) {
        if (std::abs(in.pos.x - x) < 1e-9) {
            return &in;
        }
    }
    return nullptr;
}

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
    void run(std::initializer_list<const char*> lines) {
        for (const char* l : lines) {
            proc.submit_line(l);
        }
    }
    template <class T>
    [[nodiscard]] int count() const {
        int n = 0;
        for (const Command& c : cmds) {
            n += std::holds_alternative<T>(c) ? 1 : 0;
        }
        return n;
    }
    template <class T>
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
};
} // namespace

TEST_CASE("DXF: AutoCAD's WIPEOUT comes in where it was -- turned, by polygon or rectangle -- with WIPEOUTFRAME") {
    // A one-pixel image: insertion, U and V the pixel's sides; the clip boundary in pixels
    // from the pixel's centre, v down.
    const std::string dxf =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nWIPEOUT\n5\n2A\n100\nAcDbEntity\n8\nMASKS\n100\nAcDbWipeout\n90\n0\n"
        "10\n10\n20\n0\n30\n0\n11\n0\n21\n5\n31\n0\n12\n-4\n22\n0\n32\n0\n13\n1\n23\n1\n"
        "340\n0\n70\n7\n280\n1\n281\n50\n282\n50\n283\n0\n360\n0\n71\n2\n91\n5\n"
        "14\n-0.5\n24\n-0.5\n14\n0.5\n24\n-0.5\n14\n0.5\n24\n0.5\n14\n-0.5\n24\n0.5\n14\n-0.5\n24\n-0.5\n"
        "0\nWIPEOUT\n5\n2B\n100\nAcDbEntity\n8\n0\n100\nAcDbWipeout\n90\n0\n"
        "10\n0\n20\n20\n30\n0\n11\n8\n21\n0\n31\n0\n12\n0\n22\n3\n32\n0\n13\n1\n23\n1\n"
        "70\n7\n280\n1\n71\n1\n91\n2\n14\n-0.5\n24\n-0.5\n14\n0.5\n24\n0.5\n"
        "0\nENDSEC\n"
        "0\nSECTION\n2\nOBJECTS\n0\nWIPEOUTVARIABLES\n5\n13\n100\nAcDbWipeoutVariables\n70\n0\n0\nENDSEC\n0\nEOF\n";
    io::Document doc;
    REQUIRE(io::parse_dxf(dxf, doc).ok);
    REQUIRE(doc.hatches.size() == 2);
    const io::DocHatch& turned = doc.hatches[0];
    REQUIRE(turned.pattern_name == "WIPEOUT");
    REQUIRE(turned.loops.size() == 1);
    const std::vector<Vec2>& a = turned.loops[0];
    REQUIRE(a.size() == 4); // the closing vertex not repeated
    REQUIRE(near(a[0], {6, 0}));
    REQUIRE(near(a[1], {6, 5}));
    REQUIRE(near(a[2], {10, 5}));
    REQUIRE(near(a[3], {10, 0}));
    REQUIRE(turned.props.layer < doc.layers.size());
    REQUIRE(doc.layers[turned.props.layer].name == "MASKS");
    const std::vector<Vec2>& b = doc.hatches[1].loops[0];
    REQUIRE(b.size() == 4);
    REQUIRE(near(b[0], {0, 23}));
    REQUIRE(near(b[1], {8, 23}));
    REQUIRE(near(b[2], {8, 20}));
    REQUIRE(near(b[3], {0, 20}));
    REQUIRE_FALSE(doc.wipeout_frames);
}

TEST_CASE("DXF: a wipeout goes out as a WIPEOUT, its class and WIPEOUTFRAME with it, and comes back") {
    io::Document doc;
    io::DocHatch w;
    w.pattern_name = "WIPEOUT";
    w.loops = {{{1, 2}, {7, 3}, {5, 9}}};
    doc.hatches.push_back(w);
    doc.wipeout_frames = true;
    doc.wipeout_frames_plot = false; // Display but not plot: 2
    const std::string dxf = io::serialize_dxf(doc);
    REQUIRE(dxf.find("\nWIPEOUT\n") != std::string::npos);
    REQUIRE(dxf.find("\nAcDbWipeout\n") != std::string::npos);
    REQUIRE(dxf.find("\nCLASS\n1\nWIPEOUT\n") != std::string::npos);
    REQUIRE(dxf.find("\nACAD_WIPEOUT_VARS\n") != std::string::npos);
    REQUIRE(dxf.find("\nAcDbWipeoutVariables\n70\n2\n") != std::string::npos);
    REQUIRE(dxf.find("\nHATCH\n") == std::string::npos);
    io::Document back;
    REQUIRE(io::parse_dxf(dxf, back).ok);
    REQUIRE(back.hatches.size() == 1);
    REQUIRE(back.hatches[0].pattern_name == "WIPEOUT");
    REQUIRE(back.hatches[0].loops.size() == 1);
    REQUIRE(back.hatches[0].loops[0].size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        REQUIRE(back.hatches[0].loops[0][i].x == Approx(w.loops[0][i].x));
        REQUIRE(back.hatches[0].loops[0][i].y == Approx(w.loops[0][i].y));
    }
    REQUIRE(back.wipeout_frames);
    REQUIRE_FALSE(back.wipeout_frames_plot);
}

TEST_CASE("A wipeout is its own type: Quick Select, Select Similar, Properties, LIST, EXPLODE, HATCHEDIT") {
    // The Properties palette: "Wipeout", without a hatch's pattern, scale, angle or origin.
    const AddHatchCommand w = wipeout({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, 0);
    AddHatchCommand solid = w;
    solid.pattern_name = "SOLID";
    const SelectionSummary one = summarize_selection({Command{w}});
    REQUIRE(one.type_label == "Wipeout");
    REQUIRE(one.family_plus1 == 0); // no Hatch Editor tab
    for (const PropertyField& f : one.fields) {
        REQUIRE(f.id != PropertyId::HatchPattern);
        REQUIRE(f.id != PropertyId::HatchScale);
    }
    REQUIRE(summarize_selection({Command{w}, Command{w}}).type_label == "2 Wipeouts");
    REQUIRE(summarize_selection({Command{w}, Command{solid}}).type_label == "Mixed (2)");
    bool pattern = false;
    for (const PropertyField& f : summarize_selection({Command{solid}}).fields) {
        pattern = pattern || f.id == PropertyId::HatchPattern;
    }
    REQUIRE(pattern);

    GeometryEngine engine;
    engine.start();
    engine.submit(wipeout({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, 1));
    engine.submit(wipeout({{20, 0}, {30, 0}, {25, 8}}, 2));
    AddHatchCommand hatch = solid;
    hatch.loops = {{{40, 0}, {50, 0}, {50, 10}, {40, 10}}};
    hatch.group = 3;
    engine.submit(hatch);
    engine.submit(AddLineCommand{{0, 30}, {10, 30}, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    SelectFilterCommand f;
    f.kind = SelectFilterCommand::kWipeout;
    engine.submit(f);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    f.kind = static_cast<int>(EntityKind::Hatch); // the hatches that are not wipeouts
    engine.submit(f);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    // Select Similar from one wipeout: the other, not the hatch.
    engine.submit(ClearSelectionCommand{});
    engine.submit(SelectPickCommand{{5, 0}, 1.0, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(SelectSimilarCommand{0});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));

    // HATCHEDIT's colour leaves a wipeout as it is.
    engine.submit(HatchEditCommand{HatchEditCommand::Op::Color, Rgb{255, 0, 0}, false, {}, 5});
    io::Document doc = dump(engine, "musacad_wipeout_hatchedit.musa");
    for (const io::DocHatch& h : doc.hatches) {
        if (h.pattern_name == "WIPEOUT") {
            REQUIRE(h.props.color_by_layer());
        }
    }

    engine.submit(ListQueryCommand{{5, 0}, 1.0});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("WIPEOUT") != std::string::npos; }));

    // EXPLODE: the lines of its frame.
    engine.submit(ClearSelectionCommand{});
    engine.submit(SelectPickCommand{{5, 0}, 1.0, false, false});
    engine.submit(ExplodeSelectionCommand{6});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("xplode") != std::string::npos; }));
    doc = dump(engine, "musacad_wipeout_explode.musa");
    REQUIRE(doc.lines.size() == 5); // the four sides and the line drawn
    REQUIRE(doc.hatches.size() == 2);
    engine.stop();
}

TEST_CASE("ATTSYNC: references get their block's attributes, keeping their values; one undo step") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {60, 0}, 1});
    AddAttDefCommand a;
    a.text.pos = {2, 2};
    a.text.height = 2.5;
    a.text.content = "NO";
    a.prompt = "Number";
    a.def = "D-000";
    a.group = 1;
    AddAttDefCommand b = a;
    b.text.pos = {2, 8};
    b.text.content = "REV";
    b.prompt = "Revision";
    b.def = "A";
    engine.submit(a);
    engine.submit(b);
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 3; }));
    engine.submit(DefineBlockCommand{"TB", {0, 0}, 2});
    engine.submit(AddLineCommand{{0, 50}, {10, 50}, 3});
    engine.submit(SelectPickCommand{{5, 50}, 1.0, false, false});
    engine.submit(DefineBlockCommand{"PLAIN", {0, 50}, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.block_names.size() == 2; }));
    // A reference with fewer values than the block has attributes (as an older drawing can).
    engine.submit(AddInsertCommand{0, {100, 0}, 1.0, 1.0, 0.0, 5, {}, {"D-100"}});
    io::Document doc = dump(engine, "musacad_attsync1.musa");
    REQUIRE(insert_at(doc, 100) != nullptr);
    REQUIRE(insert_at(doc, 100)->attribs.size() == 1);

    engine.submit(AttSyncCommand{"tb", {}, 0.0, 6});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "ATTSYNC complete."; }));
    doc = dump(engine, "musacad_attsync2.musa");
    const io::DocInsert* in = insert_at(doc, 100);
    REQUIRE(in != nullptr);
    REQUIRE(in->attribs.size() == 2);
    REQUIRE(in->attribs[0].value == "D-100");
    REQUIRE(in->attribs[1].tag == "REV");
    REQUIRE(in->attribs[1].value == "A");
    engine.submit(UndoLastGroupCommand{});
    doc = dump(engine, "musacad_attsync3.musa");
    REQUIRE(insert_at(doc, 100)->attribs.size() == 1);

    // By the reference picked, and by a wild card.
    engine.submit(AttSyncCommand{"", {130, 0}, 1.0, 7});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "ATTSYNC complete."; }));
    doc = dump(engine, "musacad_attsync4.musa");
    REQUIRE(insert_at(doc, 100)->attribs.size() == 2);
    engine.submit(AttSyncCommand{"*", {}, 0.0, 8});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "ATTSYNC complete."; }));
    engine.submit(AttSyncCommand{"PLAIN", {}, 0.0, 9});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Block \"PLAIN\" has no attributes."; }));
    engine.submit(AttSyncCommand{"NOPE", {}, 0.0, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Block \"NOPE\" not found."; }));
    engine.submit(AttSyncCommand{"", {500, 500}, 1.0, 11});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "ATTSYNC: that is not a block reference."; }));
    engine.stop();
}

TEST_CASE("ATTSYNC's prompts: [?/Name/Select], the block confirmed; 3DOSMODE and UCSDETECT") {
    H h;
    h.run({"ATTSYNC"});
    REQUIRE(h.out.prompt == "Enter an option [?/Name/Select] <Select>: ");
    h.run({"N"});
    REQUIRE(h.out.prompt == "Enter name of block to sync or [?]: ");
    h.run({"TB"});
    REQUIRE(h.out.prompt == "ATTSYNC block TB? [Yes/No] <Yes>: ");
    h.run({""});
    REQUIRE(h.last<AttSyncCommand>() != nullptr);
    REQUIRE(h.last<AttSyncCommand>()->block == "TB");
    REQUIRE(!h.proc.has_active_command());

    h.run({"ATTSYNC", "N", "TB", "N"}); // not confirmed: nothing done
    REQUIRE(h.count<AttSyncCommand>() == 1);
    REQUIRE(!h.proc.has_active_command());
    h.run({"ATTSYNC", "N", "T*"}); // a wild card: no question
    REQUIRE(h.last<AttSyncCommand>()->block == "T*");
    h.run({"ATTSYNC", "", "110,0"}); // a point typed: the engine finds the reference
    REQUIRE(h.last<AttSyncCommand>()->block.empty());
    REQUIRE(h.last<AttSyncCommand>()->pick == Vec2{110, 0});

    h.run({"UCSDETECT", "0"});
    REQUIRE(musacad::command::ucsdetect() == 0);
    h.run({"3DOSMODE", "126"});
    REQUIRE(musacad::command::threedosmode() == 126);
    h.run({"UCSDETECT", "1", "3DOSMODE", "11"});
    REQUIRE(musacad::command::ucsdetect() == 1);
    REQUIRE(musacad::command::threedosmode() == 11);
}

TEST_CASE("HATCHEDIT's Origin, ADd boundaries and Remove boundaries") {
    GeometryEngine engine;
    engine.start();
    AddHatchCommand fill;
    fill.loops = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{4, 4}, {6, 4}, {6, 6}, {4, 6}}};
    fill.group = 1;
    engine.submit(fill);
    AddPolylineCommand frame{{{20, 0}, {30, 0}, {30, 10}, {20, 10}}, true, 2};
    engine.submit(frame);
    engine.submit(SelectPickCommand{{0, 5}, 0.5, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    HatchEditCommand remove{HatchEditCommand::Op::RemoveBoundaries, {}, true, {}, 3};
    remove.points = {{4, 5}};
    remove.radius = 0.5;
    engine.submit(remove);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Boundaries removed."; }));
    io::Document doc = dump(engine, "musacad_hatchedit_remove.musa");
    REQUIRE(doc.hatches.size() == 1);
    REQUIRE(doc.hatches[0].loops.size() == 1); // the island gone
    remove.points = {{0, 5}};
    remove.group = 4;
    engine.submit(remove); // the last boundary stays
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "No boundary of the hatch there."; }));

    HatchEditCommand add{HatchEditCommand::Op::AddAreas, {}, true, {}, 5};
    add.points = {{25, 5}};
    engine.submit(add);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Boundaries added."; }));
    doc = dump(engine, "musacad_hatchedit_add.musa");
    REQUIRE(doc.hatches.size() == 1);
    REQUIRE(doc.hatches[0].loops.size() == 2); // the area inside the polyline too

    HatchEditCommand origin{HatchEditCommand::Op::Origin, {}, true, {}, 6};
    origin.origin = {3, 4};
    engine.submit(origin);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Hatch origin set."; }));
    doc = dump(engine, "musacad_hatchedit_origin1.musa");
    REQUIRE(doc.hatches[0].pattern_origin == Vec2{3, 4});
    origin.origin_corner = 4; // the top right of the boundary's extents
    origin.group = 7;
    engine.submit(origin);
    doc = dump(engine, "musacad_hatchedit_origin2.musa");
    REQUIRE(doc.hatches[0].pattern_origin == Vec2{10, 10});
    engine.submit(UndoLastGroupCommand{});
    doc = dump(engine, "musacad_hatchedit_origin3.musa");
    REQUIRE(doc.hatches[0].pattern_origin == Vec2{3, 4});
    engine.stop();
}

TEST_CASE("HATCHEDIT's prompts for Origin, ADd boundaries and Remove boundaries") {
    H h;
    h.run({"HATCHEDIT", "5,5", "O"});
    REQUIRE(h.out.prompt == "[Use current origin/Set new origin/Default to boundary extents] <Use current origin>: ");
    h.run({"S", "3,4"});
    REQUIRE(h.last<HatchEditCommand>()->op == HatchEditCommand::Op::Origin);
    REQUIRE(h.last<HatchEditCommand>()->origin == Vec2{3, 4});
    h.run({"HATCHEDIT", "5,5", "O", "D", "C"});
    REQUIRE(h.last<HatchEditCommand>()->origin_corner == 5);
    h.run({"HATCHEDIT", "5,5", "AD"});
    REQUIRE(h.out.prompt == "Pick internal point or [Select objects/remove Boundaries]: ");
    h.run({"25,5"});
    REQUIRE(h.last<HatchEditCommand>()->op == HatchEditCommand::Op::AddAreas);
    REQUIRE(h.last<HatchEditCommand>()->points == std::vector<Vec2>{{25, 5}});
    h.run({"B"});
    REQUIRE(h.out.prompt == "Select Objects or [Add boundaries]: ");
    h.run({"4,5", ""});
    REQUIRE(h.last<HatchEditCommand>()->op == HatchEditCommand::Op::RemoveBoundaries);
    REQUIRE(!h.proc.has_active_command());
}

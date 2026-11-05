// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Express Tools on layouts (#77): ALIGNSPACE, a viewport panned and scaled so points of the
// model fall on points of the sheet; LAYOUTMERGE, layouts made one.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/command_registry.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/text/text_codes.hpp"

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
    std::filesystem::remove(p);
    engine.consume_snapshot();
    const std::uint64_t before = engine.snapshot().document_version;
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.document_version > before && !s.dirty; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    std::filesystem::remove(p);
    return doc;
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
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
};

EntityProps on_sheet(std::uint8_t space) {
    EntityProps p;
    p.set_space(space);
    return p;
}
} // namespace

TEST_CASE("#77 ALIGNSPACE pans and scales a viewport so the model's points fall on the sheet's") {
    GeometryEngine engine;
    engine.start();
    engine.submit(SetActiveSpaceCommand{0xFF, {}}); // the first layout
    REQUIRE(wait_until(engine, [](const auto& s) { return s.active_space != 0; }));
    const std::uint8_t sheet = engine.snapshot().active_space;
    // A viewport centred at (150, 100) on the sheet, 200 x 120, showing the model round (0, 0)
    // at 1:1.
    AddViewportCommand vp;
    vp.center = {150, 100};
    vp.width = 200;
    vp.height = 120;
    vp.view_center = {0, 0};
    vp.scale = 1.0;
    vp.props = on_sheet(sheet);
    vp.group = 1;
    engine.submit(vp);
    // Model (10, 0) -- seen at (160, 100) -- to (100, 80) on the sheet, and the model's 10
    // along to the sheet's 40 along: scale 4.
    AlignSpaceCommand a;
    a.model1 = {160, 100};
    a.model2 = Vec2{170, 100};
    a.paper1 = {100, 80};
    a.paper2 = Vec2{140, 80};
    a.group = 2;
    engine.submit(a);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("Viewport aligned") != std::string::npos; }));
    io::Document doc = dump(engine, "musacad_alignspace.musa");
    REQUIRE(doc.viewports.size() == 1);
    const io::DocViewport& v = doc.viewports[0];
    REQUIRE(v.scale == Approx(4.0));
    // The model point (10, 0) is now seen at (100, 80): centre + (m - view_centre) * scale.
    REQUIRE(v.center.x + (10.0 - v.view_center.x) * v.scale == Approx(100.0));
    REQUIRE(v.center.y + (0.0 - v.view_center.y) * v.scale == Approx(80.0));
    // Outside any viewport: nothing.
    a.model1 = {500, 500};
    a.group = 3;
    engine.submit(a);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("inside a viewport") != std::string::npos; }));
    engine.stop();
}

TEST_CASE("#77 ALIGNSPACE asks for the points as AutoCAD does, on a layout only") {
    H h;
    h.run({"ALIGNSPACE"});
    REQUIRE(h.out.lines.back().find("layout") != std::string::npos); // the harness is in model space
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("#77 LAYOUTMERGE: a layout's objects onto another's sheet, the layout deleted") {
    GeometryEngine engine;
    engine.start();
    engine.submit(LayoutCommand{LayoutCommand::Op::New, "Second", {}, 0});
    engine.submit(SetActiveSpaceCommand{0, "Second"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.active_space != 0; }));
    const std::uint8_t second = engine.snapshot().active_space;
    AddLineCommand l{{0, 0}, {50, 0}, 1};
    l.props = on_sheet(second);
    engine.submit(l);
    io::Document doc = dump(engine, "musacad_layoutmerge1.musa");
    std::string first_name;
    std::uint8_t first_id = 0;
    for (const Layout& lay : doc.layouts) {
        if (lay.name != "Second") {
            first_name = lay.name;
            first_id = lay.id;
            break;
        }
    }
    REQUIRE_FALSE(first_name.empty());
    engine.submit(LayoutCommand{LayoutCommand::Op::Merge, "Second", first_name, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("merged into") != std::string::npos; }));
    doc = dump(engine, "musacad_layoutmerge2.musa");
    REQUIRE(std::none_of(doc.layouts.begin(), doc.layouts.end(), [](const Layout& x) { return x.name == "Second"; }));
    REQUIRE(doc.lines.size() == 1);
    REQUIRE(doc.lines[0].props.space() == first_id); // where it was, on the other sheet
    REQUIRE(doc.lines[0].b == Vec2{50, 0});
    engine.stop();
}

TEST_CASE("#77 LAYOUTMERGE asks for the layouts and the destination") {
    H h;
    h.run({"LAYOUTMERGE"});
    REQUIRE(h.out.prompt == "Enter layout name(s) to merge, separated by commas, or [*] <*>: ");
    h.run({""}); // the harness has no layouts: nothing to merge
    REQUIRE(h.out.lines.back() == "No layouts to merge.");
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("#77 SYSVDLG is a command: it opens the System Variables window") {
    const musacad::command::CommandRegistry reg = musacad::command::CommandRegistry::make_default();
    REQUIRE(reg.contains("SYSVDLG"));
    REQUIRE(reg.contains("ALIGNSPACE"));
    REQUIRE(reg.contains("LAYOUTMERGE"));
}

TEST_CASE("#77 ARCTEXT: the text along the arc, a character a text turned to it, grouped") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddArcCommand{{0, 0}, 10.0, 0.0, kPi, 1}); // the upper half circle
    ArcTextCommand c;
    c.pick = {0, 10};
    c.pick_radius = 0.5;
    c.content = "AB";
    c.height = 2.0;
    c.align = 3; // centred
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("along the arc") != std::string::npos; }));
    io::Document doc = dump(engine, "musacad_arctext.musa");
    REQUIRE(doc.texts.size() == 2);
    const io::DocText& a = doc.texts[0].content == "A" ? doc.texts[0] : doc.texts[1];
    const io::DocText& b = doc.texts[0].content == "A" ? doc.texts[1] : doc.texts[0];
    // Outside the arc, reading clockwise over the top: A left of the top, B right of it, both
    // on the arc and turned to it.
    REQUIRE(std::hypot(a.pos.x, a.pos.y) == Approx(10.0));
    REQUIRE(a.pos.x < 0.0);
    REQUIRE(b.pos.x > 0.0);
    REQUIRE(a.pos.x == Approx(-b.pos.x));
    REQUIRE(a.rotation == Approx(std::atan2(a.pos.y, a.pos.x) - kPi / 2.0));
    REQUIRE(doc.groups.size() == 1);
    REQUIRE(doc.groups[0].members.size() == 2);
    engine.stop();
}

TEST_CASE("#77 SUPERHATCH: an area filled with a block's copies, cut at the boundary; or a wipeout") {
    GeometryEngine engine;
    engine.start();
    // The tile: a 2 x 2 square, made a block (its reference stays far away).
    engine.submit(AddPolylineCommand{{{100, 100}, {102, 100}, {102, 102}, {100, 102}}, true, 1});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(DefineBlockCommand{"TILE", {100, 100}, 2});
    // The area: 5 x 5.
    engine.submit(AddPolylineCommand{{{0, 0}, {5, 0}, {5, 5}, {0, 5}}, true, 3});
    SuperHatchCommand c;
    c.at = {2.5, 2.5};
    c.block = "TILE";
    c.group = 4;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("Superhatch made") != std::string::npos; }));
    io::Document doc = dump(engine, "musacad_superhatch.musa");
    // Four whole copies (and the first reference), the rest cut to lines inside the area.
    REQUIRE(doc.inserts.size() == 5);
    REQUIRE_FALSE(doc.lines.empty());
    for (const io::DocLine& l : doc.lines) {
        for (const Vec2 p : {l.a, l.b}) {
            REQUIRE(p.x >= -1e-9);
            REQUIRE(p.x <= 5.0 + 1e-9);
            REQUIRE(p.y >= -1e-9);
            REQUIRE(p.y <= 5.0 + 1e-9);
        }
    }
    REQUIRE(doc.groups.size() == 1);
    // Wipeout.
    SuperHatchCommand w;
    w.at = {2.5, 2.5};
    w.wipeout = true;
    w.group = 5;
    engine.submit(w);
    doc = dump(engine, "musacad_superhatch_wipeout.musa");
    REQUIRE(std::count_if(doc.hatches.begin(), doc.hatches.end(),
                          [](const io::DocHatch& h) { return h.pattern_name == "WIPEOUT"; }) == 1);
    engine.stop();
}

TEST_CASE("#77 SUPERHATCH asks for the pattern type, the block, its scale and rotation, then points") {
    H h;
    h.run({"SUPERHATCH"});
    REQUIRE(h.out.prompt == "Enter a pattern type [Block/Wipeout/Select existing] <Block>: ");
    h.run({"W", "1,1"});
    REQUIRE(h.last<SuperHatchCommand>()->wipeout);
    REQUIRE(h.last<SuperHatchCommand>()->at == Vec2{1, 1});
    h.run({"", "SUPERHATCH", "S", "5,5"});
    REQUIRE(h.out.prompt == "Specify an internal point: ");
    h.run({"6,6", ""});
    REQUIRE(h.last<SuperHatchCommand>()->tile_pick == std::optional<Vec2>{Vec2{5, 5}});
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("#77 RTEXT's fields: a DIESEL expression's value, and a file's text read again when it changes") {
    text::FieldContext fc;
    fc.filename = "part.musa";
    fc.path = "/work/drawings/part.musa";
    fc.login = "pranay";
    text::set_field_context(fc);
    REQUIRE(text::expand_fields("%<\\AcDiesel $(upper,$(getvar,dwgname))>%") == "PART.MUSA");
    REQUIRE(text::expand_fields("%<\\AcDiesel $(getvar,dwgprefix)>%") == "/work/drawings/");
    REQUIRE(text::expand_fields("%<\\AcDiesel Sheet $(+,1,2) of 9>%") == "Sheet 3 of 9");
    REQUIRE(text::expand_fields("%<\\AcDiesel $(if,$(eq,a,a),yes,no)>%") == "yes");
    REQUIRE(text::expand_fields("%<\\AcDiesel $(substr,ABCDEF,2,3)>%") == "BCD");
    REQUIRE(text::expand_fields("%<\\AcDiesel $(nosuch,1)>%") == "$(nosuch,?" "?)");

    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_rtext.txt";
    {
        std::ofstream(p) << "REV A\r\nCHECKED\r\n";
    }
    const std::string field = "%<\\RText \"" + p.string() + "\">%";
    REQUIRE(text::expand_fields(field) == "REV A\nCHECKED");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::ofstream(p) << "REV B, a longer line\n";
    }
    REQUIRE(text::expand_fields(field) == "REV B, a longer line");
    std::filesystem::remove(p);
    REQUIRE(text::expand_fields(field).find("file not found") != std::string::npos);
    text::set_field_context(text::FieldContext{});
}

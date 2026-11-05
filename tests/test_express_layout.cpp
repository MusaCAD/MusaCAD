// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Express Tools on layouts (#77): ALIGNSPACE, a viewport panned and scaled so points of the
// model fall on points of the sheet; LAYOUTMERGE, layouts made one.

#include <chrono>
#include <filesystem>
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

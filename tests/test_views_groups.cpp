// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Issue #33: DONUT, named views (VIEW) and groups (GROUP / UNGROUP / PICKSTYLE).

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
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
struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string&) override {}
    void set_prompt(const std::string&) override {}
};
struct StubView : musacad::command::ViewControl {
    Vec2 center{5, 5};
    double scale = 2.0;
    int w = 800;
    int h = 600;
    Vec2 set_center{};
    double set_scale = 0.0;
    int sets = 0;
    void zoom_extents() override {}
    void zoom_scale(double) override {}
    bool current_view(Vec2& c, double& s) override {
        c = center;
        s = scale;
        return true;
    }
    void set_view(Vec2 c, double s) override {
        set_center = c;
        set_scale = s;
        ++sets;
    }
    bool viewport_size(int& ww, int& hh) const override {
        ww = w;
        hh = h;
        return true;
    }
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
    template <class T>
    int count() const {
        int n = 0;
        for (const Command& c : cmds) {
            n += std::holds_alternative<T>(c) ? 1 : 0;
        }
        return n;
    }
};
} // namespace

TEST_CASE("#33 DONUT: a wide two-arc polyline at each centre; a zero hole is a disc; bad sizes refused") {
    // AutoCAD's donut: two half circles on the mean diameter, as wide as the ring.
    ProcHarness h;
    h.proc.submit_line("DO");
    h.proc.submit_line("0.5");   // inside
    h.proc.submit_line("1");     // outside
    h.proc.submit_line("10,10");
    h.proc.submit_line("30,10");
    h.proc.submit_line("");      // exit
    REQUIRE(h.count<AddPolylineCommand>() == 2);
    REQUIRE(h.count<AddHatchCommand>() == 0);
    const auto* d = h.last<AddPolylineCommand>();
    REQUIRE(d->closed);
    REQUIRE(d->points.size() == 2);
    REQUIRE(d->bulges == std::vector<double>{1.0, 1.0});
    REQUIRE(std::hypot(d->points[0].x - 30.0, d->points[0].y - 10.0) == Approx(0.375));
    REQUIRE(d->widths == std::vector<double>(4, 0.25));
    REQUIRE(!h.proc.has_active_command());

    ProcHarness h2;
    h2.proc.submit_line("DONUT");
    h2.proc.submit_line("0");
    h2.proc.submit_line("4");
    h2.proc.submit_line("0,0");
    h2.proc.submit_line("");
    REQUIRE(h2.last<AddPolylineCommand>()->widths == std::vector<double>(4, 2.0));
    REQUIRE(std::hypot(h2.last<AddPolylineCommand>()->points[0].x, 0.0) == Approx(1.0));

    ProcHarness h3;
    h3.proc.submit_line("DONUT");
    h3.proc.submit_line("5");
    h3.proc.submit_line("3"); // outside <= inside: refused, still at the prompt
    REQUIRE(h3.proc.has_active_command());
    REQUIRE(h3.count<AddPolylineCommand>() == 0);
}

TEST_CASE("#33 VIEW: Save reads the camera, Restore sets it, Window frames a box, Delete submits") {
    ProcHarness h;
    h.proc.submit_line("VIEW");
    h.proc.submit_line("S");
    h.proc.submit_line("front");
    const auto* sv = h.last<SaveNamedViewCommand>();
    REQUIRE(sv != nullptr);
    REQUIRE(sv->view.name == "front");
    REQUIRE(sv->view.center == Vec2{5, 5});
    REQUIRE(sv->view.scale == Approx(2.0));

    h.proc.set_named_views({NamedView{"front", {7, 7}, 3.0}});
    h.proc.submit_line("V");
    h.proc.submit_line("R");
    h.proc.submit_line("front");
    REQUIRE(h.view.sets == 1);
    REQUIRE(h.view.set_center == Vec2{7, 7});
    REQUIRE(h.view.set_scale == Approx(3.0));
    h.proc.submit_line("V");
    h.proc.submit_line("R");
    h.proc.submit_line("missing");
    REQUIRE(h.view.sets == 1); // not found: nothing set

    h.proc.submit_line("VIEW");
    h.proc.submit_line("W");
    h.proc.submit_line("0,0");
    h.proc.submit_line("100,50");
    h.proc.submit_line("win");
    const auto* wv = h.last<SaveNamedViewCommand>();
    REQUIRE(wv->view.name == "win");
    REQUIRE(wv->view.center == Vec2{50, 25});
    REQUIRE(wv->view.scale == Approx(8.0)); // min(800/100, 600/50)

    h.proc.submit_line("VIEW");
    h.proc.submit_line("D");
    h.proc.submit_line("front");
    REQUIRE(h.last<DeleteNamedViewCommand>()->name == "front");
    h.proc.submit_line("VIEW");
    h.proc.submit_line("?");
    h.proc.submit_line("");
    REQUIRE(!h.proc.has_active_command());
}

TEST_CASE("#33 named views live in the drawing: engine table, snapshot, and the file") {
    GeometryEngine engine;
    engine.start();
    engine.submit(SaveNamedViewCommand{NamedView{"plan", {10, 20}, 4.0}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.named_views.size() == 1; }));
    REQUIRE(engine.snapshot().named_views[0].name == "plan");
    engine.submit(SaveNamedViewCommand{NamedView{"plan", {11, 21}, 5.0}}); // replace by name
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.named_views.size() == 1 && s.named_views[0].scale == Approx(5.0);
    }));
    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_views.musa";
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("Saved", 0) == 0; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    REQUIRE(doc.views.size() == 1);
    REQUIRE(doc.views[0].center == Vec2{11, 21});
    std::filesystem::remove(p);
    engine.submit(DeleteNamedViewCommand{"plan"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.named_views.empty(); }));
    engine.stop();
}

TEST_CASE("#33 GROUP: picking a member selects the group; PICKSTYLE 0 and UNGROUP switch that off; groups survive the file") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});      // not in the group
    engine.submit(AddCircleCommand{{50, 50}, 5.0, 2});      // member
    engine.submit(AddLineCommand{{100, 0}, {110, 0}, 3});   // member
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 20; }));
    engine.submit(SelectPickCommand{{55, 50}, 1.0, false, false});
    engine.submit(SelectPickCommand{{105, 0}, 1.0, true, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(CreateGroupCommand{"", ""});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Group \"*A1\" has been created.") != std::string::npos;
    }));
    REQUIRE(engine.snapshot().group_names.size() == 1);

    // Pick the circle alone: the whole group comes with it.
    engine.submit(SelectPickCommand{{55, 50}, 1.0, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    // The line outside the group selects alone.
    engine.submit(SelectPickCommand{{5, 0}, 1.0, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    // PICKSTYLE 0: members select individually.
    engine.submit(SetPickStyleCommand{false});
    engine.submit(SelectPickCommand{{55, 50}, 1.0, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.selection.size() == 1 && s.selection[0].kind == EntityKind::Circle;
    }));
    engine.submit(SetPickStyleCommand{true});

    // The group survives save and open (members re-resolved by kind and order).
    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_groups.musa";
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("Saved", 0) == 0; }));
    engine.submit(NewDocumentCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    engine.submit(OpenDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 20 && s.group_names.size() == 1; }));
    std::filesystem::remove(p);
    engine.submit(SelectPickCommand{{105, 0}, 1.0, false, false}); // the member line
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));

    // UNGROUP by pick: members select alone again.
    engine.submit(UngroupCommand{{}, {55, 50}, 1.0, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.group_names.empty(); }));
    engine.submit(SelectPickCommand{{55, 50}, 1.0, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.stop();
}

TEST_CASE("#33 GROUP / UNGROUP / PICKSTYLE command flows") {
    {
        ProcHarness h;
        h.proc.set_selection_count(2);
        h.proc.submit_line("GROUP");
        h.proc.submit_line("N");
        h.proc.submit_line("walls");
        h.proc.submit_line("D");
        h.proc.submit_line("outer walls");
        h.proc.submit_line("");
        const auto* g = h.last<CreateGroupCommand>();
        REQUIRE(g != nullptr);
        REQUIRE(g->name == "walls");
        REQUIRE(g->description == "outer walls");
    }
    { // Nothing selected: no group.
        ProcHarness h;
        h.proc.submit_line("G");
        h.proc.submit_line("");
        REQUIRE(h.count<CreateGroupCommand>() == 0);
        REQUIRE(!h.proc.has_active_command());
    }
    {
        ProcHarness h;
        h.proc.submit_line("UNGROUP");
        h.proc.submit_line("N");
        h.proc.submit_line("walls");
        const auto* u = h.last<UngroupCommand>();
        REQUIRE(u != nullptr);
        REQUIRE(u->by_name);
        REQUIRE(u->name == "walls");
        h.proc.submit_line("UNGROUP");
        h.proc.submit_line("5,5");
        REQUIRE(!h.last<UngroupCommand>()->by_name);
        REQUIRE(h.last<UngroupCommand>()->pick == Vec2{5, 5});
    }
    {
        ProcHarness h;
        h.proc.submit_line("PICKSTYLE");
        h.proc.submit_line("0");
        REQUIRE(h.last<SetPickStyleCommand>() != nullptr);
        REQUIRE(!h.last<SetPickStyleCommand>()->group_select);
    }
}

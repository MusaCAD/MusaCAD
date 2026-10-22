// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// WIPEOUT masks, FIELD text codes and GRADIENT hatches (#33): the snapshot's mask pass
// and WIPEOUTFRAME, field expansion at layout, banded gradient fills, the native v27 and
// DXF forms, the engine's polyline-to-wipeout path, and the command flows.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/block_resolve.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/hatch.hpp"
#include "musacad/core/hatch_pattern.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/osnap.hpp"
#include "musacad/core/properties_registry.hpp"
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/scene_snapshot.hpp"
#include "musacad/core/spatial_grid.hpp"
#include "musacad/core/text/text_codes.hpp"

using namespace musacad::core;
using namespace musacad::core::io;

namespace {
struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string&) override {}
    std::vector<std::string> lines;
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
double triangle_area_sum(const std::vector<Vec2>& tris) {
    double a = 0.0;
    for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
        const Vec2 p = tris[i];
        const Vec2 q = tris[i + 1];
        const Vec2 r = tris[i + 2];
        a += std::abs((q.x - p.x) * (r.y - p.y) - (r.x - p.x) * (q.y - p.y)) * 0.5;
    }
    return a;
}
} // namespace

TEST_CASE("#33 FIELD: %<...>% codes expand from the field context; unknown fields show ####") {
    text::set_field_context({"2026-09-08", "01:23", "plan.mcad", "pranay"});
    CHECK(text::expand_fields("Plotted %<Date>% at %<time>%") == "Plotted 2026-09-08 at 01:23");
    CHECK(text::expand_fields("%<Filename>% by %<Login>%") == "plan.mcad by pranay");
    CHECK(text::expand_fields("%<Bogus>%") == "####");
    CHECK(text::expand_fields("no fields here %%d") == "no fields here %%d");
    CHECK(text::expand_fields("unterminated %<Date") == "unterminated %<Date");
    // Fields expand before the %% codes, so both work together in one string.
    CHECK(text::substitute_text("%<Date>%%%d") == std::string("2026-09-08") + "\xC2\xB0");

    // The context a drawing gets: today's date (YYYY-MM-DD), HH:MM, the file's own name.
    const text::FieldContext fc = text::make_field_context("/home/x/plans/site plan.musa");
    CHECK(fc.filename == "site plan.musa");
    CHECK(fc.date.size() == 10);
    CHECK(fc.date[4] == '-');
    CHECK(fc.time.size() == 5);
    CHECK(fc.time[2] == ':');
    CHECK(text::make_field_context("").filename == "Drawing1");
    text::set_field_context(fc);
    CHECK(text::expand_fields("%<Filename>%") == "site plan.musa");
}

TEST_CASE("#33 WIPEOUT: the mask goes to the wipeout pass, never the fills; frames follow WIPEOUTFRAME") {
    GeometryStore store;
    NativeKernel2D kernel;
    store.add_hatch({{{0, 0}, {10, 0}, {10, 10}, {0, 10}}}, "WIPEOUT", 1.0, 0.0, {});
    RenderSnapshot snap;
    build_render_snapshot(store, kernel, snap, 0.01, 1.0);
    CHECK(snap.wipeout_vertices.size() == 6); // two triangles
    CHECK(triangle_area_sum(snap.wipeout_vertices) == 100.0);
    CHECK(snap.fill_vertices.empty());
    CHECK(snap.wipeout_frames);
    CHECK(snap.line_vertices.size() == 8); // the frame: four edges

    store.set_wipeout_frames(false);
    RenderSnapshot bare;
    build_render_snapshot(store, kernel, bare, 0.01, 1.0);
    CHECK_FALSE(bare.wipeout_frames);
    CHECK(bare.line_vertices.empty());
    CHECK(bare.wipeout_vertices.size() == 6);
}

TEST_CASE("#33 GRADIENT: banded fills from the entity colour to the second colour, covering the region exactly") {
    GeometryStore store;
    NativeKernel2D kernel;
    store.add_hatch({{{0, 0}, {10, 0}, {10, 10}, {0, 10}}}, "GRADIENT", 1.0, 0.0, {}, {}, Rgb{0, 0, 0});
    RenderSnapshot snap;
    build_render_snapshot(store, kernel, snap, 0.01, 1.0);
    REQUIRE(snap.fill_batches.size() >= 8);
    // The bands cover the square; each overlaps the next by 3% of its width (the seam
    // guard), so the triangle sum runs a little over the region's 100.
    const double area = triangle_area_sum(snap.fill_vertices);
    CHECK(area >= 100.0 - 1e-6);
    CHECK(area <= 100.0 + 23.0 * (10.0 / 24.0 * 0.03) * 10.0 + 1e-6);
    // Bands run along +X (angle 0): the darker a batch (toward the second colour,
    // black), the further right it lies, each band no wider than a 24th of the square.
    // Batches are keyed by colour, so order them by brightness first.
    struct Band {
        int sum;
        double xmin;
        double xmax;
    };
    std::vector<Band> bands;
    for (const ColorBatch& b : snap.fill_batches) {
        Band band{b.color.r + b.color.g + b.color.b, 1e9, -1e9};
        for (std::uint32_t i = b.first; i < b.first + b.count; ++i) {
            band.xmin = std::min(band.xmin, snap.fill_vertices[i].x);
            band.xmax = std::max(band.xmax, snap.fill_vertices[i].x);
        }
        CHECK(band.xmax - band.xmin <= 10.0 / 24.0 * 1.03 + 1e-9); // a band plus its seam overlap
        bands.push_back(band);
    }
    std::sort(bands.begin(), bands.end(), [](const Band& a, const Band& b) { return a.sum > b.sum; });
    for (std::size_t i = 1; i < bands.size(); ++i) {
        CHECK(bands[i].xmin >= bands[i - 1].xmin);
    }
    CHECK(bands.front().xmin == 0.0);
    CHECK(bands.back().xmax == 10.0);
    CHECK(bands.back().sum <= 18);
    for (const ColorBatch& b : snap.fill_batches) {
        CHECK(b.computed_color); // PLOT keeps band colours as they are
    }
}

TEST_CASE("#33 native v27 keeps the second colour and WIPEOUTFRAME; the store carries color2 through the document") {
    Document doc;
    DocHatch h;
    h.loops = {{{0, 0}, {10, 0}, {10, 10}}};
    h.pattern_name = "GRADIENT";
    h.pattern_angle = 0.5;
    h.color2 = Rgb{10, 20, 30};
    doc.hatches.push_back(h);
    doc.wipeout_frames = false;
    Document rt;
    REQUIRE(parse_native(serialize_native(doc), rt).ok);
    REQUIRE(rt.hatches.size() == 1);
    CHECK(rt.hatches[0] == doc.hatches[0]);
    CHECK_FALSE(rt.wipeout_frames);

    GeometryStore store;
    populate_store(store, rt);
    CHECK_FALSE(store.wipeout_frames());
    REQUIRE(store.hatches().live_count() == 1);
    const Document again = document_from_store(store);
    REQUIRE(again.hatches.size() == 1);
    CHECK(again.hatches[0].color2.r == 10);
    CHECK(again.hatches[0].color2.g == 20);
    CHECK(again.hatches[0].color2.b == 30);
    CHECK_FALSE(again.wipeout_frames);
}

TEST_CASE("#33 DXF: a gradient hatch round-trips through AutoCAD's gradient block; a wipeout as a WIPEOUT") {
    Document doc;
    DocHatch g;
    g.loops = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    g.pattern_name = "GRADIENT";
    g.pattern_angle = 0.75;
    g.color2 = Rgb{0, 128, 255};
    doc.hatches.push_back(g);
    DocHatch w = g;
    w.pattern_name = "WIPEOUT";
    w.color2 = Rgb{};
    doc.hatches.push_back(w);

    const std::string dxf = serialize_dxf(doc);
    CHECK(dxf.find("LINEAR") != std::string::npos);
    CHECK(dxf.find("\nWIPEOUT\n") != std::string::npos);
    CHECK(dxf.find("GRADIENT") == std::string::npos);
    Document rt;
    REQUIRE(parse_dxf(dxf, rt).ok);
    REQUIRE(rt.hatches.size() == 2);
    const DocHatch& gr = rt.hatches[0].pattern_name == "GRADIENT" ? rt.hatches[0] : rt.hatches[1];
    const DocHatch& wo = rt.hatches[0].pattern_name == "GRADIENT" ? rt.hatches[1] : rt.hatches[0];
    CHECK(gr.pattern_name == "GRADIENT");
    CHECK(gr.color2.r == 0);
    CHECK(gr.color2.g == 128);
    CHECK(gr.color2.b == 255);
    CHECK(std::abs(gr.pattern_angle - 0.75) < 1e-9);
    CHECK(gr.loops == g.loops);
    CHECK(wo.pattern_name == "WIPEOUT");
    CHECK(wo.loops == g.loops);
}

TEST_CASE("#33 engine: WIPEOUT [Polyline] masks a closed polyline (optionally erasing it); frames toggle; undo restores") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {20, 0}, {20, 20}, {0, 20}}, true, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));

    engine.submit(WipeoutFromPolylineCommand{{10, 0}, 0.5, true, 2});
    // The polyline is gone; the mask's frame (four edges) is all the line work left.
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.wipeout_vertices.size() == 6 && s.line_vertices.size() == 8 && s.wipeout_frames;
    }));
    engine.submit(SetWipeoutFramesCommand{false});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.wipeout_frames && s.line_vertices.empty(); }));
    engine.submit(SetWipeoutFramesCommand{true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.wipeout_frames && s.line_vertices.size() == 8; }));

    // Undo the wipeout group: the mask goes, the polyline is back.
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.wipeout_vertices.empty() && s.line_vertices.size() == 8;
    }));
    // An open polyline is refused.
    engine.submit(AddPolylineCommand{{{30, 0}, {40, 0}, {40, 10}}, false, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 12; }));
    engine.submit(WipeoutFromPolylineCommand{{35, 0}, 0.5, false, 4});
    engine.submit(AddLineCommand{{50, 0}, {60, 0}, 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 14; }));
    CHECK(engine.snapshot().wipeout_vertices.empty());
    engine.stop();
}

TEST_CASE("#33 commands: WIPEOUT points / Frames / Polyline, FIELD, and HATCH [Gradient]") {
    ProcHarness h;
    h.proc.submit_line("WIPEOUT");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,0");
    h.proc.submit_line("10,10");
    h.proc.submit_line(""); // Enter closes
    const auto* w = h.last<AddHatchCommand>();
    REQUIRE(w != nullptr);
    CHECK(w->pattern_name == "WIPEOUT");
    REQUIRE(w->loops.size() == 1);
    CHECK(w->loops[0].size() == 3);

    h.proc.submit_line("WIPEOUT");
    h.proc.submit_line("F");
    h.proc.submit_line("OFF");
    const auto* f = h.last<SetWipeoutFramesCommand>();
    REQUIRE(f != nullptr);
    CHECK_FALSE(f->on);

    h.proc.submit_line("WIPEOUT");
    h.proc.submit_line("P");
    h.proc.submit_line("5,5");
    h.proc.submit_line("Y");
    const auto* wp = h.last<WipeoutFromPolylineCommand>();
    REQUIRE(wp != nullptr);
    CHECK(wp->erase);
    CHECK(wp->pick == Vec2{5, 5});

    h.proc.submit_line("FIELD");
    h.proc.submit_line("D");
    h.proc.submit_line("1,2");
    h.proc.submit_line("3");
    h.proc.submit_line("");
    const auto* t = h.last<AddTextCommand>();
    REQUIRE(t != nullptr);
    CHECK(t->content == "%<Date>%");
    CHECK(t->height == 3.0);
    CHECK(t->pos == Vec2{1, 2});

    h.proc.submit_line("HATCH");
    h.proc.submit_line("G");
    h.proc.submit_line("0,0,255");
    h.proc.submit_line("90");
    h.proc.submit_line("5,5");
    const auto* g = h.last<HatchPickPointCommand>();
    REQUIRE(g != nullptr);
    CHECK(g->pattern_name == "GRADIENT");
    CHECK(g->color2.r == 0);
    CHECK(g->color2.g == 0);
    CHECK(g->color2.b == 255);
    CHECK(std::abs(g->pattern_angle - kPi / 2.0) < 1e-9);
    h.proc.submit_line("");
}

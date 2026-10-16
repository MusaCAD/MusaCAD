// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Text styles drawn Backwards and Upside down (#62); TXTEXP (#77); user-defined hatching (#41).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/font_engine.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/hatch_pattern.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad::core;

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

// "TTF" is an outline face whose every glyph is one unit square, a cap height wide.
struct SquareFont final : IFontEngine {
    bool is_outline_font(std::string_view name) const override { return name == "TTF"; }
    double advance(std::string_view, std::string_view text, double height) const override {
        return static_cast<double>(text.size()) * 1.5 * height;
    }
    void glyph_fills(std::string_view, std::string_view text, Vec2 origin, double height, double,
                     std::vector<Vec2>& tris) const override {
        for (std::size_t i = 0; i < text.size(); ++i) {
            const double x = origin.x + static_cast<double>(i) * 1.5 * height;
            tris.insert(tris.end(), {{x, origin.y}, {x + height, origin.y}, {x, origin.y + height}});
        }
    }
    void glyph_outlines(std::string_view, std::string_view text, Vec2 origin, double height, double,
                        std::vector<std::vector<Vec2>>& rings) const override {
        for (std::size_t i = 0; i < text.size(); ++i) {
            const double x = origin.x + static_cast<double>(i) * 1.5 * height;
            rings.push_back({{x, origin.y}, {x + height, origin.y}, {x + height, origin.y + height}, {x, origin.y + height}});
        }
    }
    std::vector<std::string> available() const override { return {"Standard", "TTF"}; }
    std::string substitute(std::string_view) const override { return {}; }
};

std::vector<Vec2> sorted(std::vector<Vec2> v) {
    std::sort(v.begin(), v.end(), [](Vec2 a, Vec2 b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
    return v;
}
} // namespace

TEST_CASE("TXTEXP turns text into polylines along its strokes, or round an outline font's glyphs") {
    SquareFont fonts;
    GeometryEngine engine;
    engine.set_font_engine(&fonts);
    engine.start();
    AddTextCommand t;
    t.pos = {0, 0};
    t.height = 5.0;
    t.content = "%%uHI";
    t.group = 1;
    engine.submit(t);
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    const std::vector<Vec2> drawn = sorted(engine.snapshot().line_vertices);
    engine.submit(SelectAllCommand{});
    engine.submit(TextExplodeCommand{2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.starts_with("1 text exploded into "); }));
    // The same strokes (the underline among them), now polylines.
    const std::vector<Vec2> exploded = sorted(engine.snapshot().line_vertices);
    REQUIRE(exploded.size() == drawn.size());
    for (std::size_t i = 0; i < drawn.size(); ++i) {
        REQUIRE(exploded[i].x == Catch::Approx(drawn[i].x));
        REQUIRE(exploded[i].y == Catch::Approx(drawn[i].y));
    }
    AddTextCommand tt = t;
    tt.pos = {0, 50};
    tt.content = "AB";
    tt.font = "TTF";
    tt.group = 3;
    engine.submit(ClearSelectionCommand{});
    engine.submit(tt);
    engine.submit(SelectWindowCommand{{-1, 49}, {100, 60}, false});
    engine.submit(TextExplodeCommand{4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "1 text exploded into 2 polylines."; }));
    engine.stop();
}

TEST_CASE("A text style drawn Backwards or Upside down mirrors its text about the insertion point") {
    GeometryEngine engine;
    engine.start();
    TextStyle back;
    back.name = "Back";
    back.backwards = true;
    engine.submit(SetTextStyleCommand{back, true});
    AddTextCommand t;
    t.pos = {0, 0};
    t.height = 5.0;
    t.content = "ABC";
    t.style = "Back";
    t.group = 1;
    engine.submit(t);
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    const auto& v = engine.snapshot().line_vertices;
    REQUIRE(std::all_of(v.begin(), v.end(), [](Vec2 p) { return p.x <= 1e-9; })); // to the left
    REQUIRE(std::any_of(v.begin(), v.end(), [](Vec2 p) { return p.x < -5.0; }));
    TextStyle down;
    down.name = "Down";
    down.upside_down = true;
    engine.submit(SetTextStyleCommand{down, true});
    AddTextCommand t2 = t;
    t2.pos = {0, 100};
    t2.style = "Down";
    t2.group = 2;
    engine.submit(t2);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return std::any_of(s.line_vertices.begin(), s.line_vertices.end(), [](Vec2 p) { return p.y < 99.0; });
    }));
    const auto& w = engine.snapshot().line_vertices;
    REQUIRE(std::none_of(w.begin(), w.end(), [](Vec2 p) { return p.y > 100.0 + 1e-9; })); // below the baseline
    engine.stop();

    io::Document doc;
    doc.text_styles = {back, down};
    io::Document native;
    REQUIRE(io::parse_native(io::serialize_native(doc), native).ok);
    REQUIRE(native.text_styles[0].backwards);
    REQUIRE(native.text_styles[1].upside_down);
    io::Document dxf;
    REQUIRE(io::parse_dxf(io::serialize_dxf(doc), dxf).ok);
    const auto find = [&](const char* n) {
        return *std::find_if(dxf.text_styles.begin(), dxf.text_styles.end(), [&](const TextStyle& s) { return s.name == n; });
    };
    REQUIRE(find("Back").backwards);
    REQUIRE_FALSE(find("Back").upside_down);
    REQUIRE(find("Down").upside_down);
}

TEST_CASE("PEDIT Ltype gen: off, a polyline's dashes start again at every vertex; kept in the drawing and DXF") {
    GeometryEngine engine;
    engine.start();
    EntityProps dashed;
    dashed.linetype = Linetype::Dashed;
    dashed.set_linetype_by_layer(false);
    AddPolylineCommand pl;
    pl.points = {{0, 0}, {7, 0}, {7, 7}, {14, 7}, {14, 14}};
    pl.props = dashed;
    pl.group = 1;
    engine.submit(pl);
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    const std::size_t on = engine.snapshot().line_vertices.size();
    engine.submit(PeditCommand{{3, 0}, 0.5, 22, {0, 0}, {}, 2}); // Ltype gen off
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Linetype generation off."; }));
    REQUIRE(wait_until(engine, [on](const auto& s) { return !s.line_vertices.empty() && s.line_vertices.size() != on; }));
    engine.stop();

    io::Document doc;
    io::DocPolyline p;
    p.points = {{0, 0}, {10, 0}};
    p.ltype_gen = false;
    doc.polylines.push_back(p);
    io::Document native;
    REQUIRE(io::parse_native(io::serialize_native(doc), native).ok);
    REQUIRE_FALSE(native.polylines[0].ltype_gen);
    io::Document dxf;
    REQUIRE(io::parse_dxf(io::serialize_dxf(doc), dxf).ok);
    REQUIRE_FALSE(dxf.polylines[0].ltype_gen);
    doc.polylines[0].ltype_gen = true;
    REQUIRE(io::parse_dxf(io::serialize_dxf(doc), dxf).ok);
    REQUIRE(dxf.polylines.back().ltype_gen);
}

TEST_CASE("User-defined hatching: lines a spacing apart, crossed when double, through DXF as AutoCAD's type 0") {
    const hatch::Pattern* one = hatch::builtin_pattern("_USER");
    const hatch::Pattern* two = hatch::builtin_pattern("_USERDOUBLE");
    REQUIRE(one != nullptr);
    REQUIRE(two != nullptr);
    REQUIRE(one->lines.size() == 1);
    REQUIRE(two->lines.size() == 2);
    const auto& names = hatch::builtin_pattern_names();
    REQUIRE(std::find(names.begin(), names.end(), "_USER") == names.end()); // not offered by name

    io::Document doc;
    for (const char* name : {"_USER", "_USERDOUBLE", "ANSI31"}) {
        io::DocHatch h;
        h.loops = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
        h.pattern_name = name;
        h.pattern_scale = 2.0;
        h.pattern_angle = 0.5;
        doc.hatches.push_back(h);
    }
    const std::string dxf = io::serialize_dxf(doc);
    io::Document back;
    REQUIRE(io::parse_dxf(dxf, back).ok);
    REQUIRE(back.hatches.size() == 3);
    REQUIRE(back.hatches[0].pattern_name == "_USER");
    REQUIRE(back.hatches[1].pattern_name == "_USERDOUBLE");
    REQUIRE(back.hatches[2].pattern_name == "ANSI31");
    REQUIRE(back.hatches[1].pattern_scale == Catch::Approx(2.0));
    REQUIRE(back.hatches[1].pattern_angle == Catch::Approx(0.5));
}

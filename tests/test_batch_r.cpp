// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Text styles drawn Backwards and Upside down (#62).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
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
} // namespace

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

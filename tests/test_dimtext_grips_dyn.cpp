// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// The dimension style's View Direction (DIMTXTDIRECTION) and the Tolerances tab's Scaling for
// height (DIMTFAC), DIMJUST as a variable, both kept in the drawing and in DXF (#58); a grip
// under the cursor told from one grabbed (GRIPHOVER, #54); PEDIT's Close or Open (#52); the
// prompts Dynamic Input's pointer input shows coordinates at (#65).

#include <chrono>
#include <string>
#include <thread>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/dyn_fields.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {
DimData linear(double len) {
    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {len, 0};
    d.line_pt = {len * 0.5, 10};
    return d;
}

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
    return false;
}
} // namespace

TEST_CASE("View Direction right to left reads the dimension text the other way") {
    DimStyle s;
    s.precision = 1;
    const DimGeometry ltr = compute_dim_geometry(linear(12.5), s, Rgb{});
    REQUIRE(ltr.label == "12.5");
    s.text_rtl = true;
    const DimGeometry rtl = compute_dim_geometry(linear(12.5), s, Rgb{});
    REQUIRE(rtl.label == "5.21");
    REQUIRE(apply_dim_var(s, "DIMTXTDIRECTION", 0));
    REQUIRE_FALSE(s.text_rtl);
    REQUIRE(apply_dim_var(s, "DIMJUST", 2));
    REQUIRE(s.text_just == 2);
    REQUIRE(dim_var_value(s, "DIMJUST") == Approx(2.0));
}

TEST_CASE("Scaling for height draws the tolerance at its own height beside the value") {
    DimStyle s;
    s.precision = 1;
    DimData d = linear(20);
    d.tol = DimTolerance{TolMode::Deviation, 0.2, 0.1};
    // At 1, as before: the label and its second line.
    REQUIRE(compute_dim_geometry(d, s, Rgb{}).runs.empty());
    REQUIRE(apply_dim_var(s, "DIMTFAC", 0.5));
    REQUIRE(s.tol_height == Approx(0.5));
    const DimGeometry g = compute_dim_geometry(d, s, Rgb{});
    REQUIRE(g.runs.size() == 3); // the value, the upper deviation over the lower
    REQUIRE(g.runs[0].text == "20.0");
    REQUIRE(g.runs[0].height == Approx(s.text_height));
    REQUIRE(g.runs[1].height == Approx(s.text_height * 0.5));
    REQUIRE(g.runs[2].height == Approx(s.text_height * 0.5));
    REQUIRE(g.runs[1].pos.y > g.runs[2].pos.y); // upper over lower
    std::vector<Vec2> segs;
    append_dim_label_segments(g, segs);
    REQUIRE_FALSE(segs.empty());
    // Symmetric: one line, the value then the smaller tolerance.
    d.tol = DimTolerance{TolMode::Symmetric, 0.2, 0.2};
    const DimGeometry sym = compute_dim_geometry(d, s, Rgb{});
    REQUIRE(sym.runs.size() == 2);
    REQUIRE(sym.runs[1].text.find("0.2") != std::string::npos);
}

TEST_CASE("DIMTFAC and DIMTXTDIRECTION kept with the style, in the drawing and in DXF") {
    io::Document doc;
    DimStyle s;
    s.name = "ISO";
    s.tol_height = 0.7;
    s.text_rtl = true;
    doc.dimstyles = {DimStyle{}, s};
    io::Document nat;
    REQUIRE(io::parse_native(io::serialize_native(doc), nat).ok);
    REQUIRE(nat.dimstyles.size() == 2);
    REQUIRE(nat.dimstyles[1].tol_height == Approx(0.7));
    REQUIRE(nat.dimstyles[1].text_rtl);
    REQUIRE(nat.dimstyles[0].tol_height == Approx(1.0));
    io::Document dx;
    REQUIRE(io::parse_dxf(io::serialize_dxf(doc), dx).ok);
    bool found = false;
    for (const DimStyle& ds : dx.dimstyles) {
        if (ds.name == "ISO") {
            found = true;
            REQUIRE(ds.tol_height == Approx(0.7));
            REQUIRE(ds.text_rtl);
        }
    }
    REQUIRE(found);
}

TEST_CASE("The grip under the cursor is told from the one grabbed") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1 && !s.grips.empty(); }));
    engine.submit(SetCursorCommand{{10, 0.1}, 0.5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.hot_grip >= 0; }));
    REQUIRE(engine.snapshot().hot_grip_hover);
    engine.stop();
}

TEST_CASE("PEDIT offers Close for an open polyline and Open for a closed one") {
    struct Out : musacad::command::CommandOutput {
        std::string prompt;
        void append_line(const std::string&) override {}
        void set_prompt(const std::string& p) override { prompt = p; }
    } out;
    musacad::command::CommandProcessor proc{[](Command) {}, nullptr, out};
    proc.set_hovered_kind(EntityKind::Polyline);
    proc.set_hovered_closed(true);
    proc.submit_line("PEDIT");
    proc.pick_point({5, 0}, std::nullopt);
    REQUIRE(out.prompt == "Enter an option [Open/Join/Width/Edit vertex/Fit/Spline/Decurve/Ltype gen/Reverse/Undo]: ");
    proc.submit_line("O");
    REQUIRE(out.prompt == "Enter an option [Close/Join/Width/Edit vertex/Fit/Spline/Decurve/Ltype gen/Reverse/Undo]: ");
    proc.submit_line("");
    proc.set_hovered_kind(std::nullopt);
    proc.set_hovered_closed(false);
}

TEST_CASE("Pointer input knows a prompt that asks for a point") {
    using musacad::command::prompt_asks_point;
    REQUIRE(prompt_asks_point("Specify first point: "));
    REQUIRE(prompt_asks_point("Specify center point for circle or [3P/2P/Ttr (tan tan radius)]: "));
    REQUIRE(prompt_asks_point("Specify insertion point or [Scale/X/Y/Z/Rotate]: "));
    REQUIRE(prompt_asks_point("Specify first corner point or [Chamfer/Elevation/Fillet/Thickness/Width]: "));
    REQUIRE_FALSE(prompt_asks_point("Specify fillet radius <0.0000>: "));
    REQUIRE_FALSE(prompt_asks_point("Select objects: "));
    REQUIRE_FALSE(prompt_asks_point("Enter an option [Close/Open/Join]: ")); // the options are not read
}

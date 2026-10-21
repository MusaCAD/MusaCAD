// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// The dimension style's View Direction (DIMTXTDIRECTION) and the Tolerances tab's Scaling for
// height (DIMTFAC), DIMJUST as a variable, both kept in the drawing and in DXF (#58); a grip
// under the cursor told from one grabbed (GRIPHOVER, #54); PEDIT's Close or Open (#52); the
// prompts Dynamic Input's pointer input shows coordinates at, and the grip dimensions
// (DYNDIGRIP, DYNDIVIS) of a hot grip's stretch (#65); DIMREASSOCIATE point by point (#59).

#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/dyn_fields.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/grips.hpp"
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

TEST_CASE("A hot grip's stretch is measured from the other end, the vertex before or the centre") {
    using musacad::command::grip_dyn_anchor;
    const EntityHandle line{1, 0, EntityKind::Line};
    const EntityHandle circle{2, 0, EntityKind::Circle};
    const EntityHandle pl{3, 0, EntityKind::Polyline};
    std::vector<GripInfo> grips;
    grips.push_back({{0, 0}, line, 0, 0});
    grips.push_back({{10, 0}, line, 1, 0});
    grips.push_back({{5, 0}, line, 2, 0});
    grips.push_back({{50, 50}, circle, 0, 0});
    grips.push_back({{55, 50}, circle, 1, 0});
    for (std::uint32_t i = 0; i < 3; ++i) { // an open polyline of three vertices
        grips.push_back({{100.0 + 10.0 * i, 0}, pl, i, 0});
    }
    grips.push_back({{105, 0}, pl, kSegmentGripBase, 0});
    grips.push_back({{115, 0}, pl, kSegmentGripBase + 1, 0});
    bool radius = true;
    REQUIRE(grip_dyn_anchor(grips, line, 1, radius) == std::optional<Vec2>{Vec2{0, 0}});
    REQUIRE_FALSE(radius);
    REQUIRE(grip_dyn_anchor(grips, line, 0, radius) == std::optional<Vec2>{Vec2{10, 0}});
    REQUIRE_FALSE(grip_dyn_anchor(grips, line, 2, radius)); // the midpoint moves the line
    REQUIRE(grip_dyn_anchor(grips, circle, 1, radius) == std::optional<Vec2>{Vec2{50, 50}});
    REQUIRE(radius);
    REQUIRE(grip_dyn_anchor(grips, pl, 2, radius) == std::optional<Vec2>{Vec2{110, 0}});
    REQUIRE(grip_dyn_anchor(grips, pl, 0, radius) == std::optional<Vec2>{Vec2{110, 0}}); // open: the next
    REQUIRE_FALSE(grip_dyn_anchor(grips, pl, kSegmentGripBase, radius));
}

TEST_CASE("Grip dimensions show the length change and the resulting length, as DYNDIGRIP and DYNDIVIS say") {
    using namespace musacad::command;
    PreviewSpec pv;
    pv.kind = PreviewKind::Segment;
    pv.grip_hot = true;
    pv.points = {{10, 0}}; // a line from 0,0 to 10,0, its end grip hot
    pv.grip_anchor = Vec2{0, 0};
    REQUIRE(dyn_grip_measured(pv));
    const int digrip = dyndigrip();
    const int divis = dyndivis();
    REQUIRE(digrip == 31);
    REQUIRE(divis == 1);
    // DYNDIVIS 1: the length change, then the resulting length.
    std::vector<DynField> f = dyn_fields(pv, {0, 12});
    REQUIRE(f.size() == 2);
    REQUIRE(f[0].label == "Length change");
    REQUIRE(f[0].value == Approx(2.0));
    REQUIRE(f[0].slot == 0);
    REQUIRE(f[1].label == "Length");
    REQUIRE(f[1].value == Approx(12.0));
    REQUIRE(f[1].slot == 1);
    // DYNDIVIS 2: the angles as well; DYNDIVIS 0: the length change alone.
    dyndivis() = 2;
    f = dyn_fields(pv, {0, 12});
    REQUIRE(f.size() == 4);
    REQUIRE(f[2].label == "Angle");
    REQUIRE(f[2].value == Approx(90.0));
    REQUIRE(f[3].label == "Angle change");
    REQUIRE(f[3].value == Approx(90.0));
    dyndivis() = 0;
    REQUIRE(dyn_fields(pv, {0, 12}).size() == 1);
    // DYNDIGRIP without the length change: the resulting length comes first.
    dyndivis() = 1;
    dyndigrip() = 1 | 4;
    f = dyn_fields(pv, {0, 12});
    REQUIRE(f.size() == 2);
    REQUIRE(f[0].label == "Length");
    REQUIRE(f[1].label == "Angle");
    dyndigrip() = digrip;
    dyndivis() = divis;

    // A typed length change keeps the cursor's direction; a resulting length too; an angle
    // keeps the length.
    std::vector<std::optional<double>> typed{5.0, std::nullopt};
    Vec2 p = grip_dyn_point(pv, {0, 12}, typed);
    REQUIRE(p.x == Approx(0.0).margin(1e-9));
    REQUIRE(p.y == Approx(15.0));
    typed = {std::nullopt, 4.0};
    p = grip_dyn_point(pv, {3, 0}, typed);
    REQUIRE(p.x == Approx(4.0));
    REQUIRE(compose_grip_submit(pv, {3, 0}, typed) == "#4.000000,0.000000");
    REQUIRE(compose_grip_submit(pv, {3, 0}, std::vector<std::optional<double>>{std::nullopt, std::nullopt}).empty());
    dyndivis() = 2;
    typed = {std::nullopt, std::nullopt, 90.0, std::nullopt};
    p = grip_dyn_point(pv, {20, 0}, typed);
    REQUIRE(p.x == Approx(0.0).margin(1e-9));
    REQUIRE(p.y == Approx(20.0));
    dyndivis() = divis;

    // A circle's radius grip: the radius change and the radius.
    pv.grip_radius = true;
    f = dyn_fields(pv, {0, 12});
    REQUIRE(f.size() == 2);
    REQUIRE(f[0].label == "Radius change");
    REQUIRE(f[1].label == "Radius");
}

TEST_CASE("A hot grip's STRETCH measured from the object takes a typed length") {
    struct Out : musacad::command::CommandOutput {
        void append_line(const std::string&) override {}
        void set_prompt(const std::string&) override {}
    } out;
    std::vector<Command> sent;
    musacad::command::CommandProcessor proc{[&](Command c) { sent.push_back(std::move(c)); }, nullptr, out};
    GripDragCommand begin;
    begin.handle = EntityHandle{0, 0, EntityKind::Line};
    begin.grip = 1;
    proc.start_grip_mode(begin, {10, 0}, Vec2{0, 0}, false);
    REQUIRE(proc.preview().grip_hot);
    REQUIRE(proc.preview().grip_anchor == std::optional<Vec2>{Vec2{0, 0}});
    proc.submit_line("#25,0"); // what Enter on a resulting length of 25 submits
    const GripDragCommand* commit = nullptr;
    for (const Command& c : sent) {
        if (const auto* g = std::get_if<GripDragCommand>(&c); g != nullptr && g->phase == GripDragCommand::Phase::Commit) {
            commit = g;
        }
    }
    REQUIRE(commit != nullptr);
    REQUIRE(commit->pos.x == Approx(25.0));
    REQUIRE(commit->pos.y == Approx(0.0).margin(1e-9));

    // Several hot grips at once are not measured from one object.
    GripDragCommand several = begin;
    several.also.emplace_back(EntityHandle{1, 0, EntityKind::Line}, 0);
    proc.start_grip_mode(several, {10, 0}, Vec2{0, 0}, false);
    REQUIRE_FALSE(proc.preview().grip_anchor);
    proc.submit_line("X");
}

TEST_CASE("DIMREASSOCIATE asks for each dimension's points in turn, Enter keeping one") {
    struct Out : musacad::command::CommandOutput {
        std::string prompt;
        void append_line(const std::string&) override {}
        void set_prompt(const std::string& p) override { prompt = p; }
    } out;
    std::vector<Command> sent;
    musacad::command::CommandProcessor proc{[&](Command c) { sent.push_back(std::move(c)); }, nullptr, out};
    const EntityHandle lin{0, 0, EntityKind::Dimension};
    const EntityHandle rad{1, 0, EntityKind::Dimension};
    proc.set_selection_count(2);
    proc.set_selected_dims({SelectedDim{lin, DimType::Linear, {{0, 0}, {20, 0}}},
                            SelectedDim{rad, DimType::Radius, {{50, 0}, {55, 0}}}});
    proc.submit_line("DIMREASSOCIATE");
    REQUIRE(out.prompt == "Specify first extension line origin or [Select object] <next>: ");
    REQUIRE(proc.preview().kind == musacad::command::PreviewKind::Segment);
    REQUIRE(proc.preview().points.front() == Vec2{0, 0}); // from the point there is now
    proc.submit_line("");     // the first stays
    REQUIRE(out.prompt == "Specify second extension line origin <next>: ");
    proc.submit_line("30,0"); // the second moves
    REQUIRE(out.prompt == "Select arc or circle <next>: ");
    const auto* moved = std::get_if<DimAssocCommand>(&sent.back());
    REQUIRE(moved != nullptr);
    REQUIRE(moved->op == DimAssocCommand::Op::Points);
    REQUIRE(moved->dim == lin);
    REQUIRE(moved->points.size() == 2);
    REQUIRE(moved->points[0] == Vec2{0, 0});
    REQUIRE(moved->points[1] == Vec2{30, 0});
    proc.submit_line("110,0"); // the circle there
    const auto* picked = std::get_if<DimAssocCommand>(&sent.back());
    REQUIRE(picked != nullptr);
    REQUIRE(picked->dim == rad);
    REQUIRE(picked->pick == std::optional<Vec2>{Vec2{110, 0}});
    REQUIRE_FALSE(proc.has_active_command());

    // Select object, and a dimension left as it was sends nothing.
    sent.clear();
    proc.set_selected_dims({SelectedDim{lin, DimType::Aligned, {{0, 0}, {20, 0}}},
                            SelectedDim{rad, DimType::Angular, {{0, 0}, {10, 0}, {0, 10}}}});
    proc.submit_line("DRE");
    proc.submit_line("S");
    REQUIRE(out.prompt == "Select object <next>: ");
    proc.submit_line("5,5");
    REQUIRE(out.prompt == "Specify angle vertex <next>: ");
    REQUIRE(std::get_if<DimAssocCommand>(&sent.back())->pick == std::optional<Vec2>{Vec2{5, 5}});
    proc.submit_line("");
    REQUIRE(out.prompt == "Specify first angle endpoint <next>: ");
    proc.submit_line("");
    REQUIRE(out.prompt == "Specify second angle endpoint <next>: ");
    proc.submit_line("");
    REQUIRE(sent.size() == 1);
    REQUIRE_FALSE(proc.has_active_command());
    proc.set_selected_dims({});
    proc.set_selection_count(0);
}

TEST_CASE("DIMREASSOCIATE moves a dimension's points onto the objects given") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {30, 0}, 1});
    engine.submit(AddCircleCommand{{100, 0}, 10, 2});
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Aligned), {0, 0}, {20, 0}, {10, -5}, 0, 3});
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Radius), {50, 0}, {55, 0}, {58, 0}, 0, 4});
    engine.submit(SelectAllCommand{});
    engine.submit(DimAssocCommand{DimAssocCommand::Op::Disassociate, 2, 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selected_dims.size() == 2; }));
    std::vector<SelectedDim> dims = engine.snapshot().selected_dims;
    const auto find = [](const std::vector<SelectedDim>& v, DimType t) {
        for (const SelectedDim& d : v) {
            if (d.type == t) {
                return d;
            }
        }
        return SelectedDim{};
    };
    REQUIRE(find(dims, DimType::Aligned).points[1] == Vec2{20, 0});

    DimAssocCommand to_end{DimAssocCommand::Op::Points, 2, 6};
    to_end.dim = find(dims, DimType::Aligned).handle;
    to_end.points = {{0, 0}, {30, 0}};
    engine.submit(to_end);
    DimAssocCommand to_circle{DimAssocCommand::Op::Points, 2, 6};
    to_circle.dim = find(dims, DimType::Radius).handle;
    to_circle.pick = Vec2{110, 0};
    to_circle.pick_radius = 1.0;
    engine.submit(to_circle);
    REQUIRE(wait_until(engine, [&](const auto& s) {
        const SelectedDim r = find(s.selected_dims, DimType::Radius);
        return s.selected_dims.size() == 2 && !r.points.empty() && r.points[0] == Vec2{100, 0};
    }));
    dims = engine.snapshot().selected_dims;
    REQUIRE(find(dims, DimType::Aligned).points[1] == Vec2{30, 0});
    REQUIRE(find(dims, DimType::Radius).points[1].x == Approx(110.0));
    // Associated again: the line's end moved, the dimension goes with it.
    engine.submit(SelectPickCommand{{15, 0}, 0.5, false});
    engine.submit(MoveSelectionCommand{{0, 10}, 7});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        const SelectedDim a = find(s.selected_dims, DimType::Aligned);
        return a.points.size() == 2 && a.points[1] == Vec2{30, 10};
    }));
    engine.stop();
}

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Where a dimension's text goes when it is moved, as AutoCAD has it. DIMTMOVE 0 (the
// default): the dimension line moves with the text, which slides along it -- between
// the extension lines or out beside them, the line extended under it. 1: the text moves
// alone with a leader back to the line. 2: alone, without one. Radius and diameter text
// stands where the dimension was placed and its grip swings the leader round the circle;
// angular text slides along its arc, which follows it out.

#include <cmath>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"

#include <chrono>
#include <filesystem>
#include <thread>

using namespace musacad::core;
using Catch::Approx;

namespace {

Vec2 label_centre(const DimGeometry& g) {
    Vec2 q[4];
    REQUIRE(dim_label_quad(g, false, q));
    return (q[0] + q[2]) * 0.5;
}

/// The dimension the store holds after dragging grip `grip` to `to`, in `style`.
DimData dragged(const DimData& d, const DimStyle& style, std::uint32_t grip, Vec2 to) {
    GeometryStore store;
    store.set_dimstyle(0, style);
    const EntityHandle h = store.add_dimension(d.type, d.a, d.b, d.line_pt, 0, {});
    store.set_dim_aux(h, d.aux);
    GeometryStore out;
    out.set_dimstyle(0, style);
    const EntityHandle h2 = add_command_to_store(out, edit_for_grip_drag(store, h, grip, to), EntityProps{});
    return *out.dimension(h2);
}

double max_along(const std::vector<Vec2>& pts, Vec2 u) {
    double m = -1e300;
    for (const Vec2& p : pts) {
        m = std::max(m, dot(p, u));
    }
    return m;
}

} // namespace

TEST_CASE("DIMTMOVE 0: dragging the text moves the dimension line with it") {
    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {50, 0};
    d.line_pt = {25, 10};
    d.aux = kPi; // horizontal
    const DimStyle style;
    // Up and along, still between the extension lines.
    const DimData moved = dragged(d, style, DimData::kTextGripIndex, {30.0, 20.0});
    REQUIRE(moved.text_offset.y == 0.0); // never off its line
    const DimGeometry g = compute_dim_geometry(moved, style, Rgb{});
    REQUIRE(label_centre(g).x == Approx(30.0));
    REQUIRE(label_centre(g).y == Approx(20.0));
    // The dimension line went up with it: its feet are now near y = 20.
    double ymax = -1e300;
    for (const Vec2& p : g.dim_lines) {
        ymax = std::max(ymax, p.y);
    }
    REQUIRE(ymax < 20.0);
    REQUIRE(ymax > 15.0);
    REQUIRE(dim_measure(moved) == Approx(50.0));
    // No leader: the text is on its line.
    const DimGeometry plain = compute_dim_geometry(dragged(d, style, DimData::kTextGripIndex, {25.0, 20.0}), style, Rgb{});
    REQUIRE(g.dim_lines.size() == plain.dim_lines.size());
}

TEST_CASE("DIMTMOVE 0: text slid beside the extension lines takes the dimension line out to it") {
    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {50, 0};
    d.line_pt = {25, 10};
    d.aux = kPi;
    const DimStyle style;
    const DimData beside = dragged(d, style, DimData::kTextGripIndex, {75.0, 12.0});
    const DimGeometry g = compute_dim_geometry(beside, style, Rgb{});
    REQUIRE(label_centre(g).x == Approx(75.0));
    // The line reaches under the text: past x = 75.
    REQUIRE(max_along(g.dim_lines, {1, 0}) > 75.0);
    // And back between them, it stops at the extension lines again.
    const DimGeometry back = compute_dim_geometry(dragged(beside, style, DimData::kTextGripIndex, {25.0, 12.0}), style, Rgb{});
    REQUIRE(max_along(back.dim_lines, {1, 0}) <= 50.0 + 1e-6);
    // A vertical dimension slides along its own direction.
    DimData v;
    v.type = DimType::Linear;
    v.a = {0, 0};
    v.b = {0, 40};
    v.line_pt = {10, 20};
    v.aux = kHalfPi;
    const DimGeometry gv = compute_dim_geometry(dragged(v, style, DimData::kTextGripIndex, {14.0, 60.0}), style, Rgb{});
    REQUIRE(max_along(gv.dim_lines, {0, 1}) > 55.0);
}

TEST_CASE("DIMTMOVE 1 keeps a leader to moved text, 2 lets it stand alone") {
    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {50, 0};
    d.line_pt = {25, 10};
    d.aux = kPi;
    DimStyle leader;
    leader.text_move = 1;
    DimStyle alone;
    alone.text_move = 2;
    const DimData m1 = dragged(d, leader, DimData::kTextGripIndex, {70.0, 40.0});
    const DimData m2 = dragged(d, alone, DimData::kTextGripIndex, {70.0, 40.0});
    REQUIRE(m1.line_pt == d.line_pt); // the line stays where it was
    REQUIRE(m2.line_pt == d.line_pt);
    const std::size_t base = compute_dim_geometry(d, leader, Rgb{}).dim_lines.size();
    REQUIRE(compute_dim_geometry(m1, leader, Rgb{}).dim_lines.size() > base); // a leader
    REQUIRE(compute_dim_geometry(m2, alone, Rgb{}).dim_lines.size() == base);  // none
    REQUIRE(label_centre(compute_dim_geometry(m2, alone, Rgb{})).x == Approx(70.0));
}

TEST_CASE("Radius and diameter text stands where the dimension was placed") {
    DimStyle style;
    DimData r;
    r.type = DimType::Radius;
    r.a = {0, 0};
    r.b = {10 / std::sqrt(2.0), 10 / std::sqrt(2.0)};
    r.line_pt = {20, 20}; // outside, up and to the right
    DimGeometry g = compute_dim_geometry(r, style, Rgb{});
    REQUIRE(g.text_justify == text::Justify::Left);     // reading away from the circle
    REQUIRE(g.text_pos.x > 20.0);                        // past the landing
    REQUIRE(g.text_pos.y == Approx(20.0 + style.text_height * 0.4));
    // The leader runs from the arrow on the circle out to the landing; inside the circle
    // there is only the centre mark (AutoCAD's DIMCEN rule with the line outside).
    for (const Vec2& p : g.dim_lines) {
        REQUIRE((length(p) >= 10.0 - 1e-9 || length(p) <= style.arrow_size * 0.5 + 1e-9));
    }
    // To the left the text reads the other way.
    r.b = {-10 / std::sqrt(2.0), 10 / std::sqrt(2.0)};
    r.line_pt = {-20, 20};
    REQUIRE(compute_dim_geometry(r, style, Rgb{}).text_justify == text::Justify::Right);
    // Inside: the line from the centre is broken around the text.
    r.b = {40, 0};
    r.line_pt = {20, 0};
    g = compute_dim_geometry(r, style, Rgb{});
    REQUIRE(g.text_justify == text::Justify::Center);
    REQUIRE(g.dim_lines.size() == 4); // two pieces, either side of the text
    // A diameter inside reaches across, both arrows.
    DimData dia = r;
    dia.type = DimType::Diameter;
    dia.line_pt = {0, 0};
    g = compute_dim_geometry(dia, style, Rgb{});
    double xmin = 1e300;
    for (const Vec2& p : g.dim_lines) {
        xmin = std::min(xmin, p.x);
    }
    REQUIRE(xmin == Approx(-40.0));
    // On the circle itself (a DXF one, or from before): as it always was.
    r.line_pt = r.b;
    REQUIRE(compute_dim_geometry(r, style, Rgb{}).text_justify == text::Justify::Left);
}

TEST_CASE("Dragging a radius dimension's text swings its leader round the circle") {
    DimData r;
    r.type = DimType::Radius;
    r.a = {0, 0};
    r.b = {10, 0};
    r.line_pt = {20, 0};
    const DimData moved = dragged(r, DimStyle{}, DimData::kTextGripIndex, {0.0, 25.0});
    REQUIRE(moved.b.x == Approx(0.0).margin(1e-9)); // the arrow went round to the top
    REQUIRE(moved.b.y == Approx(10.0));
    REQUIRE(moved.line_pt == Vec2{0.0, 25.0});
    REQUIRE(moved.text_offset == Vec2{0.0, 0.0});
    REQUIRE(dim_measure(moved) == Approx(10.0));
}

TEST_CASE("Angular text slides along its arc, which follows it out and grows to reach it") {
    DimData d;
    d.type = DimType::Angular;
    d.a = {0, 0};
    d.b = {10, 0};
    d.line_pt = {0, 10};
    d.aux = 8.0;
    const DimStyle style;
    // Dragged out to radius 15, past the second ray (to 120 degrees).
    const Vec2 to{15 * std::cos(2.0943951), 15 * std::sin(2.0943951)};
    const DimData moved = dragged(d, style, DimData::kTextGripIndex, to);
    REQUIRE(moved.aux == Approx(15.0));
    REQUIRE(moved.text_offset.y == 0.0);
    const DimGeometry g = compute_dim_geometry(moved, style, Rgb{});
    const Vec2 c = label_centre(g);
    REQUIRE(std::atan2(c.y, c.x) == Approx(2.0943951).margin(0.05));
    // The arc runs on past 90 degrees, to the text.
    double reach = -1e300;
    for (const Vec2& p : g.dim_lines) {
        reach = std::max(reach, std::atan2(p.y, p.x));
    }
    REQUIRE(reach > 2.0);
    REQUIRE(dim_measure(moved) == Approx(90.0));
}

TEST_CASE("DIMTMOVE: a dimension variable, kept in .musa and DXF") {
    DimStyle s;
    REQUIRE(apply_dim_var(s, "DIMTMOVE", 1));
    REQUIRE(s.text_move == 1);
    REQUIRE_FALSE(apply_dim_var(s, "DIMTMOVE", 3));
    REQUIRE(dim_var_value(s, "DIMTMOVE") == Approx(1.0));
    io::Document doc;
    DimStyle named{"LEADERS"};
    named.text_move = 2;
    doc.dimstyles = {DimStyle{"Standard"}, named};
    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_dimtmove.musa";
    REQUIRE(io::save_native(doc, p.string()).ok);
    io::Document back;
    REQUIRE(io::load_native(p.string(), back).ok);
    REQUIRE(back.dimstyles.size() == 2);
    REQUIRE(back.dimstyles[1].text_move == 2);
    REQUIRE(back.dimstyles[1].name == "LEADERS");
    const std::filesystem::path q = std::filesystem::temp_directory_path() / "musacad_dimtmove.dxf";
    REQUIRE(io::save_dxf(doc, q.string()).ok);
    io::Document dx;
    REQUIRE(io::load_dxf(q.string(), dx).ok);
    bool found = false;
    for (const DimStyle& ds : dx.dimstyles) {
        if (ds.name == "LEADERS") {
            found = ds.text_move == 2;
        }
    }
    REQUIRE(found);
    std::filesystem::remove(p);
    std::filesystem::remove(q);
}

TEST_CASE("A selected dimension is highlighted along its own lines, with no connectors between them") {
    // The highlight used to join a dimension's separate lines into one polyline: a radius
    // dimension placed outside its circle showed a line from the centre mark to its arrow.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{.type = static_cast<std::uint8_t>(DimType::Radius),
                                      .a = {0, 0},
                                      .b = {10 / std::sqrt(2.0), 10 / std::sqrt(2.0)},
                                      .line_pt = {20, 20},
                                      .group = 1});
    engine.submit(SelectAllCommand{});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool lit = false;
    while (!lit && std::chrono::steady_clock::now() < deadline) {
        engine.consume_snapshot();
        lit = !engine.snapshot().selected_line_vertices.empty();
        if (!lit) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    REQUIRE(lit);
    DimData d;
    d.type = DimType::Radius;
    d.a = {0, 0};
    d.b = {10 / std::sqrt(2.0), 10 / std::sqrt(2.0)};
    d.line_pt = {20, 20};
    const DimGeometry g = compute_dim_geometry(d, DimStyle{}, Rgb{});
    const std::vector<Vec2>& lit_pts = engine.snapshot().selected_line_vertices;
    REQUIRE(lit_pts.size() % 2 == 0);
    for (std::size_t i = 0; i + 1 < lit_pts.size(); i += 2) {
        bool drawn = false;
        for (std::size_t k = 0; k + 1 < g.dim_lines.size(); k += 2) {
            const bool same = distance(lit_pts[i], g.dim_lines[k]) < 1e-9 && distance(lit_pts[i + 1], g.dim_lines[k + 1]) < 1e-9;
            const bool flipped = distance(lit_pts[i], g.dim_lines[k + 1]) < 1e-9 && distance(lit_pts[i + 1], g.dim_lines[k]) < 1e-9;
            drawn = drawn || same || flipped;
        }
        INFO("segment " << i / 2 << ": (" << lit_pts[i].x << ", " << lit_pts[i].y << ") - (" << lit_pts[i + 1].x << ", "
                        << lit_pts[i + 1].y << ")");
        REQUIRE(drawn);
    }
    engine.stop();
}

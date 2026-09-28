// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Polyline widths (issues #37, #36): the band geometry, the widths through the store,
// the edits that rebuild a polyline, the file formats, and the commands that set them
// (PLINE Width / Halfwidth, RECTANG Width / Elevation / Thickness, PEDIT Width, DONUT,
// FILL / FILLMODE / PLINEWID).

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/command/dyn_fields.hpp"
#include "musacad/core/block_resolve.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/polyline_width.hpp"
#include "musacad/core/properties_palette.hpp"
#include "musacad/core/properties_registry.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {
constexpr double kPiT = 3.14159265358979323846;

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
bool eq(Vec2 p, Vec2 q, double eps = 1e-6) {
    return std::abs(p.x - q.x) < eps && std::abs(p.y - q.y) < eps;
}
bool has_segment(const std::vector<Vec2>& lines, Vec2 a, Vec2 b) {
    for (std::size_t i = 0; i + 1 < lines.size(); i += 2) {
        if ((eq(lines[i], a) && eq(lines[i + 1], b)) || (eq(lines[i], b) && eq(lines[i + 1], a))) {
            return true;
        }
    }
    return false;
}
double area_of(const std::vector<Vec2>& tris) {
    double a = 0.0;
    for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
        const Vec2 u = tris[i + 1] - tris[i];
        const Vec2 v = tris[i + 2] - tris[i];
        a += std::abs(u.x * v.y - u.y * v.x) * 0.5;
    }
    return a;
}
bool has_point(const std::vector<Vec2>& pts, Vec2 p) {
    for (const Vec2& q : pts) {
        if (eq(p, q)) {
            return true;
        }
    }
    return false;
}
/// The Global width row of the published selection summary (-1 when there is none).
double selected_width(const RenderSnapshot& s) {
    for (const PropertyField& f : s.selection_summary.fields) {
        if (f.id == PropertyId::PlineWidth) {
            return f.value.num;
        }
    }
    return -1.0;
}
/// Select exactly the object under `at` and wait for its Global width to read `width`.
bool picked_width_is(GeometryEngine& e, Vec2 at, double width) {
    e.submit(ClearSelectionCommand{});
    e.submit(SelectPickCommand{at, 0.3});
    return wait_until(e, [&](const auto& s) {
        return s.selection.size() == 1 && std::abs(selected_width(s) - width) < 1e-9;
    });
}
AddPolylineCommand wide(std::vector<Vec2> pts, double width, std::uint64_t group, bool closed = false) {
    AddPolylineCommand pl;
    pl.points = std::move(pts);
    pl.closed = closed;
    pl.group = group;
    pl.widths.assign(2 * pl.points.size(), width);
    return pl;
}

struct Out : musacad::command::CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
    [[nodiscard]] bool any_contains(const std::string& sub) const {
        for (const auto& l : lines) {
            if (l.find(sub) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};
struct H {
    std::vector<Command> cmds;
    Out out;
    musacad::command::CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    template <class T>
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
    template <class T>
    [[nodiscard]] int count() const {
        int n = 0;
        for (const Command& c : cmds) {
            n += std::holds_alternative<T>(c) ? 1 : 0;
        }
        return n;
    }
    void run(std::initializer_list<const char*> lines_in) {
        for (const char* l : lines_in) {
            proc.submit_line(l);
        }
    }
};
} // namespace

TEST_CASE("#37 band geometry: a straight band, a taper, a mitred corner and a donut") {
    std::vector<Vec2> tris;
    const std::vector<Vec2> seg{{0, 0}, {10, 0}};
    pline::band_triangles(seg, {}, std::vector<double>{2, 2, 2, 2}, false, 1e-3, tris);
    REQUIRE(tris.size() == 6);
    REQUIRE(area_of(tris) == Approx(20.0));
    REQUIRE(has_point(tris, {0, 1}));
    REQUIRE(has_point(tris, {10, -1}));

    tris.clear();
    pline::band_triangles(seg, {}, std::vector<double>{0, 4, 4, 4}, false, 1e-3, tris);
    REQUIRE(area_of(tris) == Approx(20.0)); // a triangle 10 long and 4 wide at its end
    REQUIRE(has_point(tris, {10, 2}));

    // Two legs of one width meet in a mitre: the outer corner is carried out to a point.
    tris.clear();
    const std::vector<Vec2> ell{{0, 0}, {10, 0}, {10, 10}};
    pline::band_triangles(ell, {}, std::vector<double>(6, 2.0), false, 1e-3, tris);
    REQUIRE(has_point(tris, {11, -1}));
    REQUIRE(has_point(tris, {9, 1}));
    REQUIRE(area_of(tris) == Approx(40.0));

    // A ring: two half circles on the mean diameter, as wide as the ring is thick.
    tris.clear();
    const std::vector<Vec2> ring{{-0.375, 0}, {0.375, 0}};
    pline::band_triangles(ring, std::vector<double>{1, 1}, std::vector<double>(4, 0.25), true, 1e-5, tris);
    REQUIRE(area_of(tris) == Approx(kPiT * (0.5 * 0.5 - 0.25 * 0.25)).epsilon(0.01));

    // The outline of the first band: two sides and two ends.
    std::vector<Vec2> lines;
    pline::band_outline(seg, {}, std::vector<double>{2, 2, 2, 2}, false, 1e-3, lines);
    REQUIRE(lines.size() == 8);
    REQUIRE(has_segment(lines, {0, 1}, {10, 1}));
    REQUIRE(has_segment(lines, {0, -1}, {10, -1}));
    REQUIRE(has_segment(lines, {0, 1}, {0, -1}));
    REQUIRE(has_segment(lines, {10, 1}, {10, -1}));

    // A polyline wide only in places keeps its plain segments as lines.
    lines.clear();
    pline::plain_segments(ell, {}, std::vector<double>{2, 2, 0, 0, 0, 0}, false, 1e-3, lines);
    REQUIRE(lines.size() == 2);
    REQUIRE(has_segment(lines, {10, 0}, {10, 10}));
}

TEST_CASE("#37 width bookkeeping: uniform, fitted to a new vertex count, inherited by a piece") {
    REQUIRE(pline::uniform_width(std::vector<double>{}).value() == 0.0);
    REQUIRE(pline::uniform_width(std::vector<double>{3, 3, 3, 3}).value() == 3.0);
    REQUIRE(!pline::uniform_width(std::vector<double>{3, 1, 3, 3}).has_value());
    REQUIRE(pline::max_width(std::vector<double>{0, 4, 1, 1}) == 4.0);

    std::vector<double> w{2, 2};
    pline::fit_widths(w, 3);
    REQUIRE(w == std::vector<double>(6, 2.0));
    w = {1, 2, 3, 4};
    pline::fit_widths(w, 3); // a taper cannot be spread over a different vertex list
    REQUIRE(w.empty());
    w = {0, 0, 0, 0};
    pline::fit_widths(w, 2);
    REQUIRE(w.empty());

    // The right-hand half of a 0 -> 10 taper runs 5 -> 10.
    const std::vector<Vec2> src{{0, 0}, {10, 0}};
    const std::vector<double> sw{0, 10, 10, 10};
    const std::vector<Vec2> piece{{5, 0}, {10, 0}};
    std::vector<double> got;
    pline::inherit_widths(src, {}, sw, false, piece, {}, false, got);
    REQUIRE(got.size() == 4);
    REQUIRE(got[0] == Approx(5.0));
    REQUIRE(got[1] == Approx(10.0));
    // Nothing wide to inherit: nothing comes back.
    got.clear();
    pline::inherit_widths(src, {}, std::vector<double>{}, false, piece, {}, false, got);
    REQUIRE(got.empty());
}

TEST_CASE("#37 a wide polyline is a filled band, an outline with FILLMODE off, and is picked on the band") {
    GeometryEngine e;
    e.start();
    e.submit(wide({{0, 0}, {10, 0}}, 2.0, 1));
    REQUIRE(wait_until(e, [](const auto& s) { return s.fill_vertices.size() == 6; }));
    REQUIRE(area_of(e.snapshot().fill_vertices) == Approx(20.0));
    REQUIRE(e.snapshot().fillmode);

    // 0.9 off the centre line is inside the band.
    REQUIRE(picked_width_is(e, {5, 0.9}, 2.0));
    // ... and the highlight covers the band.
    REQUIRE(wait_until(e, [](const auto& s) { return s.selected_fill_vertices.size() == 6; }));
    e.submit(ClearSelectionCommand{});

    e.submit(SetFillModeCommand{false});
    REQUIRE(wait_until(e, [](const auto& s) { return !s.fillmode && s.fill_vertices.empty(); }));
    REQUIRE(has_segment(e.snapshot().line_vertices, {0, 1}, {10, 1}));
    REQUIRE(has_segment(e.snapshot().line_vertices, {0, -1}, {10, -1}));
    REQUIRE(has_segment(e.snapshot().line_vertices, {10, 1}, {10, -1}));

    e.submit(SetFillModeCommand{true});
    REQUIRE(wait_until(e, [](const auto& s) { return s.fillmode && s.fill_vertices.size() == 6; }));
    e.stop();
}

TEST_CASE("#37 FILLMODE 0 hides a solid hatch and FILLMODE 1 brings it back") {
    GeometryEngine e;
    e.start();
    AddHatchCommand h;
    h.loops.push_back({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
    h.pattern_name = "SOLID";
    h.group = 1;
    e.submit(h);
    REQUIRE(wait_until(e, [](const auto& s) { return !s.fill_vertices.empty(); }));
    e.submit(SetFillModeCommand{false});
    REQUIRE(wait_until(e, [](const auto& s) { return s.fill_vertices.empty(); }));
    e.submit(SetFillModeCommand{true});
    REQUIRE(wait_until(e, [](const auto& s) { return !s.fill_vertices.empty(); }));
    e.stop();
}

TEST_CASE("#37 MOVE, SCALE, undo and redo carry the width; SCALE scales it") {
    GeometryEngine e;
    e.start();
    e.submit(wide({{0, 0}, {10, 0}}, 2.0, 1));
    REQUIRE(picked_width_is(e, {5, 0}, 2.0));
    e.submit(MoveSelectionCommand{{0, 20}, 2});
    REQUIRE(picked_width_is(e, {5, 20}, 2.0));
    e.submit(ScaleSelectionCommand{{0, 20}, 3.0, 3});
    REQUIRE(picked_width_is(e, {15, 20}, 6.0));
    e.submit(UndoLastGroupCommand{});
    REQUIRE(picked_width_is(e, {5, 20}, 2.0));
    e.submit(RedoLastGroupCommand{});
    REQUIRE(picked_width_is(e, {15, 20}, 6.0));
    e.stop();
}

TEST_CASE("#37 TRIM and BREAK pieces keep the width; a taper keeps its slope; redo makes them again") {
    GeometryEngine e;
    e.start();
    e.submit(wide({{0, 0}, {10, 0}, {10, 10}}, 1.0, 1));
    e.submit(AddLineCommand{{5, -5}, {5, 5}, 2});
    REQUIRE(wait_until(e, [](const auto& s) { return !s.fill_vertices.empty(); }));
    e.submit(TrimPickCommand{{2, 0}, 0.3, 3});
    REQUIRE(picked_width_is(e, {10, 5}, 1.0));
    // The piece starts where the cut was made.
    REQUIRE(has_point(e.snapshot().fill_vertices, {5, 0.5}));
    REQUIRE(!has_point(e.snapshot().fill_vertices, {0, 0.5}));
    e.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(e, [](const auto& s) { return has_point(s.fill_vertices, {0, 0.5}); }));
    e.submit(RedoLastGroupCommand{});
    REQUIRE(wait_until(e, [](const auto& s) { return !has_point(s.fill_vertices, {0, 0.5}); }));
    REQUIRE(picked_width_is(e, {10, 5}, 1.0));

    // BREAK between two points: both pieces as wide as the source.
    BreakCommand br;
    br.pick = {10, 5};
    br.pick_radius = 0.3;
    br.p1 = {10, 4};
    br.p2 = {10, 6};
    br.group = 4;
    e.submit(br);
    REQUIRE(picked_width_is(e, {10, 2}, 1.0));
    REQUIRE(picked_width_is(e, {10, 8}, 1.0));

    // A taper 0 -> 10 over 10 units, cut at x = 5: what is left runs 5 -> 10.
    AddPolylineCommand taper;
    taper.points = {{0, 40}, {10, 40}};
    taper.widths = {0, 10, 10, 10};
    taper.group = 5;
    e.submit(taper);
    e.submit(AddLineCommand{{5, 30}, {5, 50}, 6});
    REQUIRE(picked_width_is(e, {8, 40}, 0.0)); // Global width reads the first segment's start
    e.submit(TrimPickCommand{{1, 40}, 0.3, 7});
    REQUIRE(picked_width_is(e, {8, 40}, 5.0));
    e.stop();
}

TEST_CASE("#37 OFFSET copies the width, JOIN keeps each source's, EXPLODE says what it lost") {
    GeometryEngine e;
    e.start();
    e.submit(wide({{0, 0}, {10, 0}}, 2.0, 1));
    OffsetPickCommand off;
    off.pick = {5, 0};
    off.radius = 0.3;
    off.distance = 5.0;
    off.side = {5, 9};
    off.group = 2;
    e.submit(off);
    REQUIRE(picked_width_is(e, {5, 5}, 2.0));

    // A line joined to the wide polyline comes in with no width of its own.
    e.submit(AddLineCommand{{10, 0}, {20, 0}, 3});
    e.submit(JoinPickCommand{{{5, 0}, {15, 0}}, 0.3, 4});
    REQUIRE(picked_width_is(e, {5, 0}, 2.0));
    std::uint64_t seen = e.snapshot().status_version;
    ListQueryCommand lq;
    lq.selection = true;
    e.submit(lq);
    REQUIRE(wait_until(e, [&](const auto& s) { return s.status_version != seen; }));
    REQUIRE(e.snapshot().status.find("3 vertices") != std::string::npos);
    REQUIRE(e.snapshot().status.find("widths 2.0000-2.0000 0.0000-0.0000") != std::string::npos);

    seen = e.snapshot().status_version;
    e.submit(ExplodeSelectionCommand{5});
    REQUIRE(wait_until(e, [&](const auto& s) { return s.status_version != seen; }));
    REQUIRE(e.snapshot().status.find("lost width information") != std::string::npos);
    e.submit(UndoLastGroupCommand{});
    REQUIRE(picked_width_is(e, {5, 0}, 2.0)); // and UNDO restores it
    e.stop();
}

TEST_CASE("#52 PEDIT Width sets every segment; Reverse turns a taper round; Global width edits it") {
    GeometryEngine e;
    e.start();
    e.submit(AddPolylineCommand{{{0, 0}, {10, 0}, {10, 10}}, false, 1});
    REQUIRE(wait_until(e, [](const auto& s) { return !s.line_vertices.empty(); }));
    e.submit(PeditCommand{{5, 0}, 0.3, 8, {3.0, 0}, {}, 2});
    REQUIRE(picked_width_is(e, {5, 0}, 3.0));
    e.submit(UndoLastGroupCommand{});
    REQUIRE(picked_width_is(e, {5, 0}, 0.0));
    e.submit(RedoLastGroupCommand{});
    REQUIRE(picked_width_is(e, {5, 0}, 3.0));

    // The segment leaving the first vertex tapers 1 -> 4; reversed, the polyline ends with it.
    e.submit(PeditCommand{{5, 0}, 0.3, 9, {0, 0}, {1.0, 4.0}, 3});
    REQUIRE(picked_width_is(e, {5, 0}, 1.0));
    e.submit(PeditCommand{{5, 0}, 0.3, 2, {}, {}, 4});
    REQUIRE(picked_width_is(e, {5, 0}, 3.0)); // the first segment is now the old second one
    std::uint64_t seen = e.snapshot().status_version;
    ListQueryCommand lq;
    lq.selection = true;
    e.submit(lq);
    REQUIRE(wait_until(e, [&](const auto& s) { return s.status_version != seen; }));
    REQUIRE(e.snapshot().status.find("widths 3.0000-3.0000 4.0000-1.0000") != std::string::npos);

    // The Properties palette's Global width: one value for the whole polyline; 0 clears it.
    PropertyValue v;
    v.num = 0.5;
    e.submit(SetPropertyCommand{PropertyId::PlineWidth, v, 5});
    REQUIRE(picked_width_is(e, {5, 0}, 0.5));
    v.num = 0.0;
    e.submit(SetPropertyCommand{PropertyId::PlineWidth, v, 6});
    REQUIRE(picked_width_is(e, {5, 0}, 0.0));
    REQUIRE(wait_until(e, [](const auto& s) { return s.fill_vertices.empty(); }));
    e.stop();
}

TEST_CASE("#37 a wide polyline inside a block: saved with the block, filled where it is inserted") {
    io::Document doc;
    io::DocBlockDef b;
    b.name = "ARROW";
    io::DocPolyline p;
    p.points = {{0, 0}, {4, 0}};
    p.widths = {0, 2, 2, 2};
    b.polylines.push_back(p);
    doc.block_defs.push_back(b);
    io::DocInsert in;
    in.block_name = "ARROW";
    in.pos = {100, 50};
    in.scale_x = 2.0;
    in.scale_y = 2.0;
    doc.inserts.push_back(in);
    // The widths are part of the definition the file keeps.
    io::Document back;
    REQUIRE(io::parse_native(io::serialize_native(doc), back).ok);
    REQUIRE(back.block_defs.size() == 1);
    REQUIRE(back.block_defs[0].polylines.size() == 1);
    REQUIRE(back.block_defs[0].polylines[0].widths == p.widths);

    GeometryStore store;
    io::populate_store(store, back);
    const auto& arena = store.inserts();
    REQUIRE(arena.slot_count() == 1);
    const EntityHandle h{0, arena.generations()[0], EntityKind::Insert};
    std::vector<InsertSeg> segs;
    std::vector<InsertFill> fills;
    resolve_insert(store, *store.insert(h), 1e-3, segs, &fills);
    REQUIRE(fills.size() == 2); // one tapered band: two triangles
    double area = 0.0;
    for (const InsertFill& f : fills) {
        const Vec2 u = f.b - f.a;
        const Vec2 v = f.c - f.a;
        area += std::abs(u.x * v.y - u.y * v.x) * 0.5;
    }
    REQUIRE(area == Approx(4.0 * 4.0)); // (4 x 2 / 2) at twice the size
    // Without a fill channel (bounds, picking) and with FILLMODE off: the band's outline.
    segs.clear();
    resolve_insert(store, *store.insert(h), 1e-3, segs);
    bool wide_end = false;
    for (const InsertSeg& sg : segs) {
        wide_end = wide_end || (eq(sg.a, {108, 52}) && eq(sg.b, {108, 48})) ||
                   (eq(sg.b, {108, 52}) && eq(sg.a, {108, 48}));
    }
    REQUIRE(wide_end);
    store.set_fillmode(false);
    fills.clear();
    segs.clear();
    resolve_insert(store, *store.insert(h), 1e-3, segs, &fills);
    REQUIRE(fills.empty());
    REQUIRE(segs.size() >= 5);
}

TEST_CASE("#37 #36 the native format keeps widths, elevation, thickness and FILLMODE") {
    io::Document doc;
    io::DocPolyline a;
    a.points = {{0, 0}, {10, 0}, {10, 10}};
    a.bulges = {0.0, 0.5, 0.0};
    a.widths = {1, 1, 1, 3, 3, 3};
    a.elevation = 2.5;
    a.thickness = -1.0;
    doc.polylines.push_back(a);
    io::DocPolyline plain;
    plain.points = {{0, 0}, {1, 1}};
    doc.polylines.push_back(plain);
    io::DocPolyline flat; // straight, wide, no elevation
    flat.points = {{0, 0}, {5, 0}};
    flat.widths = {2, 2, 2, 2};
    doc.polylines.push_back(flat);
    doc.fillmode = false;

    const std::string text = io::serialize_native(doc);
    REQUIRE(text.find("FILLMODE 0") != std::string::npos);
    REQUIRE(text.find(" W 1 1 1 3 3 3 Z 2.5 -1") != std::string::npos);
    io::Document back;
    REQUIRE(io::parse_native(text, back).ok);
    REQUIRE(back.format_version == io::kFormatVersion);
    REQUIRE(back.polylines.size() == 3);
    REQUIRE(back.polylines[0] == a);
    REQUIRE(back.polylines[1] == plain);
    REQUIRE(back.polylines[2] == flat);
    REQUIRE(!back.fillmode);

    // A record whose width list does not match its vertices is refused, not guessed at.
    std::string bad = text;
    const std::size_t at = bad.find(" W 1 1 1 3 3 3");
    bad.replace(at, 14, " W 1 1 1");
    io::Document broken;
    REQUIRE(!io::parse_native(bad, broken).ok);
}

TEST_CASE("#37 #36 DXF: 43 for a constant width, 40 / 41 per vertex, 38 / 39, and the legacy POLYLINE") {
    io::Document doc;
    io::DocPolyline constant;
    constant.points = {{0, 0}, {10, 0}, {10, 10}};
    constant.widths.assign(6, 1.5);
    constant.elevation = 4.0;
    constant.thickness = 2.0;
    doc.polylines.push_back(constant);
    io::DocPolyline taper;
    taper.points = {{0, 20}, {10, 20}};
    taper.widths = {0, 3, 0, 0};
    doc.polylines.push_back(taper);

    const std::string dxf = io::serialize_dxf(doc);
    REQUIRE(dxf.find("\n43\n1.5\n") != std::string::npos);
    REQUIRE(dxf.find("\n38\n4\n") != std::string::npos);
    REQUIRE(dxf.find("\n39\n2\n") != std::string::npos);
    REQUIRE(dxf.find("\n41\n3\n") != std::string::npos);
    io::Document back;
    REQUIRE(io::parse_dxf(dxf, back).ok);
    REQUIRE(back.polylines.size() == 2);
    REQUIRE(back.polylines[0].widths == constant.widths);
    REQUIRE(back.polylines[0].elevation == Approx(4.0));
    REQUIRE(back.polylines[0].thickness == Approx(2.0));
    REQUIRE(back.polylines[1].widths == taper.widths);

    // The R12 form: default widths on the header, a vertex may override them.
    const std::string legacy =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nPOLYLINE\n8\n0\n66\n1\n70\n0\n40\n2.0\n41\n2.0\n"
        "0\nVERTEX\n8\n0\n10\n0.0\n20\n0.0\n"
        "0\nVERTEX\n8\n0\n10\n10.0\n20\n0.0\n40\n2.0\n41\n0.0\n"
        "0\nVERTEX\n8\n0\n10\n10.0\n20\n10.0\n"
        "0\nSEQEND\n0\nENDSEC\n0\nEOF\n";
    io::Document old;
    REQUIRE(io::parse_dxf(legacy, old).ok);
    REQUIRE(old.polylines.size() == 1);
    REQUIRE(old.polylines[0].widths == std::vector<double>{2, 2, 2, 0, 2, 2});
}

TEST_CASE("#37 PLINE Width and Halfwidth: the prompts, the taper, the width left in force") {
    H h;
    h.run({"PLINEWID", "0"});
    h.run({"PL", "0,0"});
    REQUIRE(h.out.any_contains("Current line-width is 0.0000"));
    REQUIRE(h.out.prompt == "Specify next point or [Arc/Halfwidth/Length/Undo/Width]: ");
    h.proc.submit_line("W");
    REQUIRE(h.out.prompt == "Specify starting width <0.0000>: ");
    h.proc.submit_line("2");
    REQUIRE(h.out.prompt == "Specify ending width <2.0000>: ");
    h.proc.submit_line(""); // the starting width
    REQUIRE(h.out.prompt == "Specify next point or [Arc/Halfwidth/Length/Undo/Width]: ");
    h.proc.submit_line("10,0");
    REQUIRE(h.out.prompt == "Specify next point or [Arc/Close/Halfwidth/Length/Undo/Width]: ");
    h.proc.submit_line("HALFWIDTH");
    REQUIRE(h.out.prompt == "Specify starting half-width <1.0000>: ");
    h.proc.submit_line("0.5");
    REQUIRE(h.out.prompt == "Specify ending half-width <0.5000>: ");
    h.proc.submit_line("2");
    h.proc.submit_line("10,10");
    h.proc.submit_line("0,10");
    h.proc.submit_line("");
    const auto* p = h.last<AddPolylineCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->points.size() == 4);
    REQUIRE(p->widths == std::vector<double>{2, 2, 1, 4, 4, 4, 4, 4});

    // The next polyline starts as wide as the last one ended.
    h.out.lines.clear();
    h.run({"PL", "0,0", "5,0", ""});
    REQUIRE(h.out.any_contains("Current line-width is 4.0000"));
    REQUIRE(h.last<AddPolylineCommand>()->widths == std::vector<double>(4, 4.0));

    // A negative width is refused; a width can be shown by a point.
    h.run({"PLINEWID", "0"});
    h.run({"PL", "0,0", "W", "-1"});
    REQUIRE(h.out.any_contains("Value must be positive or zero."));
    REQUIRE(h.out.prompt == "Specify starting width <0.0000>: ");
    h.run({"3,4", "", "10,0", ""}); // 5 from the last vertex
    REQUIRE(h.last<AddPolylineCommand>()->widths == std::vector<double>(4, 5.0));
    h.run({"PLINEWID", "0"});
    REQUIRE(musacad::command::PolylineCommand::s_width_ == 0.0);
}

TEST_CASE("#37 PLINE: widths in arc mode, Undo takes the segment's widths, Close carries the width") {
    H h;
    h.run({"PLINEWID", "0"});
    h.run({"PL", "0,0", "10,0", "A"});
    REQUIRE(h.out.prompt ==
            "Specify endpoint of arc (hold Ctrl to switch direction) or "
            "[Angle/CEnter/CLose/Direction/Halfwidth/Line/Radius/Second pt/Undo/Width]: ");
    h.run({"W", "1", "3"});
    REQUIRE(h.out.prompt.rfind("Specify endpoint of arc", 0) == 0); // back in arc mode
    h.run({"10,10", "U", "10,10", "CL"});
    const auto* p = h.last<AddPolylineCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->closed);
    REQUIRE(p->points.size() == 3);
    // Plain, then the 1 -> 3 taper is undone with its segment and the arc drawn again
    // takes the width in force (3); the closing arc too.
    REQUIRE(p->widths == std::vector<double>{0, 0, 3, 3, 3, 3});
    h.run({"PLINEWID", "0"});
}

TEST_CASE("#37 Dynamic Input on PLINE: length and angle from the last vertex") {
    using namespace musacad::command;
    H h;
    h.run({"PLINEWID", "0"});
    h.run({"PL", "0,0", "10,0"});
    const PreviewSpec pv = h.proc.preview();
    REQUIRE(pv.kind == PreviewKind::Polyline);
    REQUIRE(pv.pline_dyn);
    REQUIRE(dyn_segment_like(pv));
    const std::vector<DynField> f = dyn_fields(pv, {10, 5});
    REQUIRE(f.size() == 2);
    REQUIRE(f[0].value == Approx(5.0));
    REQUIRE(f[1].value == Approx(90.0));
    REQUIRE(compose_dyn_submit(pv, {10, 5}, 7.0, std::nullopt).rfind("@7", 0) == 0);
    const Vec2 locked = apply_dyn_lock(pv, {10, 5}, 7.0, std::nullopt);
    REQUIRE(locked.x == Approx(10.0));
    REQUIRE(locked.y == Approx(7.0));
    // An arc step and a value prompt are not length + angle drags.
    h.proc.submit_line("A");
    REQUIRE(!dyn_segment_like(h.proc.preview()));
    h.run({"L", "W"});
    REQUIRE(!dyn_segment_like(h.proc.preview()));
    h.proc.cancel();
}

TEST_CASE("#36 RECTANG Width, Elevation and Thickness: prompts, the polyline made, the modes echoed") {
    H h;
    h.proc.submit_line("REC");
    REQUIRE(h.out.prompt == "Specify first corner point or [Chamfer/Elevation/Fillet/Thickness/Width]: ");
    h.proc.submit_line("W");
    REQUIRE(h.out.prompt == "Specify line width for rectangles <0.0000>: ");
    h.proc.submit_line("0.5");
    REQUIRE(h.out.prompt == "Specify first corner point or [Chamfer/Elevation/Fillet/Thickness/Width]: ");
    h.proc.submit_line("E");
    REQUIRE(h.out.prompt == "Specify the elevation for rectangles <0.0000>: ");
    h.proc.submit_line("2");
    h.proc.submit_line("T");
    REQUIRE(h.out.prompt == "Specify thickness for rectangles <0.0000>: ");
    h.proc.submit_line("3");
    h.run({"0,0", "10,5"});
    const auto* p = h.last<AddPolylineCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->closed);
    REQUIRE(p->points.size() == 4);
    REQUIRE(p->widths == std::vector<double>(8, 0.5));
    REQUIRE(p->elevation == 2.0);
    REQUIRE(p->thickness == 3.0);

    // The values stay in force and are said at the start.
    h.out.lines.clear();
    h.proc.submit_line("RECTANG");
    REQUIRE(h.out.any_contains("Current rectangle modes: Elevation=2.0000  Thickness=3.0000  Width=0.5000"));
    h.proc.submit_line("W");
    REQUIRE(h.out.prompt == "Specify line width for rectangles <0.5000>: ");
    h.proc.submit_line("-2");
    REQUIRE(h.out.any_contains("Enter a width of zero or more."));
    h.run({"0", "E", "0", "T", "0", "0,0", "4,4"});
    const auto* q = h.last<AddPolylineCommand>();
    REQUIRE(q->widths.empty());
    REQUIRE(q->elevation == 0.0);
    REQUIRE(q->thickness == 0.0);
}

TEST_CASE("#52 PEDIT Width and the vertex Width option submit the width edits") {
    H h;
    h.run({"PE", "5,0"});
    REQUIRE(h.out.prompt ==
            "Enter an option [Close/Open/Join/Width/Edit vertex/Fit/Spline/Decurve/Ltype gen/Reverse/Undo]: ");
    h.proc.submit_line("W");
    REQUIRE(h.out.prompt == "Specify new width for all segments: ");
    h.proc.submit_line("-1");
    REQUIRE(h.count<PeditCommand>() == 0);
    h.proc.submit_line("2.5");
    const auto* w = h.last<PeditCommand>();
    REQUIRE(w != nullptr);
    REQUIRE(w->op == 8);
    REQUIRE(w->p1.x == 2.5);
    h.proc.submit_line("E");
    REQUIRE(h.out.prompt == "Enter a vertex editing option [Insert/Delete/Move/Width/eXit] <X>: ");
    h.run({"W", "10,0"});
    REQUIRE(h.out.prompt == "Specify starting width for next segment <0.0000>: ");
    h.proc.submit_line("1");
    REQUIRE(h.out.prompt == "Specify ending width for next segment <1.0000>: ");
    h.proc.submit_line("4");
    const auto* v = h.last<PeditCommand>();
    REQUIRE(v->op == 9);
    REQUIRE(v->p1.x == 10.0);
    REQUIRE(v->p2.x == 1.0);
    REQUIRE(v->p2.y == 4.0);
    h.run({"X", ""});
    REQUIRE(!h.proc.has_active_command());
}

TEST_CASE("#40 DONUT is a closed two-arc polyline as wide as the ring; diameters by two points") {
    H h;
    h.run({"DO", "0.5", "1", "10,10", "30,10", ""});
    REQUIRE(h.count<AddPolylineCommand>() == 2);
    REQUIRE(h.count<AddHatchCommand>() == 0);
    const auto* d = h.last<AddPolylineCommand>();
    REQUIRE(d->closed);
    REQUIRE(d->points.size() == 2);
    REQUIRE(d->points[0].x == Approx(30.0 - 0.375));
    REQUIRE(d->points[1].x == Approx(30.0 + 0.375));
    REQUIRE(d->points[0].y == Approx(10.0));
    REQUIRE(d->bulges == std::vector<double>{1.0, 1.0});
    REQUIRE(d->widths == std::vector<double>(4, 0.25));
    REQUIRE(!h.proc.has_active_command());

    // A filled disc: no hole, the band reaches the centre.
    H h2;
    h2.run({"DONUT", "0", "4", "0,0", ""});
    REQUIRE(h2.last<AddPolylineCommand>()->widths == std::vector<double>(4, 2.0));
    REQUIRE(h2.last<AddPolylineCommand>()->points[1].x == Approx(1.0));

    // The outside diameter must exceed the inside one.
    H h3;
    h3.run({"DONUT", "5", "3"});
    REQUIRE(h3.proc.has_active_command());
    REQUIRE(h3.count<AddPolylineCommand>() == 0);
    REQUIRE(h3.out.any_contains("Value must be greater than the inside diameter."));
    h3.proc.cancel();

    // Each diameter shown by two points.
    H h4;
    h4.run({"DONUT", "0,0"});
    REQUIRE(h4.out.prompt == "Specify second point: ");
    h4.run({"2,0", "0,0", "6,0", "0,0", ""});
    REQUIRE(h4.last<AddPolylineCommand>()->widths == std::vector<double>(4, 2.0));
    REQUIRE(h4.last<AddPolylineCommand>()->points[1].x == Approx(2.0));
    // put the session defaults back
    H h5;
    h5.run({"DONUT", "0.5", "1", ""});
}

TEST_CASE("#37 FILL and FILLMODE: the prompts and what they submit") {
    H h;
    h.proc.submit_line("FILL");
    REQUIRE(h.out.prompt == "Enter mode [ON/OFF] <ON>: ");
    h.proc.submit_line("maybe");
    REQUIRE(h.proc.has_active_command());
    h.proc.submit_line("off");
    REQUIRE(h.last<SetFillModeCommand>() != nullptr);
    REQUIRE(!h.last<SetFillModeCommand>()->on);
    REQUIRE(!h.proc.has_active_command());

    h.proc.set_fillmode(false);
    h.proc.submit_line("FILLMODE");
    REQUIRE(h.out.prompt == "Enter new value for FILLMODE <0>: ");
    h.proc.submit_line("2");
    REQUIRE(h.proc.has_active_command());
    h.proc.submit_line("1");
    REQUIRE(h.last<SetFillModeCommand>()->on);
    REQUIRE(!h.proc.has_active_command());

    // Enter keeps the mode: nothing is submitted.
    const int before = h.count<SetFillModeCommand>();
    h.run({"FILL", ""});
    REQUIRE(h.count<SetFillModeCommand>() == before);
}

TEST_CASE("#37 MATCHPROP copies a polyline's width while its Polyline setting is on") {
    const Command src = wide({{0, 0}, {10, 0}}, 2.0, 1);
    Command dst = AddPolylineCommand{{{0, 0}, {5, 0}, {5, 5}}, false, 2};
    MatchPropFilter filter;
    REQUIRE(match_properties(src, dst, filter) > 0);
    REQUIRE(std::get<AddPolylineCommand>(dst).widths == std::vector<double>(6, 2.0));

    Command kept = AddPolylineCommand{{{0, 0}, {5, 0}, {5, 5}}, false, 3};
    filter.polyline = false;
    REQUIRE(match_properties(src, kept, filter) >= 0);
    REQUIRE(std::get<AddPolylineCommand>(kept).widths.empty());

    // A line has no width to take.
    Command line = AddLineCommand{{0, 0}, {1, 1}, 4};
    filter.polyline = true;
    REQUIRE(match_properties(src, line, filter) >= 0);
    REQUIRE(std::holds_alternative<AddLineCommand>(line));
}

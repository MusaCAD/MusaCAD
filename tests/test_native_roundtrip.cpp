// Native .musa format: lossless round-trip of every entity family, and
// fail-safe handling of malformed input.

#include <cmath>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/block_resolve.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/render_snapshot.hpp"

using namespace musacad::core;
using namespace musacad::core::io;

namespace {
// Builds a store containing one of every entity family with non-trivial params.
GeometryStore make_full_store() {
    GeometryStore s;
    s.add_point({1.5, -2.25});
    s.add_line({0.0, 0.0}, {10.0, 5.0});
    s.add_circle({3.0, 4.0}, 2.5);
    s.add_arc({-1.0, 2.0}, 4.0, 0.123456789, 2.987654321);
    s.add_polyline(std::vector<Vec2>{{0, 0}, {1, 0}, {1, 1}}, false);
    s.add_polyline(std::vector<Vec2>{{5, 5}, {6, 5}, {6, 6}, {5, 6}}, true);
    s.add_spline(std::vector<Vec2>{{0, 0}, {1, 2}, {3, 1}, {4, 4}}, 3);
    return s;
}
} // namespace

TEST_CASE("Native round-trip: store -> doc -> save -> load -> doc matches exactly") {
    const GeometryStore original = make_full_store();
    const Document doc1 = document_from_store(original);
    REQUIRE(doc1.entity_count() == 7);

    const std::string text = serialize_native(doc1);
    Document doc2;
    const IoResult parsed = parse_native(text, doc2);
    REQUIRE(parsed.ok);
    REQUIRE(doc2 == doc1); // exact equality (to_chars/from_chars is lossless)
}

TEST_CASE("HATCH round-trips through native + DXF (boundary loops + pattern params)") {
    Document doc;
    DocHatch h;
    h.loops.push_back({{0, 0}, {10, 0}, {10, 10}, {0, 10}}); // outer
    h.loops.push_back({{3, 3}, {6, 3}, {6, 6}, {3, 6}});     // island
    h.pattern_name = "ANSI31";
    h.pattern_scale = 1.5;
    h.pattern_angle = 0.7853981633974483; // 45 deg
    h.pattern_origin = {2.0, 1.0};
    doc.hatches.push_back(h);

    SECTION("native is exact") {
        Document rt;
        REQUIRE(parse_native(serialize_native(doc), rt).ok);
        REQUIRE(rt.hatches.size() == 1);
        REQUIRE(rt.hatches[0] == h); // loops + name + scale + angle + origin exact
    }
    SECTION("DXF preserves the boundary + pattern") {
        Document rt;
        REQUIRE(parse_dxf(serialize_dxf(doc), rt).ok);
        REQUIRE(rt.hatches.size() == 1);
        REQUIRE(rt.hatches[0].loops.size() == 2);
        REQUIRE(rt.hatches[0].loops[0].size() == 4);
        REQUIRE(rt.hatches[0].loops[1].size() == 4);
        REQUIRE(rt.hatches[0].pattern_name == "ANSI31");
        REQUIRE(std::abs(rt.hatches[0].pattern_scale - 1.5) < 1e-9);
        REQUIRE(std::abs(rt.hatches[0].pattern_angle - 0.7853981633974483) < 1e-6);
        REQUIRE(length(rt.hatches[0].pattern_origin - Vec2{2.0, 1.0}) < 1e-6);
    }
    SECTION("through the store: add_hatch -> doc -> native -> store") {
        GeometryStore s;
        s.add_hatch(h.loops, h.pattern_name, h.pattern_scale, h.pattern_angle, h.pattern_origin);
        Document rt;
        REQUIRE(parse_native(serialize_native(document_from_store(s)), rt).ok);
        REQUIRE(rt.hatches.size() == 1);
        REQUIRE(rt.hatches[0] == h);
    }
}

TEST_CASE("Native round-trip through the store: save -> clear -> load -> store matches") {
    const GeometryStore original = make_full_store();
    const Document doc1 = document_from_store(original);

    const auto path =
        (std::filesystem::temp_directory_path() / "musacad_roundtrip.musa").string();
    REQUIRE(save_native(doc1, path).ok);

    GeometryStore loaded; // a fresh, empty store
    Document doc2;
    REQUIRE(load_native(path, doc2).ok);
    populate_store(loaded, doc2);

    REQUIRE(document_from_store(loaded) == doc1);
    std::filesystem::remove(path);
}

TEST_CASE("Empty document round-trips") {
    Document empty;
    Document out;
    REQUIRE(parse_native(serialize_native(empty), out).ok);
    REQUIRE(out == empty);
    REQUIRE(out.empty());
}

TEST_CASE("Malformed native input fails gracefully, leaves output untouched") {
    Document out;
    out.lines.push_back(DocLine{{9, 9}, {9, 9}}); // sentinel: must be preserved on failure

    SECTION("not a musa file") {
        const IoResult r = parse_native("hello world\n", out);
        REQUIRE_FALSE(r.ok);
        REQUIRE_FALSE(r.message.empty());
    }
    SECTION("truncated line record") {
        const IoResult r = parse_native("MUSACAD 1\nLINE 0 0 10\nEND\n", out);
        REQUIRE_FALSE(r.ok);
    }
    SECTION("missing END") {
        const IoResult r = parse_native("MUSACAD 1\nLINE 0 0 1 1\n", out);
        REQUIRE_FALSE(r.ok);
    }
    SECTION("future version rejected") {
        const IoResult r = parse_native("MUSACAD 999\nEND\n", out);
        REQUIRE_FALSE(r.ok);
    }
    SECTION("polyline vertex-count mismatch") {
        const IoResult r = parse_native("MUSACAD 1\nPOLYLINE 0 3 0 0 1 1\nEND\n", out);
        REQUIRE_FALSE(r.ok);
    }
    // The output document was not mutated by any failed parse.
    REQUIRE(out.lines.size() == 1);
    REQUIRE(out.lines[0].a == Vec2{9, 9});
}

TEST_CASE("Loading a missing file fails gracefully") {
    Document out;
    const IoResult r = load_native("/nonexistent/path/should/not/exist.musa", out);
    REQUIRE_FALSE(r.ok);
    REQUIRE_FALSE(r.message.empty());
}

TEST_CASE("Native round-trip: per-dimension overrides preserved; older DIM loads as ByStyle") {
    using namespace musacad::core;
    using namespace musacad::core::io;
    Document doc;
    DocDim d;
    d.type = static_cast<std::uint8_t>(DimType::Linear);
    d.a = {0, 0};
    d.b = {20, 0};
    d.line_pt = {10, 4};
    d.style = 0;
    d.overrides.set(DimOverrides::kArrowSize, true);
    d.overrides.arrow_size = 9.0;
    d.overrides.set(DimOverrides::kTextColor, true);
    d.overrides.text_color = {0, 128, 255};
    doc.dims.push_back(d);

    Document back;
    REQUIRE(parse_native(serialize_native(doc), back).ok);
    REQUIRE(back.dims.size() == 1);
    REQUIRE(back.dims[0].overrides == d.overrides); // lossless
    REQUIRE(back.dims[0].overrides.has(DimOverrides::kArrowSize));
    REQUIRE(back.dims[0].overrides.arrow_size == 9.0);

    // An older (pre-v8) DIM line without the override block loads as all-ByStyle.
    std::string text = serialize_native(doc);
    // Strip the trailing 15 override tokens from the single DIM line.
    const std::size_t dpos = text.find("\nDIM ");
    REQUIRE(dpos != std::string::npos);
    std::size_t eol = text.find('\n', dpos + 1);
    std::string line = text.substr(dpos + 1, eol - (dpos + 1));
    // keep first 16 tokens
    std::size_t cut = 0;
    int spaces = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == ' ' && ++spaces == 16) {
            cut = i;
            break;
        }
    }
    REQUIRE(cut > 0);
    text.replace(dpos + 1, eol - (dpos + 1), line.substr(0, cut));
    Document older;
    REQUIRE(parse_native(text, older).ok);
    REQUIRE(older.dims.size() == 1);
    REQUIRE(older.dims[0].overrides.mask == 0); // ByStyle
}

TEST_CASE("Native round-trip: PAGESETUP records survive save/load exactly") {
    Document doc; // defaults: layer 0, Standard dimstyle
    PageSetup a;
    a.name = "Title Block A3";        // spaces in the three free-form strings
    a.paper = "ANSI B (Tabloid)";
    a.target = "HP LaserJet 400";
    a.paper_w_mm = 431.8;
    a.paper_h_mm = 279.4;
    a.landscape = true;
    a.area = 2; // Window
    a.win_min = {-12.5, 3.25};
    a.win_max = {840.0, 590.5};
    a.fit = false;
    a.scale_num = 1.0;
    a.scale_den = 50.0;
    a.center = false;
    a.off_x_mm = 7.5;
    a.off_y_mm = -3.0;
    a.plot_lineweights = false;
    a.style = 1; // Monochrome
    PageSetup b;  // a second, mostly-default one (defaults to PDF/ISO A4)
    b.name = "Draft PDF";
    b.style = 2; // Grayscale
    doc.page_setups = {a, b};

    const std::string text = serialize_native(doc);
    Document loaded;
    REQUIRE(parse_native(text, loaded).ok);
    REQUIRE(loaded.page_setups.size() == 2);
    REQUIRE(loaded.page_setups[0] == a);
    REQUIRE(loaded.page_setups[1] == b);
}

TEST_CASE("Native: older files (no PAGESETUP) load with no page setups") {
    Document doc;
    const std::string text = serialize_native(doc); // an empty drawing writes no PAGESETUP
    REQUIRE(text.find("PAGESETUP") == std::string::npos);
    Document loaded;
    REQUIRE(parse_native(text, loaded).ok);
    REQUIRE(loaded.page_setups.empty());
}

TEST_CASE("Page setups travel through the store (add replaces same name)") {
    GeometryStore s;
    PageSetup p;
    p.name = "S1";
    p.scale_den = 10.0;
    s.add_page_setup(p);
    p.scale_den = 20.0; // same name -> replace, not append
    s.add_page_setup(p);
    REQUIRE(s.page_setups().size() == 1);
    REQUIRE(s.page_setups()[0].scale_den == 20.0);

    const Document doc = document_from_store(s);
    REQUIRE(doc.page_setups.size() == 1);
    GeometryStore s2;
    populate_store(s2, doc);
    REQUIRE(s2.page_setups().size() == 1);
    REQUIRE(s2.page_setups()[0].name == "S1");
}

TEST_CASE("Native round-trip: a block def's NESTED INSERT survives save/load") {
    // Mirrors the real DWG-imported structure: model INSERT -> container block A ->
    // nested INSERT -> geometry block B. Regression for blocks losing nested content
    // (which orphans all the geometry and plots an empty sheet).
    Document doc;
    DocBlockDef b;
    b.name = "B";
    b.lines.push_back(DocLine{{0.0, 0.0}, {10.0, 0.0}, {}});
    DocBlockDef a;
    a.name = "A";
    a.inserts.push_back(DocInsert{"B", {0.0, 0.0}, 1.0, 1.0, 0.0, {}});
    doc.block_defs = {b, a};
    doc.inserts.push_back(DocInsert{"A", {5.0, 5.0}, 1.0, 1.0, 0.0, {}});

    const std::string text = serialize_native(doc);
    Document loaded;
    REQUIRE(parse_native(text, loaded).ok);
    REQUIRE(loaded.block_defs.size() == 2);
    // Find block A in the reloaded doc and assert its nested insert survived.
    const DocBlockDef* la = nullptr;
    for (const DocBlockDef& d : loaded.block_defs) {
        if (d.name == "A") {
            la = &d;
        }
    }
    REQUIRE(la != nullptr);
    REQUIRE(la->inserts.size() == 1);            // <-- the nested insert must survive
    REQUIRE(la->inserts[0].block_name == "B");

    // And it must resolve to B's line through the store (the path render/plot use).
    GeometryStore s;
    populate_store(s, loaded);
    const auto& arena = s.inserts();
    std::size_t total_segs = 0;
    std::vector<InsertSeg> segs;
    for (std::uint32_t i = 0; i < arena.slot_count(); ++i) {
        if (!arena.alive(i)) {
            continue;
        }
        segs.clear();
        resolve_insert(s, arena.data()[i], 0.1, segs);
        total_segs += segs.size();
    }
    REQUIRE(total_segs >= 1); // the model insert -> A -> B -> one line segment
}

TEST_CASE("DXF round-trip: a block def's NESTED INSERT survives import") {
    // Same structure as the native test, but through DXF (the DWG import path). If a
    // block's nested INSERT is dropped on import, the container block comes in empty and
    // all the geometry it would reach is orphaned -> the symptom on real DWG files.
    Document doc;
    DocBlockDef b;
    b.name = "B";
    b.lines.push_back(DocLine{{0.0, 0.0}, {10.0, 0.0}, {}});
    DocBlockDef a;
    a.name = "A";
    a.inserts.push_back(DocInsert{"B", {0.0, 0.0}, 1.0, 1.0, 0.0, {}});
    doc.block_defs = {b, a};
    doc.inserts.push_back(DocInsert{"A", {5.0, 5.0}, 1.0, 1.0, 0.0, {}});

    const std::string dxf = serialize_dxf(doc);
    Document loaded;
    REQUIRE(parse_dxf(dxf, loaded).ok);
    const DocBlockDef* la = nullptr;
    for (const DocBlockDef& d : loaded.block_defs) {
        if (d.name == "A") {
            la = &d;
        }
    }
    REQUIRE(la != nullptr);
    REQUIRE(la->inserts.size() == 1);          // nested insert must survive DXF import
    REQUIRE(la->inserts[0].block_name == "B");
}

TEST_CASE("DXF import: SPLINE control points + ELLIPSE tessellation are geometrically sane") {
    // A minimal DXF with one SPLINE (4 control points, degree 3) and one ELLIPSE
    // (centre 100,100; major axis (20,0); ratio 0.5; full). Verifies the importer reads
    // the right group codes -- no stray (0,0) vertices, points near where they belong.
    const std::string dxf =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nSPLINE\n8\n0\n71\n3\n"
        "10\n0.0\n20\n0.0\n30\n0.0\n"
        "10\n1.0\n20\n2.0\n30\n0.0\n"
        "10\n3.0\n20\n2.0\n30\n0.0\n"
        "10\n4.0\n20\n0.0\n30\n0.0\n"
        "0\nELLIPSE\n8\n0\n10\n100.0\n20\n100.0\n30\n0.0\n"
        "11\n20.0\n21\n0.0\n31\n0.0\n40\n0.5\n41\n0.0\n42\n6.283185307\n"
        "0\nENDSEC\n0\nEOF\n";
    Document doc;
    REQUIRE(parse_dxf(dxf, doc).ok);

    // SPLINE -> a tessellated polyline (de Boor). Endpoints clamp to the first/last control
    // point; every sample stays within the control polygon's bounds (x in [0,4], y in [0,2]).
    REQUIRE(doc.polylines.size() == 2); // spline + ellipse
    const DocPolyline& sp = doc.polylines[0];
    REQUIRE(sp.points.size() >= 16);
    REQUIRE(std::abs(sp.points.front().x - 0.0) < 1e-6);
    REQUIRE(std::abs(sp.points.front().y - 0.0) < 1e-6);
    REQUIRE(std::abs(sp.points.back().x - 4.0) < 1e-6);
    REQUIRE(std::abs(sp.points.back().y - 0.0) < 1e-6);
    for (const Vec2& p : sp.points) {
        REQUIRE(p.x >= -0.001);
        REQUIRE(p.x <= 4.001);
        REQUIRE(p.y >= -0.001);
        REQUIRE(p.y <= 2.001);
    }

    const DocPolyline& e = doc.polylines[1]; // ellipse -> polyline
    REQUIRE(e.closed);
    REQUIRE(e.points.size() >= 24);
    // Every vertex must lie on the ellipse around (100,100), |x-100|<=20, |y-100|<=10.
    for (const Vec2& p : e.points) {
        REQUIRE(std::abs(p.x - 100.0) <= 20.001);
        REQUIRE(std::abs(p.y - 100.0) <= 10.001);
    }
}

TEST_CASE("Plot line-batch convention: for_each_line_segment yields exact pairs, no phantoms") {
    // line_batches are in SEGMENT units. A consumer that treats first/count as VERTEX
    // indices pairs the end of one segment with the start of the next -> phantom connector
    // lines across batches (the "random lines" in plots). This locks the convention.
    RenderSnapshot snap;
    // Batch 0: 3 segments (ODD count -> the next batch's `first` is odd, the bug trigger).
    const std::vector<Vec2> b0 = {{0, 0}, {1, 0}, {2, 2}, {3, 2}, {4, 4}, {5, 4}};
    // Batch 1: 2 segments, far away (a phantom connector to these would span the gap).
    const std::vector<Vec2> b1 = {{100, 100}, {101, 100}, {102, 102}, {103, 102}};
    snap.line_vertices.insert(snap.line_vertices.end(), b0.begin(), b0.end());
    snap.line_vertices.insert(snap.line_vertices.end(), b1.begin(), b1.end());
    snap.line_batches.push_back(ColorBatch{{255, 0, 0}, 0, 3, 25}); // first=0 seg, count=3 seg
    snap.line_batches.push_back(ColorBatch{{0, 255, 0}, 3, 2, 25}); // first=3 seg, count=2 seg

    std::vector<std::pair<Vec2, Vec2>> got;
    for (const ColorBatch& b : snap.line_batches) {
        for_each_line_segment(snap, b, [&](const Vec2& a, const Vec2& c) { got.emplace_back(a, c); });
    }
    REQUIRE(got.size() == 5); // 3 + 2 segments, none dropped
    // Every returned pair is an original (even-indexed) segment -- no end-to-start phantom.
    REQUIRE(got[0] == std::pair{Vec2{0, 0}, Vec2{1, 0}});
    REQUIRE(got[2] == std::pair{Vec2{4, 4}, Vec2{5, 4}});  // last of batch 0
    REQUIRE(got[3] == std::pair{Vec2{100, 100}, Vec2{101, 100}}); // first of batch 1 (no phantom from (5,4))
    REQUIRE(got[4] == std::pair{Vec2{102, 102}, Vec2{103, 102}});
    // No segment spans the 0..100 gap (a phantom connector would).
    for (const auto& [a, c] : got) {
        REQUIRE(length(c - a) < 5.0);
    }
}

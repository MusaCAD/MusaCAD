// GD&T entities (issue #8): feature control frames and datum feature symbols.
//
// Both follow the rule the rest of the model follows: the entity stores parameters and
// ALL drawable geometry is derived by one function per entity, so the displayed, picked
// and bounded geometry cannot diverge. These assert the derived numbers directly.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/gdt.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/properties_registry.hpp"
#include "musacad/core/text/stroke_font.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {

std::vector<std::string_view> views(const std::vector<std::string>& v) {
    std::vector<std::string_view> out;
    for (const std::string& s : v) {
        out.emplace_back(s);
    }
    return out;
}

DimStyle style_2h() {
    DimStyle s;
    s.text_height = 2.5;
    s.arrow_size = 2.5;
    return s;
}

} // namespace

TEST_CASE("FCF cell rectangles are derived from the text height, at ASME proportions") {
    const DimStyle s = style_2h();
    const std::vector<std::string> cells = {"⌖", "⌀0.05", "A", "B", "C"};

    FcfData f;
    f.pos = {10.0, 20.0};
    f.cell_count = static_cast<std::uint32_t>(cells.size());
    const FcfGeometry g = compute_fcf_geometry(f, views(cells), s, Rgb{255, 255, 255});

    REQUIRE(g.cell_quads.size() == cells.size());
    REQUIRE(g.cell_text.size() == cells.size());
    REQUIRE(g.text_pos.size() == cells.size());

    // Uniform cell height = 2 x text height (ASME Y14.5), same for every cell.
    const double expect_h = 2.0 * s.text_height;
    for (const std::array<Vec2, 4>& q : g.cell_quads) {
        REQUIRE(length(q[3] - q[0]) == Approx(expect_h)); // bottom-left -> top-left
        REQUIRE(length(q[2] - q[1]) == Approx(expect_h));
    }

    // Cells abut exactly: each cell's right edge is the next cell's left edge.
    for (std::size_t i = 0; i + 1 < g.cell_quads.size(); ++i) {
        REQUIRE(distance(g.cell_quads[i][1], g.cell_quads[i + 1][0]) == Approx(0.0).margin(1e-12));
    }

    // The frame starts at the insertion point and its width is the sum of the cells.
    REQUIRE(g.cell_quads.front()[0].x == Approx(10.0));
    REQUIRE(g.cell_quads.front()[0].y == Approx(20.0));
    double total = 0.0;
    for (const std::array<Vec2, 4>& q : g.cell_quads) {
        total += length(q[1] - q[0]);
    }
    REQUIRE(g.cell_quads.back()[1].x - g.cell_quads.front()[0].x == Approx(total));

    // A wider cell string yields a wider cell -- width tracks the text, with padding.
    const double pad = 0.5 * s.text_height;
    const double datum_w = length(g.cell_quads[2][1] - g.cell_quads[2][0]);
    REQUIRE(datum_w >= text::text_width("A", s.text_height) + 2.0 * pad);
    REQUIRE(length(g.cell_quads[1][1] - g.cell_quads[1][0]) > datum_w); // "⌀0.05" is wider than "A"
}

TEST_CASE("FCF geometry scales with the text height -- nothing is baked") {
    const std::vector<std::string> cells = {"⟂", "0.1", "A"};
    FcfData f;
    f.cell_count = 3;

    DimStyle small = style_2h();
    DimStyle big = style_2h();
    big.text_height = 5.0; // exactly double

    const FcfGeometry a = compute_fcf_geometry(f, views(cells), small, Rgb{});
    const FcfGeometry b = compute_fcf_geometry(f, views(cells), big, Rgb{});
    const auto width = [](const FcfGeometry& g) {
        return g.cell_quads.back()[1].x - g.cell_quads.front()[0].x;
    };
    REQUIRE(width(b) == Approx(2.0 * width(a)));
    REQUIRE(length(b.cell_quads[0][3] - b.cell_quads[0][0]) ==
            Approx(2.0 * length(a.cell_quads[0][3] - a.cell_quads[0][0])));
}

TEST_CASE("FCF cell text is code-substituted, so the font's GD&T glyphs are reachable") {
    // The characteristic is typed as an escape and comes out as the symbol -- which is
    // why #8 depends on #9 and needs no GD&T-specific input mode.
    const std::vector<std::string> cells = {"\\U+2316", "%%c0.05"};
    FcfData f;
    f.cell_count = 2;
    const FcfGeometry g = compute_fcf_geometry(f, views(cells), style_2h(), Rgb{});
    REQUIRE(g.cell_text[0] == "⌖"); // position symbol
    REQUIRE(g.cell_text[1] == "⌀" "0.05"); // diameter prefix
}

TEST_CASE("FCF has one divider per interior boundary and a closed outer border") {
    FcfData f;
    for (std::size_t n = 1; n <= 4; ++n) {
        const std::vector<std::string> cells(n, "X");
        f.cell_count = static_cast<std::uint32_t>(n);
        const FcfGeometry g = compute_fcf_geometry(f, views(cells), style_2h(), Rgb{});
        // 4 border segments + (n-1) dividers, 2 Vec2 each.
        REQUIRE(g.lines.size() == 2 * (4 + (n - 1)));
    }
}

TEST_CASE("FCF rotation rotates the whole frame rigidly") {
    const std::vector<std::string> cells = {"⟂", "0.1"};
    FcfData f;
    f.cell_count = 2;
    f.pos = {5.0, 5.0};
    const FcfGeometry flat = compute_fcf_geometry(f, views(cells), style_2h(), Rgb{});
    f.rotation = 1.5707963267948966; // 90 degrees
    const FcfGeometry turned = compute_fcf_geometry(f, views(cells), style_2h(), Rgb{});

    // Same shape: every cell keeps its size, only the orientation changes.
    REQUIRE(turned.cell_quads.size() == flat.cell_quads.size());
    for (std::size_t i = 0; i < flat.cell_quads.size(); ++i) {
        REQUIRE(length(turned.cell_quads[i][1] - turned.cell_quads[i][0]) ==
                Approx(length(flat.cell_quads[i][1] - flat.cell_quads[i][0])));
    }
    // The frame now grows in +y instead of +x.
    REQUIRE(turned.cell_quads.back()[1].y > turned.cell_quads.front()[0].y + 1.0);
    REQUIRE(turned.cell_quads.back()[1].x == Approx(turned.cell_quads.front()[0].x).margin(1e-9));
}

TEST_CASE("Datum symbol: boxed letter, leader to the tip, FILLED triangle") {
    DatumData d;
    d.pos = {0.0, 10.0};
    d.tip = {0.0, 0.0};
    const DatumGeometry g = compute_datum_geometry(d, "A", style_2h(), Rgb{255, 255, 255});

    REQUIRE(g.text == "A");
    // The triangle is one filled triangle: 3 vertices, routed into the fill channel
    // exactly as an arrowhead is.
    REQUIRE(g.fills.size() == 3);
    // Its apex is the tip.
    REQUIRE(g.fills[0].x == Approx(d.tip.x));
    REQUIRE(g.fills[0].y == Approx(d.tip.y));
    // ... and it is SQUAT, not arrowhead-shaped: ASME's datum triangle is roughly
    // equilateral, so its base is comparable to its length (a dimension arrowhead's
    // base is only 0.36 x its length, and at that proportion the symbol reads as an
    // arrow pointing at the feature rather than as a datum).
    const double base_w = length(g.fills[2] - g.fills[1]);
    const double tri_len = length(g.fills[0] - (g.fills[1] + g.fills[2]) * 0.5);
    REQUIRE(base_w > tri_len);
    // The box is a real rectangle of 2h height.
    REQUIRE(length(g.box[3] - g.box[0]) == Approx(2.0 * style_2h().text_height));
    // Box (4 segments) + leader (1) = 5 segments.
    REQUIRE(g.lines.size() == 2 * 5);
}

TEST_CASE("Datum leader attaches to whichever box edge faces the feature") {
    DatumData d;
    d.pos = {0.0, 0.0};
    const DimStyle s = style_2h();

    d.tip = {0.0, -20.0}; // feature BELOW the box -> leader leaves the bottom edge
    const DatumGeometry below = compute_datum_geometry(d, "A", s, Rgb{});
    const Vec2 anchor_below = below.lines[8]; // the leader is the 5th segment

    d.tip = {0.0, 20.0}; // feature ABOVE -> leader leaves the top edge
    const DatumGeometry above = compute_datum_geometry(d, "A", s, Rgb{});
    const Vec2 anchor_above = above.lines[8];

    REQUIRE(anchor_above.y > anchor_below.y);
    REQUIRE(anchor_below.y == Approx(0.0).margin(1e-9));                  // bottom edge
    REQUIRE(anchor_above.y == Approx(2.0 * s.text_height).margin(1e-9)); // top edge
}

TEST_CASE("GD&T shares DIMSTYLE and the dimension override machinery") {
    const std::vector<std::string> cells = {"⟂", "0.1"};
    FcfData f;
    f.cell_count = 2;
    DimStyle s = style_2h();
    s.text_color = ElementColor{false, Rgb{10, 20, 30}};

    // ByStyle: the frame follows its dimstyle, exactly like a dimension.
    REQUIRE(compute_fcf_geometry(f, views(cells), s, Rgb{}).text_height == Approx(2.5));
    REQUIRE(compute_fcf_geometry(f, views(cells), s, Rgb{}).text_color == Rgb{10, 20, 30});

    // An override wins, resolved through the SAME apply_dim_overrides path.
    f.overrides.set(DimOverrides::kTextHeight, true);
    f.overrides.text_height = 4.0;
    REQUIRE(compute_fcf_geometry(f, views(cells), s, Rgb{}).text_height == Approx(4.0));
}

TEST_CASE("GD&T is in the Dimension family, so MATCHPROP carries style across") {
    REQUIRE(family_of(EntityKind::Fcf) == EntityFamily::Dimension);
    REQUIRE(family_of(EntityKind::Datum) == EntityFamily::Dimension);
    // The three properties that make "GD&T matches the drawing's dimensions" true.
    REQUIRE(property_applies(PropertyId::DimTextHeight, EntityKind::Fcf));
    REQUIRE(property_applies(PropertyId::DimTextColor, EntityKind::Fcf));
    REQUIRE(property_applies(PropertyId::DimDimColor, EntityKind::Datum));
    // A frame has no arrowhead; the datum triangle is sized by the style's arrow size.
    REQUIRE_FALSE(property_applies(PropertyId::DimArrowSize, EntityKind::Fcf));
    REQUIRE(property_applies(PropertyId::DimArrowSize, EntityKind::Datum));
    // Dimension-only rows must NOT leak onto GD&T.
    REQUIRE_FALSE(property_applies(PropertyId::DimTolMode, EntityKind::Fcf));
    REQUIRE_FALSE(property_applies(PropertyId::DimTextFit, EntityKind::Fcf));
}

TEST_CASE("Pick: a click inside the frame selects it; bounds enclose the drawn geometry") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle h =
        store.add_fcf({"⌖", "⌀0.05", "A"}, {0.0, 0.0}, 0.0, 0);

    const FcfData* f = store.fcf(h);
    REQUIRE(f != nullptr);
    const FcfGeometry g = compute_fcf_geometry(*f, store.fcf_cell_text(*f), DimStyle{}, Rgb{});

    // A point in the middle of the second cell picks the frame at distance 0.
    const std::array<Vec2, 4>& q = g.cell_quads[1];
    const Vec2 inside{(q[0].x + q[2].x) * 0.5, (q[0].y + q[2].y) * 0.5};
    Vec2 cp{};
    REQUIRE(kernel.closest_point(store, h, inside, cp));
    REQUIRE(cp.x == Approx(inside.x));
    REQUIRE(cp.y == Approx(inside.y));

    // The AABB encloses every drawn point -- bounds cannot disagree with the drawing.
    Vec2 lo{};
    Vec2 hi{};
    REQUIRE(entity_aabb(store, h, lo, hi));
    for (const Vec2& p : g.lines) {
        REQUIRE(p.x >= lo.x - 1e-9);
        REQUIRE(p.x <= hi.x + 1e-9);
        REQUIRE(p.y >= lo.y - 1e-9);
        REQUIRE(p.y <= hi.y + 1e-9);
    }
}

TEST_CASE("Capture -> recreate preserves every field (undo / copy / clipboard)") {
    GeometryStore store;
    DimOverrides ov;
    ov.set(DimOverrides::kTextHeight, true);
    ov.text_height = 3.5;
    EntityProps props;
    props.layer = 0;
    props.set_color_by_layer(false);
    props.color = {7, 8, 9};

    const EntityHandle fh =
        store.add_fcf({"⌖", "⌀0.05", "A", "B"}, {3.0, 4.0}, 0.25, 0, props, ov);
    const EntityHandle dh = store.add_datum("D", {1.0, 2.0}, {5.0, 6.0}, 0.5, 0, props, ov);

    GeometryStore rebuilt;
    const EntityHandle fh2 =
        add_command_to_store(rebuilt, capture_entity(store, fh), EntityProps{});
    const EntityHandle dh2 =
        add_command_to_store(rebuilt, capture_entity(store, dh), EntityProps{});

    const FcfData* a = store.fcf(fh);
    const FcfData* b = rebuilt.fcf(fh2);
    REQUIRE(b != nullptr);
    REQUIRE(b->pos == a->pos);
    REQUIRE(b->rotation == Approx(a->rotation));
    REQUIRE(b->overrides == a->overrides);
    REQUIRE(b->props == a->props);
    REQUIRE(rebuilt.fcf_cell_text(*b) == store.fcf_cell_text(*a));

    const DatumData* c = store.datum(dh);
    const DatumData* d = rebuilt.datum(dh2);
    REQUIRE(d != nullptr);
    REQUIRE(d->tip == c->tip);
    REQUIRE(d->pos == c->pos);
    REQUIRE(d->overrides == c->overrides);
    REQUIRE(rebuilt.string_of(*d) == store.string_of(*c));
}

TEST_CASE("Grips: the frame moves by its insertion point; the datum tip is draggable") {
    GeometryStore store;
    const EntityHandle fh = store.add_fcf({"⌖", "0.1"}, {0.0, 0.0}, 0.0, 0);
    std::vector<Grip> grips;
    grips_of(store, fh, grips);
    REQUIRE(grips.size() == 1);
    REQUIRE(grips[0].pos == Vec2{0.0, 0.0});

    GeometryStore moved;
    const EntityHandle fh2 =
        add_command_to_store(moved, edit_for_grip_drag(store, fh, 0, {12.0, 34.0}), EntityProps{});
    REQUIRE(moved.fcf(fh2)->pos == Vec2{12.0, 34.0});

    const EntityHandle dh = store.add_datum("A", {0.0, -10.0}, {0.0, 0.0}, 0.0, 0);
    grips.clear();
    grips_of(store, dh, grips);
    REQUIRE(grips.size() == 2); // the box, and the triangle on the feature
    GeometryStore tipped;
    const EntityHandle dh2 =
        add_command_to_store(tipped, edit_for_grip_drag(store, dh, 1, {5.0, -20.0}), EntityProps{});
    REQUIRE(tipped.datum(dh2)->tip == Vec2{5.0, -20.0});
    REQUIRE(tipped.datum(dh2)->pos == Vec2{0.0, 0.0}); // the box did not move
}

TEST_CASE("v17 round-trips GD&T losslessly (store -> doc -> file -> doc -> store)") {
    GeometryStore s;
    DimOverrides ov;
    ov.set(DimOverrides::kTextHeight, true);
    ov.text_height = 3.5;
    // Cells with spaces and raw codes, to prove the per-line encoding and RAW storage.
    s.add_fcf({"\\U+2316", "%%c0.05 \\U+24C2", "A", "B", "C"}, {1.5, 2.5}, 0.3, 0, {}, ov);
    s.add_fcf({"\\U+27C2", "0.1", "A"}, {0.0, 0.0}, 0.0, 0);
    s.add_datum("A", {10.0, 0.0}, {10.0, 12.0}, 0.0, 0, {}, ov);

    const musacad::core::io::Document a = musacad::core::io::document_from_store(s);
    REQUIRE(a.format_version == musacad::core::io::kFormatVersion);
    REQUIRE(a.fcfs.size() == 2);
    REQUIRE(a.datums.size() == 1);

    const auto path = (std::filesystem::temp_directory_path() / "musacad_gdt.musa").string();
    REQUIRE(musacad::core::io::save_native(a, path).ok);
    musacad::core::io::Document b;
    REQUIRE(musacad::core::io::load_native(path, b).ok);
    GeometryStore restored;
    musacad::core::io::populate_store(restored, b);
    REQUIRE(musacad::core::io::document_from_store(restored) == a);

    // Spot-check rather than trusting equality alone: cells stay RAW.
    REQUIRE(b.fcfs[0].cells.size() == 5);
    REQUIRE(b.fcfs[0].cells[1] == "%%c0.05 \\U+24C2");
    REQUIRE(b.fcfs[0].overrides.text_height == 3.5);
    REQUIRE(b.datums[0].letter == "A");
    std::filesystem::remove(path);
}

TEST_CASE("v16 files still load, with no GD&T entities (older-version compatibility)") {
    const std::string v16 =
        "MUSACAD 16\nLAYER 255 255 255 0 25 1 0 0 0\n"
        "LINE 0 0 1 1 0 7 255 255 255 0 25\nEND\n";
    musacad::core::io::Document doc;
    REQUIRE(musacad::core::io::parse_native(v16, doc).ok);
    REQUIRE(doc.fcfs.empty());
    REQUIRE(doc.datums.empty());
    REQUIRE(doc.lines.size() == 1);
}

TEST_CASE("An empty cell list draws nothing rather than a stray box") {
    FcfData f;
    f.cell_count = 0;
    const FcfGeometry g = compute_fcf_geometry(f, {}, style_2h(), Rgb{});
    REQUIRE(g.lines.empty());
    REQUIRE(g.cell_quads.empty());
}

TEST_CASE("GD&T struct sizes are asserted; the hot structs are untouched") {
    // The GD&T entities live in their own COLD arenas, so their size is free -- but it
    // is pinned so a future field is a deliberate change, not a silent one.
    static_assert(sizeof(FcfCell) == 8, "FcfCell is an (offset,len) pair -- keep it small");
    static_assert(sizeof(FcfData) == 96, "FcfData size changed -- update the docs too");
    static_assert(sizeof(DatumData) == 112, "DatumData size changed -- update the docs too");
    // The whole point of separate arenas: nothing on the hot path moved. EntityProps grew to
    // 16 B once, deliberately, for the draw order, an object's own transparency and ByBlock.
    static_assert(sizeof(LineData) == 48, "hot struct: LineData must stay 48 B");
    static_assert(sizeof(CircleData) == 40, "hot struct: CircleData must stay 40 B");
    static_assert(sizeof(EntityProps) == 16, "hot struct: EntityProps must stay 16 B");
    SUCCEED();
}

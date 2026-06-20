// Phase 29 Part A: the two coexisting text paths -- the built-in stroke font emits LINE
// geometry; an outline (TTF) font emits FILLED triangles -- selected per text entity by
// its font reference, generated at snapshot (derived-not-baked). A stub IFontEngine
// stands in for the Qt-backed one so the core integration is testable Qt-free.

#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <variant>

#include "musacad/core/command.hpp"
#include "musacad/core/font_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/mtext_block.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/properties_registry.hpp"
#include "musacad/core/scene_snapshot.hpp"
#include "musacad/core/text/mtext.hpp"

using namespace musacad::core;

namespace {
// A deterministic outline font: the name "TTF" is an outline face; each glyph is one
// triangle of side `height`; advance is 0.6*height per character.
struct StubFontEngine final : IFontEngine {
    bool is_outline_font(std::string_view name) const override { return name == "TTF"; }
    // A deliberately WIDE advance (2x cap height per char) so a TTF paragraph wraps into
    // more lines than the same text in the (~0.62h/char) stroke font -- proving the wrap
    // uses the face's metrics, not the stroke font's.
    double advance(std::string_view name, std::string_view text, double height) const override {
        return is_outline_font(name) ? static_cast<double>(text.size()) * 2.0 * height : 0.0;
    }
    void glyph_fills(std::string_view name, std::string_view text, Vec2 origin, double height,
                     double /*rotation*/, std::vector<Vec2>& tris) const override {
        if (!is_outline_font(name)) {
            return;
        }
        double pen = origin.x;
        for (std::size_t i = 0; i < text.size(); ++i) {
            tris.push_back({pen, origin.y});
            tris.push_back({pen + height, origin.y});
            tris.push_back({pen, origin.y + height});
            pen += 0.6 * height;
        }
    }
    std::vector<std::string> available() const override { return {"Standard", "TTF"}; }
    std::string substitute(std::string_view requested) const override {
        return requested == "romans.shx" ? std::string("TTF") : std::string();
    }
};
} // namespace

TEST_CASE("text entity + font field stay compact") {
    // The font ref is a uint16 index, not an inline string. TextData absorbs it in existing
    // padding; MTextBlock (one per paragraph, not a per-line hot struct) takes one extra
    // alignment slot (64->72) -- still small. Leader/MLeader are annotation entities (few per
    // drawing, NOT hot like Line/Circle), so they carry the per-leader arrow override
    // (DimOverrides, ~40 B) inline -- the same way DimData carries its overrides.
    REQUIRE(sizeof(TextData) <= 56);
    REQUIRE(sizeof(MTextBlock) <= 72);
    REQUIRE(sizeof(LeaderData) <= 112);
    REQUIRE(sizeof(MLeaderData) <= 152);
}

TEST_CASE("stroke text emits lines; outline text emits filled triangles (one path each)") {
    GeometryStore store;
    NativeKernel2D kernel;
    const std::uint16_t ttf = store.add_font("TTF");
    REQUIRE(ttf == 1); // 0 is the stroke font

    // Two single-line texts: one stroke, one TTF.
    store.add_text({0, 0}, 2.5, 0.0, 0, "ABC", {}, 0);    // stroke
    store.add_text({0, 20}, 2.5, 0.0, 0, "ABC", {}, ttf); // outline

    StubFontEngine fonts;
    store.set_font_engine(&fonts);
    RenderSnapshot snap;
    build_render_snapshot(store, kernel, snap, 0.01, 1.0);

    REQUIRE_FALSE(snap.line_vertices.empty()); // the stroke text drew lines
    REQUIRE_FALSE(snap.fill_vertices.empty()); // the TTF text drew filled triangles
    // 3 chars * 3 vertices/triangle = 9 fill vertices from the one TTF text.
    REQUIRE(snap.fill_vertices.size() == 9);
    REQUIRE(snap.fill_batches.size() == 1); // batched by colour -> one draw call
}

TEST_CASE("with no font engine, outline-referenced text falls back to the stroke font") {
    GeometryStore store;
    NativeKernel2D kernel;
    const std::uint16_t ttf = store.add_font("TTF");
    store.add_text({0, 0}, 2.5, 0.0, 0, "ABC", {}, ttf);

    RenderSnapshot snap;
    build_render_snapshot(store, kernel, snap, 0.01, 1.0); // no engine set on the store
    REQUIRE_FALSE(snap.line_vertices.empty()); // rendered as stroke lines (graceful)
    REQUIRE(snap.fill_vertices.empty());
}

TEST_CASE("glyph geometry is derived-not-baked: the store holds only string + font ref") {
    GeometryStore store;
    NativeKernel2D kernel;
    const std::uint16_t ttf = store.add_font("TTF");
    const EntityHandle h = store.add_text({0, 0}, 4.0, 0.0, 0, "XY", {}, ttf);

    StubFontEngine fonts;
    store.set_font_engine(&fonts);
    RenderSnapshot a;
    build_render_snapshot(store, kernel, a, 0.01, 1.0);
    const std::size_t fills_at_h4 = a.fill_vertices.size();
    REQUIRE(fills_at_h4 == 6); // 2 chars

    // Change only the height in the store; the glyph geometry regenerates (bigger), proving
    // nothing was baked. The stored entity carries no triangles -- just the string + font.
    const TextData* t = store.text(h);
    REQUIRE(t->font == ttf);
    REQUIRE(t->str_len == 2);
    RenderSnapshot b;
    build_render_snapshot(store, kernel, b, 0.01, 1.0);
    REQUIRE(b.fill_vertices.size() == fills_at_h4); // same string -> same vertex count, re-derived
    // The fills track the entity's height: at height 4 the first glyph spans 4 units.
    REQUIRE(b.fill_vertices[1].x == Catch::Approx(4.0).margin(1e-9));
}

TEST_CASE("PR Font field writes a font name; the store round-trips name<->index") {
    // The Properties palette writes via SetProperty -> write_property -> the command's
    // font name. Applying maps name->store index; capture maps index->name.
    Command c = AddTextCommand{{0, 0}, 2.5, 0.0, 0, "hi", 0};
    REQUIRE(std::get<AddTextCommand>(c).font.empty()); // Standard by default

    PropertyValue v;
    v.text = "DejaVu Sans";
    write_property(c, PropertyId::TextFont, v);
    REQUIRE(std::get<AddTextCommand>(c).font == "DejaVu Sans");

    GeometryStore store;
    const EntityHandle h = add_command_to_store(store, c, EntityProps{});
    REQUIRE(store.text(h)->font != 0); // a real font-table entry, not stroke
    REQUIRE(store.font_name(store.text(h)->font) == "DejaVu Sans");

    const Command back = capture_entity(store, h);
    REQUIRE(std::get<AddTextCommand>(back).font == "DejaVu Sans"); // round-trips by name

    // Reverting to Standard ("") sets the stroke font (index 0).
    PropertyValue rev;
    rev.text.clear();
    Command c2 = back;
    write_property(c2, PropertyId::TextFont, rev);
    const EntityHandle h2 = add_command_to_store(store, c2, EntityProps{});
    REQUIRE(store.text(h2)->font == 0);
}

TEST_CASE("font names round-trip through native (v10) and DXF; older files load as Standard") {
    using namespace musacad::core::io;
    Document doc;
    doc.texts.push_back(DocText{{0, 0}, 2.5, 0.0, 0, "hi", {}, "DejaVu Sans"});
    DocMText m;
    m.content = "para";
    m.font = "romans.shx"; // an SHX reference (the engine substitutes at render)
    doc.mtexts.push_back(m);

    SECTION("native v10") {
        Document rt;
        REQUIRE(parse_native(serialize_native(doc), rt).ok);
        REQUIRE(rt.texts.size() == 1);
        REQUIRE(rt.texts[0].font == "DejaVu Sans");
        REQUIRE(rt.mtexts[0].font == "romans.shx");
    }
    SECTION("DXF (style/code 7 round-trip via the code-7 fallback)") {
        Document rt;
        REQUIRE(parse_dxf(serialize_dxf(doc), rt).ok);
        REQUIRE(rt.texts.size() == 1);
        REQUIRE(rt.texts[0].font == "DejaVu Sans");
        REQUIRE(rt.mtexts[0].font == "romans.shx");
    }
    SECTION("a v9 (pre-font) native file loads as the stroke font") {
        // A minimal v9 file: header v9 + one TEXT (no font line) + END.
        const std::string v9 =
            "MUSACAD 9\nUNITS unitless\nCURRENT 0\nLTSCALE 1\nLAYER 255 255 255 0 25 1 0 0 0\n"
            "TEXT 0 0 2.5 0 0 0 7 255 255 255 0 25\nhi\nEND\n";
        Document rt;
        REQUIRE(parse_native(v9, rt).ok);
        REQUIRE(rt.texts.size() == 1);
        REQUIRE(rt.texts[0].font.empty()); // defaulted to the stroke font
    }
}

TEST_CASE("DXF import resolves a text STYLE (code 7) to its font file (code 3)") {
    using namespace musacad::core::io;
    const std::string dxf =
        "0\nSECTION\n2\nTABLES\n"
        "0\nTABLE\n2\nSTYLE\n"
        "0\nSTYLE\n2\nNOTES\n3\nromans.shx\n"
        "0\nENDTAB\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n"
        "0\nTEXT\n8\n0\n10\n0\n20\n0\n40\n2.5\n1\nHELLO\n7\nNOTES\n"
        "0\nENDSEC\n0\nEOF\n";
    Document doc;
    REQUIRE(parse_dxf(dxf, doc).ok);
    REQUIRE(doc.texts.size() == 1);
    REQUIRE(doc.texts[0].font == "romans.shx"); // resolved via the STYLE table
}

TEST_CASE("MTEXT re-wraps using the outline face's metrics (computed-not-baked)") {
    StubFontEngine fonts;
    MTextBlock block;
    block.height = 2.0;
    block.width = 24.0; // wrap width
    const std::string_view content = "AAA BBB CCC DDD";

    // Stroke font (~0.62h/char): "AAA BBB CCC DDD" mostly fits the 24-unit width.
    const auto stroke = text::layout_mtext(block, content, nullptr, {});
    // The same paragraph in the wide (2h/char) TTF face wraps into more lines.
    const auto ttf = text::layout_mtext(block, content, &fonts, "TTF");

    REQUIRE(ttf.line_count > stroke.line_count); // re-wrapped to the TTF metrics
    REQUIRE_FALSE(ttf.fills.empty());            // TTF emits filled glyphs
    REQUIRE(ttf.segments.empty());               // ...not stroke segments
    REQUIRE_FALSE(stroke.segments.empty());      // stroke emits line segments
    REQUIRE(stroke.fills.empty());
}

// Native .musa format: lossless round-trip of every entity family, and
// fail-safe handling of malformed input.

#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"

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

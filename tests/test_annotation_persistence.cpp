// Part D: native v3 round-trips text + dimensions + dimstyles losslessly; older
// files still load; DXF carries text + linear dimensions + the DIMSTYLE table.

#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad::core;
using namespace musacad::core::io;

namespace {
GeometryStore make_annotated_store() {
    GeometryStore s;
    DimStyle ds;
    ds.name = "Arch";
    ds.text_height = 3.5;
    ds.precision = 1;
    ds.arrow_type = 1;
    const std::uint16_t arch = s.add_dimstyle(ds);

    s.add_text({1, 2}, 5.0, 0.5, 1, "Hello World 123", {});
    s.add_line({0, 0}, {10, 0}, {});
    s.add_dimension(DimType::Linear, {0, 0}, {10, 0}, {5, 4}, 0, {});
    s.add_dimension(DimType::Aligned, {0, 0}, {3, 4}, {-1, 1}, arch, {});
    return s;
}
} // namespace

TEST_CASE("Native v3 round-trips text + dimensions + dimstyles losslessly") {
    const Document a = document_from_store(make_annotated_store());
    REQUIRE(a.texts.size() == 1);
    REQUIRE(a.dims.size() == 2);
    REQUIRE(a.dimstyles.size() == 2); // Standard + Arch

    const auto path = (std::filesystem::temp_directory_path() / "musacad_annot.musa").string();
    REQUIRE(save_native(a, path).ok);
    Document b;
    REQUIRE(load_native(path, b).ok);
    GeometryStore restored;
    populate_store(restored, b);
    REQUIRE(document_from_store(restored) == a); // exact: text content, dims, styles
    std::filesystem::remove(path);
}

TEST_CASE("Text content with spaces survives the native round-trip") {
    Document a;
    a.texts.push_back(DocText{{0, 0}, 2.5, 0.0, 0, "two  spaces and tabs\there", {}});
    Document b;
    REQUIRE(parse_native(serialize_native(a), b).ok);
    REQUIRE(b.texts.size() == 1);
    REQUIRE(b.texts[0].content == "two  spaces and tabs\there");
}

TEST_CASE("v2 files (no annotations) still load") {
    const std::string v2 = "MUSACAD 2\nLAYER 255 0 0 0 25 1 0 0 red\nLINE 0 0 1 1 1 7 0 0 0 0 25\nEND\n";
    Document doc;
    REQUIRE(parse_native(v2, doc).ok);
    REQUIRE(doc.lines.size() == 1);
    REQUIRE(doc.texts.empty());
    REQUIRE(doc.dims.empty());
    REQUIRE(doc.dimstyles.size() == 1); // defaulted Standard
}

TEST_CASE("DXF carries text + linear dimensions + a DIMSTYLE table") {
    const Document a = document_from_store(make_annotated_store());
    const std::string dxf = serialize_dxf(a);
    REQUIRE(dxf.find("\nTEXT\n") != std::string::npos);
    REQUIRE(dxf.find("\nDIMENSION\n") != std::string::npos);
    REQUIRE(dxf.find("\nDIMSTYLE\n") != std::string::npos);

    Document b;
    REQUIRE(parse_dxf(dxf, b).ok);
    REQUIRE(b.texts.size() == 1);
    REQUIRE(b.texts[0].content == "Hello World 123");
    REQUIRE(b.dims.size() == 2);
    // Linear + Aligned types survive.
    bool has_linear = false;
    bool has_aligned = false;
    for (const DocDim& d : b.dims) {
        has_linear = has_linear || d.type == static_cast<std::uint8_t>(DimType::Linear);
        has_aligned = has_aligned || d.type == static_cast<std::uint8_t>(DimType::Aligned);
    }
    REQUIRE(has_linear);
    REQUIRE(has_aligned);
    // The Arch dimstyle came through the table.
    bool has_arch = false;
    for (const DimStyle& ds : b.dimstyles) {
        has_arch = has_arch || ds.name == "Arch";
    }
    REQUIRE(has_arch);
}

// Part D: native v2 round-trips layers + per-entity overrides losslessly; v1
// files load onto layer 0; DXF writes/reads a real LAYER table.

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
Layer make_layer(const char* name, Rgb color, bool on = true, bool frozen = false,
                 bool locked = false) {
    Layer l;
    l.name = name;
    l.color = color;
    l.on = on;
    l.frozen = frozen;
    l.locked = locked;
    return l;
}
// A store with several layers (varied flags/colours) and entities, including a
// per-entity colour override.
GeometryStore make_layered_store() {
    GeometryStore s;
    const std::uint16_t red = s.add_layer(make_layer("red", {255, 0, 0}));
    const std::uint16_t blue = s.add_layer(make_layer("blue", {0, 0, 255}, true, true)); // frozen
    s.add_layer(make_layer("locked", {0, 255, 0}, true, false, true));
    s.set_current_layer(red);

    EntityProps on_red;
    on_red.layer = red;
    s.add_line({0, 0}, {10, 0}, on_red);

    EntityProps on_blue;
    on_blue.layer = blue;
    on_blue.linetype = Linetype::Dashed;
    on_blue.set_linetype_by_layer(false);
    s.add_circle({5, 5}, 3.0, on_blue);

    EntityProps override_color;
    override_color.layer = red;
    override_color.set_color_by_layer(false);
    override_color.color = {200, 100, 50};
    override_color.lineweight = 50;
    override_color.set_lineweight_by_layer(false);
    s.add_arc({1, 1}, 4.0, 0.0, 1.5, override_color);

    s.add_polyline(std::vector<Vec2>{{0, 0}, {1, 0}, {1, 1}}, true, on_red);
    return s;
}
} // namespace

TEST_CASE("Native v2 round-trips layers + per-entity overrides losslessly") {
    const GeometryStore original = make_layered_store();
    const Document a = document_from_store(original);
    REQUIRE(a.layers.size() == 4); // 0, red, blue, locked
    REQUIRE(a.current_layer == 1);

    const auto path = (std::filesystem::temp_directory_path() / "musacad_layers.musa").string();
    REQUIRE(save_native(a, path).ok);

    Document b;
    REQUIRE(load_native(path, b).ok);
    GeometryStore restored;
    populate_store(restored, b);

    REQUIRE(document_from_store(restored) == a); // exact: geometry + layers + props
    std::filesystem::remove(path);
}

TEST_CASE("Native v1 files load onto layer 0, fully ByLayer") {
    // A v1 document: no layer table, geometry-only entity records.
    const std::string v1 = "MUSACAD 1\nUNITS unitless\nLINE 0 0 10 5\nEND\n";
    Document doc;
    REQUIRE(parse_native(v1, doc).ok);
    REQUIRE(doc.lines.size() == 1);
    REQUIRE(doc.layers.size() == 1);
    REQUIRE(doc.layers[0].name == "0");
    REQUIRE(doc.lines[0].props.layer == 0);
    REQUIRE(doc.lines[0].props.color_by_layer());
}

TEST_CASE("DXF writes a real LAYER table; import reads it and assigns entities") {
    const Document a = document_from_store(make_layered_store());
    const std::string dxf = serialize_dxf(a);

    // The TABLES/LAYER section is present with named layers.
    REQUIRE(dxf.find("TABLES") != std::string::npos);
    REQUIRE(dxf.find("\nLAYER\n") != std::string::npos);
    REQUIRE(dxf.find("\nred\n") != std::string::npos);
    REQUIRE(dxf.find("\nblue\n") != std::string::npos);

    Document b;
    REQUIRE(parse_dxf(dxf, b).ok);
    // Layer table survived (0 + red + blue + locked).
    REQUIRE(b.layers.size() == a.layers.size());
    // Frozen/locked flags and colours survived (via true-colour 420).
    bool blue_frozen = false;
    bool red_color = false;
    for (const Layer& l : b.layers) {
        if (l.name == "blue") {
            blue_frozen = l.frozen;
        }
        if (l.name == "red") {
            red_color = (l.color == Rgb{255, 0, 0});
        }
    }
    REQUIRE(blue_frozen);
    REQUIRE(red_color);

    // Entity layer assignment + effective colour survive the DXF round-trip.
    REQUIRE(b.lines.size() == 1);
    const Layer& line_layer = b.layers[b.lines[0].props.layer];
    REQUIRE(line_layer.name == "red");
    REQUIRE(resolve(b.lines[0].props, line_layer).color == Rgb{255, 0, 0});

    // The colour override on the arc survived.
    REQUIRE(b.arcs.size() == 1);
    REQUIRE_FALSE(b.arcs[0].props.color_by_layer());
    REQUIRE(b.arcs[0].props.color == Rgb{200, 100, 50});
}

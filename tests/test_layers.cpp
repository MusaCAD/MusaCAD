// Part A: the ByLayer / override property model and layer-table CRUD.

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/properties.hpp"

using namespace musacad::core;

TEST_CASE("resolve(): ByLayer inherits; overrides win") {
    Layer red;
    red.color = {255, 0, 0};
    red.linetype = Linetype::Dashed;
    red.lineweight = 50; // 0.50 mm (hundredths)

    SECTION("fully ByLayer entity inherits everything from its layer") {
        const EntityProps e; // all ByLayer
        const ResolvedProps r = resolve(e, red);
        REQUIRE(r.color == Rgb{255, 0, 0});
        REQUIRE(r.linetype == Linetype::Dashed);
        REQUIRE(r.lineweight == 50);
    }
    SECTION("a colour override wins; other properties still ByLayer") {
        EntityProps e;
        e.set_color_by_layer(false);
        e.color = {0, 0, 255};
        const ResolvedProps r = resolve(e, red);
        REQUIRE(r.color == Rgb{0, 0, 255});      // override
        REQUIRE(r.linetype == Linetype::Dashed); // still ByLayer
    }
}

TEST_CASE("Changing a layer's colour updates ByLayer entities, not overridden ones") {
    GeometryStore s;
    const std::uint16_t red = s.add_layer(Layer{"red", {255, 0, 0}});

    // Two entities on the red layer: one ByLayer, one with a blue override.
    EntityProps bylayer;
    bylayer.layer = red;
    EntityProps overridden;
    overridden.layer = red;
    overridden.set_color_by_layer(false);
    overridden.color = {0, 0, 255};
    const EntityHandle a = s.add_line({0, 0}, {1, 0}, bylayer);
    const EntityHandle b = s.add_line({0, 1}, {1, 1}, overridden);

    REQUIRE(resolve(*s.props(a), *s.layer(red)).color == Rgb{255, 0, 0});
    REQUIRE(resolve(*s.props(b), *s.layer(red)).color == Rgb{0, 0, 255});

    // Recolour the layer to green.
    Layer green = *s.layer(red);
    green.color = {0, 255, 0};
    REQUIRE(s.set_layer(red, green));

    REQUIRE(resolve(*s.props(a), *s.layer(red)).color == Rgb{0, 255, 0}); // followed
    REQUIRE(resolve(*s.props(b), *s.layer(red)).color == Rgb{0, 0, 255}); // unchanged
}

TEST_CASE("Layer CRUD: add/dedup/current; layer 0 always present") {
    GeometryStore s;
    REQUIRE(s.layer_count() == 1);
    REQUIRE(s.layer(0)->name == "0");
    REQUIRE(s.current_layer() == 0);

    const std::uint16_t walls = s.add_layer(Layer{"walls"});
    REQUIRE(walls == 1);
    REQUIRE(s.add_layer(Layer{"walls"}) == walls); // names unique -> dedup
    REQUIRE(s.layer_count() == 2);

    s.set_current_layer(walls);
    REQUIRE(s.current_layer() == walls);

    // Layer 0 cannot be renamed.
    Layer renamed0 = *s.layer(0);
    renamed0.name = "base";
    s.set_layer(0, renamed0);
    REQUIRE(s.layer(0)->name == "0");
}

TEST_CASE("Layer deletion: blocked for 0 / current / non-empty; refs shift down") {
    GeometryStore s;
    const std::uint16_t a = s.add_layer(Layer{"a"}); // 1
    const std::uint16_t b = s.add_layer(Layer{"b"}); // 2
    const std::uint16_t c = s.add_layer(Layer{"c"}); // 3
    REQUIRE(c == 3);

    REQUIRE_FALSE(s.remove_layer(0)); // can't delete layer 0

    s.set_current_layer(b);
    REQUIRE_FALSE(s.remove_layer(b)); // can't delete current
    s.set_current_layer(0);

    // Put an entity on layer 'a' -> can't delete a non-empty layer.
    EntityProps on_a;
    on_a.layer = a;
    const EntityHandle e = s.add_line({0, 0}, {1, 1}, on_a);
    REQUIRE(s.layer_in_use(a));
    REQUIRE_FALSE(s.remove_layer(a));

    // Deleting 'b' (empty, not current) succeeds; 'c' shifts from 3 -> 2, and the
    // entity on 'a' (1) is unaffected.
    REQUIRE(s.remove_layer(b));
    REQUIRE(s.layer_count() == 3); // 0, a, c
    REQUIRE(s.layer(2)->name == "c");
    REQUIRE(s.props(e)->layer == a); // unchanged (below the removed index)
}

TEST_CASE("Removing a layer shifts higher entity layer refs down to stay valid") {
    GeometryStore s;
    const std::uint16_t low = s.add_layer(Layer{"low"});   // 1
    const std::uint16_t high = s.add_layer(Layer{"high"}); // 2
    EntityProps on_high;
    on_high.layer = high;
    const EntityHandle e = s.add_line({0, 0}, {1, 0}, on_high);

    REQUIRE(s.remove_layer(low)); // 'low' is empty
    // 'high' is now index 1; the entity must point at the shifted index.
    REQUIRE(s.layer(1)->name == "high");
    REQUIRE(s.props(e)->layer == 1);
    REQUIRE(resolve(*s.props(e), *s.layer(s.props(e)->layer)).color == s.layer(1)->color);
}

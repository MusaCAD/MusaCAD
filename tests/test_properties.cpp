// Phase 22: Properties palette (PR) -- the descriptor registry (aggregate/varies),
// the generic SetPropertyCommand write path (universal + text), one-undo-group,
// and the multiplicity model (none/one/many-same/mixed). UI is exercised by the
// real-window selftest; here we prove the engine/registry contract.

#include <chrono>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/properties_registry.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {
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
PropertyValue numv(double n) { PropertyValue v; v.num = n; return v; }
} // namespace

TEST_CASE("Registry: summarize aggregates universal + type fields; mixed -> universal only") {
    std::vector<Command> one;
    one.push_back(AddLineCommand{{0, 0}, {10, 0}, 1, {}});
    const SelectionSummary s1 = summarize_selection(one);
    REQUIRE(s1.count == 1);
    REQUIRE_FALSE(s1.mixed);
    REQUIRE(s1.type_label == "Line");
    bool has_layer = false, has_len = false;
    for (const PropertyField& f : s1.fields) {
        has_layer = has_layer || f.id == PropertyId::Layer;
        has_len = has_len || f.id == PropertyId::GeomLength;
    }
    REQUIRE(has_layer);
    REQUIRE(has_len);

    std::vector<Command> mixed = one;
    mixed.push_back(AddCircleCommand{{30, 0}, 4.0, 2, {}});
    const SelectionSummary sm = summarize_selection(mixed);
    REQUIRE(sm.count == 2);
    REQUIRE(sm.mixed);
    for (const PropertyField& f : sm.fields) {
        REQUIRE(f.group == "General"); // type-specific (Geometry) excluded for mixed
    }
}

TEST_CASE("Registry: VARIES flag set when selected entities disagree") {
    EntityProps red;
    red.set_color_by_layer(false);
    red.color = {255, 0, 0};
    std::vector<Command> two;
    two.push_back(AddLineCommand{{0, 0}, {10, 0}, 1, {}});       // ByLayer
    two.push_back(AddLineCommand{{0, 5}, {10, 5}, 2, red});       // override red
    const SelectionSummary s = summarize_selection(two);
    bool color_varies = false, layer_varies = true;
    for (const PropertyField& f : s.fields) {
        if (f.id == PropertyId::Color) color_varies = f.varies;
        if (f.id == PropertyId::Layer) layer_varies = f.varies;
    }
    REQUIRE(color_varies);       // colors differ
    REQUIRE_FALSE(layer_varies); // both on layer 0
}

TEST_CASE("SetPropertyCommand edits the whole selection as one undo group") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{0, 5}, {10, 5}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() >= 4; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));

    // Override colour on BOTH lines via the generic write path.
    PropertyValue green;
    green.flag = false;
    green.color = {0, 200, 0};
    engine.submit(SetPropertyCommand{PropertyId::Color, green, 7});
    REQUIRE(wait_until(engine, [](const auto& s) {
        if (s.selection_summary.count != 2) return false;
        for (const auto& f : s.selection_summary.fields) {
            if (f.id == PropertyId::Color) {
                return !f.varies && !f.value.flag && f.value.color == Rgb{0, 200, 0};
            }
        }
        return false;
    }));

    // One undo group reverts BOTH back to ByLayer.
    engine.submit(UndoLastGroupCommand{});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        if (s.selection_summary.count != 2) return false;
        for (const auto& f : s.selection_summary.fields) {
            if (f.id == PropertyId::Color) {
                return f.value.flag && !f.varies; // ByLayer again
            }
        }
        return false;
    }));
    engine.stop();
}

TEST_CASE("SetPropertyCommand edits text height + re-lays-out (computed-not-baked)") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddTextCommand{{0, 0}, 2.5, 0.0, 0, "HELLO", 1, {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    // Re-layout at a larger height moves vertices further out (computed-not-baked),
    // even though the glyph *count* is unchanged.
    const auto max_y = [](const RenderSnapshot& s) {
        double m = 0.0;
        for (const Vec2& v : s.line_vertices) {
            m = std::max(m, v.y);
        }
        return m;
    };
    const double y_before = max_y(engine.snapshot());
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(SetPropertyCommand{PropertyId::TextHeight, numv(6.0), 5});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        if (max_y(s) <= y_before + 1.0) return false; // taller glyphs -> bigger extent
        for (const auto& f : s.selection_summary.fields) {
            if (f.id == PropertyId::TextHeight) return f.value.num == Approx(6.0);
        }
        return false;
    }));
    engine.stop();
}

TEST_CASE("write_property only touches applicable kinds (Geometry/text are gated)") {
    Command line = AddLineCommand{{0, 0}, {10, 0}, 1, {}};
    // Text height does not apply to a line -> no-op, no crash.
    write_property(line, PropertyId::TextHeight, numv(9.0));
    REQUIRE(property_applies(PropertyId::Layer, EntityKind::Line));
    REQUIRE_FALSE(property_applies(PropertyId::TextHeight, EntityKind::Line));
    REQUIRE(property_applies(PropertyId::TextHeight, EntityKind::Text));
    REQUIRE(property_applies(PropertyId::MtWidthFactor, EntityKind::MText));
    REQUIRE_FALSE(property_applies(PropertyId::MtWidthFactor, EntityKind::Text));
}

namespace {
const PropertyField* field_of(const SelectionSummary& s, PropertyId id) {
    for (const auto& f : s.fields) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE("MATCHPROP copies universal properties across entity kinds (line -> circle)") {
    GeometryEngine engine;
    engine.start();
    // A distinct layer so the layer copy is observable.
    Layer walls;
    walls.name = "Walls";
    walls.color = {255, 0, 0};
    engine.submit(AddLayerCommand{walls});
    std::uint16_t walls_idx = 0;
    REQUIRE(wait_until(engine, [&](const auto& s) {
        for (std::uint16_t i = 0; i < s.layers.size(); ++i) {
            if (s.layers[i].name == "Walls") {
                walls_idx = i;
                return true;
            }
        }
        return false;
    }));
    // Source line: explicit blue override + lineweight 50, on Walls.
    EntityProps sp;
    sp.layer = walls_idx;
    sp.set_color_by_layer(false);
    sp.color = {0, 0, 255};
    sp.set_lineweight_by_layer(false);
    sp.lineweight = 50;
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1, sp});
    engine.submit(AddCircleCommand{{50, 0}, 10, 2}); // target: default (ByLayer, layer 0)
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(ClearSelectionCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.empty(); }));

    // MATCHPROP: source = line midpoint, target = circle's rightmost point.
    engine.submit(MatchPropPickSourceCommand{{5, 0}, 1.0});
    engine.submit(MatchPropApplyCommand{{60, 0}, 1.0, MatchPropFilter{}, 9});
    engine.submit(SelectPickCommand{{60, 0}, 1.0, false}); // re-select the circle to read it
    REQUIRE(wait_until(engine, [&](const auto& s) {
        if (s.selection.size() != 1 || s.selection[0].kind != EntityKind::Circle) {
            return false; // still a circle, not turned into a line
        }
        const PropertyField* col = field_of(s.selection_summary, PropertyId::Color);
        const PropertyField* lw = field_of(s.selection_summary, PropertyId::Lineweight);
        const PropertyField* ly = field_of(s.selection_summary, PropertyId::Layer);
        return col != nullptr && !col->value.flag && col->value.color == Rgb{0, 0, 255} &&
               lw != nullptr && lw->value.num == Approx(50.0) && ly != nullptr &&
               ly->value.choice == static_cast<int>(walls_idx);
    }));
    engine.stop();
}

TEST_CASE("MATCHPROP copies ByLayer state, not the resolved literal") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1}); // source: default -> colour ByLayer
    EntityProps tp;                                    // target: explicit red override
    tp.set_color_by_layer(false);
    tp.color = {255, 0, 0};
    engine.submit(AddLineCommand{{0, 5}, {10, 5}, 2, tp});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(ClearSelectionCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.empty(); }));

    engine.submit(MatchPropPickSourceCommand{{5, 0}, 1.0});      // ByLayer source
    engine.submit(MatchPropApplyCommand{{5, 5}, 1.0, MatchPropFilter{}, 9}); // red target
    engine.submit(SelectPickCommand{{5, 5}, 1.0, false});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        if (s.selection.size() != 1) {
            return false;
        }
        const PropertyField* col = field_of(s.selection_summary, PropertyId::Color);
        return col != nullptr && col->value.flag; // ByLayer state copied, not "blue/red" literal
    }));
    engine.stop();
}

TEST_CASE("MATCHPROP within the text family copies height; cross-family is skipped cleanly") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddTextCommand{{0, 0}, 5.0, 0.0, 0, "SRC", 1, {}}); // source TEXT, height 5
    MTextBlock blk;
    blk.pos = {0, 20};
    blk.height = 2.5;
    engine.submit(AddMTextCommand{blk, "DST", 2, {}}); // target MTEXT, height 2.5
    engine.submit(AddLineCommand{{0, 40}, {10, 40}, 3}); // a LINE (different family)
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 3; }));
    engine.submit(ClearSelectionCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.empty(); }));

    engine.submit(MatchPropPickSourceCommand{{1, 0}, 2.0});                 // TEXT source
    engine.submit(MatchPropApplyCommand{{0, 20}, 2.0, MatchPropFilter{}, 9}); // -> MTEXT
    engine.submit(MatchPropApplyCommand{{5, 40}, 2.0, MatchPropFilter{}, 10}); // -> LINE (no crash)
    engine.submit(SelectPickCommand{{0, 20}, 2.0, false});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        if (s.selection.size() != 1 || s.selection[0].kind != EntityKind::MText) {
            return false;
        }
        const PropertyField* h = field_of(s.selection_summary, PropertyId::TextHeight);
        return h != nullptr && h->value.num == Approx(5.0); // text-family height travelled
    }));
    engine.stop();
}

TEST_CASE("MATCHPROP text-family closure reaches Leader + MLeader labels (font + height)") {
    GeometryEngine engine;
    engine.start();
    // Source TEXT: height 5, font "Arial".
    engine.submit(AddTextCommand{{0, 0}, 5.0, 0.0, 0, "SRC", 1, {}, "Arial"});
    // Target MLeader: paragraph label height 2.5, default (stroke) font.
    MTextBlock blk;
    blk.pos = {10, 30};
    blk.height = 2.5;
    engine.submit(AddMLeaderCommand{{{0, 30}, {10, 30}}, 0, blk, "ML", 2});
    // Target flat LEADER: text_height 2.5, default font.
    engine.submit(AddLeaderCommand{{0, 60}, {10, 60}, 2.5, 0, "LD", 3});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 3; }));
    engine.submit(ClearSelectionCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.empty(); }));

    engine.submit(MatchPropPickSourceCommand{{0, 0}, 2.0});                     // TEXT source
    engine.submit(MatchPropApplyCommand{{5, 30}, 2.0, MatchPropFilter{}, 9});   // -> MLeader (leader line)
    engine.submit(MatchPropApplyCommand{{5, 60}, 2.0, MatchPropFilter{}, 10});  // -> Leader (leader line)

    // The MLeader label adopts the source font + height (text-family, both in EntityFamily::Text).
    engine.submit(SelectPickCommand{{5, 30}, 2.0, false});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        if (s.selection.size() != 1 || s.selection[0].kind != EntityKind::MLeader) {
            return false;
        }
        const PropertyField* h = field_of(s.selection_summary, PropertyId::TextHeight);
        const PropertyField* f = field_of(s.selection_summary, PropertyId::TextFont);
        return h != nullptr && h->value.num == Approx(5.0) && f != nullptr && f->value.text == "Arial";
    }));

    // The flat LEADER label adopts the source font + height too.
    engine.submit(ClearSelectionCommand{});
    engine.submit(SelectPickCommand{{5, 60}, 2.0, false});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        if (s.selection.size() != 1 || s.selection[0].kind != EntityKind::Leader) {
            return false;
        }
        const PropertyField* h = field_of(s.selection_summary, PropertyId::TextHeight);
        const PropertyField* f = field_of(s.selection_summary, PropertyId::TextFont);
        return h != nullptr && h->value.num == Approx(5.0) && f != nullptr && f->value.text == "Arial";
    }));
    engine.stop();
}

TEST_CASE("MATCHPROP Settings filter gates a category (Color off keeps target colour)") {
    GeometryEngine engine;
    engine.start();
    EntityProps sp; // source: blue + lineweight 70
    sp.set_color_by_layer(false);
    sp.color = {0, 0, 255};
    sp.set_lineweight_by_layer(false);
    sp.lineweight = 70;
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1, sp});
    EntityProps tp; // target: red override, default lineweight
    tp.set_color_by_layer(false);
    tp.color = {255, 0, 0};
    engine.submit(AddLineCommand{{0, 5}, {10, 5}, 2, tp});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(ClearSelectionCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.empty(); }));

    MatchPropFilter f; // Color OFF, everything else on
    f.color = false;
    engine.submit(MatchPropPickSourceCommand{{5, 0}, 1.0});
    engine.submit(MatchPropApplyCommand{{5, 5}, 1.0, f, 9});
    engine.submit(SelectPickCommand{{5, 5}, 1.0, false});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        if (s.selection.size() != 1) {
            return false;
        }
        const PropertyField* col = field_of(s.selection_summary, PropertyId::Color);
        const PropertyField* lw = field_of(s.selection_summary, PropertyId::Lineweight);
        // Colour unchanged (still red); lineweight DID copy (70).
        return col != nullptr && !col->value.flag && col->value.color == Rgb{255, 0, 0} &&
               lw != nullptr && lw->value.num == Approx(70.0);
    }));
    engine.stop();
}

TEST_CASE("MATCHPROP noun-verb: a pre-selected object becomes the source") {
    GeometryEngine engine;
    engine.start();
    EntityProps sp; // source line: blue + lineweight 40
    sp.set_color_by_layer(false);
    sp.color = {0, 0, 255};
    sp.set_lineweight_by_layer(false);
    sp.lineweight = 40;
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1, sp});
    engine.submit(AddCircleCommand{{50, 0}, 10, 2}); // target, default props
    // Pre-select ONLY the line (the future source) -- noun-verb entry into MA.
    engine.submit(SelectPickCommand{{5, 0}, 1.0, false});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.selection.size() == 1 && s.selection[0].kind == EntityKind::Line;
    }));
    engine.submit(MatchPropSourceFromSelectionCommand{}); // source = the selection
    engine.submit(MatchPropApplyCommand{{60, 0}, 1.0, MatchPropFilter{}, 9});
    engine.submit(SelectPickCommand{{60, 0}, 1.0, false}); // re-select the circle to read it
    REQUIRE(wait_until(engine, [](const auto& s) {
        if (s.selection.size() != 1 || s.selection[0].kind != EntityKind::Circle) {
            return false;
        }
        const PropertyField* col = field_of(s.selection_summary, PropertyId::Color);
        const PropertyField* lw = field_of(s.selection_summary, PropertyId::Lineweight);
        return col != nullptr && !col->value.flag && col->value.color == Rgb{0, 0, 255} &&
               lw != nullptr && lw->value.num == Approx(40.0);
    }));
    engine.stop();
}

TEST_CASE("Dimension PR group: arrow-size override set + reset via SetPropertyCommand") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear),
                                      {0, 0}, {10, 0}, {5, 3}, 0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    // The dim group is present; arrow size is ByStyle initially (flag = true).
    const auto field = [&](PropertyId id) {
        for (const auto& f : engine.snapshot().selection_summary.fields) {
            if (f.id == id) {
                return f.value;
            }
        }
        return PropertyValue{};
    };
    const auto has = [&](PropertyId id) {
        for (const auto& f : engine.snapshot().selection_summary.fields) {
            if (f.id == id) {
                return true;
            }
        }
        return false;
    };
    REQUIRE(has(PropertyId::DimArrowSize));
    REQUIRE(has(PropertyId::DimTextColor));
    REQUIRE(field(PropertyId::DimArrowSize).flag); // ByStyle

    // Override arrow size = 9 on this dim (flag=false => set override).
    PropertyValue set;
    set.flag = false;
    set.num = 9.0;
    engine.submit(SetPropertyCommand{PropertyId::DimArrowSize, set, 7});
    REQUIRE(wait_until(engine, [&](const auto&) {
        const auto v = field(PropertyId::DimArrowSize);
        return !v.flag && v.num == Approx(9.0); // Overridden, effective = 9
    }));

    // Reset to ByStyle (flag=true) -> follows the style again.
    PropertyValue reset;
    reset.flag = true;
    engine.submit(SetPropertyCommand{PropertyId::DimArrowSize, reset, 8});
    REQUIRE(wait_until(engine, [&](const auto&) {
        const auto v = field(PropertyId::DimArrowSize);
        return v.flag && v.num == Approx(2.5); // ByStyle, effective = style arrow_size
    }));
    engine.stop();
}

TEST_CASE("PR edits per-entity CELTSCALE (Celtscale descriptor, dashing kinds)") {
    GeometryEngine engine;
    engine.start();
    EntityProps c;
    c.set_linetype_by_layer(false);
    c.linetype = Linetype::Center;
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1, c});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(SetPropertyCommand{PropertyId::Celtscale, numv(0.4), 5});
    REQUIRE(wait_until(engine, [](const auto& s) {
        const PropertyField* f = field_of(s.selection_summary, PropertyId::Celtscale);
        return f != nullptr && f->value.num == Approx(0.4);
    }));
    engine.stop();
}

TEST_CASE("MATCHPROP copies CELTSCALE (universal) between dashing entities") {
    GeometryEngine engine;
    engine.start();
    EntityProps c;
    c.set_linetype_by_layer(false);
    c.linetype = Linetype::Center;
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1, c}); // source line
    engine.submit(AddCircleCommand{{200, 0}, 20, 2});      // target circle
    engine.submit(SelectPickCommand{{50, 0}, 2.0, false}); // select the source line
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.selection.size() == 1 && s.selection[0].kind == EntityKind::Line;
    }));
    engine.submit(SetPropertyCommand{PropertyId::Celtscale, numv(0.3), 5}); // source CELTSCALE 0.3
    REQUIRE(wait_until(engine, [](const auto& s) {
        const PropertyField* f = field_of(s.selection_summary, PropertyId::Celtscale);
        return f != nullptr && f->value.num == Approx(0.3);
    }));
    engine.submit(ClearSelectionCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.empty(); }));
    engine.submit(MatchPropPickSourceCommand{{50, 0}, 2.0});
    engine.submit(MatchPropApplyCommand{{220, 0}, 2.0, MatchPropFilter{}, 9});
    engine.submit(SelectPickCommand{{220, 0}, 2.0, false}); // re-select the circle to read it
    REQUIRE(wait_until(engine, [](const auto& s) {
        if (s.selection.size() != 1 || s.selection[0].kind != EntityKind::Circle) {
            return false;
        }
        const PropertyField* f = field_of(s.selection_summary, PropertyId::Celtscale);
        return f != nullptr && f->value.num == Approx(0.3); // copied across kinds (universal)
    }));
    engine.stop();
}

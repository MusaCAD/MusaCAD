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

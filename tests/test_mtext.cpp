// Phase 20: MTEXT (paragraph text) + QLEADER (editable leader with owned label).
//  - layout is COMPUTED from the stored fields (word-wrap), never baked;
//  - width grip re-wraps; QLEADER label moves with the leader (association);
//  - native + DXF round-trip; PR-readiness = discrete editable fields.

#include <chrono>
#include <thread>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/text/mtext.hpp"

using namespace musacad::core;
using Catch::Approx;

TEST_CASE("MTEXT layout word-wraps within the defined width into multiple lines") {
    MTextBlock narrow;
    narrow.pos = {0, 0};
    narrow.height = 2.5;
    narrow.width = 12.0; // forces several lines
    const std::string content = "THE QUICK BROWN FOX JUMPS";
    const text::MTextLayout wrapped = text::layout_mtext(narrow, content);
    REQUIRE(wrapped.line_count >= 3);

    // Widen so it fits on fewer lines -> proves layout follows the field, not baked.
    MTextBlock wide = narrow;
    wide.width = 1000.0;
    const text::MTextLayout one = text::layout_mtext(wide, content);
    REQUIRE(one.line_count == 1);
    REQUIRE(one.line_count < wrapped.line_count);
}

TEST_CASE("MTEXT explicit newlines split paragraphs") {
    MTextBlock b;
    b.height = 2.5;
    b.width = 0.0; // no wrap; only explicit breaks
    const text::MTextLayout lay = text::layout_mtext(b, "LINE ONE\nLINE TWO\nLINE THREE");
    REQUIRE(lay.line_count == 3);
}

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
} // namespace

TEST_CASE("MTEXT width grip re-wraps live; insertion grip moves; undoable") {
    GeometryEngine engine;
    engine.start();
    MTextBlock b;
    b.pos = {0, 0};
    b.height = 2.5;
    b.width = 200.0; // wide: one line
    engine.submit(AddMTextCommand{b, "THE QUICK BROWN FOX JUMPS OVER", 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    // One wide line: all glyphs sit in the first line band (y >= -height ~ -2.5).
    const auto has_second_line = [](const RenderSnapshot& s) {
        for (const Vec2& v : s.line_vertices) {
            if (v.y < -3.0) {
                return true; // a glyph on a wrapped second/third line
            }
        }
        return false;
    };
    REQUIRE_FALSE(has_second_line(engine.snapshot()));

    engine.submit(SelectPickCommand{{0, 0}, 3.0, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grips.size() == 2; })); // insertion + width
    GripInfo wgrip{};
    for (const GripInfo& g : engine.snapshot().grips) {
        if (g.index == 1) {
            wgrip = g; // the width grip
        }
    }
    // Drag the width grip inward to ~12 units -> the paragraph re-wraps to >1 line.
    engine.submit(GripDragCommand{GripDragCommand::Phase::Begin, wgrip.handle, wgrip.index, {}, 0});
    engine.submit(GripDragCommand{GripDragCommand::Phase::Commit, {}, 0, {12, 0}, 7});
    REQUIRE(wait_until(engine, [&](const auto& s) { return has_second_line(s); }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [&](const auto& s) { return !has_second_line(s); }));
    engine.stop();
}

TEST_CASE("QLEADER: moving the leader moves the attached text (association)") {
    GeometryEngine engine;
    engine.start();
    MTextBlock b;
    b.pos = {20, 10}; // landing / text anchor
    b.height = 2.5;
    engine.submit(AddMLeaderCommand{{{0, 0}, {10, 10}, {20, 10}}, 0, b, "NOTE", 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    // Select + move the whole leader; the owned label travels with it. Grips:
    // 3 vertices + 1 text grip = 4.
    engine.submit(SelectPickCommand{{5, 5}, 2.0, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grips.size() == 4; }));
    // The text grip is the last one (index == vertex count == 3); it sits at the label.
    bool has_text_grip = false;
    for (const GripInfo& g : engine.snapshot().grips) {
        if (g.index == 3) {
            has_text_grip = (length_squared(g.pos - Vec2{20, 10}) < 1e-6);
        }
    }
    REQUIRE(has_text_grip);

    // Move the whole selection by (100,0): both the leader and its label shift.
    engine.submit(MoveSelectionCommand{{100, 0}, 8});
    REQUIRE(wait_until(engine, [](const auto& s) {
        for (const Vec2& v : s.line_vertices) {
            if (length_squared(v - Vec2{120, 10}) < 4.0) {
                return true; // label glyphs near the moved landing
            }
        }
        return false;
    }));
    engine.stop();
}

TEST_CASE("MTEXT + QLEADER round-trip native + DXF; stored as discrete fields") {
    using namespace musacad::core::io;
    Document doc;
    MTextBlock mb;
    mb.pos = {1, 2};
    mb.width = 50.0;
    mb.height = 3.0;
    mb.line_spacing = 1.5;
    mb.attach = 4; // middle-center
    doc.mtexts.push_back(DocMText{mb, "ALPHA BETA\nGAMMA", {}});
    MTextBlock lb;
    lb.pos = {20, 10};
    lb.height = 2.5;
    doc.mleaders.push_back(DocMLeader{{{0, 0}, {10, 10}, {20, 10}}, 0, lb, "SEE HERE", {}});

    // Native: lossless (discrete fields preserved -> PR-editable next phase).
    Document nb;
    REQUIRE(parse_native(serialize_native(doc), nb).ok);
    REQUIRE(nb.mtexts.size() == 1);
    REQUIRE(nb.mtexts[0].content == "ALPHA BETA\nGAMMA");
    REQUIRE(nb.mtexts[0].block.width == Approx(50.0));
    REQUIRE(nb.mtexts[0].block.line_spacing == Approx(1.5));
    REQUIRE(nb.mtexts[0].block.attach == 4);
    REQUIRE(nb.mleaders.size() == 1);
    REQUIRE(nb.mleaders[0].vertices.size() == 3);
    REQUIRE(nb.mleaders[0].content == "SEE HERE");

    // DXF: MTEXT comes back; the leader exports as LEADER + MTEXT (association is a
    // stated fidelity gap, so on import the count of MTEXTs is the standalone + the
    // leader's label).
    Document db;
    REQUIRE(parse_dxf(serialize_dxf(doc), db).ok);
    bool found_alpha = false;
    for (const DocMText& m : db.mtexts) {
        found_alpha = found_alpha || m.content == "ALPHA BETA\nGAMMA";
    }
    REQUIRE(found_alpha);
}

// Part A transform commands: ROTATE, SCALE, ARRAY (rectangular + polar), each on
// the selection and undoable as one group.

#include <chrono>
#include <cmath>
#include <thread>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/text_mirror.hpp"

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
bool has_segment(const RenderSnapshot& s, Vec2 a, Vec2 b, double eps = 1e-6) {
    const auto eq = [&](Vec2 p, Vec2 q) {
        return std::abs(p.x - q.x) < eps && std::abs(p.y - q.y) < eps;
    };
    for (std::size_t i = 0; i + 1 < s.line_vertices.size(); i += 2) {
        const Vec2 p = s.line_vertices[i];
        const Vec2 q = s.line_vertices[i + 1];
        if ((eq(p, a) && eq(q, b)) || (eq(p, b) && eq(q, a))) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("ROTATE turns the selection about a base point; undo restores") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    // 90 degrees about the origin: (10,0) -> (0,10).
    engine.submit(RotateSelectionCommand{{0, 0}, std::atan2(1.0, 0.0), 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {0, 10}); }));

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));
    engine.stop();
}

TEST_CASE("SCALE resizes the selection about a base point; undo restores") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(ScaleSelectionCommand{{0, 0}, 2.0, 10}); // (10,0) -> (20,0)
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {20, 0}); }));

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));
    engine.stop();
}

TEST_CASE("ARRAY rectangular replicates the selection on a grid; undo restores") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {1, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    // 2 rows x 3 cols, spacing 10 -> 6 lines total (original + 5 copies).
    engine.submit(
        ArrayRectCommand{.rows = 2, .cols = 3, .dx = 10.0, .dy = 10.0, .angle = 0.0, .group = 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 12; }));
    REQUIRE(has_segment(engine.snapshot(), {0, 0}, {1, 0}));     // original
    REQUIRE(has_segment(engine.snapshot(), {20, 10}, {21, 10})); // row 1, col 2

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.stop();
}

TEST_CASE("ARRAY polar replicates the selection around a centre; undo restores") {
    GeometryEngine engine;
    engine.start();
    // A short line near (10,0); polar array of 4 around the origin (full circle).
    engine.submit(AddLineCommand{{10, 0}, {11, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(ArrayPolarCommand{{0, 0}, 4, std::atan2(0.0, -1.0) * 2.0, true, 10});
    // 4 items total = original + 3 copies -> 8 vertices. The 90-degree copy maps
    // (10,0)->(0,10), (11,0)->(0,11).
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));
    REQUIRE(has_segment(engine.snapshot(), {0, 10}, {0, 11}, 1e-6));

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.stop();
}

// ---------------------------------------------------------------------------
// The live ROTATE / SCALE band (TransformPreviewCommand): the selection under the
// transform, previewed on the scratch store -- re-tessellated, every kind, and no
// change to the drawing until the commit, which ends the band.
// ---------------------------------------------------------------------------
namespace {
bool preview_has(const RenderSnapshot& s, Vec2 p, double eps = 1e-6) {
    for (const Vec2& v : s.grip_preview_segments) {
        if (std::abs(v.x - p.x) < eps && std::abs(v.y - p.y) < eps) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("SCALE band: the selection scaled about the base point, the drawing untouched") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    const std::uint64_t gv = engine.snapshot().geometry_version;

    engine.submit(TransformPreviewCommand{TransformPreviewCommand::Kind::Scale, {0, 0}, {2, 0}, 2.0, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return preview_has(s, {200, 0}); }));
    REQUIRE(preview_has(engine.snapshot(), {0, 0}));
    REQUIRE(engine.snapshot().geometry_version == gv);
    REQUIRE(has_segment(engine.snapshot(), {0, 0}, {100, 0})); // the line itself unchanged

    // The cursor moves on: the band follows (about a different base, a smaller factor).
    engine.submit(TransformPreviewCommand{TransformPreviewCommand::Kind::Scale, {100, 0}, {}, 0.5, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return preview_has(s, {50, 0}) && preview_has(s, {100, 0}); }));

    // The band ends with the commit: the drawing changes, the preview goes.
    engine.submit(ScaleSelectionCommand{{0, 0}, 3.0, 7, false});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.grip_preview_segments.empty() && has_segment(s, {0, 0}, {300, 0});
    }));
    engine.stop();
}

TEST_CASE("ROTATE band: the selection turned about the base point; an inactive band clears") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(TransformPreviewCommand{TransformPreviewCommand::Kind::Rotate, {0, 0}, {}, kHalfPi, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return preview_has(s, {0, 100}); }));
    REQUIRE(has_segment(engine.snapshot(), {0, 0}, {100, 0}));

    // Esc: the command sends an inactive band; nothing was changed.
    engine.submit(TransformPreviewCommand{TransformPreviewCommand::Kind::Rotate, {}, {}, 0.0, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grip_preview_segments.empty(); }));
    REQUIRE(has_segment(engine.snapshot(), {0, 0}, {100, 0}));

    // A non-positive scale factor never previews (the collapsed selection is not a band).
    engine.submit(TransformPreviewCommand{TransformPreviewCommand::Kind::Scale, {0, 0}, {}, 0.0, true});
    Layer marker;
    marker.name = "marker";
    engine.submit(AddLayerCommand{marker}); // a later publish, with the selection kept
    REQUIRE(wait_until(engine, [](const auto& s) { return s.layers.size() == 2; }));
    REQUIRE(engine.snapshot().selection.size() == 1);
    REQUIRE(engine.snapshot().grip_preview_segments.empty());
    engine.stop();
}

TEST_CASE("#50: MIRRTEXT 0 keeps mirrored text readable; 1 reflects it") {
    // Across a vertical axis a left-justified, unrotated text is turned round: rotation 0
    // again, now right-justified at the reflected point.
    std::uint8_t j = 0;
    double r = mirrored_text_rotation(0.0, kHalfPi, false, j);
    CHECK(std::cos(r) == Approx(1.0));
    CHECK(j == 2);
    // Across a horizontal axis it already reads the right way: nothing turns.
    j = 0;
    r = mirrored_text_rotation(0.0, 0.0, false, j);
    CHECK(std::cos(r) == Approx(1.0));
    CHECK(j == 0);
    // A text reading upwards (90 degrees) across a vertical axis stays readable upwards.
    j = 1;
    r = mirrored_text_rotation(kHalfPi, kHalfPi, false, j);
    CHECK(std::sin(r) == Approx(1.0));
    CHECK(j == 1);
    // MIRRTEXT 1: the plain reflection (reading back to front).
    j = 0;
    r = mirrored_text_rotation(0.0, kHalfPi, true, j);
    CHECK(std::cos(r) == Approx(-1.0));
    CHECK(j == 0);
    // MTEXT: the attachment's column swaps sides when the text turns round.
    std::uint8_t attach = 0; // TL
    r = mirrored_mtext_rotation(0.0, kHalfPi, false, attach);
    CHECK(attach == 2); // TR
    attach = 7; // BC
    r = mirrored_mtext_rotation(0.0, kHalfPi, false, attach);
    CHECK(attach == 7);
    (void)r;
}

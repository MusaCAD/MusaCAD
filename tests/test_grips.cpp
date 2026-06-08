// Phase 17: grip editing (direct manipulation).
//  - core: grips_of exposes per-entity grips; edit_for_grip_drag edits parametrically;
//  - engine: a grip drag is a transient preview (zero store mutation / op-log churn),
//    commits as one undo group on release, and Cancel/undo restore the entity.

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

using namespace musacad::core;
using Catch::Approx;

TEST_CASE("grips_of exposes the expected grips per entity") {
    GeometryStore s;
    std::vector<Grip> g;

    g.clear();
    grips_of(s, s.add_line({0, 0}, {10, 0}), g);
    REQUIRE(g.size() == 3); // 2 endpoints + midpoint

    g.clear();
    grips_of(s, s.add_circle({0, 0}, 5.0), g);
    REQUIRE(g.size() == 5); // centre + 4 quadrants

    g.clear();
    const std::array<Vec2, 3> pts{{{0, 0}, {1, 1}, {2, 0}}};
    grips_of(s, s.add_polyline(pts, false), g);
    REQUIRE(g.size() == 3); // one per vertex

    g.clear();
    grips_of(s, s.add_dimension(DimType::Linear, {0, 0}, {10, 0}, {5, 3}, 0), g);
    REQUIRE(g.size() == 5); // def a, def b, both dim-line feet, offset midpoint
}

TEST_CASE("edit_for_grip_drag edits parameters, staying parametric") {
    GeometryStore s;

    // Line endpoint grip moves that end.
    const EntityHandle line = s.add_line({0, 0}, {10, 0});
    const auto le = std::get<AddLineCommand>(edit_for_grip_drag(s, line, 1, {10, 10}));
    REQUIRE(le.a.x == Approx(0.0));
    REQUIRE(le.b.y == Approx(10.0));

    // Circle quadrant grip changes the radius (still a circle, not baked segments).
    const EntityHandle circ = s.add_circle({0, 0}, 5.0);
    const auto ce = std::get<AddCircleCommand>(edit_for_grip_drag(s, circ, 1, {8, 0}));
    REQUIRE(ce.radius == Approx(8.0));

    // Dimension def-point grip moves a def point -> the value re-measures.
    const EntityHandle dim = s.add_dimension(DimType::Linear, {0, 0}, {10, 0}, {5, 3}, 0);
    const auto de = std::get<AddDimensionCommand>(edit_for_grip_drag(s, dim, 1, {20, 0}));
    REQUIRE(de.a.x == Approx(0.0)); // a unchanged
    REQUIRE(de.b.x == Approx(20.0)); // b moved -> measures 20 now

    // Dimension dim-line offset grip moves the line, leaving the def points (value).
    const auto off = std::get<AddDimensionCommand>(edit_for_grip_drag(s, dim, 2, {5, 12}));
    REQUIRE(off.a.x == Approx(0.0));
    REQUIRE(off.b.x == Approx(10.0)); // def points unchanged -> value unchanged
    REQUIRE(off.line_pt.y == Approx(12.0)); // dim line moved
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
std::size_t verts_near(const RenderSnapshot& s, const std::vector<Vec2>& v, Vec2 p, double r) {
    std::size_t n = 0;
    for (const Vec2& q : v) {
        if (length_squared(q - p) <= r * r) {
            ++n;
        }
    }
    return n;
}
} // namespace

TEST_CASE("Grip drag is a transient preview; commits as one undo group on release") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    // Select the line so its grips publish.
    engine.submit(SelectPickCommand{{5, 0}, 1.0, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.grips.empty(); }));
    // Find the endpoint-b grip (index 1).
    GripInfo gb{};
    for (const GripInfo& g : engine.snapshot().grips) {
        if (g.index == 1) {
            gb = g;
        }
    }
    const std::uint64_t gv_before = engine.snapshot().geometry_version;

    // Begin + drag endpoint b toward (10,10) -- preview only.
    engine.submit(GripDragCommand{GripDragCommand::Phase::Begin, gb.handle, gb.index, {}, 0});
    engine.submit(GripDragCommand{GripDragCommand::Phase::Move, {}, 0, {10, 10}, 0});
    REQUIRE(wait_until(engine,
                       [](const auto& s) { return !s.grip_preview_segments.empty(); }));
    {
        const RenderSnapshot& s = engine.snapshot();
        // Preview shows the dragged endpoint at (10,10)...
        REQUIRE(verts_near(s, s.grip_preview_segments, {10, 10}, 0.1) > 0);
        // ...but the STORE is untouched: geometry unchanged, no (10,10) in the scene.
        REQUIRE(s.geometry_version == gv_before);
        REQUIRE(verts_near(s, s.line_vertices, {10, 10}, 0.1) == 0);
    }

    // Release -> commit as ONE undo group: the scene now has the moved endpoint.
    engine.submit(GripDragCommand{GripDragCommand::Phase::Commit, {}, 0, {10, 10}, 42});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return verts_near(s, s.line_vertices, {10, 10}, 0.1) > 0;
    }));
    REQUIRE(engine.snapshot().grip_preview_segments.empty()); // drag ended

    // Undo restores the original line in one step.
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return verts_near(s, s.line_vertices, {10, 10}, 0.1) == 0 &&
               verts_near(s, s.line_vertices, {10, 0}, 0.1) > 0;
    }));
    engine.stop();
}

TEST_CASE("Dimension: a non-centre grip is grabbable; dim-line drag moves it freely") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear),
                                      {0, 0}, {10, 0}, {5, 3}, 0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(SelectPickCommand{{5, 3}, 2.0, false});
    // Full grip set: both def points, both dim-line feet, the offset midpoint.
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grips.size() == 5; }));

    // Grab a dim-line FOOT grip (index 2) -- a non-centre handle.
    GripInfo foot{};
    bool found = false;
    for (const GripInfo& g : engine.snapshot().grips) {
        if (g.index == 2) {
            foot = g;
            found = true;
        }
    }
    REQUIRE(found);
    REQUIRE(foot.kind == static_cast<std::uint8_t>(GripKind::DimLine));
    const std::uint64_t gv = engine.snapshot().geometry_version;

    // Drag it far away -> the whole dim line slides there (transient preview only).
    engine.submit(GripDragCommand{GripDragCommand::Phase::Begin, foot.handle, foot.index, {}, 0});
    engine.submit(GripDragCommand{GripDragCommand::Phase::Move, {}, 0, {5, 12}, 0});
    REQUIRE(wait_until(engine,
                       [](const auto& s) { return !s.grip_preview_segments.empty(); }));
    {
        const RenderSnapshot& s = engine.snapshot();
        REQUIRE(s.geometry_version == gv);                                // zero churn
        REQUIRE(verts_near(s, s.line_vertices, {10, 12}, 0.3) == 0);       // store untouched
        REQUIRE(verts_near(s, s.grip_preview_segments, {10, 12}, 0.3) > 0); // preview moved
    }
    // Release -> one undo group; the dim line is now up at y~12.
    engine.submit(GripDragCommand{GripDragCommand::Phase::Commit, {}, 0, {5, 12}, 9});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return verts_near(s, s.line_vertices, {10, 12}, 0.3) > 0;
    }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return verts_near(s, s.line_vertices, {10, 12}, 0.3) == 0;
    }));
    engine.stop();
}

TEST_CASE("Esc cancels a grip drag, leaving the entity unchanged") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 5.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(SelectPickCommand{{5, 0}, 1.0, false}); // pick on the rim
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.grips.empty(); }));
    GripInfo q{};
    for (const GripInfo& g : engine.snapshot().grips) {
        if (g.index == 1) {
            q = g; // an E quadrant -> radius grip
        }
    }
    const std::uint64_t gv = engine.snapshot().geometry_version;
    engine.submit(GripDragCommand{GripDragCommand::Phase::Begin, q.handle, q.index, {}, 0});
    engine.submit(GripDragCommand{GripDragCommand::Phase::Move, {}, 0, {40, 0}, 0});
    REQUIRE(wait_until(engine,
                       [](const auto& s) { return !s.grip_preview_segments.empty(); }));
    // Cancel: no commit, geometry never changed.
    engine.submit(GripDragCommand{GripDragCommand::Phase::Cancel, {}, 0, {}, 0});
    REQUIRE(wait_until(engine,
                       [](const auto& s) { return s.grip_preview_segments.empty(); }));
    REQUIRE(engine.snapshot().geometry_version == gv);
    engine.stop();
}

TEST_CASE("Grip-edited geometry stays parametric and round-trips native + DXF") {
    using namespace musacad::core::io;
    GeometryStore s;
    const EntityHandle circ = s.add_circle({1, 2}, 5.0);
    // Drag a quadrant grip to radius 9 -> still a parametric circle.
    const Command edited = edit_for_grip_drag(s, circ, 1, {1 + 9.0, 2});
    s.remove(circ);
    add_command_to_store(s, edited, EntityProps{0});

    const Document doc = document_from_store(s);
    REQUIRE(doc.circles.size() == 1);
    REQUIRE(doc.circles[0].radius == Approx(9.0)); // parametric, not baked

    // Native round-trip.
    Document nb;
    REQUIRE(parse_native(serialize_native(doc), nb).ok);
    REQUIRE(nb.circles.size() == 1);
    REQUIRE(nb.circles[0].radius == Approx(9.0));

    // DXF round-trip.
    Document db;
    REQUIRE(parse_dxf(serialize_dxf(doc), db).ok);
    REQUIRE(db.circles.size() == 1);
    REQUIRE(db.circles[0].radius == Approx(9.0));
}

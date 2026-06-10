// Phase 28 Part A: real block entities -- the block-definition table + INSERT
// references, and the single transformed-geometry resolution path. Geometry lives
// once in the definition; instances are lightweight transforms resolved on demand.

#include <cmath>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/block_resolve.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/native_kernel_2d.hpp"

using namespace musacad::core;
using namespace musacad::core::io;
using Catch::Approx;

namespace {
// A unit-square block (lines) + a circle, base at origin.
BlockDef square_block(const char* name) {
    BlockDef b;
    b.name = name;
    b.content.lines.push_back(LineData{{0, 0}, {1, 0}});
    b.content.lines.push_back(LineData{{1, 0}, {1, 1}});
    b.content.lines.push_back(LineData{{1, 1}, {0, 1}});
    b.content.lines.push_back(LineData{{0, 1}, {0, 0}});
    b.content.circles.push_back(CircleData{{0.5, 0.5}, 0.25});
    return b;
}

bool has_seg(const std::vector<InsertSeg>& segs, Vec2 a, Vec2 b, double tol = 1e-6) {
    for (const InsertSeg& s : segs) {
        const bool fwd = (std::abs(s.a.x - a.x) < tol && std::abs(s.a.y - a.y) < tol &&
                          std::abs(s.b.x - b.x) < tol && std::abs(s.b.y - b.y) < tol);
        const bool rev = (std::abs(s.a.x - b.x) < tol && std::abs(s.a.y - b.y) < tol &&
                          std::abs(s.b.x - a.x) < tol && std::abs(s.b.y - a.y) < tol);
        if (fwd || rev) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("hot entity structs stay compact; new block structs are reported") {
    // The new INSERT entity must not fatten the per-primitive hot path.
    REQUIRE(sizeof(LineData) <= 40);
    REQUIRE(sizeof(CircleData) <= 40);
    // InsertData is a small transform + a definition index; report it.
    INFO("sizeof(InsertData)=" << sizeof(InsertData) << " sizeof(BlockDef)=" << sizeof(BlockDef));
    REQUIRE(sizeof(InsertData) <= 64);
}

TEST_CASE("INSERT geometry lives once in the definition, not per-instance in the store") {
    GeometryStore store;
    const std::uint16_t b = store.add_block(square_block("SQ"));

    // Three instances of the same block.
    store.add_insert(b, {10, 0}, 1.0, 1.0, 0.0);
    store.add_insert(b, {20, 0}, 1.0, 1.0, 0.0);
    store.add_insert(b, {30, 0}, 1.0, 1.0, 0.0);

    // The model space holds exactly the 3 inserts -- the block's 4 lines + circle are
    // NOT copied into the arenas (no bloat: 10 instances != 10 geometry copies).
    REQUIRE(store.live_count() == 3);
    REQUIRE(store.lines().live_count() == 0);
    REQUIRE(store.circles().live_count() == 0);
    REQUIRE(store.block_count() == 1);
}

TEST_CASE("resolve_insert applies the instance transform to the definition") {
    GeometryStore store;
    const std::uint16_t b = store.add_block(square_block("SQ"));

    SECTION("pure translation") {
        const EntityHandle h = store.add_insert(b, {10, 5}, 1.0, 1.0, 0.0);
        std::vector<InsertSeg> segs;
        resolve_insert(store, *store.insert(h), 0.01, segs);
        // The square's corners are shifted by the insertion point.
        REQUIRE(has_seg(segs, {10, 5}, {11, 5}));
        REQUIRE(has_seg(segs, {11, 5}, {11, 6}));
        REQUIRE(has_seg(segs, {10, 6}, {10, 5}));
    }

    SECTION("non-uniform scale") {
        const EntityHandle h = store.add_insert(b, {0, 0}, 2.0, 3.0, 0.0);
        std::vector<InsertSeg> segs;
        resolve_insert(store, *store.insert(h), 0.01, segs);
        REQUIRE(has_seg(segs, {0, 0}, {2, 0})); // 1*2 along x
        REQUIRE(has_seg(segs, {2, 0}, {2, 3})); // 1*3 along y
    }

    SECTION("90-degree rotation") {
        const EntityHandle h = store.add_insert(b, {0, 0}, 1.0, 1.0, M_PI / 2.0);
        std::vector<InsertSeg> segs;
        resolve_insert(store, *store.insert(h), 0.01, segs);
        // (1,0) rotates to (0,1); the base edge (0,0)->(1,0) becomes (0,0)->(0,1).
        REQUIRE(has_seg(segs, {0, 0}, {0, 1}));
    }
}

TEST_CASE("nested blocks compose transforms (parent x child)") {
    GeometryStore store;
    const std::uint16_t inner = store.add_block(square_block("INNER"));

    // OUTER places INNER at offset (5,0), scaled 2x.
    BlockDef outer;
    outer.name = "OUTER";
    outer.content.inserts.push_back(InsertData{inner, {5, 0}, 2.0, 2.0, 0.0});
    const std::uint16_t o = store.add_block(outer);

    // Place OUTER at (100, 0). INNER's (0,0)->(1,0) edge becomes, after the inner
    // transform: (5,0)->(7,0); then after the outer translation: (105,0)->(107,0).
    const EntityHandle h = store.add_insert(o, {100, 0}, 1.0, 1.0, 0.0);
    std::vector<InsertSeg> segs;
    resolve_insert(store, *store.insert(h), 0.01, segs);
    REQUIRE_FALSE(segs.empty());
    REQUIRE(has_seg(segs, {105, 0}, {107, 0}));
}

TEST_CASE("nested-block depth guard terminates on a self-referential definition") {
    GeometryStore store;
    BlockDef loop;
    loop.name = "LOOP";
    const std::uint16_t l = store.add_block(loop); // index known before self-insert
    // Make LOOP reference itself (a pathological/cyclic definition).
    BlockDef loop2 = *store.block(l);
    loop2.content.inserts.push_back(InsertData{l, {1, 0}, 1.0, 1.0, 0.0});
    loop2.content.lines.push_back(LineData{{0, 0}, {1, 0}});
    store.set_block_table({loop2});

    const EntityHandle h = store.add_insert(0, {0, 0}, 1.0, 1.0, 0.0);
    std::vector<InsertSeg> segs;
    // Must terminate (depth-guarded), not recurse forever.
    resolve_insert(store, *store.insert(h), 0.01, segs);
    REQUIRE(segs.size() <= static_cast<std::size_t>(kMaxBlockDepth) + 2);
}

TEST_CASE("DXF import reads BLOCK definitions and INSERT references") {
    const std::string dxf =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nINSERT\n8\n0\n2\nWIDGET\n10\n100\n20\n50\n41\n2\n42\n2\n50\n0\n"
        "0\nENDSEC\n"
        "0\nSECTION\n2\nBLOCKS\n"
        "0\nBLOCK\n2\nWIDGET\n10\n0\n20\n0\n"
        "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n1\n21\n0\n"
        "0\nCIRCLE\n8\n0\n10\n0\n20\n0\n40\n0.5\n"
        "0\nENDBLK\n"
        "0\nENDSEC\n0\nEOF\n";
    Document doc;
    REQUIRE(parse_dxf(dxf, doc).ok);
    REQUIRE(doc.block_defs.size() == 1);
    REQUIRE(doc.block_defs[0].name == "WIDGET");
    REQUIRE(doc.block_defs[0].lines.size() == 1);
    REQUIRE(doc.block_defs[0].circles.size() == 1);
    REQUIRE(doc.inserts.size() == 1);
    REQUIRE(doc.inserts[0].block_name == "WIDGET");
    REQUIRE(doc.inserts[0].pos.x == Approx(100));
    REQUIRE(doc.inserts[0].scale_x == Approx(2));

    // Loads into a store: the block geometry stays in the definition, one insert in
    // model space (the previously-empty block-heavy file now recovers its geometry).
    GeometryStore store;
    populate_store(store, doc);
    REQUIRE(store.block_count() == 1);
    REQUIRE(store.inserts().live_count() == 1);
    REQUIRE(store.lines().live_count() == 0); // not exploded into model space
    std::vector<InsertSeg> segs;
    resolve_insert(store, *store.insert(EntityHandle{0, 1, EntityKind::Insert}), 0.01, segs);
    REQUIRE_FALSE(segs.empty()); // resolves to world geometry
}

TEST_CASE("native format round-trips block definitions and inserts (v9)") {
    Document doc;
    DocBlockDef bd;
    bd.name = "B";
    bd.base = {0, 0};
    bd.lines.push_back(DocLine{{0, 0}, {1, 1}});
    bd.circles.push_back(DocCircle{{0, 0}, 2.0});
    bd.inserts.push_back(DocInsert{"B", {3, 3}, 1, 1, 0}); // nested self-ref (depth-guarded)
    doc.block_defs.push_back(bd);
    doc.inserts.push_back(DocInsert{"B", {10, 20}, 1.5, 2.5, 0.3});

    Document rt;
    REQUIRE(parse_native(serialize_native(doc), rt).ok);
    REQUIRE(rt.block_defs.size() == 1);
    REQUIRE(rt.block_defs[0].name == "B");
    REQUIRE(rt.block_defs[0].lines.size() == 1);
    REQUIRE(rt.block_defs[0].circles.size() == 1);
    REQUIRE(rt.inserts.size() == 1);
    REQUIRE(rt.inserts[0].block_name == "B");
    REQUIRE(rt.inserts[0].scale_y == Approx(2.5));
    REQUIRE(rt == doc); // full structural equality
}

TEST_CASE("a block instance picks as one object and moves the instance, not the definition") {
    GeometryStore store;
    const std::uint16_t b = store.add_block(square_block("SQ"));
    const EntityHandle h = store.add_insert(b, {10, 0}, 1.0, 1.0, 0.0);

    NativeKernel2D kernel;
    SECTION("pick resolves to the whole insert against transformed geometry") {
        // A query near the block's bottom edge (world ~ (10.5, 0)) lands on real geometry;
        // closest_point on the INSERT handle returns a point on that transformed edge.
        Vec2 cp;
        REQUIRE(kernel.closest_point(store, h, {10.5, 0.02}, cp));
        REQUIRE(cp.y == Approx(0.0).margin(0.05));
        REQUIRE(cp.x == Approx(10.5).margin(0.05));
        // A query far from all geometry stays far (no phantom-connector false hit).
        Vec2 cp2;
        REQUIRE(kernel.closest_point(store, h, {10.5, 100.0}, cp2));
        REQUIRE(length(Vec2{10.5, 100.0} - cp2) > 50.0);
    }

    SECTION("move acts on the instance transform; the definition is untouched") {
        // The insert has a single move grip at its insertion point.
        std::vector<Grip> grips;
        grips_of(store, h, grips);
        REQUIRE(grips.size() == 1);
        REQUIRE(grips[0].pos.x == Approx(10.0));

        Command moved = edit_for_grip_drag(store, h, 0, {25, 5});
        const auto* ai = std::get_if<AddInsertCommand>(&moved);
        REQUIRE(ai != nullptr);
        REQUIRE(ai->pos.x == Approx(25.0));
        REQUIRE(ai->pos.y == Approx(5.0));
        REQUIRE(ai->block == b); // still references the same definition

        // The block definition's geometry is unchanged (move didn't bake/copy it).
        REQUIRE(store.block(b)->content.lines.size() == 4);
        REQUIRE(store.block(b)->content.lines[0].a.x == Approx(0.0));
    }

    SECTION("the insert's AABB encloses its transformed block geometry") {
        Vec2 lo, hi;
        REQUIRE(entity_aabb(store, h, lo, hi));
        REQUIRE(lo.x == Approx(10.0)); // square shifted to x in [10,11]
        REQUIRE(hi.x == Approx(11.0));
    }
}

TEST_CASE("DXF export then import keeps blocks (not exploded)") {
    Document doc;
    DocBlockDef bd;
    bd.name = "SQ";
    bd.lines.push_back(DocLine{{0, 0}, {1, 0}});
    doc.block_defs.push_back(bd);
    doc.inserts.push_back(DocInsert{"SQ", {5, 5}, 1, 1, 0});

    Document rt;
    REQUIRE(parse_dxf(serialize_dxf(doc), rt).ok);
    REQUIRE(rt.block_defs.size() == 1);
    REQUIRE(rt.block_defs[0].lines.size() == 1); // geometry stayed in the definition
    REQUIRE(rt.inserts.size() == 1);
    REQUIRE(rt.lines.empty()); // NOT exploded into model space
}

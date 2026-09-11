// Raster IMAGE entity (issue #10): the definition table, the derived placement quad,
// clipping, the path-traversal boundary, and native v18 persistence.
//
// The decoder itself is a Qt implementation above core, so these tests exercise the
// core half -- which is exactly the half that must work with NO decoder injected.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/image.hpp"
#include "musacad/core/image_decoder.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/scene_snapshot.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {

ImageData placed(double w, double h, double rot = 0.0) {
    ImageData d;
    d.pos = {10.0, 20.0};
    d.width = w;
    d.height = h;
    d.rotation = rot;
    return d;
}

} // namespace

TEST_CASE("The placement quad is derived from position, size and rotation") {
    const ImageQuad q = resolve_image_quad(placed(40.0, 30.0));
    // CCW from the insertion point: BL, BR, TR, TL.
    REQUIRE(q[0].x == Approx(10.0));
    REQUIRE(q[0].y == Approx(20.0));
    REQUIRE(q[1].x == Approx(50.0));
    REQUIRE(q[1].y == Approx(20.0));
    REQUIRE(q[2].x == Approx(50.0));
    REQUIRE(q[2].y == Approx(50.0));
    REQUIRE(q[3].x == Approx(10.0));
    REQUIRE(q[3].y == Approx(50.0));

    // Rotation is rigid: edge lengths are preserved.
    const ImageQuad r = resolve_image_quad(placed(40.0, 30.0, 0.7));
    REQUIRE(length(r[1] - r[0]) == Approx(40.0));
    REQUIRE(length(r[3] - r[0]) == Approx(30.0));
    REQUIRE(std::abs(dot(normalized(r[1] - r[0]), normalized(r[3] - r[0]))) ==
            Approx(0.0).margin(1e-12));
}

TEST_CASE("A clip shrinks the quad AND selects the matching UV region") {
    ImageData d = placed(40.0, 30.0);
    d.clipped = true;
    d.clip_u0 = 0.25;
    d.clip_u1 = 0.75;
    d.clip_v0 = 0.0;
    d.clip_v1 = 0.5; // the TOP half (v is measured down from the top-left)

    const ImageQuad q = resolve_image_quad(d);
    REQUIRE(length(q[1] - q[0]) == Approx(20.0)); // half the width
    REQUIRE(length(q[3] - q[0]) == Approx(15.0)); // half the height
    // v0..v1 = top half, so the visible band sits at the TOP of the placement.
    REQUIRE(q[0].y == Approx(35.0));
    REQUIRE(q[3].y == Approx(50.0));

    const std::array<double, 4> uv = resolve_image_uv(d);
    REQUIRE(uv[0] == Approx(0.25));
    REQUIRE(uv[1] == Approx(0.0));
    REQUIRE(uv[2] == Approx(0.75));
    REQUIRE(uv[3] == Approx(0.5));
}

TEST_CASE("A malformed clip is clamped and ordered, never inverting the quad") {
    ImageData d = placed(40.0, 30.0);
    d.clipped = true;
    d.clip_u0 = 1.4;  // out of range
    d.clip_u1 = -0.3; // and inverted
    d.clip_v0 = 0.8;
    d.clip_v1 = 0.2;
    const ImageQuad q = resolve_image_quad(d);
    REQUIRE(length(q[1] - q[0]) == Approx(40.0)); // clamped back to the full width
    REQUIRE(length(q[3] - q[0]) == Approx(0.6 * 30.0));
    // Still counter-clockwise, i.e. positive area -- an inverted quad would flip the
    // pick test's sign convention.
    const double area = (q[1].x - q[0].x) * (q[3].y - q[0].y);
    REQUIRE(area > 0.0);
}

TEST_CASE("Pick is point-in-quad and respects the clip") {
    ImageData d = placed(40.0, 30.0);
    REQUIRE(point_in_image(d, {30.0, 35.0}));
    REQUIRE_FALSE(point_in_image(d, {5.0, 35.0}));

    // Clip away the bottom half; a point there is no longer on the image.
    d.clipped = true;
    d.clip_v0 = 0.0;
    d.clip_v1 = 0.5; // keep the top half only
    REQUIRE(point_in_image(d, {30.0, 45.0}));
    REQUIRE_FALSE(point_in_image(d, {30.0, 25.0}));
}

TEST_CASE("Bounds and the kernel pick agree with the drawn quad") {
    GeometryStore store;
    NativeKernel2D kernel;
    const EntityHandle h = store.add_image(0, {10.0, 20.0}, 40.0, 30.0, 0.0);

    Vec2 lo{};
    Vec2 hi{};
    REQUIRE(entity_aabb(store, h, lo, hi));
    REQUIRE(lo.x == Approx(10.0));
    REQUIRE(lo.y == Approx(20.0));
    REQUIRE(hi.x == Approx(50.0));
    REQUIRE(hi.y == Approx(50.0));

    Vec2 cp{};
    REQUIRE(kernel.closest_point(store, h, {30.0, 35.0}, cp)); // inside -> distance 0
    REQUIRE(cp.x == Approx(30.0));
    REQUIRE(cp.y == Approx(35.0));
}

TEST_CASE("The image-definition table dedups by payload (the BLOCKDEF/INSERT shape)") {
    GeometryStore store;
    ImageDef a;
    a.source = "logo.png";
    ImageDef b;
    b.source = "logo.png"; // the same logo placed twice
    ImageDef c;
    c.source = "pictorial.png";

    const std::uint16_t ia = store.add_image_def(a);
    const std::uint16_t ib = store.add_image_def(b);
    const std::uint16_t ic = store.add_image_def(c);
    REQUIRE(ia == ib);   // one definition, one copy of the payload
    REQUIRE(ic != ia);
    REQUIRE(store.image_defs().size() == 2);
}

TEST_CASE("The snapshot carries transforms, NEVER pixels") {
    // This is the architectural constraint: the snapshot is copied through the triple
    // buffer on every publish, so megabytes of decoded raster here would wreck the
    // geometry->render handoff. ImageInstance must stay small and pixel-free.
    // 176 B: a quad (4 Vec2 = 64), UVs (16), def + version + handle, and three vector
    // headers for a polygonal clip (its boundary and pre-triangulated region -- a few
    // points, on the heap). The number itself matters less than the ceiling -- what must
    // never happen is pixels appearing here.
    static_assert(sizeof(ImageInstance) <= 192,
                  "ImageInstance must stay small -- it is copied through the triple buffer "
                  "on every publish, so pixels must never live here");
    GeometryStore store;
    NativeKernel2D kernel;
    ImageDef def;
    def.source = "logo.png";
    def.pixel_w = 512;
    def.pixel_h = 256;
    const std::uint16_t di = store.add_image_def(def);
    store.add_image(di, {0.0, 0.0}, 20.0, 10.0, 0.0);

    RenderSnapshot snap;
    // No decoder injected at all -- the geometry half must still work.
    REQUIRE(store.image_decoder() == nullptr);
    build_render_snapshot(store, kernel, snap, 0.1, 1.0);

    REQUIRE(snap.images.size() == 1);
    REQUIRE(snap.images[0].def == di);
    REQUIRE(snap.images[0].def_version == def.version);
    REQUIRE(snap.images[0].quad[2].x == Approx(20.0));
    // Bounds must include the image even though it emits no line/fill vertices.
    REQUIRE(snap.has_bounds);
    REQUIRE(snap.bounds_max.x == Approx(20.0));
    REQUIRE(snap.bounds_max.y == Approx(10.0));
}

TEST_CASE("resolve_image_path refuses to escape the drawing's directory") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "musacad_img_dir";
    const fs::path sub = dir / "assets";
    fs::create_directories(sub);
    { std::ofstream(dir / "logo.png") << "x"; }
    { std::ofstream(sub / "deep.png") << "x"; }

    std::string out;
    // Ordinary relative sources resolve.
    REQUIRE(resolve_image_path(dir.string(), "logo.png", out));
    REQUIRE(out == fs::weakly_canonical(dir / "logo.png").string());
    REQUIRE(resolve_image_path(dir.string(), "assets/deep.png", out));
    // A path that stays inside after traversing is fine.
    REQUIRE(resolve_image_path(dir.string(), "assets/../logo.png", out));

    // Escapes are refused -- a .musa is a document that can be mailed around, so a
    // source of "../../etc/passwd" must not be read just because a drawing asked.
    REQUIRE_FALSE(resolve_image_path(dir.string(), "../outside.png", out));
    REQUIRE_FALSE(resolve_image_path(dir.string(), "../../../../etc/passwd", out));
    REQUIRE_FALSE(resolve_image_path(dir.string(), "assets/../../escape.png", out));
    // Absolute paths are refused outright: a drawing must not name a machine path.
    REQUIRE_FALSE(resolve_image_path(dir.string(), "/etc/passwd", out));
    REQUIRE_FALSE(resolve_image_path(dir.string(), "", out));

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("v18 round-trips images losslessly, including an embedded base64 payload") {
    GeometryStore store;
    ImageDef embedded;
    embedded.source = "";
    // A payload with every byte value, so a base64 bug in any bit position shows up.
    embedded.bytes.resize(256);
    for (std::size_t i = 0; i < embedded.bytes.size(); ++i) {
        embedded.bytes[i] = static_cast<std::uint8_t>(i);
    }
    embedded.pixel_w = 16;
    embedded.pixel_h = 16;
    ImageDef external;
    external.source = "assets/company logo.png"; // a space, to prove the source line
    external.pixel_w = 512;
    external.pixel_h = 256;

    const std::uint16_t e0 = store.add_image_def(embedded);
    const std::uint16_t e1 = store.add_image_def(external);
    store.add_image(e0, {1.0, 2.0}, 30.0, 15.0, 0.25);
    const EntityHandle clipped = store.add_image(e1, {40.0, 5.0}, 20.0, 10.0, 0.0);
    if (ImageData* d = store.mutable_image(clipped)) {
        d->clipped = true;
        d->clip_u0 = 0.1;
        d->clip_v0 = 0.2;
        d->clip_u1 = 0.9;
        d->clip_v1 = 0.8;
    }

    const musacad::core::io::Document a = musacad::core::io::document_from_store(store);
    REQUIRE(a.format_version == musacad::core::io::kFormatVersion);
    REQUIRE(a.image_defs.size() == 2);
    REQUIRE(a.images.size() == 2);

    const auto path = (std::filesystem::temp_directory_path() / "musacad_image.musa").string();
    REQUIRE(musacad::core::io::save_native(a, path).ok);
    musacad::core::io::Document b;
    REQUIRE(musacad::core::io::load_native(path, b).ok);
    GeometryStore restored;
    musacad::core::io::populate_store(restored, b);
    REQUIRE(musacad::core::io::document_from_store(restored) == a);

    // Spot-check the payload byte-for-byte rather than trusting equality alone.
    REQUIRE(b.image_defs[0].bytes == embedded.bytes);
    REQUIRE(b.image_defs[1].source == "assets/company logo.png");
    REQUIRE(b.images[1].clipped);
    REQUIRE(b.images[1].clip_u1 == Approx(0.9));
    std::filesystem::remove(path);
}

TEST_CASE("A corrupt base64 payload fails the load rather than yielding a garbled image") {
    const std::string bad =
        "MUSACAD 18\nLAYER 255 255 255 0 25 1 0 0 0\n"
        "IMAGEDEF 4 4 1\n\n"
        "not!valid!base64!\n"
        "END\n";
    musacad::core::io::Document doc;
    REQUIRE_FALSE(musacad::core::io::parse_native(bad, doc).ok);
}

TEST_CASE("v17 files still load, with no images (older-version compatibility)") {
    const std::string v17 =
        "MUSACAD 17\nLAYER 255 255 255 0 25 1 0 0 0\n"
        "LINE 0 0 1 1 0 7 255 255 255 0 25\nEND\n";
    musacad::core::io::Document doc;
    REQUIRE(musacad::core::io::parse_native(v17, doc).ok);
    REQUIRE(doc.images.empty());
    REQUIRE(doc.image_defs.empty());
    REQUIRE(doc.lines.size() == 1);
}

TEST_CASE("Capture -> recreate preserves the placement and the clip") {
    GeometryStore store;
    const EntityHandle h = store.add_image(3, {7.0, 8.0}, 12.0, 6.0, 0.5);
    if (ImageData* d = store.mutable_image(h)) {
        d->clipped = true;
        d->clip_u0 = 0.2;
        d->clip_v1 = 0.7;
    }
    GeometryStore rebuilt;
    const EntityHandle h2 = add_command_to_store(rebuilt, capture_entity(store, h), EntityProps{});
    const ImageData* a = store.image(h);
    const ImageData* b = rebuilt.image(h2);
    REQUIRE(b != nullptr);
    REQUIRE(b->def == a->def);
    REQUIRE(b->pos == a->pos);
    REQUIRE(b->width == Approx(a->width));
    REQUIRE(b->rotation == Approx(a->rotation));
    REQUIRE(b->clipped);
    REQUIRE(b->clip_u0 == Approx(0.2));
    REQUIRE(b->clip_v1 == Approx(0.7));
}

TEST_CASE("Grips: insertion moves, the opposite corner scales in the image's own frame") {
    GeometryStore store;
    const EntityHandle h = store.add_image(0, {0.0, 0.0}, 10.0, 5.0, 0.0);
    std::vector<Grip> grips;
    grips_of(store, h, grips);
    REQUIRE(grips.size() == 2);

    GeometryStore moved;
    const EntityHandle m =
        add_command_to_store(moved, edit_for_grip_drag(store, h, 0, {3.0, 4.0}), EntityProps{});
    REQUIRE(moved.image(m)->pos == Vec2{3.0, 4.0});

    GeometryStore scaled;
    const EntityHandle sc =
        add_command_to_store(scaled, edit_for_grip_drag(store, h, 1, {20.0, 12.0}), EntityProps{});
    REQUIRE(scaled.image(sc)->width == Approx(20.0));
    REQUIRE(scaled.image(sc)->height == Approx(12.0));
    REQUIRE(scaled.image(sc)->pos == Vec2{0.0, 0.0}); // the insertion point did not move
}

TEST_CASE("Struct sizes: images live in a cold arena, hot structs untouched") {
    static_assert(sizeof(LineData) == 40, "hot struct: LineData must stay 40 B");
    static_assert(sizeof(CircleData) == 32, "hot struct: CircleData must stay 32 B");
    static_assert(sizeof(EntityProps) == 8, "hot struct: EntityProps must stay 8 B");
    static_assert(sizeof(ImageData) == 104, "ImageData size changed -- update the docs too");
    SUCCEED();
}

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// IMAGECLIP polygonal boundaries (#10): the store's polygon, the derived triangles and
// outline, picking inside the polygon, the native form, and the command's point loop.

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/image.hpp"
#include "musacad/core/image_decoder.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/scene_snapshot.hpp"

using namespace musacad::core;
using namespace musacad::core::io;

namespace {
struct StubDecoder final : IImageDecoder {
    DecodedImage decode_file(std::string_view) const override { return make(); }
    DecodedImage decode_bytes(const std::vector<std::uint8_t>&) const override { return make(); }
    static DecodedImage make() {
        DecodedImage d;
        d.width = 4;
        d.height = 2;
        d.rgba.assign(4 * 2 * 4, 0xFF);
        return d;
    }
};
struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompts.push_back(p); }
    std::vector<std::string> lines;
    std::vector<std::string> prompts;
};
struct StubView : musacad::command::ViewControl {
    void zoom_extents() override {}
    void zoom_scale(double) override {}
};
struct ProcHarness {
    std::vector<Command> cmds;
    SilentOutput out;
    StubView view;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, &view, out};
    template <class T>
    const T* last() const {
        const T* found = nullptr;
        for (const Command& c : cmds) {
            if (const auto* p = std::get_if<T>(&c)) {
                found = p;
            }
        }
        return found;
    }
};
template <class Pred>
bool wait_until(GeometryEngine& e, Pred pred) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        e.consume_snapshot();
        if (pred(e.snapshot())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    e.consume_snapshot();
    return pred(e.snapshot());
}
std::string temp_image() {
    const auto dir = std::filesystem::temp_directory_path() / "musacad_polyclip_tests";
    std::filesystem::create_directories(dir);
    const std::string p = (dir / "pic.png").string();
    std::ofstream(p) << "not really a png";
    return p;
}
} // namespace

TEST_CASE("#10 polygonal clip in the store: outline, triangles, picking, bounds box") {
    GeometryStore store;
    NativeKernel2D kernel;
    ImageDef def;
    def.pixel_w = 4;
    def.pixel_h = 2;
    const std::uint16_t di = store.add_image_def(def);
    // A 40 x 20 image from (0, 0); a triangle clip: bottom-left, bottom-right, top-centre.
    const EntityHandle h = store.add_image(di, {0, 0}, 40.0, 20.0, 0.0);
    REQUIRE(store.set_image_clip_polygon(h, {{0, 1}, {1, 1}, {0.5, 0}}));
    const ImageData* im = store.image(h);
    CHECK(store.image_clip_polygon(*im).size() == 3);
    CHECK(im->clipped);
    CHECK(im->clip_u0 == 0.0);
    CHECK(im->clip_v0 == 0.0);
    CHECK(im->clip_u1 == 1.0);
    const std::vector<Vec2> world = image_uv_to_world(*im, store.image_clip_polygon(*im));
    REQUIRE(world.size() == 3);
    CHECK(std::abs(world[2].x - 20.0) < 1e-9); // the apex at the top centre
    CHECK(std::abs(world[2].y - 20.0) < 1e-9);
    CHECK(point_in_polygon(world, {20, 10}));
    CHECK_FALSE(point_in_polygon(world, {2, 18}));
    Vec2 cp;
    CHECK(kernel.closest_point(store, h, {20, 10}, cp)); // inside the triangle: distance 0
    CHECK(cp == Vec2{20, 10});
    CHECK(kernel.closest_point(store, h, {2, 18}, cp));  // outside: snaps to an edge
    CHECK(std::hypot(cp.x - 2.0, cp.y - 18.0) > 1e-6);

    RenderSnapshot snap;
    build_render_snapshot(store, kernel, snap, 0.01, 1.0);
    REQUIRE(snap.images.size() == 1);
    CHECK(snap.images[0].clip_world.size() == 3);
    // The region as triangles (however the triangulator splits it): their area is the
    // polygon's, 40 x 20 / 2, in world units; the uv triangles are the same count.
    const auto& tw = snap.images[0].tri_world;
    REQUIRE(tw.size() % 3 == 0);
    REQUIRE(tw.size() >= 3);
    CHECK(snap.images[0].tri_uv.size() == tw.size());
    double area = 0.0;
    for (std::size_t i = 0; i + 2 < tw.size(); i += 3) {
        area += std::abs((tw[i + 1].x - tw[i].x) * (tw[i + 2].y - tw[i].y) -
                         (tw[i + 2].x - tw[i].x) * (tw[i + 1].y - tw[i].y)) * 0.5;
    }
    CHECK(std::abs(area - 400.0) < 1e-6);
    CHECK(snap.line_vertices.size() == 6); // the frame follows the polygon
    // Clearing the polygon leaves the rectangle.
    REQUIRE(store.set_image_clip_polygon(h, {}));
    CHECK(store.image_clip_polygon(*store.image(h)).empty());
}

TEST_CASE("#10 IMAGECLIP Polygonal through the engine and the command; native v33 keeps it") {
    const StubDecoder decoder;
    GeometryEngine engine;
    engine.set_image_decoder(&decoder);
    engine.start();
    engine.submit(AttachImageCommand{temp_image(), true, {0, 0}, 10.0, 0.0, 1}); // 40 x 20
    REQUIRE(wait_until(engine, [](const auto& s) { return s.images.size() == 1; }));
    engine.submit(SetImagePolyClipCommand{{20, 10}, 1.0, {{0, 0}, {40, 0}, {20, 20}}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.images.size() == 1 && !s.images[0].clip_world.empty(); }));
    CHECK(engine.snapshot().images[0].clip_world.size() == 3);
    // A rectangular boundary replaces it; Delete clears it.
    engine.submit(SetImageClipCommand{{20, 5}, 1.0, SetImageClipCommand::Mode::NewRect, {5, 2}, {35, 18}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.images.size() == 1 && s.images[0].clip_world.empty() && s.images[0].uv[0] > 0.1; }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.images.size() == 1 && s.images[0].clip_world.size() == 3; }));
    engine.stop();

    Document doc;
    DocImageDef d;
    d.pixel_w = 4;
    d.pixel_h = 2;
    doc.image_defs.push_back(d);
    DocImage im;
    im.width = 40;
    im.height = 20;
    im.clipped = true;
    im.clip_polygon = {{0, 1}, {1, 1}, {0.5, 0}};
    doc.images.push_back(im);
    Document rt;
    REQUIRE(parse_native(serialize_native(doc), rt).ok);
    REQUIRE(rt.images.size() == 1);
    CHECK(rt.images[0].clip_polygon == im.clip_polygon);
    GeometryStore store;
    populate_store(store, rt);
    const Document again = document_from_store(store);
    CHECK(again.images[0].clip_polygon == im.clip_polygon);

    ProcHarness h;
    h.proc.submit_line("IMAGECLIP");
    h.proc.submit_line("3,3");
    h.proc.submit_line("N");
    h.proc.submit_line("P");
    h.proc.submit_line("0,0");
    h.proc.submit_line("40,0");
    h.proc.submit_line("30,20");
    h.proc.submit_line("U");     // drop the last point
    h.proc.submit_line("20,20");
    h.proc.submit_line("");      // close
    const auto* c = h.last<SetImagePolyClipCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->points.size() == 3);
    CHECK(c->points[2] == Vec2{20, 20});
    CHECK(c->pick == Vec2{3, 3});
}

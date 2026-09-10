// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// IMAGEATTACH / IMAGECLIP / IMAGEFRAME (#10): the engine's attach path (embed vs. path,
// the size limit, pixel-size placement), the renderer-facing definition table in the
// snapshot, rectangular clipping, and the command flows.

#include <chrono>
#include <cmath>
#include <cstdio>
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
#include "musacad/core/image_decoder.hpp"

using namespace musacad::core;

namespace {
/// A decoder that "decodes" any input as a 4 x 2 image (the tests need no image codec).
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
    std::string image_file_dialog() override { return "/tmp/from-dialog.png"; }
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
std::filesystem::path temp_dir() {
    const auto dir = std::filesystem::temp_directory_path() / "musacad_image_tests";
    std::filesystem::create_directories(dir);
    return dir;
}
std::string write_file(const std::filesystem::path& p, std::size_t bytes) {
    std::ofstream out(p, std::ios::binary);
    const std::string chunk(1024, 'x');
    for (std::size_t written = 0; written < bytes; written += chunk.size()) {
        out.write(chunk.data(), static_cast<std::streamsize>(std::min(chunk.size(), bytes - written)));
    }
    return p.string();
}
} // namespace

TEST_CASE("#10 IMAGEATTACH: embed, place at pixel size, publish the definition; IMAGEFRAME; the embed limit") {
    const StubDecoder decoder;
    GeometryEngine engine;
    engine.set_image_decoder(&decoder);
    engine.start();
    const std::string small = write_file(temp_dir() / "logo.png", 100);
    engine.submit(AttachImageCommand{small, true, {10, 20}, 2.5, 0.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.images.size() == 1; }));
    {
        const RenderSnapshot& s = engine.snapshot();
        REQUIRE(s.image_defs.size() == 1);
        CHECK(s.image_defs[0].bytes != nullptr);   // embedded payload, shared by pointer
        CHECK(s.image_defs[0].bytes->size() == 100);
        CHECK(s.image_defs[0].source.empty());
        // 4 x 2 pixels at scale 2.5 -> 10 x 5 units from (10, 20).
        CHECK(std::abs(s.images[0].quad[0].x - 10.0) < 1e-9);
        CHECK(std::abs(s.images[0].quad[1].x - 20.0) < 1e-9);
        CHECK(std::abs(s.images[0].quad[2].y - 25.0) < 1e-9);
        CHECK(s.image_frame == 1);
        CHECK(s.line_vertices.size() == 8); // the frame
    }
    engine.submit(SetImageFrameCommand{0});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.image_frame == 0 && s.line_vertices.empty(); }));
    engine.submit(SetImageFrameCommand{1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));

    // An unsaved drawing cannot reference by path: the image is embedded instead.
    engine.submit(AttachImageCommand{small, false, {0, 0}, 1.0, 0.0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.images.size() == 2; }));
    CHECK(engine.snapshot().image_defs.size() == 1); // same bytes -> the same definition

    // Over the limit: refused, nothing added.
    const std::string big = write_file(temp_dir() / "huge.png", kMaxEmbeddedImageBytes + 1);
    engine.submit(AttachImageCommand{big, true, {0, 0}, 1.0, 0.0, 3});
    engine.submit(AddLineCommand{{100, 0}, {110, 0}, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 18; }));
    CHECK(engine.snapshot().images.size() == 2);
    // A missing file: refused.
    engine.submit(AttachImageCommand{(temp_dir() / "missing.png").string(), true, {0, 0}, 1.0, 0.0, 5});
    engine.submit(AddLineCommand{{100, 10}, {110, 10}, 6});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 20; }));
    CHECK(engine.snapshot().images.size() == 2);
    // Undo removes the last placed image.
    engine.submit(UndoLastGroupCommand{});
    engine.submit(UndoLastGroupCommand{});
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.images.size() == 1; }));
    engine.stop();
}

TEST_CASE("#10 IMAGECLIP: a rectangular boundary in image fractions through the placement; Delete / OFF / ON") {
    const StubDecoder decoder;
    GeometryEngine engine;
    engine.set_image_decoder(&decoder);
    engine.start();
    const std::string small = write_file(temp_dir() / "logo2.png", 64);
    // 4 x 2 px at scale 10 -> a 40 x 20 image from (0, 0).
    engine.submit(AttachImageCommand{small, true, {0, 0}, 10.0, 0.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.images.size() == 1; }));
    engine.submit(SetImageClipCommand{{20, 10}, 1.0, SetImageClipCommand::Mode::NewRect, {10, 5}, {30, 20}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.images.size() == 1 && s.images[0].uv[0] > 0.2; }));
    {
        const ImageInstance& im = engine.snapshot().images[0];
        CHECK(std::abs(im.uv[0] - 0.25) < 1e-6); // u0 = 10/40
        CHECK(std::abs(im.uv[1] - 0.0) < 1e-6);  // v0: the top (y = 20)
        CHECK(std::abs(im.uv[2] - 0.75) < 1e-6); // u1 = 30/40
        CHECK(std::abs(im.uv[3] - 0.75) < 1e-6); // v1 = 1 - 5/20
        CHECK(std::abs(im.quad[0].x - 10.0) < 1e-6);
        CHECK(std::abs(im.quad[0].y - 5.0) < 1e-6);
        CHECK(std::abs(im.quad[2].x - 30.0) < 1e-6);
        CHECK(std::abs(im.quad[2].y - 20.0) < 1e-6);
    }
    engine.submit(SetImageClipCommand{{20, 10}, 1.0, SetImageClipCommand::Mode::Off, {}, {}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.images.size() == 1 && s.images[0].uv[0] == 0.0f; }));
    engine.submit(SetImageClipCommand{{20, 10}, 1.0, SetImageClipCommand::Mode::On, {}, {}, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.images.size() == 1 && s.images[0].uv[0] > 0.2; }));
    engine.submit(SetImageClipCommand{{20, 10}, 1.0, SetImageClipCommand::Mode::Delete, {}, {}, 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.images.size() == 1 && s.images[0].uv[0] == 0.0f; }));
    engine.submit(SetImageClipCommand{{20, 10}, 1.0, SetImageClipCommand::Mode::On, {}, {}, 6});
    engine.submit(AddLineCommand{{100, 0}, {110, 0}, 7});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 10; }));
    CHECK(engine.snapshot().images[0].uv[2] == 1.0f); // Delete dropped the boundary for good
    // Undo the whole clip history: the original unclipped image.
    for (int i = 0; i < 6; ++i) {
        engine.submit(UndoLastGroupCommand{});
    }
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.images.size() == 1 && s.images[0].uv[0] == 0.0f && s.images[0].uv[2] == 1.0f &&
               s.line_vertices.size() == 8;
    }));
    engine.stop();
}

TEST_CASE("#10 commands: IMAGEATTACH (typed path and the dialog), IMAGECLIP options, IMAGEFRAME") {
    ProcHarness h;
    h.proc.submit_line("IMAGEATTACH");
    h.proc.submit_line("/tmp/plan.png");
    h.proc.submit_line("Y");
    h.proc.submit_line("5,6");
    h.proc.submit_line("2");
    h.proc.submit_line("90");
    const auto* a = h.last<AttachImageCommand>();
    REQUIRE(a != nullptr);
    CHECK(a->path == "/tmp/plan.png");
    CHECK(a->embed);
    CHECK(a->pos == Vec2{5, 6});
    CHECK(a->scale == 2.0);
    CHECK(std::abs(a->rotation - kPi / 2.0) < 1e-9);

    h.proc.submit_line("IAT");
    h.proc.submit_line("~"); // the file dialog
    h.proc.submit_line("");  // reference, do not embed
    h.proc.submit_line("");  // 0,0
    h.proc.submit_line("");  // scale 1
    h.proc.submit_line("");  // rotation 0
    const auto* b = h.last<AttachImageCommand>();
    REQUIRE(b != nullptr);
    CHECK(b->path == "/tmp/from-dialog.png");
    CHECK_FALSE(b->embed);
    CHECK(b->pos == Vec2{0, 0});

    h.proc.submit_line("IMAGECLIP");
    h.proc.submit_line("3,3");
    h.proc.submit_line("");  // New boundary
    h.proc.submit_line("P"); // polygonal: not available, asks again
    h.proc.submit_line("R");
    h.proc.submit_line("1,1");
    h.proc.submit_line("4,2");
    const auto* c = h.last<SetImageClipCommand>();
    REQUIRE(c != nullptr);
    CHECK(c->mode == SetImageClipCommand::Mode::NewRect);
    CHECK(c->pick == Vec2{3, 3});
    CHECK(c->a == Vec2{1, 1});
    CHECK(c->b == Vec2{4, 2});
    h.proc.submit_line("ICL");
    h.proc.submit_line("3,3");
    h.proc.submit_line("OFF");
    CHECK(h.last<SetImageClipCommand>()->mode == SetImageClipCommand::Mode::Off);
    h.proc.submit_line("ICL");
    h.proc.submit_line("3,3");
    h.proc.submit_line("D");
    CHECK(h.last<SetImageClipCommand>()->mode == SetImageClipCommand::Mode::Delete);

    h.proc.submit_line("IMAGEFRAME");
    h.proc.submit_line("2");
    const auto* f = h.last<SetImageFrameCommand>();
    REQUIRE(f != nullptr);
    CHECK(f->mode == 2);
}

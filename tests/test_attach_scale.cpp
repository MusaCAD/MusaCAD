// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// An attached image's size as AutoCAD gives it (one unit wide at scale 1, or its true size
// from the resolution the file records in a drawing with a unit), -IMAGEATTACH's base size,
// dragged scale and angle; the image's corner grips keeping its proportions; -INSERT's
// insertion-point options, opposite corner, Corner and XYZ, and its previews.

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/image.hpp"
#include "musacad/core/image_decoder.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {
// Big-endian bytes, for PNG and JPEG headers.
void be(std::vector<std::uint8_t>& b, std::uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i) {
        b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}
void chunk(std::vector<std::uint8_t>& b, const char* type, const std::vector<std::uint8_t>& data) {
    be(b, static_cast<std::uint32_t>(data.size()), 4);
    b.insert(b.end(), type, type + 4);
    b.insert(b.end(), data.begin(), data.end());
    be(b, 0, 4); // the CRC is not checked here
}
// A PNG header: IHDR w x h, and pHYs `ppm` pixels per metre when not 0.
std::vector<std::uint8_t> png(std::uint32_t w, std::uint32_t h, std::uint32_t ppm) {
    std::vector<std::uint8_t> b = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    be(ihdr, w, 4);
    be(ihdr, h, 4);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(b, "IHDR", ihdr);
    if (ppm != 0) {
        std::vector<std::uint8_t> phys;
        be(phys, ppm, 4);
        be(phys, ppm, 4);
        phys.push_back(1); // per metre
        chunk(b, "pHYs", phys);
    }
    chunk(b, "IDAT", {0});
    return b;
}

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
    return false;
}

std::string write_bytes(const char* name, const std::vector<std::uint8_t>& b) {
    const auto dir = std::filesystem::temp_directory_path() / "musacad_attach_scale";
    std::filesystem::create_directories(dir);
    const std::string p = (dir / name).string();
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return p;
}

struct Out : musacad::command::CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
};

struct H {
    std::vector<Command> cmds;
    Out out;
    musacad::command::CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    template <class T>
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
    void run(std::initializer_list<const char*> lines) {
        for (const char* l : lines) {
            proc.submit_line(l);
        }
    }
};
} // namespace

TEST_CASE("A raster file's resolution and pixel size from its header; AutoCAD's base size of an image") {
    // PNG: 11811 pixels per metre is 300 dots per inch.
    const std::vector<std::uint8_t> p300 = png(3000, 1500, 11811);
    const auto dpi = image_resolution(p300);
    REQUIRE(dpi);
    REQUIRE(dpi->x == Approx(300.0).epsilon(1e-3));
    const auto px = image_pixel_size(p300);
    REQUIRE(px);
    REQUIRE((*px)[0] == 3000);
    REQUIRE((*px)[1] == 1500);
    REQUIRE_FALSE(image_resolution(png(10, 10, 0))); // no pHYs: no resolution

    // JPEG: JFIF density in dots per inch, and the size from SOF0.
    std::vector<std::uint8_t> jpg = {0xFF, 0xD8, 0xFF, 0xE0};
    be(jpg, 16, 2);
    jpg.insert(jpg.end(), {'J', 'F', 'I', 'F', 0, 1, 2, 1});
    be(jpg, 200, 2);
    be(jpg, 200, 2);
    jpg.insert(jpg.end(), {0, 0});
    jpg.insert(jpg.end(), {0xFF, 0xC0});
    be(jpg, 11, 2);
    jpg.push_back(8);
    be(jpg, 600, 2); // height
    be(jpg, 800, 2); // width
    jpg.insert(jpg.end(), {1, 1, 0x11, 0});
    REQUIRE(image_resolution(jpg)->x == Approx(200.0));
    REQUIRE((*image_pixel_size(jpg))[0] == 800);
    REQUIRE((*image_pixel_size(jpg))[1] == 600);

    // BMP: pixels per metre in the info header.
    std::vector<std::uint8_t> bmp(54, 0);
    bmp[0] = 'B';
    bmp[1] = 'M';
    bmp[18] = 64;   // width 64
    bmp[22] = 32;   // height 32
    bmp[38] = 0xC4; // 3780 per metre (96 dpi), little-endian
    bmp[39] = 0x0E;
    REQUIRE(image_resolution(bmp)->x == Approx(96.0).epsilon(1e-3));
    REQUIRE((*image_pixel_size(bmp))[0] == 64);

    // The base size: its true size in a drawing with a unit when the file says its resolution.
    const Vec2 mm = image_base_size(3000, 1500, Vec2{300, 300}, 4);
    REQUIRE(mm.x == Approx(254.0));
    REQUIRE(mm.y == Approx(127.0));
    const Vec2 inch = image_base_size(3000, 1500, Vec2{300, 300}, 1);
    REQUIRE(inch.x == Approx(10.0));
    // Unitless, or no resolution: one unit wide, so the scale factor is the width.
    REQUIRE(image_base_size(3000, 1500, Vec2{300, 300}, 0) == Vec2{1.0, 0.5});
    REQUIRE(image_base_size(3000, 1500, std::nullopt, 4) == Vec2{1.0, 0.5});
}

TEST_CASE("IMAGEATTACH: an image at its true size in millimetres, or one unit wide") {
    const StubDecoder decoder; // decodes everything as 4 x 2 pixels
    GeometryEngine engine;
    engine.set_image_decoder(&decoder);
    engine.start();
    // 1000 pixels per metre = 25.4 dots per inch: one pixel a millimetre.
    const std::string file = write_bytes("mm.png", png(4, 2, 1000));
    DrawingUnits u;
    u.insunits = 4;
    engine.submit(SetUnitsCommand{u});
    engine.submit(AttachImageCommand{file, true, {0, 0}, 1.0, 0.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.images.size() == 1; }));
    REQUIRE(engine.snapshot().images[0].quad[2].x == Approx(4.0));
    REQUIRE(engine.snapshot().images[0].quad[2].y == Approx(2.0));
    // Unitless: one unit wide, times the scale.
    u.insunits = 0;
    engine.submit(SetUnitsCommand{u});
    engine.submit(AttachImageCommand{file, true, {10, 0}, 3.0, 0.0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.images.size() == 2; }));
    REQUIRE(engine.snapshot().images[1].quad[2].x == Approx(13.0));
    REQUIRE(engine.snapshot().images[1].quad[2].y == Approx(1.5));
    engine.stop();
}

TEST_CASE("-IMAGEATTACH: the base image size, the scale and angle dragged from the insertion point") {
    const std::string file = write_bytes("cmd.png", png(4, 2, 1000));
    H h;
    DrawingUnits u;
    u.insunits = 4;
    h.proc.set_units(u);
    h.run({"-IMAGEATTACH", file.c_str(), "N"});
    REQUIRE(h.out.prompt == "Specify insertion point <0,0>: ");
    REQUIRE(h.proc.preview().kind == musacad::command::PreviewKind::Outline); // the frame at the cursor
    h.run({"0,0"});
    REQUIRE(h.out.lines.back() == "Base image size: Width: 4.000000, Height: 2.000000, Millimeters");
    REQUIRE(h.out.prompt == "Specify scale factor <1>: ");
    REQUIRE(h.proc.preview().outline_anchor == std::optional<Vec2>(Vec2{0, 0}));
    h.run({"3,0"}); // three units away: a factor of 3
    REQUIRE(h.out.prompt == "Specify rotation angle <0>: ");
    REQUIRE(h.proc.preview().outline_rotate);
    h.run({"0,5"}); // straight up: 90 degrees
    const auto* a = h.last<AttachImageCommand>();
    REQUIRE(a != nullptr);
    REQUIRE(a->scale == Approx(3.0));
    REQUIRE(a->rotation == Approx(kHalfPi));
}

TEST_CASE("An image's corner grips scale it about the opposite corner, its proportions kept") {
    GeometryStore store;
    const EntityHandle h = store.add_image(0, {0, 0}, 10.0, 5.0, 0.0);
    std::vector<Grip> grips;
    grips_of(store, h, grips);
    REQUIRE(grips.size() == 4);
    REQUIRE(grips[2].pos == Vec2{10, 5});
    // The top-right corner out along the diagonal: twice as large about the bottom-left one.
    const Command edited = edit_for_grip_drag(store, h, 2, {20, 10});
    const auto* e = std::get_if<AddImageCommand>(&edited);
    REQUIRE(e != nullptr);
    REQUIRE(e->width == Approx(20.0));
    REQUIRE(e->height == Approx(10.0)); // the proportions kept
    REQUIRE(e->pos == Vec2{0, 0});
    // The bottom-left corner: about the top-right one.
    const Command edited2 = edit_for_grip_drag(store, h, 0, {-10, -5});
    const auto* e2 = std::get_if<AddImageCommand>(&edited2);
    REQUIRE(e2->width == Approx(20.0));
    REQUIRE(e2->pos.x == Approx(-10.0));
    REQUIRE(e2->pos.y == Approx(-5.0));
}

TEST_CASE("-INSERT: Scale, X, Y and Rotate before the point; the opposite corner, Corner and XYZ") {
    H h;
    h.proc.set_block_names({"B"});
    h.proc.set_block_previews(std::make_shared<const std::vector<std::vector<Vec2>>>(
        std::vector<std::vector<Vec2>>{{{0, 0}, {1, 0}, {1, 0}, {1, 1}}}));
    h.run({"-INSERT", "B"});
    REQUIRE(h.out.prompt == "Specify insertion point or [Scale/X/Y/Z/Rotate]: ");
    REQUIRE(h.proc.preview().kind == musacad::command::PreviewKind::Outline); // the block at the cursor
    REQUIRE(h.proc.preview().points.size() == 4);
    h.run({"S"});
    REQUIRE(h.out.prompt == "Specify scale factor for XYZ axes <1>: ");
    h.run({"2", "R", "90", "5,5"}); // both given before the point: neither asked after it
    const auto* a = h.last<InsertBlockCommand>();
    REQUIRE(a != nullptr);
    REQUIRE(a->pos == Vec2{5, 5});
    REQUIRE(a->scale_x == Approx(2.0));
    REQUIRE(a->scale_y == Approx(2.0));
    REQUIRE(a->rotation == Approx(kHalfPi));

    h.run({"-INSERT", "B", "0,0"});
    REQUIRE(h.out.prompt == "Enter X scale factor, specify opposite corner, or [Corner/XYZ] <1>: ");
    REQUIRE(h.proc.preview().outline_corner); // sized by the corner
    h.run({"3,2"}); // the opposite corner: X 3, Y 2
    REQUIRE(h.out.prompt == "Specify rotation angle <0>: ");
    h.run({""});
    REQUIRE(h.last<InsertBlockCommand>()->scale_x == Approx(3.0));
    REQUIRE(h.last<InsertBlockCommand>()->scale_y == Approx(2.0));

    h.run({"-INSERT", "B", "0,0", "C"});
    REQUIRE(h.out.prompt == "Specify opposite corner: ");
    h.run({"-2,4", ""});
    REQUIRE(h.last<InsertBlockCommand>()->scale_x == Approx(-2.0)); // mirrored, as AutoCAD's corner does
    REQUIRE(h.last<InsertBlockCommand>()->scale_y == Approx(4.0));

    h.run({"-INSERT", "B", "0,0", "XYZ"});
    REQUIRE(h.out.prompt == "Specify X scale factor or [Corner] <1>: ");
    h.run({"2"});
    REQUIRE(h.out.prompt == "Specify Y scale factor or <use X scale factor>: ");
    h.run({"3"});
    REQUIRE(h.out.prompt == "Specify Z scale factor or <use X scale factor>: ");
    h.run({"4", ""});
    REQUIRE(h.last<InsertBlockCommand>()->scale_x == Approx(2.0));
    REQUIRE(h.last<InsertBlockCommand>()->scale_y == Approx(3.0));

    h.run({"-INSERT", "B", "X", "5", "0,0"});
    REQUIRE(h.out.prompt == "Specify rotation angle <0>: "); // the scale given: not asked again
    h.run({"0"});
    REQUIRE(h.last<InsertBlockCommand>()->scale_x == Approx(5.0));
    REQUIRE(h.last<InsertBlockCommand>()->scale_y == Approx(1.0));
}

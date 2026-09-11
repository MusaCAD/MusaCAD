// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// DXF IMAGE / IMAGEDEF (#31): the writer's handles, CLASSES and OBJECTS web, a placed
// image both ways (placement, rotation, rectangular and polygonal clips), sidecar files
// for embedded images on save, and absolute references pulled in on load.

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"

using namespace musacad::core;
using namespace musacad::core::io;

namespace {
std::filesystem::path temp_dir() {
    const auto dir = std::filesystem::temp_directory_path() / "musacad_dxf_image_tests";
    std::filesystem::create_directories(dir);
    return dir;
}
int count(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (std::size_t pos = hay.find(needle); pos != std::string::npos; pos = hay.find(needle, pos + needle.size())) {
        ++n;
    }
    return n;
}
} // namespace

TEST_CASE("#31 DXF writer: handles on every record, $HANDSEED, sections in order, CLASSES and OBJECTS for images") {
    Document doc;
    doc.lines.push_back(DocLine{{0, 0}, {10, 0}});
    DocBlockDef b;
    b.name = "SYM";
    b.circles.push_back(DocCircle{{0, 0}, 2.0});
    doc.block_defs.push_back(b);
    const std::string plain = serialize_dxf(doc);
    CHECK(plain.find("$HANDSEED") != std::string::npos);
    CHECK(plain.find("\nCLASSES\n") == std::string::npos);  // no images: no classes
    CHECK(plain.find("\nOBJECTS\n") == std::string::npos);
    CHECK(plain.find("BLOCK_RECORD") != std::string::npos);
    CHECK(plain.find("*Model_Space") != std::string::npos);
    CHECK(plain.find("\nBLOCKS\n") < plain.find("\nENTITIES\n")); // blocks before entities
    // Every entity, block and table record carries a handle.
    CHECK(count(plain, "\n5\n") >= 12);
    Document rt;
    REQUIRE(parse_dxf(plain, rt).ok);
    CHECK(rt.lines.size() == 1);
    CHECK(rt.block_defs.size() == 1);

    DocImageDef def;
    def.source = "logo.png";
    def.pixel_w = 200;
    def.pixel_h = 100;
    doc.image_defs.push_back(def);
    DocImage im;
    im.def = 0;
    im.pos = {5, 6};
    im.width = 40;
    im.height = 20;
    im.rotation = 0.5;
    im.clipped = true;
    im.clip_u0 = 0.25;
    im.clip_v0 = 0.1;
    im.clip_u1 = 0.75;
    im.clip_v1 = 0.9;
    doc.images.push_back(im);
    DocImage poly = im;
    poly.pos = {100, 0};
    poly.rotation = 0.0;
    poly.clip_polygon = {{0, 1}, {1, 1}, {0.5, 0}};
    doc.images.push_back(poly);
    const std::string dxf = serialize_dxf(doc);
    CHECK(dxf.find("\nCLASSES\n") != std::string::npos);
    CHECK(dxf.find("AcDbRasterImage\n") != std::string::npos);
    CHECK(dxf.find("\nOBJECTS\n") != std::string::npos);
    CHECK(dxf.find("ACAD_IMAGE_DICT") != std::string::npos);
    CHECK(count(dxf, "\n0\nIMAGEDEF_REACTOR\n") == 2); // the objects (the CLASSES entry aside)
    CHECK(count(dxf, "\n0\nIMAGE\n") == 2);            // the entities
    CHECK(dxf.find("\nOBJECTS\n") > dxf.find("\nENTITIES\n"));

    Document back;
    const IoResult r = parse_dxf(dxf, back);
    REQUIRE(r.ok);
    REQUIRE(back.image_defs.size() == 1);
    CHECK(back.image_defs[0].source == "logo.png");
    CHECK(back.image_defs[0].pixel_w == 200);
    REQUIRE(back.images.size() == 2);
    CHECK(std::abs(back.images[0].width - 40.0) < 1e-6);
    CHECK(std::abs(back.images[0].height - 20.0) < 1e-6);
    CHECK(std::abs(back.images[0].rotation - 0.5) < 1e-9);
    CHECK(back.images[0].pos == Vec2{5, 6});
    CHECK(back.images[0].clipped);
    CHECK(std::abs(back.images[0].clip_u0 - 0.25) < 1e-9);
    CHECK(std::abs(back.images[0].clip_v1 - 0.9) < 1e-9);
    CHECK(back.images[0].clip_polygon.empty());
    REQUIRE(back.images[1].clip_polygon.size() == 3);
    CHECK(std::abs(back.images[1].clip_polygon[2].x - 0.5) < 1e-9);
    CHECK(r.message.find("IMAGE") == std::string::npos); // nothing skipped
}

TEST_CASE("#31 DXF files: an embedded image is written beside the file; an absolute reference is read in") {
    const auto dir = temp_dir();
    Document doc;
    DocImageDef def;
    def.bytes = {0x89, 'P', 'N', 'G', 1, 2, 3, 4};
    def.pixel_w = 2;
    def.pixel_h = 2;
    doc.image_defs.push_back(def);
    DocImage im;
    im.width = 2;
    im.height = 2;
    doc.images.push_back(im);
    const std::string path = (dir / "plan.dxf").string();
    REQUIRE(save_dxf(doc, path).ok);
    REQUIRE(std::filesystem::exists(dir / "plan-image1.png"));
    Document back;
    REQUIRE(load_dxf(path, back).ok);
    REQUIRE(back.image_defs.size() == 1);
    CHECK(back.image_defs[0].source == "plan-image1.png"); // inside the folder: a relative reference
    CHECK(back.image_defs[0].bytes.empty());

    // A reference to a file outside the folder comes in as bytes.
    const auto elsewhere = std::filesystem::temp_directory_path() / "musacad_dxf_image_outside.png";
    std::ofstream(elsewhere, std::ios::binary) << "PNGDATA";
    Document ext;
    DocImageDef edef;
    edef.source = elsewhere.string();
    edef.pixel_w = 1;
    edef.pixel_h = 1;
    ext.image_defs.push_back(edef);
    ext.images.push_back(DocImage{});
    const std::string ext_path = (dir / "ext.dxf").string();
    {
        std::ofstream(ext_path) << serialize_dxf(ext);
    }
    Document loaded;
    REQUIRE(load_dxf(ext_path, loaded).ok);
    REQUIRE(loaded.image_defs.size() == 1);
    CHECK(loaded.image_defs[0].source.empty());
    CHECK(loaded.image_defs[0].bytes.size() == 7);
}

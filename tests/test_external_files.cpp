// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Sandboxed file access (Flatpak document portal): spotting a drawing that loads other
// files by path, and spotting a single-file portal grant.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/external_files.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad::core;
using namespace musacad::core::io;

namespace {
Document with_xref() {
    Document doc;
    DocBlockDef b;
    b.name = "SITE";
    b.xref_path = "refs/site plan.musa";
    doc.block_defs.push_back(b);
    return doc;
}
Document with_image(bool embedded) {
    Document doc;
    DocImageDef def;
    if (embedded) {
        def.bytes = {0x89, 'P', 'N', 'G', 1, 2, 3, 4};
    } else {
        def.source = "photo.png";
    }
    def.pixel_w = 2;
    def.pixel_h = 2;
    doc.image_defs.push_back(def);
    DocImage im;
    im.width = 2;
    im.height = 2;
    doc.images.push_back(im);
    return doc;
}
} // namespace

TEST_CASE("External files: a native drawing names an xref or an image kept by path") {
    Document plain;
    plain.lines.push_back(DocLine{{0, 0}, {10, 0}});
    DocBlockDef local;
    local.name = "SYM";
    plain.block_defs.push_back(local);
    CHECK_FALSE(references_external_files(serialize_native(plain), false));
    CHECK(references_external_files(serialize_native(with_xref()), false));
    CHECK(references_external_files(serialize_native(with_image(false)), false));
    // An embedded image travels inside the file: nothing to fetch.
    CHECK_FALSE(references_external_files(serialize_native(with_image(true)), false));
    // CRLF line endings (a file carried through Windows) read the same.
    std::string crlf;
    for (const char ch : serialize_native(with_image(false))) {
        if (ch == '\n') {
            crlf += '\r';
        }
        crlf += ch;
    }
    CHECK(references_external_files(crlf, false));
    CHECK_FALSE(references_external_files("", false));
}

TEST_CASE("External files: a DXF names an xref block or any IMAGEDEF") {
    Document plain;
    plain.lines.push_back(DocLine{{0, 0}, {10, 0}});
    DocBlockDef local;
    local.name = "SYM";
    local.circles.push_back(DocCircle{{0, 0}, 2.0});
    plain.block_defs.push_back(local);
    CHECK_FALSE(references_external_files(serialize_dxf(plain), true));
    CHECK(references_external_files(serialize_dxf(with_xref()), true));
    // DXF cannot embed a raster: even an embedded image is written out as a named file.
    CHECK(references_external_files(serialize_dxf(with_image(false)), true));
    CHECK(references_external_files(serialize_dxf(with_image(true)), true));
    // Group codes are right-aligned in files from other writers; the xref flag may carry
    // the overlay bit (8) too.
    CHECK(references_external_files("  0\nSECTION\n  2\nBLOCKS\n  0\nBLOCK\n  2\nX\n 70\n    12\n", true));
    CHECK_FALSE(references_external_files("  0\nBLOCK\n  2\nX\n 70\n     2\n", true));
    // Flag 70 on anything but a BLOCK header is not an xref.
    CHECK_FALSE(references_external_files("0\nLAYER\n70\n4\n", true));
}

TEST_CASE("External files: a single-file document portal grant") {
    const std::string rt = "/run/user/1000";
    CHECK(is_portal_file_grant("/run/user/1000/doc/1a2b3c4d/plan.musa", rt));
    CHECK(is_portal_file_grant("/run/user/1000/doc/1a2b3c4d/plan.musa", rt + "/"));
    CHECK(is_portal_file_grant("/run/flatpak/doc/1a2b3c4d/plan.musa", rt));
    CHECK(is_portal_file_grant("/run/flatpak/doc/1a2b3c4d/plan.musa", ""));
    // A granted folder is a real directory: siblings are real files.
    CHECK_FALSE(is_portal_file_grant("/run/user/1000/doc/1a2b3c4d/project/plan.musa", rt));
    CHECK_FALSE(is_portal_file_grant("/run/user/1000/doc/1a2b3c4d/project/", rt));
    // Not the portal at all.
    CHECK_FALSE(is_portal_file_grant("/home/user/plan.musa", rt));
    CHECK_FALSE(is_portal_file_grant("/run/user/1000/plan.musa", rt));
    CHECK_FALSE(is_portal_file_grant("/run/user/1000/doc/", rt));
    CHECK_FALSE(is_portal_file_grant("/run/user/1000/doc/1a2b3c4d/", rt));
    CHECK_FALSE(is_portal_file_grant("/run/user/10000/doc/1a2b3c4d/plan.musa", rt));
}

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// PEDIT's vertex editor (#52): the X on the current vertex, Next and Previous, Insert,
// Move, Straighten and Break between two vertices.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"

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

io::Document dump(GeometryEngine& engine, const char* name) {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
    engine.consume_snapshot();
    const std::uint64_t before = engine.snapshot().document_version;
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [before](const auto& s) { return s.document_version > before; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    std::filesystem::remove(p);
    return doc;
}

PeditCommand vop(std::uint8_t op, std::uint64_t group, Vec2 p1 = {}, Vec2 p2 = {}) {
    return PeditCommand{{5, 0}, 0.5, op, p1, p2, group};
}

// The X's centre (the middle of its first stroke), if there is one.
bool marker_at(const RenderSnapshot& s, Vec2 at) {
    if (s.grip_preview_segments.size() != 4) {
        return false;
    }
    const Vec2 c = (s.grip_preview_segments[0] + s.grip_preview_segments[1]) * 0.5;
    return std::abs(c.x - at.x) < 1e-9 && std::abs(c.y - at.y) < 1e-9;
}
} // namespace

TEST_CASE("PEDIT's vertex editor: the X walks the vertices; Insert, Move, Straighten and Break") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {10, 0}, {20, 0}, {30, 0}}, false, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(vop(11, 0));
    REQUIRE(wait_until(engine, [](const auto& s) { return marker_at(s, {0, 0}); }));
    engine.submit(vop(12, 0)); // Next
    REQUIRE(wait_until(engine, [](const auto& s) { return marker_at(s, {10, 0}); }));
    engine.submit(vop(18, 2, {15, 5})); // Insert after it: the X moves on to it
    REQUIRE(wait_until(engine, [](const auto& s) { return marker_at(s, {15, 5}); }));
    engine.submit(vop(19, 3, {15, 8})); // Move
    REQUIRE(wait_until(engine, [](const auto& s) { return marker_at(s, {15, 8}); }));
    io::Document doc = dump(engine, "musacad_pedit_vertex1.musa");
    REQUIRE(doc.polylines.size() == 1);
    REQUIRE(doc.polylines[0].points.size() == 5);
    REQUIRE(doc.polylines[0].points[2] == Vec2{15, 8});

    engine.submit(vop(13, 0)); // Previous: (10, 0)
    engine.submit(vop(14, 0)); // mark it
    engine.submit(vop(12, 0));
    engine.submit(vop(12, 0)); // on to (20, 0)
    engine.submit(vop(16, 4)); // Straighten
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Straightened."; }));
    doc = dump(engine, "musacad_pedit_vertex2.musa");
    REQUIRE(doc.polylines[0].points.size() == 4);
    REQUIRE(doc.polylines[0].points[2] == Vec2{20, 0});

    engine.submit(vop(14, 0)); // the X is on (10, 0) now: mark it
    engine.submit(vop(12, 0));
    engine.submit(vop(15, 5)); // Break between (10, 0) and (20, 0)
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Broken."; }));
    doc = dump(engine, "musacad_pedit_vertex3.musa");
    REQUIRE(doc.polylines.size() == 2);
    std::size_t pts = 0;
    for (const auto& p : doc.polylines) {
        pts += p.points.size();
        REQUIRE(p.points.size() == 2);
    }
    REQUIRE(pts == 4);
    engine.submit(vop(17, 0)); // eXit: the X goes
    REQUIRE(wait_until(engine, [](const auto& s) { return s.grip_preview_segments.empty(); }));
    engine.stop();
}

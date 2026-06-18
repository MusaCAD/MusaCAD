// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

// Multi-document (Phase A) engine core: one engine swapping the active document.
// Verifies content/undo/dirty isolation, open-into-new-tab, and close lifecycle --
// all at the engine level (no UI), so the foundation is proven on its own.

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad::core;

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
        if ((eq(s.line_vertices[i], a) && eq(s.line_vertices[i + 1], b)) ||
            (eq(s.line_vertices[i], b) && eq(s.line_vertices[i + 1], a))) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("Multidoc: create + switch isolates document content") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1}); // Drawing1: horizontal
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.documents.size() == 1 && has_segment(s, {0, 0}, {10, 0});
    }));
    const std::uint64_t d1 = engine.snapshot().active_document_id;

    engine.submit(CreateDocumentCommand{}); // Drawing2: new, active, empty
    REQUIRE(wait_until(engine, [&](const auto& s) {
        return s.documents.size() == 2 && s.active_document_id != d1 && s.line_vertices.empty();
    }));
    const std::uint64_t d2 = engine.snapshot().active_document_id;
    engine.submit(AddLineCommand{{0, 0}, {0, 20}, 2}); // Drawing2: vertical
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {0, 20}); }));

    // Back to Drawing1: its horizontal line returns, Drawing2's vertical is gone.
    engine.submit(SwitchDocumentCommand{d1});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        return s.active_document_id == d1 && has_segment(s, {0, 0}, {10, 0}) &&
               !has_segment(s, {0, 0}, {0, 20});
    }));
    engine.stop();
}

TEST_CASE("Multidoc: undo is per-document") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));
    const std::uint64_t a = engine.snapshot().active_document_id;
    engine.submit(CreateDocumentCommand{});
    REQUIRE(wait_until(engine,
                       [&](const auto& s) { return s.active_document_id != a && s.line_vertices.empty(); }));
    const std::uint64_t b = engine.snapshot().active_document_id;
    engine.submit(AddLineCommand{{0, 0}, {0, 20}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {0, 20}); }));

    // Undo on A rewinds A's op only; B is untouched.
    engine.submit(SwitchDocumentCommand{a});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        return s.active_document_id == a && has_segment(s, {0, 0}, {10, 0});
    }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    engine.submit(SwitchDocumentCommand{b});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        return s.active_document_id == b && has_segment(s, {0, 0}, {0, 20});
    }));
    engine.stop();
}

TEST_CASE("Multidoc: dirty flag is per-document") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.dirty; }));
    const std::uint64_t a = engine.snapshot().active_document_id;
    engine.submit(CreateDocumentCommand{}); // fresh doc is clean
    REQUIRE(wait_until(engine, [&](const auto& s) {
        if (s.active_document_id == a || s.dirty || s.documents.size() != 2) {
            return false;
        }
        bool a_dirty = false;
        for (const auto& d : s.documents) {
            if (d.id == a) {
                a_dirty = d.dirty;
            }
        }
        return a_dirty; // A still dirty while B (active) is clean
    }));
    engine.stop();
}

TEST_CASE("Multidoc: open creates a new tab without disturbing the current one") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));
    const std::uint64_t a = engine.snapshot().active_document_id;

    const std::string path =
        (std::filesystem::temp_directory_path() / "musa_multidoc_open.musa").string();
    engine.submit(SaveDocumentCommand{path, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.dirty; }));

    engine.submit(CreateDocumentCommand{}); // B: unsaved work we must NOT lose
    REQUIRE(wait_until(engine,
                       [&](const auto& s) { return s.active_document_id != a && s.line_vertices.empty(); }));
    const std::uint64_t b = engine.snapshot().active_document_id;
    engine.submit(AddLineCommand{{0, 0}, {0, 20}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {0, 20}); }));

    // Open the saved file in a NEW tab.
    OpenDocumentCommand oc;
    oc.path = path;
    oc.new_tab = true;
    engine.submit(oc);
    REQUIRE(wait_until(engine, [&](const auto& s) {
        return s.documents.size() == 3 && s.active_document_id != a && s.active_document_id != b &&
               has_segment(s, {0, 0}, {10, 0});
    }));

    // B's unsaved content is intact.
    engine.submit(SwitchDocumentCommand{b});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        return s.active_document_id == b && has_segment(s, {0, 0}, {0, 20});
    }));
    engine.stop();
    std::filesystem::remove(path);
}

TEST_CASE("Multidoc: close activates a neighbour; closing the last resets to empty") {
    GeometryEngine engine;
    engine.start();
    engine.submit(CreateDocumentCommand{}); // now two docs
    REQUIRE(wait_until(engine, [](const auto& s) { return s.documents.size() == 2; }));
    const RenderSnapshot snap = engine.snapshot();
    const std::uint64_t d1 = snap.documents[0].id;
    const std::uint64_t d2 = snap.documents[1].id;
    REQUIRE(snap.active_document_id == d2);

    engine.submit(CloseDocumentCommand{d2}); // close active -> neighbour d1 active
    REQUIRE(wait_until(engine, [&](const auto& s) {
        return s.documents.size() == 1 && s.active_document_id == d1;
    }));

    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));
    engine.submit(CloseDocumentCommand{d1}); // close the last -> never zero, resets to empty
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.documents.size() == 1 && s.line_vertices.empty();
    }));
    engine.stop();
}

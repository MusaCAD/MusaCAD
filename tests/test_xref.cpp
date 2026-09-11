// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// XREF (#25): an attached drawing is a block that follows its file -- attach with its own
// blocks carried along, reload after the file changes, detach, the file forms, the command.

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
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad::core;
using namespace musacad::core::io;

namespace {
struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompts.push_back(p); }
    std::vector<std::string> lines;
    std::vector<std::string> prompts;
};
struct StubView : musacad::command::ViewControl {
    void zoom_extents() override {}
    void zoom_scale(double) override {}
    std::string open_file_dialog(const std::string&) override { return "/tmp/picked.musa"; }
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
int segments_near(const RenderSnapshot& s, Vec2 a, Vec2 b, double tol = 1e-6) {
    int n = 0;
    for (std::size_t i = 0; i + 1 < s.line_vertices.size(); i += 2) {
        const Vec2 p = s.line_vertices[i];
        const Vec2 q = s.line_vertices[i + 1];
        const bool fwd = std::abs(p.x - a.x) < tol && std::abs(p.y - a.y) < tol && std::abs(q.x - b.x) < tol && std::abs(q.y - b.y) < tol;
        const bool rev = std::abs(p.x - b.x) < tol && std::abs(p.y - b.y) < tol && std::abs(q.x - a.x) < tol && std::abs(q.y - a.y) < tol;
        n += (fwd || rev) ? 1 : 0;
    }
    return n;
}
std::filesystem::path temp_dir() {
    const auto dir = std::filesystem::temp_directory_path() / "musacad_xref_tests";
    std::filesystem::create_directories(dir);
    return dir;
}
/// A source drawing: a line of `len`, a block SUB (a 5-unit circle) inserted at (20, 0).
std::string write_source(double len) {
    Document doc;
    doc.lines.push_back(DocLine{{0, 0}, {len, 0}});
    DocBlockDef sub;
    sub.name = "SUB";
    sub.circles.push_back(DocCircle{{0, 0}, 5.0});
    doc.block_defs.push_back(sub);
    DocInsert in;
    in.block_name = "SUB";
    in.pos = {20, 0};
    doc.inserts.push_back(in);
    const std::string path = (temp_dir() / "part.musa").string();
    std::ofstream(path) << serialize_native(doc);
    return path;
}
} // namespace

TEST_CASE("#25 XREF attach / reload / detach: a block that follows its file, nested blocks carried along") {
    const std::string src = write_source(10.0);
    GeometryEngine engine;
    engine.start();
    engine.submit(XrefAttachCommand{src, {100, 0}, 2.0, 0.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.block_names.size() == 2; }));
    {
        const RenderSnapshot& s = engine.snapshot();
        bool part = false;
        bool sub = false;
        for (const std::string& n : s.block_names) {
            part = part || n == "part";
            sub = sub || n == "part|SUB";
        }
        CHECK(part);
        CHECK(sub);
    }
    // The reference draws the line at scale 2 and the nested circle around (140, 0).
    REQUIRE(wait_until(engine, [](const auto& s) { return segments_near(s, {100, 0}, {120, 0}) == 1; }));
    bool circle_near = false;
    for (const Vec2& v : engine.snapshot().line_vertices) {
        circle_near = circle_near || (std::abs(v.x - 150.0) < 0.5 && std::abs(v.y) < 0.5); // rightmost point
    }
    CHECK(circle_near);

    // The file changes; Reload picks it up.
    write_source(50.0);
    engine.submit(XrefReloadCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return segments_near(s, {100, 0}, {200, 0}) == 1; }));
    engine.submit(XrefListCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("part <- ") != std::string::npos; }));

    // A block of the same name that is not an xref blocks a second attach.
    engine.submit(XrefDetachCommand{"nope", 2});
    engine.submit(XrefDetachCommand{"part", 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.block_names.empty() && s.line_vertices.empty(); }));
    engine.submit(UndoLastGroupCommand{}); // the reference comes back; its definition is gone
    engine.submit(AddLineCommand{{0, 50}, {10, 50}, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() >= 2; }));
    engine.stop();
}

TEST_CASE("#25 XREF: native v32 and DXF keep the source path; a drawing re-reads its xrefs on open") {
    const std::string src = write_source(30.0);
    Document doc;
    DocBlockDef b;
    b.name = "part";
    b.xref_path = src;
    b.lines.push_back(DocLine{{0, 0}, {1, 0}}); // a stale copy
    doc.block_defs.push_back(b);
    DocInsert in;
    in.block_name = "part";
    doc.inserts.push_back(in);
    Document rt;
    REQUIRE(parse_native(serialize_native(doc), rt).ok);
    REQUIRE(rt.block_defs.size() == 1);
    CHECK(rt.block_defs[0].xref_path == src);
    const std::string dxf = serialize_dxf(doc);
    CHECK(dxf.find("\n70\n4\n") != std::string::npos);
    Document from_dxf;
    REQUIRE(parse_dxf(dxf, from_dxf).ok);
    REQUIRE(from_dxf.block_defs.size() == 1);
    CHECK(from_dxf.block_defs[0].xref_path == src);

    // Opening a drawing with an xref re-reads it: the stale 1-unit line becomes 30.
    const std::string host = (temp_dir() / "host.musa").string();
    std::ofstream(host) << serialize_native(doc);
    GeometryEngine engine;
    engine.start();
    engine.submit(OpenDocumentCommand{host, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return segments_near(s, {0, 0}, {30, 0}) == 1; }));
    engine.stop();
}

TEST_CASE("#25 commands: XREF Attach (typed path and the dialog), Detach, Reload, ?") {
    ProcHarness h;
    h.proc.submit_line("XREF");
    h.proc.submit_line("");
    h.proc.submit_line("/tmp/part.musa");
    h.proc.submit_line("5,5");
    h.proc.submit_line("2");
    h.proc.submit_line("90");
    const auto* a = h.last<XrefAttachCommand>();
    REQUIRE(a != nullptr);
    CHECK(a->path == "/tmp/part.musa");
    CHECK(a->pos == Vec2{5, 5});
    CHECK(a->scale == 2.0);
    CHECK(std::abs(a->rotation - kPi / 2.0) < 1e-9);
    h.proc.submit_line("XR");
    h.proc.submit_line("A");
    h.proc.submit_line("~");
    h.proc.submit_line("");
    h.proc.submit_line("");
    h.proc.submit_line("");
    CHECK(h.last<XrefAttachCommand>()->path == "/tmp/picked.musa");
    h.proc.submit_line("XREF");
    h.proc.submit_line("D");
    h.proc.submit_line("part");
    CHECK(h.last<XrefDetachCommand>()->name == "part");
    h.proc.submit_line("XREF");
    h.proc.submit_line("R");
    h.proc.submit_line("");
    CHECK(h.last<XrefReloadCommand>()->name.empty());
    h.proc.submit_line("XREF");
    h.proc.submit_line("?");
    CHECK(h.last<XrefListCommand>() != nullptr);
}

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// TRIM / EXTEND by a path drawn with the pointer (a freehand path, or a fence of two
// picks on empty space) and Quick mode's delete; COPYCLIP / COPYBASE / PASTECLIP and the
// system clipboard's Musa CAD objects.

#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad::core;
using musacad::command::CommandOutput;
using musacad::command::CommandProcessor;
using musacad::command::ViewControl;

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
    const auto eq = [&](Vec2 p, Vec2 q) { return std::abs(p.x - q.x) < eps && std::abs(p.y - q.y) < eps; };
    for (std::size_t i = 0; i + 1 < s.line_vertices.size(); i += 2) {
        const Vec2 p = s.line_vertices[i];
        const Vec2 q = s.line_vertices[i + 1];
        if ((eq(p, a) && eq(q, b)) || (eq(p, b) && eq(q, a))) {
            return true;
        }
    }
    return false;
}

std::size_t segments(const RenderSnapshot& s) { return s.line_vertices.size() / 2; }

struct Out : CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
};

struct View : ViewControl {
    std::string clip_kind = "objects";
    int prepared = 0;
    std::optional<Vec2> pasted_at;
    bool pasted_original = false;
    void zoom_extents() override {}
    void zoom_scale(double) override {}
    std::string prepare_paste() override {
        ++prepared;
        return clip_kind;
    }
    bool paste_at(Vec2 at, bool original) override {
        pasted_at = at;
        pasted_original = original;
        return true;
    }
};

struct H {
    std::vector<Command> cmds;
    Out out;
    View view;
    CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, &view, out};
    template <class T>
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
};

/// Three posts standing on a ground line: the ground is the cutting edge.
void add_posts(GeometryEngine& engine) {
    engine.submit(AddLineCommand{{0, 0}, {30, 0}, 1}); // the ground
    engine.submit(AddLineCommand{{5, -10}, {5, 10}, 2});
    engine.submit(AddLineCommand{{15, -10}, {15, 10}, 3});
    engine.submit(AddLineCommand{{25, -10}, {25, 10}, 4});
}
} // namespace

// ---------------------------------------------------------------------------
// The engine: a path trims everything it crosses
// ---------------------------------------------------------------------------

TEST_CASE("TRIM by a path: every object the path crosses is trimmed where it is crossed") {
    GeometryEngine engine;
    engine.start();
    add_posts(engine);
    REQUIRE(wait_until(engine, [](const auto& s) { return segments(s) == 4; }));

    // A freehand stroke across the three posts below the ground.
    engine.submit(TrimPathCommand{{{1, -6}, {12, -5}, {28, -7}}, false, 0.5, 10});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {5, 0}, {5, 10}) && has_segment(s, {15, 0}, {15, 10}) &&
               has_segment(s, {25, 0}, {25, 10}) && has_segment(s, {0, 0}, {30, 0});
    }));
    engine.consume_snapshot();
    REQUIRE(segments(engine.snapshot()) == 4);
    REQUIRE(engine.snapshot().status.find("Trimmed 3 objects") != std::string::npos);

    // The whole path is one undo step.
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {5, -10}, {5, 10}) && has_segment(s, {15, -10}, {15, 10}) &&
               has_segment(s, {25, -10}, {25, 10});
    }));
    engine.stop();
}

TEST_CASE("TRIM by a path: what has nothing to be trimmed to is deleted (Quick mode)") {
    GeometryEngine engine;
    engine.start();
    add_posts(engine);
    engine.submit(AddLineCommand{{40, -10}, {40, 10}, 5}); // stands alone, crosses nothing
    REQUIRE(wait_until(engine, [](const auto& s) { return segments(s) == 5; }));

    engine.submit(TrimPathCommand{{{20, -5}, {45, -5}}, false, 0.5, 10});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {25, 0}, {25, 10}) && !has_segment(s, {40, -10}, {40, 10});
    }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().status.find("deleted 1") != std::string::npos);
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {40, -10}, {40, 10}) && has_segment(s, {25, -10}, {25, 10});
    }));
    engine.stop();
}

TEST_CASE("TRIM by a path that crosses nothing changes nothing") {
    GeometryEngine engine;
    engine.start();
    add_posts(engine);
    REQUIRE(wait_until(engine, [](const auto& s) { return segments(s) == 4; }));
    engine.submit(TrimPathCommand{{{50, 50}, {60, 60}}, false, 0.5, 10});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("crosses nothing") != std::string::npos;
    }));
    REQUIRE(segments(engine.snapshot()) == 4);
    engine.stop();
}

TEST_CASE("A single TRIM pick on an object with no cutting edge deletes it (Quick mode)") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return segments(s) == 1; }));
    engine.submit(TrimPickCommand{{5, 0}, 1.0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return segments(s) == 0; }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));
    engine.stop();
}

TEST_CASE("EXTEND by a path: every object the path crosses is extended to its boundary") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 20}, {30, 20}, 1}); // the boundary
    engine.submit(AddLineCommand{{5, 0}, {5, 10}, 2});
    engine.submit(AddLineCommand{{15, 0}, {15, 10}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return segments(s) == 3; }));
    // Across the upper halves: the ends nearer the crossing go out to the boundary.
    engine.submit(TrimPathCommand{{{0, 8}, {20, 8}}, true, 0.5, 10});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {5, 0}, {5, 20}) && has_segment(s, {15, 0}, {15, 20});
    }));
    engine.stop();
}

// ---------------------------------------------------------------------------
// The command: the gesture, the fence, Shift
// ---------------------------------------------------------------------------

TEST_CASE("TRIM takes a freehand path from the viewport as one path command") {
    H h;
    h.proc.submit_line("TR");
    REQUIRE(h.proc.wants_freehand());
    h.proc.submit_freehand({{0, 0}, {1, 1}, {2, 0}});
    const auto* p = h.last<TrimPathCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->path.size() == 3);
    REQUIRE_FALSE(p->extend);
    REQUIRE(h.proc.has_active_command()); // TRIM goes on

    h.proc.set_shift_held(true); // Shift: the other one
    h.proc.submit_freehand({{0, 0}, {5, 5}});
    REQUIRE(h.last<TrimPathCommand>()->extend);
    h.proc.set_shift_held(false);
    h.proc.submit_line("");
    REQUIRE_FALSE(h.proc.has_active_command());
    REQUIRE_FALSE(h.proc.wants_freehand());
}

TEST_CASE("TRIM: two picks on empty space are a fence between them") {
    H h;
    h.proc.submit_line("TR");
    h.proc.pick_point({0, 0}, std::nullopt); // nothing under the cursor
    REQUIRE(h.last<TrimPathCommand>() == nullptr);
    REQUIRE(h.out.prompt == "Specify second fence point: ");
    h.proc.pick_point({10, 5}, std::nullopt);
    const auto* p = h.last<TrimPathCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->path.size() == 2);
    REQUIRE(std::abs(p->path[1].x - 10.0) < 1e-12);
    REQUIRE(h.out.prompt ==
            "Select object to trim or shift-select to extend or [cuTting edges/Crossing/mOde/Project/eRase/Undo]: ");

    // On an object, a pick is a pick.
    h.proc.set_hovered_kind(EntityKind::Line);
    h.proc.pick_point({3, 3}, std::nullopt);
    REQUIRE(h.last<TrimPickCommand>() != nullptr);
    h.proc.cancel();
}

TEST_CASE("TRIM Fence: typed points until Enter; EXTEND takes the same path") {
    H h;
    h.proc.submit_line("EXTEND");
    REQUIRE(h.out.prompt == "Select object to extend or shift-select to trim or [Boundary edges/Crossing/mOde/Project]: ");
    h.proc.submit_line("F");
    REQUIRE(h.out.prompt == "Specify first fence point: ");
    h.proc.submit_line("0,0");
    h.proc.submit_line("5,5");
    h.proc.submit_line("10,0");
    REQUIRE(h.last<TrimPathCommand>() == nullptr);
    h.proc.submit_line("");
    const auto* p = h.last<TrimPathCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->path.size() == 3);
    REQUIRE(p->extend);
    h.proc.cancel();
}

// ---------------------------------------------------------------------------
// The clipboard
// ---------------------------------------------------------------------------

TEST_CASE("COPYCLIP hands the system clipboard the objects as a drawing and their picture") {
    GeometryEngine engine;
    std::mutex m;
    std::optional<ClipboardExport> got;
    engine.set_clipboard_listener([&](const ClipboardExport& e) {
        std::scoped_lock lock(m);
        got = e;
    });
    engine.start();
    engine.submit(AddLineCommand{{10, 10}, {20, 10}, 1});
    engine.submit(AddCircleCommand{{15, 15}, 2.0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return segments(s) > 1; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(CopyClipboardCommand{});
    REQUIRE(wait_until(engine, [&](const auto&) {
        std::scoped_lock lock(m);
        return got.has_value();
    }));
    std::scoped_lock lock(m);
    REQUIRE(got->count == 2);
    REQUIRE(!got->native_text.empty());
    REQUIRE(!got->drawing.line_vertices.empty());
    REQUIRE(std::abs(got->lo.x - 10.0) < 1e-9);
    REQUIRE(std::abs(got->hi.y - 17.0) < 1e-9);
    engine.stop();
}

TEST_CASE("Objects copied in one Musa CAD paste in another, by their base point") {
    std::string text;
    Vec2 base{};
    {
        GeometryEngine source;
        std::mutex m;
        source.set_clipboard_listener([&](const ClipboardExport& e) {
            std::scoped_lock lock(m);
            text = e.native_text;
            base = e.base;
        });
        source.start();
        source.submit(AddLineCommand{{100, 100}, {110, 100}, 1});
        REQUIRE(wait_until(source, [](const auto& s) { return segments(s) == 1; }));
        source.submit(SelectAllCommand{});
        REQUIRE(wait_until(source, [](const auto& s) { return s.selection.size() == 1; }));
        source.submit(CopyClipboardCommand{Vec2{105, 100}}); // COPYBASE at the midpoint
        REQUIRE(wait_until(source, [&](const auto&) {
            std::scoped_lock lock(m);
            return !text.empty();
        }));
        source.stop();
    }

    GeometryEngine target;
    target.start();
    // Read in as the clip first (PASTECLIP's band follows it), then placed.
    PasteDocumentCommand load;
    REQUIRE(std::abs(base.x - 105.0) < 1e-12);
    load.native_text = text;
    load.load_only = true;
    load.base = base;
    target.submit(std::move(load));
    REQUIRE(wait_until(target, [](const auto& s) {
        return s.status.find("1 object on the clipboard") != std::string::npos;
    }));
    target.submit(PastePreviewCommand{{0, 0}, true});
    REQUIRE(wait_until(target, [](const auto& s) { return !s.grip_preview_segments.empty(); }));
    target.submit(PastePreviewCommand{{}, false});
    target.submit(PasteClipboardCommand{{0, 0}, 7});
    // The base point travelled with the objects: the midpoint lands on the point.
    REQUIRE(wait_until(target, [](const auto& s) {
        return has_segment(s, {-5, 0}, {5, 0}) && s.grip_preview_segments.empty();
    }));
    target.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(target, [](const auto& s) { return segments(s) == 0; }));

    // Pasted at once, at its own coordinates (PASTEORIG).
    target.submit(PasteDocumentCommand{text, {}, 8, false, false});
    REQUIRE(wait_until(target, [](const auto& s) { return has_segment(s, {100, 100}, {110, 100}); }));
    target.stop();
}

TEST_CASE("A clipboard that is not a Musa CAD drawing is said so, not pasted") {
    GeometryEngine engine;
    engine.start();
    engine.submit(PasteDocumentCommand{"not a drawing", {0, 0}, 1, true, false});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("could not be read") != std::string::npos;
    }));
    REQUIRE(segments(engine.snapshot()) == 0);
    engine.stop();
}

TEST_CASE("PASTECLIP asks for the insertion point; the objects follow the cursor until it") {
    H h;
    h.proc.submit_line("PASTECLIP");
    REQUIRE(h.view.prepared == 1);
    REQUIRE(h.out.prompt == "Specify insertion point: ");
    REQUIRE(h.proc.preview().paste_band);
    h.proc.submit_line("12,7");
    REQUIRE(h.view.pasted_at.has_value());
    REQUIRE(std::abs(h.view.pasted_at->x - 12.0) < 1e-12);
    REQUIRE_FALSE(h.view.pasted_original);
    REQUIRE_FALSE(h.proc.has_active_command());
    REQUIRE_FALSE(h.proc.preview().paste_band);
}

TEST_CASE("PASTECLIP with an image or text: no objects follow the cursor; Enter pastes nothing") {
    H h;
    h.view.clip_kind = "image";
    h.proc.submit_line("PASTECLIP");
    REQUIRE_FALSE(h.proc.preview().paste_band);
    h.proc.submit_line("");
    REQUIRE_FALSE(h.view.pasted_at.has_value());
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("PASTEORIG pastes at the objects' own coordinates without asking") {
    H h;
    h.proc.submit_line("PASTEORIG");
    REQUIRE(h.view.pasted_at.has_value());
    REQUIRE(h.view.pasted_original);
    REQUIRE_FALSE(h.proc.has_active_command());

    h.view.pasted_at.reset();
    h.view.clip_kind = "text";
    h.proc.submit_line("PASTEORIG");
    REQUIRE_FALSE(h.view.pasted_at.has_value());
    REQUIRE(h.out.lines.back().find("PASTEORIG pastes objects") != std::string::npos);
}

TEST_CASE("Without a window PASTECLIP pastes the drawing's own clip at the point") {
    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[&](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    proc.submit_line("PASTECLIP");
    proc.submit_line("3,4");
    REQUIRE(!cmds.empty());
    const auto* p = std::get_if<PasteClipboardCommand>(&cmds.back());
    REQUIRE(p != nullptr);
    REQUIRE(std::abs(p->at.y - 4.0) < 1e-12);
    REQUIRE(p->at_cursor);
}

TEST_CASE("COPYCLIP asks for objects when none are selected; COPYBASE asks for the base point first") {
    H h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("COPYCLIP");
    REQUIRE(h.proc.in_selection_phase());
    REQUIRE(h.last<CopyClipboardCommand>() == nullptr);
    h.proc.cancel();

    h.proc.set_selection_count(2);
    h.proc.submit_line("COPYCLIP");
    const auto* c = h.last<CopyClipboardCommand>();
    REQUIRE(c != nullptr);
    REQUIRE_FALSE(c->base.has_value());
    REQUIRE_FALSE(h.proc.has_active_command());

    h.proc.submit_line("COPYBASE");
    REQUIRE(h.out.prompt == "Specify base point: ");
    h.proc.submit_line("4,5");
    c = h.last<CopyClipboardCommand>();
    REQUIRE(c->base.has_value());
    REQUIRE(std::abs(c->base->x - 4.0) < 1e-12);

    h.proc.submit_line("CUTBASE");
    h.proc.submit_line("1,1");
    const auto* x = h.last<CutClipboardCommand>();
    REQUIRE(x != nullptr);
    REQUIRE(x->base.has_value());
}

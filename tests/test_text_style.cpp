// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Issue #29: the text STYLE table. Width factor and obliquing angle are one transform
// shared by drawing, picking and bounds; TEXT follows the current style; the table
// travels through the native format and the DXF STYLE table.

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/text/stroke_font.hpp"

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
struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string&) override {}
    void set_prompt(const std::string& p) override { prompts.push_back(p); }
    std::vector<std::string> prompts;
};
struct ProcHarness {
    std::vector<Command> cmds;
    SilentOutput out;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
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
} // namespace

TEST_CASE("#29 style transform: width factor scales about the anchor, oblique shears; box corners") {
    std::vector<Vec2> pts{{10, 0}, {20, 0}, {20, 5}}; // anchor at (10,0), unrotated
    text::apply_text_style(pts, {10, 0}, 0.0, 2.0, 0.0);
    REQUIRE(pts[0] == Vec2{10, 0});
    REQUIRE(pts[1] == Vec2{30, 0});
    REQUIRE(pts[2].x == Approx(30.0));
    REQUIRE(pts[2].y == Approx(5.0));
    std::vector<Vec2> sh{{0, 4}};
    text::apply_text_style(sh, {0, 0}, 0.0, 1.0, kPi / 4.0); // 45 deg: x += y
    REQUIRE(sh[0].x == Approx(4.0));
    REQUIRE(sh[0].y == Approx(4.0));
    // Rotated frame: a point one unit "up" from the anchor along the rotated y stays put
    // under a pure width factor (only x scales).
    std::vector<Vec2> rot{{-std::sin(kHalfPi), std::cos(kHalfPi)}}; // (−1, 0): local (0,1)
    text::apply_text_style(rot, {0, 0}, kHalfPi, 3.0, 0.0);
    REQUIRE(rot[0].x == Approx(-1.0).margin(1e-9));
    REQUIRE(rot[0].y == Approx(0.0).margin(1e-9));
    Vec2 c[4];
    text::text_box_corners(10.0, 2.0, 1.5, kPi / 4.0, c);
    REQUIRE(c[1] == Vec2{15, 0});
    REQUIRE(c[2].x == Approx(17.0));
    REQUIRE(c[3].x == Approx(2.0));
}

TEST_CASE("#29 store: the style table, current style, in-use and purge; text bounds follow the style") {
    GeometryStore s;
    REQUIRE(s.text_styles().size() == 1);
    REQUIRE(s.text_styles()[0].name == "Standard");
    TextStyle wide;
    wide.name = "Wide";
    wide.width_factor = 2.0;
    const std::uint16_t wi = s.add_text_style(wide);
    REQUIRE(wi == 1);
    wide.height = 3.0;
    REQUIRE(s.add_text_style(wide) == 1); // replaced by name, not duplicated
    REQUIRE(s.text_styles()[1].height == Approx(3.0));
    REQUIRE(s.text_style_index("Wide") == 1);
    REQUIRE(s.text_style_index("nope") == 0xFFFF);
    s.set_current_text_style(1);
    REQUIRE(s.current_text_style() == 1);

    const EntityHandle plain = s.add_text({0, 0}, 2.0, 0.0, 0, "II", {}, 0, 0);
    const EntityHandle styled = s.add_text({0, 0}, 2.0, 0.0, 0, "II", {}, 0, 1);
    Vec2 lo;
    Vec2 hi;
    REQUIRE(entity_aabb(s, plain, lo, hi));
    const double w_plain = hi.x - lo.x;
    REQUIRE(entity_aabb(s, styled, lo, hi));
    REQUIRE(hi.x - lo.x == Approx(2.0 * w_plain)); // the width factor, in the bounds too

    REQUIRE(s.text_style_in_use(1));
    REQUIRE(!s.remove_text_style(1)); // in use (and current)
    REQUIRE(!s.remove_text_style(0)); // Standard stays
    s.set_current_text_style(0);
    s.remove(styled);
    REQUIRE(s.remove_text_style(1));
    REQUIRE(s.text_styles().size() == 1);
}

TEST_CASE("#29 STYLE command flow and TEXT following the current style") {
    ProcHarness h;
    h.proc.submit_line("-STYLE");
    h.proc.submit_line("Title");
    h.proc.submit_line("");     // font: keep (txt = stroke)
    h.proc.submit_line("5");    // fixed height
    h.proc.submit_line("1.5");  // width factor
    h.proc.submit_line("15");   // oblique degrees
    h.proc.submit_line("N");
    h.proc.submit_line("N");
    h.proc.submit_line("N");
    const auto* st = h.last<SetTextStyleCommand>();
    REQUIRE(st != nullptr);
    REQUIRE(st->style.name == "Title");
    REQUIRE(st->style.font.empty());
    REQUIRE(st->style.height == Approx(5.0));
    REQUIRE(st->style.width_factor == Approx(1.5));
    REQUIRE(st->style.oblique == Approx(to_radians(15.0)));
    REQUIRE(st->make_current);
    const TextStyle title = st->style; // copy: later submissions grow the command vector

    // TEXT with a fixed-height current style: no height prompt, height from the style.
    h.proc.set_text_styles({TextStyle{}, title}, 1);
    h.out.prompts.clear();
    h.proc.submit_line("TEXT");
    h.proc.submit_line("0,0");
    h.proc.submit_line("0");      // rotation
    h.proc.submit_line("hello");
    h.proc.submit_line("");       // an empty line ends TEXT
    const auto* t = h.last<AddTextCommand>();
    REQUIRE(t != nullptr);
    REQUIRE(t->height == Approx(5.0));
    REQUIRE(t->style == "Title");
    bool asked_height = false;
    for (const std::string& p : h.out.prompts) {
        asked_height = asked_height || p.find("Specify height") != std::string::npos;
    }
    REQUIRE(!asked_height);

    // Standard current: the height prompt is back.
    h.proc.set_text_styles({TextStyle{}, title}, 0);
    h.out.prompts.clear();
    h.proc.submit_line("TEXT");
    h.proc.submit_line("0,0");
    h.proc.submit_line("3");
    h.proc.submit_line("0");
    h.proc.submit_line("plain");
    h.proc.submit_line("");
    REQUIRE(h.last<AddTextCommand>()->height == Approx(3.0));
    REQUIRE(h.last<AddTextCommand>()->style.empty());
}

TEST_CASE("#29 engine: the style resolves on add, is published, and round-trips through .musa and DXF") {
    GeometryEngine engine;
    engine.start();
    TextStyle wide;
    wide.name = "Wide";
    wide.width_factor = 2.0;
    wide.oblique = to_radians(10.0);
    engine.submit(SetTextStyleCommand{wide, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.text_styles.size() == 2 && s.current_text_style == 1; }));
    AddTextCommand a{{0, 0}, 2.0, 0.0, 0, "II", 1};
    AddTextCommand b{{0, 20}, 2.0, 0.0, 0, "II", 2};
    b.style = "Wide";
    engine.submit(a);
    engine.submit(b);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 8; }));
    // Pick the styled text well past where the plain one ends: only the wide one is there.
    engine.submit(SelectPickCommand{{3.2, 21.0}, 0.3, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_tstyle.musa";
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.rfind("Saved", 0) == 0; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    REQUIRE(doc.text_styles.size() == 2);
    REQUIRE(doc.text_styles[1].name == "Wide");
    REQUIRE(doc.text_styles[1].width_factor == Approx(2.0));
    REQUIRE(doc.current_text_style == 1);
    REQUIRE(doc.texts.size() == 2);
    int styled = 0;
    for (const io::DocText& t : doc.texts) {
        styled += t.style == 1 ? 1 : 0;
    }
    REQUIRE(styled == 1);
    std::filesystem::remove(p);

    const std::filesystem::path d = std::filesystem::temp_directory_path() / "musacad_tstyle.dxf";
    REQUIRE(io::save_dxf(doc, d.string()).ok);
    io::Document back;
    REQUIRE(io::load_dxf(d.string(), back).ok);
    REQUIRE(back.text_styles.size() == 2);
    REQUIRE(back.text_styles[1].name == "Wide");
    REQUIRE(back.text_styles[1].width_factor == Approx(2.0));
    REQUIRE(back.text_styles[1].oblique == Approx(to_radians(10.0)));
    styled = 0;
    for (const io::DocText& t : back.texts) {
        styled += t.style == 1 ? 1 : 0;
    }
    REQUIRE(styled == 1);
    std::filesystem::remove(d);
    engine.stop();
}

TEST_CASE("#29 a store moved out of (a parked tab) and cleared is a fresh store again: Standard style, defaults") {
    GeometryStore s;
    s.add_text_style(TextStyle{"Notes"});
    s.set_current_text_style(1);
    s.set_wipeout_frames(false);
    GeometryStore parked = std::move(s);
    s.clear();
    // Opening a file into a new tab does exactly this, then looks text styles up while
    // indexing the loaded entities -- a moved-from (empty) style table must not be seen.
    REQUIRE(s.text_styles().size() == 1);
    CHECK(s.text_styles()[0].name == "Standard");
    CHECK(s.current_text_style() == 0);
    CHECK(s.wipeout_frames());
    CHECK(s.named_views().empty());
    const EntityHandle t = s.add_text({0, 0}, 2.5, 0.0, 0, "abc");
    CHECK(s.text_style_of(*s.text(t)).name == "Standard");
    CHECK(parked.text_styles().size() == 2);
}

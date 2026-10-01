// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Single-line text (issue #42): the fifteen justifications and the frame they lay a text
// out in, Aligned and Fit, the text's own width factor, the files, and the commands --
// TEXT with Justify / Style, line after line, TEXTEDIT, JUSTIFYTEXT, SCALETEXT, TXT2MTXT.

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/properties_palette.hpp"
#include "musacad/core/text/justify.hpp"
#include "musacad/core/text/stroke_font.hpp"
#include "musacad/core/text/text_frame.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {
constexpr double kPiT = 3.14159265358979323846;

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
bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }
bool near(Vec2 p, Vec2 q, double eps = 1e-6) { return near(p.x, q.x, eps) && near(p.y, q.y, eps); }
/// The one text in the drawing, as it is laid out (its edit target carries the frame):
/// where its baseline starts, how wide and how tall it is, which way it runs.
bool laid_out(const RenderSnapshot& s, Vec2 origin, double width, double height, double rotation = 0.0) {
    if (s.text_edit_targets.size() != 1) {
        return false;
    }
    const TextEditTarget& t = s.text_edit_targets[0];
    return near(t.anchor, origin) && near(t.max.x - t.min.x, width) && near(t.height, height) &&
           near(t.rotation, rotation);
}
int justify_of_selection(const RenderSnapshot& s) {
    for (const PropertyField& f : s.selection_summary.fields) {
        if (f.id == PropertyId::TextJustify) {
            return f.value.choice;
        }
    }
    return -1;
}
AddTextCommand text_at(Vec2 pos, double height, std::uint8_t justify, std::string content,
                       std::uint64_t group, Vec2 align = {}) {
    AddTextCommand t;
    t.pos = pos;
    t.height = height;
    t.justify = justify;
    t.content = std::move(content);
    t.group = group;
    t.align = align;
    return t;
}

struct Out : musacad::command::CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
    [[nodiscard]] bool any_contains(const std::string& sub) const {
        for (const auto& l : lines) {
            if (l.find(sub) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
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
    template <class T>
    [[nodiscard]] int count() const {
        int n = 0;
        for (const Command& c : cmds) {
            n += std::holds_alternative<T>(c) ? 1 : 0;
        }
        return n;
    }
    void run(std::initializer_list<const char*> lines_in) {
        for (const char* l : lines_in) {
            proc.submit_line(l);
        }
    }
    /// Left, 2.5 high, unrotated: what a fresh session starts with.
    void reset_text_defaults() {
        run({"TEXT", "J", "L", "0,0", "2.5", "0"});
        proc.cancel();
    }
};
} // namespace

TEST_CASE("#42 the fifteen justifications place a text's frame") {
    // A run 4 wide per unit of height, 2 high: 8 wide, its descenders 2/3 below the line.
    const Vec2 pos{10, 10};
    const auto frame = [&](std::uint8_t j, double rotation = 0.0) {
        return text::justify_frame(j, pos, pos, 2.0, rotation, 4.0, 1.0);
    };
    REQUIRE(near(frame(0).origin, {10, 10}));           // Left
    REQUIRE(near(frame(1).origin, {6, 10}));            // Center
    REQUIRE(near(frame(2).origin, {2, 10}));            // Right
    REQUIRE(near(frame(4).origin, {6, 10 - 2.0 / 3.0})); // Middle: the whole box's middle
    REQUIRE(near(frame(6).origin, {10, 8}));            // TL
    REQUIRE(near(frame(7).origin, {6, 8}));             // TC
    REQUIRE(near(frame(8).origin, {2, 8}));             // TR
    REQUIRE(near(frame(9).origin, {10, 9}));            // ML
    REQUIRE(near(frame(10).origin, {6, 9}));            // MC
    REQUIRE(near(frame(11).origin, {2, 9}));            // MR
    REQUIRE(near(frame(12).origin, {10, 10 + 2.0 / 3.0})); // BL
    REQUIRE(near(frame(14).origin, {2, 10 + 2.0 / 3.0}));  // BR
    // Turned a quarter: along is up the page.
    REQUIRE(near(frame(1, kPiT / 2.0).origin, {10, 6}));
    REQUIRE(near(frame(6, kPiT / 2.0).origin, {12, 10}));
    // A width factor widens the run the justification is worked out on.
    REQUIRE(near(text::justify_frame(2, pos, pos, 2.0, 0.0, 4.0, 0.5).origin, {6, 10}));

    // Aligned scales the height to reach; Fit keeps it and squeezes the width.
    const text::JustifyFrame a = text::justify_frame(3, {0, 0}, {3, 4}, 9.0, 1.0, 4.0, 1.0);
    REQUIRE(a.height == Approx(1.25));
    REQUIRE(a.rotation == Approx(std::atan2(4.0, 3.0)));
    REQUIRE(a.width_factor == Approx(1.0));
    REQUIRE(near(a.origin, {0, 0}));
    const text::JustifyFrame f = text::justify_frame(5, {0, 0}, {10, 0}, 2.0, 1.0, 4.0, 3.0);
    REQUIRE(f.height == Approx(2.0));
    REQUIRE(f.rotation == Approx(0.0));
    REQUIRE(f.width_factor == Approx(1.25));
    // Two ends that coincide leave the text as it was given.
    REQUIRE(text::justify_frame(3, pos, pos, 2.0, 0.5, 4.0, 1.0).height == Approx(2.0));
}

TEST_CASE("#42 every justification can name its point on a text that stays put") {
    const text::JustifyFrame where{{1, 2}, 2.0, kPiT / 6.0, 1.2};
    const double unit = 4.0;
    const double width = unit * where.height * where.width_factor;
    for (std::uint8_t j = 0; j < text::kTextJustifyCount; ++j) {
        Vec2 pos{};
        Vec2 align{};
        text::justify_points(j, where, width, pos, align);
        const double factor = j == 5 ? 7.0 : where.width_factor; // Fit works its own out
        const text::JustifyFrame back =
            text::justify_frame(j, pos, align, where.height, where.rotation, unit, factor);
        INFO("justification " << static_cast<int>(j));
        REQUIRE(near(back.origin, where.origin));
        REQUIRE(back.height == Approx(where.height));
        REQUIRE(back.rotation == Approx(where.rotation));
        REQUIRE(back.width_factor == Approx(where.width_factor));
    }
}

TEST_CASE("#42 justifications by keyword and as DXF's 72 / 73") {
    REQUIRE(text::justify_of_keyword("tl") == 6);
    REQUIRE(text::justify_of_keyword("MC") == 10);
    REQUIRE(text::justify_of_keyword("BR") == 14);
    REQUIRE(text::justify_of_keyword("Align") == 3);
    REQUIRE(text::justify_of_keyword("aligned") == 3);
    REQUIRE(text::justify_of_keyword("A") == 3);
    REQUIRE(text::justify_of_keyword("fit") == 5);
    REQUIRE(text::justify_of_keyword("M") == 4);
    REQUIRE(text::justify_of_keyword("center") == 1);
    REQUIRE(text::justify_of_keyword("X") == -1);
    REQUIRE(text::justify_of_keyword("") == -1);
    REQUIRE(text::justify_of_keyword("10,10") == -1);
    for (std::uint8_t j = 0; j < text::kTextJustifyCount; ++j) {
        int h = -1;
        int v = -1;
        text::justify_to_dxf(j, h, v);
        REQUIRE(text::justify_from_dxf(h, v) == j);
    }
    int h = 0;
    int v = 0;
    text::justify_to_dxf(10, h, v); // middle center
    REQUIRE(h == 1);
    REQUIRE(v == 2);
    text::justify_to_dxf(12, h, v); // bottom left
    REQUIRE(h == 0);
    REQUIRE(v == 1);
    REQUIRE(text::justify_from_dxf(2, 3) == 8); // top right
    REQUIRE(text::justify_from_dxf(4, 0) == 4); // middle
}

TEST_CASE("#42 a justified text is drawn, bounded and picked where its justification puts it") {
    GeometryEngine e;
    e.start();
    const double w = text::text_width("AB", 2.0);
    e.submit(text_at({10, 10}, 2.0, 1, "AB", 1)); // Center
    REQUIRE(wait_until(e, [&](const auto& s) { return laid_out(s, {10 - w / 2, 10}, w, 2.0); }));
    // What is drawn lies inside that box.
    REQUIRE(e.snapshot().has_bounds);
    REQUIRE(e.snapshot().bounds_min.x >= 10 - w / 2 - 1e-9);
    REQUIRE(e.snapshot().bounds_max.x <= 10 + w / 2 + 1e-9);
    REQUIRE(e.snapshot().bounds_min.x < 10.0);
    REQUIRE(e.snapshot().bounds_min.y >= 10 - 1e-9);
    // Picked on its left half, which a left-justified text at that point would not have.
    e.submit(SelectPickCommand{{10 - w / 4, 11}, 0.1});
    REQUIRE(wait_until(e, [](const auto& s) { return s.selection.size() == 1; }));
    REQUIRE(justify_of_selection(e.snapshot()) == 1);
    e.submit(EraseSelectionCommand{2});

    e.submit(text_at({0, 0}, 2.0, 6, "AB", 3)); // TL: hangs below its point
    REQUIRE(wait_until(e, [&](const auto& s) { return laid_out(s, {0, -2}, w, 2.0); }));
    REQUIRE(e.snapshot().bounds_max.y <= 1e-9);
    e.submit(SelectAllCommand{});
    e.submit(EraseSelectionCommand{4});
    e.submit(text_at({0, 0}, 2.0, 11, "AB", 5)); // MR
    REQUIRE(wait_until(e, [&](const auto& s) { return laid_out(s, {-w, -1}, w, 2.0); }));
    e.stop();
}

TEST_CASE("#42 Aligned and Fit run between two points; both points travel with the text") {
    GeometryEngine e;
    e.start();
    const double unit = text::text_width("ABCD", 1.0);
    e.submit(text_at({0, 0}, 9.0, 3, "ABCD", 1, {20, 0})); // Aligned: the height follows
    REQUIRE(wait_until(e, [&](const auto& s) { return laid_out(s, {0, 0}, 20.0, 20.0 / unit); }));
    e.submit(SelectAllCommand{});
    REQUIRE(wait_until(e, [](const auto& s) { return s.selection.size() == 1 && s.grips.size() == 2; }));
    REQUIRE(near(e.snapshot().grips[0].pos, {0, 0}));
    REQUIRE(near(e.snapshot().grips[1].pos, {20, 0}));

    e.submit(MoveSelectionCommand{{5, 5}, 2});
    REQUIRE(wait_until(e, [&](const auto& s) { return laid_out(s, {5, 5}, 20.0, 20.0 / unit); }));
    e.submit(ScaleSelectionCommand{{5, 5}, 0.5, 3});
    REQUIRE(wait_until(e, [&](const auto& s) { return laid_out(s, {5, 5}, 10.0, 10.0 / unit); }));
    e.submit(RotateSelectionCommand{{5, 5}, kPiT / 2.0, 4});
    REQUIRE(wait_until(e, [&](const auto& s) {
        return laid_out(s, {5, 5}, 10.0, 10.0 / unit, kPiT / 2.0);
    }));
    REQUIRE(wait_until(e, [](const auto& s) { return s.grips.size() == 2 && near(s.grips[1].pos, {5, 15}); }));
    e.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(e, [&](const auto& s) { return laid_out(s, {5, 5}, 10.0, 10.0 / unit); }));
    e.submit(SelectAllCommand{});
    e.submit(EraseSelectionCommand{5});

    e.submit(text_at({0, 0}, 2.0, 5, "ABCD", 6, {20, 0})); // Fit: the height stays
    REQUIRE(wait_until(e, [&](const auto& s) { return laid_out(s, {0, 0}, 20.0, 2.0); }));
    e.stop();
}

TEST_CASE("#42 JUSTIFYTEXT leaves the text where it is; SCALETEXT scales it about its own point") {
    GeometryEngine e;
    e.start();
    const double w = text::text_width("AB", 2.0);
    e.submit(text_at({0, 0}, 2.0, 0, "AB", 1));
    e.submit(SelectAllCommand{});
    REQUIRE(wait_until(e, [](const auto& s) { return s.selection.size() == 1; }));
    e.submit(JustifyTextCommand{10, 2}); // middle center
    REQUIRE(wait_until(e, [](const auto& s) { return justify_of_selection(s) == 10; }));
    REQUIRE(laid_out(e.snapshot(), {0, 0}, w, 2.0));
    // Its grip is at the middle of it now.
    REQUIRE(wait_until(e, [&](const auto& s) { return s.grips.size() == 1 && near(s.grips[0].pos, {w / 2, 1}); }));

    ScaleTextCommand twice;
    twice.mode = 1;
    twice.value = 2.0;
    twice.group = 3;
    e.submit(twice); // about the insertion point: the middle
    REQUIRE(wait_until(e, [&](const auto& s) { return laid_out(s, {-w / 2, -1}, 2 * w, 4.0); }));
    ScaleTextCommand to_height;
    to_height.base = 13; // bottom left, whatever the text's own justification
    to_height.mode = 0;
    to_height.value = 1.0;
    to_height.group = 4;
    e.submit(to_height);
    // The bottom left corner (on the descender line, a third of the height under the
    // baseline) is what stays.
    REQUIRE(wait_until(e, [&](const auto& s) { return laid_out(s, {-w / 2, -2}, w / 2, 1.0); }));
    e.submit(UndoLastGroupCommand{});
    e.submit(UndoLastGroupCommand{});
    e.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(e, [&](const auto& s) { return laid_out(s, {0, 0}, w, 2.0); }));
    REQUIRE(e.snapshot().text_edit_targets.size() == 1);

    // Matched to another text's height.
    e.submit(text_at({0, 50}, 7.0, 0, "tall", 5));
    REQUIRE(wait_until(e, [](const auto& s) { return s.text_edit_targets.size() == 2; }));
    e.submit(ClearSelectionCommand{});
    e.submit(SelectPickCommand{{w / 2, 1}, 0.1});
    REQUIRE(wait_until(e, [](const auto& s) { return s.selection.size() == 1; }));
    ScaleTextCommand match;
    match.mode = 2;
    match.match_pick = {1, 53};
    match.pick_radius = 0.2;
    match.group = 6;
    e.submit(match);
    REQUIRE(wait_until(e, [](const auto& s) {
        return s.text_edit_targets.size() == 2 && near(s.text_edit_targets[0].height, 7.0) &&
               near(s.text_edit_targets[1].height, 7.0);
    }));
    e.stop();
}

TEST_CASE("#42 an Aligned text made Left keeps the height its two points had given it") {
    GeometryEngine e;
    e.start();
    const double unit = text::text_width("ABCD", 1.0);
    e.submit(text_at({0, 0}, 9.0, 3, "ABCD", 1, {20, 0}));
    e.submit(SelectAllCommand{});
    REQUIRE(wait_until(e, [](const auto& s) { return s.selection.size() == 1; }));
    e.submit(JustifyTextCommand{0, 2});
    REQUIRE(wait_until(e, [](const auto& s) { return justify_of_selection(s) == 0; }));
    REQUIRE(laid_out(e.snapshot(), {0, 0}, 20.0, 20.0 / unit));
    REQUIRE(wait_until(e, [](const auto& s) { return s.grips.size() == 1; }));
    // ... and a Fit text its width, as the text's own width factor.
    e.submit(EraseSelectionCommand{3});
    e.submit(text_at({0, 0}, 2.0, 5, "ABCD", 4, {20, 0}));
    e.submit(SelectAllCommand{});
    REQUIRE(wait_until(e, [](const auto& s) { return s.selection.size() == 1; }));
    e.submit(JustifyTextCommand{2, 5}); // right
    REQUIRE(wait_until(e, [](const auto& s) { return justify_of_selection(s) == 2; }));
    REQUIRE(laid_out(e.snapshot(), {0, 0}, 20.0, 2.0));
    e.stop();
}

TEST_CASE("#42 TXT2MTXT: the lines, top first, become one multiline text") {
    GeometryEngine e;
    e.start();
    e.submit(text_at({0, 5}, 2.0, 0, "two", 1));
    e.submit(text_at({0, 10}, 2.0, 0, "one", 2));
    e.submit(AddLineCommand{{50, 0}, {60, 0}, 3});
    e.submit(SelectAllCommand{});
    REQUIRE(wait_until(e, [](const auto& s) { return s.selection.size() == 3; }));
    std::uint64_t seen = e.snapshot().status_version;
    e.submit(TextToMTextCommand{4});
    REQUIRE(wait_until(e, [&](const auto& s) { return s.status_version != seen; }));
    REQUIRE(e.snapshot().status.find("2 texts converted to 1 multiline text.") != std::string::npos);
    REQUIRE(wait_until(e, [](const auto& s) {
        return s.text_edit_targets.size() == 1 && s.text_edit_targets[0].multiline &&
               s.text_edit_targets[0].content == "one\ntwo";
    }));
    e.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(e, [](const auto& s) { return s.text_edit_targets.size() == 2; }));
    e.stop();
}

TEST_CASE("#42 the files keep the justification, the second point and the width factor") {
    io::Document doc;
    io::DocText a;
    a.pos = {10, 10};
    a.height = 2.0;
    a.justify = 10;
    a.content = "middle";
    a.width_factor = 0.8;
    doc.texts.push_back(a);
    io::DocText b;
    b.pos = {0, 0};
    b.align = {30, 40};
    b.height = 2.0;
    b.justify = 3;
    b.content = "aligned text";
    doc.texts.push_back(b);
    io::DocText c;
    c.pos = {1, 2};
    c.content = "plain";
    doc.texts.push_back(c);
    io::DocAttDef d;
    d.text.pos = {5, 5};
    d.text.align = {15, 5};
    d.text.justify = 5;
    d.text.content = "TAG";
    d.prompt = "Tag?";
    doc.attdefs.push_back(d);
    io::DocBlockDef block;
    block.name = "B";
    io::DocText inside = a;
    inside.justify = 8;
    block.texts.push_back(inside);
    doc.block_defs.push_back(block);

    const std::string native = io::serialize_native(doc);
    io::Document back;
    REQUIRE(io::parse_native(native, back).ok);
    REQUIRE(back.format_version == io::kFormatVersion);
    REQUIRE(back.texts == doc.texts);
    REQUIRE(back.attdefs == doc.attdefs);
    REQUIRE(back.block_defs.size() == 1);
    REQUIRE(back.block_defs[0].texts == block.texts);

    const std::string dxf = io::serialize_dxf(doc);
    // A justified text names the point it is justified on (11 / 21) and its vertical
    // justification (73); its first point is where its baseline starts.
    REQUIRE(dxf.find("\n73\n2\n") != std::string::npos);
    REQUIRE(dxf.find("\n11\n10\n21\n10\n") != std::string::npos);
    REQUIRE(dxf.find("\n41\n0.8\n") != std::string::npos);
    io::Document from_dxf;
    REQUIRE(io::parse_dxf(dxf, from_dxf).ok);
    REQUIRE(from_dxf.texts.size() == 3);
    REQUIRE(from_dxf.texts[0].justify == 10);
    REQUIRE(near(from_dxf.texts[0].pos, {10, 10}));
    REQUIRE(from_dxf.texts[0].width_factor == Approx(0.8));
    REQUIRE(from_dxf.texts[1].justify == 3);
    REQUIRE(near(from_dxf.texts[1].pos, {0, 0}));
    REQUIRE(near(from_dxf.texts[1].align, {30, 40}));
    REQUIRE(from_dxf.texts[2].justify == 0);
    REQUIRE(near(from_dxf.texts[2].pos, {1, 2}));
    REQUIRE(from_dxf.texts[2].width_factor == 1.0);
    REQUIRE(from_dxf.attdefs.size() == 1);
    REQUIRE(from_dxf.attdefs[0].text.justify == 5);
    REQUIRE(near(from_dxf.attdefs[0].text.align, {15, 5}));

    // A file from elsewhere: centred on its second point, not on its first.
    const std::string foreign = "0\nSECTION\n2\nENTITIES\n0\nTEXT\n8\n0\n10\n3.0\n20\n7.0\n40\n2.0\n1\nHi\n"
                                "72\n1\n11\n5.0\n21\n7.0\n0\nENDSEC\n0\nEOF\n";
    io::Document other;
    REQUIRE(io::parse_dxf(foreign, other).ok);
    REQUIRE(other.texts.size() == 1);
    REQUIRE(other.texts[0].justify == 1);
    REQUIRE(near(other.texts[0].pos, {5, 7}));
}

TEST_CASE("#42 TEXT: the settings said, Justify, height and rotation remembered, line after line") {
    H h;
    h.reset_text_defaults();
    h.out.lines.clear();
    h.proc.submit_line("TEXT");
    REQUIRE(h.out.any_contains(
        "Current text style: \"Standard\"  Text height: 2.5000  Annotative: No  Justify: Left"));
    REQUIRE(h.out.prompt == "Specify start point of text or [Justify/Style]: ");
    h.proc.submit_line("J");
    REQUIRE(h.out.prompt ==
            "Enter an option [Left/Center/Right/Align/Middle/Fit/TL/TC/TR/ML/MC/MR/BL/BC/BR]: ");
    h.proc.submit_line("zz");
    REQUIRE(h.out.any_contains("Invalid option keyword."));
    h.proc.submit_line("MC");
    REQUIRE(h.out.prompt == "Specify middle point of text: ");
    h.proc.submit_line("10,10");
    REQUIRE(h.out.prompt == "Specify height <2.5000>: ");
    h.proc.submit_line("5");
    REQUIRE(h.out.prompt == "Specify rotation angle of text <0>: ");
    REQUIRE(!h.proc.wants_free_text());
    h.proc.submit_line("90");
    REQUIRE(h.out.prompt == "Enter text: ");
    REQUIRE(h.proc.wants_free_text());
    REQUIRE(h.proc.preview().kind == musacad::command::PreviewKind::Text);
    REQUIRE(h.proc.preview().text_justify == 10);
    REQUIRE(h.proc.preview().text_height == 5.0);

    h.proc.submit_line("Hello  world "); // as typed, spaces and all
    const auto* first = h.last<AddTextCommand>();
    REQUIRE(first != nullptr);
    REQUIRE(first->content == "Hello  world ");
    REQUIRE(first->pos == Vec2{10, 10});
    REQUIRE(first->height == 5.0);
    REQUIRE(first->rotation == Approx(kPiT / 2.0));
    REQUIRE(first->justify == 10);
    const std::uint64_t first_group = first->group; // `first` points into a vector that grows
    REQUIRE(h.proc.has_active_command());
    // The next line stands under the first: 5 / 3 of the height down the (turned) page.
    h.proc.submit_line("Second");
    const auto* second = h.last<AddTextCommand>();
    REQUIRE(second->content == "Second");
    REQUIRE(second->pos.x == Approx(10.0 + 25.0 / 3.0));
    REQUIRE(second->pos.y == Approx(10.0));
    REQUIRE(second->group != first_group);
    h.proc.submit_line("");
    REQUIRE(!h.proc.has_active_command());
    REQUIRE(h.count<AddTextCommand>() == 2);
    REQUIRE(h.out.any_contains("2 lines of text placed."));

    // Remembered: the justification, the height, the rotation.
    h.out.lines.clear();
    h.run({"DT", "0,0"});
    REQUIRE(h.out.any_contains("Text height: 5.0000  Annotative: No  Justify: Middle center"));
    REQUIRE(h.out.prompt == "Specify height <5.0000>: ");
    h.proc.submit_line("");
    REQUIRE(h.out.prompt == "Specify rotation angle of text <90>: ");
    h.run({"", "again", ""});
    REQUIRE(h.last<AddTextCommand>()->justify == 10);
    REQUIRE(h.last<AddTextCommand>()->height == 5.0);
    h.reset_text_defaults();
}

TEST_CASE("#42 TEXT: Align and Fit take two points; a height or a rotation may be shown by a point") {
    H h;
    h.reset_text_defaults();
    h.run({"TEXT", "J", "A"});
    REQUIRE(h.out.prompt == "Specify first endpoint of text baseline: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompt == "Specify second endpoint of text baseline: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.any_contains("The two endpoints must differ."));
    h.proc.submit_line("10,0");
    REQUIRE(h.out.prompt == "Enter text: "); // no height, no rotation: the points give both
    h.run({"abc", ""});
    REQUIRE(h.last<AddTextCommand>()->justify == 3);
    REQUIRE(h.last<AddTextCommand>()->align == Vec2{10, 0});

    // A justification typed at the start prompt, without going through Justify.
    h.run({"TEXT", "F", "0,0", "0,10"});
    REQUIRE(h.out.prompt == "Specify height <2.5000>: ");
    h.run({"3"});
    REQUIRE(h.out.prompt == "Enter text: "); // Fit keeps the height, its points the direction
    h.run({"abc", "def", ""});
    const auto* fit = h.last<AddTextCommand>();
    REQUIRE(fit->justify == 5);
    REQUIRE(fit->height == 3.0);
    // The second line's two points are both a line further on (to the right of a text
    // running up the page).
    REQUIRE(fit->pos.x == Approx(5.0));
    REQUIRE(fit->align.x == Approx(5.0));
    REQUIRE(fit->align.y == Approx(10.0));

    h.run({"TEXT", "L", "0,0", "3,4", "0,7", "x", ""});
    REQUIRE(h.last<AddTextCommand>()->height == Approx(5.0));
    REQUIRE(h.last<AddTextCommand>()->rotation == Approx(kPiT / 2.0));
    h.run({"TEXT", "0,0", "-1"});
    REQUIRE(h.out.any_contains("Value must be positive and nonzero."));
    h.proc.cancel();

    h.run({"TEXT", "S"});
    REQUIRE(h.out.prompt == "Enter style name or [?] <Standard>: ");
    h.proc.submit_line("nosuch");
    REQUIRE(h.out.any_contains("Cannot find text style \"nosuch\"."));
    h.proc.submit_line("");
    REQUIRE(h.out.prompt == "Specify start point of text or [Justify/Style]: ");
    h.proc.cancel();
    h.reset_text_defaults();
}

TEST_CASE("#42 TEXTEDIT asks for one object after another; Undo and Mode") {
    H h;
    h.run({"TEXTEDITMODE", "0"});
    h.out.lines.clear();
    h.proc.submit_line("ED");
    REQUIRE(h.out.any_contains("Current settings: Edit mode = Multiple"));
    REQUIRE(h.out.prompt == "Select an annotation object or [Undo/Mode]: ");
    h.proc.submit_line("U");
    REQUIRE(h.out.any_contains("Nothing to undo."));
    h.proc.submit_line("5,5");
    REQUIRE(h.out.prompt == "Enter new text: ");
    REQUIRE(h.proc.wants_free_text());
    h.proc.submit_line("New  text");
    REQUIRE(h.last<EditTextContentCommand>()->content == "New  text");
    REQUIRE(h.out.prompt == "Select an annotation object or [Undo/Mode]: ");
    h.proc.submit_line("U");
    REQUIRE(h.count<UndoLastGroupCommand>() == 1);
    h.proc.submit_line("M");
    REQUIRE(h.out.prompt == "Enter a text edit mode option [Single/Multiple] <Multiple>: ");
    h.proc.submit_line("S");
    h.run({"1,1", "once"});
    REQUIRE(!h.proc.has_active_command()); // Single: one object and done
    h.proc.submit_line("TEXTEDITMODE");
    REQUIRE(h.out.prompt == "Enter new value for TEXTEDITMODE <1>: ");
    h.proc.submit_line("0");
    h.run({"ED", ""});
    REQUIRE(!h.proc.has_active_command());
}

TEST_CASE("#42 JUSTIFYTEXT, SCALETEXT and TXT2MTXT: what they ask and what they submit") {
    H h;
    h.proc.set_selection_count(2);
    h.proc.submit_line("JUSTIFYTEXT");
    REQUIRE(h.out.prompt == "Enter a justification option "
                            "[Left/Align/Fit/Center/Middle/Right/TL/TC/TR/ML/MC/MR/BL/BC/BR] <Left>: ");
    h.proc.submit_line("q");
    REQUIRE(h.proc.has_active_command());
    h.proc.submit_line("mc");
    REQUIRE(h.last<JustifyTextCommand>()->justify == 10);
    REQUIRE(!h.proc.has_active_command());
    h.run({"JUSTIFYTEXT", "L"});

    h.proc.submit_line("SCALETEXT");
    REQUIRE(h.out.prompt == "Enter a base point option for scaling "
                            "[Existing/Left/Center/Middle/Right/TL/TC/TR/ML/MC/MR/BL/BC/BR] <Existing>: ");
    h.proc.submit_line("BL");
    REQUIRE(h.out.prompt.rfind("Specify new model height or [Paper height/Match object/Scale factor] <", 0) == 0);
    h.proc.submit_line("5");
    const auto* s = h.last<ScaleTextCommand>();
    REQUIRE(s->base == 13);
    REQUIRE(s->mode == 0);
    REQUIRE(s->value == 5.0);

    h.run({"SCALETEXT", "", "S"});
    REQUIRE(h.out.prompt.rfind("Specify scale factor or [Reference] <", 0) == 0);
    h.run({"R", "2", "5"});
    REQUIRE(h.last<ScaleTextCommand>()->base == 0);
    REQUIRE(h.last<ScaleTextCommand>()->mode == 1);
    REQUIRE(h.last<ScaleTextCommand>()->value == Approx(2.5));

    h.run({"SCALETEXT", "E", "M"});
    REQUIRE(h.out.prompt == "Select a text object with the desired height: ");
    h.proc.submit_line("3,4");
    REQUIRE(h.last<ScaleTextCommand>()->mode == 2);
    REQUIRE(h.last<ScaleTextCommand>()->match_pick == Vec2{3, 4});
    h.run({"SCALETEXT", "", "2.5"}); // the remembered height back

    h.proc.submit_line("TXT2MTXT");
    REQUIRE(h.count<TextToMTextCommand>() == 1);
    REQUIRE(!h.proc.has_active_command());

    // With nothing selected each starts at Select objects:.
    H n;
    n.proc.submit_line("JUSTIFYTEXT");
    REQUIRE(n.out.prompt == "Select objects: ");
    n.proc.cancel();
}

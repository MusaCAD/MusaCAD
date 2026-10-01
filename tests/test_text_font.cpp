// Part A: the single-stroke vector font covers the required character set and
// lays text out (width, justification, rotation) as world-space segments.

#include <algorithm>
#include <cstdint>
#include <ios>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/text/stroke_font.hpp"

using namespace musacad::core;
using namespace musacad::core::text;
using Catch::Approx;

TEST_CASE("Every printable ASCII glyph produces strokes") {
    // The whole printable range, not a curated subset. The old list covered only
    // alphanumerics plus ".,-+/<>()", which hid that '%', '!', '?', '$', '&', '@',
    // '[', ']', '{', '}', '^', '_', '`', '|' and '~' had NO glyph at all: they drew
    // blank with an advance, so "50% FULL" plotted as "50 FULL".
    for (char c = 0x21; c <= 0x7E; ++c) { // 0x20 (space) is legitimately blank
        std::vector<Vec2> segs;
        append_text_segments(std::string(1, c), {0, 0}, 1.0, 0.0, Justify::Left, segs);
        INFO("glyph: " << c << " (0x" << std::hex << static_cast<int>(c) << ")");
        REQUIRE(segs.size() >= 2); // at least one segment
        REQUIRE(segs.size() % 2 == 0);
    }
    // Space is the one printable character that is deliberately blank, but it advances.
    std::vector<Vec2> sp;
    append_text_segments(" ", {0, 0}, 1.0, 0.0, Justify::Left, sp);
    REQUIRE(sp.empty());
    REQUIRE(text_width(" ", 1.0) > 0.0);
}

TEST_CASE("CAD symbols (degree, plus-minus, diameter) render") {
    for (const char* sym : {"°", "±", "⌀"}) {
        std::vector<Vec2> segs;
        append_text_segments(sym, {0, 0}, 1.0, 0.0, Justify::Left, segs);
        REQUIRE(segs.size() >= 2);
    }
}

namespace {

/// UTF-8 encodes one code point, so the tests below can name symbols by their
/// standard code point rather than by an unreadable byte string.
std::string utf8(char32_t cp) {
    std::string s;
    if (cp <= 0x7F) {
        s += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

/// The drafting symbol set the font promises (stroke_font.hpp documents the same
/// list). Declared here independently so the test states the CONTRACT rather than
/// echoing whatever the implementation happens to contain.
constexpr char32_t kDraftingSymbols[] = {
    // Hole callouts.
    0x2334, 0x21A7, 0x2335,
    // GD&T characteristics.
    0x23E4, 0x23E5, 0x25CB, 0x232D, 0x2312, 0x2313, 0x2220, 0x27C2, 0x2225, 0x2316, 0x25CE,
    0x232F, 0x2197, 0x2330,
    // Material condition / modifiers.
    0x24C2, 0x24C1, 0x24C8, 0x24C5, 0x24BB, 0x24C9, 0x24CA,
    // Feature form.
    0x25A1, 0x2332, 0x2333,
};

} // namespace

TEST_CASE("Every declared drafting symbol renders real stroke geometry") {
    for (const char32_t cp : kDraftingSymbols) {
        std::vector<Vec2> segs;
        append_text_segments(utf8(cp), {0, 0}, 2.5, 0.0, Justify::Left, segs);
        INFO("code point: U+" << std::hex << static_cast<std::uint32_t>(cp));
        REQUIRE(segs.size() >= 2);      // not blank
        REQUIRE(segs.size() % 2 == 0);  // whole segments (pairs of endpoints)
    }
}

TEST_CASE("Drafting symbols honour the monospace advance and the 6x8 cell") {
    // THE invariant the font rests on: one shared advance drives text_width, layout,
    // bounds and pick. A symbol that measured differently would desynchronise all four.
    const double one = text_width("A", 2.5);
    REQUIRE(one > 0.0);
    for (const char32_t cp : kDraftingSymbols) {
        INFO("code point: U+" << std::hex << static_cast<std::uint32_t>(cp));
        REQUIRE(text_width(utf8(cp), 2.5) == Approx(one));
        // ... and in a run: N glyphs are exactly N advances, whatever they are.
        REQUIRE(text_width(utf8(cp) + "12", 2.5) == Approx(3.0 * one));
    }

    // Geometry stays inside the cell the letters use: x within one advance of the pen,
    // y between the descender and the cap. (Checked against 'A'/'g' rather than magic
    // numbers, so it tracks the font's own metrics.)
    std::vector<Vec2> ref;
    append_text_segments("Ag", {0, 0}, 2.5, 0.0, Justify::Left, ref);
    double ymin = 1e9;
    double ymax = -1e9;
    for (const Vec2& v : ref) {
        ymin = std::min(ymin, v.y);
        ymax = std::max(ymax, v.y);
    }
    for (const char32_t cp : kDraftingSymbols) {
        std::vector<Vec2> segs;
        append_text_segments(utf8(cp), {0, 0}, 2.5, 0.0, Justify::Left, segs);
        INFO("code point: U+" << std::hex << static_cast<std::uint32_t>(cp));
        for (const Vec2& v : segs) {
            REQUIRE(v.x >= -1e-9);
            REQUIRE(v.x <= one + 1e-9);
            REQUIRE(v.y >= ymin - 1e-9);
            REQUIRE(v.y <= ymax + 1e-9);
        }
    }
}

TEST_CASE("Units and typography: em dash, multiplication sign, omega, micro (#79)") {
    // "test_pcb — LED test board", "40 × 30 mm", "750 Ω", "35 µm": each one a glyph of
    // its own, at the one advance, inside the letters' cell.
    std::vector<Vec2> ref;
    append_text_segments("Ag", {0, 0}, 2.5, 0.0, Justify::Left, ref);
    double ymin = 1e9;
    double ymax = -1e9;
    for (const Vec2& v : ref) {
        ymin = std::min(ymin, v.y);
        ymax = std::max(ymax, v.y);
    }
    const double one = text_width("A", 2.5);
    for (const char32_t cp : {char32_t{0x2014}, char32_t{0x00D7}, char32_t{0x03A9}, char32_t{0x2126},
                              char32_t{0x00B5}, char32_t{0x03BC}}) {
        INFO("code point: U+" << std::hex << static_cast<std::uint32_t>(cp));
        REQUIRE(has_glyph(cp));
        std::vector<Vec2> segs;
        append_text_segments(utf8(cp), {0, 0}, 2.5, 0.0, Justify::Left, segs);
        REQUIRE(segs.size() >= 2);
        REQUIRE(segs.size() % 2 == 0);
        REQUIRE(text_width(utf8(cp), 2.5) == Approx(one));
        for (const Vec2& v : segs) {
            REQUIRE(v.x >= -1e-9);
            REQUIRE(v.x <= one + 1e-9);
            REQUIRE(v.y >= ymin - 1e-9);
            REQUIRE(v.y <= ymax + 1e-9);
        }
    }
    // The ohm sign is omega, and mu is the micro sign: the same strokes.
    const auto strokes = [](char32_t cp) {
        std::vector<Vec2> s;
        append_text_segments(utf8(cp), {0, 0}, 2.5, 0.0, Justify::Left, s);
        return s;
    };
    REQUIRE(strokes(0x2126).size() == strokes(0x03A9).size());
    REQUIRE(strokes(0x03BC).size() == strokes(0x00B5).size());
    // The multiplication sign is not the letter x: smaller, and off the baseline.
    double times_low = 1e9;
    for (const Vec2& v : strokes(0x00D7)) {
        times_low = std::min(times_low, v.y);
    }
    REQUIRE(times_low > 0.1);
    // The micro sign reaches below the baseline (its descender); the em dash is one
    // stroke wider than the hyphen.
    double micro_low = 1e9;
    for (const Vec2& v : strokes(0x00B5)) {
        micro_low = std::min(micro_low, v.y);
    }
    REQUIRE(micro_low < -0.5);
    const std::vector<Vec2> dash = strokes(0x2014);
    const std::vector<Vec2> hyphen = strokes('-');
    REQUIRE(std::abs(dash[1].x - dash[0].x) > std::abs(hyphen[1].x - hyphen[0].x));
}

TEST_CASE("has_glyph: what the font draws, and what it would leave blank") {
    for (char c = 0x21; c <= 0x7E; ++c) {
        REQUIRE(has_glyph(static_cast<char32_t>(c)));
    }
    REQUIRE(has_glyph(U' '));
    REQUIRE(has_glyph(0x00B0));
    REQUIRE(has_glyph(0x2300));
    REQUIRE_FALSE(has_glyph(0x2603)); // a snowman
    REQUIRE_FALSE(has_glyph(0x4E2D)); // a CJK ideograph
}

TEST_CASE("An unmapped code point is blank, never a wrong glyph") {
    std::vector<Vec2> segs;
    append_text_segments(utf8(0x2603), {0, 0}, 2.5, 0.0, Justify::Left, segs); // a snowman
    REQUIRE(segs.empty());
    // ... but it still advances, so layout does not silently collapse.
    REQUIRE(text_width(utf8(0x2603), 2.5) == Approx(text_width("A", 2.5)));
}

TEST_CASE("Width is monospace and scales with height; space advances") {
    REQUIRE(text_width("", 10.0) == Approx(0.0));
    const double one = text_width("A", 10.0);
    REQUIRE(one > 0.0);
    REQUIRE(text_width("AB", 10.0) == Approx(2.0 * one));
    REQUIRE(text_width("A", 20.0) == Approx(2.0 * one)); // scales with height
    REQUIRE(text_width(" ", 10.0) > 0.0);                // space advances
}

TEST_CASE("Justification shifts the run; rotation transforms it") {
    std::vector<Vec2> left;
    std::vector<Vec2> center;
    append_text_segments("123", {0, 0}, 1.0, 0.0, Justify::Left, left);
    append_text_segments("123", {0, 0}, 1.0, 0.0, Justify::Center, center);
    REQUIRE(left.size() == center.size());
    const double w = text_width("123", 1.0);
    // Center run is shifted left by half the width.
    REQUIRE(center[0].x == Approx(left[0].x - w / 2.0));

    std::vector<Vec2> rot;
    append_text_segments("1", {0, 0}, 1.0, 1.5707963267948966, Justify::Left, rot); // 90 deg
    std::vector<Vec2> flat;
    append_text_segments("1", {0, 0}, 1.0, 0.0, Justify::Left, flat);
    // 90-degree rotation maps local (x,y) -> (-y, x): x-extent becomes y-extent.
    double flat_max_x = 0.0;
    double rot_max_y = 0.0;
    for (const Vec2& p : flat) {
        flat_max_x = std::max(flat_max_x, p.x);
    }
    for (const Vec2& p : rot) {
        rot_max_y = std::max(rot_max_y, p.y);
    }
    REQUIRE(rot_max_y == Approx(flat_max_x).margin(1e-9));
}

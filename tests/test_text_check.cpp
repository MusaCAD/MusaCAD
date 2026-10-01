// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Issue #80: `musacad --check` reports text whose letters overlap other text, leave the
// frame, are crossed by a line (--lines), or use a character the font draws blank. The
// boxes are the letters' own, laid out as the plot lays them out.

#include <cmath>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/text/stroke_font.hpp"
#include "musacad/core/text_check.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {

io::Document base() {
    io::Document doc;
    const Layer zero{"0", {255, 255, 255}, Linetype::Continuous, 25, true, false, false};
    const Layer off{"HIDDEN", {255, 255, 255}, Linetype::Continuous, 25, false, false, false};
    doc.layers = {zero, off}; // a Document starts with layer 0 of its own
    return doc;
}

io::DocText a_text(Vec2 at, const char* s, double h = 2.5, double rotation = 0.0) {
    io::DocText t;
    t.pos = at;
    t.height = h;
    t.rotation = rotation;
    t.content = s;
    return t;
}

TextCheckReport run(const io::Document& doc, TextCheckOptions o = {}) {
    GeometryStore store;
    io::populate_store(store, doc);
    const NativeKernel2D kernel;
    return check_text(store, kernel, o);
}

int count(const TextCheckReport& r, TextProblemKind k) {
    int n = 0;
    for (const TextProblem& p : r.problems) {
        n += p.kind == k ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("#80 letters over letters: reported once, side by side is fine") {
    io::Document doc = base();
    doc.texts.push_back(a_text({0, 0}, "R1 750"));
    doc.texts.push_back(a_text({4, 0.5}, "C3")); // on top of "750"
    doc.texts.push_back(a_text({0, 10}, "AB"));
    doc.texts.push_back(a_text({text::text_width("AB", 2.5) + 0.2, 10}, "CD")); // just after it
    const TextCheckReport r = run(doc);
    REQUIRE(r.runs.size() == 4);
    REQUIRE(count(r, TextProblemKind::Overlap) == 1);
    const TextProblem& p = r.problems.front();
    REQUIRE(r.runs[p.run].text == "R1 750");
    REQUIRE(r.runs[p.other].text == "C3");
}

TEST_CASE("#80 the box is the letters': a turned text is checked along its own axes") {
    io::Document doc = base();
    doc.texts.push_back(a_text({0, 0}, "ABCDEFGH", 2.5, std::atan(1.0))); // up the diagonal
    // Inside the diagonal text's bounds, but well clear of its letters.
    doc.texts.push_back(a_text({8, 0}, "X", 1.0));
    REQUIRE(count(run(doc), TextProblemKind::Overlap) == 0);
    // ... and one across it.
    doc.texts.push_back(a_text({2, 4}, "ACROSS", 1.0));
    REQUIRE(count(run(doc), TextProblemKind::Overlap) == 1);
}

TEST_CASE("#80 the lines of one MTEXT are one text; a TEXT over one of them is not") {
    io::Document doc = base();
    io::DocMText m;
    m.block.pos = {0, 20};
    m.block.height = 2.5;
    m.content = "FIRST LINE\nSECOND LINE";
    doc.mtexts.push_back(m);
    TextCheckReport r = run(doc);
    REQUIRE(r.runs.size() == 2);
    REQUIRE(r.runs[0].entity == r.runs[1].entity);
    REQUIRE(r.problems.empty());
    doc.texts.push_back(a_text({r.runs[1].lo.x + 1.0, r.runs[1].lo.y + 0.5}, "OVER"));
    r = run(doc);
    REQUIRE(count(r, TextProblemKind::Overlap) == 1);
    REQUIRE(r.runs[r.problems.front().run].type != r.runs[r.problems.front().other].type);
}

TEST_CASE("#80 the frame: the largest rectangle, or the window; outside is reported") {
    io::Document doc = base();
    io::DocPolyline border;
    border.points = {{0, 0}, {100, 0}, {100, 50}, {0, 50}};
    border.closed = true;
    io::DocPolyline cell; // a title block cell: a rectangle too, but not the frame
    cell.points = {{60, 0}, {100, 0}, {100, 10}, {60, 10}, {60, 0}};
    doc.polylines = {border, cell};
    doc.texts.push_back(a_text({62, 4}, "TITLE"));
    doc.texts.push_back(a_text({92, 40}, "OUTSIDE"));
    TextCheckReport r = run(doc);
    REQUIRE(r.frames.size() == 1);
    REQUIRE(r.frames[0].hi.x == Approx(100.0));
    REQUIRE(r.frames[0].hi.y == Approx(50.0));
    REQUIRE_FALSE(r.frames[0].from_window);
    REQUIRE(count(r, TextProblemKind::OutsideFrame) == 1);
    REQUIRE(r.runs[r.problems.front().run].text == "OUTSIDE");

    TextCheckOptions wide;
    wide.window = std::pair<Vec2, Vec2>{{200, 100}, {0, 0}}; // corners in any order
    r = run(doc, wide);
    REQUIRE(r.frames.size() == 1);
    REQUIRE(r.frames[0].from_window);
    REQUIRE(r.problems.empty());

    // No rectangle, no window: no frame, nothing outside it.
    doc.polylines.clear();
    r = run(doc);
    REQUIRE(r.frames.empty());
    REQUIRE(r.problems.empty());
}

TEST_CASE("#80 --lines: a line through the letters; under them, or a dimension's own, is not") {
    io::Document doc = base();
    doc.texts.push_back(a_text({0, 0}, "ABC")); // no descenders: the letters stand on the baseline
    io::DocLine through;
    through.a = {-5, 1};
    through.b = {20, 1};
    io::DocLine under;
    under.a = {-5, -0.3};
    under.b = {20, -0.3};
    doc.lines = {under};
    io::DocDim dim; // its value sits over its own dimension line
    dim.type = 0;
    dim.a = {0, 30};
    dim.b = {40, 30};
    dim.line_pt = {20, 40};
    dim.aux = kPi;
    doc.dims.push_back(dim);
    TextCheckOptions o;
    o.lines = true;
    TextCheckReport r = run(doc, o);
    REQUIRE(r.runs.size() == 2);
    REQUIRE(count(r, TextProblemKind::CrossesLine) == 0);
    doc.lines.push_back(through);
    r = run(doc, o);
    REQUIRE(count(r, TextProblemKind::CrossesLine) == 1);
    REQUIRE(r.problems.front().line_type == "LINE");
    // Without --lines, lines are not looked at.
    REQUIRE(count(run(doc), TextProblemKind::CrossesLine) == 0);
}

TEST_CASE("#80 characters the font draws blank; hidden layers are not checked") {
    io::Document doc = base();
    doc.texts.push_back(a_text({0, 0}, "40 × 30 mm — 750 Ω, 35 µm")); // #79: all drawn
    doc.texts.push_back(a_text({0, 10}, "SNOW ☃ ☃"));
    io::DocText hidden = a_text({0, 0}, "ON TOP ☃");
    hidden.props.layer = 1; // an off layer
    doc.texts.push_back(hidden);
    const TextCheckReport r = run(doc);
    REQUIRE(r.runs.size() == 2);
    REQUIRE(count(r, TextProblemKind::MissingGlyph) == 1);
    REQUIRE(count(r, TextProblemKind::Overlap) == 0);
    const TextProblem& p = r.problems.front();
    REQUIRE(r.runs[p.run].text == "SNOW ☃ ☃");
    REQUIRE(p.missing.size() == 1); // once, however often it is used
    REQUIRE(p.missing[0] == 0x2603);
}

TEST_CASE("#80 a dimension's value is text like any other") {
    io::Document doc = base();
    io::DocDim dim;
    dim.type = 0;
    dim.a = {0, 0};
    dim.b = {40, 0};
    dim.line_pt = {20, 10};
    dim.aux = kPi;
    doc.dims.push_back(dim);
    TextCheckReport r = run(doc);
    REQUIRE(r.runs.size() == 1);
    REQUIRE(r.runs[0].type == "DIMENSION");
    REQUIRE(r.runs[0].text == "40.00");
    doc.texts.push_back(a_text({r.runs[0].lo.x, r.runs[0].lo.y + 0.5}, "NOTE"));
    r = run(doc);
    REQUIRE(count(r, TextProblemKind::Overlap) == 1);
}

TEST_CASE("#80 the JSON report: the problems, the texts they name, escaped") {
    io::Document doc = base();
    doc.texts.push_back(a_text({0, 0}, "SAY \"HI\""));
    doc.texts.push_back(a_text({1, 0.5}, "BACK\\SLASH"));
    io::DocPolyline border;
    border.points = {{-10, -10}, {100, -10}, {100, 50}, {-10, 50}};
    border.closed = true;
    doc.polylines.push_back(border);
    const TextCheckReport r = run(doc);
    const std::string j = text_check_json(r, "part \"1\".musa");
    REQUIRE(j.find("\"file\": \"part \\\"1\\\".musa\"") != std::string::npos);
    REQUIRE(j.find("\"kind\": \"overlap\"") != std::string::npos);
    REQUIRE(j.find("\"text\": \"SAY \\\"HI\\\"\"") != std::string::npos);
    REQUIRE(j.find("\"text\": \"BACK\\\\SLASH\"") != std::string::npos);
    REQUIRE(j.find("\"with\": {") != std::string::npos);
    REQUIRE(j.find("\"from\": \"rectangle\"") != std::string::npos);
    REQUIRE(j.find("\"problems_found\": 1") != std::string::npos);
    const std::string t = text_check_text(r, "part.musa");
    REQUIRE(t.rfind("part.musa: overlap: TEXT \"SAY \"HI\"\"", 0) == 0);

    io::Document clean = base();
    clean.texts.push_back(a_text({0, 0}, "ALONE"));
    const TextCheckReport c = run(clean);
    REQUIRE(c.problems.empty());
    REQUIRE(text_check_text(c, "x").empty());
    REQUIRE(text_check_json(c, "x").find("\"problems\": []") != std::string::npos);
}

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::core {

class GeometryStore;
class IGeometryKernel;

/// One run of letters as the drawing shows it: a TEXT, an attribute definition's tag,
/// one line of an MTEXT or of a multileader's text, a dimension's value, a leader's
/// label. Its box is the box of the letters themselves -- the strokes (or, in an outline
/// font, the filled glyphs) the plot draws, in the run's own direction -- not the cell a
/// line of text would take.
struct TextRun {
    std::string type;       ///< TEXT, ATTDEF, MTEXT, MLEADER, DIMENSION or LEADER
    std::string layer;
    std::string text;       ///< as drawn (control codes expanded)
    std::string font;       ///< the font's name ("" = the built-in stroke font)
    std::uint8_t space = 0; ///< 0 = model space, otherwise a layout's id
    std::size_t entity = 0; ///< which entity: the lines of one MTEXT share it
    Vec2 box[4]{};          ///< baseline-left, baseline-right, top-right, top-left (world)
    Vec2 lo{};              ///< the box's bounds
    Vec2 hi{};
};

enum class TextProblemKind : std::uint8_t {
    Overlap,      ///< its letters overlap another text's letters
    OutsideFrame, ///< its letters reach outside the frame (the drawing's border, or a window)
    CrossesLine,  ///< a line, an arc, a curve or a block's line crosses its letters
    MissingGlyph, ///< the font has no glyph for a character it uses: it is drawn blank
};

struct TextProblem {
    TextProblemKind kind = TextProblemKind::Overlap;
    std::size_t run = 0;    ///< index into TextCheckReport::runs
    std::size_t other = 0;  ///< Overlap: the other run
    std::string line_type;  ///< CrossesLine: LINE, POLYLINE, ARC, CIRCLE, ELLIPSE, SPLINE, INSERT
    std::string line_layer;
    Vec2 line_a{};          ///< ... the segment found crossing the letters
    Vec2 line_b{};
    std::vector<char32_t> missing; ///< MissingGlyph: each character once
};

/// The rectangle a space's text must stay inside: the window asked for, or the largest
/// closed axis-aligned rectangle (a closed polyline of four corners) drawn in that space.
struct TextCheckFrame {
    std::uint8_t space = 0;
    Vec2 lo{};
    Vec2 hi{};
    bool from_window = false;
};

struct TextCheckOptions {
    bool lines = false; ///< also report texts whose letters a line crosses
    /// The frame for every space, as `--plot --window` gives a plot area; unset, each
    /// space's largest rectangle (no frame check in a space without one).
    std::optional<std::pair<Vec2, Vec2>> window;
};

struct TextCheckReport {
    std::vector<TextRun> runs;
    std::vector<TextCheckFrame> frames;
    std::vector<TextProblem> problems;
};

/// Checks the visible text of `store` (layers on and thawed, objects not hidden): letters
/// over letters, letters outside the frame, characters the font draws blank, and with
/// `options.lines` lines through letters. Letters are measured with the store's font
/// engine when it has one, else with the stroke font (as a store with no engine draws).
/// Two runs of one entity never count as overlapping each other; a dimension's lines and
/// a leader's line are not lines here, so a value on its own dimension line is fine.
[[nodiscard]] TextCheckReport check_text(const GeometryStore& store, const IGeometryKernel& kernel,
                                         const TextCheckOptions& options);

/// The report as one JSON object (see docs/CLI.md): the file, the frames, every run
/// checked and every problem, each naming the runs and the line it involves.
[[nodiscard]] std::string text_check_json(const TextCheckReport& report, std::string_view file);

/// The problems as lines for a person, each starting with `file: `; empty when none.
[[nodiscard]] std::string text_check_text(const TextCheckReport& report, std::string_view file);

} // namespace musacad::core

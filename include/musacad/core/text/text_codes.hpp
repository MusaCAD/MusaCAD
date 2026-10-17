// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace musacad::core::text {

/// A byte range [begin, end) into a SubstitutedText::text string. Used to mark the
/// runs that an overline / underline toggle covered.
struct DecorSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
};

/// The result of expanding AutoCAD text control codes: the visible UTF-8 string plus
/// the over/under-line decoration runs (the toggles themselves are removed).
struct SubstitutedText {
    std::string text;
    std::vector<DecorSpan> overline;
    std::vector<DecorSpan> underline;

    [[nodiscard]] bool has_decor() const noexcept {
        return !overline.empty() || !underline.empty();
    }
};

/// Expand AutoCAD text control codes to Unicode. This is a RENDER/LAYOUT-time pass --
/// the entity stores the RAW string with codes intact (derived-not-baked, Ph16/20/23),
/// so editing re-shows the codes and save/load round-trips them. The ONE substitution
/// function shared by the single-line TEXT, the paragraph MTEXT, and the Leader/MLeader
/// labels. Codes (case-insensitive letter):
///   %%d -> U+00B0 degree, %%p -> U+00B1 plus-minus, %%c -> U+2300 diameter,
///   %%b -> U+2334 counterbore/spotface, %%h -> U+21A7 depth, %%v -> U+2335 countersink,
///   %%% -> literal '%', %%nnn -> Latin-1 char nnn (1-3 decimal digits),
///   %%o / %%u -> overline / underline TOGGLE (removed; the covered run is recorded),
///   \U+XXXX -> that code point (4 hex digits).
///
/// `\U+XXXX` is the GENERAL escape and works everywhere, not only in MTEXT: the stroke
/// font carries far more symbols than there are free %%-letters (the GD&T
/// characteristics, the material-condition modifiers), and a symbol reachable in a
/// paragraph but not in a single-line TEXT or a leader would be an arbitrary gap.
/// A malformed escape stays literal.
[[nodiscard]] SubstitutedText substitute_text_codes(std::string_view raw);

/// FIELDS (AutoCAD FIELD): `%<Date>%`, `%<Time>%`, `%<Filename>%` and `%<Login>%` in
/// any text are replaced at layout time with the values the engine publishes before
/// each rebuild (set_field_context); an unknown field shows as "####", as AutoCAD
/// shows an invalid one. Expansion runs before the %% / \U+ codes.
///
/// AutoCAD's own form is read too: `%<\AcVar Name \f "format">%`. Date, CreateDate and
/// SaveDate take a date format (M, MM, MMM, MMMM, d, dd, ddd, dddd, yy, yyyy, h, hh, H,
/// HH, m, mm, s, ss, tt); Filename takes "%fn2" (the name), "%fn6" (with its path) or
/// "%fn1" (the name without its extension); the text fields take %tc1 (UPPERCASE), %tc2
/// (lowercase), %tc3 (First capital) and %tc4 (Title Case). Title, Subject, Author,
/// Keywords, Comments, HyperlinkBase and CustomDP.<name> are the drawing's properties
/// (DWGPROPS); one with no value shows "----", as AutoCAD shows it.
struct FieldContext {
    std::string date;
    std::string time;
    std::string filename;
    std::string login;
    std::int64_t now = 0;     ///< seconds since the epoch (a Date with a format)
    std::int64_t created = 0; ///< CreateDate (0: not known)
    std::int64_t saved = 0;   ///< SaveDate (0: not saved yet)
    std::string path{};       ///< the drawing's file, with its folder
    std::string title{};
    std::string subject{};
    std::string author{};
    std::string keywords{};
    std::string comments{};
    std::string hyperlink_base{};
    std::vector<std::pair<std::string, std::string>> custom{}; ///< CustomDP: name / value
};
/// A time (seconds since the epoch, local time) in AutoCAD's date format tokens.
[[nodiscard]] std::string format_date(std::int64_t when, std::string_view format);
void set_field_context(FieldContext ctx);
[[nodiscard]] FieldContext field_context();
/// The context for a drawing at `document_path` right now: today's date, the time to the
/// minute, the file name (or "Drawing1" for an unsaved drawing) and the login name. The
/// engine publishes it before every rebuild; the headless plot before its one build.
[[nodiscard]] FieldContext make_field_context(std::string_view document_path);
[[nodiscard]] std::string expand_fields(std::string_view raw);

/// Convenience wrapper returning only the visible string (for measurement / callers
/// that do not draw the over/under-line decoration).
[[nodiscard]] std::string substitute_text(std::string_view raw);

} // namespace musacad::core::text

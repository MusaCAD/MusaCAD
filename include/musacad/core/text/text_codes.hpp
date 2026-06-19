// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
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
///   %%% -> literal '%', %%nnn -> Latin-1 char nnn (1-3 decimal digits),
///   %%o / %%u -> overline / underline TOGGLE (removed; the covered run is recorded).
/// When `mtext` is true the MTEXT-only Unicode escape `\U+XXXX` (4 hex digits) is also
/// expanded.
[[nodiscard]] SubstitutedText substitute_text_codes(std::string_view raw, bool mtext = false);

/// Convenience wrapper returning only the visible string (for measurement / callers
/// that do not draw the over/under-line decoration).
[[nodiscard]] std::string substitute_text(std::string_view raw, bool mtext = false);

} // namespace musacad::core::text

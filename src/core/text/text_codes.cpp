// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/text/text_codes.hpp"

#include <cctype>
#include <cstdint>

namespace musacad::core::text {

namespace {

void append_utf8(std::string& out, char32_t cp) {
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    const char l = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l >= 'a' && l <= 'f') {
        return 10 + (l - 'a');
    }
    return -1;
}

} // namespace

SubstitutedText substitute_text_codes(std::string_view in, bool mtext) {
    SubstitutedText r;
    r.text.reserve(in.size());

    bool over = false;
    bool under = false;
    std::size_t over_start = 0;
    std::size_t under_start = 0;
    const auto toggle = [&](bool& flag, std::size_t& start, std::vector<DecorSpan>& spans) {
        if (flag) {
            if (r.text.size() > start) {
                spans.push_back({start, r.text.size()});
            }
            flag = false;
        } else {
            flag = true;
            start = r.text.size();
        }
    };

    for (std::size_t i = 0; i < in.size();) {
        // AutoCAD %%-codes -------------------------------------------------------
        if (in[i] == '%' && i + 2 < in.size() && in[i + 1] == '%') {
            const char c = in[i + 2];
            const char lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lc == 'd') {
                append_utf8(r.text, 0x00B0); // degree
                i += 3;
                continue;
            }
            if (lc == 'p') {
                append_utf8(r.text, 0x00B1); // plus-minus
                i += 3;
                continue;
            }
            if (lc == 'c') {
                append_utf8(r.text, 0x2300); // diameter
                i += 3;
                continue;
            }
            if (c == '%') {
                r.text += '%'; // literal percent
                i += 3;
                continue;
            }
            if (lc == 'o') {
                toggle(over, over_start, r.overline);
                i += 3;
                continue;
            }
            if (lc == 'u') {
                toggle(under, under_start, r.underline);
                i += 3;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                // %%nnn -> Latin-1 character (1..3 decimal digits).
                std::size_t j = i + 2;
                int val = 0;
                int n = 0;
                while (j < in.size() && n < 3 && std::isdigit(static_cast<unsigned char>(in[j]))) {
                    val = val * 10 + (in[j] - '0');
                    ++j;
                    ++n;
                }
                if (val > 0 && val <= 0xFF) {
                    append_utf8(r.text, static_cast<char32_t>(val));
                } else {
                    // Out of range (0 or >255): keep the sequence literal -- never emit a
                    // wrapped glyph or an embedded NUL.
                    r.text.append(in.substr(i, j - i));
                }
                i = j;
                continue;
            }
            // Unknown %%x: keep one literal '%' and re-scan from the next char.
            r.text += in[i];
            ++i;
            continue;
        }
        // MTEXT-only Unicode escape \U+XXXX -------------------------------------
        if (mtext && in[i] == '\\' && i + 2 < in.size() &&
            (in[i + 1] == 'U' || in[i + 1] == 'u') && in[i + 2] == '+') {
            std::size_t j = i + 3;
            unsigned cp = 0;
            int n = 0;
            while (j < in.size() && n < 4 && hex_value(in[j]) >= 0) {
                cp = cp * 16u + static_cast<unsigned>(hex_value(in[j]));
                ++j;
                ++n;
            }
            if (n == 4) {
                append_utf8(r.text, static_cast<char32_t>(cp));
                i = j;
                continue;
            }
            r.text += in[i]; // not a valid escape -> literal backslash
            ++i;
            continue;
        }
        r.text += in[i];
        ++i;
    }

    // A toggle left open at end-of-string decorates to the end.
    if (over && r.text.size() > over_start) {
        r.overline.push_back({over_start, r.text.size()});
    }
    if (under && r.text.size() > under_start) {
        r.underline.push_back({under_start, r.text.size()});
    }
    return r;
}

std::string substitute_text(std::string_view in, bool mtext) {
    return substitute_text_codes(in, mtext).text;
}

} // namespace musacad::core::text

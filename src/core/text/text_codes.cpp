// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/text/text_codes.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <utility>

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

namespace {
std::mutex g_field_mutex;
FieldContext g_fields;
std::string upper_ascii(std::string_view s) {
    std::string u(s);
    for (char& c : u) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return u;
}
} // namespace

void set_field_context(FieldContext ctx) {
    const std::scoped_lock lock(g_field_mutex);
    g_fields = std::move(ctx);
}

FieldContext field_context() {
    const std::scoped_lock lock(g_field_mutex);
    return g_fields;
}

FieldContext make_field_context(std::string_view document_path) {
    FieldContext fc;
    const std::time_t now = std::time(nullptr);
    std::tm tmv{};
    // The thread-safe local-time conversion is spelled differently per C library:
    // POSIX has localtime_r(time, tm); the MSVC CRT has localtime_s(tm, time) and no
    // localtime_r at all.
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);
    fc.date = buf;
    std::strftime(buf, sizeof(buf), "%H:%M", &tmv);
    fc.time = buf;
    const std::size_t slash = document_path.find_last_of("/\\");
    fc.filename = document_path.empty()
                      ? std::string("Drawing1")
                      : std::string(document_path.substr(slash == std::string_view::npos ? 0 : slash + 1));
    const char* user = std::getenv("USER");
    fc.login = user != nullptr ? user : "";
    return fc;
}

std::string expand_fields(std::string_view raw) {
    if (raw.find("%<") == std::string_view::npos) {
        return std::string(raw);
    }
    const FieldContext fc = field_context();
    std::string out;
    out.reserve(raw.size());
    std::size_t i = 0;
    while (i < raw.size()) {
        if (raw[i] == '%' && i + 1 < raw.size() && raw[i + 1] == '<') {
            const std::size_t close = raw.find(">%", i + 2);
            if (close != std::string_view::npos) {
                const std::string name = upper_ascii(raw.substr(i + 2, close - i - 2));
                if (name == "DATE") {
                    out += fc.date;
                } else if (name == "TIME") {
                    out += fc.time;
                } else if (name == "FILENAME") {
                    out += fc.filename;
                } else if (name == "LOGIN") {
                    out += fc.login;
                } else {
                    out += "####";
                }
                i = close + 2;
                continue;
            }
        }
        out += raw[i];
        ++i;
    }
    return out;
}

SubstitutedText substitute_text_codes(std::string_view raw_in) {
    const std::string expanded = expand_fields(raw_in);
    const std::string_view in = expanded;
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
            // Hole-callout aliases. The letters are chosen from what is still free:
            // d/p/c/o/u/% and the numeric %%nnn form are all taken, so counterbore,
            // depth ("hole depth") and countersink ("vee") get b/h/v.
            if (lc == 'b') {
                append_utf8(r.text, 0x2334); // counterbore / spotface
                i += 3;
                continue;
            }
            if (lc == 'h') {
                append_utf8(r.text, 0x21A7); // depth
                i += 3;
                continue;
            }
            if (lc == 'v') {
                append_utf8(r.text, 0x2335); // countersink
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
        // Unicode escape \U+XXXX ------------------------------------------------
        // Universal, not MTEXT-only: it is the general escape for any code point the
        // font carries but no %%-alias names (the GD&T characteristics, the material
        // condition modifiers). Restricting it to MTEXT would mean a symbol reachable
        // in a paragraph but not in a single-line TEXT, a leader, or dimension text.
        if (in[i] == '\\' && i + 2 < in.size() &&
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

std::string substitute_text(std::string_view in) {
    return substitute_text_codes(in).text;
}

} // namespace musacad::core::text

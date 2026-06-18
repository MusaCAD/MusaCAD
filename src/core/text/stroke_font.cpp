// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/text/stroke_font.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace musacad::core::text {

namespace {

// Glyph grid: gx 0..6, gy 0..8 with baseline at gy=2 and cap at gy=8. Normalised
// to a cell where the baseline is y=0 and the cap height is y=1.
constexpr double kGlyphW = 0.52;    // glyph drawing width (cell fraction of height)
constexpr double kAdvance = 0.62;   // pen advance per glyph
constexpr double kSpace = 0.45;     // advance for a space
constexpr double kSmallCap = 0.66;  // lowercase rendered as small caps at this cap ratio

using Stroke = std::vector<Vec2>;   // a polyline of normalised cell points
using Glyph = std::vector<Stroke>;

Vec2 cell(double gx, double gy) { return {(gx / 6.0) * kGlyphW, (gy - 2.0) / 6.0}; }

// Parses "gx,gy gx,gy | gx,gy ..." into normalised strokes.
Glyph parse(const char* s) {
    Glyph g;
    Stroke cur;
    int gx = 0;
    int gy = 0;
    bool have_x = false;
    int acc = 0;
    bool in_num = false;
    bool neg = false;
    const auto flush_num = [&](int& target) {
        target = neg ? -acc : acc;
        acc = 0;
        in_num = false;
        neg = false;
    };
    const auto end_point = [&]() {
        if (have_x) {
            cur.push_back(cell(gx, gy));
            have_x = false;
        }
    };
    for (const char* p = s;; ++p) {
        const char c = *p;
        if (c >= '0' && c <= '9') {
            acc = acc * 10 + (c - '0');
            in_num = true;
        } else if (c == '-') {
            neg = true;
            in_num = true;
        } else if (c == ',') {
            flush_num(gx);
            have_x = true;
        } else if (c == ' ' || c == '|' || c == '\0') {
            if (in_num) {
                flush_num(gy);
                end_point();
            }
            if (c == '|' || c == '\0') {
                if (cur.size() >= 2) {
                    g.push_back(cur);
                }
                cur.clear();
            }
            if (c == '\0') {
                break;
            }
        }
    }
    return g;
}

// ASCII glyph source (gx 0..6, gy 0..8). Empty => blank (advance only).
struct Entry {
    char ch;
    const char* strokes;
};
constexpr Entry kFont[] = {
    {'0', "1,2 1,8 5,8 5,2 1,2|2,2 4,8"},
    {'1', "2,7 3,8 3,2|1,2 5,2"},
    {'2', "1,7 2,8 4,8 5,7 5,6 1,2 5,2"},
    {'3', "1,8 5,8 3,5|3,5 5,4 5,3 4,2 1,2"},
    {'4', "4,2 4,8 1,4 5,4"},
    {'5', "5,8 1,8 1,5 4,5 5,4 5,3 4,2 1,2"},
    {'6', "5,7 4,8 2,8 1,7 1,3 2,2 4,2 5,3 5,4 4,5 1,5"},
    {'7', "1,8 5,8 2,2"},
    {'8', "2,5 1,6 1,7 2,8 4,8 5,7 5,6 4,5 2,5 1,4 1,3 2,2 4,2 5,3 5,4 4,5"},
    {'9', "5,5 2,5 1,6 1,7 2,8 4,8 5,7 5,3 4,2 2,2 1,3"},
    {'A', "1,2 3,8 5,2|2,4 4,4"},
    {'B', "1,2 1,8 4,8 5,7 5,6 4,5 1,5|4,5 5,4 5,3 4,2 1,2"},
    {'C', "5,7 4,8 2,8 1,7 1,3 2,2 4,2 5,3"},
    {'D', "1,2 1,8 4,8 5,7 5,3 4,2 1,2"},
    {'E', "5,8 1,8 1,2 5,2|1,5 4,5"},
    {'F', "5,8 1,8 1,2|1,5 4,5"},
    {'G', "5,7 4,8 2,8 1,7 1,3 2,2 4,2 5,3 5,5 3,5"},
    {'H', "1,2 1,8|5,2 5,8|1,5 5,5"},
    {'I', "2,8 4,8|3,8 3,2|2,2 4,2"},
    {'J', "5,8 5,3 4,2 2,2 1,3"},
    {'K', "1,2 1,8|5,8 1,5 5,2"},
    {'L', "1,8 1,2 5,2"},
    {'M', "1,2 1,8 3,5 5,8 5,2"},
    {'N', "1,2 1,8 5,2 5,8"},
    {'O', "2,2 1,3 1,7 2,8 4,8 5,7 5,3 4,2 2,2"},
    {'P', "1,2 1,8 4,8 5,7 5,6 4,5 1,5"},
    {'Q', "2,2 1,3 1,7 2,8 4,8 5,7 5,3 4,2 2,2|3,3 5,1"},
    {'R', "1,2 1,8 4,8 5,7 5,6 4,5 1,5|3,5 5,2"},
    {'S', "5,7 4,8 2,8 1,7 1,6 2,5 4,5 5,4 5,3 4,2 2,2 1,3"},
    {'T', "1,8 5,8|3,8 3,2"},
    {'U', "1,8 1,3 2,2 4,2 5,3 5,8"},
    {'V', "1,8 3,2 5,8"},
    {'W', "1,8 2,2 3,5 4,2 5,8"},
    {'X', "1,2 5,8|1,8 5,2"},
    {'Y', "1,8 3,5 5,8|3,5 3,2"},
    {'Z', "1,8 5,8 1,2 5,2"},
    {'.', "3,2 3,3"},
    {',', "4,2 3,1"},
    {'-', "1,5 5,5"},
    {'+', "1,5 5,5|3,3 3,7"},
    {'/', "1,2 5,8"},
    {'\\', "1,8 5,2"},
    {'<', "5,7 1,5 5,3"},
    {'>', "1,7 5,5 1,3"},
    {'=', "1,4 5,4|1,6 5,6"},
    {'(', "4,8 2,6 2,4 4,2"},
    {')', "2,8 4,6 4,4 2,2"},
    {':', "3,3 4,3|3,6 4,6"},
    {';', "3,6 4,6|4,3 3,2"},
    {'#', "2,3 2,7|4,3 4,7|1,4 5,4|1,6 5,6"},
    {'*', "3,5 3,7|2,5 4,7|4,5 2,7"},
    {'\'', "3,8 3,7"},
    {'"', "2,8 2,7|4,8 4,7"},
};

// Special CAD symbols (Unicode), authored directly in normalised cell points.
Glyph degree_glyph() {
    Glyph g;
    Stroke ring;
    for (int i = 0; i <= 8; ++i) {
        const double a = (static_cast<double>(i) / 8.0) * 2.0 * 3.14159265358979;
        ring.push_back({(3.0 / 6.0) * kGlyphW + 0.06 * std::cos(a),
                        (7.0 - 2.0) / 6.0 + 0.10 * std::sin(a)});
    }
    g.push_back(ring);
    return g;
}
Glyph plusminus_glyph() {
    Glyph g = parse("1,4 5,4|3,3 3,7"); // plus (upper)
    g.push_back(parse("1,2 5,2")[0]);   // bar (lower)
    return g;
}
Glyph diameter_glyph() {
    Glyph g = parse("2,2 1,3 1,7 2,8 4,8 5,7 5,3 4,2 2,2"); // O
    g.push_back(parse("1,2 5,8")[0]);                        // slash
    return g;
}

const std::array<Glyph, 128>& ascii_table() {
    static const std::array<Glyph, 128> table = [] {
        std::array<Glyph, 128> t;
        for (const Entry& e : kFont) {
            t[static_cast<std::size_t>(e.ch)] = parse(e.strokes);
        }
        return t;
    }();
    return table;
}

std::vector<char32_t> decode_utf8(std::string_view s) {
    std::vector<char32_t> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out.push_back(c);
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            out.push_back(static_cast<char32_t>(((c & 0x1F) << 6) |
                                                (static_cast<unsigned char>(s[i + 1]) & 0x3F)));
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            out.push_back(static_cast<char32_t>(((c & 0x0F) << 12) |
                                                ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                                                (static_cast<unsigned char>(s[i + 2]) & 0x3F)));
            i += 3;
        } else {
            i += 1; // skip a malformed byte
        }
    }
    return out;
}

// Returns the glyph for a codepoint, and whether to draw it at small-cap scale.
const Glyph* glyph_for(char32_t cp, bool& small_cap) {
    small_cap = false;
    static const Glyph empty;
    if (cp == 0x00B0) {
        static const Glyph g = degree_glyph();
        return &g;
    }
    if (cp == 0x00B1) {
        static const Glyph g = plusminus_glyph();
        return &g;
    }
    if (cp == 0x2300) {
        static const Glyph g = diameter_glyph();
        return &g;
    }
    if (cp >= 'a' && cp <= 'z') {
        small_cap = true;
        cp = cp - 'a' + 'A'; // small capitals
    }
    if (cp < 128) {
        return &ascii_table()[static_cast<std::size_t>(cp)];
    }
    return &empty;
}

bool is_space(char32_t cp) { return cp == ' ' || cp == '\t'; }

} // namespace

double text_width(std::string_view text, double height) {
    double w = 0.0;
    for (const char32_t cp : decode_utf8(text)) {
        w += (is_space(cp) ? kSpace : kAdvance) * height;
    }
    return w;
}

void append_text_segments(std::string_view text, Vec2 origin, double height, double rotation,
                          Justify justify, std::vector<Vec2>& out) {
    const std::vector<char32_t> cps = decode_utf8(text);
    const double total = text_width(text, height);
    double pen = justify == Justify::Center ? -total / 2.0 : justify == Justify::Right ? -total : 0.0;

    const double cs = std::cos(rotation);
    const double sn = std::sin(rotation);
    const auto place = [&](Vec2 local) {
        const double wx = origin.x + local.x * cs - local.y * sn;
        const double wy = origin.y + local.x * sn + local.y * cs;
        out.push_back({wx, wy});
    };

    for (const char32_t cp : cps) {
        if (is_space(cp)) {
            pen += kSpace * height;
            continue;
        }
        bool small_cap = false;
        const Glyph* g = glyph_for(cp, small_cap);
        const double yscale = small_cap ? kSmallCap : 1.0;
        if (g != nullptr) {
            for (const Stroke& stroke : *g) {
                for (std::size_t i = 1; i < stroke.size(); ++i) {
                    place({pen + stroke[i - 1].x * height, stroke[i - 1].y * yscale * height});
                    place({pen + stroke[i].x * height, stroke[i].y * yscale * height});
                }
            }
        }
        pen += kAdvance * height;
    }
}

} // namespace musacad::core::text

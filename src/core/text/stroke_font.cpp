// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/text/stroke_font.hpp"

#include "musacad/core/math/math.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

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
    // The remaining printable ASCII. These were missing, so '%' (and 14 others) drew
    // BLANK with only an advance -- "50% FULL" plotted as "50 FULL". Same 6x8 cell,
    // same advance, authored in the style of the punctuation above.
    {'!', "3,8 3,4|3,3 3,2"},
    {'?', "1,7 2,8 4,8 5,7 5,6 3,5 3,4|3,3 3,2"},
    {'%', "1,7 2,8 1,8 1,7|1,2 5,8|5,3 4,2 5,2 5,3"},
    {'$', "5,7 4,8 2,8 1,7 1,6 5,4 5,3 4,2 2,2 1,3|3,8 3,1"},
    {'&', "5,2 2,5 2,7 3,8 4,7 4,6 1,4 1,3 2,2 4,2 5,3"},
    {'@', "4,4 3,3 2,4 2,5 3,6 4,5 4,3|4,4 5,4 5,7 4,8 2,8 1,7 1,3 2,2 4,2"},
    {'[', "4,8 2,8 2,2 4,2"},
    {']', "2,8 4,8 4,2 2,2"},
    {'{', "4,8 3,7 3,6 2,5 3,4 3,3 4,2"},
    {'}', "2,8 3,7 3,6 4,5 3,4 3,3 2,2"},
    {'^', "1,6 3,8 5,6"},
    {'_', "1,1 5,1"},
    {'`', "2,8 4,7"},
    {'|', "3,8 3,1"},
    {'~', "1,5 2,6 4,4 5,5"},
    // Real lowercase (simplex/Hershey-class, single-stroke). x-height = gy 6, ascenders
    // reach the cap line (gy 8), descenders drop to gy 0. Hand-authored on the same grid;
    // monospace advance is unchanged (kAdvance), so layout/width metrics are preserved.
    {'a', "5,2 5,6|5,5 4,6 2,6 1,5 1,3 2,2 4,2 5,3"},
    {'b', "1,8 1,2|1,3 2,2 4,2 5,3 5,5 4,6 2,6 1,5"},
    {'c', "5,5 4,6 2,6 1,5 1,3 2,2 4,2 5,3"},
    {'d', "5,8 5,2|5,3 4,2 2,2 1,3 1,5 2,6 4,6 5,5"},
    {'e', "1,4 5,4 5,5 4,6 2,6 1,5 1,3 2,2 4,2 5,3"},
    {'f', "5,7 4,8 3,8 3,2|1,6 5,6"},
    {'g', "5,6 5,1 4,0 2,0 1,1|5,5 4,6 2,6 1,5 1,3 2,2 4,2 5,3"},
    {'h', "1,8 1,2|1,5 2,6 4,6 5,5 5,2"},
    {'i', "3,2 3,6|3,7 3,8"},
    {'j', "4,6 4,1 3,0 2,0 1,1|4,7 4,8"},
    {'k', "1,8 1,2|5,6 1,4 5,2"},
    {'l', "3,8 3,2 4,2"},
    {'m', "1,2 1,6|1,5 2,6 3,6 3,2|3,5 4,6 5,6 5,2"},
    {'n', "1,2 1,6|1,5 2,6 4,6 5,5 5,2"},
    {'o', "1,3 1,5 2,6 4,6 5,5 5,3 4,2 2,2 1,3"},
    {'p', "1,6 1,0|1,3 2,2 4,2 5,3 5,5 4,6 2,6 1,5"},
    {'q', "5,6 5,0|5,3 4,2 2,2 1,3 1,5 2,6 4,6 5,5"},
    {'r', "1,2 1,6|1,5 2,6 4,6 5,5"},
    {'s', "5,5 4,6 2,6 1,5 2,4 4,4 5,3 4,2 2,2 1,3"},
    {'t', "3,8 3,3 4,2|1,6 5,6"},
    {'u', "1,6 1,3 2,2 4,2 5,3 5,6"},
    {'v', "1,6 3,2 5,6"},
    {'w', "1,6 2,2 3,4 4,2 5,6"},
    {'x', "1,6 5,2|1,2 5,6"},
    {'y', "1,6 3,2|5,6 1,0"},
    {'z', "1,6 5,6 1,2 5,2"},
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

// ---------------------------------------------------------------------------
// Drafting symbols (hole callouts + GD&T), hand-authored on the SAME 6x8 cell as
// the letters, at the SAME advance -- the font stays monospace, so text_width,
// layout, bounds and pick are untouched by adding glyphs.
//
// Every outline here is drawn from the ASME Y14.5 / ISO 1101 symbol descriptions
// on this project's own grid, in the style of the existing lowercase set. Nothing
// is traced from an SHX file, a proprietary font, or any other source.
// ---------------------------------------------------------------------------

// The capital-O outline. Reused as the ring for circularity and for every circled
// material-condition modifier, so they all share one circle shape.
constexpr const char* kRing = "2,2 1,3 1,7 2,8 4,8 5,7 5,3 4,2 2,2";

/// A smooth arc in GRID coordinates (gx 0..6, gy 0..8), sampled to `steps` segments.
/// The stroke table's parser only takes integers, which is fine for straight-edged
/// glyphs but turns a profile arc into a visible peak -- these need real curvature.
Stroke arc_stroke(double cx, double cy, double rx, double ry, double a0_deg, double a1_deg,
                  int steps) {
    Stroke s;
    s.reserve(static_cast<std::size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        const double a = (a0_deg + (a1_deg - a0_deg) * t) * kPi / 180.0;
        s.push_back(cell(cx + rx * std::cos(a), cy + ry * std::sin(a)));
    }
    return s;
}

/// Scales a glyph about the cell centre -- how the letter inside a circled modifier
/// (Ⓜ Ⓛ Ⓢ …) is shrunk to fit its ring without authoring a second alphabet.
Glyph scaled_about_center(const Glyph& src, double s) {
    const Vec2 c = cell(3, 5); // the cell's optical centre
    Glyph out;
    out.reserve(src.size());
    for (const Stroke& stroke : src) {
        Stroke t;
        t.reserve(stroke.size());
        for (const Vec2& p : stroke) {
            t.push_back({c.x + (p.x - c.x) * s, c.y + (p.y - c.y) * s});
        }
        out.push_back(std::move(t));
    }
    return out;
}

void append_glyph(Glyph& dst, const Glyph& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

/// A material-condition modifier: the shared ring with a shrunken capital inside.
Glyph circled_letter(char letter) {
    Glyph g = parse(kRing);
    append_glyph(g, scaled_about_center(ascii_table()[static_cast<std::size_t>(letter)], 0.52));
    return g;
}

// Symbols whose whole outline is plain strokes on the grid.
struct SymbolEntry {
    char32_t cp;
    const char* strokes;
};
constexpr SymbolEntry kSymbols[] = {
    // --- Hole callouts (ASME Y14.5 §3.3) ---------------------------------------
    {0x2334, "1,7 1,3 5,3 5,7"},                 // counterbore / spotface
    {0x21A7, "1,8 5,8|3,8 3,3|1,5 3,3 5,5"},     // depth ("deep"): barred down-arrow
    {0x2335, "1,7 3,3 5,7"},                     // countersink: 90-degree opened V
    // --- GD&T characteristics (ISO 1101 / ASME Y14.5 Table) --------------------
    {0x23E4, "1,5 5,5"},                         // straightness
    // Flatness is a parallelogram. It must be WIDE and SHALLOW: the cell is far taller
    // than it is wide, so a parallelogram spanning gy 3..7 collapses into two near-
    // vertical slashes and becomes indistinguishable from parallelism (//).
    {0x23E5, "1,3 2,6 5,6 4,3 1,3"},
    {0x2220, "1,3 5,3|1,3 5,7"},                 // angularity
    {0x27C2, "3,8 3,3|1,3 5,3"},                 // perpendicularity
    {0x2225, "1,3 3,7|3,3 5,7"},                 // parallelism
    {0x232F, "3,8 3,2|1,6 5,6|1,4 5,4"},         // symmetry
    {0x2197, "1,3 5,7|5,7 3,7|5,7 5,5"},         // circular runout: single arrow
    {0x2330, "0,3 3,6|3,6 1,6|3,6 3,4|"          // total runout: double arrow
              "3,3 6,6|6,6 4,6|6,6 6,4"},
    // --- Feature-of-size / form modifiers --------------------------------------
    {0x25A1, "1,3 5,3 5,7 1,7 1,3"},             // square (a square feature)
    // Taper and slope are both wedges opening right; they only read as DIFFERENT
    // symbols if the taper is symmetric about the axis and the slope is markedly
    // shallower with a flat base. At equal angles they both just look like an angle.
    {0x2332, "1,5 5,7|1,5 5,3"},                 // conical taper: symmetric wedge
    {0x2333, "1,3 5,3|1,3 5,5"},                 // slope: shallow, flat-based
};

// Symbols composed from the shared ring (so every circle in the font is one shape).
struct ComposedEntry {
    char32_t cp;
    char letter; ///< '\0' = no letter inside
    const char* extra; ///< additional strokes, or nullptr
    double inner; ///< 0 = none, else a concentric ring scaled by this
};
constexpr ComposedEntry kComposed[] = {
    {0x25CB, '\0', nullptr, 0.0},        // circularity: the bare ring
    {0x232D, '\0', "0,3 1,7|5,3 6,7", 0.0}, // cylindricity: ring between two obliques
    {0x2316, '\0', "3,2 3,8|0,5 6,5", 0.0}, // position: ring with a full crosshair
    {0x25CE, '\0', nullptr, 0.5},        // concentricity: two concentric rings
    {0x24C2, 'M', nullptr, 0.0},         // MMC
    {0x24C1, 'L', nullptr, 0.0},         // LMC
    {0x24C8, 'S', nullptr, 0.0},         // RFS (regardless of feature size)
    {0x24C5, 'P', nullptr, 0.0},         // projected tolerance zone
    {0x24BB, 'F', nullptr, 0.0},         // free state
    {0x24C9, 'T', nullptr, 0.0},         // tangent plane
    {0x24CA, 'U', nullptr, 0.0},         // unequally disposed profile
};

/// Every non-ASCII glyph, built once. A flat sorted-by-insertion map is plenty for
/// ~30 entries and keeps the authored tables above as the single source of truth.
const std::unordered_map<char32_t, Glyph>& symbol_table() {
    static const std::unordered_map<char32_t, Glyph> table = [] {
        std::unordered_map<char32_t, Glyph> t;
        t.emplace(0x00B0, degree_glyph());
        t.emplace(0x00B1, plusminus_glyph());
        t.emplace(0x2300, diameter_glyph());
        for (const SymbolEntry& e : kSymbols) {
            t.emplace(e.cp, parse(e.strokes));
        }
        // The two profile symbols are genuine arcs, so they are sampled rather than
        // authored as grid points -- a 5-point integer polyline reads as a sharp peak,
        // not an arc, and "profile of a surface" then looks like a triangle.
        const Stroke profile_arc = arc_stroke(3.0, 3.4, 2.6, 3.0, 160.0, 20.0, 10);
        t.emplace(0x2312, Glyph{profile_arc});                       // profile of a LINE
        t.emplace(0x2313, Glyph{profile_arc, parse("1,4 5,4")[0]});  // ... of a SURFACE
        for (const ComposedEntry& e : kComposed) {
            Glyph g = e.letter != '\0' ? circled_letter(e.letter) : parse(kRing);
            if (e.extra != nullptr) {
                append_glyph(g, parse(e.extra));
            }
            if (e.inner > 0.0) {
                append_glyph(g, scaled_about_center(parse(kRing), e.inner));
            }
            t.emplace(e.cp, std::move(g));
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
    if (cp >= 0x80) {
        const auto& t = symbol_table();
        const auto it = t.find(cp);
        return it != t.end() ? &it->second : &empty;
    }
    if (cp >= 'a' && cp <= 'z') {
        const Glyph& lower = ascii_table()[static_cast<std::size_t>(cp)];
        if (!lower.empty()) {
            return &lower; // real lowercase glyph (true ascenders/x-height/descenders)
        }
        small_cap = true;    // defensive fallback: small capitals if a glyph is missing
        cp = cp - 'a' + 'A';
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

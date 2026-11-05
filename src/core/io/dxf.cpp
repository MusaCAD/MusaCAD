// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/io/dxf.hpp"
#include "musacad/core/text/mtext.hpp"
#include "musacad/core/text/text_frame.hpp"

#include "musacad/core/text/parse_double.hpp"

#include "musacad/core/spline_eval.hpp"

#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/hatch.hpp"
#include "musacad/core/mleader_blocks.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <filesystem>
#include <optional>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::core::io {

namespace {

// --- B-spline (NURBS) evaluation for SPLINE import -------------------------
// Real DXF splines are non-uniform: they carry an explicit knot vector. Evaluating them
// as a uniform B-spline gives wild curves, so we evaluate the ACTUAL curve (de Boor with
// the imported knots) and tessellate to a polyline -- correct regardless of the internal
// spline model.

// Evaluate a B-spline at parameter u via de Boor. knots.size() must be ctrl.size()+p+1.
Vec2 deboor_eval(const std::vector<Vec2>& ctrl, int p, const std::vector<double>& knots,
                 double u) {
    const int n = static_cast<int>(ctrl.size()) - 1;
    int k = p;
    while (k < n && knots[static_cast<std::size_t>(k) + 1] <= u) {
        ++k;
    }
    std::vector<Vec2> d(static_cast<std::size_t>(p) + 1);
    for (int j = 0; j <= p; ++j) {
        d[static_cast<std::size_t>(j)] = ctrl[static_cast<std::size_t>(k - p + j)];
    }
    for (int r = 1; r <= p; ++r) {
        for (int j = p; j >= r; --j) {
            const int i = k - p + j;
            const double lo = knots[static_cast<std::size_t>(i)];
            const double hi = knots[static_cast<std::size_t>(i + p - r + 1)];
            const double a = (hi - lo) > 1e-12 ? (u - lo) / (hi - lo) : 0.0;
            d[static_cast<std::size_t>(j)] = d[static_cast<std::size_t>(j - 1)] * (1.0 - a) +
                                             d[static_cast<std::size_t>(j)] * a;
        }
    }
    return d[static_cast<std::size_t>(p)];
}

// Tessellate a B-spline (control points + degree + optional knots) to a point chain. A
// missing/mismatched knot vector falls back to a clamped-uniform one (open B-spline).
std::vector<Vec2> tessellate_bspline(const std::vector<Vec2>& ctrl, int degree,
                                     std::vector<double> knots) {
    std::vector<Vec2> out;
    const int n = static_cast<int>(ctrl.size()) - 1;
    int p = degree;
    if (p < 1) {
        p = 1;
    }
    if (p > n) {
        p = n; // degree can't exceed control count - 1
    }
    const std::size_t need = ctrl.size() + static_cast<std::size_t>(p) + 1;
    if (knots.size() != need) {
        // Clamped-uniform: p+1 zeros, interior 1..(n-p), p+1 of (n-p+1).
        knots.clear();
        const int interior = n - p; // number of interior knots
        for (int i = 0; i <= p; ++i) {
            knots.push_back(0.0);
        }
        for (int i = 1; i <= interior; ++i) {
            knots.push_back(static_cast<double>(i));
        }
        const double last = static_cast<double>(interior) + 1.0;
        for (int i = 0; i <= p; ++i) {
            knots.push_back(last);
        }
    }
    const double u0 = knots[static_cast<std::size_t>(p)];
    const double u1 = knots[static_cast<std::size_t>(n) + 1];
    if (!(u1 > u0)) {
        return ctrl; // degenerate -- just return the control polygon
    }
    // Sample density scales with the control count (these splines reach ~1000 points).
    const auto steps = static_cast<int>(std::clamp<std::size_t>(ctrl.size() * 4, 32, 4000));
    out.reserve(static_cast<std::size_t>(steps) + 1);
    for (int s = 0; s <= steps; ++s) {
        double u = u0 + (u1 - u0) * (static_cast<double>(s) / static_cast<double>(steps));
        if (u > u1) {
            u = u1;
        }
        out.push_back(deboor_eval(ctrl, p, knots, u));
    }
    return out;
}

// --- writing ---------------------------------------------------------------

void code(std::string& s, int c, std::string_view value) {
    s += std::to_string(c);
    s += '\n';
    s += value;
    s += '\n';
}
void code_d(std::string& s, int c, double v) {
    char buf[40];
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
    code(s, c, std::string_view(buf, static_cast<std::size_t>(ptr - buf)));
}

// A text as DXF places it: 10 is where its baseline starts, 11 the point it is justified
// on (the second end of the baseline for Aligned and Fit), 72 / 73 name the
// justification, 40 / 50 / 41 are the height, the rotation and the width factor as drawn.
// `measured` is the string whose width justifies the text (an attribute definition is
// justified by its tag).
int emit_text_place(std::string& s, const Document& doc, const DocText& t, std::string_view measured) {
    const double style_factor =
        (t.style != 0 && t.style < doc.text_styles.size()) ? doc.text_styles[t.style].width_factor : 1.0;
    const text::TextFrame f = text::frame_of(measured, t.justify, t.pos, t.align, t.height, t.rotation,
                                             style_factor * t.width_factor);
    const bool two = text::two_point(t.justify);
    const Vec2 first = two ? t.pos : f.origin;
    code_d(s, 10, first.x);
    code_d(s, 20, first.y);
    code_d(s, 30, 0.0);
    code_d(s, 40, f.height);
    code_d(s, 50, to_degrees(f.rotation));
    if (std::abs(f.width_factor - 1.0) > 1e-12) {
        code_d(s, 41, f.width_factor);
    }
    int horizontal = 0;
    int vertical = 0;
    text::justify_to_dxf(t.justify, horizontal, vertical);
    code(s, 72, std::to_string(horizontal));
    if (t.justify != 0) {
        const Vec2 second = two ? t.align : t.pos;
        code_d(s, 11, second.x);
        code_d(s, 21, second.y);
        code_d(s, 31, 0.0);
    }
    return vertical; // AcDbText's second part (73) or an attribute's 74, after the text
}

// A TEXT's second AcDbText part: the vertical alignment (73), after the string and style.
void emit_text_end(std::string& s, int vertical) {
    code(s, 100, "AcDbText");
    if (vertical != 0) {
        code(s, 73, std::to_string(vertical));
    }
}

// LWPOLYLINE widths: 43 when every segment has one width, else 40 / 41 per vertex;
// 38 / 39 carry the elevation and the thickness.
void emit_polyline_widths_header(std::string& s, const DocPolyline& p) {
    if (p.widths.size() == 2 * p.points.size() && !p.widths.empty()) {
        bool uniform = true;
        for (const double w : p.widths) {
            uniform = uniform && w == p.widths.front();
        }
        if (uniform && p.widths.front() > 0.0) {
            code_d(s, 43, p.widths.front());
        }
    }
    if (p.elevation != 0.0) {
        code_d(s, 38, p.elevation);
    }
    if (p.thickness != 0.0) {
        code_d(s, 39, p.thickness);
    }
}
void emit_vertex_widths(std::string& s, const DocPolyline& p, std::size_t i) {
    if (p.widths.size() != 2 * p.points.size()) {
        return;
    }
    bool uniform = true;
    for (const double w : p.widths) {
        uniform = uniform && w == p.widths.front();
    }
    if (uniform) {
        return; // 43 said it
    }
    if (p.widths[2 * i] != 0.0 || p.widths[2 * i + 1] != 0.0) {
        code_d(s, 40, p.widths[2 * i]);
        code_d(s, 41, p.widths[2 * i + 1]);
    }
}
void code_i(std::string& s, int c, long v) { code(s, c, std::to_string(v)); }

// GD&T symbols: Musa keeps them as \U+XXXX escapes inside a frame cell; AutoCAD's
// TOLERANCE string uses the gdt font's letters as {\Fgdt;x}. Both directions.
struct GdtSym {
    const char* uni; // "\U+2316"
    char letter;
};
constexpr GdtSym kGdt[] = {
    {"\\U+2316", 'j'}, // position
    {"\\U+25CE", 'r'}, // concentricity
    {"\\U+232F", 'i'}, // symmetry
    {"\\U+2225", 'f'}, // parallelism
    {"\\U+27C2", 'b'}, // perpendicularity
    {"\\U+22A5", 'b'}, // perpendicularity (alternate code point)
    {"\\U+2220", 'a'}, // angularity
    {"\\U+232D", 'g'}, // cylindricity
    {"\\U+23E5", 'c'}, // flatness
    {"\\U+25CB", 'e'}, // circularity
    {"\\U+23E4", 'u'}, // straightness
    {"\\U+2313", 'd'}, // profile of a surface
    {"\\U+2312", 'k'}, // profile of a line
    {"\\U+2197", 'h'}, // circular runout
    {"\\U+2330", 't'}, // total runout
    {"\\U+24C2", 'm'}, // MMC
    {"\\U+24C1", 'l'}, // LMC
    {"\\U+24C8", 's'}, // RFS
    {"\\U+2300", 'n'}, // diameter
    {"\\U+24C5", 'p'}, // projected tolerance zone
    {"\\U+24C9", 'w'}, // tangent plane
    {"\\U+24BB", 'x'}, // free state
};
std::string gdt_to_dxf(std::string cell) {
    for (const GdtSym& g : kGdt) {
        const std::string u(g.uni);
        std::size_t at = 0;
        while ((at = cell.find(u, at)) != std::string::npos) {
            const std::string rep = std::string("{\\Fgdt;") + g.letter + "}";
            cell.replace(at, u.size(), rep);
            at += rep.size();
        }
    }
    return cell;
}
std::string gdt_from_dxf(std::string cell) {
    std::size_t at = 0;
    while ((at = cell.find("{\\Fgdt;", at)) != std::string::npos) {
        const std::size_t close = cell.find('}', at);
        if (close == std::string::npos || close != at + 8) {
            at += 7;
            continue;
        }
        const char letter = cell[at + 7];
        std::string rep;
        for (const GdtSym& g : kGdt) {
            if (g.letter == letter) {
                rep = g.uni;
                break;
            }
        }
        if (rep.empty()) {
            rep = std::string(1, letter); // an unmapped gdt glyph: keep the letter
        }
        cell.replace(at, close - at + 1, rep);
        at += rep.size();
    }
    return cell;
}

// The drawing's loaded linetypes while it is read or written: entities and layers name
// theirs through it.
thread_local const std::vector<LinetypeDef>* t_ltypes = nullptr;
struct LinetypeScope {
    explicit LinetypeScope(const std::vector<LinetypeDef>* table) { t_ltypes = table; }
    ~LinetypeScope() { t_ltypes = nullptr; }
    LinetypeScope(const LinetypeScope&) = delete;
    LinetypeScope& operator=(const LinetypeScope&) = delete;
};

const char* linetype_name(Linetype t) {
    if (const auto v = static_cast<std::size_t>(t); v >= kFirstLoadedLinetype) {
        return t_ltypes != nullptr && v - kFirstLoadedLinetype < t_ltypes->size()
                   ? (*t_ltypes)[v - kFirstLoadedLinetype].name.c_str()
                   : "Continuous";
    }
    switch (t) {
    case Linetype::Dashed:
        return "DASHED";
    case Linetype::Center:
        return "CENTER";
    case Linetype::Hidden:
        return "HIDDEN";
    case Linetype::Continuous:
        break;
    }
    return "Continuous";
}
Linetype linetype_from(const std::string& name) {
    std::string u = name;
    for (char& ch : u) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    for (std::size_t k = 0; t_ltypes != nullptr && k < t_ltypes->size(); ++k) {
        std::string ln = (*t_ltypes)[k].name;
        for (char& ch : ln) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        if (ln == u) {
            return static_cast<Linetype>(kFirstLoadedLinetype + k);
        }
    }
    if (u == "DASHED") {
        return Linetype::Dashed;
    }
    if (u == "CENTER") {
        return Linetype::Center;
    }
    if (u == "HIDDEN") {
        return Linetype::Hidden;
    }
    return Linetype::Continuous;
}
long true_color(Rgb c) {
    return (static_cast<long>(c.r) << 16) | (static_cast<long>(c.g) << 8) | c.b;
}
Rgb from_true_color(long v) {
    return {static_cast<std::uint8_t>((v >> 16) & 0xff), static_cast<std::uint8_t>((v >> 8) & 0xff),
            static_cast<std::uint8_t>(v & 0xff)};
}

// The standard AutoCAD Color Index (ACI) palette, indices 1..255. Most DWG layers
// (and many entities) carry colour as an ACI in group code 62, not a 24-bit true
// colour (420); without this table they all resolve to the default white. 0=ByBlock
// and 256=ByLayer are handled by the caller (left to inherit). 7 maps to white, which
// is correct on Musa's dark viewport.
Rgb aci_to_rgb(long aci) {
    static constexpr std::uint8_t kPalette[256][3] = {
        {0, 0, 0},       {255, 0, 0},     {255, 255, 0},   {0, 255, 0},     {0, 255, 255},
        {0, 0, 255},     {255, 0, 255},   {255, 255, 255}, {128, 128, 128}, {192, 192, 192},
        {255, 0, 0},     {255, 127, 127}, {165, 0, 0},     {165, 82, 82},   {127, 0, 0},
        {127, 63, 63},   {76, 0, 0},      {76, 38, 38},    {38, 0, 0},      {38, 19, 19},
        {255, 63, 0},    {255, 159, 127}, {165, 41, 0},    {165, 103, 82},  {127, 31, 0},
        {127, 79, 63},   {76, 19, 0},     {76, 47, 38},    {38, 9, 0},      {38, 23, 19},
        {255, 127, 0},   {255, 191, 127}, {165, 82, 0},    {165, 124, 82},  {127, 63, 0},
        {127, 95, 63},   {76, 38, 0},     {76, 57, 38},    {38, 19, 0},     {38, 28, 19},
        {255, 191, 0},   {255, 223, 127}, {165, 124, 0},   {165, 145, 82},  {127, 95, 0},
        {127, 111, 63},  {76, 57, 0},     {76, 66, 38},    {38, 28, 0},     {38, 33, 19},
        {255, 255, 0},   {255, 255, 127}, {165, 165, 0},   {165, 165, 82},  {127, 127, 0},
        {127, 127, 63},  {76, 76, 0},     {76, 76, 38},    {38, 38, 0},     {38, 38, 19},
        {191, 255, 0},   {223, 255, 127}, {124, 165, 0},   {145, 165, 82},  {95, 127, 0},
        {111, 127, 63},  {57, 76, 0},     {66, 76, 38},    {28, 38, 0},     {33, 38, 19},
        {127, 255, 0},   {191, 255, 127}, {82, 165, 0},    {124, 165, 82},  {63, 127, 0},
        {95, 127, 63},   {38, 76, 0},     {57, 76, 38},    {19, 38, 0},     {28, 38, 19},
        {63, 255, 0},    {159, 255, 127}, {41, 165, 0},    {103, 165, 82},  {31, 127, 0},
        {79, 127, 63},   {19, 76, 0},     {47, 76, 38},    {9, 38, 0},      {23, 38, 19},
        {0, 255, 0},     {127, 255, 127}, {0, 165, 0},     {82, 165, 82},   {0, 127, 0},
        {63, 127, 63},   {0, 76, 0},      {38, 76, 38},    {0, 38, 0},      {19, 38, 19},
        {0, 255, 63},    {127, 255, 159}, {0, 165, 41},    {82, 165, 103},  {0, 127, 31},
        {63, 127, 79},   {0, 76, 19},     {38, 76, 47},    {0, 38, 9},      {19, 38, 23},
        {0, 255, 127},   {127, 255, 191}, {0, 165, 82},    {82, 165, 124},  {0, 127, 63},
        {63, 127, 95},   {0, 76, 38},     {38, 76, 57},    {0, 38, 19},     {19, 38, 28},
        {0, 255, 191},   {127, 255, 223}, {0, 165, 124},   {82, 165, 145},  {0, 127, 95},
        {63, 127, 111},  {0, 76, 57},     {38, 76, 66},    {0, 38, 28},     {19, 38, 33},
        {0, 255, 255},   {127, 255, 255}, {0, 165, 165},   {82, 165, 165},  {0, 127, 127},
        {63, 127, 127},  {0, 76, 76},     {38, 76, 76},    {0, 38, 38},     {19, 38, 38},
        {0, 191, 255},   {127, 223, 255}, {0, 124, 165},   {82, 145, 165},  {0, 95, 127},
        {63, 111, 127},  {0, 57, 76},     {38, 66, 76},    {0, 28, 38},     {19, 33, 38},
        {0, 127, 255},   {127, 191, 255}, {0, 82, 165},    {82, 124, 165},  {0, 63, 127},
        {63, 95, 127},   {0, 38, 76},     {38, 57, 76},    {0, 19, 38},     {19, 28, 38},
        {0, 63, 255},    {127, 159, 255}, {0, 41, 165},    {82, 103, 165},  {0, 31, 127},
        {63, 79, 127},   {0, 19, 76},     {38, 47, 76},    {0, 9, 38},      {19, 23, 38},
        {0, 0, 255},     {127, 127, 255}, {0, 0, 165},     {82, 82, 165},   {0, 0, 127},
        {63, 63, 127},   {0, 0, 76},      {38, 38, 76},    {0, 0, 38},      {19, 19, 38},
        {63, 0, 255},    {159, 127, 255}, {41, 0, 165},    {103, 82, 165},  {31, 0, 127},
        {79, 63, 127},   {19, 0, 76},     {47, 38, 76},    {9, 0, 38},      {23, 19, 38},
        {127, 0, 255},   {191, 127, 255}, {82, 0, 165},    {124, 82, 165},  {63, 0, 127},
        {95, 63, 127},   {38, 0, 76},     {57, 38, 76},    {19, 0, 38},     {28, 19, 38},
        {191, 0, 255},   {223, 127, 255}, {124, 0, 165},   {145, 82, 165},  {95, 0, 127},
        {111, 63, 127},  {57, 0, 76},     {66, 38, 76},    {28, 0, 38},     {33, 19, 38},
        {255, 0, 255},   {255, 127, 255}, {165, 0, 165},   {165, 82, 165},  {127, 0, 127},
        {127, 63, 127},  {76, 0, 76},     {76, 38, 76},    {38, 0, 38},     {38, 19, 38},
        {255, 0, 191},   {255, 127, 223}, {165, 0, 124},   {165, 82, 145},  {127, 0, 95},
        {127, 63, 111},  {76, 0, 57},     {76, 38, 66},    {38, 0, 28},     {38, 19, 33},
        {255, 0, 127},   {255, 127, 191}, {165, 0, 82},    {165, 82, 124},  {127, 0, 63},
        {127, 63, 95},   {76, 0, 38},     {76, 38, 57},    {38, 0, 19},     {38, 19, 28},
        {255, 0, 63},    {255, 127, 159}, {165, 0, 41},    {165, 82, 103},  {127, 0, 31},
        {127, 63, 79},   {76, 0, 19},     {76, 38, 47},    {38, 0, 9},      {38, 19, 23},
        {51, 51, 51},    {91, 91, 91},    {132, 132, 132}, {173, 173, 173}, {214, 214, 214},
        {255, 255, 255},
    };
    if (aci < 1 || aci > 255) {
        return {255, 255, 255};
    }
    const auto& c = kPalette[aci];
    return {c[0], c[1], c[2]};
}

// Every table record, block, entity and object gets a handle (group 5), counted up as
// the file is written; $HANDSEED is the next free one. The two dictionaries the
// OBJECTS section needs have fixed handles below the counter's start.
thread_local std::uint64_t g_handle = 0x20;
constexpr std::uint64_t kRootDictHandle = 0x10;
constexpr std::uint64_t kImageDictHandle = 0x11;
constexpr std::uint64_t kMlineStyleDictHandle = 0x12;
constexpr std::uint64_t kWipeoutVarsHandle = 0x13;
constexpr std::uint64_t kGroupDictHandle = 0x14;

// Owners (330), as an AutoCAD 2000 file has them: a table's records belong to the table, an
// entity to its block record -- *Model_Space's, *Paper_Space's or, inside a BLOCK, that
// block's (`g_owner`, 0 outside one). The records' handles are given with the BLOCK_RECORD
// table, which comes first.
thread_local std::uint64_t g_owner = 0;
thread_local std::uint64_t g_model_record = 0;
thread_local std::uint64_t g_paper_record = 0;
thread_local std::vector<std::uint64_t> g_block_records; // each DocBlockDef's, in order

std::string handle_hex(std::uint64_t h) {
    char buf[20];
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), h, 16);
    std::string out(buf, static_cast<std::size_t>(ptr - buf));
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

/// *Model_Space or *Paper_Space: the blocks AutoCAD keeps the two spaces' entities in, which
/// are not blocks to insert.
bool is_space_block(std::string_view name) {
    const auto same = [&](std::string_view w) {
        return name.size() == w.size() && std::equal(name.begin(), name.end(), w.begin(), [](char a, char b) {
                   return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b));
               });
    };
    return same("*Model_Space") || same("*Paper_Space");
}

std::uint64_t emit_handle(std::string& s) {
    const std::uint64_t h = g_handle++;
    code(s, 5, handle_hex(h));
    return h;
}

void emit_header(std::string& s, const Document& doc, std::uint64_t handseed) {
    code(s, 0, "SECTION");
    code(s, 2, "HEADER");
    code(s, 9, "$ACADVER");
    code(s, 1, "AC1015"); // AutoCAD R2000 -- widely compatible
    code(s, 9, "$HANDSEED");
    code(s, 5, handle_hex(handseed));
    code(s, 9, "$LTSCALE");
    code_d(s, 40, doc.ltscale); // global linetype scale
    // MLINE's current style, justification and scale.
    code(s, 9, "$CMLSTYLE");
    code(s, 2, doc.current_mline_style < doc.mline_styles.size() ? doc.mline_styles[doc.current_mline_style].name
                                                                  : std::string("STANDARD"));
    code(s, 9, "$CMLJUST");
    code(s, 70, std::to_string(doc.cmljust));
    code(s, 9, "$CMLSCALE");
    code_d(s, 40, doc.cmlscale);
    code(s, 9, "$DIMSTYLE"); // the current dimension style
    code(s, 2, doc.current_dimstyle < doc.dimstyles.size() ? doc.dimstyles[doc.current_dimstyle].name
                                                            : std::string("Standard"));
    // The units (UNITS), so another program reads the drawing at the right scale and shows
    // lengths and angles the way this one does.
    const auto code_i = [&](const char* var, long v) {
        code(s, 9, var);
        code(s, 70, std::to_string(v));
    };
    code_i("$INSUNITS", doc.display_units.insunits);
    code_i("$LUNITS", static_cast<long>(doc.display_units.linear));
    code_i("$LUPREC", doc.display_units.linear_precision);
    code_i("$AUNITS", static_cast<long>(doc.display_units.angular));
    code_i("$AUPREC", doc.display_units.angular_precision);
    code_i("$ANGDIR", doc.display_units.clockwise ? 1 : 0);
    code(s, 9, "$ANGBASE");
    code_d(s, 50, to_degrees(doc.display_units.base_angle));
    code_i("$MEASUREMENT", doc.display_units.measurement); // acad.pat/.lin (0) or acadiso's (1)
    code_i("$PDMODE", doc.pdmode);
    code(s, 9, "$PDSIZE");
    code_d(s, 40, doc.pdsize);
    // DIMASSOC 2: the dimensions a DIMASSOC object names are associative, the others not (#59).
    code(s, 9, "$DIMASSOC");
    code(s, 280, "2");
    code(s, 9, "$INSBASE");
    code_d(s, 10, doc.insbase.x);
    code_d(s, 20, doc.insbase.y);
    code_d(s, 30, 0.0);
    code(s, 9, "$LIMMIN");
    code_d(s, 10, doc.limits_min.x);
    code_d(s, 20, doc.limits_min.y);
    code(s, 9, "$LIMMAX");
    code_d(s, 10, doc.limits_max.x);
    code_d(s, 20, doc.limits_max.y);
    code_i("$LIMCHECK", doc.limits_on ? 1 : 0);
    code_i("$FILLMODE", doc.fillmode ? 1 : 0);
    code(s, 0, "ENDSEC");
}

bool has_wipeouts(const Document& doc) {
    return std::any_of(doc.hatches.begin(), doc.hatches.end(),
                       [](const DocHatch& h) { return h.pattern_name == "WIPEOUT"; });
}

// The classes the raster IMAGE and WIPEOUT objects belong to (AutoCAD reads IMAGE, IMAGEDEF,
// IMAGEDEF_REACTOR, WIPEOUT and WIPEOUTVARIABLES only when the CLASSES section declares them).
void emit_classes(std::string& s, const Document& doc) {
    const bool wipeouts = has_wipeouts(doc);
    if (doc.images.empty() && !wipeouts && doc.dim_refs.empty()) {
        return;
    }
    code(s, 0, "SECTION");
    code(s, 2, "CLASSES");
    const auto cls = [&](const char* name, const char* cpp, long flags, long is_entity) {
        code(s, 0, "CLASS");
        code(s, 1, name);
        code(s, 2, cpp);
        const std::string_view n(name);
        code(s, 3, n.starts_with("WIPEOUT")  ? "WipeOut|AutoCAD Express Tool|expresstools@autodesk.com"
                   : n == "DIMASSOC"         ? "AcDbDimAssoc|Product Desc:     AcDim ARX App For Dimension|"
                                               "Company:          Autodesk, Inc.|WEB Address:      www.autodesk.com"
                                             : "ISM");
        code_i(s, 90, flags);
        code_i(s, 280, 0);
        code_i(s, 281, is_entity);
    };
    if (!doc.images.empty()) {
        cls("IMAGE", "AcDbRasterImage", 127, 1);
        cls("IMAGEDEF", "AcDbRasterImageDef", 0, 0);
        cls("IMAGEDEF_REACTOR", "AcDbRasterImageDefReactor", 1, 0);
    }
    if (wipeouts) {
        cls("WIPEOUT", "AcDbWipeout", 127, 1);
        cls("WIPEOUTVARIABLES", "AcDbWipeoutVariables", 0, 0);
    }
    if (!doc.dim_refs.empty()) {
        cls("DIMASSOC", "AcDbDimAssoc", 0, 0);
    }
    code(s, 0, "ENDSEC");
}

// One LTYPE record: name + dash elements (positive = dash, negative = gap), in
// drawing units. Must match core/linetype.cpp so a Musa-dashed entity reads dashed
// in other CAD apps. `elems` empty => Continuous (solid).
// A table's head (its handle, owned by nothing) and a record's: its handle and table, and
// the subclass markers an AutoCAD 2000 reader reads the record by.
std::uint64_t emit_table_head(std::string& s, const char* name, long count) {
    code(s, 0, "TABLE");
    code(s, 2, name);
    const std::uint64_t h = emit_handle(s);
    code(s, 330, "0");
    code(s, 100, "AcDbSymbolTable");
    code_i(s, 70, count);
    return h;
}
thread_local std::uint64_t g_table = 0; // the table whose records are being written
std::uint64_t emit_record_head(std::string& s, const char* type, const char* subclass) {
    code(s, 0, type);
    const std::uint64_t h = emit_handle(s);
    code(s, 330, handle_hex(g_table));
    code(s, 100, "AcDbSymbolTableRecord");
    code(s, 100, subclass);
    return h;
}

void emit_ltype(std::string& s, const char* name, std::span<const double> elems) {
    emit_record_head(s, "LTYPE", "AcDbLinetypeTableRecord");
    code(s, 2, name);
    code_i(s, 70, 0);
    code(s, 3, name); // description
    code_i(s, 72, 65); // alignment 'A'
    code_i(s, 73, static_cast<long>(elems.size()));
    double total = 0.0;
    for (double e : elems) {
        total += e < 0.0 ? -e : e;
    }
    code_d(s, 40, total);
    for (double e : elems) {
        code_d(s, 49, e);
    }
}

void emit_layer_table(std::string& s, const Document& doc, std::vector<std::uint64_t>& layer_handles) {
    code(s, 0, "SECTION");
    code(s, 2, "TABLES");
    // LTYPE table first -- layers/entities reference these patterns by name. The
    // element lengths mirror core/linetype.cpp (Dashed/Hidden/Center).
    g_table = emit_table_head(s, "LTYPE", 4);
    emit_ltype(s, "ByBlock", {});
    emit_ltype(s, "ByLayer", {});
    // The drawing's own loaded linetypes after the built-in ones (one of those it has
    // loaded by a built-in name is written in its place).
    const auto loaded = [&](const char* name) {
        return std::any_of(doc.linetypes.begin(), doc.linetypes.end(), [&](const LinetypeDef& d) {
            std::string u = d.name;
            for (char& ch : u) {
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            return u == name;
        });
    };
    emit_ltype(s, "Continuous", {});
    // The drawing's own Dashed, Center and Hidden, as it draws them.
    const std::pair<const char*, std::size_t> builtin[] = {{"DASHED", 0}, {"HIDDEN", 2}, {"CENTER", 1}};
    for (const auto& [name, at] : builtin) {
        if (!loaded(name)) {
            emit_ltype(s, name, doc.builtin_linetypes[at]);
        }
    }
    for (const LinetypeDef& d : doc.linetypes) {
        emit_record_head(s, "LTYPE", "AcDbLinetypeTableRecord");
        code(s, 2, d.name);
        code_i(s, 70, 0);
        code(s, 3, d.description);
        code_i(s, 72, 65);
        code_i(s, 73, static_cast<long>(d.pattern.size()));
        double total = 0.0;
        for (const double e : d.pattern) {
            total += std::abs(e);
        }
        code_d(s, 40, total);
        for (const double e : d.pattern) {
            code_d(s, 49, e);
            code_i(s, 74, 0); // a plain dash, gap or dot
        }
    }
    code(s, 0, "ENDTAB");
    // VPORT table: the model window's tiled viewports, each an *Active entry with its
    // corners in window fractions, its centre and its view height (AutoCAD's form).
    {
        std::vector<TiledViewport> tiles = doc.vports;
        if (tiles.empty()) {
            tiles.push_back(TiledViewport{});
        }
        g_table = emit_table_head(s, "VPORT", static_cast<long>(tiles.size()));
        for (const TiledViewport& t : tiles) {
            emit_record_head(s, "VPORT", "AcDbViewportTableRecord");
            code(s, 2, "*Active");
            code_i(s, 70, 0);
            code_d(s, 10, t.x0);
            code_d(s, 20, t.y0);
            code_d(s, 11, t.x1);
            code_d(s, 21, t.y1);
            code_d(s, 12, t.center.x);
            code_d(s, 22, t.center.y);
            code_d(s, 40, t.height);
        }
        code(s, 0, "ENDTAB");
    }
    g_table = emit_table_head(s, "LAYER", static_cast<long>(doc.layers.size()));
    for (const Layer& l : doc.layers) {
        // VIEWPORT 331 points back here
        layer_handles.push_back(emit_record_head(s, "LAYER", "AcDbLayerTableRecord"));
        code(s, 2, l.name);
        // 1 = frozen, 2 = frozen in new viewports (VPLAYER Vpvisdflt), 4 = locked.
        code_i(s, 70, (l.frozen ? 1 : 0) | (l.vp_freeze_new ? 2 : 0) | (l.locked ? 4 : 0));
        code_i(s, 62, l.on ? 7 : -7); // ACI; negative = layer off
        code_i(s, 420, true_color(l.color)); // exact RGB
        code(s, 6, linetype_name(l.linetype));
        code_i(s, 290, l.plot ? 1 : 0); // plotted
        code_i(s, 370, l.lineweight);
        if (!l.description.empty()) {
            // The description goes where AutoCAD keeps it: the layer's AcAecLayerStandard XDATA.
            code(s, 1001, "AcAecLayerStandard");
            code(s, 1000, "");
            code(s, 1000, l.description);
        }
        if (l.transparency != 0) {
            // Where AutoCAD keeps a layer's transparency: AcCmTransparency XDATA, the alpha
            // (0..255) flagged 0x02000000 "by alpha".
            const long alpha = std::lround(255.0 * (100.0 - std::min<int>(l.transparency, 90)) / 100.0);
            code(s, 1001, "AcCmTransparency");
            code_i(s, 1071, 0x02000000L | alpha);
        }
    }
    code(s, 0, "ENDTAB");
    // STYLE table (v26): Standard first, then the drawing's text styles -- name, font
    // file (txt for the stroke font), fixed height, width factor, obliquing angle.
    g_table = emit_table_head(s, "STYLE", static_cast<long>(std::max<std::size_t>(doc.text_styles.size(), 1)));
    if (doc.text_styles.empty()) {
        emit_record_head(s, "STYLE", "AcDbTextStyleTableRecord");
        code(s, 2, "Standard");
        code_i(s, 70, 0);
        code_d(s, 40, 0.0);
        code_d(s, 41, 1.0);
        code_d(s, 50, 0.0);
        code(s, 3, "txt");
    }
    for (const TextStyle& ts : doc.text_styles) {
        emit_record_head(s, "STYLE", "AcDbTextStyleTableRecord");
        code(s, 2, ts.name);
        code_i(s, 70, 0);
        code_d(s, 40, ts.height);
        code_d(s, 41, ts.width_factor);
        code_d(s, 50, to_degrees(ts.oblique));
        code_i(s, 71, (ts.backwards ? 2 : 0) | (ts.upside_down ? 4 : 0)); // generation flags
        code(s, 3, ts.font.empty() ? "txt" : ts.font);
    }
    code(s, 0, "ENDTAB");
    // VIEW and UCS: none written, but AutoCAD expects the tables. APPID: the applications
    // whose XDATA the file carries (ACAD's DSTYLE overrides, the layers' description and
    // transparency).
    g_table = emit_table_head(s, "VIEW", 0);
    code(s, 0, "ENDTAB");
    g_table = emit_table_head(s, "UCS", 0);
    code(s, 0, "ENDTAB");
    {
        const char* apps[] = {"ACAD", "AcAecLayerStandard", "AcCmTransparency"};
        g_table = emit_table_head(s, "APPID", 3);
        for (const char* app : apps) {
            emit_record_head(s, "APPID", "AcDbRegAppTableRecord");
            code(s, 2, app);
            code_i(s, 70, 0);
        }
        code(s, 0, "ENDTAB");
    }
    // DIMSTYLE table (minimal: name + the formatting Musa models). Its records carry their
    // handle in 105, not 5.
    g_table = emit_table_head(s, "DIMSTYLE", static_cast<long>(doc.dimstyles.size()));
    code(s, 100, "AcDbDimStyleTable");
    for (const DimStyle& ds : doc.dimstyles) {
        code(s, 0, "DIMSTYLE");
        code(s, 105, handle_hex(g_handle++));
        code(s, 330, handle_hex(g_table));
        code(s, 100, "AcDbSymbolTableRecord");
        code(s, 100, "AcDbDimStyleTableRecord");
        code(s, 2, ds.name);
        code_d(s, 140, ds.text_height); // DIMTXT
        code_d(s, 41, ds.arrow_size);   // DIMASZ
        code_d(s, 42, ds.ext_offset);   // DIMEXO
        code_d(s, 44, ds.ext_extension); // DIMEXE
        code_i(s, 77, dim_text_vpos(ds)); // DIMTAD
        code_i(s, 280, ds.text_just);      // DIMJUST
        code_i(s, 294, ds.text_rtl ? 1 : 0); // DIMTXTDIRECTION
        code_i(s, 78, ds.zero_suppression);   // DIMZIN
        code_i(s, 271, ds.precision);   // DIMDEC
        code_i(s, 278, static_cast<unsigned char>(ds.decimal_separator)); // DIMDSEP
        code_i(s, 279, ds.text_move);         // DIMTMOVE
        code_i(s, 371, ds.dim_lineweight);    // DIMLWD
        if (!ds.post.empty()) {
            code(s, 3, ds.post); // DIMPOST
        }
        if (!ds.alt_post.empty()) {
            code(s, 4, ds.alt_post); // DIMAPOST
        }
        code_d(s, 45, ds.round_off);          // DIMRND
        code_d(s, 143, ds.alt_factor);        // DIMALTF
        code_d(s, 144, ds.linear_factor);     // DIMLFAC
        code_d(s, 148, ds.alt_round);         // DIMALTRND
        code_i(s, 170, ds.alt ? 1 : 0);       // DIMALT
        code_i(s, 171, ds.alt_precision);     // DIMALTD
        code_i(s, 179, ds.angular_precision); // DIMADEC
        code_i(s, 273, ds.alt_unit);          // DIMALTU
        code_i(s, 275, ds.angular_unit);      // DIMAUNIT
        code_i(s, 276, ds.fraction_format);   // DIMFRAC
        code_i(s, 277, ds.linear_unit);       // DIMLUNIT
        if (ds.tol_method != 0) {
            // The Tolerances tab: DIMTOL / DIMLIM, the values, and a basic dimension's
            // negative DIMGAP.
            code_i(s, 71, ds.tol_method == 1 || ds.tol_method == 2 ? 1 : 0); // DIMTOL
            code_i(s, 72, ds.tol_method == 3 ? 1 : 0);                      // DIMLIM
            code_d(s, 47, ds.tol_upper);                                    // DIMTP
            code_d(s, 48, ds.tol_method == 1 ? ds.tol_upper : ds.tol_lower); // DIMTM
            if (ds.tol_method == 4) {
                code_d(s, 147, -0.625); // DIMGAP < 0: the text boxed
            }
        }
        if (ds.tol_precision >= 0) {
            code_i(s, 272, ds.tol_precision); // DIMTDEC
        }
        code_d(s, 146, ds.tol_height); // DIMTFAC
    }
    code(s, 0, "ENDTAB");
    // BLOCK_RECORD: the two spaces and every definition (R2000 readers look them up); the
    // entities name theirs as their owner.
    const auto space_blocks = std::count_if(doc.block_defs.begin(), doc.block_defs.end(),
                                            [](const DocBlockDef& b) { return is_space_block(b.name); });
    g_table = emit_table_head(s, "BLOCK_RECORD",
                              static_cast<long>(doc.block_defs.size()) + 2 - static_cast<long>(space_blocks));
    const auto block_record = [&](const std::string& name, std::uint8_t units) {
        const std::uint64_t h = emit_record_head(s, "BLOCK_RECORD", "AcDbBlockTableRecord");
        code(s, 2, name);
        code_i(s, 70, units); // the block's insertion units
        return h;
    };
    g_model_record = block_record("*Model_Space", 0);
    g_paper_record = block_record("*Paper_Space", 0);
    g_block_records.clear();
    for (const DocBlockDef& b : doc.block_defs) {
        g_block_records.push_back(is_space_block(b.name) ? 0 : block_record(b.name, b.units));
    }
    code(s, 0, "ENDTAB");
    code(s, 0, "ENDSEC");
}

// Per-entity layer/colour/linetype/lineweight (after geometry codes).
void emit_props_body(std::string& s, const Document& doc, const EntityProps& p);

std::uint64_t emit_props(std::string& s, const Document& doc, const EntityProps& p) {
    const std::uint64_t h = emit_handle(s);
    emit_props_body(s, doc, p);
    return h;
}

void emit_props_body(std::string& s, const Document& doc, const EntityProps& p) {
    // Its owner, then AcDbEntity: in paper space (67) when it is not in a block.
    code(s, 330, handle_hex(g_owner != 0 ? g_owner : p.space() != 0 ? g_paper_record : g_model_record));
    code(s, 100, "AcDbEntity");
    if (g_owner == 0 && p.space() != 0) {
        code_i(s, 67, 1); // paper space (the layouts are written as one paper space)
    }
    const std::string layer =
        p.layer < doc.layers.size() ? doc.layers[p.layer].name : std::string("0");
    code(s, 8, layer);
    if (p.color_by_block()) {
        code_i(s, 62, 0); // ByBlock
    } else if (p.color_by_layer()) {
        code_i(s, 62, 256); // ByLayer
    } else {
        code_i(s, 420, true_color(p.color));
    }
    if (p.linetype_by_block()) {
        code(s, 6, "BYBLOCK");
    } else if (!p.linetype_by_layer()) {
        code(s, 6, linetype_name(p.linetype));
    }
    if (p.lineweight_by_block()) {
        code_i(s, 370, -2);
    } else if (!p.lineweight_by_layer()) {
        code_i(s, 370, p.lineweight);
    }
    // Transparency (440): 0x02000000 with the opacity (0 clear .. 255 opaque) in the low
    // byte, or 0x01000000 for ByBlock; none for ByLayer.
    if (p.transparency == EntityProps::kTransparencyByBlock) {
        code_i(s, 440, 0x01000000L);
    } else if (p.transparency <= 90) {
        code_i(s, 440, 0x02000000L | std::lround(255.0 * (100.0 - p.transparency) / 100.0));
    }
}

} // namespace

/// What the OBJECTS section needs from the entities pass: the IMAGEDEF handle of every
/// image definition (assigned before the entities) and, per IMAGE entity, the reactor
/// handle it referenced. Plus the section boundaries, so the file can be assembled in
/// the order other readers expect (BLOCKS before ENTITIES).
struct WriterRefs {
    std::vector<std::uint64_t> layer_handles; ///< LAYER table handles by index (VIEWPORT 331)
    std::vector<std::uint64_t> def_handles;
    std::vector<std::uint64_t> mlstyle_handles; ///< MLINESTYLE handles by style index
    std::vector<std::pair<std::uint64_t, std::uint64_t>> reactors; ///< (image, reactor)
    /// A dimension's DIMASSOC object (#59): its extension dictionary and itself, their handles
    /// given before the entities so the objects it names can carry it as a reactor; the
    /// dimension's handle, and the references with their objects' handles, as written.
    struct DimAssoc {
        std::uint32_t dim = 0;
        std::uint64_t dict = 0;
        std::uint64_t assoc = 0;
        std::uint64_t dim_handle = 0;
        std::vector<const DocDimRef*> refs;
        std::vector<std::uint64_t> objects;
    };
    std::vector<DimAssoc> dim_assocs;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> reactors_on; ///< object key -> DIMASSOCs
    std::unordered_map<std::uint32_t, std::size_t> assoc_of_dim;               ///< dimension -> dim_assocs
    std::unordered_map<std::uint64_t, std::uint64_t> handle_of;                ///< object key -> its handle
    std::unordered_map<std::uint32_t, std::uint64_t> hatch_handle;             ///< associative hatch -> its handle
    std::size_t entities_begin = 0;
    std::size_t blocks_begin = 0;
    std::size_t blocks_end = 0;
};

void emit_objects(std::string& s, const Document& doc, const WriterRefs& refs);

void serialize_body(std::string& s, const Document& doc, WriterRefs& refs) {
    refs.layer_handles.clear();
    emit_layer_table(s, doc, refs.layer_handles);
    refs.def_handles.clear();
    for (std::size_t i = 0; i < doc.image_defs.size(); ++i) {
        refs.def_handles.push_back(g_handle++);
    }
    refs.mlstyle_handles.clear();
    for (std::size_t i = 0; i < doc.mline_styles.size(); ++i) {
        refs.mlstyle_handles.push_back(g_handle++);
    }

    // The DIMASSOC objects (#59), one per dimension with points on objects. A polyline the
    // file writes as a heavy POLYLINE (a fitted one) is not named: its vertices are other
    // records. A key is an object's (kind, index).
    const auto key_of = [](EntityKind k, std::size_t i) {
        return (static_cast<std::uint64_t>(k) << 32) | static_cast<std::uint64_t>(i);
    };
    refs.dim_assocs.clear();
    refs.reactors_on.clear();
    refs.assoc_of_dim.clear();
    refs.handle_of.clear();
    for (const DocDimRef& r : doc.dim_refs) {
        if (r.dim >= doc.dims.size()) {
            continue;
        }
        if (static_cast<EntityKind>(r.kind) == EntityKind::Polyline) {
            if (r.index >= doc.polylines.size()) {
                continue;
            }
            const DocPolyline& p = doc.polylines[r.index];
            if ((p.fit != 0 && p.frame.size() >= 2) || !p.tangents.empty()) {
                continue;
            }
        }
        auto [it, fresh] = refs.assoc_of_dim.try_emplace(r.dim, refs.dim_assocs.size());
        if (fresh) {
            WriterRefs::DimAssoc a;
            a.dim = r.dim;
            a.dict = g_handle++;
            a.assoc = g_handle++;
            refs.dim_assocs.push_back(std::move(a));
        }
        WriterRefs::DimAssoc& a = refs.dim_assocs[it->second];
        a.refs.push_back(&r);
        std::vector<std::uint64_t>& on = refs.reactors_on[key_of(static_cast<EntityKind>(r.kind), r.index)];
        if (std::find(on.begin(), on.end(), a.assoc) == on.end()) {
            on.push_back(a.assoc);
        }
    }
    // An associative hatch (#41): its handle given now, so the objects its loops lie on can
    // carry it as a reactor; they are written before the hatches.
    refs.hatch_handle.clear();
    for (const DocHatchRef& r : doc.hatch_refs) {
        if (r.hatch >= doc.hatches.size() || doc.hatches[r.hatch].pattern_name == "WIPEOUT" ||
            doc.hatches[r.hatch].unassoc) {
            continue;
        }
        auto [it, fresh] = refs.hatch_handle.try_emplace(r.hatch, 0);
        if (fresh) {
            it->second = g_handle++;
        }
        std::vector<std::uint64_t>& on = refs.reactors_on[key_of(static_cast<EntityKind>(r.kind), r.index)];
        if (std::find(on.begin(), on.end(), it->second) == on.end()) {
            on.push_back(it->second);
        }
    }
    // An entity's handle, then the DIMASSOC objects it is measured by (its reactors) or, for a
    // dimension, the dictionary that holds its DIMASSOC, then its properties.
    const auto emit_props_at = [&](EntityKind kind, std::size_t index, const EntityProps& props) {
        const std::uint64_t key = key_of(kind, index);
        const auto on = refs.reactors_on.find(key);
        const auto own = kind == EntityKind::Dimension ? refs.assoc_of_dim.find(static_cast<std::uint32_t>(index))
                                                       : refs.assoc_of_dim.end();
        const std::uint64_t h = emit_handle(s);
        if (on != refs.reactors_on.end()) {
            refs.handle_of[key] = h;
            code(s, 102, "{ACAD_REACTORS");
            for (const std::uint64_t a : on->second) {
                code(s, 330, handle_hex(a));
            }
            code(s, 102, "}");
        }
        if (own != refs.assoc_of_dim.end()) {
            WriterRefs::DimAssoc& a = refs.dim_assocs[own->second];
            a.dim_handle = h;
            code(s, 102, "{ACAD_XDICTIONARY");
            code(s, 360, handle_hex(a.dict));
            code(s, 102, "}");
        }
        emit_props_body(s, doc, props);
    };

    refs.entities_begin = s.size();
    code(s, 0, "SECTION");
    code(s, 2, "ENTITIES");

    for (const DocPoint& p : doc.points) {
        code(s, 0, "POINT");
        emit_props_at(EntityKind::Point, static_cast<std::size_t>(&p - doc.points.data()), p.props);
        code(s, 100, "AcDbPoint");
        code_d(s, 10, p.p.x);
        code_d(s, 20, p.p.y);
        code_d(s, 30, 0.0);
    }
    // ELLIPSE: 10/20 centre, 11/21 major-axis endpoint RELATIVE to the centre, 40
    // minor/major ratio, 41/42 start/end parameters (radians) -- our exact definition.
    for (const DocEllipse& e : doc.ellipses) {
        code(s, 0, "ELLIPSE");
        emit_props_at(EntityKind::Ellipse, static_cast<std::size_t>(&e - doc.ellipses.data()), e.props);
        code(s, 100, "AcDbEllipse");
        code_d(s, 10, e.center.x);
        code_d(s, 20, e.center.y);
        code_d(s, 30, 0.0);
        code_d(s, 11, e.major.x);
        code_d(s, 21, e.major.y);
        code_d(s, 31, 0.0);
        code_d(s, 40, e.ratio);
        code_d(s, 41, e.start);
        code_d(s, 42, e.end);
    }
    // SPLINE: 70 flags (8 = planar), 71 degree, 72/73 knot/control counts, 40 knots
    // (the spline's own, or the clamped uniform vector it is drawn with), 10/20 control points.
    for (const DocSpline& sp : doc.splines) {
        const int n = static_cast<int>(sp.control_points.size());
        if (n < 2) {
            continue;
        }
        const int deg = spline::effective_degree(n, static_cast<int>(sp.degree));
        const std::vector<double> knots =
            spline::usable_knots(sp.knots, n, deg) ? sp.knots : spline::clamped_knots(n, deg);
        // A spline by fit points carries them (11, a closed one's first again at the end), the
        // fit tolerance (44) and the end tangents (12, 13), as AutoCAD writes its fit data.
        std::vector<Vec2> fit_pts = sp.fit.points;
        if (sp.fit.closed && !fit_pts.empty()) {
            fit_pts.push_back(fit_pts.front());
        }
        code(s, 0, "SPLINE");
        emit_props_at(EntityKind::Spline, static_cast<std::size_t>(&sp - doc.splines.data()), sp.props);
        code(s, 100, "AcDbSpline");
        code(s, 70, "8");
        code(s, 71, std::to_string(deg).c_str());
        code(s, 72, std::to_string(knots.size()).c_str());
        code(s, 73, std::to_string(n).c_str());
        code(s, 74, std::to_string(fit_pts.size()).c_str());
        if (!fit_pts.empty()) {
            code_d(s, 44, sp.fit.tolerance);
            if (sp.fit.start_tangent) {
                code_d(s, 12, sp.fit.start_tangent->x);
                code_d(s, 22, sp.fit.start_tangent->y);
                code_d(s, 32, 0.0);
            }
            if (sp.fit.end_tangent) {
                code_d(s, 13, sp.fit.end_tangent->x);
                code_d(s, 23, sp.fit.end_tangent->y);
                code_d(s, 33, 0.0);
            }
        }
        for (const double k : knots) {
            code_d(s, 40, k);
        }
        for (const Vec2& p : sp.control_points) {
            code_d(s, 10, p.x);
            code_d(s, 20, p.y);
            code_d(s, 30, 0.0);
        }
        for (const Vec2& p : fit_pts) {
            code_d(s, 11, p.x);
            code_d(s, 21, p.y);
            code_d(s, 31, 0.0);
        }
    }
    // XLINE / RAY: DXF code 10 = base (first) point, code 11 = unit direction vector.
    for (const DocXline& x : doc.xlines) {
        code(s, 0, x.ray ? "RAY" : "XLINE");
        emit_props(s, doc, x.props);
        code(s, 100, x.ray ? "AcDbRay" : "AcDbXline");
        code_d(s, 10, x.base.x);
        code_d(s, 20, x.base.y);
        code_d(s, 30, 0.0);
        code_d(s, 11, x.dir.x);
        code_d(s, 21, x.dir.y);
        code_d(s, 31, 0.0);
    }
    // MLINE: the style (2, and its MLINESTYLE's handle 340), scale 40, justification 70, flags
    // 71, then per vertex its point (11), the direction of the segment from it (12) and the
    // mitre direction (13), and per element 74 parameters (41): how far along the mitre the
    // element starts, then the gaps along the segment from there.
    for (const DocMLine& m : doc.mlines) {
        const std::size_t nv = m.vertices.size();
        if (nv < 2) {
            continue;
        }
        static const MLineStyle kStandard{};
        const MLineStyle& mst = m.style < doc.mline_styles.size() ? doc.mline_styles[m.style] : kStandard;
        const std::vector<Vec2> mw = mline::miters(m.vertices, m.closed, mst.start_angle, mst.end_angle);
        const std::vector<double> md = mline::offsets(mst, m.scale, m.justification);
        code(s, 0, "MLINE");
        emit_handle(s);
        emit_props_body(s, doc, m.props);
        code(s, 100, "AcDbMline");
        code(s, 2, mst.name);
        if (m.style < refs.mlstyle_handles.size()) {
            code(s, 340, handle_hex(refs.mlstyle_handles[m.style]));
        }
        code_d(s, 40, m.scale);
        code_i(s, 70, m.justification);
        code_i(s, 71, 1 | (m.closed ? 2 : 0) | (m.no_start_caps ? 4 : 0) | (m.no_end_caps ? 8 : 0));
        code_i(s, 72, static_cast<long>(nv));
        code_i(s, 73, static_cast<long>(md.size()));
        code_d(s, 10, m.vertices[0].x);
        code_d(s, 20, m.vertices[0].y);
        code_d(s, 30, 0.0);
        code_d(s, 210, 0.0);
        code_d(s, 220, 0.0);
        code_d(s, 230, 1.0);
        for (std::size_t i = 0; i < nv; ++i) {
            const std::size_t j = i + 1 < nv ? i + 1 : (m.closed ? 0 : i);
            Vec2 dir = j != i ? m.vertices[j] - m.vertices[i] : m.vertices[i] - m.vertices[i - 1];
            const double dl = length(dir);
            dir = dl > 1e-12 ? dir * (1.0 / dl) : Vec2{1.0, 0.0};
            const double wl = length(mw[i]);
            const Vec2 miter = wl > 1e-12 ? mw[i] * (1.0 / wl) : Vec2{-dir.y, dir.x};
            code_d(s, 11, m.vertices[i].x);
            code_d(s, 21, m.vertices[i].y);
            code_d(s, 31, 0.0);
            code_d(s, 12, dir.x);
            code_d(s, 22, dir.y);
            code_d(s, 32, 0.0);
            code_d(s, 13, miter.x);
            code_d(s, 23, miter.y);
            code_d(s, 33, 0.0);
            for (std::size_t e = 0; e < md.size(); ++e) {
                const std::vector<double>* gaps =
                    i < m.breaks.size() && e < m.breaks[i].size() ? &m.breaks[i][e] : nullptr;
                code_i(s, 74, static_cast<long>(1 + (gaps != nullptr ? gaps->size() : 0)));
                code_d(s, 41, md[e] * wl);
                if (gaps != nullptr) {
                    for (const double g : *gaps) {
                        code_d(s, 41, g);
                    }
                }
                code_i(s, 75, 0);
            }
        }
    }
    for (const DocLine& l : doc.lines) {
        code(s, 0, "LINE");
        emit_props_at(EntityKind::Line, static_cast<std::size_t>(&l - doc.lines.data()), l.props);
        if (l.celtscale != 1.0) {
            code_d(s, 48, l.celtscale); // per-entity linetype scale (CELTSCALE)
        }
        code(s, 100, "AcDbLine");
        code_d(s, 10, l.a.x);
        code_d(s, 20, l.a.y);
        code_d(s, 30, 0.0);
        code_d(s, 11, l.b.x);
        code_d(s, 21, l.b.y);
        code_d(s, 31, 0.0);
    }
    for (const DocCircle& c : doc.circles) {
        code(s, 0, "CIRCLE");
        emit_props_at(EntityKind::Circle, static_cast<std::size_t>(&c - doc.circles.data()), c.props);
        if (c.celtscale != 1.0) {
            code_d(s, 48, c.celtscale);
        }
        code(s, 100, "AcDbCircle");
        code_d(s, 10, c.center.x);
        code_d(s, 20, c.center.y);
        code_d(s, 30, 0.0);
        code_d(s, 40, c.radius);
    }
    for (const DocArc& a : doc.arcs) {
        code(s, 0, "ARC");
        emit_props_at(EntityKind::Arc, static_cast<std::size_t>(&a - doc.arcs.data()), a.props);
        if (a.celtscale != 1.0) {
            code_d(s, 48, a.celtscale);
        }
        code(s, 100, "AcDbCircle");
        code_d(s, 10, a.center.x);
        code_d(s, 20, a.center.y);
        code_d(s, 30, 0.0);
        code_d(s, 40, a.radius);
        code(s, 100, "AcDbArc");
        code_d(s, 50, to_degrees(a.start_angle));
        code_d(s, 51, to_degrees(a.end_angle));
    }
    // A fitted polyline (PEDIT Fit or Spline) or one with a vertex tangent goes out as a
    // POLYLINE: an LWPOLYLINE keeps neither its frame nor its tangents. Spline-fit: the frame
    // (vertex flag 16) then the curve (8); curve-fit: the polyline's own vertices and the ones
    // Fit added (1) in order; a tangent set: flag 2, code 50.
    const auto emit_heavy_polyline = [&](const DocPolyline& p) {
        const auto tangent = [&](std::size_t i) {
            return i < p.tangents.size() && !std::isnan(p.tangents[i]) ? p.tangents[i]
                                                                       : std::numeric_limits<double>::quiet_NaN();
        };
        const bool spline = p.fit == 5 || p.fit == 6;
        code(s, 0, "POLYLINE");
        const std::uint64_t parent = emit_props(s, doc, p.props);
        const std::uint64_t owner = g_owner;
        if (p.celtscale != 1.0) {
            code_d(s, 48, p.celtscale);
        }
        code(s, 100, "AcDb2dPolyline");
        code_i(s, 66, 1);
        code_d(s, 10, 0.0);
        code_d(s, 20, 0.0);
        code_d(s, 30, p.elevation);
        if (p.thickness != 0.0) {
            code_d(s, 39, p.thickness);
        }
        code_i(s, 70, (p.closed ? 1 : 0) | (p.fit == 1 ? 2 : 0) | (spline ? 4 : 0) | (p.ltype_gen ? 128 : 0));
        if (spline) {
            code_i(s, 75, p.fit);
        }
        const bool has_bulge = p.bulges.size() == p.points.size();
        const bool has_width = p.widths.size() == 2 * p.points.size();
        const auto vertex = [&](Vec2 at, long flags, double bulge, double w0, double w1, double tan) {
            code(s, 0, "VERTEX");
            emit_props(s, doc, p.props);
            code(s, 100, "AcDbVertex");
            code(s, 100, "AcDb2dVertex");
            code_d(s, 10, at.x);
            code_d(s, 20, at.y);
            code_d(s, 30, p.elevation);
            if (w0 != 0.0 || w1 != 0.0) {
                code_d(s, 40, w0);
                code_d(s, 41, w1);
            }
            if (bulge != 0.0) {
                code_d(s, 42, bulge);
            }
            code_i(s, 70, flags | (std::isnan(tan) ? 0 : 2));
            if (!std::isnan(tan)) {
                code_d(s, 50, to_degrees(tan));
            }
        };
        const double none = std::numeric_limits<double>::quiet_NaN();
        g_owner = parent; // its VERTEX and SEQEND records are the POLYLINE's
        if (spline) {
            for (std::size_t i = 0; i < p.frame.size(); ++i) {
                vertex(p.frame[i], 16, 0.0, 0.0, 0.0, tangent(i));
            }
        }
        std::size_t k = 0; // the next of the frame a curve-fit vertex can be
        for (std::size_t i = 0; i < p.points.size(); ++i) {
            const double b = has_bulge ? p.bulges[i] : 0.0;
            const double w0 = has_width ? p.widths[2 * i] : 0.0;
            const double w1 = has_width ? p.widths[2 * i + 1] : 0.0;
            if (spline) {
                vertex(p.points[i], 8, b, w0, w1, none);
            } else if (p.fit == 1) {
                const bool own = k < p.frame.size() && length(p.points[i] - p.frame[k]) < 1e-9;
                vertex(p.points[i], own ? 0 : 1, b, w0, w1, own ? tangent(k) : none);
                k += own ? 1 : 0;
            } else {
                vertex(p.points[i], 0, b, w0, w1, tangent(i));
            }
        }
        code(s, 0, "SEQEND");
        emit_props(s, doc, p.props);
        g_owner = owner;
    };
    for (const DocPolyline& p : doc.polylines) {
        if ((p.fit != 0 && p.frame.size() >= 2) || !p.tangents.empty()) {
            emit_heavy_polyline(p);
            continue;
        }
        code(s, 0, "LWPOLYLINE");
        emit_props_at(EntityKind::Polyline, static_cast<std::size_t>(&p - doc.polylines.data()), p.props);
        if (p.celtscale != 1.0) {
            code_d(s, 48, p.celtscale);
        }
        code(s, 100, "AcDbPolyline");
        code_i(s, 90, static_cast<long>(p.points.size()));
        code_i(s, 70, (p.closed ? 1 : 0) | (p.ltype_gen ? 128 : 0)); // 128: linetype generation
        emit_polyline_widths_header(s, p);
        const bool has_bulge = p.bulges.size() == p.points.size();
        for (std::size_t i = 0; i < p.points.size(); ++i) {
            code_d(s, 10, p.points[i].x);
            code_d(s, 20, p.points[i].y);
            emit_vertex_widths(s, p, i);
            // Code 42 is the vertex bulge; emit only non-zero ones (DXF convention).
            if (has_bulge && p.bulges[i] != 0.0) {
                code_d(s, 42, p.bulges[i]);
            }
        }
    }
    for (const DocText& t : doc.texts) {
        code(s, 0, "TEXT");
        emit_props(s, doc, t.props);
        code(s, 100, "AcDbText");
        const int vertical = emit_text_place(s, doc, t, t.content);
        code(s, 1, t.content);
        if (t.style != 0 && t.style < doc.text_styles.size()) {
            code(s, 7, doc.text_styles[t.style].name); // the STYLE table entry
        } else if (!t.font.empty()) {
            code(s, 7, t.font); // style name = font (re-imported via the code-7 fallback)
        }
        emit_text_end(s, vertical);
    }
    for (const DocAttDef& a : doc.attdefs) {
        code(s, 0, "ATTDEF");
        emit_props(s, doc, a.text.props);
        code(s, 100, "AcDbText");
        const int vertical = emit_text_place(s, doc, a.text, a.text.content);
        code(s, 1, a.def);            // default value
        if (a.text.style != 0 && a.text.style < doc.text_styles.size()) {
            code(s, 7, doc.text_styles[a.text.style].name);
        } else if (!a.text.font.empty()) {
            code(s, 7, a.text.font);
        }
        code(s, 100, "AcDbAttributeDefinition");
        code(s, 3, a.prompt);         // prompt
        code(s, 2, a.text.content);   // tag
        code_i(s, 70, a.flags);       // 1 invisible, 2 constant, 4 verify, 8 preset
        if (vertical != 0) {
            code_i(s, 74, vertical);
        }
    }
    for (const DocDim& d : doc.dims) {
        const std::string style =
            d.style < doc.dimstyles.size() ? doc.dimstyles[d.style].name : std::string("Standard");
        code(s, 0, "DIMENSION");
        emit_props_at(EntityKind::Dimension, static_cast<std::size_t>(&d - doc.dims.data()), d.props);
        code(s, 100, "AcDbDimension");
        code(s, 2, "*D0"); // anonymous block name (graphics regenerated by readers)
        const auto kind = static_cast<DimType>(d.type);
        const bool radial = kind == DimType::Radius || kind == DimType::Diameter || kind == DimType::Jogged;
        // The groups by type, as AutoCAD lays them out. Linear and aligned: 10 on the
        // dimension line, 13 and 14 the extension lines' points. An ordinate's 10 is the
        // origin it measures from (AutoCAD's UCS origin then). Radial: 10 the centre (a
        // diameter's far side) and 15 the point on the circle. Angular: 15 the vertex, 13 and
        // 14 the rays, 10 a point on the arc. 11 is where the text goes.
        Vec2 p10 = kind == DimType::Ordinate ? d.origin : d.line_pt;
        Vec2 p11 = d.line_pt;
        if (radial) {
            p10 = kind == DimType::Diameter ? d.a * 2.0 - d.b : d.a;
        } else if (kind == DimType::Angular) {
            const Vec2 r1 = d.b - d.a;
            const Vec2 r2 = d.line_pt - d.a;
            const double radius = d.aux > 0.0 ? d.aux : std::max(length(r1), length(r2)) * 0.8;
            double sweep = ccw_sweep(r1, r2);
            if (d.aux <= 0.0 && sweep > kPi) { // the smaller angle, either way round
                sweep -= kTwoPi;
            }
            const double mid = std::atan2(r1.y, r1.x) + sweep * 0.5;
            p10 = d.a + Vec2{std::cos(mid), std::sin(mid)} * radius;
            p11 = p10;
        }
        code_d(s, 10, p10.x);
        code_d(s, 20, p10.y);
        code_d(s, 30, 0.0);
        code_d(s, 11, p11.x);
        code_d(s, 21, p11.y);
        code_d(s, 31, 0.0);
        // 70 dim type: 0 rotated/linear, 1 aligned, 3 diameter, 4 radius, 5 angular.
        long dimtype = 0;
        switch (static_cast<DimType>(d.type)) {
        case DimType::Aligned:
            dimtype = 1;
            break;
        case DimType::Diameter:
            dimtype = 3;
            break;
        case DimType::Radius:
            dimtype = 4;
            break;
        case DimType::Angular:
            dimtype = 5;
            break;
        case DimType::Ordinate:
            dimtype = 6 + (d.aux < 0.5 ? 64 : 0); // bit 64 = X-type ordinate
            break;
        case DimType::Jogged:
            dimtype = 4; // written as a radius dimension: same value, the jog is lost
            break;
        case DimType::ArcLength:
            dimtype = 1; // written as an aligned dimension between the arc's ends,
                         // with the arc length as the text override (code 1 below)
            break;
        case DimType::Linear:
            dimtype = 0;
            break;
        }
        code_i(s, 70, dimtype);
        // Text decoration -> the DIMENSION text OVERRIDE (group code 1), so other CAD
        // shows the decorated value instead of a bare measurement. The string is built
        // by the SAME compose_dim_label the renderer uses, so what a vendor sees can
        // never disagree with what Musa CAD draws.
        //
        // Honest gap: DXF has no place for the tolerance SEMANTICS (mode + deviations)
        // that Musa CAD stores, so a re-import returns the composed string as a text
        // override, not as an editable tolerance. The decoration is native-only --
        // exactly the gap already documented for per-dimension overrides. The measured
        // value itself is unaffected: it is recomputed from the def points as always.
        if (d.tol.mode == TolMode::None && d.prefix.empty() && d.suffix.empty() && !d.text_override.empty()) {
            // The override on its own: as it was typed, so <> stays the measurement.
            std::string text;
            for (const char ch : d.text_override) {
                text += ch == '\n' ? std::string("\\P") : std::string(1, ch);
            }
            code(s, 1, text);
        } else if (d.tol.mode != TolMode::None || !d.prefix.empty() || !d.suffix.empty()) {
            DimData dd;
            dd.type = static_cast<DimType>(d.type);
            dd.a = d.a;
            dd.b = d.b;
            dd.line_pt = d.line_pt;
            dd.tol = d.tol;
            const DimStyle ds = d.style < doc.dimstyles.size() ? doc.dimstyles[d.style] : DimStyle{};
            const DimLabel lbl =
                compose_dim_label(dd, apply_dim_overrides(ds, d.overrides),
                                  {d.prefix, d.suffix, d.text_override});
            // A two-line (limits) label becomes AutoCAD's stacked-text form.
            code(s, 1, lbl.line2.empty() ? lbl.line1 : lbl.line1 + "\\P" + lbl.line2);
        }
        if (d.text_angle != 0.0) {
            code_d(s, 53, to_degrees(d.text_angle)); // the text's own angle
        }
        code(s, 3, style);
        // The type's own part. Linear and aligned: the extension lines' points, a rotated
        // one's angle (50) and the extension lines' (52). Radial: the point on the circle
        // (15). Angular: the rays' points and the vertex. Ordinate: the feature and the
        // leader's end.
        Vec2 pa = d.a;
        Vec2 pb = d.b;
        if (kind == DimType::ArcLength) {
            // Aligned between the arc's two ends (a = centre, b = start, aux = end angle).
            const double r = std::hypot(d.b.x - d.a.x, d.b.y - d.a.y);
            pa = d.b;
            pb = {d.a.x + r * std::cos(d.aux), d.a.y + r * std::sin(d.aux)};
        } else if (kind == DimType::Angular) {
            pa = d.b;
            pb = d.line_pt;
        }
        const char* subclass = kind == DimType::Angular   ? "AcDb3PointAngularDimension"
                               : kind == DimType::Ordinate ? "AcDbOrdinateDimension"
                               : kind == DimType::Diameter ? "AcDbDiametricDimension"
                               : radial                    ? "AcDbRadialDimension"
                                                           : "AcDbAlignedDimension";
        code(s, 100, subclass);
        if (!radial) {
            code_d(s, 13, pa.x);
            code_d(s, 23, pa.y);
            code_d(s, 33, 0.0);
            code_d(s, 14, pb.x);
            code_d(s, 24, pb.y);
            code_d(s, 34, 0.0);
        }
        if (radial || kind == DimType::Angular) {
            const Vec2 p15 = radial ? d.b : d.a;
            code_d(s, 15, p15.x);
            code_d(s, 25, p15.y);
            code_d(s, 35, 0.0);
        }
        if (radial) {
            code_d(s, 40, 0.0); // the leader length: none
        }
        if (static_cast<DimType>(d.type) == DimType::Linear) {
            // 50: the angle of a rotated (horizontal, vertical, rotated) dimension's line.
            DimData dl;
            dl.type = DimType::Linear;
            dl.a = d.a;
            dl.b = d.b;
            dl.aux = d.aux;
            const Vec2 u = dim_line_direction(dl);
            code_d(s, 50, to_degrees(std::atan2(u.y, u.x)));
        }
        if (d.oblique != 0.0 && !radial && kind != DimType::Angular && kind != DimType::Ordinate) {
            code_d(s, 52, to_degrees(d.oblique)); // the extension lines' angle
        }
        if (kind == DimType::Linear) {
            code(s, 100, "AcDbRotatedDimension");
        }
    }
    // Leaders: exported as a DXF LEADER (vertices) plus a TEXT label so any reader
    // shows them. Import reconstructs them as line + text (see docs); Musa keeps
    // leaders as first-class entities via the native format.
    // TOLERANCE (GD&T): a feature control frame is its cell strings joined by %%v, the
    // dimstyle it draws with, its insertion point and x-axis direction. A datum feature
    // symbol is written as a one-cell frame holding the datum letter (AutoCAD's own
    // form); its leader triangle has no DXF counterpart.
    for (const DocFcf& f : doc.fcfs) {
        code(s, 0, "TOLERANCE");
        emit_props(s, doc, f.props);
        code(s, 100, "AcDbFcf");
        code(s, 3, f.style < doc.dimstyles.size() ? doc.dimstyles[f.style].name : std::string("Standard"));
        code_d(s, 10, f.pos.x);
        code_d(s, 20, f.pos.y);
        code_d(s, 30, 0.0);
        std::string text;
        for (std::size_t i = 0; i < f.cells.size(); ++i) {
            if (f.cells[i] == "^J") {
                text += "^J"; // the next row, as AutoCAD writes it
                continue;
            }
            if (i > 0 && f.cells[i - 1] != "^J") {
                text += "%%v";
            }
            text += gdt_to_dxf(f.cells[i]);
        }
        code(s, 1, text);
        code_d(s, 11, std::cos(f.rotation));
        code_d(s, 21, std::sin(f.rotation));
        code_d(s, 31, 0.0);
    }
    for (const DocDatum& d : doc.datums) {
        code(s, 0, "TOLERANCE");
        emit_props(s, doc, d.props);
        code(s, 100, "AcDbFcf");
        code(s, 3, d.style < doc.dimstyles.size() ? doc.dimstyles[d.style].name : std::string("Standard"));
        code_d(s, 10, d.pos.x);
        code_d(s, 20, d.pos.y);
        code_d(s, 30, 0.0);
        code(s, 1, d.letter);
        code_d(s, 11, std::cos(d.rotation));
        code_d(s, 21, std::sin(d.rotation));
        code_d(s, 31, 0.0);
    }
    for (const DocLeader& l : doc.leaders) {
        code(s, 0, "LEADER");
        emit_props(s, doc, l.props);
        code(s, 100, "AcDbLeader");
        const std::string style =
            l.style < doc.dimstyles.size() ? doc.dimstyles[l.style].name : std::string("Standard");
        code(s, 3, style);
        code_i(s, 76, 2); // vertex count
        code_d(s, 10, l.tip.x);
        code_d(s, 20, l.tip.y);
        code_d(s, 30, 0.0);
        code_d(s, 10, l.knee.x);
        code_d(s, 20, l.knee.y);
        code_d(s, 30, 0.0);
        if (!l.content.empty()) {
            code(s, 0, "TEXT");
            emit_props(s, doc, l.props);
            code(s, 100, "AcDbText");
            code_d(s, 10, l.knee.x);
            code_d(s, 20, l.knee.y);
            code_d(s, 30, 0.0);
            code_d(s, 40, l.text_height);
            code(s, 1, l.content);
            emit_text_end(s, 0);
        }
    }
    // MTEXT: standard group codes (read by AutoCAD/LibreCAD).
    const auto emit_mtext = [&](const MTextBlock& b, const std::string& content,
                                const EntityProps& props, const std::string& font = {}) {
        code(s, 0, "MTEXT");
        emit_props(s, doc, props);
        code(s, 100, "AcDbMText");
        code_d(s, 10, b.pos.x);
        code_d(s, 20, b.pos.y);
        code_d(s, 30, 0.0);
        code_d(s, 40, b.height);
        if (b.width > 0.0) {
            code_d(s, 41, b.width); // reference rectangle width
        }
        code_i(s, 71, b.attach + 1); // DXF attachment is 1..9
        code_d(s, 50, to_degrees(b.rotation));
        if (!font.empty()) {
            code(s, 7, font); // style name = font (re-imported via the code-7 fallback)
        }
        // MTEXT encodes hard line breaks as "\P" (a literal newline would break the
        // DXF code/value line structure).
        std::string enc;
        for (const char c : content) {
            if (c == '\n') {
                enc += "\\P";
            } else {
                enc += c;
            }
        }
        code(s, 1, enc);
    };
    for (const DocMText& m : doc.mtexts) {
        emit_mtext(m.block, m.content, m.props, m.font);
    }
    // MLEADER: full MLEADER block is heavy; write a readable LEADER (the leader
    // line via vertices) + an MTEXT label. Round-trips through Musa via the native
    // format; through DXF it comes back as a leader line + MTEXT (association not
    // preserved -- stated fidelity gap).
    for (const DocMLeader& m : doc.mleaders) {
        if (m.vertices.size() >= 2 && (m.leader_flags & 2u) == 0) {
            code(s, 0, "LEADER");
            emit_props(s, doc, m.props);
            code(s, 100, "AcDbLeader");
            code_i(s, 71, (m.leader_flags & 4u) != 0 ? 0 : 1); // arrowhead flag
            code_i(s, 72, (m.leader_flags & 1u) != 0 ? 1 : 0); // path type: straight / spline
            code_i(s, 76, static_cast<long>(m.vertices.size()));
            for (const Vec2& v : m.vertices) {
                code_d(s, 10, v.x);
                code_d(s, 20, v.y);
                code_d(s, 30, 0.0);
            }
            // The leaders MLEADEREDIT added: each a LEADER of its own, to the landing.
            const Vec2 attach = mleader_attach(m.vertices, m.leader_flags);
            for (const Vec2& tip : m.extra_tips) {
                code(s, 0, "LEADER");
                emit_props(s, doc, m.props);
                code(s, 100, "AcDbLeader");
                code_i(s, 71, (m.leader_flags & 4u) != 0 ? 0 : 1);
                code_i(s, 72, 0);
                code_i(s, 76, 2);
                for (const Vec2& v : {tip, attach}) {
                    code_d(s, 10, v.x);
                    code_d(s, 20, v.y);
                    code_d(s, 30, 0.0);
                }
            }
        }
        if (m.content_block.empty() && (m.block.str_len != 0 || !m.content.empty())) {
            emit_mtext(m.block, m.content, m.props, m.font); // carry the label font (code 7)
        }
    }
    // HATCH: AutoCAD-standard codes. Each boundary loop is emitted as a polyline path
    // (code 92 bit 2 = polyline); the outer (loop 0) also sets the external bit (1).
    // WIPEOUT: AutoCAD's wipeout, a raster image of one pixel clipped to the polygon -- the
    // insertion point at the polygon's lower-left corner, the U and V vectors its width and
    // height, the clip boundary in that pixel's coordinates (-0.5 to 0.5, v down), closed.
    // One per boundary.
    for (const DocHatch& h : doc.hatches) {
        if (h.pattern_name != "WIPEOUT") {
            continue;
        }
        for (const std::vector<Vec2>& loop : h.loops) {
            if (loop.size() < 3) {
                continue;
            }
            Vec2 lo = loop.front();
            Vec2 hi = loop.front();
            for (const Vec2& v : loop) {
                lo = {std::min(lo.x, v.x), std::min(lo.y, v.y)};
                hi = {std::max(hi.x, v.x), std::max(hi.y, v.y)};
            }
            const double w = std::max(hi.x - lo.x, 1e-9);
            const double ht = std::max(hi.y - lo.y, 1e-9);
            code(s, 0, "WIPEOUT");
            emit_handle(s);
            emit_props_body(s, doc, h.props);
            code(s, 100, "AcDbWipeout");
            code_i(s, 90, 0);
            code_d(s, 10, lo.x);
            code_d(s, 20, lo.y);
            code_d(s, 30, 0.0);
            code_d(s, 11, w);
            code_d(s, 21, 0.0);
            code_d(s, 31, 0.0);
            code_d(s, 12, 0.0);
            code_d(s, 22, ht);
            code_d(s, 32, 0.0);
            code_d(s, 13, 1.0);
            code_d(s, 23, 1.0);
            code(s, 340, "0");
            code_i(s, 70, 7); // shown, shown when not aligned, clipped
            code_i(s, 280, 1);
            code_i(s, 281, 50);
            code_i(s, 282, 50);
            code_i(s, 283, 0);
            code(s, 360, "0");
            code_i(s, 71, 2);
            code_i(s, 91, static_cast<long>(loop.size() + 1));
            for (std::size_t i = 0; i <= loop.size(); ++i) {
                const Vec2& v = loop[i % loop.size()];
                code_d(s, 14, (v.x - lo.x) / w - 0.5);
                code_d(s, 24, 0.5 - (v.y - lo.y) / ht);
            }
        }
    }
    // A hatch, in model space or inside a block definition.
    // A model-space hatch is `model` (its index in the Document): an associative one's loops
    // name the objects they lie on, which carry it back as a reactor.
    constexpr std::size_t kInBlock = static_cast<std::size_t>(-1);
    const auto emit_hatch = [&](const DocHatch& h, std::size_t model) {
        if (h.pattern_name == "WIPEOUT") {
            return; // written as a WIPEOUT on its own
        }
        const bool gradient = hatch::is_gradient(h.pattern_name);
        const auto own = model != kInBlock ? refs.hatch_handle.find(static_cast<std::uint32_t>(model))
                                           : refs.hatch_handle.end();
        const bool associative = own != refs.hatch_handle.end();
        // Each loop's boundary objects, by their handles (written before the hatches).
        std::vector<std::vector<std::uint64_t>> sources(h.loops.size());
        if (associative) {
            for (const DocHatchRef& r : doc.hatch_refs) {
                if (r.hatch != model || r.loop >= sources.size()) {
                    continue;
                }
                const auto oh = refs.handle_of.find(key_of(static_cast<EntityKind>(r.kind), r.index));
                if (oh != refs.handle_of.end()) {
                    sources[r.loop].push_back(oh->second);
                }
            }
        }
        code(s, 0, "HATCH");
        if (associative) {
            code(s, 5, handle_hex(own->second));
            emit_props_body(s, doc, h.props);
        } else {
            emit_props(s, doc, h.props);
        }
        code(s, 100, "AcDbHatch");
        code_d(s, 10, 0.0); // elevation point + normal (planar at z=0)
        code_d(s, 20, 0.0);
        code_d(s, 30, 0.0);
        code_d(s, 210, 0.0);
        code_d(s, 220, 0.0);
        code_d(s, 230, 1.0);
        code(s, 2, gradient ? std::string("SOLID") : h.pattern_name);
        code_i(s, 70, (h.pattern_name == "SOLID" || gradient) ? 1 : 0); // solid-fill flag
        code_i(s, 71, associative ? 1 : 0);                // associative: its loops name their objects
        code_i(s, 91, static_cast<long>(h.loops.size()));  // boundary path count
        for (std::size_t i = 0; i < h.loops.size(); ++i) {
            const std::vector<Vec2>& loop = h.loops[i];
            code_i(s, 92, i == 0 ? 3 : 2); // 2 = polyline; +1 = external (outer loop)
            code_i(s, 72, 0);              // has-bulge = no
            code_i(s, 73, 1);              // is-closed = yes
            code_i(s, 93, static_cast<long>(loop.size()));
            for (const Vec2& v : loop) {
                code_d(s, 10, v.x);
                code_d(s, 20, v.y);
            }
            code_i(s, 97, static_cast<long>(sources[i].size())); // the loop's boundary objects
            for (const std::uint64_t o : sources[i]) {
                code(s, 330, handle_hex(o));
            }
        }
        code_i(s, 75, 0);  // hatch style = normal (odd parity)
        // User-defined (_USER, _USERDOUBLE here): AutoCAD's type 0 with its line families
        // written out, as it has no file to look them up in.
        const bool user = h.pattern_name == "_USER" || h.pattern_name == "_USERDOUBLE";
        // The drawing's own pattern: AutoCAD's custom type, its line families written out.
        const hatch::Pattern* custom = nullptr;
        for (const hatch::Pattern& p : doc.patterns) {
            custom = p.name == h.pattern_name ? &p : custom;
        }
        code_i(s, 76, user ? 0 : custom != nullptr ? 2 : 1); // user-defined / predefined / custom
        code_d(s, 52, to_degrees(h.pattern_angle));
        code_d(s, 41, h.pattern_scale);
        // A standard pattern's line families are written as it is drawn, as AutoCAD writes
        // them: acadiso.pat's size for a hatch made with MEASUREMENT 1.
        const hatch::Pattern* standard =
            custom == nullptr && !user && h.pattern_name != "SOLID" && !hatch::is_gradient(h.pattern_name)
                ? hatch::builtin_pattern(h.pattern_name)
                : nullptr;
        if (const hatch::Pattern* def = custom != nullptr ? custom : standard) {
            const double sc = (h.pattern_scale > 0.0 ? h.pattern_scale : 1.0) *
                              (standard != nullptr && h.iso ? hatch::kIsoPatternFactor : 1.0);
            const auto turn = [](Vec2 v, double a) {
                return Vec2{v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a)};
            };
            code_i(s, 78, static_cast<long>(def->lines.size()));
            for (const hatch::PatternLine& l : def->lines) {
                const double drawn = to_radians(l.angle) + h.pattern_angle;
                const Vec2 o = turn(l.origin * sc, h.pattern_angle);
                const Vec2 d = turn(l.delta * sc, drawn);
                code_d(s, 53, to_degrees(drawn));
                code_d(s, 43, o.x);
                code_d(s, 44, o.y);
                code_d(s, 45, d.x);
                code_d(s, 46, d.y);
                code_i(s, 79, static_cast<long>(l.dashes.size()));
                for (const double dash : l.dashes) {
                    code_d(s, 49, dash * sc);
                }
            }
        }
        if (user) {
            const bool twice = h.pattern_name == "_USERDOUBLE";
            code_i(s, 77, twice ? 1 : 0);
            code_i(s, 78, twice ? 2 : 1);
            for (int k = 0; k < (twice ? 2 : 1); ++k) {
                const double a = h.pattern_angle + (k == 0 ? 0.0 : kPi * 0.5);
                code_d(s, 53, to_degrees(a));
                code_d(s, 43, h.pattern_origin.x);
                code_d(s, 44, h.pattern_origin.y);
                code_d(s, 45, -std::sin(a) * h.pattern_scale);
                code_d(s, 46, std::cos(a) * h.pattern_scale);
                code_i(s, 79, 0);
            }
        }
        code_i(s, 98, 1);  // one seed point = the pattern origin
        code_d(s, 10, h.pattern_origin.x);
        code_d(s, 20, h.pattern_origin.y);
        if (gradient) {
            // AutoCAD's gradient block: two colour stops (the entity colour and the second
            // colour) along pattern_angle, linear.
            const Rgb c1 = h.props.color_by_layer()
                               ? (h.props.layer < doc.layers.size() ? doc.layers[h.props.layer].color
                                                                     : Rgb{255, 255, 255})
                               : h.props.color;
            code_i(s, 450, 1);
            code_i(s, 451, 0);
            code_d(s, 460, h.pattern_angle);
            code_d(s, 461, 0.0);
            code_i(s, 452, 0);
            code_d(s, 462, 1.0);
            code_i(s, 453, 2);
            code_d(s, 463, 0.0);
            code_i(s, 63, 5);
            code_i(s, 421, true_color(c1));
            code_d(s, 463, 1.0);
            code_i(s, 63, 2);
            code_i(s, 421, true_color(h.color2));
            code(s, 470, std::string(hatch::gradient_dxf_name(*hatch::gradient_shape(h.pattern_name))));
        }
    };
    for (const DocHatch& h : doc.hatches) {
        emit_hatch(h, static_cast<std::size_t>(&h - doc.hatches.data()));
    }
    // Model-space block references.
    const auto emit_insert = [&](const DocInsert& in) {
        code(s, 0, "INSERT");
        const std::uint64_t parent = emit_props(s, doc, in.props);
        code(s, 100, "AcDbBlockReference");
        // Attributes follow as ATTRIB entities (66 = 1) and end with SEQEND, each placed
        // where the definition's attdef lands under this insert's transform.
        const DocBlockDef* def = nullptr;
        for (const DocBlockDef& b : doc.block_defs) {
            if (b.name == in.block_name) {
                def = &b;
                break;
            }
        }
        const bool with_attribs = def != nullptr && !def->attdefs.empty() && !in.attribs.empty();
        if (with_attribs) {
            code_i(s, 66, 1);
        }
        code(s, 2, in.block_name);
        code_d(s, 10, in.pos.x);
        code_d(s, 20, in.pos.y);
        code_d(s, 30, 0.0);
        code_d(s, 41, in.scale_x);
        code_d(s, 42, in.scale_y);
        code_d(s, 43, 1.0);
        code_d(s, 50, to_degrees(in.rotation));
        if (!with_attribs) {
            return;
        }
        const double cs = std::cos(in.rotation);
        const double sn = std::sin(in.rotation);
        const std::uint64_t owner = g_owner;
        g_owner = parent; // its ATTRIB and SEQEND records are the INSERT's
        for (const DocAttDef& a : def->attdefs) {
            std::string value = a.def;
            for (const DocAttrib& at : in.attribs) {
                if (at.tag == a.text.content) {
                    value = at.value;
                    break;
                }
            }
            const Vec2 local{(a.text.pos.x - def->base.x) * in.scale_x,
                             (a.text.pos.y - def->base.y) * in.scale_y};
            const Vec2 world{in.pos.x + local.x * cs - local.y * sn,
                             in.pos.y + local.x * sn + local.y * cs};
            code(s, 0, "ATTRIB");
            emit_props(s, doc, a.text.props);
            code(s, 100, "AcDbText");
            code_d(s, 10, world.x);
            code_d(s, 20, world.y);
            code_d(s, 30, 0.0);
            code_d(s, 40, a.text.height * std::abs(in.scale_y));
            code(s, 1, value);
            code_d(s, 50, to_degrees(a.text.rotation + in.rotation));
            code_i(s, 72, a.text.justify);
            code(s, 100, "AcDbAttribute");
            code(s, 2, a.text.content);
            code_i(s, 70, a.flags);
        }
        code(s, 0, "SEQEND");
        emit_handle(s);
        code(s, 330, handle_hex(parent));
        code(s, 100, "AcDbEntity");
        code(s, 8, "0");
        g_owner = owner;
    };
    for (const DocInsert& in : doc.inserts) {
        emit_insert(in);
    }
    // A multileader with block content (#61): its blocks as references after the LEADER
    // written above, each item with its attribute values.
    for (const DocMLeader& m : doc.mleaders) {
        if (m.content_block.empty()) {
            continue;
        }
        const DocBlockDef* def = nullptr;
        for (const DocBlockDef& b : doc.block_defs) {
            def = same_table_name(b.name, m.content_block) ? &b : def;
        }
        if (def == nullptr) {
            continue;
        }
        // The block's size, for laying out collected items (its outline's extents).
        Vec2 lo{1e300, 1e300};
        Vec2 hi{-1e300, -1e300};
        const auto grow = [&](Vec2 p, double r) {
            lo = {std::min(lo.x, p.x - r), std::min(lo.y, p.y - r)};
            hi = {std::max(hi.x, p.x + r), std::max(hi.y, p.y + r)};
        };
        for (const DocLine& l : def->lines) {
            grow(l.a, 0.0);
            grow(l.b, 0.0);
        }
        for (const DocCircle& c : def->circles) {
            grow(c.center, c.radius);
        }
        for (const DocArc& a : def->arcs) {
            grow(a.center, a.radius);
        }
        for (const DocPolyline& p : def->polylines) {
            for (const Vec2& v : p.points) {
                grow(v, 0.0);
            }
        }
        const double scale = m.block.height > 0.0 ? m.block.height : 1.0;
        const Vec2 size = hi.x >= lo.x ? (hi - lo) * scale : Vec2{scale, scale};
        const double cs = std::cos(m.block.rotation);
        const double sn = std::sin(m.block.rotation);
        std::size_t start = 0;
        for (std::size_t k = 0;; ++k) {
            const std::size_t end = m.content.find('\x1E', start);
            const std::string item = m.content.substr(start, end == std::string::npos ? std::string::npos : end - start);
            const Vec2 off = collected_offset(k, m.collect, size);
            DocInsert in;
            in.block_name = def->name;
            in.pos = m.block.pos + Vec2{off.x * cs - off.y * sn, off.x * sn + off.y * cs};
            in.scale_x = scale;
            in.scale_y = scale;
            in.rotation = m.block.rotation;
            in.props = m.props;
            std::size_t vs = 0;
            for (const DocAttDef& a : def->attdefs) {
                const std::size_t ve = item.find('\x1F', vs);
                in.attribs.push_back({a.text.content, vs <= item.size() ? item.substr(vs, ve == std::string::npos ? std::string::npos : ve - vs) : std::string()});
                vs = ve == std::string::npos ? item.size() + 1 : ve + 1;
            }
            emit_insert(in);
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }
    // Raster images: AutoCAD's IMAGE entity -- insertion point, the U and V vectors
    // (one pixel each, in drawing units), the size in pixels, the IMAGEDEF it shows, and
    // the clip boundary in pixel coordinates (v down from the top, as the image is stored).
    for (const DocImage& im : doc.images) {
        if (im.def >= doc.image_defs.size()) {
            continue;
        }
        const DocImageDef& d = doc.image_defs[im.def];
        const double pw = d.pixel_w > 0 ? static_cast<double>(d.pixel_w) : 1.0;
        const double ph = d.pixel_h > 0 ? static_cast<double>(d.pixel_h) : 1.0;
        const double cs = std::cos(im.rotation);
        const double sn = std::sin(im.rotation);
        code(s, 0, "IMAGE");
        const std::uint64_t self = emit_handle(s);
        emit_props_body(s, doc, im.props);
        code(s, 100, "AcDbRasterImage");
        code_i(s, 90, 0);
        code_d(s, 10, im.pos.x);
        code_d(s, 20, im.pos.y);
        code_d(s, 30, 0.0);
        code_d(s, 11, im.width / pw * cs);
        code_d(s, 21, im.width / pw * sn);
        code_d(s, 31, 0.0);
        code_d(s, 12, -im.height / ph * sn);
        code_d(s, 22, im.height / ph * cs);
        code_d(s, 32, 0.0);
        code_d(s, 13, pw);
        code_d(s, 23, ph);
        code(s, 340, handle_hex(refs.def_handles[im.def]));
        code_i(s, 70, im.clipped ? 5 : 1); // 1 show, 4 use the clip boundary
        code_i(s, 280, im.clipped ? 1 : 0);
        code_i(s, 281, 50);
        code_i(s, 282, 50);
        code_i(s, 283, 0);
        const std::uint64_t reactor = g_handle++;
        code(s, 360, handle_hex(reactor));
        refs.reactors.emplace_back(self, reactor);
        if (!im.clip_polygon.empty()) {
            code_i(s, 71, 2);
            code_i(s, 91, static_cast<long>(im.clip_polygon.size() + 1));
            for (std::size_t i = 0; i <= im.clip_polygon.size(); ++i) {
                const Vec2& v = im.clip_polygon[i % im.clip_polygon.size()];
                code_d(s, 14, v.x * pw);
                code_d(s, 24, v.y * ph);
            }
        } else {
            code_i(s, 71, 1);
            code_i(s, 91, 2);
            code_d(s, 14, im.clip_u0 * pw);
            code_d(s, 24, im.clip_v0 * ph);
            code_d(s, 14, im.clip_u1 * pw);
            code_d(s, 24, im.clip_v1 * ph);
        }
    }
    // Paper-space viewports (AutoCAD's VIEWPORT entity): the frame in paper units and
    // the model view it shows (view centre, view height = frame height / scale).
    for (const DocViewport& v : doc.viewports) {
        code(s, 0, "VIEWPORT");
        EntityProps in_paper = v.props; // a viewport is in paper space
        in_paper.set_space(std::max<std::uint8_t>(v.props.space(), 1));
        emit_props(s, doc, in_paper);
        code(s, 100, "AcDbViewport");
        code_d(s, 10, v.center.x);
        code_d(s, 20, v.center.y);
        code_d(s, 30, 0.0);
        code_d(s, 40, v.width);
        code_d(s, 41, v.height);
        code_i(s, 68, v.on ? 1 : 0);
        code_i(s, 69, 2);
        code_d(s, 12, v.view_center.x);
        code_d(s, 22, v.view_center.y);
        code_d(s, 45, v.scale > 0.0 ? v.height / v.scale : v.height);
        for (const std::uint16_t li : v.frozen_layers) {
            if (li < refs.layer_handles.size()) {
                code(s, 331, handle_hex(refs.layer_handles[li])); // frozen in this viewport
            }
        }
    }
    code(s, 0, "ENDSEC");

    // BLOCKS section: each definition's geometry between BLOCK/ENDBLK. Written here,
    // after the entities (the emitters above are what it uses), and moved before
    // ENTITIES when the file is assembled.
    refs.blocks_begin = s.size();
    {
        code(s, 0, "SECTION");
        code(s, 2, "BLOCKS");
        // BLOCK and ENDBLK, owned by the block's record, as AutoCAD 2000 writes them.
        const auto block_begin = [&](std::uint64_t record, const std::string& name, bool paper) {
            code(s, 0, "BLOCK");
            emit_handle(s);
            code(s, 330, handle_hex(record));
            code(s, 100, "AcDbEntity");
            if (paper) {
                code_i(s, 67, 1);
            }
            code(s, 8, "0");
            code(s, 100, "AcDbBlockBegin");
            code(s, 2, name);
        };
        const auto block_end = [&](std::uint64_t record, bool paper) {
            code(s, 0, "ENDBLK");
            emit_handle(s);
            code(s, 330, handle_hex(record));
            code(s, 100, "AcDbEntity");
            if (paper) {
                code_i(s, 67, 1);
            }
            code(s, 8, "0");
            code(s, 100, "AcDbBlockEnd");
        };
        // The two spaces' blocks: empty, their entities are in ENTITIES.
        for (const auto& [record, name, paper] :
             {std::tuple{g_model_record, std::string("*Model_Space"), false},
              std::tuple{g_paper_record, std::string("*Paper_Space"), true}}) {
            block_begin(record, name, paper);
            code_i(s, 70, 0);
            code_d(s, 10, 0.0);
            code_d(s, 20, 0.0);
            code_d(s, 30, 0.0);
            code(s, 3, name);
            code(s, 1, "");
            block_end(record, paper);
        }
        for (std::size_t bi = 0; bi < doc.block_defs.size(); ++bi) {
            const DocBlockDef& b = doc.block_defs[bi];
            if (is_space_block(b.name)) {
                continue; // one from a file read before these were left out: the spaces are above
            }
            const std::uint64_t record = bi < g_block_records.size() ? g_block_records[bi] : g_model_record;
            block_begin(record, b.name, false);
            // 1 = anonymous (a name starting with *, as AutoCAD's *U and *D), 4 = an external reference.
            code_i(s, 70, (b.name.starts_with('*') ? 1 : 0) | (b.xref_path.empty() ? 0 : 4));
            if (!b.xref_path.empty()) {
                code(s, 1, b.xref_path);
            }
            code_d(s, 10, b.base.x);
            code_d(s, 20, b.base.y);
            code_d(s, 30, 0.0);
            code(s, 3, b.name);
            g_owner = record; // what follows is the block's
            for (const DocLine& l : b.lines) {
                code(s, 0, "LINE");
                emit_props(s, doc, l.props);
                code(s, 100, "AcDbLine");
                code_d(s, 10, l.a.x);
                code_d(s, 20, l.a.y);
                code_d(s, 11, l.b.x);
                code_d(s, 21, l.b.y);
            }
            for (const DocCircle& c : b.circles) {
                code(s, 0, "CIRCLE");
                emit_props(s, doc, c.props);
                code(s, 100, "AcDbCircle");
                code_d(s, 10, c.center.x);
                code_d(s, 20, c.center.y);
                code_d(s, 40, c.radius);
            }
            for (const DocArc& a : b.arcs) {
                code(s, 0, "ARC");
                emit_props(s, doc, a.props);
                code(s, 100, "AcDbCircle");
                code_d(s, 10, a.center.x);
                code_d(s, 20, a.center.y);
                code_d(s, 40, a.radius);
                code(s, 100, "AcDbArc");
                code_d(s, 50, to_degrees(a.start_angle));
                code_d(s, 51, to_degrees(a.end_angle));
            }
            for (const DocPolyline& p : b.polylines) {
                code(s, 0, "LWPOLYLINE");
                emit_props(s, doc, p.props);
                code(s, 100, "AcDbPolyline");
                code_i(s, 90, static_cast<long>(p.points.size()));
                code_i(s, 70, p.closed ? 1 : 0);
                emit_polyline_widths_header(s, p);
                const bool hb = p.bulges.size() == p.points.size();
                for (std::size_t i = 0; i < p.points.size(); ++i) {
                    code_d(s, 10, p.points[i].x);
                    code_d(s, 20, p.points[i].y);
                    emit_vertex_widths(s, p, i);
                    if (hb && p.bulges[i] != 0.0) {
                        code_d(s, 42, p.bulges[i]);
                    }
                }
            }
            for (const DocText& t : b.texts) {
                code(s, 0, "TEXT");
                emit_props(s, doc, t.props);
                code(s, 100, "AcDbText");
                const int vertical = emit_text_place(s, doc, t, t.content);
                code(s, 1, t.content);
                emit_text_end(s, vertical);
            }
            for (const DocMText& m : b.mtexts) {
                emit_mtext(m.block, m.content, m.props);
            }
            for (const DocHatch& h : b.hatches) {
                emit_hatch(h, kInBlock);
            }
            for (const DocAttDef& a : b.attdefs) {
                code(s, 0, "ATTDEF");
                emit_props(s, doc, a.text.props);
                code(s, 100, "AcDbText");
                const int vertical = emit_text_place(s, doc, a.text, a.text.content);
                code(s, 1, a.def);            // default value
                if (a.text.style != 0 && a.text.style < doc.text_styles.size()) {
                    code(s, 7, doc.text_styles[a.text.style].name);
                } else if (!a.text.font.empty()) {
                    code(s, 7, a.text.font);
                }
                code(s, 100, "AcDbAttributeDefinition");
                code(s, 3, a.prompt);         // prompt
                code(s, 2, a.text.content);   // tag
                code_i(s, 70, a.flags);       // 1 invisible, 2 constant, 4 verify, 8 preset
                if (vertical != 0) {
                    code_i(s, 74, vertical);
                }
            }
            for (const DocInsert& in : b.inserts) {
                emit_insert(in); // nested block reference
            }
            g_owner = 0;
            block_end(record, false);
        }
        code(s, 0, "ENDSEC");
    }
    refs.blocks_end = s.size();
    emit_objects(s, doc, refs);
    code(s, 0, "EOF");
}

void emit_objects(std::string& s, const Document& doc, const WriterRefs& refs) {
    const bool images = !(doc.images.empty() && doc.image_defs.empty());
    const bool mlines = !doc.mlines.empty() || !(doc.mline_styles.size() == 1 && doc.mline_styles[0] == MLineStyle{});
    const bool wipeouts = has_wipeouts(doc);
    // The named object dictionary, its ACAD_IMAGE_DICT, one IMAGEDEF per definition
    // and one IMAGEDEF_REACTOR per placed image -- the object web AutoCAD expects
    // around a raster -- and the ACAD_MLINESTYLE dictionary with the multiline styles.
    code(s, 0, "SECTION");
    code(s, 2, "OBJECTS");
    code(s, 0, "DICTIONARY");
    code(s, 5, handle_hex(kRootDictHandle));
    code(s, 330, "0");
    code(s, 100, "AcDbDictionary");
    code_i(s, 281, 1);
    code(s, 3, "ACAD_GROUP"); // groups are not written, but AutoCAD expects the dictionary
    code(s, 350, handle_hex(kGroupDictHandle));
    if (mlines) {
        code(s, 3, "ACAD_MLINESTYLE");
        code(s, 350, handle_hex(kMlineStyleDictHandle));
    }
    if (images) {
        code(s, 3, "ACAD_IMAGE_DICT");
        code(s, 350, handle_hex(kImageDictHandle));
    }
    if (wipeouts) {
        code(s, 3, "ACAD_WIPEOUT_VARS");
        code(s, 350, handle_hex(kWipeoutVarsHandle));
    }
    code(s, 0, "DICTIONARY");
    code(s, 5, handle_hex(kGroupDictHandle));
    code(s, 330, handle_hex(kRootDictHandle));
    code(s, 100, "AcDbDictionary");
    code_i(s, 281, 1);
    if (mlines) {
        code(s, 0, "DICTIONARY");
        code(s, 5, handle_hex(kMlineStyleDictHandle));
        code(s, 330, handle_hex(kRootDictHandle));
        code(s, 100, "AcDbDictionary");
        code_i(s, 281, 1);
        for (std::size_t i = 0; i < doc.mline_styles.size() && i < refs.mlstyle_handles.size(); ++i) {
            code(s, 3, doc.mline_styles[i].name);
            code(s, 350, handle_hex(refs.mlstyle_handles[i]));
        }
        const auto color = [&](std::uint8_t mode, Rgb c) {
            code_i(s, 62, mode == 1 ? 256 : mode == 2 ? 0 : 7);
            if (mode == 0) {
                code_i(s, 420, true_color(c));
            }
        };
        for (std::size_t i = 0; i < doc.mline_styles.size() && i < refs.mlstyle_handles.size(); ++i) {
            const MLineStyle& ms = doc.mline_styles[i];
            code(s, 0, "MLINESTYLE");
            code(s, 5, handle_hex(refs.mlstyle_handles[i]));
            code(s, 330, handle_hex(kMlineStyleDictHandle));
            code(s, 100, "AcDbMlineStyle");
            code(s, 2, ms.name);
            code_i(s, 70, ms.flags);
            code(s, 3, ms.description);
            color(ms.fill_mode, ms.fill_color);
            code_d(s, 51, ms.start_angle);
            code_d(s, 52, ms.end_angle);
            code_i(s, 71, static_cast<long>(ms.elements.size()));
            for (const MLineElement& e : ms.elements) {
                code_d(s, 49, e.offset);
                color(e.color_mode, e.color);
                code(s, 6, e.linetype_mode == 1 ? "BYLAYER" : e.linetype_mode == 2 ? "BYBLOCK" : linetype_name(e.linetype));
            }
        }
    }
    if (wipeouts) { // WIPEOUTFRAME: 0 frames hidden, 1 shown, 2 shown but not plotted
        code(s, 0, "WIPEOUTVARIABLES");
        code(s, 5, handle_hex(kWipeoutVarsHandle));
        code(s, 330, handle_hex(kRootDictHandle));
        code(s, 100, "AcDbWipeoutVariables");
        code_i(s, 70, !doc.wipeout_frames ? 0 : doc.wipeout_frames_plot ? 1 : 2);
    }
    // DIMASSOC (#59): the dimension's extension dictionary holds it as ACAD_DIMASSOC. It names
    // the dimension, which of its points are on objects (90, a bit each), and for each such
    // point the object (331), the object snap that finds it (72), a polyline's segment (91)
    // and where on the object it is (40), as AutoCAD writes them.
    for (const WriterRefs::DimAssoc& a : refs.dim_assocs) {
        std::vector<std::pair<const DocDimRef*, std::uint64_t>> on;
        for (const DocDimRef* r : a.refs) {
            const auto h = refs.handle_of.find((static_cast<std::uint64_t>(r->kind) << 32) | r->index);
            if (h != refs.handle_of.end() && r->point < 4) {
                on.emplace_back(r, h->second);
            }
        }
        std::sort(on.begin(), on.end(), [](const auto& x, const auto& y) { return x.first->point < y.first->point; });
        on.erase(std::unique(on.begin(), on.end(),
                             [](const auto& x, const auto& y) { return x.first->point == y.first->point; }),
                 on.end());
        long flags = 0;
        for (const auto& [r, h] : on) {
            flags |= 1L << r->point;
        }
        code(s, 0, "DICTIONARY");
        code(s, 5, handle_hex(a.dict));
        code(s, 330, handle_hex(a.dim_handle));
        code(s, 100, "AcDbDictionary");
        code_i(s, 280, 1);
        code_i(s, 281, 1);
        code(s, 3, "ACAD_DIMASSOC");
        code(s, 360, handle_hex(a.assoc));
        code(s, 0, "DIMASSOC");
        code(s, 5, handle_hex(a.assoc));
        code(s, 102, "{ACAD_REACTORS");
        code(s, 330, handle_hex(a.dict));
        code(s, 102, "}");
        code(s, 330, handle_hex(a.dict));
        code(s, 100, "AcDbDimAssoc");
        code(s, 330, handle_hex(a.dim_handle));
        code_i(s, 90, flags);
        code_i(s, 70, 0); // the dimension and its objects in the same space
        code_i(s, 71, 0);
        for (const auto& [r, h] : on) {
            code(s, 1, "AcDbOsnapPointRef");
            code_i(s, 72, r->osnap);
            code(s, 331, handle_hex(h));
            code_i(s, 73, static_cast<EntityKind>(r->kind) == EntityKind::Point ? 0 : 2); // an edge
            code_i(s, 91, static_cast<long>(r->segment));
            code_d(s, 40, r->param);
            code_d(s, 10, 0.0); // no last point: the object snap finds it
            code_d(s, 20, 0.0);
            code_d(s, 30, 2.0e50);
            code_i(s, 75, 0);
        }
    }
    if (!images) {
        code(s, 0, "ENDSEC");
        return;
    }
    code(s, 0, "DICTIONARY");
    code(s, 5, handle_hex(kImageDictHandle));
    code(s, 330, handle_hex(kRootDictHandle));
    code(s, 100, "AcDbDictionary");
    code_i(s, 281, 1);
    for (std::size_t i = 0; i < doc.image_defs.size() && i < refs.def_handles.size(); ++i) {
        const DocImageDef& d = doc.image_defs[i];
        const std::string name = d.source.empty() ? "embedded-image-" + std::to_string(i + 1)
                                                  : d.source;
        code(s, 3, name);
        code(s, 350, handle_hex(refs.def_handles[i]));
    }
    for (std::size_t i = 0; i < doc.image_defs.size() && i < refs.def_handles.size(); ++i) {
        const DocImageDef& d = doc.image_defs[i];
        code(s, 0, "IMAGEDEF");
        code(s, 5, handle_hex(refs.def_handles[i]));
        code(s, 330, handle_hex(kImageDictHandle));
        code(s, 100, "AcDbRasterImageDef");
        code_i(s, 90, 0);
        code(s, 1, d.source.empty() ? "embedded-image-" + std::to_string(i + 1) + ".png" : d.source);
        code_d(s, 10, d.pixel_w > 0 ? static_cast<double>(d.pixel_w) : 1.0);
        code_d(s, 20, d.pixel_h > 0 ? static_cast<double>(d.pixel_h) : 1.0);
        code_d(s, 11, 1.0); // default pixel size (drawing units): the entity's U/V rule
        code_d(s, 21, 1.0);
        code_i(s, 280, 1); // loaded
        code_i(s, 281, 0); // resolution units: none
    }
    for (const auto& [image, reactor] : refs.reactors) {
        code(s, 0, "IMAGEDEF_REACTOR");
        code(s, 5, handle_hex(reactor));
        code(s, 330, handle_hex(image));
        code(s, 100, "AcDbRasterImageDefReactor");
        code_i(s, 90, 2);
        code(s, 330, handle_hex(image));
    }
    code(s, 0, "ENDSEC");
}

std::string serialize_las(const LayerState& state) {
    std::string s;
    const auto put = [&](int code, const std::string& value) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%3d", code);
        s += buf;
        s += "\r\n";
        s += value;
        s += "\r\n";
    };
    put(0, "LAYERSTATEDICTIONARY");
    put(0, "LAYERSTATE");
    put(1, state.name);
    put(91, "2047"); // every property restored
    put(301, "");
    put(290, "0");
    put(302, state.current);
    for (const Layer& l : state.layers) {
        put(8, l.name);
        put(90, std::to_string((l.on ? 0 : 1) | (l.frozen ? 2 : 0) | (l.locked ? 4 : 0) | (l.plot ? 8 : 0) |
                               (l.vp_freeze_new ? 16 : 0)));
        // The nearest colour index; a colour that is not one also goes as a true colour.
        long aci = 7;
        long best = std::numeric_limits<long>::max();
        for (long i = 1; i <= 255; ++i) {
            const Rgb p = aci_to_rgb(i);
            const long dr = static_cast<long>(p.r) - l.color.r;
            const long dg = static_cast<long>(p.g) - l.color.g;
            const long db = static_cast<long>(p.b) - l.color.b;
            const long d2 = dr * dr + dg * dg + db * db;
            if (d2 < best) {
                best = d2;
                aci = i;
            }
        }
        put(62, std::to_string(aci));
        put(370, std::to_string(static_cast<int>(l.lineweight)));
        put(6, linetype_name(l.linetype));
        put(2, "Color_" + std::to_string(aci));
        const long alpha = std::lround(255.0 * (100.0 - std::min<int>(l.transparency, 90)) / 100.0);
        put(440, std::to_string(l.transparency == 0 ? 0L : (0x02000000L | alpha)));
        if (best != 0) {
            put(92, std::to_string(static_cast<std::int32_t>(0xC2000000u | static_cast<std::uint32_t>(true_color(l.color)))));
        }
    }
    return s;
}

bool parse_las(std::string_view text, LayerState& out, std::string& error) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3); // a UTF-8 byte order mark
    }
    std::vector<std::string_view> lines;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
    }
    const auto trim = [](std::string_view v) {
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
            v.remove_prefix(1);
        }
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) {
            v.remove_suffix(1);
        }
        return v;
    };
    const auto number = [&](std::string_view v, long& n) {
        v = trim(v);
        const auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
        return ec == std::errc{} && ptr == v.data() + v.size();
    };
    out = LayerState{};
    bool dict = false;
    bool state = false;
    for (std::size_t i = 0; i + 1 < lines.size(); i += 2) {
        long code = 0;
        if (!number(lines[i], code)) {
            error = "line " + std::to_string(i + 1) + " is not a group code";
            return false;
        }
        const std::string_view value = lines[i + 1];
        const std::string_view word = trim(value);
        long n = 0;
        if (code == 0) {
            if (word == "LAYERSTATEDICTIONARY" || word == "LAYERSTATEDICT") {
                dict = true;
                continue;
            }
            if (word == "LAYERSTATE" && dict && !state) {
                state = true;
                continue;
            }
            break; // a second state: one per file
        }
        if (!state) {
            error = "not a layer state file";
            return false;
        }
        Layer* l = out.layers.empty() ? nullptr : &out.layers.back();
        switch (code) {
        case 1:
            if (l == nullptr) {
                out.name = std::string(value);
            }
            break;
        case 302:
            out.current = std::string(value);
            break;
        case 8:
            out.layers.push_back(Layer{});
            out.layers.back().name = std::string(value);
            break;
        case 90:
            if (l != nullptr && number(value, n)) {
                l->on = (n & 1) == 0;
                l->frozen = (n & 2) != 0;
                l->locked = (n & 4) != 0;
                l->plot = (n & 8) != 0;
                l->vp_freeze_new = (n & 16) != 0;
            }
            break;
        case 62:
            if (l != nullptr && number(value, n) && n != 0) {
                l->color = aci_to_rgb(std::labs(n) > 255 ? 7 : std::labs(n));
                l->on = l->on && n > 0; // a negative colour is a layer that is off
            }
            break;
        case 92:
            if (l != nullptr && number(value, n)) {
                l->color = from_true_color(n & 0xFFFFFF);
            }
            break;
        case 370:
            if (l != nullptr && number(value, n)) {
                l->lineweight = static_cast<std::uint8_t>(n < 0 ? 25 : std::min<long>(n, 211));
            }
            break;
        case 6:
            if (l != nullptr) {
                l->linetype = linetype_from(std::string(word));
            }
            break;
        case 440:
            if (l != nullptr && number(value, n)) {
                const long alpha = n & 0xFF;
                l->transparency = static_cast<std::uint8_t>(
                    n == 0 ? 0 : std::clamp<long>(std::lround((255.0 - static_cast<double>(alpha)) * 100.0 / 255.0), 0, 90));
            }
            break;
        default:
            break; // 91, 301, 290, 2: kept by AutoCAD, not by Musa CAD
        }
    }
    if (!state || out.name.empty()) {
        error = "not a layer state file";
        return false;
    }
    return true;
}

std::string serialize_dxf(const Document& doc) {
    const LinetypeScope ltypes_scope(&doc.linetypes);
    g_handle = 0x20;
    WriterRefs refs;
    std::string body;
    serialize_body(body, doc, refs);
    std::string out;
    emit_header(out, doc, g_handle + 1);
    emit_classes(out, doc);
    // Tables, then BLOCKS, then ENTITIES, then OBJECTS + EOF.
    out += body.substr(0, refs.entities_begin);
    out += body.substr(refs.blocks_begin, refs.blocks_end - refs.blocks_begin);
    out += body.substr(refs.entities_begin, refs.blocks_begin - refs.entities_begin);
    out += body.substr(refs.blocks_end);
    return out;
}

namespace {

struct Pair {
    int code = 0;
    std::string value;
};

bool parse_int(const std::string& t, int& out) {
    std::size_t a = 0;
    std::size_t b = t.size();
    while (a < b && std::isspace(static_cast<unsigned char>(t[a]))) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(t[b - 1]))) {
        --b;
    }
    const auto [ptr, ec] = std::from_chars(t.data() + a, t.data() + b, out);
    return ec == std::errc{} && ptr == t.data() + b;
}
double to_d(const std::string& t) {
    double v = 0.0;
    std::size_t a = 0;
    while (a < t.size() && std::isspace(static_cast<unsigned char>(t[a]))) {
        ++a;
    }
    parse_double(t.data() + a, t.data() + t.size(), v);
    return v;
}
long to_l(const std::string& t) {
    long v = 0;
    std::size_t a = 0;
    while (a < t.size() && std::isspace(static_cast<unsigned char>(t[a]))) {
        ++a;
    }
    std::from_chars(t.data() + a, t.data() + t.size(), v);
    return v;
}
const std::string* find(const std::vector<Pair>& body, int c) {
    for (const Pair& p : body) {
        if (p.code == c) {
            return &p.value;
        }
    }
    return nullptr;
}
double getd(const std::vector<Pair>& body, int c, double def = 0.0) {
    const std::string* v = find(body, c);
    return v != nullptr ? to_d(*v) : def;
}

// Convert MTEXT inline-formatted content to plain text. MTEXT (AutoCAD/LibreCAD/ODA)
// wraps text in backslash control runs -- \fCambria|b0|i0|c0|p18; (font), \C1; (colour),
// \H2x; (height), \A1; (alignment), {...} groups, \P (hard return), etc. Musa's text model
// is plain, so we honour structure (\P -> newline, \~ -> space, escaped literals) and DROP
// the styling runs rather than render them verbatim as "\fCambria...". Losing font/bold/
// colour is an intentional, catalogued fidelity gap -- not garbled output.
// An MTEXT's content as Musa CAD keeps it: its format codes as AutoCAD wrote them (they
// are drawn: heights, colours, fonts, underlines, stacked fractions), its paragraphs \P as
// line breaks, a ^I tab as a space and ^J as a line break.
std::string mtext_from_dxf(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '\\' && i + 1 < in.size()) {
            if (in[i + 1] == 'P') {
                out += '\n';
            } else {
                out += c;
                out += in[i + 1]; // a code, or an escaped \\ \{ \}: kept as it is
            }
            ++i;
            continue;
        }
        if (c == '^' && i + 1 < in.size() && (in[i + 1] == 'I' || in[i + 1] == 'J' || in[i + 1] == 'M')) {
            out += in[i + 1] == 'I' ? std::string(" ") : in[i + 1] == 'J' ? std::string("\n") : std::string();
            ++i;
            continue;
        }
        out += c;
    }
    // Codes the plain layout would show as text (\~, \\, braces, a lone toggle) when no
    // other code makes the text formatted: read, as they were before.
    return text::has_mtext_codes(out) ? out : text::mtext_plain(out, true);
}

/// An angular dimension as the model keeps it: `a` the vertex, the angle the
/// counter-clockwise sweep from `b` to `line_pt`, `aux` the arc's radius. A DXF names the
/// two rays and a point on the arc, and that point says which way round they go.
void set_angular_dim(DocDim& d, Vec2 vertex, Vec2 ray1, Vec2 ray2, Vec2 on_arc) {
    d.a = vertex;
    const bool through = ccw_sweep(ray1 - vertex, on_arc - vertex) <= ccw_sweep(ray1 - vertex, ray2 - vertex);
    d.b = through ? ray1 : ray2;
    d.line_pt = through ? ray2 : ray1;
    d.aux = length(on_arc - vertex);
}

/// A two-line angular dimension (DXF type 2): the lines 13-14 and 15-10, the arc through 16.
/// The vertex is where the lines cross, and the rays are the sides of them the arc lies
/// between. False when the lines are parallel: they have no vertex.
bool set_two_line_angular_dim(DocDim& d, Vec2 p13, Vec2 p14, Vec2 p15, Vec2 p10, Vec2 on_arc) {
    const Vec2 u1 = normalized(p14 - p13);
    const Vec2 u2 = normalized(p10 - p15);
    const double denom = cross(u1, u2);
    if (std::abs(denom) < 1e-9) {
        return false;
    }
    const Vec2 vertex = p13 + u1 * (cross(p15 - p13, u2) / denom);
    const Vec2 w = on_arc - vertex; // alpha u1 + beta u2: the signs pick the sides
    const Vec2 ray1 = cross(w, u2) / denom >= 0.0 ? u1 : u1 * -1.0;
    const Vec2 ray2 = cross(u1, w) / denom >= 0.0 ? u2 : u2 * -1.0;
    // Each ray's point: its line's far end on that side, or on the arc if the line stops short.
    const auto far_end = [&](Vec2 ray, Vec2 p, Vec2 q) {
        const double reach = std::max(dot(p - vertex, ray), dot(q - vertex, ray));
        return vertex + ray * (reach > 1e-9 ? reach : length(w));
    };
    set_angular_dim(d, vertex, far_end(ray1, p13, p14), far_end(ray2, p15, p10), on_arc);
    return true;
}

/// The dimension variables an entity overrides, from its `ACAD` xdata: the DSTYLE list of
/// `1070 <variable's group code>` each followed by its value.
std::vector<std::pair<long, std::string>> dstyle_overrides(const std::vector<Pair>& body) {
    std::vector<std::pair<long, std::string>> out;
    for (std::size_t k = 0; k + 1 < body.size(); ++k) {
        if (body[k].code != 1000 || body[k].value != "DSTYLE") {
            continue;
        }
        for (std::size_t j = k + 1; j + 1 < body.size(); ++j) {
            if (body[j].code == 1002 && body[j].value == "}") {
                break;
            }
            if (body[j].code == 1070) {
                out.emplace_back(to_l(body[j].value), body[j + 1].value);
                ++j;
            }
        }
        break;
    }
    return out;
}

} // namespace

IoResult parse_dxf(const std::string& text, Document& out) {
    std::istringstream in(text);
    std::vector<Pair> pairs;
    std::string code_line;
    std::string value_line;
    while (std::getline(in, code_line)) {
        if (!std::getline(in, value_line)) {
            return IoResult::failure("Malformed DXF: dangling group code (truncated file).");
        }
        if (!code_line.empty() && code_line.back() == '\r') {
            code_line.pop_back();
        }
        if (!value_line.empty() && value_line.back() == '\r') {
            value_line.pop_back();
        }
        int gc = 0;
        if (!parse_int(code_line, gc)) {
            return IoResult::failure("Malformed DXF: expected a numeric group code.");
        }
        pairs.push_back(Pair{gc, value_line});
    }
    if (pairs.empty()) {
        return IoResult::failure("Empty or invalid DXF file.");
    }

    Document doc;
    doc.layers.assign(1, Layer{"0"}); // layer 0 always exists
    // The drawing's Dashed, Center and Hidden start as AutoCAD loads them for its MEASUREMENT
    // (acad.lin or acadiso.lin); its own LTYPE entries of those names take their place.
    for (std::size_t k = 0; k + 1 < pairs.size(); ++k) {
        if (pairs[k].code == 9 && pairs[k].value == "$MEASUREMENT" && pairs[k + 1].code == 70) {
            doc.display_units.measurement = to_l(pairs[k + 1].value) != 0 ? 1 : 0;
            break;
        }
        if (pairs[k].code == 0 && pairs[k].value == "ENDSEC") {
            break; // the header is the first section
        }
    }
    doc.builtin_linetypes = standard_builtin_linetypes(doc.display_units.measurement == 1);
    // The drawing's linetypes (its LTYPE table): those beyond the built-in four, and one by a
    // built-in name that the drawing draws otherwise, come with it; entities and layers then
    // take theirs by name.
    for (std::size_t k = 0; k < pairs.size(); ++k) {
        if (pairs[k].code != 0 || pairs[k].value != "LTYPE") {
            continue;
        }
        LinetypeDef d;
        std::size_t j = k + 1;
        for (; j < pairs.size() && pairs[j].code != 0; ++j) {
            if (pairs[j].code == 2) {
                d.name = pairs[j].value;
            } else if (pairs[j].code == 3) {
                d.description = pairs[j].value;
            } else if (pairs[j].code == 49) {
                d.pattern.push_back(to_d(pairs[j].value));
            }
        }
        k = j - 1;
        std::string u = d.name;
        for (char& ch : u) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        // DASHED, CENTER and HIDDEN are the drawing's own definitions of the built-in three.
        const int at = u == "DASHED" ? 0 : u == "CENTER" ? 1 : u == "HIDDEN" ? 2 : -1;
        if (at >= 0 && !d.pattern.empty()) {
            doc.builtin_linetypes[static_cast<std::size_t>(at)] = d.pattern;
            continue;
        }
        if (u.empty() || u == "CONTINUOUS" || u == "BYLAYER" || u == "BYBLOCK" || d.pattern.empty() ||
            doc.linetypes.size() >= kMaxLoadedLinetypes) {
            continue;
        }
        if (std::none_of(doc.linetypes.begin(), doc.linetypes.end(), [&](const LinetypeDef& o) { return o.name == d.name; })) {
            doc.linetypes.push_back(std::move(d));
        }
    }
    const LinetypeScope ltypes_scope(&doc.linetypes);
    std::map<std::string, std::uint16_t> layer_index{{"0", std::uint16_t{0}}};
    const auto ensure_layer = [&](const std::string& name) -> std::uint16_t {
        const auto it = layer_index.find(name);
        if (it != layer_index.end()) {
            return it->second;
        }
        const auto idx = static_cast<std::uint16_t>(doc.layers.size());
        Layer l;
        l.name = name;
        doc.layers.push_back(l);
        layer_index[name] = idx;
        return idx;
    };
    std::map<std::string, std::uint16_t> style_index{{std::string("Standard"), std::uint16_t{0}}};
    const auto ensure_dimstyle = [&](const std::string& name) -> std::uint16_t {
        const auto it = style_index.find(name);
        if (it != style_index.end()) {
            return it->second;
        }
        const auto idx = static_cast<std::uint16_t>(doc.dimstyles.size());
        DimStyle ds;
        ds.name = name;
        doc.dimstyles.push_back(ds);
        style_index[name] = idx;
        return idx;
    };

    std::map<std::uint16_t, double> dimstyle_scale; // each style's DIMSCALE
    std::map<std::string, int> skipped;
    bool saw_section = false;
    std::string section;
    std::size_t i = 0;
    const std::size_t n = pairs.size();

    // STYLE table: text-style name (code 2) -> font file (code 3, e.g. "arial.ttf" or
    // "txt.shx"). TEXT/MTEXT reference a style via code 7; we resolve it to the font name
    // and the font engine substitutes SHX names to TTF lookalikes at render time.
    std::map<std::string, std::string> styles;
    std::map<std::string, std::uint16_t> text_style_index; // name -> doc.text_styles index
    const auto add_style_record = [&](const std::vector<Pair>& body) {
        const std::string* name = find(body, 2);
        const std::string* font = find(body, 3);
        if (name != nullptr && font != nullptr && !font->empty()) {
            styles[*name] = *font;
        }
        // The STYLE table itself (v26): name, font, fixed height, width factor, oblique.
        if (name != nullptr && !name->empty() && *name != "Standard" && *name != "STANDARD" &&
            text_style_index.find(*name) == text_style_index.end()) {
            if (doc.text_styles.empty()) {
                doc.text_styles.push_back(TextStyle{}); // [0] Standard
            }
            TextStyle ts;
            ts.name = *name;
            ts.font = font != nullptr ? *font : std::string{};
            if (ts.font == "txt" || ts.font == "txt.shx") {
                ts.font.clear(); // the stroke font stands in for AutoCAD's txt.shx
            }
            ts.height = getd(body, 40, 0.0);
            ts.width_factor = getd(body, 41, 1.0);
            ts.oblique = to_radians(getd(body, 50, 0.0));
            if (const std::string* g = find(body, 71)) {
                ts.backwards = (to_l(*g) & 2) != 0;
                ts.upside_down = (to_l(*g) & 4) != 0;
            }
            text_style_index[*name] = static_cast<std::uint16_t>(doc.text_styles.size());
            doc.text_styles.push_back(std::move(ts));
        }
    };
    // The font name a text entity references (via its STYLE, code 7). Falls back to the
    // code-7 value itself when the style is unknown (Musa export names styles after fonts).
    const auto font_of_entity = [&](const std::vector<Pair>& body) -> std::string {
        const std::string* style = find(body, 7);
        if (style == nullptr || style->empty() || *style == "Standard" || *style == "STANDARD") {
            return {};
        }
        if (const auto it = styles.find(*style); it != styles.end()) {
            return it->second;
        }
        return *style; // unknown style: treat its name as the font reference
    };

    // LAYER handle -> index, for VIEWPORT 331 (layers frozen in that viewport).
    std::map<std::string, std::uint16_t> layer_by_handle;
    // DIMASSOC (#59): in a file that has $DIMASSOC, as AutoCAD writes it, a dimension is
    // associative when a DIMASSOC object names it, and not otherwise. A file without (one an
    // older Musa CAD wrote) leaves its dimensions following what their points are on.
    std::map<std::string, std::size_t> dim_by_handle; // the drawing's dimensions by handle
    std::set<std::string> associated;                 // those a DIMASSOC object names
    bool dimassoc_var = false;
    std::map<std::string, std::string> block_record_names; // BLOCK_RECORD names, by handle (#61)
    const auto upper_hex = [](std::string h) {
        for (char& ch : h) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        return h;
    };
    const auto add_layer_record = [&](const std::vector<Pair>& body) {
        const std::string* name = find(body, 2);
        if (name == nullptr) {
            return;
        }
        const std::uint16_t idx = ensure_layer(*name);
        Layer& l = doc.layers[idx];
        if (const std::string* h = find(body, 5)) {
            layer_by_handle[upper_hex(*h)] = idx;
        }
        if (const std::string* flag = find(body, 70)) {
            const long f = to_l(*flag);
            l.frozen = (f & 1) != 0;
            l.vp_freeze_new = (f & 2) != 0;
            l.locked = (f & 4) != 0;
        }
        if (const std::string* col = find(body, 62)) {
            const long aci = to_l(*col);
            l.on = aci >= 0;                  // negative ACI = layer off
            const long mag = aci < 0 ? -aci : aci; // |ACI| is the colour even when off
            if (mag >= 1 && mag <= 255) {
                l.color = aci_to_rgb(mag);
            }
        }
        if (const std::string* tc = find(body, 420)) {
            l.color = from_true_color(to_l(*tc)); // true colour wins over the ACI
        }
        if (const std::string* lt = find(body, 6)) {
            l.linetype = linetype_from(*lt);
        }
        if (const std::string* pl = find(body, 290)) {
            l.plot = to_l(*pl) != 0;
        }
        for (std::size_t k = 0; k < body.size(); ++k) {
            // AcAecLayerStandard XDATA: an empty string, then the description.
            if (body[k].code == 1001 && body[k].value == "AcCmTransparency") {
                for (std::size_t j = k + 1; j < body.size() && body[j].code != 1001; ++j) {
                    if (body[j].code == 1071) {
                        const long v = to_l(body[j].value);
                        if ((v & 0x02000000L) != 0) { // by alpha
                            const double alpha = static_cast<double>(v & 0xFF);
                            l.transparency = static_cast<std::uint8_t>(
                                std::clamp(std::lround(100.0 - alpha * 100.0 / 255.0), 0L, 90L));
                        }
                        break;
                    }
                }
            }
            if (body[k].code == 1001 && body[k].value == "AcAecLayerStandard") {
                int strings = 0;
                for (std::size_t j = k + 1; j < body.size() && body[j].code != 1001; ++j) {
                    if (body[j].code == 1000 && ++strings == 2) {
                        l.description = body[j].value;
                        break;
                    }
                }
            }
        }
        if (const std::string* lw = find(body, 370)) {
            // Only 0..211 are real widths; -1/-2/-3 (ByLayer/ByBlock/Default) and any
            // out-of-range value mean "inherit", not a literal 2.5mm line. A layer must
            // hold a concrete width, so keep its default when the code isn't a real one.
            if (const long w = to_l(*lw); w >= 0 && w <= 211) {
                l.lineweight = static_cast<std::uint8_t>(w);
            }
        }
    };

    std::uint32_t dxf_order = EntityProps::kFirstOrder;
    const auto props_of = [&](const std::vector<Pair>& body) {
        EntityProps p;
        const std::string* layer = find(body, 8);
        p.layer = ensure_layer(layer != nullptr ? *layer : std::string("0"));
        if (const std::string* sp = find(body, 67); sp != nullptr && to_l(*sp) == 1) {
            p.set_space(1); // paper space -> the first layout
        }
        if (const std::string* tc = find(body, 420)) {
            p.set_color_by_layer(false);
            p.color = from_true_color(to_l(*tc));
        } else if (const std::string* aci = find(body, 62)) {
            // 256=ByLayer, 0=ByBlock (the reference's); 1..255 is a concrete colour override.
            if (const long v = to_l(*aci); v >= 1 && v <= 255) {
                p.set_color_by_layer(false);
                p.color = aci_to_rgb(v);
            } else if (v == 0) {
                p.set_color_by_block(true);
            }
        }
        if (const std::string* lt = find(body, 6)) {
            std::string u = *lt;
            for (char& ch : u) {
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            if (u == "BYBLOCK") {
                p.set_linetype_by_block(true);
            } else if (u != "BYLAYER") { // that is the layer's
                p.set_linetype_by_layer(false);
                p.linetype = linetype_from(*lt);
            }
        }
        if (const std::string* lw = find(body, 370)) {
            // 0..211 = an explicit width; -2 ByBlock; -1/-3 (ByLayer/Default) or anything
            // out of range means "inherit", so leave the ByLayer default rather than
            // casting -3 to 253 (a 2.53mm line) and fattening the whole import.
            if (const long w = to_l(*lw); w >= 0 && w <= 211) {
                p.set_lineweight_by_layer(false);
                p.lineweight = static_cast<std::uint8_t>(w);
            } else if (w == -2) {
                p.set_lineweight_by_block(true);
            }
        }
        if (const std::string* tr = find(body, 440)) { // 0x02000000 | opacity, 0x01000000 ByBlock
            const long v = to_l(*tr);
            if ((v & 0x01000000L) != 0 && (v & 0x02000000L) == 0) {
                p.transparency = EntityProps::kTransparencyByBlock;
            } else if ((v & 0x02000000L) != 0) {
                const double opacity = static_cast<double>(v & 0xFFL) / 255.0;
                p.transparency = static_cast<std::uint8_t>(std::clamp(std::lround(100.0 * (1.0 - opacity)), 0L, 90L));
            }
        }
        // The file's order is the draw order (the first drawn first), as AutoCAD draws a
        // drawing without a sort table.
        dxf_order += EntityProps::kOrderStep;
        p.order = dxf_order;
        return p;
    };

    const auto add_dimstyle_record = [&](const std::vector<Pair>& body) {
        const std::string* name = find(body, 2);
        if (name == nullptr) {
            return;
        }
        const std::uint16_t style_idx = ensure_dimstyle(*name);
        DimStyle& ds = doc.dimstyles[style_idx];
        // DIMSCALE multiplies every size in the style. 0 is "scale to the layout", which
        // reads as 1 in model space.
        const double dimscale = getd(body, 40, 1.0) > 0.0 ? getd(body, 40, 1.0) : 1.0;
        dimstyle_scale[style_idx] = dimscale;
        if (const std::string* v = find(body, 140)) {
            ds.text_height = to_d(*v) * dimscale;
        }
        if (const std::string* v = find(body, 41)) {
            ds.arrow_size = to_d(*v) * dimscale;
        }
        if (const std::string* v = find(body, 271)) {
            ds.precision = static_cast<std::uint8_t>(to_l(*v));
        }
        if (const std::string* v = find(body, 42)) {
            ds.ext_offset = to_d(*v) * dimscale;
        }
        if (const std::string* v = find(body, 44)) {
            ds.ext_extension = to_d(*v) * dimscale;
        }
        if (const std::string* v = find(body, 280)) {
            const long just = to_l(*v);
            ds.text_just = just >= 0 && just <= 4 ? static_cast<std::uint8_t>(just) : std::uint8_t{0};
        }
        if (const std::string* v = find(body, 294)) {
            ds.text_rtl = to_l(*v) != 0; // DIMTXTDIRECTION
        }
        if (const std::string* v = find(body, 77)) {
            const long tad = to_l(*v);
            ds.text_above = tad != 0 && tad != 4;
            ds.text_vpos = tad >= 2 && tad <= 4 ? static_cast<std::uint8_t>(tad) : std::uint8_t{0};
        }
        if (const std::string* v = find(body, 78)) {
            ds.zero_suppression = static_cast<std::uint8_t>(to_l(*v) & 0x0F); // the feet/inch bits too
        }
        if (const std::string* v = find(body, 278)) {
            ds.decimal_separator = to_l(*v) == ',' ? ',' : '.';
        }
        if (const std::string* v = find(body, 279)) {
            const long m = to_l(*v);
            ds.text_move = static_cast<std::uint8_t>(m >= 0 && m <= 2 ? m : 0);
        }
        if (const std::string* v = find(body, 371)) {
            if (const long w = to_l(*v); w >= 0 && w <= 211) {
                ds.dim_lineweight = static_cast<std::uint8_t>(w);
            }
        }
        // The units: each read only within its range, the default kept otherwise.
        const auto ranged = [&](int code, long lo, long hi, auto set) {
            if (const std::string* v = find(body, code)) {
                if (const long x = to_l(*v); x >= lo && x <= hi) {
                    set(x);
                }
            }
        };
        ranged(277, 1, 6, [&](long x) { ds.linear_unit = static_cast<std::uint8_t>(x); });
        ranged(276, 0, 2, [&](long x) { ds.fraction_format = static_cast<std::uint8_t>(x); });
        ranged(275, 0, 3, [&](long x) { ds.angular_unit = static_cast<std::uint8_t>(x); });
        ranged(179, -1, 8, [&](long x) { ds.angular_precision = static_cast<std::int8_t>(x); });
        ranged(170, 0, 1, [&](long x) { ds.alt = x != 0; });
        ranged(273, 1, 6, [&](long x) { ds.alt_unit = static_cast<std::uint8_t>(x); });
        ranged(171, 0, 8, [&](long x) { ds.alt_precision = static_cast<std::uint8_t>(x); });
        // The Tolerances tab.
        const double tp = getd(body, 47);
        const double tm = getd(body, 48);
        const auto flag = [&](int code) {
            const std::string* v = find(body, code);
            return v != nullptr && to_l(*v) != 0;
        };
        const bool dimlim = flag(72);
        const bool dimtol = flag(71);
        const bool basic = getd(body, 147) < 0.0;
        ds.tol_method = static_cast<std::uint8_t>(dimlim ? 3 : dimtol ? (std::abs(tp - tm) < 1e-12 ? 1 : 2) : basic ? 4 : 0);
        ds.tol_upper = tp;
        ds.tol_lower = tm;
        ranged(272, 0, 8, [&](long x) { ds.tol_precision = static_cast<std::int8_t>(x); });
        if (const double tfac = getd(body, 146, 1.0); tfac > 0.0) {
            ds.tol_height = tfac; // DIMTFAC
        }
        if (const std::string* v = find(body, 45); v != nullptr && to_d(*v) >= 0.0) {
            ds.round_off = to_d(*v);
        }
        if (const std::string* v = find(body, 144); v != nullptr && to_d(*v) > 0.0) {
            ds.linear_factor = to_d(*v);
        }
        if (const std::string* v = find(body, 143); v != nullptr && to_d(*v) > 0.0) {
            ds.alt_factor = to_d(*v);
        }
        if (const std::string* v = find(body, 148); v != nullptr && to_d(*v) >= 0.0) {
            ds.alt_round = to_d(*v);
        }
        if (const std::string* v = find(body, 3)) {
            ds.post = *v;
        }
        if (const std::string* v = find(body, 4)) {
            ds.alt_post = *v;
        }
    };

    // An entity destination: model space (all targets) or a block definition (the
    // importable subset; dims/leaders/points inside a block route to `skipped`).
    struct Sink {
        std::vector<DocLine>* lines = nullptr;
        std::vector<DocCircle>* circles = nullptr;
        std::vector<DocArc>* arcs = nullptr;
        std::vector<DocPolyline>* polylines = nullptr;
        std::vector<DocText>* texts = nullptr;
        std::vector<DocMText>* mtexts = nullptr;
        std::vector<DocDim>* dims = nullptr;
        std::vector<DocLeader>* leaders = nullptr;
        std::vector<DocPoint>* points = nullptr;
        std::vector<DocInsert>* inserts = nullptr;
        std::vector<DocHatch>* hatches = nullptr;
        std::vector<DocXline>* xlines = nullptr;
        std::vector<DocEllipse>* ellipses = nullptr;
        std::vector<DocSpline>* splines = nullptr;
        std::vector<DocFcf>* fcfs = nullptr;
        std::vector<DocAttDef>* attdefs = nullptr;
        std::vector<std::vector<Pair>>* images = nullptr; ///< IMAGE bodies, resolved at the end
        std::vector<std::vector<Pair>>* mlines = nullptr; ///< MLINE bodies, resolved once the styles are read
    };

    // A text's justification (72 / 73), the point it is justified on (11 / 21) and its
    // width factor (41). `t.pos` holds 10 / 20 and `t.style` the STYLE entry on entry.
    const auto read_text_place = [&](const std::vector<Pair>& body, DocText& t) {
        const std::string* h72 = find(body, 72);
        const std::string* v73 = find(body, 73);
        t.justify = text::justify_from_dxf(h72 != nullptr ? static_cast<int>(to_l(*h72)) : 0,
                                           v73 != nullptr ? static_cast<int>(to_l(*v73)) : 0);
        const std::string* x11 = find(body, 11);
        const std::string* y21 = find(body, 21);
        if (text::two_point(t.justify)) {
            if (x11 != nullptr && y21 != nullptr) {
                t.align = {to_d(*x11), to_d(*y21)};
            } else {
                t.justify = 0; // no second point to run to: a plain text at its start
            }
        } else if (t.justify != 0 && x11 != nullptr && y21 != nullptr) {
            t.pos = {to_d(*x11), to_d(*y21)}; // the point the text is justified on
        }
        // 41 is the factor as drawn: the style's share comes off, and Fit works its own
        // out from its two points.
        const double drawn = getd(body, 41, 0.0);
        const double style_factor =
            (t.style != 0 && t.style < doc.text_styles.size()) ? doc.text_styles[t.style].width_factor : 1.0;
        if (drawn > 0.0 && style_factor > 0.0 && t.justify != 5) {
            t.width_factor = drawn / style_factor;
            if (std::abs(t.width_factor - 1.0) < 1e-9) {
                t.width_factor = 1.0;
            }
        }
    };

    // A dimension's own overrides of its style (the ACAD DSTYLE xdata). The sizes are
    // multiplied by DIMSCALE like the style's; the rest of the list has no per-dimension
    // home here and stays with the style.
    const auto read_dim_overrides = [&](const std::vector<Pair>& body, DocDim& d) {
        const std::vector<std::pair<long, std::string>> list = dstyle_overrides(body);
        if (list.empty()) {
            return;
        }
        const DimStyle& ds = doc.dimstyles[d.style];
        double scale = dimstyle_scale.contains(d.style) ? dimstyle_scale[d.style] : 1.0;
        for (const auto& [var, value] : list) {
            if (var == 40 && to_d(value) > 0.0) {
                scale = to_d(value);
            }
        }
        double tol_plus = ds.tol_upper;
        double tol_minus = ds.tol_lower;
        bool dimtol = false;
        bool dimlim = false;
        const auto color = [](const std::string& value, DimOverrides::Bit bit, Rgb& out_color, DimOverrides& o) {
            const long aci = to_l(value);
            if (aci >= 1 && aci <= 255) {
                out_color = aci_to_rgb(aci);
                o.set(bit, true);
            }
        };
        for (const auto& [var, value] : list) {
            const double v = to_d(value);
            switch (var) {
            case 140:
                d.overrides.text_height = v * scale;
                d.overrides.set(DimOverrides::kTextHeight, std::abs(v * scale - ds.text_height) > 1e-12);
                break;
            case 41:
                d.overrides.arrow_size = v * scale;
                d.overrides.set(DimOverrides::kArrowSize, std::abs(v * scale - ds.arrow_size) > 1e-12);
                break;
            case 271:
                d.overrides.precision = static_cast<std::uint8_t>(std::clamp(to_l(value), 0L, 8L));
                d.overrides.set(DimOverrides::kPrecision, d.overrides.precision != ds.precision);
                break;
            case 77: {
                const long tad = to_l(value);
                d.overrides.text_above = tad != 0 && tad != 4;
                d.overrides.set(DimOverrides::kTextAbove, d.overrides.text_above != ds.text_above);
                break;
            }
            case 176:
                color(value, DimOverrides::kDimColor, d.overrides.dim_color, d.overrides);
                break;
            case 177:
                color(value, DimOverrides::kExtColor, d.overrides.ext_color, d.overrides);
                break;
            case 178:
                color(value, DimOverrides::kTextColor, d.overrides.text_color, d.overrides);
                break;
            case 147:
                if (v < 0.0) {
                    d.tol.mode = TolMode::Basic; // a negative DIMGAP boxes the text
                }
                break;
            case 47:
                tol_plus = v;
                break;
            case 48:
                tol_minus = v;
                break;
            case 71:
                dimtol = v != 0.0;
                break;
            case 72:
                dimlim = v != 0.0;
                break;
            default:
                break;
            }
        }
        if (dimlim || dimtol) {
            d.tol.mode = dimlim                                  ? TolMode::Limits
                         : std::abs(tol_plus - tol_minus) < 1e-12 ? TolMode::Symmetric
                                                                  : TolMode::Deviation;
            d.tol.upper = tol_plus;
            d.tol.lower = -tol_minus;
        }
    };

    // A SOLID or TRACE: a filled triangle or quad. Its corners run 1 2 4 3 around the
    // outline (the fourth repeats the third on a triangle). Coordinates are in the entity's
    // own frame, like an arc's.
    const auto solid_outline = [&](const std::vector<Pair>& body, bool mirrored) {
        const auto corner = [&](int cx, int cy) {
            return Vec2{mirrored ? -getd(body, cx) : getd(body, cx), getd(body, cy)};
        };
        const Vec2 a = corner(10, 20);
        const Vec2 b = corner(11, 21);
        const Vec2 c = corner(12, 22);
        const Vec2 e = corner(13, 23);
        std::vector<Vec2> loop{a, b, e};
        if (length(e - c) > 1e-12) {
            loop.push_back(c);
        }
        return loop;
    };

    const auto build_entity = [&](Sink& sink, const std::string& type,
                                  const std::vector<Pair>& body) {
        // A reversed extrusion (230 = -1, what mirroring leaves behind): the entity's own
        // frame has X running the other way. Lines, points and the like are in world
        // coordinates and do not care; the entities with an own frame turn over below.
        const bool mirrored = getd(body, 230, 1.0) < 0.0;
        if (type == "SOLID" || type == "TRACE") {
            std::vector<Vec2> loop = solid_outline(body, mirrored);
            if (sink.hatches != nullptr) {
                DocHatch h;
                h.props = props_of(body);
                h.pattern_name = "SOLID";
                h.loops.push_back(std::move(loop));
                sink.hatches->push_back(std::move(h));
            } else if (sink.polylines != nullptr) { // a block holds no fills: its outline
                DocPolyline pl;
                pl.points = std::move(loop);
                pl.closed = true;
                pl.props = props_of(body);
                sink.polylines->push_back(std::move(pl));
            } else {
                ++skipped[type];
            }
            return;
        }
        if (type == "INSERT") {
            if (sink.inserts == nullptr) {
                ++skipped[type];
                return;
            }
            DocInsert ins;
            ins.props = props_of(body);
            const std::string* name = find(body, 2);
            ins.block_name = name != nullptr ? *name : std::string();
            ins.pos = {getd(body, 10), getd(body, 20)};
            ins.scale_x = getd(body, 41, 1.0);
            ins.scale_y = getd(body, 42, 1.0);
            ins.rotation = to_radians(getd(body, 50));
            if (mirrored) {
                ins.pos.x = -ins.pos.x;
                ins.scale_x = -ins.scale_x;
                ins.rotation = -ins.rotation;
            }
            // MINSERT: a grid of it, columns by 44 and rows by 45 along its own axes.
            const std::string* ncols = find(body, 70);
            const std::string* nrows = find(body, 71);
            const long cols = std::clamp(ncols != nullptr ? to_l(*ncols) : 1L, 1L, 4096L);
            const long rows = std::clamp(nrows != nullptr ? to_l(*nrows) : 1L, 1L, 4096L);
            const Vec2 across = Vec2{std::cos(ins.rotation), std::sin(ins.rotation)} * getd(body, 44);
            const Vec2 up = Vec2{-std::sin(ins.rotation), std::cos(ins.rotation)} * getd(body, 45);
            const Vec2 origin = ins.pos;
            for (long r = 0; r < rows; ++r) {
                for (long c = 0; c < cols; ++c) {
                    if (r == rows - 1 && c == cols - 1) {
                        break; // the last one takes the original
                    }
                    DocInsert copy = ins;
                    copy.pos = origin + across * static_cast<double>(c) + up * static_cast<double>(r);
                    sink.inserts->push_back(std::move(copy));
                }
            }
            ins.pos = origin + across * static_cast<double>(cols - 1) + up * static_cast<double>(rows - 1);
            sink.inserts->push_back(std::move(ins));
            return;
        }
        if (type == "MTEXT") {
            if (sink.mtexts == nullptr) {
                ++skipped[type];
                return;
            }
            DocMText m;
            m.props = props_of(body);
            m.block.pos = {getd(body, 10), getd(body, 20)};
            m.block.height = getd(body, 40, 2.5);
            m.block.width = getd(body, 41, 0.0);
            m.block.rotation = to_radians(getd(body, 50));
            if (const std::string* a = find(body, 71)) {
                const long att = to_l(*a) - 1; // DXF 1..9 -> 0..8
                m.block.attach = static_cast<std::uint8_t>(att < 0 ? 0 : att);
            }
            if (const std::string* ls = find(body, 44)) { // line-spacing factor
                if (const double f = to_d(*ls); f > 0.0) {
                    m.block.line_spacing = f;
                }
            }
            // Long MTEXT splits into 250-char group-3 chunks followed by a final group 1;
            // concatenate them in order, then convert the inline formatting to plain text.
            std::string raw;
            for (const Pair& p : body) {
                if (p.code == 3) {
                    raw += p.value;
                }
            }
            if (const std::string* c = find(body, 1)) {
                raw += *c;
            }
            m.content = mtext_from_dxf(raw);
            m.font = font_of_entity(body);
            sink.mtexts->push_back(std::move(m));
            return;
        }
        if (type == "TEXT") {
            if (sink.texts == nullptr) {
                ++skipped[type];
                return;
            }
            DocText t;
            t.props = props_of(body);
            t.pos = {getd(body, 10), getd(body, 20)};
            t.height = getd(body, 40, 2.5);
            t.rotation = to_radians(getd(body, 50));
            if (const std::string* c = find(body, 1)) {
                t.content = *c; // keep the RAW %%-codes; they expand at render time
            }
            t.font = font_of_entity(body);
            if (const std::string* st = find(body, 7)) {
                if (const auto it = text_style_index.find(*st); it != text_style_index.end()) {
                    t.style = it->second; // model-space text references the STYLE entry
                }
            }
            read_text_place(body, t);
            sink.texts->push_back(std::move(t));
            return;
        }
        if (type == "WIPEOUT") {
            // A wipeout: a raster image of one pixel clipped to its polygon. A clip vertex
            // (14, 24) is in pixels from the top-left pixel's centre, v down: in the drawing,
            // insertion + U (x + 0.5) + V (height - y - 0.5).
            if (sink.hatches == nullptr) {
                ++skipped[type];
                return;
            }
            const Vec2 ins{getd(body, 10), getd(body, 20)};
            const Vec2 u{getd(body, 11, 1.0), getd(body, 21)};
            const Vec2 v{getd(body, 12), getd(body, 22, 1.0)};
            const double pw = getd(body, 13, 1.0);
            const double ph = getd(body, 23, 1.0);
            std::vector<Vec2> clip;
            std::optional<double> cx;
            for (const Pair& q : body) {
                if (q.code == 14) {
                    cx = to_d(q.value);
                } else if (q.code == 24 && cx) {
                    clip.push_back({*cx, to_d(q.value)});
                    cx.reset();
                }
            }
            if (getd(body, 280, 1.0) == 0.0 || clip.size() < 2) { // not clipped: the whole image
                clip = {{-0.5, -0.5}, {pw - 0.5, ph - 0.5}};
            }
            if (clip.size() == 2) { // a rectangle by two corners
                const Vec2 a0 = clip[0];
                const Vec2 a1 = clip[1];
                clip = {a0, {a1.x, a0.y}, a1, {a0.x, a1.y}};
            }
            if (clip.size() > 3 && std::abs(clip.front().x - clip.back().x) < 1e-12 &&
                std::abs(clip.front().y - clip.back().y) < 1e-12) {
                clip.pop_back(); // the boundary closed by its first vertex again
            }
            DocHatch h;
            h.props = props_of(body);
            h.pattern_name = "WIPEOUT";
            std::vector<Vec2> loop;
            for (const Vec2& c : clip) {
                loop.push_back(ins + u * (c.x + 0.5) + v * (ph - c.y - 0.5));
            }
            if (loop.size() < 3) {
                ++skipped[type];
                return;
            }
            h.loops.push_back(std::move(loop));
            sink.hatches->push_back(std::move(h));
            return;
        }
        if (type == "IMAGE") {
            // Resolved after the whole file is read: the IMAGEDEF it points to (340)
            // lives in the OBJECTS section, which follows the entities.
            if (sink.images != nullptr) {
                sink.images->push_back(body);
            } else {
                ++skipped[type];
            }
            return;
        }
        if (type == "VIEWPORT") {
            // The paper-space (id 1 is the layout's own overall viewport: skipped).
            const std::string* id = find(body, 69);
            if (id != nullptr && to_l(*id) == 1) {
                return;
            }
            DocViewport v;
            v.props = props_of(body);
            v.props.set_space(1);
            v.center = {getd(body, 10), getd(body, 20)};
            v.width = getd(body, 40, 100.0);
            v.height = getd(body, 41, 60.0);
            v.view_center = {getd(body, 12), getd(body, 22)};
            const double view_h = getd(body, 45, v.height);
            v.scale = view_h > 0.0 ? v.height / view_h : 1.0;
            if (const std::string* st = find(body, 68)) {
                v.on = to_l(*st) > 0;
            }
            for (const Pair& q : body) { // 331: a layer frozen in this viewport
                if (q.code == 331) {
                    const auto it = layer_by_handle.find(upper_hex(q.value));
                    if (it != layer_by_handle.end()) {
                        v.frozen_layers.push_back(it->second);
                    }
                }
            }
            doc.viewports.push_back(std::move(v));
            return;
        }
        if (type == "ATTDEF") {
            if (sink.attdefs == nullptr) {
                ++skipped[type];
                return;
            }
            DocAttDef a;
            a.text.props = props_of(body);
            a.text.pos = {getd(body, 10), getd(body, 20)};
            a.text.height = getd(body, 40, 2.5);
            a.text.rotation = to_radians(getd(body, 50));
            if (const std::string* tg = find(body, 2)) {
                a.text.content = *tg; // the tag
            }
            if (const std::string* pr = find(body, 3)) {
                a.prompt = *pr;
            }
            if (const std::string* dv = find(body, 1)) {
                a.def = dv->substr(0, 256);
            }
            if (const std::string* f = find(body, 70)) {
                a.flags = static_cast<std::uint8_t>(to_l(*f) & 15);
            }
            a.text.font = font_of_entity(body);
            if (const std::string* st = find(body, 7)) {
                if (const auto it = text_style_index.find(*st); it != text_style_index.end()) {
                    a.text.style = it->second;
                }
            }
            read_text_place(body, a.text);
            sink.attdefs->push_back(std::move(a));
            return;
        }
        if (type == "ATTRIB") {
            // Rides with the INSERT just read (66 = 1): its tag/value pair.
            if (sink.inserts == nullptr || sink.inserts->empty()) {
                ++skipped[type];
                return;
            }
            const std::string* tg = find(body, 2);
            const std::string* val = find(body, 1);
            sink.inserts->back().attribs.push_back(
                DocAttrib{tg != nullptr ? *tg : std::string(), val != nullptr ? *val : std::string()});
            return;
        }
        if (type == "SEQEND") {
            return; // closes an INSERT's ATTRIB run (legacy POLYLINEs never reach here)
        }
        if (type == "DIMENSION") {
            if (sink.dims == nullptr) {
                // A block holds no dimensions: its drawn form instead, which the file keeps
                // as a block of its own, placed by 12.
                const std::string* drawn = find(body, 2);
                if (sink.inserts == nullptr || drawn == nullptr) {
                    ++skipped[type];
                    return;
                }
                DocInsert ins;
                ins.props = props_of(body);
                ins.block_name = *drawn;
                ins.pos = {getd(body, 12), getd(body, 22)};
                sink.inserts->push_back(std::move(ins));
                return;
            }
            const std::string* flag_v = find(body, 70);
            const long flag_raw = flag_v != nullptr ? to_l(*flag_v) : 0;
            const long flag = flag_raw & 7;
            DocDim d;
            d.props = props_of(body);
            const Vec2 p10{getd(body, 10), getd(body, 20)};
            const Vec2 p11{getd(body, 11), getd(body, 21)}; // the middle of the text
            const Vec2 p13{getd(body, 13), getd(body, 23)};
            const Vec2 p14{getd(body, 14), getd(body, 24)};
            const Vec2 p15{getd(body, 15), getd(body, 25)};
            const Vec2 p16{getd(body, 16), getd(body, 26)};
            const bool has_text_pos = find(body, 11) != nullptr;
            // Only linear, aligned, angular and ordinate dimensions have 13 and 14. A radial
            // one that does was written by Musa CAD before these followed the DXF layout:
            // the centre in 13, the point on the circle in 14 and the text in 15.
            const bool old_radial = find(body, 13) != nullptr;
            d.a = p13;
            d.b = p14;
            d.line_pt = p10;
            DimType dt = DimType::Linear;
            switch (flag) {
            case 1:
                dt = DimType::Aligned;
                break;
            case 2:
                // Parallel lines have no angle to measure: the first line as an aligned one.
                dt = set_two_line_angular_dim(d, p13, p14, p15, p10, p16) ? DimType::Angular : DimType::Aligned;
                break;
            case 3:
                dt = DimType::Diameter;
                if (!old_radial) { // 10 and 15: opposite points on the circle
                    d.a = (p10 + p15) * 0.5;
                    d.b = p15;
                    d.line_pt = has_text_pos ? p11 : p15;
                }
                break;
            case 4:
                dt = DimType::Radius;
                if (!old_radial) { // 10 the centre, 15 the point on the arc
                    d.a = p10;
                    d.b = p15;
                    d.line_pt = has_text_pos ? p11 : p15;
                }
                break;
            case 5:
                dt = DimType::Angular;
                // 15 the vertex, 13 and 14 the rays, 10 on the arc. Musa CAD used to write
                // the vertex in 13 and the same point in 10 and 15.
                if (length(p15 - p10) > 1e-9) {
                    set_angular_dim(d, p15, p13, p14, p10);
                }
                break;
            case 6: {
                dt = DimType::Ordinate;
                d.aux = (flag_raw & 64) != 0 ? 0.0 : 1.0; // bit 64 = X-type
                // 10 is the origin it measures from, 14 the leader's end. (Files written by
                // Musa CAD before had the leader's end in 10 as well: those measure from 0,0.)
                d.origin = length(p10 - d.b) < 1e-9 ? Vec2{} : p10;
                d.line_pt = d.b;
                break;
            }
            default:
                dt = DimType::Linear;
                break;
            }
            // A rotated dimension's line angle (50); without one, the axis its points
            // differ most on (what this reader always did, and what older files mean).
            if (dt == DimType::Linear && find(body, 50) != nullptr) {
                d.aux = linear_dim_aux(to_radians(getd(body, 50)));
            }
            // Radius/diameter in the old layout: the text's place in 15.
            if ((dt == DimType::Radius || dt == DimType::Diameter) && old_radial && find(body, 15) != nullptr) {
                d.line_pt = p15;
            }
            d.type = static_cast<std::uint8_t>(dt);
            if (find(body, 53) != nullptr) {
                d.text_angle = to_radians(getd(body, 53)); // the text's own angle; 0 = as laid out
            }
            if ((dt == DimType::Linear || dt == DimType::Aligned) && find(body, 52) != nullptr) {
                d.oblique = to_radians(getd(body, 52)); // the extension lines' angle
            }
            if (const std::string* st = find(body, 3)) {
                d.style = ensure_dimstyle(*st);
            }
            // The text: "" and "<>" are the measurement, anything else replaces it, with
            // <> standing for the measurement inside it.
            if (const std::string* ov = find(body, 1); ov != nullptr && !ov->empty() && *ov != "<>") {
                d.text_override = text::mtext_plain(mtext_from_dxf(*ov), true);
            }
            read_dim_overrides(body, d);
            // Text the author moved (flag 128): the offset from where the layout puts it.
            // A radial dimension's place is its line_pt already.
            if ((flag_raw & 128) != 0 && has_text_pos && dt != DimType::Radius && dt != DimType::Diameter) {
                DimData probe;
                probe.type = dt;
                probe.a = d.a;
                probe.b = d.b;
                probe.line_pt = d.line_pt;
                probe.aux = d.aux;
                probe.tol = d.tol;
                probe.overrides = d.overrides;
                probe.text_angle = d.text_angle;
                probe.oblique = d.oblique;
                probe.origin = d.origin;
                const DimStyle style = apply_dim_overrides(doc.dimstyles[d.style], d.overrides);
                const DimGeometry base = compute_dim_geometry(probe, style, Rgb{}, {d.prefix, d.suffix, d.text_override});
                Vec2 q[4];
                const Vec2 anchor = dim_label_quad(base, false, q) ? (q[0] + q[2]) * 0.5 : base.text_pos;
                const Vec2 delta = p11 - anchor;
                const double cs = std::cos(base.layout_rotation);
                const double sn = std::sin(base.layout_rotation);
                d.text_offset = {delta.x * cs + delta.y * sn, -delta.x * sn + delta.y * cs};
            }
            if (const std::string* h = find(body, 5)) {
                dim_by_handle[upper_hex(*h)] = sink.dims->size();
            }
            sink.dims->push_back(std::move(d));
            return;
        }
        if (type == "TOLERANCE") {
            if (sink.fcfs == nullptr) {
                ++skipped[type];
                return;
            }
            DocFcf f;
            f.props = props_of(body);
            f.pos = {getd(body, 10), getd(body, 20)};
            f.rotation = std::atan2(getd(body, 21, 0.0), getd(body, 11, 1.0));
            if (const std::string* st = find(body, 3)) {
                f.style = ensure_dimstyle(*st);
            }
            const std::string* tol_ptr = find(body, 1);
            const std::string tol_text = tol_ptr != nullptr ? *tol_ptr : std::string{};
            // Rows are separated by ^J and cells by %%v; symbols come back as \U+ escapes.
            std::size_t row_start = 0;
            for (;;) {
                const std::size_t row_end = tol_text.find("^J", row_start);
                const std::string row =
                    tol_text.substr(row_start, row_end == std::string::npos ? std::string::npos : row_end - row_start);
                std::size_t start = 0;
                for (;;) {
                    const std::size_t sep = row.find("%%v", start);
                    std::string cell = gdt_from_dxf(row.substr(start, sep == std::string::npos ? std::string::npos : sep - start));
                    if (!cell.empty()) { // an empty compartment is not drawn, as in AutoCAD
                        f.cells.push_back(std::move(cell));
                    }
                    if (sep == std::string::npos) {
                        break;
                    }
                    start = sep + 3;
                }
                if (row_end == std::string::npos) {
                    break;
                }
                f.cells.push_back("^J");
                row_start = row_end + 2;
            }
            while (!f.cells.empty() && f.cells.back() == "^J") {
                f.cells.pop_back(); // no row after the last break
            }
            sink.fcfs->push_back(std::move(f));
            return;
        }
        if (type == "LEADER") {
            if (sink.leaders == nullptr) {
                if (sink.polylines == nullptr) {
                    ++skipped[type];
                    return;
                }
                DocPolyline pl; // a block holds no leaders: its line
                double vx = 0.0;
                for (const Pair& pr : body) {
                    if (pr.code == 10) {
                        vx = to_d(pr.value);
                    } else if (pr.code == 20) {
                        pl.points.push_back({vx, to_d(pr.value)});
                    }
                }
                pl.props = props_of(body);
                sink.polylines->push_back(std::move(pl));
                return;
            }
            // The label is the MTEXT or TEXT that follows, read on its own.
            std::vector<Vec2> path;
            double vx = 0.0;
            for (const Pair& pr : body) {
                if (pr.code == 10) {
                    vx = to_d(pr.value);
                } else if (pr.code == 20) {
                    path.push_back({vx, to_d(pr.value)});
                }
            }
            if (path.size() < 2) {
                ++skipped[type]; // no line to draw
                return;
            }
            const std::string* style_name = find(body, 3);
            const std::uint16_t style = style_name != nullptr ? ensure_dimstyle(*style_name) : std::uint16_t{0};
            if (path.size() > 2) { // a bent leader needs every vertex
                DocMLeader m;
                m.vertices = std::move(path);
                m.style = style;
                m.block.pos = m.vertices.back();
                m.props = props_of(body);
                if (const std::string* arrow = find(body, 71); arrow != nullptr && to_l(*arrow) == 0) {
                    m.leader_flags |= LeaderForm::kNoArrow;
                }
                if (const std::string* form = find(body, 72); form != nullptr && to_l(*form) == 1) {
                    m.leader_flags |= LeaderForm::kSpline;
                }
                doc.mleaders.push_back(std::move(m));
                return;
            }
            DocLeader l;
            l.props = props_of(body);
            l.tip = path.front();
            l.knee = path.back();
            l.style = style;
            sink.leaders->push_back(std::move(l));
            return;
        }
        if (type == "MULTILEADER") {
            // CONTEXT_DATA{ (300 ... 301) holds the text, then a LEADER{ (302 ... 303) per
            // landing: its point (10), the dogleg's direction (11) and length (40), and a
            // LEADER_LINE{ (304 ... 305) per line with its points from the arrowhead on.
            struct Leg {
                std::vector<std::vector<Vec2>> lines;
                std::optional<Vec2> landing;
                Vec2 dogleg_dir{};
                double dogleg = 0.0;
            };
            std::vector<Leg> legs;
            std::string raw_text;
            Vec2 text_pos{};
            Vec2 line_x{};
            Vec2 leg_x{};
            double text_height = 2.5;
            double text_width = 0.0;
            double text_rotation = 0.0;
            double arrow = 0.0;
            long attach = 1;
            long line_type = 1; // 0 no line, 1 straight, 2 spline
            bool in_context = false;
            bool in_leg = false;
            bool in_line = false;
            bool past_context = false;
            // Block content (#61): in CONTEXT_DATA 296 says so, 341 the block's record, 15/25
            // where it is and 16 its scale; after it, each attribute's index (177) and value (302).
            bool has_block = false;
            std::string block_record;
            Vec2 block_pos{};
            double block_scale = 1.0;
            long attr_index = 0;
            std::vector<std::pair<long, std::string>> attr_values;
            for (const Pair& q : body) {
                if (q.code == 300 && q.value == "CONTEXT_DATA{") {
                    in_context = true;
                } else if (q.code == 301) {
                    in_context = false;
                    past_context = true;
                } else if (q.code == 302 && q.value == "LEADER{") {
                    in_leg = true;
                    legs.emplace_back();
                } else if (q.code == 303) {
                    in_leg = false;
                } else if (q.code == 304 && q.value == "LEADER_LINE{") {
                    in_line = true;
                    legs.back().lines.emplace_back();
                } else if (q.code == 305) {
                    in_line = false;
                } else if (in_line) {
                    if (q.code == 10) {
                        line_x.x = to_d(q.value);
                    } else if (q.code == 20) {
                        legs.back().lines.back().push_back({line_x.x, to_d(q.value)});
                    }
                } else if (in_leg) {
                    if (q.code == 10) {
                        leg_x.x = to_d(q.value);
                    } else if (q.code == 20) {
                        legs.back().landing = Vec2{leg_x.x, to_d(q.value)};
                    } else if (q.code == 11) {
                        legs.back().dogleg_dir.x = to_d(q.value);
                    } else if (q.code == 21) {
                        legs.back().dogleg_dir.y = to_d(q.value);
                    } else if (q.code == 40) {
                        legs.back().dogleg = to_d(q.value);
                    }
                } else if (in_context) {
                    switch (q.code) {
                    case 304:
                        raw_text += q.value;
                        break;
                    case 12:
                        text_pos.x = to_d(q.value);
                        break;
                    case 22:
                        text_pos.y = to_d(q.value);
                        break;
                    case 41:
                        text_height = to_d(q.value);
                        break;
                    case 42:
                        text_rotation = to_d(q.value);
                        break;
                    case 43:
                        text_width = to_d(q.value);
                        break;
                    case 140:
                        arrow = to_d(q.value);
                        break;
                    case 171:
                        attach = to_l(q.value);
                        break;
                    case 296:
                        has_block = to_l(q.value) == 1;
                        break;
                    case 341:
                        block_record = q.value;
                        break;
                    case 15:
                        block_pos.x = to_d(q.value);
                        break;
                    case 25:
                        block_pos.y = to_d(q.value);
                        break;
                    case 16:
                        block_scale = to_d(q.value);
                        break;
                    default:
                        break;
                    }
                } else if (past_context && q.code == 177) {
                    attr_index = to_l(q.value);
                } else if (past_context && q.code == 302) {
                    attr_values.emplace_back(attr_index, q.value);
                } else if (past_context && q.code == 170) {
                    line_type = to_l(q.value);
                }
            }
            const auto path_of = [](const Leg& leg, const std::vector<Vec2>& line) {
                std::vector<Vec2> path = line;
                if (leg.landing) {
                    path.push_back(*leg.landing);
                    if (leg.dogleg > 0.0) {
                        path.push_back(*leg.landing + leg.dogleg_dir * leg.dogleg);
                    }
                }
                return path;
            };
            MTextBlock label;
            label.pos = text_pos;
            label.height = text_height > 0.0 ? text_height : 2.5;
            label.width = text_width;
            label.rotation = text_rotation;
            label.attach = static_cast<std::uint8_t>(std::clamp(attach - 1, 0L, 8L));
            const std::string content = mtext_from_dxf(raw_text);
            if (sink.mtexts == nullptr || sink.polylines == nullptr) {
                ++skipped[type];
                return;
            }
            if (sink.leaders == nullptr) { // a block holds no multileaders: its lines and text
                for (const Leg& leg : legs) {
                    for (const std::vector<Vec2>& line : leg.lines) {
                        DocPolyline pl;
                        pl.points = path_of(leg, line);
                        pl.props = props_of(body);
                        if (pl.points.size() >= 2 && line_type != 0) {
                            sink.polylines->push_back(std::move(pl));
                        }
                    }
                }
                if (!content.empty()) {
                    DocMText m;
                    m.block = label;
                    m.content = content;
                    m.props = props_of(body);
                    sink.mtexts->push_back(std::move(m));
                }
                return;
            }
            DocMLeader m;
            for (const Leg& leg : legs) {
                for (const std::vector<Vec2>& line : leg.lines) {
                    if (m.vertices.empty()) {
                        m.vertices = path_of(leg, line);
                    } else if (!line.empty()) {
                        m.extra_tips.push_back(line.front());
                    }
                }
            }
            if (m.vertices.empty() && content.empty()) {
                ++skipped[type]; // a block-content multileader with no leader of its own
                return;
            }
            if (m.vertices.empty()) {
                m.vertices.push_back(text_pos);
            }
            m.block = label;
            m.content = content;
            m.props = props_of(body);
            if (content.empty()) {
                m.block.pos = m.vertices.back();
            }
            if (const auto rec = block_record_names.find(upper_hex(block_record));
                has_block && rec != block_record_names.end()) {
                // The block, its values (by their index) in the text, its scale as its size.
                std::stable_sort(attr_values.begin(), attr_values.end(),
                                 [](const auto& a, const auto& b) { return a.first < b.first; });
                m.content_block = rec->second;
                m.content.clear();
                for (std::size_t k = 0; k < attr_values.size(); ++k) {
                    m.content += (k == 0 ? "" : "\x1F") + attr_values[k].second;
                }
                m.block = MTextBlock{};
                m.block.pos = block_pos;
                m.block.height = block_scale > 0.0 ? block_scale : 1.0;
            }
            if (line_type == 0) {
                m.leader_flags |= LeaderForm::kNoLine;
            } else if (line_type == 2) {
                m.leader_flags |= LeaderForm::kSpline;
            }
            if (arrow > 0.0) {
                m.overrides.arrow_size = arrow;
                m.overrides.set(DimOverrides::kArrowSize, true);
            }
            doc.mleaders.push_back(std::move(m));
            return;
        }
        if (type == "HATCH") {
            if (sink.hatches == nullptr) {
                ++skipped[type];
                return;
            }
            DocHatch h;
            h.props = props_of(body);
            if (const std::string* pn = find(body, 2)) {
                h.pattern_name = *pn;
            }
            h.unassoc = getd(body, 71, 0.0) == 0.0; // 71: the associativity flag
            if (getd(body, 76, 1.0) == 0.0 || h.pattern_name == "_USER" || h.pattern_name == "_user") {
                h.pattern_name = getd(body, 77, 0.0) != 0.0 ? "_USERDOUBLE" : "_USER"; // user-defined
            }
            h.pattern_scale = getd(body, 41, 1.0);
            h.pattern_angle = to_radians(getd(body, 52, 0.0));
            // Walk the body in order: code 93 opens a polyline loop with N vertices; the
            // 10/20 pairs that follow fill it. A 10/20 pair outside any loop (the seed point
            // emitted after code 98) is the pattern origin.
            int remaining = 0;
            std::vector<Vec2>* cur = nullptr;
            std::optional<double> px;
            for (const Pair& pr : body) {
                if (pr.code == 93) {
                    h.loops.emplace_back();
                    cur = &h.loops.back();
                    remaining = static_cast<int>(to_l(pr.value));
                    cur->reserve(static_cast<std::size_t>(std::max(0, remaining)));
                    px.reset();
                } else if (pr.code == 10) {
                    px = to_d(pr.value);
                } else if (pr.code == 20 && px) {
                    const Vec2 p{*px, to_d(pr.value)};
                    px.reset();
                    if (cur != nullptr && remaining > 0) {
                        cur->push_back(p);
                        if (--remaining == 0) {
                            cur = nullptr;
                        }
                    } else {
                        h.pattern_origin = p; // seed / origin (last one wins)
                    }
                }
            }
            if (const std::string* g = find(body, 450); g != nullptr && to_l(*g) == 1) {
                // A gradient fill: the second 421 stop is the second colour, 460 the angle.
                int stops = 0;
                for (const Pair& gp : body) {
                    if (gp.code == 421) {
                        ++stops;
                        if (stops == 2) {
                            h.color2 = from_true_color(to_l(gp.value));
                        }
                    }
                }
                // 470 the shape (LINEAR, SPHERICAL ...): a linear one keeps the name it always had.
                const std::string* name = find(body, 470);
                const hatch::GradientShape shape = hatch::gradient_from_dxf(name != nullptr ? *name : std::string());
                h.pattern_name = shape == hatch::GradientShape::Linear ? std::string("GRADIENT")
                                                                        : std::string(hatch::gradient_pattern_name(shape));
                h.pattern_angle = getd(body, 460, 0.0);
            }
            // A standard pattern at acadiso.pat's size: its line families, written as drawn,
            // space 25.4 times as wide as acad.pat's at the hatch's scale. With no families
            // written, the drawing's MEASUREMENT says which file it came from.
            if (const hatch::Pattern* std_pat = hatch::builtin_pattern(h.pattern_name);
                std_pat != nullptr && !std_pat->lines.empty() && h.pattern_name != "_USER" &&
                h.pattern_name != "_USERDOUBLE") {
                std::optional<Vec2> first_offset;
                bool in_lines = false;
                std::optional<double> ox;
                for (const Pair& pr : body) {
                    if (pr.code == 78) {
                        in_lines = true;
                    } else if (in_lines && pr.code == 45) {
                        ox = to_d(pr.value);
                    } else if (in_lines && pr.code == 46 && ox) {
                        first_offset = Vec2{*ox, to_d(pr.value)};
                        break;
                    } else if (pr.code == 98) {
                        break;
                    }
                }
                const double own = length(std_pat->lines.front().delta) * (h.pattern_scale > 0.0 ? h.pattern_scale : 1.0);
                if (first_offset && own > 1e-12) {
                    const double ratio = length(*first_offset) / own;
                    h.iso = std::abs(ratio - hatch::kIsoPatternFactor) < 0.01 * hatch::kIsoPatternFactor;
                } else {
                    h.iso = doc.display_units.measurement == 1;
                }
            }
            // A pattern Musa CAD does not have (an AutoCAD drawing's custom one): its line
            // families come with the hatch, scaled and turned as it draws them -- taken back
            // to the definition and kept with the drawing, so it draws here too.
            if (h.pattern_name != "SOLID" && !hatch::is_gradient(h.pattern_name) && h.pattern_name != "_USER" &&
                h.pattern_name != "_USERDOUBLE" && hatch::builtin_pattern(h.pattern_name) == nullptr &&
                std::none_of(doc.patterns.begin(), doc.patterns.end(),
                             [&](const hatch::Pattern& p) { return p.name == h.pattern_name; })) {
                const double s = h.pattern_scale > 0.0 ? h.pattern_scale : 1.0;
                const auto turn = [](Vec2 v, double a) {
                    return Vec2{v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a)};
                };
                hatch::Pattern pat;
                pat.name = h.pattern_name;
                bool in_lines = false;
                for (const Pair& pr : body) {
                    if (pr.code == 78) {
                        in_lines = true;
                    } else if (!in_lines) {
                        continue;
                    } else if (pr.code == 53) {
                        pat.lines.emplace_back();
                        pat.lines.back().angle = to_d(pr.value); // the line's angle as drawn
                    } else if (pat.lines.empty()) {
                        continue;
                    } else if (pr.code == 43) {
                        pat.lines.back().origin.x = to_d(pr.value);
                    } else if (pr.code == 44) {
                        pat.lines.back().origin.y = to_d(pr.value);
                    } else if (pr.code == 45) {
                        pat.lines.back().delta.x = to_d(pr.value);
                    } else if (pr.code == 46) {
                        pat.lines.back().delta.y = to_d(pr.value);
                    } else if (pr.code == 49) {
                        pat.lines.back().dashes.push_back(to_d(pr.value) / s);
                    } else if (pr.code == 98) {
                        break; // the seed points follow the pattern data
                    }
                }
                for (hatch::PatternLine& l : pat.lines) {
                    const double drawn = to_radians(l.angle);
                    l.origin = turn(l.origin, -h.pattern_angle) / s;
                    l.delta = turn(l.delta, -drawn) / s; // the offset in the line's own frame
                    l.angle = l.angle - to_degrees(h.pattern_angle);
                }
                if (!pat.lines.empty()) {
                    doc.patterns.push_back(std::move(pat));
                }
            }
            if (mirrored) { // its boundary and origin are in its own frame
                for (std::vector<Vec2>& loop : h.loops) {
                    for (Vec2& q : loop) {
                        q.x = -q.x;
                    }
                }
                h.pattern_origin.x = -h.pattern_origin.x;
                h.pattern_angle = kPi - h.pattern_angle;
            }
            sink.hatches->push_back(std::move(h));
            return;
        }
        if (type == "LINE") {
            if (sink.lines != nullptr) {
                sink.lines->push_back(DocLine{{getd(body, 10), getd(body, 20)},
                                              {getd(body, 11), getd(body, 21)},
                                              props_of(body),
                                              getd(body, 48, 1.0)}); // code 48 = CELTSCALE
            } else {
                ++skipped[type];
            }
        } else if (type == "CIRCLE") {
            if (sink.circles != nullptr) {
                sink.circles->push_back(DocCircle{{mirrored ? -getd(body, 10) : getd(body, 10), getd(body, 20)},
                                                  getd(body, 40),
                                                  props_of(body),
                                                  getd(body, 48, 1.0)});
            } else {
                ++skipped[type];
            }
        } else if (type == "ARC") {
            if (sink.arcs != nullptr) {
                // Turned over, the arc runs the other way: it starts where it used to end.
                const double a50 = to_radians(getd(body, 50));
                const double a51 = to_radians(getd(body, 51));
                sink.arcs->push_back(DocArc{{mirrored ? -getd(body, 10) : getd(body, 10), getd(body, 20)},
                                            getd(body, 40),
                                            mirrored ? kPi - a51 : a50,
                                            mirrored ? kPi - a50 : a51,
                                            props_of(body),
                                            getd(body, 48, 1.0)});
            } else {
                ++skipped[type];
            }
        } else if (type == "POINT") {
            if (sink.points != nullptr) {
                sink.points->push_back(DocPoint{{getd(body, 10), getd(body, 20)}, props_of(body)});
            } else {
                ++skipped[type];
            }
        } else if (type == "MLINE") {
            if (sink.mlines != nullptr) {
                sink.mlines->push_back(body);
            } else {
                ++skipped[type];
            }
        } else if (type == "XLINE" || type == "RAY") {
            if (sink.xlines != nullptr) {
                const Vec2 dir{getd(body, 11), getd(body, 21)};
                sink.xlines->push_back(DocXline{{getd(body, 10), getd(body, 20)},
                                                dir,
                                                type == "RAY",
                                                props_of(body)});
            } else {
                ++skipped[type];
            }
        } else if (type == "LWPOLYLINE") {
            if (sink.polylines == nullptr) {
                ++skipped[type];
                return;
            }
            DocPolyline pl;
            const std::string* flag = find(body, 70);
            pl.closed = flag != nullptr && (to_l(*flag) & 1) != 0;
            pl.ltype_gen = flag != nullptr && (to_l(*flag) & 128) != 0; // off unless set, as AutoCAD reads it
            double x = 0.0;
            bool have_x = false;
            bool any_bulge = false;
            // Widths: 43 is one width for every segment; 40 / 41 after a vertex are the
            // start and end width of the segment leaving it (they default to 43's).
            // A legacy POLYLINE header gives its default start and end width as 40 / 41
            // ahead of the vertices.
            const double constant = getd(body, 43, 0.0);
            double default_start = constant;
            double default_end = constant;
            bool any_width = constant > 0.0;
            double frame_x = 0.0;
            for (const Pair& p : body) {
                if (p.code == 10) {
                    x = to_d(p.value);
                    have_x = true;
                } else if ((p.code == 40 || p.code == 41) && pl.points.empty()) {
                    (p.code == 40 ? default_start : default_end) = to_d(p.value);
                    any_width = any_width || to_d(p.value) > 0.0;
                } else if (p.code == 20 && have_x) {
                    pl.points.push_back({x, to_d(p.value)});
                    pl.bulges.push_back(0.0); // default straight; a code 42 may follow
                    pl.widths.push_back(default_start);
                    pl.widths.push_back(default_end);
                    have_x = false;
                } else if (p.code == 42 && !pl.bulges.empty()) {
                    pl.bulges.back() = to_d(p.value); // bulge of the vertex just read
                    any_bulge = true;
                } else if (p.code == 40 && !pl.points.empty()) {
                    pl.widths[pl.widths.size() - 2] = to_d(p.value);
                    any_width = any_width || to_d(p.value) > 0.0;
                } else if (p.code == 41 && !pl.points.empty()) {
                    pl.widths.back() = to_d(p.value);
                    any_width = any_width || to_d(p.value) > 0.0;
                } else if (p.code == 2070) { // from a POLYLINE: PEDIT Fit (1) or Spline (5, 6)
                    pl.fit = static_cast<std::uint8_t>(std::clamp(to_l(p.value), 0L, 6L));
                } else if (p.code == 2011) { // ... a vertex placed, and its tangent
                    frame_x = to_d(p.value);
                } else if (p.code == 2021) {
                    pl.frame.push_back({frame_x, to_d(p.value)});
                } else if (p.code == 2050) {
                    pl.tangents.push_back(p.value == "nan" ? std::numeric_limits<double>::quiet_NaN()
                                                           : to_radians(to_d(p.value)));
                }
            }
            if (mirrored) { // x runs the other way, and so does every bulge
                for (Vec2& q : pl.points) {
                    q.x = -q.x;
                }
                for (double& bulge : pl.bulges) {
                    bulge = -bulge;
                }
            }
            if (pl.fit != 1 && pl.fit != 5 && pl.fit != 6) {
                pl.fit = 0;
            }
            if (pl.fit == 0 || pl.frame.size() < 2) {
                pl.fit = 0;
                pl.frame.clear();
            }
            if (std::all_of(pl.tangents.begin(), pl.tangents.end(), [](double a) { return std::isnan(a); })) {
                pl.tangents.clear();
            }
            if (!any_bulge) {
                pl.bulges.clear(); // all straight -> store none
            }
            if (!any_width) {
                pl.widths.clear();
            }
            pl.elevation = getd(body, 38, 0.0);
            pl.thickness = getd(body, 39, 0.0);
            pl.props = props_of(body);
            pl.celtscale = getd(body, 48, 1.0); // code 48 = CELTSCALE
            sink.polylines->push_back(std::move(pl));
        } else if (type == "SPLINE") {
            // Evaluate the ACTUAL B-spline (control points 10/20 + degree 71 + knots 40)
            // via de Boor and store the tessellated curve as a polyline -- real DXF splines
            // are non-uniform, so a uniform approximation gives wild curves. Falls back to
            // fit points (11/21) if there are no control points.
            if (sink.polylines == nullptr && sink.splines == nullptr) {
                ++skipped[type];
                return;
            }
            int degree = 3;
            if (const std::string* d = find(body, 71)) {
                degree = static_cast<int>(to_l(*d));
            }
            std::vector<Vec2> ctrl;
            std::vector<double> knots;
            double x = 0.0;
            bool have_x = false;
            for (const Pair& p : body) {
                if (p.code == 10) {
                    x = to_d(p.value);
                    have_x = true;
                } else if (p.code == 20 && have_x) {
                    ctrl.push_back({x, to_d(p.value)});
                    have_x = false;
                } else if (p.code == 40) {
                    knots.push_back(to_d(p.value));
                }
            }
            // The fit data (11/21 fit points, 44 tolerance, 12/22 and 13/23 end tangents): a spline
            // by fit points keeps them; without control points they define it.
            std::vector<Vec2> fit;
            have_x = false;
            for (const Pair& p : body) {
                if (p.code == 11) {
                    x = to_d(p.value);
                    have_x = true;
                } else if (p.code == 21 && have_x) {
                    fit.push_back({x, to_d(p.value)});
                    have_x = false;
                }
            }
            if (ctrl.empty()) {
                knots.clear();
            }
            SplineFit fit_data;
            if (fit.size() >= 2) {
                fit_data.points = fit;
                // AutoCAD writes a fit tolerance of 1e-10 for "through the points".
                const double tol = getd(body, 44, 0.0);
                fit_data.tolerance = tol > 1e-6 ? tol : 0.0;
                if (find(body, 12) != nullptr) {
                    fit_data.start_tangent = Vec2{getd(body, 12), getd(body, 22)};
                }
                if (find(body, 13) != nullptr) {
                    fit_data.end_tangent = Vec2{getd(body, 13), getd(body, 23)};
                }
                // Closed: flagged so (70 bit 1), or round to its first point again.
                const std::string* sf = find(body, 70);
                const bool flagged = sf != nullptr && (to_l(*sf) & 1) != 0;
                const bool round = fit_data.points.size() >= 4 &&
                                   length(fit_data.points.front() - fit_data.points.back()) < 1e-9;
                if ((flagged || round) && fit_data.points.size() >= 3) {
                    if (round) {
                        fit_data.points.pop_back();
                    }
                    fit_data.closed = true;
                    fit_data.start_tangent.reset();
                    fit_data.end_tangent.reset();
                }
            }
            if (sink.splines != nullptr) {
                // A REAL spline whenever it can be represented exactly: fit points are
                // interpolated (chord parameterisation, AutoCAD's default), and control
                // points are taken as they are -- with their own knots when those are not
                // our clamped uniform ones (as an AutoCAD spline's almost never are). Knots
                // that do not fit the control points draw as a tessellated polyline below.
                const auto is_clamped_uniform = [&](const std::vector<double>& k) {
                    const int cp_count = static_cast<int>(ctrl.size());
                    const int deg = spline::effective_degree(cp_count, degree);
                    const std::vector<double> ref = spline::clamped_knots(cp_count, deg);
                    if (k.size() != ref.size()) {
                        return false;
                    }
                    const double k0 = k.front();
                    const double kr = k.back() - k0;
                    if (!(kr > 0.0)) {
                        return false;
                    }
                    for (std::size_t ki = 0; ki < k.size(); ++ki) {
                        if (std::abs((k[ki] - k0) / kr - ref[ki]) > 1e-9) {
                            return false;
                        }
                    }
                    return true;
                };
                if (ctrl.empty() && fit.size() >= 2) {
                    DocSpline sp;
                    sp.control_points = spline::fit_spline(fit_data.points, degree, spline::FitParam::Chord,
                                                           fit_data.tolerance, fit_data.closed, fit_data.start_tangent,
                                                           fit_data.end_tangent);
                    sp.degree = static_cast<std::uint32_t>(degree);
                    sp.props = props_of(body);
                    sp.fit = fit_data;
                    sink.splines->push_back(std::move(sp));
                    return;
                }
                const int cp_n = static_cast<int>(ctrl.size());
                const bool own = cp_n >= 2 && !knots.empty() && !is_clamped_uniform(knots) &&
                                 spline::usable_knots(knots, cp_n, spline::effective_degree(cp_n, degree));
                if (ctrl.size() >= 2 && (knots.empty() || is_clamped_uniform(knots) || own)) {
                    DocSpline sp;
                    sp.control_points = ctrl;
                    sp.degree = static_cast<std::uint32_t>(degree);
                    sp.props = props_of(body);
                    if (own) {
                        sp.knots = knots;
                    }
                    sp.fit = fit_data; // drawn by its control points; dragged by its fit points
                    sink.splines->push_back(std::move(sp));
                    return;
                }
            }
            if (sink.polylines == nullptr) {
                ++skipped[type];
                return;
            }
            if (ctrl.empty()) {
                ctrl = fit; // legacy approximation: fit points evaluated as control points
            }
            if (ctrl.size() < 2) {
                ++skipped[type];
                return;
            }
            DocPolyline pl;
            pl.points = tessellate_bspline(ctrl, degree, std::move(knots));
            pl.props = props_of(body);
            sink.polylines->push_back(std::move(pl));
        } else if (type == "ELLIPSE") {
            // DXF: 10/20 centre, 11/21 major-axis endpoint RELATIVE to centre, 40
            // minor/major ratio, 41/42 start/end parameters (radians) -- the same
            // definition as our ELLIPSE entity, so model space gets a real ellipse.
            // Block definitions hold no ellipses yet, so there it is tessellated to a
            // polyline (one path, no phantom connectors) as before.
            // Its points are in world coordinates; turned over, only the sweep reverses.
            const double e41 = mirrored ? 6.283185307179586 - getd(body, 42, 6.283185307179586) : getd(body, 41, 0.0);
            const double e42 = mirrored ? 6.283185307179586 - getd(body, 41, 0.0) : getd(body, 42, 6.283185307179586);
            if (sink.ellipses != nullptr) {
                sink.ellipses->push_back(DocEllipse{{getd(body, 10), getd(body, 20)},
                                                    {getd(body, 11), getd(body, 21)},
                                                    getd(body, 40, 1.0),
                                                    e41,
                                                    e42,
                                                    props_of(body)});
                return;
            }
            if (sink.polylines == nullptr) {
                ++skipped[type];
                return;
            }
            const Vec2 c{getd(body, 10), getd(body, 20)};
            const Vec2 major{getd(body, 11), getd(body, 21)};
            const double ratio = getd(body, 40, 1.0);
            const double a0 = e41;
            const double a1 = e42;
            const Vec2 minor{-major.y * ratio, major.x * ratio}; // perp(major) * ratio
            double sweep = a1 - a0;
            if (sweep <= 0.0) {
                sweep += 6.283185307179586;
            }
            const bool full = sweep >= 6.283185307179586 - 1e-6;
            const auto segs = static_cast<std::size_t>(std::clamp(sweep / 0.1, 24.0, 256.0));
            DocPolyline pl;
            pl.closed = full;
            const std::size_t np = full ? segs : segs + 1;
            for (std::size_t k = 0; k < np; ++k) {
                const double t = a0 + sweep * (static_cast<double>(k) / static_cast<double>(segs));
                pl.points.push_back({c.x + major.x * std::cos(t) + minor.x * std::sin(t),
                                     c.y + major.y * std::cos(t) + minor.y * std::sin(t)});
            }
            pl.props = props_of(body);
            sink.polylines->push_back(std::move(pl));
        } else {
            ++skipped[type];
        }
    };

    // Legacy POLYLINE (the R12-era form): a header entity, then one VERTEX entity per
    // vertex, then SEQEND. Folded into the LWPOLYLINE pair list and handed to the same
    // builder, so the two forms cannot drift apart. 3D polylines and polygon/polyface
    // meshes (header flag bits 8, 16, 64) are not plane polylines: reported as skipped.
    // Spline-frame control vertices (vertex flag 16) are the invisible frame of a
    // spline-fit polyline; the fitted vertices (flag 8) that follow them are the curve.
    std::vector<Pair> legacy_poly;
    bool in_legacy_poly = false;
    bool legacy_poly_skip = false;
    long legacy_fit = 0; // PEDIT Fit (1) or Spline (5, 6) of the POLYLINE being read
    const auto legacy_polyline = [&](Sink& sink, const std::string& type,
                                     const std::vector<Pair>& body) {
        if (type == "POLYLINE") {
            legacy_poly.clear();
            const std::string* hf = find(body, 70);
            const long flags = hf != nullptr ? to_l(*hf) : 0;
            legacy_poly_skip = (flags & (8 | 16 | 64)) != 0;
            // Fitted: 4 a spline-fit polyline (its curve type 75: 5 quadratic, 6 cubic), 2 a
            // curve-fit one; kept as pseudo-pairs the LWPOLYLINE reading below takes.
            legacy_fit = 0;
            if ((flags & 4) != 0) {
                const std::string* ct = find(body, 75);
                legacy_fit = ct != nullptr && to_l(*ct) == 5 ? 5 : 6;
            } else if ((flags & 2) != 0) {
                legacy_fit = 1;
            }
            if (legacy_poly_skip) {
                ++skipped["POLYLINE (3D/mesh)"];
            }
            for (const Pair& q : body) {
                // The header's 10/20/30 is a dummy point and 66 the "vertices follow" flag.
                if (q.code != 10 && q.code != 20 && q.code != 30 && q.code != 66) {
                    legacy_poly.push_back(q);
                }
            }
            if (legacy_fit != 0) {
                legacy_poly.push_back(Pair{2070, std::to_string(legacy_fit)});
            }
            in_legacy_poly = true;
            return true;
        }
        if (!in_legacy_poly) {
            return false;
        }
        if (type == "VERTEX") {
            const std::string* vf = find(body, 70);
            const long flags = vf != nullptr ? to_l(*vf) : 0;
            // A vertex placed -- a spline-fit polyline's frame (16), a curve-fit one's own vertex
            // (not one Fit added, 1), any vertex of a plain one -- with its tangent (2, code 50):
            // pseudo-pairs 2011 / 2021 / 2050 (past DXF's last code, 1071: never an XDATA pair).
            const bool placed = (flags & 16) != 0 || (legacy_fit == 1 && (flags & 1) == 0) || legacy_fit == 0;
            if (!legacy_poly_skip && placed) {
                const std::string* px = find(body, 10);
                const std::string* py = find(body, 20);
                const std::string* ta = (flags & 2) != 0 ? find(body, 50) : nullptr;
                legacy_poly.push_back(Pair{2011, px != nullptr ? *px : std::string("0")});
                legacy_poly.push_back(Pair{2021, py != nullptr ? *py : std::string("0")});
                legacy_poly.push_back(Pair{2050, ta != nullptr ? *ta : std::string("nan")});
            }
            if (!legacy_poly_skip && (flags & 16) == 0) {
                const std::string* px = find(body, 10);
                const std::string* py = find(body, 20);
                legacy_poly.push_back(Pair{10, px != nullptr ? *px : std::string("0")});
                legacy_poly.push_back(Pair{20, py != nullptr ? *py : std::string("0")});
                if (const std::string* b = find(body, 42)) {
                    legacy_poly.push_back(Pair{42, *b});
                }
                if (const std::string* w0 = find(body, 40)) {
                    legacy_poly.push_back(Pair{40, *w0});
                }
                if (const std::string* w1 = find(body, 41)) {
                    legacy_poly.push_back(Pair{41, *w1});
                }
            }
            return true;
        }
        if (type == "SEQEND") {
            if (!legacy_poly_skip) {
                build_entity(sink, "LWPOLYLINE", legacy_poly);
            }
            in_legacy_poly = false;
            legacy_poly_skip = false;
            return true;
        }
        return false;
    };

    // Model space writes to every doc vector; block content takes the importable subset
    // (dims/leaders/points inside a block route to the skip catalog).
    std::vector<std::vector<Pair>> image_bodies;
    std::vector<std::vector<Pair>> mline_bodies;
    std::vector<MLineStyle> mline_styles_read; // MLINESTYLE objects, in the file's order
    std::string current_mlstyle_name;          // $CMLSTYLE
    struct ImageDefRecord {
        std::string path;
        double pixel_w = 0.0;
        double pixel_h = 0.0;
    };
    std::map<std::string, ImageDefRecord> imagedefs; // by handle
    Sink model_sink{&doc.lines,  &doc.circles, &doc.arcs,    &doc.polylines, &doc.texts,
                    &doc.mtexts, &doc.dims,    &doc.leaders, &doc.points,    &doc.inserts,
                    &doc.hatches, &doc.xlines, &doc.ellipses, &doc.splines, &doc.fcfs,
                    &doc.attdefs, &image_bodies, &mline_bodies};

    // The block currently being read in the BLOCKS section. Its sink takes the
    // importable subset; dims/leaders/points/nested-INSERT-targets that a block can't
    // hold route to `skipped`. The DocBlockDef is reused across blocks (stable address),
    // so the sink's pointers stay valid as each block is filled then moved into the doc.
    DocBlockDef block;
    Sink block_sink{&block.lines,  &block.circles, &block.arcs, &block.polylines, &block.texts,
                    &block.mtexts, nullptr,        nullptr,     nullptr,          &block.inserts,
                    &block.hatches, nullptr,       nullptr,     nullptr,          nullptr,
                    &block.attdefs, nullptr}; // images are not held inside block definitions
    bool in_block = false;
    // The dimension blocks that dimensions inside other blocks draw with. They can come
    // before or after the block using them, so these are found first.
    const auto is_dimension_block = [](const std::string& name) {
        return name.size() > 2 && name[0] == '*' && (name[1] == 'D' || name[1] == 'd');
    };
    // Those a dimension inside a user block draws with -- and those inserted by name, as Musa
    // CAD writes a dimension inside a block (an INSERT of its drawn form).
    std::set<std::string> block_dimensions;
    {
        bool blocks = false;
        bool entities = false;
        bool user_block = false;
        for (std::size_t k = 0; k + 1 < pairs.size(); ++k) {
            if (pairs[k].code == 2 && k > 0 && pairs[k - 1].code == 0 && pairs[k - 1].value == "SECTION") {
                blocks = pairs[k].value == "BLOCKS";
                entities = pairs[k].value == "ENTITIES";
            } else if (entities && pairs[k].code == 0 && pairs[k].value == "INSERT") {
                for (std::size_t j = k + 1; j < pairs.size() && pairs[j].code != 0; ++j) {
                    if (pairs[j].code == 2) {
                        block_dimensions.insert(pairs[j].value);
                        break;
                    }
                }
            } else if (!blocks) {
                continue;
            } else if (pairs[k].code == 0 && pairs[k].value == "BLOCK") {
                for (std::size_t j = k + 1; j < pairs.size() && pairs[j].code != 0; ++j) {
                    if (pairs[j].code == 2) {
                        user_block = !is_dimension_block(pairs[j].value);
                        break;
                    }
                }
            } else if (user_block && pairs[k].code == 0 && (pairs[k].value == "DIMENSION" || pairs[k].value == "INSERT")) {
                for (std::size_t j = k + 1; j < pairs.size() && pairs[j].code != 0; ++j) {
                    if (pairs[j].code == 2) {
                        block_dimensions.insert(pairs[j].value);
                        break;
                    }
                }
            }
        }
    }
    std::string current_dimstyle_name; // $DIMSTYLE
    std::map<std::string, std::uint8_t> block_units; // BLOCK_RECORD 70, by block name

    while (i < n) {
        const Pair& p = pairs[i];
        // Header variable: $LTSCALE <40 value>.
        if (section == "HEADER" && p.code == 9 && p.value == "$LTSCALE") {
            if (i + 1 < n && pairs[i + 1].code == 40) {
                doc.ltscale = to_d(pairs[i + 1].value);
                i += 2;
                continue;
            }
        }
        // $CMLSTYLE <2 name>: resolved against the multiline styles once they have been read.
        if (section == "HEADER" && p.code == 9 && p.value == "$CMLSTYLE") {
            if (i + 1 < n && pairs[i + 1].code == 2) {
                current_mlstyle_name = pairs[i + 1].value;
                i += 2;
                continue;
            }
        }
        // $DIMSTYLE <2 name>: resolved against the DIMSTYLE table once it has been read.
        if (section == "HEADER" && p.code == 9 && p.value == "$DIMSTYLE") {
            if (i + 1 < n && pairs[i + 1].code == 2) {
                current_dimstyle_name = pairs[i + 1].value;
                i += 2;
                continue;
            }
        }
        // The units and drawing settings this program keeps (one value each; the limits are
        // a point: codes 10 and 20).
        if (section == "HEADER" && p.code == 9 && i + 1 < n && p.value.size() > 1 && p.value[0] == '$') {
            const std::string& var = p.value;
            const Pair& q = pairs[i + 1];
            DrawingUnits& du = doc.display_units;
            const auto whole = [&](long lo, long hi) { return std::clamp(to_l(q.value), lo, hi); };
            bool took = true;
            if (var == "$DIMASSOC") {
                dimassoc_var = true;
            } else if (var == "$CMLJUST" && q.code == 70) {
                doc.cmljust = static_cast<std::uint8_t>(whole(0, 2));
            } else if (var == "$CMLSCALE" && q.code == 40) {
                doc.cmlscale = to_d(q.value);
            } else if (var == "$INSUNITS" && q.code == 70) {
                du.insunits = static_cast<std::uint8_t>(whole(0, 20));
            } else if (var == "$MEASUREMENT" && q.code == 70) {
                du.measurement = static_cast<std::uint8_t>(whole(0, 1));
            } else if (var == "$LUNITS" && q.code == 70) {
                du.linear = static_cast<LinearFormat>(whole(1, 5));
            } else if (var == "$LUPREC" && q.code == 70) {
                du.linear_precision = static_cast<std::uint8_t>(whole(0, 8));
            } else if (var == "$AUNITS" && q.code == 70) {
                du.angular = static_cast<AngleFormat>(whole(0, 4));
            } else if (var == "$AUPREC" && q.code == 70) {
                du.angular_precision = static_cast<std::uint8_t>(whole(0, 8));
            } else if (var == "$ANGDIR" && q.code == 70) {
                du.clockwise = to_l(q.value) != 0;
            } else if (var == "$ANGBASE" && q.code == 50) {
                du.base_angle = to_radians(to_d(q.value));
            } else if (var == "$PDMODE" && q.code == 70) {
                doc.pdmode = static_cast<std::uint8_t>(whole(0, 100));
            } else if (var == "$PDSIZE" && q.code == 40) {
                doc.pdsize = to_d(q.value);
            } else if (var == "$LIMCHECK" && q.code == 70) {
                doc.limits_on = to_l(q.value) != 0;
            } else if (var == "$FILLMODE" && q.code == 70) {
                doc.fillmode = to_l(q.value) != 0;
            } else if (var == "$INSBASE" && q.code == 10 && i + 2 < n && pairs[i + 2].code == 20) {
                doc.insbase = Vec2{to_d(q.value), to_d(pairs[i + 2].value)};
                i += 3;
                continue;
            } else if ((var == "$LIMMIN" || var == "$LIMMAX") && q.code == 10 && i + 2 < n && pairs[i + 2].code == 20) {
                (var == "$LIMMIN" ? doc.limits_min : doc.limits_max) = Vec2{to_d(q.value), to_d(pairs[i + 2].value)};
                i += 3;
                continue;
            } else {
                took = false;
            }
            if (took) {
                i += 2;
                continue;
            }
        }
        if (p.code != 0) {
            ++i;
            continue;
        }
        if (p.value == "SECTION") {
            saw_section = true;
            const bool has_name = i + 1 < n && pairs[i + 1].code == 2;
            section = has_name ? pairs[i + 1].value : std::string();
            i += has_name ? std::size_t{2} : std::size_t{1};
            continue;
        }
        if (p.value == "ENDSEC") {
            section.clear();
            ++i;
            continue;
        }
        if (p.value == "EOF") {
            break;
        }
        if (section == "TABLES" && p.value == "VPORT") {
            // *Active entries are the model window's tiles (the whole window when one).
            std::vector<Pair> body;
            ++i;
            while (i < n && pairs[i].code != 0) {
                body.push_back(pairs[i]);
                ++i;
            }
            const std::string* name = find(body, 2);
            if (name != nullptr && upper_hex(*name) == "*ACTIVE") {
                TiledViewport t;
                t.x0 = getd(body, 10, 0.0);
                t.y0 = getd(body, 20, 0.0);
                t.x1 = getd(body, 11, 1.0);
                t.y1 = getd(body, 21, 1.0);
                t.center = {getd(body, 12), getd(body, 22)};
                t.height = getd(body, 40, 0.0);
                doc.vports.push_back(t);
            }
            continue;
        }
        if (section == "TABLES" && p.value == "BLOCK_RECORD") {
            // A block's insertion units (70), matched to its definition by name below, and its
            // handle, by which a multileader names its block.
            std::string name;
            std::string handle;
            long units = 0;
            ++i;
            while (i < n && pairs[i].code != 0) {
                if (pairs[i].code == 2) {
                    name = pairs[i].value;
                } else if (pairs[i].code == 70) {
                    units = to_l(pairs[i].value);
                } else if (pairs[i].code == 5 && handle.empty()) {
                    handle = pairs[i].value;
                }
                ++i;
            }
            if (!name.empty() && !handle.empty()) {
                block_record_names[upper_hex(handle)] = name;
            }
            if (!name.empty() && units > 0 && units <= 24) {
                block_units[name] = static_cast<std::uint8_t>(units);
            }
            continue;
        }
        if (section == "TABLES" &&
            (p.value == "LAYER" || p.value == "DIMSTYLE" || p.value == "STYLE")) {
            const std::string kind = p.value;
            std::vector<Pair> body;
            ++i;
            while (i < n && pairs[i].code != 0) {
                body.push_back(pairs[i]);
                ++i;
            }
            if (kind == "LAYER") {
                add_layer_record(body);
            } else if (kind == "STYLE") {
                add_style_record(body);
            } else {
                add_dimstyle_record(body);
            }
            continue;
        }
        if (section == "OBJECTS") {
            const std::string type = p.value;
            std::vector<Pair> body;
            ++i;
            while (i < n && pairs[i].code != 0) {
                body.push_back(pairs[i]);
                ++i;
            }
            if (type == "DIMASSOC") {
                // The dimension (the 330 after the subclass marker) and which of its points are
                // on objects (90); none means it is not associated.
                bool sub = false;
                const std::string* dim = nullptr;
                long flags = 0;
                for (const Pair& q : body) {
                    if (q.code == 100) {
                        sub = true;
                    } else if (q.code == 330 && sub && dim == nullptr) {
                        dim = &q.value;
                    } else if (q.code == 90) {
                        flags = to_l(q.value);
                    }
                }
                if (dim != nullptr && flags != 0) {
                    associated.insert(upper_hex(*dim));
                }
                continue;
            }
            if (type == "MLINESTYLE") {
                // Its name, flags, description, fill colour and end angles, then per element an
                // offset (49), a colour (62 / 420) and a linetype (6).
                MLineStyle ms;
                ms.elements.clear();
                bool elements = false;
                const auto set_color = [&](std::uint8_t& mode, Rgb& c, long aci) {
                    mode = static_cast<std::uint8_t>(aci == 256 ? 1 : aci == 0 ? 2 : 0);
                    if (mode == 0) {
                        c = aci_to_rgb(std::labs(aci) > 255 ? 7 : std::labs(aci));
                    }
                };
                for (const Pair& q : body) {
                    switch (q.code) {
                    case 2:
                        ms.name = q.value;
                        break;
                    case 70:
                        ms.flags = static_cast<std::uint16_t>(to_l(q.value));
                        break;
                    case 3:
                        ms.description = q.value;
                        break;
                    case 51:
                        ms.start_angle = to_d(q.value);
                        break;
                    case 52:
                        ms.end_angle = to_d(q.value);
                        break;
                    case 71:
                        elements = true;
                        break;
                    case 49:
                        ms.elements.push_back(MLineElement{to_d(q.value)});
                        break;
                    case 62:
                        if (elements && !ms.elements.empty()) {
                            set_color(ms.elements.back().color_mode, ms.elements.back().color, to_l(q.value));
                        } else if (!elements) {
                            set_color(ms.fill_mode, ms.fill_color, to_l(q.value));
                        }
                        break;
                    case 420:
                        if (elements && !ms.elements.empty()) {
                            ms.elements.back().color_mode = 0;
                            ms.elements.back().color = from_true_color(to_l(q.value));
                        } else if (!elements) {
                            ms.fill_mode = 0;
                            ms.fill_color = from_true_color(to_l(q.value));
                        }
                        break;
                    case 6:
                        if (!ms.elements.empty()) {
                            std::string u = q.value;
                            for (char& ch : u) {
                                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                            }
                            MLineElement& e = ms.elements.back();
                            e.linetype_mode = static_cast<std::uint8_t>(u == "BYLAYER" ? 1 : u == "BYBLOCK" ? 2 : 0);
                            e.linetype = e.linetype_mode == 0 ? linetype_from(q.value) : Linetype::Continuous;
                        }
                        break;
                    default:
                        break;
                    }
                }
                if (!ms.name.empty()) {
                    mline_styles_read.push_back(std::move(ms));
                }
            }
            if (type == "WIPEOUTVARIABLES") { // WIPEOUTFRAME: 0 hidden, 1 shown, 2 not plotted
                const std::string* frame = find(body, 70);
                const long f = frame != nullptr ? to_l(*frame) : 1L;
                doc.wipeout_frames = f != 0;
                doc.wipeout_frames_plot = f != 2;
            }
            if (type == "IMAGEDEF") {
                if (const std::string* h = find(body, 5)) {
                    ImageDefRecord rec;
                    if (const std::string* p1 = find(body, 1)) {
                        rec.path = *p1;
                    }
                    rec.pixel_w = getd(body, 10, 0.0);
                    rec.pixel_h = getd(body, 20, 0.0);
                    imagedefs[*h] = rec;
                }
            }
            continue;
        }
        if (section == "ENTITIES") {
            const std::string type = p.value;
            std::vector<Pair> body;
            ++i;
            while (i < n && pairs[i].code != 0) {
                body.push_back(pairs[i]);
                ++i;
            }
            if (!legacy_polyline(model_sink, type, body)) {
                build_entity(model_sink, type, body);
            }
            continue;
        }
        if (section == "BLOCKS") {
            const std::string type = p.value;
            std::vector<Pair> body;
            ++i;
            while (i < n && pairs[i].code != 0) {
                body.push_back(pairs[i]);
                ++i;
            }
            if (type == "BLOCK") {
                block = DocBlockDef{};
                if (const std::string* nm = find(body, 2)) {
                    block.name = *nm;
                }
                block.base = {getd(body, 10), getd(body, 20)};
                if (const std::string* f = find(body, 70); f != nullptr && (to_l(*f) & 4) != 0) {
                    if (const std::string* p1 = find(body, 1)) {
                        block.xref_path = *p1;
                    }
                }
                // A dimension's drawn form: Musa CAD draws dimensions from their own points,
                // so it stays out unless a dimension inside a block needs it. The two spaces'
                // blocks are not blocks to insert (their entities are in ENTITIES).
                in_block = !is_space_block(block.name) &&
                           (!is_dimension_block(block.name) || block_dimensions.contains(block.name));
            } else if (type == "ENDBLK") {
                if (in_block) {
                    doc.block_defs.push_back(std::move(block));
                    block = DocBlockDef{};
                    in_block = false;
                }
            } else if (in_block) {
                if (!legacy_polyline(block_sink, type, body)) {
                    build_entity(block_sink, type, body); // entities accumulate into the block
                }
            }
            continue;
        }
        ++i;
    }

    if (!saw_section) {
        return IoResult::failure("Not a DXF file (no SECTION found).");
    }

    // A dimension inside a block whose drawn form the file does not carry: nothing to place.
    std::set<std::string> defined_blocks;
    for (const DocBlockDef& def : doc.block_defs) {
        defined_blocks.insert(def.name);
    }
    for (DocBlockDef& def : doc.block_defs) {
        std::erase_if(def.inserts, [&](const DocInsert& ins) {
            return is_dimension_block(ins.block_name) && !defined_blocks.contains(ins.block_name);
        });
    }

    // The multiline styles (STANDARD first, as the drawing always has it), then the multilines
    // in them: the path, scale, justification and flags, and per vertex and element the gaps
    // (each element's parameters after the first, which is where it starts along the mitre).
    if (!mline_styles_read.empty()) {
        const auto upper = [](std::string v) {
            for (char& ch : v) {
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            return v;
        };
        doc.mline_styles.assign(1, MLineStyle{});
        for (MLineStyle& ms : mline_styles_read) {
            if (upper(ms.name) == "STANDARD") {
                doc.mline_styles[0] = ms;
            } else {
                doc.mline_styles.push_back(ms);
            }
        }
    }
    const auto mline_style_named = [&](const std::string& name) -> std::uint16_t {
        for (std::size_t k = 0; k < doc.mline_styles.size(); ++k) {
            const std::string& sn = doc.mline_styles[k].name;
            if (sn.size() == name.size() && std::equal(sn.begin(), sn.end(), name.begin(), [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                })) {
                return static_cast<std::uint16_t>(k);
            }
        }
        return 0;
    };
    if (!current_mlstyle_name.empty()) {
        doc.current_mline_style = mline_style_named(current_mlstyle_name);
    }
    for (const std::vector<Pair>& body : mline_bodies) {
        DocMLine m;
        m.props = props_of(body);
        if (const std::string* sn = find(body, 2)) {
            m.style = mline_style_named(*sn);
        }
        m.scale = getd(body, 40, 1.0);
        if (const std::string* j = find(body, 70)) {
            m.justification = static_cast<std::uint8_t>(std::clamp(to_l(*j), 0L, 2L));
        }
        if (const std::string* f = find(body, 71)) {
            const long flags = to_l(*f);
            m.closed = (flags & 2) != 0;
            m.no_start_caps = (flags & 4) != 0;
            m.no_end_caps = (flags & 8) != 0;
        }
        long element = -1;
        long params_left = 0;
        bool first_param = false;
        for (const Pair& q : body) {
            if (q.code == 11) {
                m.vertices.push_back({to_d(q.value), 0.0});
                element = -1;
            } else if (q.code == 21 && !m.vertices.empty()) {
                m.vertices.back().y = to_d(q.value);
            } else if (q.code == 74 && !m.vertices.empty()) {
                ++element;
                params_left = to_l(q.value);
                first_param = true;
            } else if (q.code == 41 && params_left > 0 && element >= 0) {
                --params_left;
                if (first_param) {
                    first_param = false; // where the element starts along the mitre
                    continue;
                }
                const std::size_t seg = m.vertices.size() - 1;
                if (m.breaks.size() <= seg) {
                    m.breaks.resize(seg + 1);
                }
                if (m.breaks[seg].size() <= static_cast<std::size_t>(element)) {
                    m.breaks[seg].resize(static_cast<std::size_t>(element) + 1);
                }
                m.breaks[seg][static_cast<std::size_t>(element)].push_back(to_d(q.value));
            }
        }
        if (m.vertices.size() >= 2) {
            doc.mlines.push_back(std::move(m));
        } else {
            ++skipped["MLINE"];
        }
    }

    // IMAGE entities: one definition per distinct IMAGEDEF (by handle); the placement
    // from the U/V vectors (a pixel each) and the pixel size; the clip boundary back
    // from pixel coordinates to image fractions.
    std::map<std::string, std::uint16_t> def_index;
    for (const std::vector<Pair>& body : image_bodies) {
        const std::string* ref = find(body, 340);
        if (ref == nullptr) {
            ++skipped["IMAGE"];
            continue;
        }
        const auto it = imagedefs.find(*ref);
        if (it == imagedefs.end()) {
            ++skipped["IMAGE"];
            continue;
        }
        std::uint16_t di = 0;
        if (const auto known = def_index.find(*ref); known != def_index.end()) {
            di = known->second;
        } else {
            DocImageDef d;
            d.source = it->second.path;
            d.pixel_w = static_cast<std::uint32_t>(std::max(0.0, it->second.pixel_w));
            d.pixel_h = static_cast<std::uint32_t>(std::max(0.0, it->second.pixel_h));
            doc.image_defs.push_back(d);
            di = static_cast<std::uint16_t>(doc.image_defs.size() - 1);
            def_index[*ref] = di;
        }
        const double pw = getd(body, 13, it->second.pixel_w > 0.0 ? it->second.pixel_w : 1.0);
        const double ph = getd(body, 23, it->second.pixel_h > 0.0 ? it->second.pixel_h : 1.0);
        const Vec2 u{getd(body, 11, 1.0), getd(body, 21, 0.0)};
        const Vec2 v{getd(body, 12, 0.0), getd(body, 22, 1.0)};
        DocImage im;
        im.def = di;
        im.props = props_of(body);
        im.pos = {getd(body, 10), getd(body, 20)};
        im.width = std::hypot(u.x, u.y) * pw;
        im.height = std::hypot(v.x, v.y) * ph;
        im.rotation = std::atan2(u.y, u.x);
        long flags = 1;
        if (const std::string* f = find(body, 70)) {
            flags = to_l(*f);
        }
        long clipping = 0;
        if (const std::string* c = find(body, 280)) {
            clipping = to_l(*c);
        }
        std::vector<Vec2> verts;
        std::optional<double> vx;
        for (const Pair& pr : body) {
            if (pr.code == 14) {
                vx = to_d(pr.value);
            } else if (pr.code == 24 && vx) {
                verts.push_back({*vx / (pw > 0.0 ? pw : 1.0), to_d(pr.value) / (ph > 0.0 ? ph : 1.0)});
                vx.reset();
            }
        }
        long clip_type = 1;
        if (const std::string* ct = find(body, 71)) {
            clip_type = to_l(*ct);
        }
        if ((flags & 4) != 0 && clipping != 0 && !verts.empty()) {
            im.clipped = true;
            if (clip_type == 2 && verts.size() >= 3) {
                if (verts.size() > 3 && std::abs(verts.front().x - verts.back().x) < 1e-9 &&
                    std::abs(verts.front().y - verts.back().y) < 1e-9) {
                    verts.pop_back(); // the closing repeat
                }
                im.clip_polygon = verts;
                double u0 = 1.0, v0 = 1.0, u1 = 0.0, v1 = 0.0;
                for (const Vec2& q : verts) {
                    u0 = std::min(u0, q.x);
                    v0 = std::min(v0, q.y);
                    u1 = std::max(u1, q.x);
                    v1 = std::max(v1, q.y);
                }
                im.clip_u0 = u0;
                im.clip_v0 = v0;
                im.clip_u1 = u1;
                im.clip_v1 = v1;
            } else if (verts.size() >= 2) {
                im.clip_u0 = std::min(verts[0].x, verts[1].x);
                im.clip_v0 = std::min(verts[0].y, verts[1].y);
                im.clip_u1 = std::max(verts[0].x, verts[1].x);
                im.clip_v1 = std::max(verts[0].y, verts[1].y);
            }
        }
        doc.images.push_back(std::move(im));
    }
    if (dimassoc_var) {
        for (const auto& [h, k] : dim_by_handle) {
            if (k < doc.dims.size() && !associated.contains(h)) {
                doc.dims[k].unassoc = true;
            }
        }
    }
    for (std::size_t k = 0; k < doc.dimstyles.size(); ++k) {
        if (!current_dimstyle_name.empty() && doc.dimstyles[k].name == current_dimstyle_name) {
            doc.current_dimstyle = static_cast<std::uint16_t>(k);
        }
    }
    for (DocBlockDef& b : doc.block_defs) {
        if (const auto it = block_units.find(b.name); it != block_units.end()) {
            b.units = it->second;
        }
    }
    std::string msg = "Imported " + std::to_string(doc.entity_count()) + " entities on " +
                      std::to_string(doc.layers.size()) + " layers";
    if (!doc.block_defs.empty()) {
        msg += " (" + std::to_string(doc.inserts.size()) + " block refs over " +
               std::to_string(doc.block_defs.size()) + " definitions)";
    }
    if (!skipped.empty()) {
        int total = 0;
        std::string names;
        for (const auto& [name, count] : skipped) {
            total += count;
            if (!names.empty()) {
                names += ", ";
            }
            names += name;
        }
        msg += "; skipped " + std::to_string(total) + " unsupported (" + names + ")";
    }
    msg += ".";
    out = std::move(doc);
    return IoResult::success(msg);
}

IoResult save_dxf(const Document& doc, const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        return IoResult::failure("Cannot write file: " + path);
    }
    // DXF has no embedded rasters: an embedded image is written next to the file as
    // <stem>-imageN.<png|jpg> and referenced by that name. One that cannot be written (a
    // read-only folder) is still referenced by that name, and the result says it is missing.
    Document copy = doc;
    namespace fs = std::filesystem;
    const fs::path out_path(path);
    std::vector<std::string> unwritten;
    for (std::size_t i = 0; i < copy.image_defs.size(); ++i) {
        DocImageDef& d = copy.image_defs[i];
        if (d.bytes.empty()) {
            continue;
        }
        const bool jpeg = d.bytes.size() > 2 && d.bytes[0] == 0xFF && d.bytes[1] == 0xD8;
        const std::string name = out_path.stem().string() + "-image" + std::to_string(i + 1) +
                                 (jpeg ? ".jpg" : ".png");
        const fs::path side_path = out_path.parent_path() / name;
        std::ofstream side(side_path, std::ios::binary | std::ios::trunc);
        const bool opened = side.is_open();
        side.write(reinterpret_cast<const char*>(d.bytes.data()),
                   static_cast<std::streamsize>(d.bytes.size()));
        side.close();
        if (!side) {
            if (opened) {
                std::error_code ec;
                fs::remove(side_path, ec); // no truncated image left behind
            }
            unwritten.push_back(name);
        }
        d.source = name;
        d.bytes.clear();
    }
    const std::string text = serialize_dxf(copy);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f) {
        return IoResult::failure("Write failed: " + path);
    }
    std::string msg = "Exported " + std::to_string(doc.entity_count()) + " entities to DXF";
    if (!unwritten.empty()) {
        msg += "; " + std::to_string(unwritten.size()) +
               (unwritten.size() == 1 ? " embedded image" : " embedded images") +
               " could not be written beside it (";
        for (std::size_t i = 0; i < unwritten.size(); ++i) {
            msg += (i == 0 ? "" : ", ") + unwritten[i];
        }
        msg += "), so the DXF refers to missing files";
    }
    return IoResult::success(msg + ".");
}

IoResult load_dxf(const std::string& path, Document& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return IoResult::failure("Cannot open file: " + path);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    const IoResult r = parse_dxf(ss.str(), out);
    // An image referenced by an absolute path, or by one outside the drawing's folder,
    // cannot be a relative reference: read it in as an embedded payload instead.
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::path(path).parent_path();
    for (DocImageDef& d : out.image_defs) {
        if (d.source.empty() || !d.bytes.empty()) {
            continue;
        }
        const fs::path src(d.source);
        const fs::path full = src.is_absolute() ? src : dir / src;
        const fs::path rel = fs::weakly_canonical(full, ec).lexically_relative(fs::weakly_canonical(dir, ec));
        const std::string rs = rel.generic_string();
        if (!rs.empty() && rs != "." && rs.rfind("..", 0) != 0) {
            d.source = rs;
            continue;
        }
        const auto size = fs::file_size(full, ec);
        std::ifstream img(full, std::ios::binary);
        if (!ec && size > 0 && img) {
            d.bytes.resize(static_cast<std::size_t>(size));
            if (img.read(reinterpret_cast<char*>(d.bytes.data()), static_cast<std::streamsize>(size))) {
                d.source.clear();
            } else {
                d.bytes.clear();
            }
        }
    }
    return r;
}

Rgb dxf_aci_to_rgb(long aci) { return aci_to_rgb(aci); }

} // namespace musacad::core::io

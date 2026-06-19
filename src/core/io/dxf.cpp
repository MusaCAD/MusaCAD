// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/io/dxf.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
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
void code_i(std::string& s, int c, long v) { code(s, c, std::to_string(v)); }

const char* linetype_name(Linetype t) {
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

void emit_header(std::string& s, const Document& doc) {
    code(s, 0, "SECTION");
    code(s, 2, "HEADER");
    code(s, 9, "$ACADVER");
    code(s, 1, "AC1015"); // AutoCAD R2000 -- widely compatible
    code(s, 9, "$LTSCALE");
    code_d(s, 40, doc.ltscale); // global linetype scale
    code(s, 0, "ENDSEC");
}

// One LTYPE record: name + dash elements (positive = dash, negative = gap), in
// drawing units. Must match core/linetype.cpp so a Musa-dashed entity reads dashed
// in other CAD apps. `elems` empty => Continuous (solid).
void emit_ltype(std::string& s, const char* name, std::initializer_list<double> elems) {
    code(s, 0, "LTYPE");
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

void emit_layer_table(std::string& s, const Document& doc) {
    code(s, 0, "SECTION");
    code(s, 2, "TABLES");
    // LTYPE table first -- layers/entities reference these patterns by name. The
    // element lengths mirror core/linetype.cpp (Dashed/Hidden/Center).
    code(s, 0, "TABLE");
    code(s, 2, "LTYPE");
    code_i(s, 70, 4);
    emit_ltype(s, "Continuous", {});
    emit_ltype(s, "DASHED", {5.0, -2.5});
    emit_ltype(s, "HIDDEN", {2.5, -1.25});
    emit_ltype(s, "CENTER", {12.5, -2.5, 2.5, -2.5});
    code(s, 0, "ENDTAB");
    code(s, 0, "TABLE");
    code(s, 2, "LAYER");
    code_i(s, 70, static_cast<long>(doc.layers.size()));
    for (const Layer& l : doc.layers) {
        code(s, 0, "LAYER");
        code(s, 2, l.name);
        code_i(s, 70, (l.frozen ? 1 : 0) | (l.locked ? 4 : 0)); // 1=frozen, 4=locked
        code_i(s, 62, l.on ? 7 : -7); // ACI; negative = layer off
        code_i(s, 420, true_color(l.color)); // exact RGB
        code(s, 6, linetype_name(l.linetype));
        code_i(s, 370, l.lineweight);
    }
    code(s, 0, "ENDTAB");
    // DIMSTYLE table (minimal: name + the formatting Musa models).
    code(s, 0, "TABLE");
    code(s, 2, "DIMSTYLE");
    code_i(s, 70, static_cast<long>(doc.dimstyles.size()));
    for (const DimStyle& ds : doc.dimstyles) {
        code(s, 0, "DIMSTYLE");
        code(s, 2, ds.name);
        code_d(s, 140, ds.text_height); // DIMTXT
        code_d(s, 41, ds.arrow_size);   // DIMASZ
        code_i(s, 271, ds.precision);   // DIMDEC
    }
    code(s, 0, "ENDTAB");
    code(s, 0, "ENDSEC");
}

// Per-entity layer/colour/linetype/lineweight (after geometry codes).
void emit_props(std::string& s, const Document& doc, const EntityProps& p) {
    const std::string layer =
        p.layer < doc.layers.size() ? doc.layers[p.layer].name : std::string("0");
    code(s, 8, layer);
    if (p.color_by_layer()) {
        code_i(s, 62, 256); // ByLayer
    } else {
        code_i(s, 420, true_color(p.color));
    }
    if (!p.linetype_by_layer()) {
        code(s, 6, linetype_name(p.linetype));
    }
    if (!p.lineweight_by_layer()) {
        code_i(s, 370, p.lineweight);
    }
}

} // namespace

std::string serialize_dxf(const Document& doc) {
    std::string s;
    emit_header(s, doc);
    emit_layer_table(s, doc);

    code(s, 0, "SECTION");
    code(s, 2, "ENTITIES");

    for (const DocPoint& p : doc.points) {
        code(s, 0, "POINT");
        emit_props(s, doc, p.props);
        code_d(s, 10, p.p.x);
        code_d(s, 20, p.p.y);
        code_d(s, 30, 0.0);
    }
    for (const DocLine& l : doc.lines) {
        code(s, 0, "LINE");
        emit_props(s, doc, l.props);
        if (l.celtscale != 1.0) {
            code_d(s, 48, l.celtscale); // per-entity linetype scale (CELTSCALE)
        }
        code_d(s, 10, l.a.x);
        code_d(s, 20, l.a.y);
        code_d(s, 30, 0.0);
        code_d(s, 11, l.b.x);
        code_d(s, 21, l.b.y);
        code_d(s, 31, 0.0);
    }
    for (const DocCircle& c : doc.circles) {
        code(s, 0, "CIRCLE");
        emit_props(s, doc, c.props);
        if (c.celtscale != 1.0) {
            code_d(s, 48, c.celtscale);
        }
        code_d(s, 10, c.center.x);
        code_d(s, 20, c.center.y);
        code_d(s, 30, 0.0);
        code_d(s, 40, c.radius);
    }
    for (const DocArc& a : doc.arcs) {
        code(s, 0, "ARC");
        emit_props(s, doc, a.props);
        if (a.celtscale != 1.0) {
            code_d(s, 48, a.celtscale);
        }
        code_d(s, 10, a.center.x);
        code_d(s, 20, a.center.y);
        code_d(s, 30, 0.0);
        code_d(s, 40, a.radius);
        code_d(s, 50, to_degrees(a.start_angle));
        code_d(s, 51, to_degrees(a.end_angle));
    }
    for (const DocPolyline& p : doc.polylines) {
        code(s, 0, "LWPOLYLINE");
        emit_props(s, doc, p.props);
        if (p.celtscale != 1.0) {
            code_d(s, 48, p.celtscale);
        }
        code_i(s, 90, static_cast<long>(p.points.size()));
        code_i(s, 70, p.closed ? 1 : 0);
        const bool has_bulge = p.bulges.size() == p.points.size();
        for (std::size_t i = 0; i < p.points.size(); ++i) {
            code_d(s, 10, p.points[i].x);
            code_d(s, 20, p.points[i].y);
            // Code 42 is the vertex bulge; emit only non-zero ones (DXF convention).
            if (has_bulge && p.bulges[i] != 0.0) {
                code_d(s, 42, p.bulges[i]);
            }
        }
    }
    for (const DocText& t : doc.texts) {
        code(s, 0, "TEXT");
        emit_props(s, doc, t.props);
        code_d(s, 10, t.pos.x);
        code_d(s, 20, t.pos.y);
        code_d(s, 30, 0.0);
        code_d(s, 40, t.height);
        code(s, 1, t.content);
        code_d(s, 50, to_degrees(t.rotation));
        code_i(s, 72, t.justify); // 0 left, 1 centre, 2 right
        if (!t.font.empty()) {
            code(s, 7, t.font); // style name = font (re-imported via the code-7 fallback)
        }
    }
    for (const DocDim& d : doc.dims) {
        const std::string style =
            d.style < doc.dimstyles.size() ? doc.dimstyles[d.style].name : std::string("Standard");
        code(s, 0, "DIMENSION");
        emit_props(s, doc, d.props);
        code(s, 2, "*D0"); // anonymous block name (graphics regenerated by readers)
        code(s, 3, style);
        // 10 = dimension-line location; 13/14 = extension-line def points (a, b).
        code_d(s, 10, d.line_pt.x);
        code_d(s, 20, d.line_pt.y);
        code_d(s, 30, 0.0);
        code_d(s, 11, d.line_pt.x);
        code_d(s, 21, d.line_pt.y);
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
        case DimType::Linear:
            dimtype = 0;
            break;
        }
        code_i(s, 70, dimtype);
        code_d(s, 13, d.a.x);
        code_d(s, 23, d.a.y);
        code_d(s, 33, 0.0);
        code_d(s, 14, d.b.x);
        code_d(s, 24, d.b.y);
        code_d(s, 34, 0.0);
        code_d(s, 15, d.line_pt.x); // radius/diameter "edge" def point reuse
        code_d(s, 25, d.line_pt.y);
        code_d(s, 35, 0.0);
    }
    // Leaders: exported as a DXF LEADER (vertices) plus a TEXT label so any reader
    // shows them. Import reconstructs them as line + text (see docs); Musa keeps
    // leaders as first-class entities via the native format.
    for (const DocLeader& l : doc.leaders) {
        code(s, 0, "LEADER");
        emit_props(s, doc, l.props);
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
            code_d(s, 10, l.knee.x);
            code_d(s, 20, l.knee.y);
            code_d(s, 30, 0.0);
            code_d(s, 40, l.text_height);
            code(s, 1, l.content);
        }
    }
    // MTEXT: standard group codes (read by AutoCAD/LibreCAD).
    const auto emit_mtext = [&](const MTextBlock& b, const std::string& content,
                                const EntityProps& props, const std::string& font = {}) {
        code(s, 0, "MTEXT");
        emit_props(s, doc, props);
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
        if (m.vertices.size() >= 2) {
            code(s, 0, "LEADER");
            emit_props(s, doc, m.props);
            code_i(s, 76, static_cast<long>(m.vertices.size()));
            for (const Vec2& v : m.vertices) {
                code_d(s, 10, v.x);
                code_d(s, 20, v.y);
                code_d(s, 30, 0.0);
            }
        }
        if (m.block.str_len != 0 || !m.content.empty()) {
            emit_mtext(m.block, m.content, m.props);
        }
    }
    // Model-space block references.
    const auto emit_insert = [&](const DocInsert& in) {
        code(s, 0, "INSERT");
        emit_props(s, doc, in.props);
        code(s, 2, in.block_name);
        code_d(s, 10, in.pos.x);
        code_d(s, 20, in.pos.y);
        code_d(s, 30, 0.0);
        code_d(s, 41, in.scale_x);
        code_d(s, 42, in.scale_y);
        code_d(s, 43, 1.0);
        code_d(s, 50, to_degrees(in.rotation));
    };
    for (const DocInsert& in : doc.inserts) {
        emit_insert(in);
    }
    code(s, 0, "ENDSEC");

    // BLOCKS section: each definition's geometry between BLOCK/ENDBLK. (Emitted after
    // ENTITIES; Musa's importer is section-order-agnostic. Strict BLOCKS-before-ENTITIES
    // ordering for other readers is a noted minor fidelity item.)
    if (!doc.block_defs.empty()) {
        code(s, 0, "SECTION");
        code(s, 2, "BLOCKS");
        for (const DocBlockDef& b : doc.block_defs) {
            code(s, 0, "BLOCK");
            code(s, 8, "0");
            code(s, 2, b.name);
            code_i(s, 70, 0);
            code_d(s, 10, b.base.x);
            code_d(s, 20, b.base.y);
            code_d(s, 30, 0.0);
            code(s, 3, b.name);
            for (const DocLine& l : b.lines) {
                code(s, 0, "LINE");
                emit_props(s, doc, l.props);
                code_d(s, 10, l.a.x);
                code_d(s, 20, l.a.y);
                code_d(s, 11, l.b.x);
                code_d(s, 21, l.b.y);
            }
            for (const DocCircle& c : b.circles) {
                code(s, 0, "CIRCLE");
                emit_props(s, doc, c.props);
                code_d(s, 10, c.center.x);
                code_d(s, 20, c.center.y);
                code_d(s, 40, c.radius);
            }
            for (const DocArc& a : b.arcs) {
                code(s, 0, "ARC");
                emit_props(s, doc, a.props);
                code_d(s, 10, a.center.x);
                code_d(s, 20, a.center.y);
                code_d(s, 40, a.radius);
                code_d(s, 50, to_degrees(a.start_angle));
                code_d(s, 51, to_degrees(a.end_angle));
            }
            for (const DocPolyline& p : b.polylines) {
                code(s, 0, "LWPOLYLINE");
                emit_props(s, doc, p.props);
                code_i(s, 90, static_cast<long>(p.points.size()));
                code_i(s, 70, p.closed ? 1 : 0);
                const bool hb = p.bulges.size() == p.points.size();
                for (std::size_t i = 0; i < p.points.size(); ++i) {
                    code_d(s, 10, p.points[i].x);
                    code_d(s, 20, p.points[i].y);
                    if (hb && p.bulges[i] != 0.0) {
                        code_d(s, 42, p.bulges[i]);
                    }
                }
            }
            for (const DocText& t : b.texts) {
                code(s, 0, "TEXT");
                emit_props(s, doc, t.props);
                code_d(s, 10, t.pos.x);
                code_d(s, 20, t.pos.y);
                code_d(s, 40, t.height);
                code(s, 1, t.content);
                code_d(s, 50, to_degrees(t.rotation));
                code_i(s, 72, t.justify);
            }
            for (const DocMText& m : b.mtexts) {
                emit_mtext(m.block, m.content, m.props);
            }
            for (const DocInsert& in : b.inserts) {
                emit_insert(in); // nested block reference
            }
            code(s, 0, "ENDBLK");
            code(s, 8, "0");
        }
        code(s, 0, "ENDSEC");
    }
    code(s, 0, "EOF");
    return s;
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
    std::from_chars(t.data() + a, t.data() + t.size(), v);
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
std::string strip_mtext(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '{' || c == '}') {
            continue; // grouping delimiters -- formatting scope, no glyphs
        }
        if (c == '^' && i + 1 < in.size()) {
            // Caret notation for control chars: ^I = tab (used to indent list items after
            // \P), ^J/^M = lf/cr. Without this the bare 'I' leaks in as "\P^I 2." -> "I 2.".
            const char n2 = in[i + 1];
            if (n2 == 'I') {
                out += ' '; // tab -> space (keeps the indent, drops the stray glyph)
                ++i;
                continue;
            }
            if (n2 == 'J' || n2 == 'M') {
                ++i; // line feed / carriage return -- \P already drives breaks
                continue;
            }
            // any other ^x: a literal caret, emitted by the normal path below
        }
        if (c != '\\' || i + 1 >= in.size()) {
            out += c;
            continue;
        }
        const char n = in[i + 1];
        switch (n) {
        case 'P':
            out += '\n'; // hard paragraph break (note: \P upper; \p... is a format run)
            ++i;
            break;
        case '~':
            out += ' '; // non-breaking space
            ++i;
            break;
        case '\\':
        case '{':
        case '}':
            out += n; // escaped literal
            ++i;
            break;
        case 'L':
        case 'l':
        case 'O':
        case 'o':
        case 'K':
        case 'k':
            i += 1; // underline/overline/strike toggles: no argument
            break;
        case 'f':
        case 'F':
        case 'C':
        case 'c':
        case 'H':
        case 'W':
        case 'T':
        case 'Q':
        case 'A':
        case 'p':
        case 'S': {
            // A formatting run with an argument terminated by ';' (or, for \S stacking,
            // the fraction body). Skip through the ';'. For \S keep the operands so a
            // fraction like 1^2; / 1/2; still reads as text.
            const std::size_t semi = in.find(';', i + 2);
            const std::size_t end = (semi == std::string::npos) ? in.size() : semi;
            if (n == 'S') {
                for (std::size_t k = i + 2; k < end; ++k) {
                    const char s = in[k];
                    out += (s == '^' || s == '#') ? '/' : s;
                }
            }
            i = end; // loop ++i steps past the ';'
            break;
        }
        default:
            out += n; // unknown escape: keep the char, drop the backslash
            ++i;
            break;
        }
    }
    return out;
}

// Decode single-line TEXT (DTEXT) overrides: %%c -> diameter, %%d -> degree, %%p -> +/-,
// %%%/%%% -> percent, and strip the %%u/%%o under/overline toggles.
std::string decode_dtext(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() && in[i + 1] == '%') {
            const char k = static_cast<char>(std::tolower(static_cast<unsigned char>(in[i + 2])));
            switch (k) {
            case 'c':
                out += "Ø"; // diameter
                i += 2;
                continue;
            case 'd':
                out += "°"; // degree
                i += 2;
                continue;
            case 'p':
                out += "±"; // plus/minus
                i += 2;
                continue;
            case 'u':
            case 'o':
                i += 2; // under/overline toggle: strip
                continue;
            default:
                break;
            }
        }
        out += in[i];
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
    std::map<std::string, std::uint16_t> layer_index{{"0", 0}};
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
    std::map<std::string, std::uint16_t> style_index{{"Standard", 0}};
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

    std::map<std::string, int> skipped;
    bool saw_section = false;
    std::string section;
    std::size_t i = 0;
    const std::size_t n = pairs.size();

    // STYLE table: text-style name (code 2) -> font file (code 3, e.g. "arial.ttf" or
    // "txt.shx"). TEXT/MTEXT reference a style via code 7; we resolve it to the font name
    // and the font engine substitutes SHX names to TTF lookalikes at render time.
    std::map<std::string, std::string> styles;
    const auto add_style_record = [&](const std::vector<Pair>& body) {
        const std::string* name = find(body, 2);
        const std::string* font = find(body, 3);
        if (name != nullptr && font != nullptr && !font->empty()) {
            styles[*name] = *font;
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

    const auto add_layer_record = [&](const std::vector<Pair>& body) {
        const std::string* name = find(body, 2);
        if (name == nullptr) {
            return;
        }
        const std::uint16_t idx = ensure_layer(*name);
        Layer& l = doc.layers[idx];
        if (const std::string* flag = find(body, 70)) {
            const long f = to_l(*flag);
            l.frozen = (f & 1) != 0;
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
        if (const std::string* lw = find(body, 370)) {
            // Only 0..211 are real widths; -1/-2/-3 (ByLayer/ByBlock/Default) and any
            // out-of-range value mean "inherit", not a literal 2.5mm line. A layer must
            // hold a concrete width, so keep its default when the code isn't a real one.
            if (const long w = to_l(*lw); w >= 0 && w <= 211) {
                l.lineweight = static_cast<std::uint8_t>(w);
            }
        }
    };

    const auto props_of = [&](const std::vector<Pair>& body) {
        EntityProps p;
        const std::string* layer = find(body, 8);
        p.layer = ensure_layer(layer != nullptr ? *layer : std::string("0"));
        if (const std::string* tc = find(body, 420)) {
            p.set_color_by_layer(false);
            p.color = from_true_color(to_l(*tc));
        } else if (const std::string* aci = find(body, 62)) {
            // 0=ByBlock, 256=ByLayer both inherit; 1..255 is a concrete colour override.
            if (const long v = to_l(*aci); v >= 1 && v <= 255) {
                p.set_color_by_layer(false);
                p.color = aci_to_rgb(v);
            }
        }
        if (const std::string* lt = find(body, 6)) {
            p.set_linetype_by_layer(false);
            p.linetype = linetype_from(*lt);
        }
        if (const std::string* lw = find(body, 370)) {
            // 0..211 = an explicit width; -1/-2/-3 (ByLayer/ByBlock/Default) or anything
            // out of range means "inherit", so leave the ByLayer default rather than
            // casting -2 to 254 (a 2.54mm line) and fattening the whole import.
            if (const long w = to_l(*lw); w >= 0 && w <= 211) {
                p.set_lineweight_by_layer(false);
                p.lineweight = static_cast<std::uint8_t>(w);
            }
        }
        return p;
    };

    const auto add_dimstyle_record = [&](const std::vector<Pair>& body) {
        const std::string* name = find(body, 2);
        if (name == nullptr) {
            return;
        }
        DimStyle& ds = doc.dimstyles[ensure_dimstyle(*name)];
        if (const std::string* v = find(body, 140)) {
            ds.text_height = to_d(*v);
        }
        if (const std::string* v = find(body, 41)) {
            ds.arrow_size = to_d(*v);
        }
        if (const std::string* v = find(body, 271)) {
            ds.precision = static_cast<std::uint8_t>(to_l(*v));
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
    };

    const auto build_entity = [&](Sink& sink, const std::string& type,
                                  const std::vector<Pair>& body) {
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
            m.content = strip_mtext(raw);
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
                t.content = decode_dtext(*c);
            }
            if (const std::string* j = find(body, 72)) {
                t.justify = static_cast<std::uint8_t>(to_l(*j));
            }
            t.font = font_of_entity(body);
            sink.texts->push_back(std::move(t));
            return;
        }
        if (type == "DIMENSION") {
            if (sink.dims == nullptr) {
                ++skipped[type];
                return;
            }
            const std::string* flag_v = find(body, 70);
            const long flag = flag_v != nullptr ? (to_l(*flag_v) & 7) : 0;
            DocDim d;
            d.props = props_of(body);
            d.a = {getd(body, 13), getd(body, 23)};
            d.b = {getd(body, 14), getd(body, 24)};
            d.line_pt = {getd(body, 10), getd(body, 20)};
            DimType dt = DimType::Linear;
            switch (flag) {
            case 1:
                dt = DimType::Aligned;
                break;
            case 3:
                dt = DimType::Diameter;
                break;
            case 4:
                dt = DimType::Radius;
                break;
            case 5:
                dt = DimType::Angular;
                break;
            default:
                dt = DimType::Linear;
                break;
            }
            // Radius/diameter store the edge point in line_pt (code 15) on export.
            if ((dt == DimType::Radius || dt == DimType::Diameter) && find(body, 15) != nullptr) {
                d.line_pt = {getd(body, 15), getd(body, 25)};
            }
            d.type = static_cast<std::uint8_t>(dt);
            if (const std::string* st = find(body, 3)) {
                d.style = ensure_dimstyle(*st);
            }
            sink.dims->push_back(std::move(d));
            return;
        }
        if (type == "LEADER") {
            if (sink.leaders == nullptr) {
                ++skipped[type];
                return;
            }
            // Reconstruct from the two vertices; the label is the following TEXT.
            DocLeader l;
            l.props = props_of(body);
            l.tip = {getd(body, 10), getd(body, 20)};
            // The last 10/20 pair is the landing point; getd returns the first, so
            // scan for a second occurrence.
            bool first = true;
            for (const Pair& pr : body) {
                if (pr.code == 10) {
                    if (first) {
                        first = false;
                    } else {
                        l.knee.x = to_d(pr.value);
                    }
                } else if (pr.code == 20 && !first) {
                    l.knee.y = to_d(pr.value);
                }
            }
            if (const std::string* st = find(body, 3)) {
                l.style = ensure_dimstyle(*st);
            }
            sink.leaders->push_back(std::move(l));
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
                sink.circles->push_back(DocCircle{{getd(body, 10), getd(body, 20)},
                                                  getd(body, 40),
                                                  props_of(body),
                                                  getd(body, 48, 1.0)});
            } else {
                ++skipped[type];
            }
        } else if (type == "ARC") {
            if (sink.arcs != nullptr) {
                sink.arcs->push_back(DocArc{{getd(body, 10), getd(body, 20)},
                                            getd(body, 40),
                                            to_radians(getd(body, 50)),
                                            to_radians(getd(body, 51)),
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
        } else if (type == "LWPOLYLINE") {
            if (sink.polylines == nullptr) {
                ++skipped[type];
                return;
            }
            DocPolyline pl;
            const std::string* flag = find(body, 70);
            pl.closed = flag != nullptr && (to_l(*flag) & 1) != 0;
            double x = 0.0;
            bool have_x = false;
            bool any_bulge = false;
            for (const Pair& p : body) {
                if (p.code == 10) {
                    x = to_d(p.value);
                    have_x = true;
                } else if (p.code == 20 && have_x) {
                    pl.points.push_back({x, to_d(p.value)});
                    pl.bulges.push_back(0.0); // default straight; a code 42 may follow
                    have_x = false;
                } else if (p.code == 42 && !pl.bulges.empty()) {
                    pl.bulges.back() = to_d(p.value); // bulge of the vertex just read
                    any_bulge = true;
                }
            }
            if (!any_bulge) {
                pl.bulges.clear(); // all straight -> store none
            }
            pl.props = props_of(body);
            pl.celtscale = getd(body, 48, 1.0); // code 48 = CELTSCALE
            sink.polylines->push_back(std::move(pl));
        } else if (type == "SPLINE") {
            // Evaluate the ACTUAL B-spline (control points 10/20 + degree 71 + knots 40)
            // via de Boor and store the tessellated curve as a polyline -- real DXF splines
            // are non-uniform, so a uniform approximation gives wild curves. Falls back to
            // fit points (11/21) if there are no control points.
            if (sink.polylines == nullptr) {
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
            if (ctrl.empty()) { // fall back to fit points (11/21); no usable knots then
                have_x = false;
                knots.clear();
                for (const Pair& p : body) {
                    if (p.code == 11) {
                        x = to_d(p.value);
                        have_x = true;
                    } else if (p.code == 21 && have_x) {
                        ctrl.push_back({x, to_d(p.value)});
                        have_x = false;
                    }
                }
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
            // Musa has no ellipse primitive -- tessellate to a polyline (one path, no
            // phantom connectors). DXF: 10/20 centre, 11/21 major-axis endpoint RELATIVE
            // to centre, 40 minor/major ratio, 41/42 start/end angle (radians).
            if (sink.polylines == nullptr) {
                ++skipped[type];
                return;
            }
            const Vec2 c{getd(body, 10), getd(body, 20)};
            const Vec2 major{getd(body, 11), getd(body, 21)};
            const double ratio = getd(body, 40, 1.0);
            const double a0 = getd(body, 41, 0.0);
            const double a1 = getd(body, 42, 6.283185307179586);
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

    // Model space writes to every doc vector; block content takes the importable subset
    // (dims/leaders/points inside a block route to the skip catalog).
    Sink model_sink{&doc.lines,  &doc.circles, &doc.arcs,   &doc.polylines, &doc.texts,
                    &doc.mtexts, &doc.dims,    &doc.leaders, &doc.points,   &doc.inserts};

    // The block currently being read in the BLOCKS section. Its sink takes the
    // importable subset; dims/leaders/points/nested-INSERT-targets that a block can't
    // hold route to `skipped`. The DocBlockDef is reused across blocks (stable address),
    // so the sink's pointers stay valid as each block is filled then moved into the doc.
    DocBlockDef block;
    Sink block_sink{&block.lines, &block.circles, &block.arcs,    &block.polylines, &block.texts,
                    &block.mtexts, nullptr,        nullptr,        nullptr,          &block.inserts};
    bool in_block = false;

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
        if (section == "ENTITIES") {
            const std::string type = p.value;
            std::vector<Pair> body;
            ++i;
            while (i < n && pairs[i].code != 0) {
                body.push_back(pairs[i]);
                ++i;
            }
            build_entity(model_sink, type, body);
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
                in_block = true;
            } else if (type == "ENDBLK") {
                if (in_block) {
                    doc.block_defs.push_back(std::move(block));
                    block = DocBlockDef{};
                    in_block = false;
                }
            } else if (in_block) {
                build_entity(block_sink, type, body); // entities accumulate into the block
            }
            continue;
        }
        ++i;
    }

    if (!saw_section) {
        return IoResult::failure("Not a DXF file (no SECTION found).");
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
    const std::string text = serialize_dxf(doc);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f) {
        return IoResult::failure("Write failed: " + path);
    }
    return IoResult::success("Exported " + std::to_string(doc.entity_count()) + " entities to DXF.");
}

IoResult load_dxf(const std::string& path, Document& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return IoResult::failure("Cannot open file: " + path);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse_dxf(ss.str(), out);
}

} // namespace musacad::core::io

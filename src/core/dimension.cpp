// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#include "musacad/core/dimension.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace musacad::core {

namespace {

Vec2 dim_direction(const DimData& d) {
    if (d.type == DimType::Aligned) {
        const Vec2 v = d.b - d.a;
        return length_squared(v) > 1e-18 ? normalized(v) : Vec2{1, 0};
    }
    return std::abs(d.b.x - d.a.x) >= std::abs(d.b.y - d.a.y) ? Vec2{1, 0} : Vec2{0, 1};
}
Vec2 foot(Vec2 p, Vec2 line_pt, Vec2 dir) { return line_pt + dir * dot(p - line_pt, dir); }
void seg(std::vector<Vec2>& out, Vec2 a, Vec2 b) {
    out.push_back(a);
    out.push_back(b);
}
void tri(std::vector<Vec2>& out, Vec2 a, Vec2 b, Vec2 c) {
    out.push_back(a);
    out.push_back(b);
    out.push_back(c);
}

} // namespace

void append_arrowhead(std::vector<Vec2>& fills, std::vector<Vec2>& lines, Vec2 tip, Vec2 along,
                      double size, ArrowType type) {
    const Vec2 u = length_squared(along) > 1e-18 ? normalized(along) : Vec2{1, 0};
    const Vec2 perp{-u.y, u.x};
    const Vec2 base = tip + u * size;
    switch (type) {
    case ArrowType::Filled: {
        tri(fills, tip, base + perp * (size * 0.18), base - perp * (size * 0.18));
        break;
    }
    case ArrowType::Dot: {
        const double r = size * 0.35;
        const Vec2 c = tip;
        const Vec2 a0 = c + perp * r;
        const Vec2 a1 = c + u * r;
        const Vec2 a2 = c - perp * r;
        const Vec2 a3 = c - u * r;
        tri(fills, a0, a1, a2); // a filled diamond approximates the dot
        tri(fills, a0, a2, a3);
        break;
    }
    case ArrowType::Tick: {
        const Vec2 d = (u + perp) * (size * 0.5);
        seg(lines, tip - d, tip + d);
        break;
    }
    case ArrowType::Open: {
        seg(lines, tip, base + perp * (size * 0.25));
        seg(lines, tip, base - perp * (size * 0.25));
        break;
    }
    }
}

double dim_measure(const DimData& d) {
    switch (d.type) {
    case DimType::Aligned:
        return distance(d.a, d.b);
    case DimType::Radius:
        return distance(d.a, d.b); // a = centre, b = point on the circle/arc
    case DimType::Diameter:
        return 2.0 * distance(d.a, d.b);
    case DimType::Angular: {
        // a = vertex, b = point on ray 1, line_pt = point on ray 2.
        const Vec2 u1 = normalized(d.b - d.a);
        const Vec2 u2 = normalized(d.line_pt - d.a);
        const double c = std::clamp(dot(u1, u2), -1.0, 1.0);
        return to_degrees(std::acos(c));
    }
    case DimType::Linear:
        break;
    }
    const Vec2 dir = dim_direction(d);
    return std::abs(dot(d.b - d.a, dir));
}

std::string format_measurement(double value, std::uint8_t precision) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", static_cast<int>(precision), value);
    return std::string(buf);
}

// Builds the geometry from an ALREADY-EFFECTIVE style (overrides applied).
static DimGeometry compute_dim_geometry_styled(const DimData& d, const DimStyle& style,
                                               Rgb base_color);

DimGeometry compute_dim_geometry(const DimData& d, const DimStyle& style, Rgb base_color) {
    // The single resolution point: per-dimension overrides win over the style
    // (the Ph12 ByLayer/override shape), then the body reads the effective style.
    return compute_dim_geometry_styled(d, apply_dim_overrides(style, d.overrides), base_color);
}

static DimGeometry compute_dim_geometry_styled(const DimData& d, const DimStyle& style,
                                               Rgb base_color) {
    DimGeometry g;
    g.text_height = style.text_height;
    g.lineweight = style.dim_lineweight;
    g.dim_color = style.dim_color.resolve(base_color);
    g.ext_color = style.ext_color.resolve(base_color);
    g.arrow_color = style.arrow_color.resolve(base_color);
    g.text_color = style.text_color.resolve(base_color);
    const auto atype = static_cast<ArrowType>(style.arrow_type);
    const double value = dim_measure(d);

    if (d.type == DimType::Radius || d.type == DimType::Diameter) {
        const Vec2 center = d.a;
        const Vec2 edge = d.b;
        const Vec2 u = normalized(edge - center);
        if (d.type == DimType::Radius) {
            seg(g.dim_lines, center, edge);
            append_arrowhead(g.arrow_fills, g.arrow_lines, edge, u * -1.0, style.arrow_size, atype);
            g.label = "R" + format_measurement(value, style.precision);
        } else {
            const Vec2 other = center - u * distance(center, edge);
            seg(g.dim_lines, other, edge);
            append_arrowhead(g.arrow_fills, g.arrow_lines, edge, u * -1.0, style.arrow_size, atype);
            append_arrowhead(g.arrow_fills, g.arrow_lines, other, u, style.arrow_size, atype);
            g.label = "⌀" + format_measurement(value, style.precision); // diameter symbol
        }
        g.text_pos = edge + u * (style.text_height * 0.4);
        g.text_rotation = 0.0;
        g.text_justify = text::Justify::Left;
        return g;
    }

    if (d.type == DimType::Angular) {
        const Vec2 v = d.a;
        const Vec2 u1 = normalized(d.b - v);
        const Vec2 u2 = normalized(d.line_pt - v);
        const double r = std::max(distance(v, d.b), distance(v, d.line_pt)) * 0.8;
        double a0 = std::atan2(u1.y, u1.x);
        double a1 = std::atan2(u2.y, u2.x);
        double sweep = a1 - a0;
        while (sweep <= -kPi) {
            sweep += kTwoPi;
        }
        while (sweep > kPi) {
            sweep -= kTwoPi;
        }
        constexpr int kSteps = 24;
        Vec2 prev{};
        for (int i = 0; i <= kSteps; ++i) {
            const double a = a0 + sweep * (static_cast<double>(i) / kSteps);
            const Vec2 p{v.x + r * std::cos(a), v.y + r * std::sin(a)};
            if (i > 0) {
                seg(g.dim_lines, prev, p);
            }
            prev = p;
        }
        // Arrowheads tangent to the arc at each end.
        const Vec2 e0{v.x + r * std::cos(a0), v.y + r * std::sin(a0)};
        const Vec2 e1{v.x + r * std::cos(a1), v.y + r * std::sin(a1)};
        const double s = sweep >= 0 ? 1.0 : -1.0;
        append_arrowhead(g.arrow_fills, g.arrow_lines, e0,
                         Vec2{std::sin(a0), -std::cos(a0)} * s, style.arrow_size, atype);
        append_arrowhead(g.arrow_fills, g.arrow_lines, e1,
                         Vec2{-std::sin(a1), std::cos(a1)} * s, style.arrow_size, atype);
        const double am = a0 + sweep * 0.5;
        g.text_pos = {v.x + r * std::cos(am), v.y + r * std::sin(am)};
        g.label = format_measurement(value, style.precision) + "°"; // degree symbol
        g.text_rotation = 0.0;
        return g;
    }

    // Linear / Aligned.
    const Vec2 dir = dim_direction(d);
    const Vec2 fa = foot(d.a, d.line_pt, dir);
    const Vec2 fb = foot(d.b, d.line_pt, dir);
    const auto ext = [&](Vec2 def, Vec2 f) {
        const Vec2 v = f - def;
        const double len = length(v);
        if (len < 1e-9) {
            return;
        }
        const Vec2 n = v / len;
        seg(g.ext_lines, def + n * style.ext_offset, f + n * style.ext_extension);
    };
    ext(d.a, fa);
    ext(d.b, fb);
    seg(g.dim_lines, fa, fb);

    const double span = distance(fa, fb);
    if (span > 1e-9) {
        const Vec2 u = (fb - fa) / span;
        append_arrowhead(g.arrow_fills, g.arrow_lines, fa, u, style.arrow_size, atype);
        append_arrowhead(g.arrow_fills, g.arrow_lines, fb, u * -1.0, style.arrow_size, atype);
    }

    const Vec2 mid = (fa + fb) * 0.5;
    g.text_rotation = std::atan2(dir.y, dir.x);
    if (g.text_rotation > 1.5708 || g.text_rotation < -1.5708) {
        g.text_rotation += kPi;
    }
    // Offset along the text's own baseline->cap direction ("up"), derived from the final
    // text rotation -- NOT the geometric perpendicular. The stroke font grows glyphs from
    // the baseline toward the cap; for a rotated (e.g. vertical) dimension that direction
    // is not the geometric perp, so anchoring to perp inverts Above/Centered. "Centered"
    // straddles the dim line (baseline half a glyph below it); "Above" clears it.
    const double cs = std::cos(g.text_rotation);
    const double sn = std::sin(g.text_rotation);
    const Vec2 text_up{-sn, cs};
    const double off = style.text_above ? style.text_height * 0.4 : -style.text_height * 0.5;
    g.text_pos = mid + text_up * off;
    g.label = format_measurement(value, style.precision);
    return g;
}

} // namespace musacad::core

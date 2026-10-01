// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/dimension.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "musacad/core/text/text_codes.hpp"

namespace musacad::core {

Vec2 dim_line_direction(const DimData& d);

namespace {

Vec2 dim_direction(const DimData& d) { return dim_line_direction(d); }
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

Vec2 dim_line_direction(const DimData& d) {
    if (d.type == DimType::Aligned) {
        const Vec2 v = d.b - d.a;
        return length_squared(v) > 1e-18 ? normalized(v) : Vec2{1, 0};
    }
    if (d.aux == 0.0) { // a linear dimension from before v38
        return std::abs(d.b.x - d.a.x) >= std::abs(d.b.y - d.a.y) ? Vec2{1, 0} : Vec2{0, 1};
    }
    // Readable: left to right, or upwards for a vertical one.
    Vec2 u{std::cos(d.aux), std::sin(d.aux)};
    if (u.x < -1e-12 || (std::abs(u.x) <= 1e-12 && u.y < 0.0)) {
        u = u * -1.0;
    }
    if (std::abs(u.x) < 1e-15) {
        u = {0.0, 1.0};
    } else if (std::abs(u.y) < 1e-15) {
        u = {1.0, 0.0};
    }
    return u;
}

double linear_dim_auto_angle(Vec2 a, Vec2 b, Vec2 line_pt) {
    const Vec2 lo{std::min(a.x, b.x), std::min(a.y, b.y)};
    const Vec2 hi{std::max(a.x, b.x), std::max(a.y, b.y)};
    const double out_x = line_pt.x < lo.x ? lo.x - line_pt.x : (line_pt.x > hi.x ? line_pt.x - hi.x : 0.0);
    const double out_y = line_pt.y < lo.y ? lo.y - line_pt.y : (line_pt.y > hi.y ? line_pt.y - hi.y : 0.0);
    if (out_x > out_y) {
        return kHalfPi; // to the left or right: the dimension stands up
    }
    if (out_y > out_x) {
        return 0.0; // above or below: it runs across
    }
    return hi.x - lo.x >= hi.y - lo.y ? 0.0 : kHalfPi;
}

double linear_dim_aux(double angle) {
    double a = std::fmod(angle, kPi);
    if (a <= 1e-12) {
        a += kPi;
    }
    return a > kPi ? a - kPi : a;
}

void orient_linear_dim(DimData& d, std::optional<double> fixed, bool circle) {
    if (circle) {
        const Vec2 c = (d.a + d.b) * 0.5;
        const double r = distance(d.a, d.b) * 0.5;
        const double angle = fixed ? *fixed : linear_dim_auto_angle(c - Vec2{r, r}, c + Vec2{r, r}, d.line_pt);
        const Vec2 u{std::cos(angle), std::sin(angle)};
        d.a = c - u * r;
        d.b = c + u * r;
        d.aux = linear_dim_aux(angle);
        return;
    }
    d.aux = linear_dim_aux(fixed ? *fixed : linear_dim_auto_angle(d.a, d.b, d.line_pt));
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
    case DimType::Ordinate:
        return d.aux < 0.5 ? d.a.x : d.a.y; // the feature's coordinate from the origin
    case DimType::Jogged:
        return distance(d.a, d.b); // the TRUE radius (a = true centre), never the override
    case DimType::ArcLength: {
        const double r = distance(d.a, d.b);
        double sweep = std::fmod(d.aux - std::atan2(d.b.y - d.a.y, d.b.x - d.a.x), kTwoPi);
        if (sweep < 0.0) {
            sweep += kTwoPi;
        }
        if (sweep <= 1e-12) {
            sweep = kTwoPi;
        }
        return r * sweep;
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

namespace {

/// The type-specific decoration the measured value always carries (R, the diameter
/// sign, the degree sign). Independent of the AUTHORED decoration below. `value` is
/// passed in rather than measured here so the Limits mode can format its two limits
/// through exactly the same per-type rule.
std::string measured_text_for(DimType type, const DimStyle& style, double value) {
    const std::string v = format_measurement(value, style.precision);
    switch (type) {
    case DimType::Radius:
    case DimType::Jogged:
        return "R" + v;
    case DimType::Ordinate:
    case DimType::ArcLength:
        return v;
    case DimType::Diameter:
        return "⌀" + v; // U+2300
    case DimType::Angular:
        return v + "°"; // U+00B0
    case DimType::Linear:
    case DimType::Aligned:
        break;
    }
    return v;
}

std::string measured_text(const DimData& d, const DimStyle& style) {
    return measured_text_for(d.type, style, dim_measure(d));
}

} // namespace

bool dim_label_quad(const DimGeometry& g, bool second, Vec2 (&out)[4]) {
    const std::string& s = second ? g.label2 : g.label;
    if (s.empty()) {
        return false;
    }
    const Vec2 at = second ? g.label2_pos : g.text_pos;
    const double w = text::text_width(s, g.text_height);
    const double x0 = g.text_justify == text::Justify::Center  ? -w * 0.5
                      : g.text_justify == text::Justify::Right ? -w
                                                               : 0.0;
    const double cs = std::cos(g.text_rotation);
    const double sn = std::sin(g.text_rotation);
    const Vec2 ax{cs, sn};  // along the baseline
    const Vec2 ay{-sn, cs}; // baseline -> cap
    out[0] = at + ax * x0;
    out[1] = at + ax * (x0 + w);
    out[2] = at + ax * (x0 + w) + ay * g.text_height;
    out[3] = at + ax * x0 + ay * g.text_height;
    return true;
}

DimLabel compose_dim_label(const DimData& d, const DimStyle& style, DimTextParts parts) {
    // Prefix/suffix are ordinary text: run them through the ONE control-code pass, so
    // "%%c200" and "6X" work here exactly as they do in a TEXT entity, with no
    // dimension-specific symbol handling. Storage stays raw (derived-not-baked).
    const std::string pre = text::substitute_text(parts.prefix);
    const std::string suf = text::substitute_text(parts.suffix);
    const auto wrap = [&](const std::string& core) { return pre + core + suf; };

    DimLabel out;

    // ---------------------------------------------------------------------------
    // Text override (issue #20). AutoCAD's Properties "Text override" field, where
    // `<>` stands in for the measurement -- so "<> H7" tracks the geometry while
    // "APPROX 50" does not, and both are the author's explicit choice.
    //
    // DECISION: an override produces the WHOLE label. The prefix/suffix and the
    // deviation-deriving tolerance modes (Symmetric, Limits) are not applied on top,
    // because an author writing the text themselves is not also asking for text to be
    // generated around it -- stacking them silently would produce strings nobody typed.
    // Basic (the box) and Reference (the parentheses) DO still apply: those frame the
    // label rather than saying anything, and a basic dimension stays basic whatever its
    // text reads. Rejected: applying every mode on top of the override, which makes
    // "<> H7" in Limits mode mean something no one can predict.
    //
    // The measurement is still computed regardless, so an override can always be
    // inspected, compared against the geometry, and removed.
    if (!parts.text_override.empty()) {
        const std::string raw(parts.text_override);
        const std::string measured = measured_text(d, style);
        std::string expanded;
        expanded.reserve(raw.size() + measured.size());
        for (std::size_t i = 0; i < raw.size();) {
            if (raw.compare(i, 2, "<>") == 0) {
                expanded += measured;
                i += 2;
            } else {
                expanded += raw[i];
                ++i;
            }
        }
        // Control codes expand through the SAME pass every other text uses, so an
        // override can carry %%c and \U+XXXX with no dimension-specific handling.
        out.line1 = text::substitute_text(expanded);
        if (d.tol.mode == TolMode::Basic) {
            out.boxed = true;
        } else if (d.tol.mode == TolMode::Reference) {
            out.line1 = "(" + out.line1 + ")";
        }
        return out;
    }

    switch (d.tol.mode) {
    case TolMode::Symmetric:
        // One line: the value, then the deviation. ISO 129-1 writes a single
        // deviation as value ± tol.
        out.line1 = wrap(measured_text(d, style) + " ±" +
                         format_measurement(std::abs(d.tol.upper), style.precision));
        break;
    case TolMode::Limits: {
        // Two lines: the actual limit VALUES, upper over lower -- not the deviations.
        // That is what a machinist reads off the drawing.
        const double v = dim_measure(d);
        const double hi = std::max(v + d.tol.upper, v + d.tol.lower);
        const double lo = std::min(v + d.tol.upper, v + d.tol.lower);
        out.line1 = wrap(measured_text_for(d.type, style, hi));
        out.line2 = wrap(measured_text_for(d.type, style, lo));
        break;
    }
    case TolMode::Basic:
        out.line1 = wrap(measured_text(d, style));
        out.boxed = true;
        break;
    case TolMode::Reference:
        out.line1 = "(" + wrap(measured_text(d, style)) + ")";
        break;
    case TolMode::None:
        out.line1 = wrap(measured_text(d, style));
        break;
    }
    return out;
}

// Builds the geometry from an ALREADY-EFFECTIVE style (overrides applied).
static DimGeometry compute_dim_geometry_styled(const DimData& d, const DimStyle& style,
                                               Rgb base_color, DimTextParts parts);

DimGeometry compute_dim_geometry(const DimData& d, const DimStyle& style, Rgb base_color,
                                 DimTextParts parts) {
    // The single resolution point: per-dimension overrides win over the style
    // (the Ph12 ByLayer/override shape), then the body reads the effective style.
    return compute_dim_geometry_styled(d, apply_dim_overrides(style, d.overrides), base_color,
                                       parts);
}

static DimGeometry compute_dim_geometry_styled(const DimData& d, const DimStyle& style,
                                               Rgb base_color, DimTextParts parts) {
    DimGeometry g;
    g.text_height = style.text_height;
    g.lineweight = style.dim_lineweight;
    g.dim_color = style.dim_color.resolve(base_color);
    g.ext_color = style.ext_color.resolve(base_color);
    g.arrow_color = style.arrow_color.resolve(base_color);
    g.text_color = style.text_color.resolve(base_color);
    const auto atype = static_cast<ArrowType>(style.arrow_type);
    // The label is composed ONCE, here, for every dimension type -- the measured value
    // (which is never authorable) plus its authored decoration. Placing it in the same
    // function that builds the geometry is what makes the text participate in layout,
    // bounds and selection instead of floating alongside them.
    const DimLabel label = compose_dim_label(d, style, parts);
    g.label = label.line1;
    g.label2 = label.line2;

    /// Finishes the label once text_pos / rotation / justify are final: places the
    /// second line (Limits mode) and, for a basic dimension, draws the ASME Y14.5
    /// frame around exactly what will be drawn. The frame is dimension-line geometry,
    /// so it takes that colour and is picked and bounded with the dimension.
    const auto finish_label = [&]() {
        const double h = g.text_height;
        const double cs = std::cos(g.text_rotation);
        const double sn = std::sin(g.text_rotation);
        const Vec2 ax{cs, sn};  // along the baseline
        const Vec2 ay{-sn, cs}; // baseline -> cap
        const double line_gap = h * 1.5;
        // Author displacement (issue #21), applied HERE because finish_label is the one
        // place text_pos becomes final for every dimension type -- so all five get the
        // grip from a single edit, and the automatic ISO 129-1 fit (#12) still ran above
        // to choose the derived position this offset is measured from.
        if (d.text_offset.x != 0.0 || d.text_offset.y != 0.0) {
            g.derived_text_pos = g.text_pos; // remember where it would have sat
            g.text_moved = true;
            g.text_pos = g.text_pos + ax * d.text_offset.x + ay * d.text_offset.y;
        }
        const double pad = h * 0.4; // ASME draws the frame close around the text
        // A boxed label needs the FRAME, not the text, to clear the dimension line --
        // otherwise the box's lower edge lands exactly on it. Lift before placing the
        // second line so both stay inside the frame.
        if (label.boxed) {
            g.text_pos = g.text_pos + ay * pad;
        }
        if (!g.label2.empty()) {
            g.label2_pos = g.text_pos - ay * line_gap; // the lower limit sits under the upper
        }
        if (!label.boxed) {
            return;
        }
        const double w = std::max(text::text_width(g.label, h), text::text_width(g.label2, h));
        const double x0 = g.text_justify == text::Justify::Center  ? -w * 0.5
                          : g.text_justify == text::Justify::Right ? -w
                                                                   : 0.0;
        const double y_bottom = g.label2.empty() ? 0.0 : -line_gap;
        const Vec2 c0 = g.text_pos + ax * (x0 - pad) + ay * (y_bottom - pad);
        const Vec2 c1 = g.text_pos + ax * (x0 + w + pad) + ay * (y_bottom - pad);
        const Vec2 c2 = g.text_pos + ax * (x0 + w + pad) + ay * (h + pad);
        const Vec2 c3 = g.text_pos + ax * (x0 - pad) + ay * (h + pad);
        seg(g.dim_lines, c0, c1);
        seg(g.dim_lines, c1, c2);
        seg(g.dim_lines, c2, c3);
        seg(g.dim_lines, c3, c0);
    };

    /// A displaced label that has left its dimension line gets a connector back to it,
    /// which is what ISO 129-1 expects when the value is not adjacent to what it
    /// measures -- otherwise a dragged value is visually orphaned. Drawn only once the
    /// text has actually cleared the line (a nudge of less than one text height is still
    /// "next to" it), so small tidying drags stay clean.
    const auto connect_moved_label = [&]() {
        if (!g.text_moved) {
            return;
        }
        const double h = g.text_height;
        if (length(g.text_pos - g.derived_text_pos) < h * 1.5) {
            return;
        }
        const double cs = std::cos(g.text_rotation);
        const double sn = std::sin(g.text_rotation);
        const Vec2 ax{cs, sn};
        const Vec2 ay{-sn, cs};
        // Land on the label's near baseline corner, so the connector meets the text
        // rather than stopping in space beside it.
        const double w = std::max(text::text_width(g.label, h), text::text_width(g.label2, h));
        const double x0 = g.text_justify == text::Justify::Center  ? -w * 0.5
                          : g.text_justify == text::Justify::Right ? -w
                                                                   : 0.0;
        const Vec2 left = g.text_pos + ax * x0;
        const Vec2 right = g.text_pos + ax * (x0 + w);
        const Vec2 land = distance(g.derived_text_pos, left) <= distance(g.derived_text_pos, right)
                              ? left
                              : right;
        seg(g.dim_lines, g.derived_text_pos, land);
        seg(g.dim_lines, land, land + (land.x >= g.derived_text_pos.x ? ax : ax * -1.0) * (h * 0.6));
    };

    if (d.type == DimType::Radius || d.type == DimType::Diameter) {
        const Vec2 center = d.a;
        const Vec2 edge = d.b;
        const Vec2 u = normalized(edge - center);
        if (d.type == DimType::Radius) {
            seg(g.dim_lines, center, edge);
            append_arrowhead(g.arrow_fills, g.arrow_lines, edge, u * -1.0, style.arrow_size, atype);
        } else {
            const Vec2 other = center - u * distance(center, edge);
            seg(g.dim_lines, other, edge);
            append_arrowhead(g.arrow_fills, g.arrow_lines, edge, u * -1.0, style.arrow_size, atype);
            append_arrowhead(g.arrow_fills, g.arrow_lines, other, u, style.arrow_size, atype);
        }
        g.text_pos = edge + u * (style.text_height * 0.4);
        g.text_rotation = 0.0;
        g.text_justify = text::Justify::Left;
        finish_label();
        connect_moved_label();
        return g;
    }

    if (d.type == DimType::Ordinate) {
        // AutoCAD DIMORDINATE: a leader from the feature along the datum axis to the
        // leader endpoint, with a dogleg when the endpoint is off that axis; the label
        // continues the leader (reading along it for an X datum).
        const bool xdatum = d.aux < 0.5;
        const Vec2 f = d.a;
        const Vec2 e = d.b;
        if (xdatum) {
            const double dy = e.y - f.y;
            const double sy = dy >= 0.0 ? 1.0 : -1.0;
            const Vec2 p0{f.x, f.y + sy * style.ext_offset};
            if (std::abs(e.x - f.x) < 1e-9) {
                seg(g.dim_lines, p0, e);
            } else {
                const double ky = f.y + dy * 0.6;
                seg(g.dim_lines, p0, Vec2{f.x, ky});
                seg(g.dim_lines, Vec2{f.x, ky}, Vec2{e.x, ky});
                seg(g.dim_lines, Vec2{e.x, ky}, e);
            }
            g.text_pos = e + Vec2{0.0, sy * style.text_height * 0.4};
            g.text_rotation = sy > 0.0 ? kHalfPi : -kHalfPi;
        } else {
            const double dx = e.x - f.x;
            const double sx = dx >= 0.0 ? 1.0 : -1.0;
            const Vec2 p0{f.x + sx * style.ext_offset, f.y};
            if (std::abs(e.y - f.y) < 1e-9) {
                seg(g.dim_lines, p0, e);
            } else {
                const double kx = f.x + dx * 0.6;
                seg(g.dim_lines, p0, Vec2{kx, f.y});
                seg(g.dim_lines, Vec2{kx, f.y}, Vec2{kx, e.y});
                seg(g.dim_lines, Vec2{kx, e.y}, e);
            }
            g.text_pos = e + Vec2{sx * style.text_height * 0.4, 0.0};
            g.text_rotation = 0.0;
        }
        g.text_justify = text::Justify::Left;
        finish_label();
        connect_moved_label();
        return g;
    }

    if (d.type == DimType::Jogged) {
        // AutoCAD DIMJOGGED: the dimension line runs from the CENTRE OVERRIDE (line_pt)
        // to the arc with a zigzag jog at `aux` of its length; the value is the true
        // radius from the true centre (a), which the drawing never shows.
        const Vec2 oc = d.line_pt;
        const Vec2 edge = d.b;
        const double len = distance(oc, edge);
        const Vec2 u = len > 1e-9 ? (edge - oc) / len : Vec2{1.0, 0.0};
        const Vec2 n{-u.y, u.x};
        const double t = std::clamp(d.aux, 0.1, 0.9);
        const Vec2 P = oc + u * (len * t);
        const double h = style.text_height * 0.8;
        const Vec2 A = P - u * h;
        const Vec2 B = P - u * (h / 3.0) + n * (h * 0.6);
        const Vec2 C = P + u * (h / 3.0) - n * (h * 0.6);
        const Vec2 D = P + u * h;
        seg(g.dim_lines, oc, A);
        seg(g.dim_lines, A, B);
        seg(g.dim_lines, B, C);
        seg(g.dim_lines, C, D);
        seg(g.dim_lines, D, edge);
        append_arrowhead(g.arrow_fills, g.arrow_lines, edge, u * -1.0, style.arrow_size, atype);
        g.text_pos = P + n * (h * 0.6 + style.text_height * 0.5);
        g.text_rotation = 0.0;
        g.text_justify = text::Justify::Center;
        finish_label();
        connect_moved_label();
        return g;
    }

    if (d.type == DimType::ArcLength) {
        // AutoCAD DIMARC: radial extension lines from the arc's ends, a dimension ARC
        // concentric with it at the placement radius, arrowheads at both ends, and the
        // arc length as the value.
        const Vec2 c = d.a;
        const double r = distance(c, d.b);
        const double a0 = std::atan2(d.b.y - c.y, d.b.x - c.x);
        double sweep = std::fmod(d.aux - a0, kTwoPi);
        if (sweep < 0.0) {
            sweep += kTwoPi;
        }
        if (sweep <= 1e-12) {
            sweep = kTwoPi;
        }
        const double a1 = a0 + sweep;
        const double rd = std::max(distance(c, d.line_pt), r + style.ext_offset + style.arrow_size);
        const auto on = [&](double ang, double rad) {
            return Vec2{c.x + rad * std::cos(ang), c.y + rad * std::sin(ang)};
        };
        seg(g.ext_lines, on(a0, r + style.ext_offset), on(a0, rd + style.ext_extension));
        seg(g.ext_lines, on(a1, r + style.ext_offset), on(a1, rd + style.ext_extension));
        const int steps = std::max(24, static_cast<int>(std::ceil(sweep / 0.05)));
        Vec2 prev{};
        for (int i = 0; i <= steps; ++i) {
            const Vec2 p = on(a0 + sweep * (static_cast<double>(i) / steps), rd);
            if (i > 0) {
                seg(g.dim_lines, prev, p);
            }
            prev = p;
        }
        const Vec2 e0 = on(a0, rd);
        const Vec2 e1 = on(a1, rd);
        append_arrowhead(g.arrow_fills, g.arrow_lines, e0, Vec2{std::sin(a0), -std::cos(a0)},
                         style.arrow_size, atype);
        append_arrowhead(g.arrow_fills, g.arrow_lines, e1, Vec2{-std::sin(a1), std::cos(a1)},
                         style.arrow_size, atype);
        const double am = a0 + sweep * 0.5;
        g.text_pos = on(am, rd + style.text_height * 0.7);
        g.text_rotation = 0.0;
        g.text_justify = text::Justify::Center;
        finish_label();
        connect_moved_label();
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
        g.text_rotation = 0.0;
        finish_label();
        connect_moved_label();
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

    // ---------------------------------------------------------------------------
    // ISO 129-1 narrow-dimension fit. This is a RENDERER decision because only the
    // renderer knows the glyph advance: a 15 mm feature at 1:5 leaves 3.0 mm between
    // the extension lines, and "15" at the ISO-recommended 2.5 mm text height is
    // exactly 2 x 1.5 = 3.0 mm wide with this font's 0.6h advance -- the text cell
    // exactly fills the gap and the first glyph renders in ink contact with the line.
    //
    // Text and arrows are tested SEPARATELY, giving the four states the standard
    // recognises (both inside / arrows out / text out / both out). One binary test
    // would push arrows outside on a dimension whose arrows fit perfectly well.
    // ---------------------------------------------------------------------------
    const double span = distance(fa, fb);
    const Vec2 u = span > 1e-9 ? (fb - fa) / span : Vec2{1, 0};
    const double gap = style.text_height * 0.4; // clear space text <-> extension line

    // Measure the FULLY DECORATED, code-substituted label with the same function that
    // emits it (text::text_width), using the widest line when Limits stacks two.
    const double label_w =
        std::max(text::text_width(g.label, style.text_height),
                 text::text_width(g.label2, style.text_height));
    const bool text_fits = label_w + 2.0 * gap <= span;
    // Arrows need room for both heads plus a minimum clear span between their bases.
    const bool arrows_fit = 2.0 * style.arrow_size + style.arrow_size * 0.6 <= span;

    const auto fit_mode = static_cast<TextFit>(style.text_fit);
    const bool text_inside = fit_mode == TextFit::Inside    ? true
                             : fit_mode == TextFit::Outside ? false
                                                            : text_fits;
    const bool arrows_inside = arrows_fit;

    // Arrows outside: the SAME append_arrowhead call with the direction reversed, so
    // each head sits beyond its extension line pointing back in. There is deliberately
    // no second arrowhead code path -- `along` was already the direction parameter.
    if (span > 1e-9) {
        const double s = arrows_inside ? 1.0 : -1.0;
        append_arrowhead(g.arrow_fills, g.arrow_lines, fa, u * s, style.arrow_size, atype);
        append_arrowhead(g.arrow_fills, g.arrow_lines, fb, u * -s, style.arrow_size, atype);
    }

    // The dimension line runs foot to foot, extended past each extension line by the
    // arrow length plus a short stub when the arrows are outside, so each head has a
    // line to sit on (ISO 129-1 fig. 8).
    const double stub = arrows_inside ? 0.0 : style.arrow_size * 1.6;
    seg(g.dim_lines, fa - u * stub, fb + u * stub);

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
    if (text_inside) {
        g.text_pos = mid + text_up * off;
    } else {
        // Outside: on the dimension line's own extension, past the second extension
        // line, clearing the outside arrowhead + stub when there is one, then the gap.
        // Left-justified from there, so the text reads away from the feature.
        g.text_pos = fb + u * (stub + gap) + text_up * off;
        g.text_justify = text::Justify::Left;
    }
    // A two-line label (Limits) stacks DOWNWARD from text_pos, so lift the block by one
    // line to keep it clear of the dimension line -- otherwise the lower limit lands on it.
    if (!g.label2.empty()) {
        g.text_pos = g.text_pos + text_up * (g.text_height * 1.5);
    }
    finish_label();
    connect_moved_label();
    return g;
}

} // namespace musacad::core

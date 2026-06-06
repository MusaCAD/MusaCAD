#include "musacad/core/dimension.hpp"

#include <cmath>
#include <cstdio>

namespace musacad::core {

namespace {

// Direction the dimension line runs, for Linear (dominant axis) / Aligned.
Vec2 dim_direction(const DimData& d) {
    if (d.type == DimType::Aligned) {
        const Vec2 v = d.b - d.a;
        return length_squared(v) > 1e-18 ? normalized(v) : Vec2{1, 0};
    }
    // Linear: pick the dominant axis of the two def points.
    return std::abs(d.b.x - d.a.x) >= std::abs(d.b.y - d.a.y) ? Vec2{1, 0} : Vec2{0, 1};
}

// Foot of point p on the dimension line (through line_pt, along dir).
Vec2 foot(Vec2 p, Vec2 line_pt, Vec2 dir) { return line_pt + dir * dot(p - line_pt, dir); }

void push_seg(std::vector<Vec2>& out, Vec2 a, Vec2 b) {
    out.push_back(a);
    out.push_back(b);
}

// A filled-triangle arrowhead approximated by a fan of lines from the tip; or a
// 45-degree tick. `tip` is on the dim line, `along` points back into the line.
void arrowhead(std::vector<Vec2>& out, Vec2 tip, Vec2 along, double size, std::uint8_t type) {
    const Vec2 perp{-along.y, along.x};
    if (type == 1) { // tick: a short diagonal slash through the tip
        const Vec2 d = (along + perp) * (size * 0.5);
        push_seg(out, tip - d, tip + d);
        return;
    }
    const Vec2 base = tip + along * size;
    const Vec2 b1 = base + perp * (size * 0.18);
    const Vec2 b2 = base - perp * (size * 0.18);
    // Outline + a fan from the tip to fake a filled triangle for a line renderer.
    push_seg(out, tip, b1);
    push_seg(out, tip, b2);
    push_seg(out, b1, b2);
    for (int i = 1; i < 4; ++i) {
        const Vec2 mid = b2 + (b1 - b2) * (static_cast<double>(i) / 4.0);
        push_seg(out, tip, mid);
    }
}

} // namespace

double dim_measure(const DimData& d) {
    switch (d.type) {
    case DimType::Aligned:
        return distance(d.a, d.b);
    case DimType::Radius:
        return distance(d.a, d.b); // a=center, b=point on circle
    case DimType::Diameter:
        return 2.0 * distance(d.a, d.b);
    case DimType::Angular:
        return 0.0; // staged
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

DimGeometry compute_dim_geometry(const DimData& d, const DimStyle& style) {
    DimGeometry g;
    g.text_height = style.text_height;
    g.label = format_measurement(dim_measure(d), style.precision);

    if (d.type != DimType::Linear && d.type != DimType::Aligned) {
        // Other types are staged: place the label at line_pt, no lines yet.
        g.text_pos = d.line_pt;
        return g;
    }

    const Vec2 dir = dim_direction(d);
    const Vec2 fa = foot(d.a, d.line_pt, dir);
    const Vec2 fb = foot(d.b, d.line_pt, dir);

    // Extension lines: from a small gap off each def point, out past the dim line.
    const auto ext = [&](Vec2 def, Vec2 f) {
        const Vec2 v = f - def;
        const double len = length(v);
        if (len < 1e-9) {
            return;
        }
        const Vec2 n = v / len;
        push_seg(g.lines, def + n * style.ext_offset, f + n * style.ext_extension);
    };
    ext(d.a, fa);
    ext(d.b, fb);

    // Dimension line between the feet.
    push_seg(g.lines, fa, fb);

    // Arrowheads at each end, pointing inward.
    const double span = distance(fa, fb);
    if (span > 1e-9) {
        const Vec2 u = (fb - fa) / span; // fa -> fb
        arrowhead(g.arrows, fa, u, style.arrow_size, style.arrow_type);
        arrowhead(g.arrows, fb, u * -1.0, style.arrow_size, style.arrow_type);
    }

    // Text: centred on the dim line, lifted above it, aligned with the line.
    const Vec2 mid = (fa + fb) * 0.5;
    Vec2 perp{-dir.y, dir.x};
    // Keep text upright-ish: flip the perpendicular so the lift is "up".
    if (perp.y < 0.0 || (std::abs(perp.y) < 1e-9 && perp.x < 0.0)) {
        perp = perp * -1.0;
    }
    const double lift = style.text_above ? style.text_height * 0.4 : -style.text_height * 0.5;
    g.text_pos = mid + perp * lift;
    g.text_rotation = std::atan2(dir.y, dir.x);
    if (g.text_rotation > 1.5708 || g.text_rotation < -1.5708) {
        g.text_rotation += 3.14159265358979; // avoid upside-down text
    }
    g.text_justify = text::Justify::Center;
    return g;
}

} // namespace musacad::core

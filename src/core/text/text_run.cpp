// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/text/text_run.hpp"

#include <cmath>
#include <string_view>

#include "musacad/core/font_engine.hpp"
#include "musacad/core/text/stroke_font.hpp"
#include "musacad/core/text/text_codes.hpp"
#include "musacad/core/text_style.hpp"

namespace musacad::core::text {

void text_run_geometry(std::string_view raw, Vec2 origin, double height, double rotation, Justify justify,
                       std::string_view font_name, const IFontEngine* fonts, const TextStyle* ts, bool outlines,
                       TextRunGeometry& out) {
    out.segments.clear();
    out.tris.clear();
    out.rings.clear();
    out.bars.clear();
    const double wf = ts != nullptr ? ts->width_factor : 1.0;
    const double ob = ts != nullptr ? ts->oblique : 0.0;
    const bool flip_x = ts != nullptr && ts->backwards;
    const bool flip_y = ts != nullptr && ts->upside_down;
    // Backwards / Upside down: mirrored in the text's own frame, about the insertion
    // point (left to right) and the baseline (top to bottom).
    const auto mirror = [&](std::vector<Vec2>& pts) {
        if (!flip_x && !flip_y) {
            return;
        }
        const double c = std::cos(rotation);
        const double s = std::sin(rotation);
        for (Vec2& p : pts) {
            const Vec2 d = p - origin;
            double lx = d.x * c + d.y * s;
            double ly = -d.x * s + d.y * c;
            lx = flip_x ? -lx : lx;
            ly = flip_y ? -ly : ly;
            p = origin + Vec2{lx * c - ly * s, lx * s + ly * c};
        }
    };
    // Control codes are expanded here (storage stays raw). `str` drives every metric and
    // glyph; the over/under-line toggles become the bars.
    const SubstitutedText sub = substitute_text_codes(raw);
    const std::string_view str = sub.text;
    out.outline = fonts != nullptr && fonts->is_outline_font(font_name);
    const auto measure = [&](std::string_view s) {
        return out.outline ? fonts->advance(font_name, s, height) : text_width(s, height);
    };
    double off = 0.0; // justification along the baseline
    if (justify != Justify::Left) {
        const double w = measure(str);
        off = justify == Justify::Center ? -w * 0.5 : -w;
    }
    const double cs = std::cos(rotation);
    const double sn = std::sin(rotation);
    const Vec2 o{origin.x + off * cs, origin.y + off * sn};
    if (out.outline && outlines) {
        fonts->glyph_outlines(font_name, str, o, height, rotation, out.rings);
        for (std::vector<Vec2>& ring : out.rings) {
            apply_text_style(ring, origin, rotation, wf, ob);
            mirror(ring);
        }
    } else if (out.outline) {
        fonts->glyph_fills(font_name, str, o, height, rotation, out.tris);
        apply_text_style(out.tris, origin, rotation, wf, ob); // STYLE width/oblique
        mirror(out.tris);
    } else {
        append_text_segments(str, o, height, rotation, Justify::Left, out.segments);
        apply_text_style(out.segments, origin, rotation, wf, ob);
        mirror(out.segments);
    }
    // Overline (%%o) / underline (%%u): a horizontal stroke spanning each toggled run,
    // placed just above the cap height / just below the baseline.
    const auto bar = [&](const std::vector<DecorSpan>& spans, double ly) {
        for (const DecorSpan& s : spans) {
            const double x0 = measure(str.substr(0, s.begin));
            const double x1 = measure(str.substr(0, s.end));
            std::vector<Vec2> pts{{o.x + x0 * cs - ly * sn, o.y + x0 * sn + ly * cs},
                                  {o.x + x1 * cs - ly * sn, o.y + x1 * sn + ly * cs}};
            apply_text_style(pts, origin, rotation, wf, ob);
            mirror(pts);
            out.bars.insert(out.bars.end(), pts.begin(), pts.end());
        }
    };
    bar(sub.overline, height * 1.15);
    bar(sub.underline, -height * 0.22);
}

} // namespace musacad::core::text

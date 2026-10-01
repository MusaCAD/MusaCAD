// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/text/text_frame.hpp"

#include <string>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/text/mtext.hpp"
#include "musacad/core/text/stroke_font.hpp"
#include "musacad/core/text/text_codes.hpp"

namespace musacad::core::text {

namespace {
TextFrame from_frame(const JustifyFrame& f, double unit, double oblique) {
    TextFrame out;
    out.origin = f.origin;
    out.height = f.height;
    out.rotation = f.rotation;
    out.width_factor = f.width_factor;
    out.oblique = oblique;
    out.advance = unit * f.height;
    return out;
}
} // namespace

TextFrame frame_of(const GeometryStore& store, const TextData& t) {
    const TextStyle& style = store.text_style_of(t);
    // What is measured is what is seen: the control codes expanded.
    const std::string visible = substitute_text(store.string_of(t));
    const double unit = text_advance(store.font_engine(), store.font_name(t.font), visible, 1.0);
    const double factor = style.width_factor * t.width_factor;
    return from_frame(justify_frame(t.justify, t.pos, t.align, t.height, t.rotation, unit, factor), unit,
                      style.oblique);
}

TextFrame frame_of(std::string_view content, std::uint8_t justify, Vec2 pos, Vec2 align, double height,
                   double rotation, double width_factor) {
    const double unit = text_width(substitute_text(content), 1.0);
    return from_frame(justify_frame(justify, pos, align, height, rotation, unit, width_factor), unit, 0.0);
}

} // namespace musacad::core::text

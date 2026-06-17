// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#include "musacad/render/stroke_font.hpp"

#include <array>
#include <utility>

namespace musacad::render {

namespace {

using Seg = std::pair<Vec2, Vec2>;

// Seven-segment node coordinates in a [0,1]x[0,1] glyph cell (y-up).
constexpr Vec2 TL{0.0, 1.0};
constexpr Vec2 TR{1.0, 1.0};
constexpr Vec2 ML{0.0, 0.5};
constexpr Vec2 MR{1.0, 0.5};
constexpr Vec2 BL{0.0, 0.0};
constexpr Vec2 BR{1.0, 0.0};

constexpr Seg A{TL, TR}; // top
constexpr Seg B{TR, MR}; // upper-right
constexpr Seg C{MR, BR}; // lower-right
constexpr Seg D{BL, BR}; // bottom
constexpr Seg E{BL, ML}; // lower-left
constexpr Seg F{TL, ML}; // upper-left
constexpr Seg G{ML, MR}; // middle

// Appends the segments for one glyph (returns false for an unknown glyph, which
// is rendered as blank).
bool glyph(char c, std::vector<Seg>& out) {
    switch (c) {
    case '0': out = {A, B, C, D, E, F}; return true;
    case '1': out = {B, C}; return true;
    case '2': out = {A, B, G, E, D}; return true;
    case '3': out = {A, B, G, C, D}; return true;
    case '4': out = {F, G, B, C}; return true;
    case '5': out = {A, F, G, C, D}; return true;
    case '6': out = {A, F, G, E, C, D}; return true;
    case '7': out = {A, B, C}; return true;
    case '8': out = {A, B, C, D, E, F, G}; return true;
    case '9': out = {A, B, C, D, F, G}; return true;
    case '.': out = {Seg{{0.35, 0.0}, {0.55, 0.0}}}; return true;
    case '-': out = {G}; return true;
    case 'F': out = {Seg{TL, BL}, A, G}; return true;
    case 'P': out = {Seg{TL, BL}, A, B, G}; return true;
    case 'S': out = {A, F, G, C, D}; return true;
    case 'M':
        out = {Seg{BL, TL}, Seg{BR, TR}, Seg{TL, {0.5, 0.45}}, Seg{{0.5, 0.45}, TR}};
        return true;
    case ' ':
    default:
        out.clear();
        return false;
    }
}

} // namespace

double append_text_segments(std::string_view text, Vec2 origin, double height_px,
                            std::vector<Vec2>& out) {
    constexpr double kAspect = 0.6;       // glyph width / height
    const double glyph_w = height_px * kAspect;
    const double advance = glyph_w * 1.35; // include inter-glyph spacing
    double x = origin.x;

    std::vector<Seg> segs;
    for (const char c : text) {
        if (glyph(c, segs)) {
            for (const Seg& s : segs) {
                out.push_back({x + s.first.x * glyph_w, origin.y + (1.0 - s.first.y) * height_px});
                out.push_back({x + s.second.x * glyph_w, origin.y + (1.0 - s.second.y) * height_px});
            }
        }
        x += advance;
    }
    return x - origin.x;
}

} // namespace musacad::render

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

#include "musacad/core/math/math.hpp"

namespace musacad::core::text {

// Single-line text justification, AutoCAD's fifteen (DXF 72 / 73). The first three are
// the ones a text always had: the insertion point on the baseline, at its left end, its
// middle or its right end. Aligned and Fit run the text between two points (Aligned by
// scaling the height, Fit by squeezing the width); the rest anchor a corner, an edge
// middle or the centre of the text's box.
enum class TextJustify : std::uint8_t {
    Left = 0,
    Center = 1,
    Right = 2,
    Aligned = 3,
    Middle = 4,
    Fit = 5,
    TopLeft = 6,
    TopCenter = 7,
    TopRight = 8,
    MiddleLeft = 9,
    MiddleCenter = 10,
    MiddleRight = 11,
    BottomLeft = 12,
    BottomCenter = 13,
    BottomRight = 14,
};
inline constexpr std::uint8_t kTextJustifyCount = 15;

/// How far the descenders reach below the baseline, as a fraction of the text height:
/// where the Bottom justifications and Middle put the bottom of the box.
inline constexpr double kTextDescent = 1.0 / 3.0;

struct JustifyInfo {
    const char* keyword; ///< what TEXT's Justify option takes ("TL")
    const char* name;    ///< "Top left"
    const char* prompt;  ///< the point TEXT asks for
};
inline constexpr JustifyInfo kJustifyInfo[kTextJustifyCount] = {
    {"L", "Left", "Specify start point of text: "},
    {"C", "Center", "Specify center point of text: "},
    {"R", "Right", "Specify right endpoint of text baseline: "},
    {"A", "Aligned", "Specify first endpoint of text baseline: "},
    {"M", "Middle", "Specify middle point of text: "},
    {"F", "Fit", "Specify first endpoint of text baseline: "},
    {"TL", "Top left", "Specify top-left point of text: "},
    {"TC", "Top center", "Specify top-center point of text: "},
    {"TR", "Top right", "Specify top-right point of text: "},
    {"ML", "Middle left", "Specify middle-left point of text: "},
    {"MC", "Middle center", "Specify middle point of text: "},
    {"MR", "Middle right", "Specify middle-right point of text: "},
    {"BL", "Bottom left", "Specify bottom-left point of text: "},
    {"BC", "Bottom center", "Specify bottom-center point of text: "},
    {"BR", "Bottom right", "Specify bottom-right point of text: "},
};

[[nodiscard]] inline constexpr bool two_point(std::uint8_t justify) noexcept {
    return justify == static_cast<std::uint8_t>(TextJustify::Aligned) ||
           justify == static_cast<std::uint8_t>(TextJustify::Fit);
}

/// Where along the baseline (0 left, 0.5 middle, 1 right) and how far above it (in text
/// heights) a justification's point lies on the text's box.
inline constexpr void justify_anchor(std::uint8_t justify, double& along, double& up) noexcept {
    along = 0.0;
    up = 0.0;
    switch (static_cast<TextJustify>(justify < kTextJustifyCount ? justify : 0)) {
    case TextJustify::Left:
    case TextJustify::Aligned:
    case TextJustify::Fit:
        break;
    case TextJustify::Center:
        along = 0.5;
        break;
    case TextJustify::Right:
        along = 1.0;
        break;
    case TextJustify::Middle: // the middle of the whole box, descenders included
        along = 0.5;
        up = (1.0 - kTextDescent) * 0.5;
        break;
    case TextJustify::TopLeft:
        up = 1.0;
        break;
    case TextJustify::TopCenter:
        along = 0.5;
        up = 1.0;
        break;
    case TextJustify::TopRight:
        along = 1.0;
        up = 1.0;
        break;
    case TextJustify::MiddleLeft:
        up = 0.5;
        break;
    case TextJustify::MiddleCenter:
        along = 0.5;
        up = 0.5;
        break;
    case TextJustify::MiddleRight:
        along = 1.0;
        up = 0.5;
        break;
    case TextJustify::BottomLeft:
        up = -kTextDescent;
        break;
    case TextJustify::BottomCenter:
        along = 0.5;
        up = -kTextDescent;
        break;
    case TextJustify::BottomRight:
        along = 1.0;
        up = -kTextDescent;
        break;
    }
}

/// The text's own frame, as everything that draws, bounds or picks it lays it out: the
/// left end of its baseline, its height, its direction, and the factor its width is
/// scaled by.
struct JustifyFrame {
    Vec2 origin{};
    double height = 1.0;
    double rotation = 0.0;
    double width_factor = 1.0;
};

/// `unit` is the run's advance at height 1 with no width factor (what the font measures);
/// `width_factor` the style's and the text's own, multiplied. Aligned scales the height
/// so the run reaches from `pos` to `align`; Fit keeps the height and takes its width
/// factor from the distance.
[[nodiscard]] inline JustifyFrame justify_frame(std::uint8_t justify, Vec2 pos, Vec2 align,
                                                double height, double rotation, double unit,
                                                double width_factor) noexcept {
    JustifyFrame f{pos, height, rotation, width_factor};
    if (two_point(justify)) {
        const Vec2 d = align - pos;
        const double span = length(d);
        if (span > 1e-12) {
            f.rotation = std::atan2(d.y, d.x);
            if (unit > 1e-12) {
                if (justify == static_cast<std::uint8_t>(TextJustify::Aligned)) {
                    f.height = span / (unit * (width_factor > 1e-12 ? width_factor : 1.0));
                } else if (height > 1e-12) {
                    f.width_factor = span / (unit * height);
                }
            }
        }
        return f;
    }
    double along = 0.0;
    double up = 0.0;
    justify_anchor(justify, along, up);
    const double cs = std::cos(rotation);
    const double sn = std::sin(rotation);
    const double w = unit * height * width_factor;
    const double lx = -along * w;
    const double ly = -up * height;
    f.origin = {pos.x + lx * cs - ly * sn, pos.y + lx * sn + ly * cs};
    return f;
}

/// The other way round: the point (and, for Aligned and Fit, the second point) that
/// leaves a text where `frame` has it under another justification. `width` is the run's
/// drawn width. JUSTIFYTEXT's rule: the justification changes, the text does not move.
inline void justify_points(std::uint8_t justify, const JustifyFrame& frame, double width, Vec2& pos,
                           Vec2& align) noexcept {
    const double cs = std::cos(frame.rotation);
    const double sn = std::sin(frame.rotation);
    double along = 0.0;
    double up = 0.0;
    justify_anchor(justify, along, up);
    const double lx = along * width;
    const double ly = up * frame.height;
    pos = {frame.origin.x + lx * cs - ly * sn, frame.origin.y + lx * sn + ly * cs};
    align = two_point(justify) ? Vec2{frame.origin.x + width * cs, frame.origin.y + width * sn} : pos;
}

/// DXF's pair: 72 (horizontal: 0 left, 1 centre, 2 right, 3 aligned, 4 middle, 5 fit)
/// and 73 (vertical: 0 baseline, 1 bottom, 2 middle, 3 top).
inline constexpr void justify_to_dxf(std::uint8_t justify, int& horizontal, int& vertical) noexcept {
    horizontal = 0;
    vertical = 0;
    if (justify <= 5) {
        horizontal = justify;
        return;
    }
    if (justify < kTextJustifyCount) {
        horizontal = (justify - 6) % 3;
        vertical = 3 - (justify - 6) / 3;
    }
}
[[nodiscard]] inline constexpr std::uint8_t justify_from_dxf(int horizontal, int vertical) noexcept {
    if (vertical <= 0 || vertical > 3) {
        return static_cast<std::uint8_t>(horizontal >= 0 && horizontal <= 5 ? horizontal : 0);
    }
    const int h = horizontal >= 0 && horizontal <= 2 ? horizontal : 0;
    return static_cast<std::uint8_t>(6 + (3 - vertical) * 3 + h);
}

/// The justification a keyword names ("TL", "MC", "Align", "fit"), or -1.
[[nodiscard]] inline int justify_of_keyword(std::string_view word) noexcept {
    const auto same = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            const char x = a[i] >= 'a' && a[i] <= 'z' ? static_cast<char>(a[i] - 32) : a[i];
            const char y = b[i] >= 'a' && b[i] <= 'z' ? static_cast<char>(b[i] - 32) : b[i];
            if (x != y) {
                return false;
            }
        }
        return true;
    };
    for (std::uint8_t i = 0; i < kTextJustifyCount; ++i) {
        if (same(word, kJustifyInfo[i].keyword)) {
            return i;
        }
    }
    constexpr std::string_view words[] = {"LEFT", "CENTER", "RIGHT", "ALIGN", "MIDDLE", "FIT"};
    for (int i = 0; i < 6; ++i) {
        if (same(word, words[i])) {
            return i;
        }
    }
    return same(word, "ALIGNED") ? 3 : -1;
}

} // namespace musacad::core::text

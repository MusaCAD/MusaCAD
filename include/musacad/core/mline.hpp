// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "musacad/core/math/math.hpp"
#include "musacad/core/properties.hpp"

namespace musacad::core {

/// One line of a multiline style (MLSTYLE's Elements): its offset from the multiline's
/// path (positive to the left of the drawing direction, at scale 1), its colour and its
/// linetype -- each ByLayer, ByBlock (the multiline's own) or its own.
struct MLineElement {
    double offset = 0.0;
    std::uint8_t color_mode = 1; ///< 0 its own `color`, 1 ByLayer, 2 ByBlock
    Rgb color{255, 255, 255};
    std::uint8_t linetype_mode = 1; ///< 0 its own `linetype`, 1 ByLayer, 2 ByBlock
    Linetype linetype = Linetype::Continuous;
    friend bool operator==(const MLineElement&, const MLineElement&) = default;
};

/// A multiline style (MLSTYLE, DXF MLINESTYLE): the elements and how the ends, the joints and
/// the area between the outer lines look. `flags` are DXF's group 70 bits.
struct MLineStyle {
    static constexpr std::uint16_t kFill = 1;             ///< the area between the outer lines filled
    static constexpr std::uint16_t kJoints = 2;           ///< a line across at each vertex
    static constexpr std::uint16_t kStartLine = 16;       ///< a line across the start
    static constexpr std::uint16_t kStartInnerArcs = 32;  ///< arcs joining the inner pairs at the start
    static constexpr std::uint16_t kStartOuterArc = 64;   ///< an arc joining the outer lines at the start
    static constexpr std::uint16_t kEndLine = 256;
    static constexpr std::uint16_t kEndInnerArcs = 512;
    static constexpr std::uint16_t kEndOuterArc = 1024;

    std::string name = "STANDARD";
    std::string description;
    std::uint16_t flags = 0;
    double start_angle = 90.0; ///< degrees: the start's slant from the path (90 square)
    double end_angle = 90.0;
    std::uint8_t fill_mode = 1; ///< the fill's colour: 0 `fill_color`, 1 ByLayer, 2 ByBlock
    Rgb fill_color{255, 255, 255};
    std::vector<MLineElement> elements{MLineElement{0.5}, MLineElement{-0.5}};
    friend bool operator==(const MLineStyle&, const MLineStyle&) = default;
};

/// A multiline (MLINE): its path through `vertices`, the style it is drawn in, its scale (the
/// style's offsets times it) and justification -- 0 Top (the path along the line of largest
/// offset), 1 Zero (along offset 0), 2 Bottom (along the line of smallest offset). `breaks`,
/// per segment and per element, are the gaps MLEDIT cut: pairs of distances along the segment
/// from where the element starts on it.
struct MLineData {
    std::vector<Vec2> vertices;
    std::vector<std::vector<std::vector<double>>> breaks{};
    std::uint16_t style = 0;
    double scale = 1.0;
    std::uint8_t justification = 0;
    bool closed = false;
    bool no_start_caps = false; ///< DXF 71 bit 4: the style's start caps not drawn
    bool no_end_caps = false;   ///< DXF 71 bit 8
    EntityProps props{};
};

namespace mline {

/// The elements' offsets from the path, justified and scaled, in the style's order.
[[nodiscard]] std::vector<double> offsets(const MLineStyle& style, double scale, std::uint8_t justification);

/// For each vertex, the vector whose multiple by an element's offset is where that element
/// passes it: mitred at a corner, slanted by the style's start and end angles at an open end.
[[nodiscard]] std::vector<Vec2> miters(const std::vector<Vec2>& vertices, bool closed, double start_angle_deg,
                                       double end_angle_deg);

/// The multiline as lines to draw, every element's pieces (its gaps left out) with the element
/// they belong to, and the caps, the joints and the fill.
struct Geometry {
    struct Piece {
        std::size_t element = 0;
        Vec2 a;
        Vec2 b;
    };
    std::vector<Piece> pieces;
    std::vector<Vec2> caps;  ///< segment pairs: the end lines and arcs and the joints
    std::vector<Vec2> fill;  ///< triangles between the outer lines
    struct Arc {
        Vec2 center;
        double radius = 0.0;
        double start = 0.0; ///< radians, counter-clockwise from start to end
        double end = 0.0;
    };
    std::vector<Arc> arcs;     ///< the caps' arcs (also tessellated into `caps`), for EXPLODE
    std::vector<Vec2> lines;   ///< the caps' and joints' straight lines as segment pairs, for EXPLODE
};
[[nodiscard]] Geometry geometry(const MLineData& m, const MLineStyle& style);

/// Where element `e` passes vertex `i`.
[[nodiscard]] Vec2 element_point(const MLineData& m, const MLineStyle& style, std::size_t i, std::size_t e);

/// The gaps with the empty lists at their ends left off, so equal gaps compare equal.
void trim_breaks(std::vector<std::vector<std::vector<double>>>& breaks);

/// The number of segments (vertices less one, or as many as vertices when closed).
[[nodiscard]] std::size_t segment_count(const MLineData& m) noexcept;

} // namespace mline
} // namespace musacad::core

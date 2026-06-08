#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "musacad/core/math/math.hpp"
#include "musacad/core/mtext_block.hpp"
#include "musacad/core/properties.hpp"

namespace musacad::core {
class GeometryStore;
}

namespace musacad::core::io {

/// The current native document format version. v1: geometry only. v2: layer table
/// + per-entity properties. v3: text + dimensions + dimension styles. v4: leaders +
/// expanded DIMSTYLE (per-element colours, dim lineweight, arrow types). v5: polyline
/// per-vertex arc bulges. v6: MTEXT (paragraph text) + MLEADER (editable leaders).
/// Readers reject newer versions; older files load fine (no layers => layer 0; no
/// dims; no bulges => straight polylines; no mtext/mleader).
inline constexpr std::uint32_t kFormatVersion = 6;

// Self-contained, pool-free records for serialization: own vertices, no
// generational handles, plus the entity's EntityProps (layer + overrides).
struct DocPoint {
    Vec2 p;
    EntityProps props{};
    friend bool operator==(const DocPoint&, const DocPoint&) = default;
};
struct DocLine {
    Vec2 a;
    Vec2 b;
    EntityProps props{};
    friend bool operator==(const DocLine&, const DocLine&) = default;
};
struct DocCircle {
    Vec2 center;
    double radius = 0.0;
    EntityProps props{};
    friend bool operator==(const DocCircle&, const DocCircle&) = default;
};
struct DocArc {
    Vec2 center;
    double radius = 0.0;
    double start_angle = 0.0;
    double end_angle = 0.0;
    EntityProps props{};
    friend bool operator==(const DocArc&, const DocArc&) = default;
};
struct DocPolyline {
    std::vector<Vec2> points;
    bool closed = false;
    EntityProps props{};
    std::vector<double> bulges = {}; ///< per-vertex arc bulges (empty = all straight)
    friend bool operator==(const DocPolyline&, const DocPolyline&) = default;
};
struct DocSpline {
    std::vector<Vec2> control_points;
    std::uint32_t degree = 3;
    EntityProps props{};
    friend bool operator==(const DocSpline&, const DocSpline&) = default;
};
struct DocText {
    Vec2 pos;
    double height = 2.5;
    double rotation = 0.0;
    std::uint8_t justify = 0;
    std::string content;
    EntityProps props{};
    friend bool operator==(const DocText&, const DocText&) = default;
};
struct DocDim {
    std::uint8_t type = 0;
    Vec2 a;
    Vec2 b;
    Vec2 line_pt;
    std::uint16_t style = 0;
    EntityProps props{};
    friend bool operator==(const DocDim&, const DocDim&) = default;
};
struct DocLeader {
    Vec2 tip;
    Vec2 knee;
    double text_height = 2.5;
    std::uint16_t style = 0;
    std::string content;
    EntityProps props{};
    friend bool operator==(const DocLeader&, const DocLeader&) = default;
};
struct DocMText {
    MTextBlock block;
    std::string content;
    EntityProps props{};
    friend bool operator==(const DocMText&, const DocMText&) = default;
};
struct DocMLeader {
    std::vector<Vec2> vertices;
    std::uint16_t style = 0;
    MTextBlock block;
    std::string content;
    EntityProps props{};
    friend bool operator==(const DocMLeader&, const DocMLeader&) = default;
};

/// A complete, serializable 2D drawing: metadata, the layer table, and every
/// entity family with its properties.
struct Document {
    std::uint32_t format_version = kFormatVersion;
    std::string units = "unitless";

    std::vector<Layer> layers{Layer{"0"}}; // layer 0 always present
    std::uint16_t current_layer = 0;
    std::vector<DimStyle> dimstyles{DimStyle{"Standard"}}; // index 0 always present

    std::vector<DocPoint> points;
    std::vector<DocLine> lines;
    std::vector<DocCircle> circles;
    std::vector<DocArc> arcs;
    std::vector<DocPolyline> polylines;
    std::vector<DocSpline> splines;
    std::vector<DocText> texts;
    std::vector<DocDim> dims;
    std::vector<DocLeader> leaders;
    std::vector<DocMText> mtexts;
    std::vector<DocMLeader> mleaders;

    [[nodiscard]] std::size_t entity_count() const noexcept {
        return points.size() + lines.size() + circles.size() + arcs.size() + polylines.size() +
               splines.size() + texts.size() + dims.size() + leaders.size() + mtexts.size() +
               mleaders.size();
    }
    [[nodiscard]] bool empty() const noexcept { return entity_count() == 0; }

    friend bool operator==(const Document&, const Document&) = default;
};

/// Reads every live entity out of a store into a Document (geometry thread).
[[nodiscard]] Document document_from_store(const GeometryStore& store);

/// Adds all of a Document's entities to a store. The caller is responsible for
/// clearing the store first (and rebuilding any spatial index afterwards).
void populate_store(GeometryStore& store, const Document& doc);

} // namespace musacad::core::io

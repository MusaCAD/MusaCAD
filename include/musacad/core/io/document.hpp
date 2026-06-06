#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "musacad/core/math/math.hpp"
#include "musacad/core/properties.hpp"

namespace musacad::core {
class GeometryStore;
}

namespace musacad::core::io {

/// The current native document format version. v1 had geometry only; v2 adds the
/// layer table + per-entity properties. Readers reject newer versions; v1 files
/// load with everything on layer 0, fully ByLayer.
inline constexpr std::uint32_t kFormatVersion = 2;

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
    friend bool operator==(const DocPolyline&, const DocPolyline&) = default;
};
struct DocSpline {
    std::vector<Vec2> control_points;
    std::uint32_t degree = 3;
    EntityProps props{};
    friend bool operator==(const DocSpline&, const DocSpline&) = default;
};

/// A complete, serializable 2D drawing: metadata, the layer table, and every
/// entity family with its properties.
struct Document {
    std::uint32_t format_version = kFormatVersion;
    std::string units = "unitless";

    std::vector<Layer> layers{Layer{"0"}}; // layer 0 always present
    std::uint16_t current_layer = 0;

    std::vector<DocPoint> points;
    std::vector<DocLine> lines;
    std::vector<DocCircle> circles;
    std::vector<DocArc> arcs;
    std::vector<DocPolyline> polylines;
    std::vector<DocSpline> splines;

    [[nodiscard]] std::size_t entity_count() const noexcept {
        return points.size() + lines.size() + circles.size() + arcs.size() + polylines.size() +
               splines.size();
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

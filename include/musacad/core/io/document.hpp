#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::core {
class GeometryStore;
}

namespace musacad::core::io {

/// The current native document format version. Bump when the on-disk layout
/// changes; readers reject versions newer than they understand.
inline constexpr std::uint32_t kFormatVersion = 1;

// Self-contained, pool-free records for serialization. Unlike the store's SoA
// Data structs these carry their own vertices and no generational handles -- the
// Document is the portable intermediate representation for save/load and DXF.
struct DocLine {
    Vec2 a;
    Vec2 b;
    friend bool operator==(const DocLine&, const DocLine&) = default;
};
struct DocCircle {
    Vec2 center;
    double radius = 0.0;
    friend bool operator==(const DocCircle&, const DocCircle&) = default;
};
struct DocArc {
    Vec2 center;
    double radius = 0.0;
    double start_angle = 0.0;
    double end_angle = 0.0;
    friend bool operator==(const DocArc&, const DocArc&) = default;
};
struct DocPolyline {
    std::vector<Vec2> points;
    bool closed = false;
    friend bool operator==(const DocPolyline&, const DocPolyline&) = default;
};
struct DocSpline {
    std::vector<Vec2> control_points;
    std::uint32_t degree = 3;
    friend bool operator==(const DocSpline&, const DocSpline&) = default;
};

/// A complete, serializable 2D drawing: metadata plus every entity family.
struct Document {
    std::uint32_t format_version = kFormatVersion;
    std::string units = "unitless";

    std::vector<Vec2> points;
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

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "musacad/core/math/math.hpp"
#include "musacad/core/mtext_block.hpp"
#include "musacad/core/page_setup.hpp"
#include "musacad/core/properties.hpp"

namespace musacad::core {
class GeometryStore;
}

namespace musacad::core::io {

/// The current native document format version. v1: geometry only. v2: layer table
/// + per-entity properties. v3: text + dimensions + dimension styles. v4: leaders +
/// expanded DIMSTYLE (per-element colours, dim lineweight, arrow types). v5: polyline
/// per-vertex arc bulges. v6: MTEXT (paragraph text) + MLEADER (editable leaders).
/// v7: global LTSCALE. Readers reject newer versions; older files load fine (no
/// layers => layer 0; no dims; no bulges => straight polylines; no mtext/mleader;
/// no LTSCALE => 1.0). v8: per-dimension style overrides (no overrides => ByStyle).
/// v9: block definitions (BLOCKDEF..ENDBLOCKDEF) + block references (INSERT). Older
/// files have no blocks; the keys simply never appear. v10: a font-name line after the
/// content of TEXT/MTEXT/LEADER/MLEADER records (older files have none => stroke font).
/// v11: saved plot page setups (PAGESETUP records; older files have none). v12: per-entity
/// CELTSCALE (linetype scale) for line/circle/arc/polyline, written as trailing CELTSCALE
/// records (older files have none => 1.0); the LTSCALE global is still the LTSCALE record.
inline constexpr std::uint32_t kFormatVersion = 12;

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
    double celtscale = 1.0; ///< per-entity linetype scale (CELTSCALE; v12+)
    friend bool operator==(const DocLine&, const DocLine&) = default;
};
struct DocCircle {
    Vec2 center;
    double radius = 0.0;
    EntityProps props{};
    double celtscale = 1.0; ///< per-entity linetype scale (CELTSCALE; v12+)
    friend bool operator==(const DocCircle&, const DocCircle&) = default;
};
struct DocArc {
    Vec2 center;
    double radius = 0.0;
    double start_angle = 0.0;
    double end_angle = 0.0;
    EntityProps props{};
    double celtscale = 1.0; ///< per-entity linetype scale (CELTSCALE; v12+)
    friend bool operator==(const DocArc&, const DocArc&) = default;
};
struct DocPolyline {
    std::vector<Vec2> points;
    bool closed = false;
    EntityProps props{};
    std::vector<double> bulges = {}; ///< per-vertex arc bulges (empty = all straight)
    double celtscale = 1.0;          ///< per-entity linetype scale (CELTSCALE; v12+)
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
    std::string font{}; ///< font name ("" = stroke "Standard")
    friend bool operator==(const DocText&, const DocText&) = default;
};
struct DocDim {
    std::uint8_t type = 0;
    Vec2 a;
    Vec2 b;
    Vec2 line_pt;
    std::uint16_t style = 0;
    EntityProps props{};
    DimOverrides overrides{}; ///< per-dimension style overrides (v8+)
    friend bool operator==(const DocDim&, const DocDim&) = default;
};
struct DocLeader {
    Vec2 tip;
    Vec2 knee;
    double text_height = 2.5;
    std::uint16_t style = 0;
    std::string content;
    EntityProps props{};
    std::string font{}; ///< font name ("" = stroke "Standard")
    friend bool operator==(const DocLeader&, const DocLeader&) = default;
};
struct DocMText {
    MTextBlock block;
    std::string content;
    EntityProps props{};
    std::string font{}; ///< font name ("" = stroke "Standard"); maps to block.font index
    friend bool operator==(const DocMText&, const DocMText&) = default;
};
struct DocMLeader {
    std::vector<Vec2> vertices;
    std::uint16_t style = 0;
    MTextBlock block;
    std::string content;
    EntityProps props{};
    std::string font{}; ///< font name ("" = stroke "Standard"); maps to block.font index
    friend bool operator==(const DocMLeader&, const DocMLeader&) = default;
};

/// A block reference: a transform (insertion point + X/Y scale + rotation) and the
/// name of the block definition it places. Name-based (DXF/interchange convention);
/// resolved to a block-table index when loaded into the store.
struct DocInsert {
    std::string block_name;
    Vec2 pos;
    double scale_x = 1.0;
    double scale_y = 1.0;
    double rotation = 0.0; ///< radians, CCW
    EntityProps props{};
    friend bool operator==(const DocInsert&, const DocInsert&) = default;
};

/// A block definition: a name, a base point, and self-contained geometry (the
/// importable subset, mirroring the store's BlockContent). Inserts may nest.
struct DocBlockDef {
    std::string name;
    Vec2 base{0.0, 0.0};
    std::vector<DocLine> lines;
    std::vector<DocCircle> circles;
    std::vector<DocArc> arcs;
    std::vector<DocPolyline> polylines;
    std::vector<DocText> texts;
    std::vector<DocMText> mtexts;
    std::vector<DocInsert> inserts; ///< nested block references
    friend bool operator==(const DocBlockDef&, const DocBlockDef&) = default;
};

/// A complete, serializable 2D drawing: metadata, the layer table, and every
/// entity family with its properties.
struct Document {
    std::uint32_t format_version = kFormatVersion;
    std::string units = "unitless";

    std::vector<Layer> layers{Layer{"0"}}; // layer 0 always present
    std::uint16_t current_layer = 0;
    std::vector<DimStyle> dimstyles{DimStyle{"Standard"}}; // index 0 always present
    double ltscale = 1.0;                                  // global linetype scale (LTSCALE)
    std::vector<PageSetup> page_setups;                    // saved PLOT configurations (v11)

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
    std::vector<DocInsert> inserts;        ///< model-space block references
    std::vector<DocBlockDef> block_defs;   ///< block-definition table (not in entity_count)

    [[nodiscard]] std::size_t entity_count() const noexcept {
        return points.size() + lines.size() + circles.size() + arcs.size() + polylines.size() +
               splines.size() + texts.size() + dims.size() + leaders.size() + mtexts.size() +
               mleaders.size() + inserts.size();
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

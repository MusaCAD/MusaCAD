// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "musacad/core/layout.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/mtext_block.hpp"
#include "musacad/core/named_view.hpp"
#include "musacad/core/text_style.hpp"
#include "musacad/core/units.hpp"
#include "musacad/core/page_setup.hpp"
#include "musacad/core/properties.hpp"
#include "musacad/core/table_types.hpp"

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
/// v13: per-leader arrow override block appended to LEADER/MLEADER records (the same
/// override block dimensions use; detected by token count, older files => ByStyle).
/// v14: HATCH entities (boundary loops + pattern name/scale/angle/origin). Older files
/// simply have no HATCH records.
/// v15: dimension text decoration -- three trailing fields on the DIM record (tolerance
/// mode, upper, lower) plus a prefix line and a suffix line after it. Detected by token
/// count (34 vs the v8 31 vs the pre-v8 16); v1-v14 dimensions load undecorated.
/// v16: ISO 129-1 narrow-dimension fit. The shared DimOverrides block gains a 16th
/// field (text_fit), so DIM goes 34 -> 35 tokens and LEADER/MLEADER 29 -> 30; DIMSTYLE
/// gains text_fit BEFORE its name (the name absorbs the rest of the line, so a trailing
/// field would be ambiguous) and is read by version, not token count. v1-v15 files load
/// with text_fit = Auto, which is the default -- so older drawings simply stop colliding.
/// v20: TABLE entities + the TABLESTYLE table. A TABLE record carries the grid shape and
/// the column/row sizes, then one line per cell (`span_cols span_rows align` followed by
/// the raw text on the next line, since it may contain spaces). Older files simply have
/// no TABLESTYLE/TABLE records.
/// v19: dimension text override + text offset -- two trailing fields on the DIM record
/// (the offset) plus a third content line (the raw override, which may contain spaces).
/// Detected by token count; v1-v18 dimensions load with no override and a zero offset,
/// i.e. exactly today's derived placement.
/// v18: raster IMAGE entities. IMAGEDEF records carry the payload (an external source
/// path relative to the drawing, or base64 chunked across following lines because the
/// format is line-oriented); IMAGE records place a definition with a transform + clip.
/// Older files simply have no IMAGEDEF/IMAGE records.
/// v17: GD&T entities -- FCF records (cell count, then one cell string per following
/// line) and DATUM records. Older files simply have no FCF/DATUM records.
inline constexpr std::uint32_t kFormatVersion = 33;

// Self-contained, pool-free records for serialization: own vertices, no
// generational handles, plus the entity's EntityProps (layer + overrides).
struct DocPoint {
    Vec2 p;
    EntityProps props{};
    friend bool operator==(const DocPoint&, const DocPoint&) = default;
};
/// A group (v24). Members are (entity kind, index among that kind in this document's
/// order), which is the order document_from_store writes and populate_store re-adds.
struct DocGroup {
    std::string name;
    std::string description;
    bool selectable = true;
    std::vector<std::pair<std::uint8_t, std::uint32_t>> members;
    friend bool operator==(const DocGroup&, const DocGroup&) = default;
};
/// An ellipse / elliptical arc (v22): centre, major half-axis, ratio, parameter range.
struct DocEllipse {
    Vec2 center;
    Vec2 major{1.0, 0.0};
    double ratio = 1.0;
    double start = 0.0;
    double end = 6.283185307179586;
    EntityProps props{};
    friend bool operator==(const DocEllipse&, const DocEllipse&) = default;
};
/// A construction line (XLINE/RAY): base, unit direction, and whether it is a RAY (v21).
struct DocXline {
    Vec2 base;
    Vec2 dir{1.0, 0.0};
    bool ray = false;
    EntityProps props{};
    friend bool operator==(const DocXline&, const DocXline&) = default;
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
    std::uint16_t style = 0; ///< text style index (v26)
    friend bool operator==(const DocText&, const DocText&) = default;
};
/// ATTDEF (v28): a text-like definition showing its tag; prompt/default/modes ride along.
struct DocAttDef {
    DocText text; ///< placement, props, font, style; `content` = the tag
    std::string prompt;
    std::string def;
    std::uint8_t flags = 0;
    friend bool operator==(const DocAttDef&, const DocAttDef&) = default;
};
/// One attribute value on a block reference (v28), matched to the definition by tag.
struct DocAttrib {
    std::string tag;
    std::string value;
    friend bool operator==(const DocAttrib&, const DocAttrib&) = default;
};
struct DocDim {
    std::uint8_t type = 0;
    Vec2 a;
    Vec2 b;
    Vec2 line_pt;
    std::uint16_t style = 0;
    EntityProps props{};
    DimOverrides overrides{}; ///< per-dimension style overrides (v8+)
    // v15 text decoration: raw prefix/suffix (codes unexpanded) + the tolerance mode
    // and deviations. The measured VALUE is never serialised -- it is recomputed from
    // the def points on load, so a .musa can never carry a value that lies.
    std::string prefix;
    std::string suffix;
    DimTolerance tol{};
    /// v19: the AutoCAD-style text override (raw; `<>` = the measurement) and the
    /// author's displacement of the label from its derived position.
    std::string text_override;
    Vec2 text_offset{};
    double aux = 0.0; ///< v23: the extra datum of ordinate / jogged / arc-length dims
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
    DimOverrides overrides{}; ///< per-leader arrow override (v13+; no overrides => ByStyle)
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
    DimOverrides overrides{}; ///< per-leader arrow override (v13+; no overrides => ByStyle)
    friend bool operator==(const DocMLeader&, const DocMLeader&) = default;
};

/// A HATCH: closed boundary loops (loop 0 = outer, the rest islands) + a pattern name
/// ("SOLID" = filled) and pattern scale / angle(radians) / origin.
struct DocHatch {
    std::vector<std::vector<Vec2>> loops;
    std::string pattern_name = "SOLID";
    double pattern_scale = 1.0;
    double pattern_angle = 0.0; ///< radians, CCW
    Vec2 pattern_origin{};
    EntityProps props{};
    Rgb color2{}; ///< GRADIENT second colour (v27)
    friend bool operator==(const DocHatch&, const DocHatch&) = default;
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
    std::vector<DocAttrib> attribs; ///< attribute values (v28)
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
    std::vector<DocAttDef> attdefs; ///< attribute definitions (v28)
    std::vector<DocInsert> inserts; ///< nested block references
    std::string xref_path;          ///< XREF source drawing (v32); "" for an ordinary block
    friend bool operator==(const DocBlockDef&, const DocBlockDef&) = default;
};

/// A GD&T feature control frame in the serializable IR: the cells inline (pool-free).
struct DocFcf {
    std::vector<std::string> cells;
    Vec2 pos;
    double rotation = 0.0;
    std::uint16_t style = 0;
    EntityProps props{};
    DimOverrides overrides{};
    friend bool operator==(const DocFcf&, const DocFcf&) = default;
};

/// A GD&T datum feature symbol in the serializable IR.
struct DocDatum {
    std::string letter;
    Vec2 tip;
    Vec2 pos;
    double rotation = 0.0;
    std::uint16_t style = 0;
    EntityProps props{};
    DimOverrides overrides{};
    friend bool operator==(const DocDatum&, const DocDatum&) = default;
};

/// A table cell in the serializable IR: content inline (pool-free).
struct DocTableCell {
    std::string text;
    std::uint16_t span_cols = 1;
    std::uint16_t span_rows = 1;
    std::uint8_t align = 1;
    friend bool operator==(const DocTableCell&, const DocTableCell&) = default;
};

/// A TABLE in the serializable IR.
struct DocTable {
    std::uint16_t rows = 0;
    std::uint16_t cols = 0;
    bool has_title = false;
    bool has_header = false;
    Vec2 pos;
    double rotation = 0.0;
    std::uint16_t style = 0;
    std::vector<double> col_widths;
    std::vector<double> row_heights;
    std::vector<DocTableCell> cells; ///< row-major, rows*cols
    EntityProps props{};
    friend bool operator==(const DocTable&, const DocTable&) = default;
};

/// A raster image definition in the serializable IR (the payload lives here, once).
struct DocImageDef {
    std::string source;
    std::vector<std::uint8_t> bytes;
    std::uint32_t pixel_w = 0;
    std::uint32_t pixel_h = 0;
    friend bool operator==(const DocImageDef&, const DocImageDef&) = default;
};

/// A placed raster image in the serializable IR.
struct DocImage {
    std::uint16_t def = 0;
    Vec2 pos;
    double width = 1.0;
    double height = 1.0;
    double rotation = 0.0;
    bool clipped = false;
    double clip_u0 = 0.0;
    double clip_v0 = 0.0;
    double clip_u1 = 1.0;
    double clip_v1 = 1.0;
    EntityProps props{};
    std::vector<Vec2> clip_polygon; ///< polygonal clip in image fractions (v33), or empty
    friend bool operator==(const DocImage&, const DocImage&) = default;
};

/// A paper-space viewport (v31; see ViewportData).
struct DocViewport {
    Vec2 center;
    double width = 100.0;
    double height = 60.0;
    Vec2 view_center;
    double scale = 1.0;
    bool on = true;
    EntityProps props{};
    friend bool operator==(const DocViewport&, const DocViewport&) = default;
};

/// A complete, serializable 2D drawing: metadata, the layer table, and every
/// entity family with its properties.
struct Document {
    std::uint32_t format_version = kFormatVersion;
    std::string units = "unitless";
    DrawingUnits display_units{}; ///< UNITS: display formats (v25)
    std::vector<TextStyle> text_styles; ///< STYLE table (v26; [0] Standard; not in entity_count)
    std::uint16_t current_text_style = 0;
    bool wipeout_frames = true; ///< WIPEOUTFRAME (v27)
    std::uint8_t attdisp = 0;   ///< ATTDISP (v28): 0 Normal, 1 ON, 2 OFF
    std::uint8_t image_frame = 1; ///< IMAGEFRAME (v29): 0 hidden, 1 shown and plotted, 2 shown only
    std::vector<Layout> layouts;  ///< the layout tabs (v30); entities refer to them by EntityProps::space
    std::uint8_t active_space = 0; ///< 0 model, else a layout id (v30)

    std::vector<Layer> layers{Layer{"0"}}; // layer 0 always present
    std::uint16_t current_layer = 0;
    std::vector<DimStyle> dimstyles{DimStyle{"Standard"}}; // index 0 always present
    double ltscale = 1.0;                                  // global linetype scale (LTSCALE)
    std::vector<PageSetup> page_setups;                    // saved PLOT configurations (v11)
    std::vector<NamedView> views;         ///< named views (v24; not in entity_count)
    std::vector<DocGroup> groups;         ///< groups (v24; members by kind + order; not in entity_count)

    std::vector<DocPoint> points;
    std::vector<DocXline> xlines;        ///< construction lines (v21)
    std::vector<DocEllipse> ellipses;    ///< ellipses / elliptical arcs (v22)
    std::vector<DocLine> lines;
    std::vector<DocCircle> circles;
    std::vector<DocArc> arcs;
    std::vector<DocPolyline> polylines;
    std::vector<DocSpline> splines;
    std::vector<DocText> texts;
    std::vector<DocAttDef> attdefs; ///< ATTDEF entities in model space (v28)
    std::vector<DocDim> dims;
    std::vector<DocLeader> leaders;
    std::vector<DocMText> mtexts;
    std::vector<DocMLeader> mleaders;
    std::vector<DocHatch> hatches;        ///< filled / patterned regions (v14)
    std::vector<DocFcf> fcfs;             ///< GD&T feature control frames (v17)
    std::vector<DocDatum> datums;         ///< GD&T datum feature symbols (v17)
    std::vector<DocImage> images;         ///< placed raster images (v18)
    std::vector<DocViewport> viewports;   ///< paper-space viewports (v31)
    std::vector<DocTable> tables;         ///< tables (v20)
    std::vector<TableStyle> table_styles; ///< table-style table (not in entity_count)
    std::vector<DocImageDef> image_defs;  ///< image-definition table (not in entity_count)
    std::vector<DocInsert> inserts;        ///< model-space block references
    std::vector<DocBlockDef> block_defs;   ///< block-definition table (not in entity_count)

    [[nodiscard]] std::size_t entity_count() const noexcept {
        return points.size() + xlines.size() + ellipses.size() + lines.size() + circles.size() +
               arcs.size() +
               polylines.size() +
               splines.size() + texts.size() + attdefs.size() + dims.size() + leaders.size() + mtexts.size() +
               mleaders.size() + hatches.size() + inserts.size() + fcfs.size() + datums.size() +
               images.size() + viewports.size() + tables.size();
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

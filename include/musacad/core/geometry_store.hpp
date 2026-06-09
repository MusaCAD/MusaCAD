#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "musacad/core/entity_handle.hpp"
#include "musacad/core/generational_arena.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/mtext_block.hpp"
#include "musacad/core/properties.hpp"

namespace musacad::core {

// ---------------------------------------------------------------------------
// Per-primitive SoA records. Fixed-size primitives store their data inline;
// variable-length primitives (polyline, spline) store an (offset, count) view
// into a shared contiguous vertex pool.
// ---------------------------------------------------------------------------

// Every primitive carries an EntityProps column (layer ref + ByLayer/override
// colour, linetype, lineweight). Defaults to layer 0, fully ByLayer.
struct PointData {
    Vec2 p;
    EntityProps props{};
};

struct LineData {
    Vec2 a;
    Vec2 b;
    EntityProps props{};
};

struct CircleData {
    Vec2 center;
    double radius;
    EntityProps props{};
};

/// Arc on a circle, swept counter-clockwise from start_angle to end_angle
/// (radians). end_angle may exceed start_angle; the kernel normalises the sweep.
struct ArcData {
    Vec2 center;
    double radius;
    double start_angle;
    double end_angle;
    EntityProps props{};
};

struct PolylineData {
    static constexpr std::uint32_t kNoBulges = 0xFFFFFFFFu; ///< all segments straight

    std::uint32_t offset; ///< first vertex index in the polyline vertex pool
    std::uint32_t count;  ///< number of vertices
    std::uint32_t bulge_offset = kNoBulges; ///< first of `count` bulges, or kNoBulges
    bool closed;
    EntityProps props{};
};

struct SplineData {
    std::uint32_t offset; ///< first control point in the spline pool
    std::uint32_t count;  ///< number of control points
    std::uint32_t degree;
    EntityProps props{};
};

/// Single-line text. The string lives in a shared char pool (offset, len) like
/// polyline vertices -- no fat inline buffer on the per-entity struct.
struct TextData {
    Vec2 pos;                  ///< insertion point (justification anchor, on the baseline)
    double height = 1.0;
    double rotation = 0.0;     ///< radians, CCW
    std::uint8_t justify = 0;  ///< 0 = left, 1 = center, 2 = right
    std::uint32_t str_offset = 0;
    std::uint32_t str_len = 0;
    EntityProps props{};
};

/// A composite dimension. The measured value is COMPUTED from the definition
/// points (a, b) -- never baked -- so editing them updates the dimension. `line_pt`
/// positions the dimension line; `style` indexes the dimstyle table. DimType lives
/// in properties.hpp.
struct DimData {
    DimType type = DimType::Linear;
    Vec2 a;       ///< first definition point
    Vec2 b;       ///< second definition point
    Vec2 line_pt; ///< a point the dimension line passes through (placement)
    std::uint16_t style = 0;
    EntityProps props{};
    DimOverrides overrides{}; ///< per-dimension style overrides (ByStyle by default)
};

/// A quick leader: an arrowhead at `tip`, a leader line to `knee`, and a text
/// label anchored at `knee`. Shares the dimstyle arrow + the stroke font.
struct LeaderData {
    Vec2 tip;              ///< arrowhead point (what the leader points at)
    Vec2 knee;            ///< landing / text anchor
    double text_height = 2.5;
    std::uint16_t style = 0; ///< dimstyle (for arrow type/size + colours)
    std::uint32_t str_offset = 0;
    std::uint32_t str_len = 0;
    EntityProps props{};
};

/// Multi-line paragraph text (MTEXT): a formatting block + layer properties.
struct MTextData {
    MTextBlock text;
    EntityProps props{};
};

/// An editable leader (QLEADER): a leader polyline (vertices in the shared pool;
/// vertex 0 is the arrow tip, the last is the landing) drawn with a dimstyle
/// arrow, plus an OWNED paragraph-text label (the same MTextBlock + layout as
/// MTEXT -- no text fork). Ownership is the association: moving the leader moves
/// the text; there is no cross-entity reference to dangle.
struct MLeaderData {
    std::uint32_t vtx_offset = 0; ///< first leader vertex in the polyline pool
    std::uint32_t vtx_count = 0;  ///< number of leader vertices (>= 1)
    std::uint16_t style = 0;      ///< dimstyle (arrow type/size + colours)
    MTextBlock text;              ///< the attached label (computed layout)
    EntityProps props{};
};

/// Structure-of-Arrays geometry storage. Each primitive kind lives in its own
/// GenerationalArena; variable-length vertex data lives in shared pools. All
/// access is non-virtual.
///
/// Note (Phase 2): removing a polyline/spline frees its arena slot but leaves
/// its vertices in the pool, so other handles' (offset, count) views stay
/// valid. Pool compaction is a future optimisation.
class GeometryStore {
public:
    // --- creation (props default to layer 0, fully ByLayer) -----------------
    EntityHandle add_point(Vec2 p, EntityProps props = {});
    EntityHandle add_line(Vec2 a, Vec2 b, EntityProps props = {});
    EntityHandle add_circle(Vec2 center, double radius, EntityProps props = {});
    EntityHandle add_arc(Vec2 center, double radius, double start_angle, double end_angle,
                         EntityProps props = {});
    EntityHandle add_polyline(std::span<const Vec2> vertices, bool closed, EntityProps props = {});
    /// Polyline with per-vertex arc bulges (b = tan(theta/4); 0 = straight). `bulges`
    /// must be empty (all straight) or the same length as `vertices`.
    EntityHandle add_polyline(std::span<const Vec2> vertices, std::span<const double> bulges,
                              bool closed, EntityProps props = {});
    EntityHandle add_spline(std::span<const Vec2> control_points, std::uint32_t degree,
                            EntityProps props = {});
    EntityHandle add_text(Vec2 pos, double height, double rotation, std::uint8_t justify,
                          std::string_view content, EntityProps props = {});
    EntityHandle add_dimension(DimType type, Vec2 a, Vec2 b, Vec2 line_pt, std::uint16_t style,
                               EntityProps props = {}, DimOverrides overrides = {});
    EntityHandle add_leader(Vec2 tip, Vec2 knee, double text_height, std::uint16_t style,
                            std::string_view content, EntityProps props = {});
    /// Multi-line paragraph text. `block.str_offset/str_len` are ignored; `content`
    /// is copied into the shared string pool and the range is recorded.
    EntityHandle add_mtext(const MTextBlock& block, std::string_view content,
                           EntityProps props = {});
    /// Editable leader with an owned paragraph label. `vertices[0]` is the arrow tip.
    EntityHandle add_mleader(std::span<const Vec2> vertices, std::uint16_t style,
                             const MTextBlock& text, std::string_view content,
                             EntityProps props = {});

    // --- removal / validity -------------------------------------------------
    bool remove(EntityHandle handle) noexcept;
    [[nodiscard]] bool is_valid(EntityHandle handle) const noexcept;
    [[nodiscard]] std::size_t live_count() const noexcept;

    /// Drops every entity and vertex pool, leaving an empty store (used by
    /// New / Open). Generations are not preserved -- handles are runtime-only.
    void clear() noexcept;

    // --- typed accessors (nullptr if invalid or wrong kind) -----------------
    [[nodiscard]] const PointData* point(EntityHandle h) const noexcept;
    [[nodiscard]] const LineData* line(EntityHandle h) const noexcept;
    [[nodiscard]] const CircleData* circle(EntityHandle h) const noexcept;
    [[nodiscard]] const ArcData* arc(EntityHandle h) const noexcept;
    [[nodiscard]] const PolylineData* polyline(EntityHandle h) const noexcept;
    [[nodiscard]] const SplineData* spline(EntityHandle h) const noexcept;
    [[nodiscard]] const TextData* text(EntityHandle h) const noexcept;
    [[nodiscard]] const DimData* dimension(EntityHandle h) const noexcept;
    [[nodiscard]] const LeaderData* leader(EntityHandle h) const noexcept;
    [[nodiscard]] const MTextData* mtext(EntityHandle h) const noexcept;
    [[nodiscard]] const MLeaderData* mleader(EntityHandle h) const noexcept;
    /// The string content of a text entity.
    [[nodiscard]] std::string_view string_of(const TextData& t) const noexcept;
    [[nodiscard]] std::string_view string_of(const LeaderData& l) const noexcept;
    /// Content of a paragraph-text block (MTEXT entity or QLEADER label).
    [[nodiscard]] std::string_view string_of(const MTextBlock& b) const noexcept;
    /// Leader-polyline vertices of an MLeader.
    [[nodiscard]] std::span<const Vec2> vertices_of(const MLeaderData& m) const noexcept;

    // --- batch arena access (const; includes dead slots) --------------------
    [[nodiscard]] const GenerationalArena<PointData>& points() const noexcept { return points_; }
    [[nodiscard]] const GenerationalArena<LineData>& lines() const noexcept { return lines_; }
    [[nodiscard]] const GenerationalArena<CircleData>& circles() const noexcept { return circles_; }
    [[nodiscard]] const GenerationalArena<ArcData>& arcs() const noexcept { return arcs_; }
    [[nodiscard]] const GenerationalArena<PolylineData>& polylines() const noexcept {
        return polylines_;
    }
    [[nodiscard]] const GenerationalArena<SplineData>& splines() const noexcept { return splines_; }
    [[nodiscard]] const GenerationalArena<TextData>& texts() const noexcept { return texts_; }
    [[nodiscard]] const GenerationalArena<DimData>& dimensions() const noexcept { return dims_; }
    [[nodiscard]] const GenerationalArena<LeaderData>& leaders() const noexcept { return leaders_; }
    [[nodiscard]] const GenerationalArena<MTextData>& mtexts() const noexcept { return mtexts_; }
    [[nodiscard]] const GenerationalArena<MLeaderData>& mleaders() const noexcept {
        return mleaders_;
    }

    // --- dimension styles ("Standard" always at index 0) --------------------
    [[nodiscard]] const std::vector<DimStyle>& dimstyles() const noexcept { return dimstyles_; }
    [[nodiscard]] const DimStyle* dimstyle(std::uint16_t index) const noexcept;
    std::uint16_t add_dimstyle(const DimStyle& style);
    bool set_dimstyle(std::uint16_t index, const DimStyle& style);
    /// Replaces the dimstyle table (Open/Import); ensures "Standard" at index 0.
    void set_dimstyle_table(std::vector<DimStyle> styles);

    // --- vertex pools -------------------------------------------------------
    [[nodiscard]] std::span<const Vec2> polyline_vertices() const noexcept {
        return polyline_pool_;
    }
    [[nodiscard]] std::span<const Vec2> spline_control_pool() const noexcept {
        return spline_pool_;
    }
    /// The vertex view for a given polyline/spline record.
    [[nodiscard]] std::span<const Vec2> vertices_of(const PolylineData& pl) const noexcept;
    /// Per-vertex bulges for a polyline, or an empty span when all segments are
    /// straight. Same length as vertices_of() when non-empty.
    [[nodiscard]] std::span<const double> bulges_of(const PolylineData& pl) const noexcept;
    [[nodiscard]] std::span<const Vec2> control_points_of(const SplineData& sp) const noexcept;

    void reserve_lines(std::size_t n) { lines_.reserve(n); }

    // --- per-entity properties ----------------------------------------------
    /// The entity's property attributes (nullptr if invalid). Read-only.
    [[nodiscard]] const EntityProps* props(EntityHandle h) const noexcept;
    /// Replaces an entity's property attributes. Returns false if invalid.
    bool set_props(EntityHandle h, const EntityProps& props) noexcept;

    // --- layer table --------------------------------------------------------
    // Layer 0 always exists at index 0. Layers are few; a contiguous vector is
    // plenty. Indices are stable for a session's lifetime (no mid-session
    // removal reindex beyond the removed slot -- see remove_layer).
    [[nodiscard]] const std::vector<Layer>& layers() const noexcept { return layers_; }
    [[nodiscard]] std::size_t layer_count() const noexcept { return layers_.size(); }
    [[nodiscard]] const Layer* layer(std::uint16_t index) const noexcept;
    [[nodiscard]] std::uint16_t current_layer() const noexcept { return current_layer_; }
    void set_current_layer(std::uint16_t index) noexcept;

    /// Global linetype scale (AutoCAD LTSCALE): multiplies every dash pattern. Dash
    /// geometry is derived at snapshot from this + the stored linetype (not baked).
    [[nodiscard]] double ltscale() const noexcept { return ltscale_; }
    void set_ltscale(double scale) noexcept { ltscale_ = scale > 0.0 ? scale : ltscale_; }

    /// Adds a layer, or returns the existing index if the name is already taken
    /// (layer names are unique, AutoCAD-style).
    std::uint16_t add_layer(const Layer& layer);
    /// Replaces the entire layer table (used by Open/Import). Ensures at least
    /// layer 0 exists and clamps the current index.
    void set_layer_table(std::vector<Layer> layers, std::uint16_t current);
    /// Updates the layer at `index` (keeps the name unique; ignores a rename of
    /// layer 0). Returns false if the index is invalid.
    bool set_layer(std::uint16_t index, const Layer& layer);
    /// True if any live entity references the layer.
    [[nodiscard]] bool layer_in_use(std::uint16_t index) const noexcept;
    /// Removes a layer. Fails (returns false) for layer 0, the current layer, or
    /// a layer that still contains entities (AutoCAD rule). Remaining entities'
    /// layer indices above the removed one are shifted down to stay valid.
    bool remove_layer(std::uint16_t index);

private:
    void shift_layer_refs_after_removal(std::uint16_t removed) noexcept;

    GenerationalArena<PointData> points_;
    GenerationalArena<LineData> lines_;
    GenerationalArena<CircleData> circles_;
    GenerationalArena<ArcData> arcs_;
    GenerationalArena<PolylineData> polylines_;
    GenerationalArena<SplineData> splines_;
    GenerationalArena<TextData> texts_;
    GenerationalArena<DimData> dims_;
    GenerationalArena<LeaderData> leaders_;
    GenerationalArena<MTextData> mtexts_;
    GenerationalArena<MLeaderData> mleaders_;

    std::vector<Vec2> polyline_pool_;
    std::vector<double> bulge_pool_; // per-vertex polyline arc bulges
    std::vector<Vec2> spline_pool_;
    std::vector<char> string_pool_; // text content

    std::vector<Layer> layers_{Layer{"0"}}; // layer 0 always present
    std::uint16_t current_layer_ = 0;
    std::vector<DimStyle> dimstyles_{DimStyle{"Standard"}}; // index 0 always present
    double ltscale_ = 1.0;                                  // global linetype scale
};

} // namespace musacad::core

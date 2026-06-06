#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "musacad/core/entity_handle.hpp"
#include "musacad/core/generational_arena.hpp"
#include "musacad/core/math/math.hpp"
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
    std::uint32_t offset; ///< first vertex index in the polyline vertex pool
    std::uint32_t count;  ///< number of vertices
    bool closed;
    EntityProps props{};
};

struct SplineData {
    std::uint32_t offset; ///< first control point in the spline pool
    std::uint32_t count;  ///< number of control points
    std::uint32_t degree;
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
    EntityHandle add_spline(std::span<const Vec2> control_points, std::uint32_t degree,
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

    // --- batch arena access (const; includes dead slots) --------------------
    [[nodiscard]] const GenerationalArena<PointData>& points() const noexcept { return points_; }
    [[nodiscard]] const GenerationalArena<LineData>& lines() const noexcept { return lines_; }
    [[nodiscard]] const GenerationalArena<CircleData>& circles() const noexcept { return circles_; }
    [[nodiscard]] const GenerationalArena<ArcData>& arcs() const noexcept { return arcs_; }
    [[nodiscard]] const GenerationalArena<PolylineData>& polylines() const noexcept {
        return polylines_;
    }
    [[nodiscard]] const GenerationalArena<SplineData>& splines() const noexcept { return splines_; }

    // --- vertex pools -------------------------------------------------------
    [[nodiscard]] std::span<const Vec2> polyline_vertices() const noexcept {
        return polyline_pool_;
    }
    [[nodiscard]] std::span<const Vec2> spline_control_pool() const noexcept {
        return spline_pool_;
    }
    /// The vertex view for a given polyline/spline record.
    [[nodiscard]] std::span<const Vec2> vertices_of(const PolylineData& pl) const noexcept;
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

    std::vector<Vec2> polyline_pool_;
    std::vector<Vec2> spline_pool_;

    std::vector<Layer> layers_{Layer{"0"}}; // layer 0 always present
    std::uint16_t current_layer_ = 0;
};

} // namespace musacad::core

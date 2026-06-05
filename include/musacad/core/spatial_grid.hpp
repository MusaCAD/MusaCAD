#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "musacad/core/entity_handle.hpp"
#include "musacad/core/math/math.hpp"

namespace musacad::core {

/// Uniform-grid spatial index over entity AABBs. Geometry-side, single source of
/// proximity truth: it serves BOTH the OSNAP engine and ERASE cursor-pick.
/// Entities are bucketed into the cells their AABB overlaps; a query returns the
/// (deduplicated) handles whose buckets overlap the query AABB. The caller then
/// does exact geometric tests on the small candidate set.
///
/// Maintenance is O(cells covered) per insert/remove and is driven by the
/// GeometryEngine alongside the store/undo lifecycle.
class SpatialGrid {
public:
    explicit SpatialGrid(double cell_size = 16.0);

    void insert(EntityHandle handle, Vec2 min, Vec2 max);
    void remove(EntityHandle handle);
    void clear();

    /// Appends candidate handles overlapping [min,max] to `out` (deduplicated).
    void query(Vec2 min, Vec2 max, std::vector<EntityHandle>& out) const;

    [[nodiscard]] std::size_t entity_count() const noexcept { return entity_cells_.size(); }
    [[nodiscard]] double cell_size() const noexcept { return cell_; }

private:
    struct CellKey {
        std::int64_t x;
        std::int64_t y;
        bool operator==(const CellKey&) const noexcept = default;
    };
    struct CellHash {
        std::size_t operator()(const CellKey& k) const noexcept {
            return std::hash<std::int64_t>{}(k.x) * 1099511628211ull ^
                   std::hash<std::int64_t>{}(k.y);
        }
    };
    struct CellRange {
        std::int64_t x0, y0, x1, y1;
    };

    [[nodiscard]] std::int64_t cell_index(double v) const noexcept;
    [[nodiscard]] CellRange range_of(Vec2 min, Vec2 max) const noexcept;
    [[nodiscard]] static std::uint64_t key_of(EntityHandle h) noexcept;

    double cell_;
    std::unordered_map<CellKey, std::vector<EntityHandle>, CellHash> cells_;
    std::unordered_map<std::uint64_t, CellRange> entity_cells_;
};

} // namespace musacad::core

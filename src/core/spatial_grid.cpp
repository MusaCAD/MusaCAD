#include "musacad/core/spatial_grid.hpp"

#include <algorithm>
#include <cmath>

namespace musacad::core {

SpatialGrid::SpatialGrid(double cell_size) : cell_(cell_size > 0.0 ? cell_size : 16.0) {}

std::int64_t SpatialGrid::cell_index(double v) const noexcept {
    return static_cast<std::int64_t>(std::floor(v / cell_));
}

SpatialGrid::CellRange SpatialGrid::range_of(Vec2 min, Vec2 max) const noexcept {
    if (min.x > max.x) {
        std::swap(min.x, max.x);
    }
    if (min.y > max.y) {
        std::swap(min.y, max.y);
    }
    return {cell_index(min.x), cell_index(min.y), cell_index(max.x), cell_index(max.y)};
}

std::uint64_t SpatialGrid::key_of(EntityHandle h) noexcept {
    return (static_cast<std::uint64_t>(h.index) << 32) ^
           (static_cast<std::uint64_t>(h.generation) << 3) ^ static_cast<std::uint64_t>(h.kind);
}

void SpatialGrid::insert(EntityHandle handle, Vec2 min, Vec2 max) {
    const CellRange r = range_of(min, max);
    for (std::int64_t cy = r.y0; cy <= r.y1; ++cy) {
        for (std::int64_t cx = r.x0; cx <= r.x1; ++cx) {
            cells_[CellKey{cx, cy}].push_back(handle);
        }
    }
    entity_cells_[key_of(handle)] = r;
}

void SpatialGrid::remove(EntityHandle handle) {
    const auto it = entity_cells_.find(key_of(handle));
    if (it == entity_cells_.end()) {
        return;
    }
    const CellRange r = it->second;
    for (std::int64_t cy = r.y0; cy <= r.y1; ++cy) {
        for (std::int64_t cx = r.x0; cx <= r.x1; ++cx) {
            const auto cit = cells_.find(CellKey{cx, cy});
            if (cit == cells_.end()) {
                continue;
            }
            auto& bucket = cit->second;
            std::erase(bucket, handle);
            if (bucket.empty()) {
                cells_.erase(cit);
            }
        }
    }
    entity_cells_.erase(it);
}

void SpatialGrid::clear() {
    cells_.clear();
    entity_cells_.clear();
}

void SpatialGrid::query(Vec2 min, Vec2 max, std::vector<EntityHandle>& out) const {
    const CellRange r = range_of(min, max);
    const std::size_t start = out.size();
    for (std::int64_t cy = r.y0; cy <= r.y1; ++cy) {
        for (std::int64_t cx = r.x0; cx <= r.x1; ++cx) {
            const auto it = cells_.find(CellKey{cx, cy});
            if (it == cells_.end()) {
                continue;
            }
            out.insert(out.end(), it->second.begin(), it->second.end());
        }
    }
    // Deduplicate handles that spanned multiple queried cells.
    std::sort(out.begin() + static_cast<std::ptrdiff_t>(start), out.end(),
              [](EntityHandle a, EntityHandle b) {
                  return key_of(a) < key_of(b);
              });
    out.erase(std::unique(out.begin() + static_cast<std::ptrdiff_t>(start), out.end()), out.end());
}

} // namespace musacad::core

#include "musacad/core/geometry_store.hpp"

namespace musacad::core {

EntityHandle GeometryStore::add_point(Vec2 p, EntityProps props) {
    const auto slot = points_.insert(PointData{p, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Point};
}

EntityHandle GeometryStore::add_line(Vec2 a, Vec2 b, EntityProps props) {
    const auto slot = lines_.insert(LineData{a, b, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Line};
}

EntityHandle GeometryStore::add_circle(Vec2 center, double radius, EntityProps props) {
    const auto slot = circles_.insert(CircleData{center, radius, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Circle};
}

EntityHandle GeometryStore::add_arc(Vec2 center, double radius, double start_angle,
                                    double end_angle, EntityProps props) {
    const auto slot = arcs_.insert(ArcData{center, radius, start_angle, end_angle, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Arc};
}

EntityHandle GeometryStore::add_polyline(std::span<const Vec2> vertices, bool closed,
                                         EntityProps props) {
    const auto offset = static_cast<std::uint32_t>(polyline_pool_.size());
    polyline_pool_.insert(polyline_pool_.end(), vertices.begin(), vertices.end());
    const auto slot = polylines_.insert(
        PolylineData{offset, static_cast<std::uint32_t>(vertices.size()), closed, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Polyline};
}

EntityHandle GeometryStore::add_spline(std::span<const Vec2> control_points, std::uint32_t degree,
                                       EntityProps props) {
    const auto offset = static_cast<std::uint32_t>(spline_pool_.size());
    spline_pool_.insert(spline_pool_.end(), control_points.begin(), control_points.end());
    const auto slot = splines_.insert(
        SplineData{offset, static_cast<std::uint32_t>(control_points.size()), degree, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Spline};
}

EntityHandle GeometryStore::add_text(Vec2 pos, double height, double rotation, std::uint8_t justify,
                                     std::string_view content, EntityProps props) {
    const auto offset = static_cast<std::uint32_t>(string_pool_.size());
    string_pool_.insert(string_pool_.end(), content.begin(), content.end());
    const auto slot = texts_.insert(TextData{pos, height, rotation, justify, offset,
                                             static_cast<std::uint32_t>(content.size()), props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Text};
}

EntityHandle GeometryStore::add_dimension(DimType type, Vec2 a, Vec2 b, Vec2 line_pt,
                                          std::uint16_t style, EntityProps props) {
    const auto slot = dims_.insert(DimData{type, a, b, line_pt, style, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Dimension};
}

EntityHandle GeometryStore::add_leader(Vec2 tip, Vec2 knee, double text_height, std::uint16_t style,
                                       std::string_view content, EntityProps props) {
    const auto offset = static_cast<std::uint32_t>(string_pool_.size());
    string_pool_.insert(string_pool_.end(), content.begin(), content.end());
    const auto slot = leaders_.insert(LeaderData{tip, knee, text_height, style, offset,
                                                 static_cast<std::uint32_t>(content.size()), props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Leader};
}

bool GeometryStore::remove(EntityHandle h) noexcept {
    switch (h.kind) {
    case EntityKind::Point:
        return points_.erase(h.index, h.generation);
    case EntityKind::Line:
        return lines_.erase(h.index, h.generation);
    case EntityKind::Polyline:
        return polylines_.erase(h.index, h.generation);
    case EntityKind::Circle:
        return circles_.erase(h.index, h.generation);
    case EntityKind::Arc:
        return arcs_.erase(h.index, h.generation);
    case EntityKind::Spline:
        return splines_.erase(h.index, h.generation);
    case EntityKind::Text:
        return texts_.erase(h.index, h.generation);
    case EntityKind::Dimension:
        return dims_.erase(h.index, h.generation);
    case EntityKind::Leader:
        return leaders_.erase(h.index, h.generation);
    }
    return false;
}

bool GeometryStore::is_valid(EntityHandle h) const noexcept {
    switch (h.kind) {
    case EntityKind::Point:
        return points_.is_valid(h.index, h.generation);
    case EntityKind::Line:
        return lines_.is_valid(h.index, h.generation);
    case EntityKind::Polyline:
        return polylines_.is_valid(h.index, h.generation);
    case EntityKind::Circle:
        return circles_.is_valid(h.index, h.generation);
    case EntityKind::Arc:
        return arcs_.is_valid(h.index, h.generation);
    case EntityKind::Spline:
        return splines_.is_valid(h.index, h.generation);
    case EntityKind::Text:
        return texts_.is_valid(h.index, h.generation);
    case EntityKind::Dimension:
        return dims_.is_valid(h.index, h.generation);
    case EntityKind::Leader:
        return leaders_.is_valid(h.index, h.generation);
    }
    return false;
}

std::size_t GeometryStore::live_count() const noexcept {
    return points_.live_count() + lines_.live_count() + polylines_.live_count() +
           circles_.live_count() + arcs_.live_count() + splines_.live_count() +
           texts_.live_count() + dims_.live_count() + leaders_.live_count();
}

void GeometryStore::clear() noexcept {
    points_.clear();
    lines_.clear();
    circles_.clear();
    arcs_.clear();
    polylines_.clear();
    splines_.clear();
    texts_.clear();
    dims_.clear();
    leaders_.clear();
    polyline_pool_.clear();
    spline_pool_.clear();
    string_pool_.clear();
    layers_.assign(1, Layer{"0"}); // reset to just layer 0
    current_layer_ = 0;
    dimstyles_.assign(1, DimStyle{"Standard"});
}

const PointData* GeometryStore::point(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Point ? points_.get(h.index, h.generation) : nullptr;
}
const LineData* GeometryStore::line(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Line ? lines_.get(h.index, h.generation) : nullptr;
}
const CircleData* GeometryStore::circle(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Circle ? circles_.get(h.index, h.generation) : nullptr;
}
const ArcData* GeometryStore::arc(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Arc ? arcs_.get(h.index, h.generation) : nullptr;
}
const PolylineData* GeometryStore::polyline(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Polyline ? polylines_.get(h.index, h.generation) : nullptr;
}
const SplineData* GeometryStore::spline(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Spline ? splines_.get(h.index, h.generation) : nullptr;
}
const TextData* GeometryStore::text(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Text ? texts_.get(h.index, h.generation) : nullptr;
}
const DimData* GeometryStore::dimension(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Dimension ? dims_.get(h.index, h.generation) : nullptr;
}
const LeaderData* GeometryStore::leader(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Leader ? leaders_.get(h.index, h.generation) : nullptr;
}
std::string_view GeometryStore::string_of(const TextData& t) const noexcept {
    return std::string_view(string_pool_.data() + t.str_offset, t.str_len);
}
std::string_view GeometryStore::string_of(const LeaderData& l) const noexcept {
    return std::string_view(string_pool_.data() + l.str_offset, l.str_len);
}

const DimStyle* GeometryStore::dimstyle(std::uint16_t index) const noexcept {
    return index < dimstyles_.size() ? &dimstyles_[index] : nullptr;
}
std::uint16_t GeometryStore::add_dimstyle(const DimStyle& style) {
    for (std::size_t i = 0; i < dimstyles_.size(); ++i) {
        if (dimstyles_[i].name == style.name) {
            return static_cast<std::uint16_t>(i);
        }
    }
    dimstyles_.push_back(style);
    return static_cast<std::uint16_t>(dimstyles_.size() - 1);
}
void GeometryStore::set_dimstyle_table(std::vector<DimStyle> styles) {
    dimstyles_ = std::move(styles);
    if (dimstyles_.empty()) {
        dimstyles_.push_back(DimStyle{"Standard"});
    }
    dimstyles_[0].name = "Standard";
}

bool GeometryStore::set_dimstyle(std::uint16_t index, const DimStyle& style) {
    if (index >= dimstyles_.size()) {
        return false;
    }
    DimStyle updated = style;
    if (index == 0) {
        updated.name = "Standard"; // index 0 is always "Standard"
    }
    dimstyles_[index] = updated;
    return true;
}

std::span<const Vec2> GeometryStore::vertices_of(const PolylineData& pl) const noexcept {
    return std::span<const Vec2>(polyline_pool_).subspan(pl.offset, pl.count);
}
std::span<const Vec2> GeometryStore::control_points_of(const SplineData& sp) const noexcept {
    return std::span<const Vec2>(spline_pool_).subspan(sp.offset, sp.count);
}

// --- per-entity properties -------------------------------------------------

const EntityProps* GeometryStore::props(EntityHandle h) const noexcept {
    switch (h.kind) {
    case EntityKind::Point:
        if (const PointData* d = point(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Line:
        if (const LineData* d = line(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Circle:
        if (const CircleData* d = circle(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Arc:
        if (const ArcData* d = arc(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Polyline:
        if (const PolylineData* d = polyline(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Spline:
        if (const SplineData* d = spline(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Text:
        if (const TextData* d = text(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Dimension:
        if (const DimData* d = dimension(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Leader:
        if (const LeaderData* d = leader(h)) {
            return &d->props;
        }
        break;
    }
    return nullptr;
}

bool GeometryStore::set_props(EntityHandle h, const EntityProps& p) noexcept {
    switch (h.kind) {
    case EntityKind::Point:
        if (PointData* d = points_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Line:
        if (LineData* d = lines_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Circle:
        if (CircleData* d = circles_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Arc:
        if (ArcData* d = arcs_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Polyline:
        if (PolylineData* d = polylines_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Spline:
        if (SplineData* d = splines_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Text:
        if (TextData* d = texts_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Dimension:
        if (DimData* d = dims_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Leader:
        if (LeaderData* d = leaders_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    }
    return false;
}

// --- layer table -----------------------------------------------------------

const Layer* GeometryStore::layer(std::uint16_t index) const noexcept {
    return index < layers_.size() ? &layers_[index] : nullptr;
}

void GeometryStore::set_current_layer(std::uint16_t index) noexcept {
    if (index < layers_.size()) {
        current_layer_ = index;
    }
}

void GeometryStore::set_layer_table(std::vector<Layer> layers, std::uint16_t current) {
    layers_ = std::move(layers);
    if (layers_.empty()) {
        layers_.push_back(Layer{"0"});
    }
    layers_[0].name = "0"; // index 0 is always layer "0"
    current_layer_ = current < layers_.size() ? current : 0;
}

std::uint16_t GeometryStore::add_layer(const Layer& layer) {
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        if (layers_[i].name == layer.name) {
            return static_cast<std::uint16_t>(i); // names are unique
        }
    }
    layers_.push_back(layer);
    return static_cast<std::uint16_t>(layers_.size() - 1);
}

bool GeometryStore::set_layer(std::uint16_t index, const Layer& layer) {
    if (index >= layers_.size()) {
        return false;
    }
    Layer updated = layer;
    if (index == 0) {
        updated.name = "0"; // layer 0 cannot be renamed
    }
    layers_[index] = updated;
    return true;
}

namespace {
template <class Arena, class Fn>
void for_each_live_mut(Arena& arena, Fn&& fn) {
    for (std::uint32_t i = 0; i < arena.slot_count(); ++i) {
        if (arena.alive(i)) {
            fn(*arena.get(i, arena.generations()[i]));
        }
    }
}
template <class Arena, class Fn>
void for_each_live_const(const Arena& arena, Fn&& fn) {
    for (std::uint32_t i = 0; i < arena.slot_count(); ++i) {
        if (arena.alive(i)) {
            fn(arena.data()[i]);
        }
    }
}
} // namespace

bool GeometryStore::layer_in_use(std::uint16_t index) const noexcept {
    bool used = false;
    const auto check = [&](const auto& data) {
        if (data.props.layer == index) {
            used = true;
        }
    };
    for_each_live_const(points_, check);
    for_each_live_const(lines_, check);
    for_each_live_const(circles_, check);
    for_each_live_const(arcs_, check);
    for_each_live_const(polylines_, check);
    for_each_live_const(splines_, check);
    for_each_live_const(texts_, check);
    for_each_live_const(dims_, check);
    for_each_live_const(leaders_, check);
    return used;
}

void GeometryStore::shift_layer_refs_after_removal(std::uint16_t removed) noexcept {
    const auto fix = [&](auto& data) {
        if (data.props.layer > removed) {
            --data.props.layer;
        }
    };
    for_each_live_mut(points_, fix);
    for_each_live_mut(lines_, fix);
    for_each_live_mut(circles_, fix);
    for_each_live_mut(arcs_, fix);
    for_each_live_mut(polylines_, fix);
    for_each_live_mut(splines_, fix);
    for_each_live_mut(texts_, fix);
    for_each_live_mut(dims_, fix);
    for_each_live_mut(leaders_, fix);
}

bool GeometryStore::remove_layer(std::uint16_t index) {
    if (index == 0 || index >= layers_.size() || index == current_layer_ || layer_in_use(index)) {
        return false; // AutoCAD: can't delete layer 0, current, or a non-empty layer
    }
    layers_.erase(layers_.begin() + index);
    shift_layer_refs_after_removal(index);
    if (current_layer_ > index) {
        --current_layer_;
    }
    return true;
}

} // namespace musacad::core

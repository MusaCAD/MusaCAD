#include "musacad/core/geometry_engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <variant>

#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/properties_registry.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/osnap.hpp"
#include "musacad/core/scene_snapshot.hpp"

namespace musacad::core {

namespace {
/// Segment-segment intersection (returns the crossing point if within both).
bool segment_intersection(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, Vec2& out) {
    const Vec2 r = p2 - p1;
    const Vec2 s = p4 - p3;
    const double rxs = cross(r, s);
    if (std::abs(rxs) < 1e-12) {
        return false;
    }
    const Vec2 qp = p3 - p1;
    const double t = cross(qp, s) / rxs;
    const double u = cross(qp, r) / rxs;
    if (t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0) {
        out = p1 + r * t;
        return true;
    }
    return false;
}
} // namespace

void GeometryEngine::start() {
    if (worker_.joinable()) {
        return; // already running
    }
    worker_ = std::jthread([this](std::stop_token token) { run(std::move(token)); });
}

void GeometryEngine::stop() noexcept {
    if (worker_.joinable()) {
        worker_.request_stop(); // wakes wait_pop via the stop_token
        worker_.join();
    }
}

void GeometryEngine::run(std::stop_token token) {
    while (!token.stop_requested()) {
        std::optional<Command> cmd = queue_.wait_pop(token);
        if (!cmd) {
            break; // stop requested with an empty queue
        }
        apply(*cmd);
        // Drain any further pending commands before rebuilding the snapshot, so
        // a burst of edits yields a single coherent publish.
        while (std::optional<Command> more = queue_.try_pop()) {
            apply(*more);
        }
        rebuild_and_publish();
    }
}

EntityHandle GeometryEngine::create_entity(const Command& add_command) {
    // A fresh draw (props unset) lands on the current layer, fully ByLayer; a
    // captured/restored/transformed entity carries its exact props. The apply logic
    // is shared with the grip-preview path via core::add_command_to_store.
    return add_command_to_store(store_, add_command, EntityProps{store_.current_layer()});
}

EntityHandle GeometryEngine::create_indexed(const Command& add_command) {
    const EntityHandle h = create_entity(add_command);
    Vec2 lo;
    Vec2 hi;
    if (entity_aabb(store_, h, lo, hi)) {
        grid_.insert(h, lo, hi);
    }
    return h;
}

void GeometryEngine::remove_indexed(EntityHandle h) {
    grid_.remove(h); // while the handle is still valid
    store_.remove(h);
}

Command GeometryEngine::capture_entity(EntityHandle h) const {
    // Shared with the grip-edit/preview path (core::grips). One capture definition.
    return core::capture_entity(store_, h);
}

EntityHandle GeometryEngine::most_recent_live() const {
    for (auto git = undo_.rbegin(); git != undo_.rend(); ++git) {
        for (auto it = git->items.rbegin(); it != git->items.rend(); ++it) {
            if (it->is_create && store_.is_valid(it->handle)) {
                return it->handle;
            }
        }
    }
    return EntityHandle::null();
}

EntityHandle GeometryEngine::pick_nearest(Vec2 world, double radius) const {
    if (radius <= 0.0) {
        return EntityHandle::null();
    }
    std::vector<EntityHandle> candidates;
    grid_.query({world.x - radius, world.y - radius}, {world.x + radius, world.y + radius},
                candidates);
    EntityHandle best = EntityHandle::null();
    double best_d2 = radius * radius;
    Vec2 cp;
    for (const EntityHandle h : candidates) {
        if (!selectable(h)) {
            continue; // off/frozen aren't drawn; locked is inert
        }
        if (kernel_.closest_point(store_, h, world, cp)) {
            const double d2 = length_squared(cp - world);
            if (d2 <= best_d2) {
                best_d2 = d2;
                best = h;
            }
        }
    }
    return best;
}

bool GeometryEngine::selectable(EntityHandle h) const {
    const EntityProps* p = store_.props(h);
    if (p == nullptr) {
        return false;
    }
    const Layer* l = store_.layer(p->layer);
    return l != nullptr && l->on && !l->frozen && !l->locked;
}

std::vector<EntityHandle> GeometryEngine::all_live() const {
    std::vector<EntityHandle> live;
    const auto collect = [&](const auto& arena, EntityKind kind) {
        for (std::uint32_t i = 0; i < arena.slot_count(); ++i) {
            if (arena.alive(i)) {
                live.push_back(EntityHandle{i, arena.generations()[i], kind});
            }
        }
    };
    collect(store_.points(), EntityKind::Point);
    collect(store_.lines(), EntityKind::Line);
    collect(store_.circles(), EntityKind::Circle);
    collect(store_.arcs(), EntityKind::Arc);
    collect(store_.polylines(), EntityKind::Polyline);
    collect(store_.splines(), EntityKind::Spline);
    collect(store_.texts(), EntityKind::Text);
    collect(store_.dimensions(), EntityKind::Dimension);
    collect(store_.leaders(), EntityKind::Leader);
    collect(store_.mtexts(), EntityKind::MText);
    collect(store_.mleaders(), EntityKind::MLeader);
    return live;
}

void GeometryEngine::push_create_item(std::uint64_t group, EntityHandle handle, Command data) {
    if (undo_.empty() || undo_.back().id != group) {
        undo_.push_back(Group{group, {}});
    }
    undo_.back().items.push_back(Item{std::move(data), handle, true});
}

void GeometryEngine::push_erase_item(std::uint64_t group, Command data) {
    if (undo_.empty() || undo_.back().id != group) {
        undo_.push_back(Group{group, {}});
    }
    undo_.back().items.push_back(Item{std::move(data), EntityHandle::null(), false});
}

void GeometryEngine::do_undo_group() {
    if (undo_.empty()) {
        return;
    }
    Group g = std::move(undo_.back());
    undo_.pop_back();
    // Reverse the items in reverse order so a mixed (erase+create) group undoes
    // cleanly.
    for (auto it = g.items.rbegin(); it != g.items.rend(); ++it) {
        if (it->is_create) {
            remove_indexed(it->handle);
            it->handle = EntityHandle::null();
        } else {
            it->handle = create_indexed(it->data);
        }
    }
    redo_.push_back(std::move(g));
}

void GeometryEngine::do_redo_group() {
    if (redo_.empty()) {
        return;
    }
    Group g = std::move(redo_.back());
    redo_.pop_back();
    for (Item& it : g.items) {
        if (it.is_create) {
            it.handle = create_indexed(it.data);
        } else {
            remove_indexed(it.handle);
            it.handle = EntityHandle::null();
        }
    }
    undo_.push_back(std::move(g));
}

void GeometryEngine::do_undo_op() {
    redo_.clear(); // a transient in-command edit invalidates redo
    while (!undo_.empty() && undo_.back().items.empty()) {
        undo_.pop_back();
    }
    if (undo_.empty()) {
        return;
    }
    Group& g = undo_.back();
    Item it = std::move(g.items.back());
    g.items.pop_back();
    if (it.is_create) {
        remove_indexed(it.handle);
    } else {
        create_indexed(it.data);
    }
    if (g.items.empty()) {
        undo_.pop_back();
    }
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

bool GeometryEngine::sel_contains(EntityHandle h) const {
    return std::find(selection_.begin(), selection_.end(), h) != selection_.end();
}

void GeometryEngine::sel_add(EntityHandle h) {
    if (!h.is_null() && store_.is_valid(h) && !sel_contains(h)) {
        selection_.push_back(h);
    }
}

void GeometryEngine::prune_selection() {
    std::erase_if(selection_, [this](EntityHandle h) { return !store_.is_valid(h); });
}

namespace {
bool point_in_rect(Vec2 p, Vec2 mn, Vec2 mx) {
    return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y;
}
// True if segment a-b intersects the axis-aligned rect [mn,mx].
bool segment_hits_rect(Vec2 a, Vec2 b, Vec2 mn, Vec2 mx) {
    if (point_in_rect(a, mn, mx) || point_in_rect(b, mn, mx)) {
        return true;
    }
    Vec2 hit{};
    const Vec2 c1{mn.x, mn.y};
    const Vec2 c2{mx.x, mn.y};
    const Vec2 c3{mx.x, mx.y};
    const Vec2 c4{mn.x, mx.y};
    return segment_intersection(a, b, c1, c2, hit) || segment_intersection(a, b, c2, c3, hit) ||
           segment_intersection(a, b, c3, c4, hit) || segment_intersection(a, b, c4, c1, hit);
}
} // namespace

void GeometryEngine::select_window(Vec2 mn, Vec2 mx, bool crossing, bool additive) {
    if (!additive) {
        selection_.clear();
    }
    std::vector<EntityHandle> candidates;
    grid_.query(mn, mx, candidates);
    std::vector<Vec2> tess;
    for (const EntityHandle h : candidates) {
        if (!selectable(h)) {
            continue; // window/crossing select ignores off/frozen/locked layers
        }
        kernel_.tessellate(store_, h, kDefaultTessTolerance, tess);
        if (tess.empty()) {
            continue;
        }
        bool selected = false;
        if (crossing) {
            for (std::size_t i = 1; i < tess.size() && !selected; ++i) {
                selected = segment_hits_rect(tess[i - 1], tess[i], mn, mx);
            }
            if (!selected && tess.size() == 1) {
                selected = point_in_rect(tess[0], mn, mx);
            }
        } else {
            selected = true; // window: every point must be inside
            for (const Vec2& p : tess) {
                if (!point_in_rect(p, mn, mx)) {
                    selected = false;
                    break;
                }
            }
        }
        if (selected) {
            sel_add(h);
        }
    }
}

// ---------------------------------------------------------------------------
// Modify
// ---------------------------------------------------------------------------

namespace {

void translate_cmd(Command& c, Vec2 d) {
    std::visit(
        [&](auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, AddLineCommand>) {
                x.a += d;
                x.b += d;
            } else if constexpr (std::is_same_v<T, AddCircleCommand>) {
                x.center += d;
            } else if constexpr (std::is_same_v<T, AddArcCommand>) {
                x.center += d;
            } else if constexpr (std::is_same_v<T, AddPolylineCommand>) {
                for (Vec2& p : x.points) {
                    p += d;
                }
            } else if constexpr (std::is_same_v<T, AddTextCommand>) {
                x.pos += d;
            } else if constexpr (std::is_same_v<T, AddDimensionCommand>) {
                x.a += d;
                x.b += d;
                x.line_pt += d;
            } else if constexpr (std::is_same_v<T, AddLeaderCommand>) {
                x.tip += d;
                x.knee += d;
            } else if constexpr (std::is_same_v<T, AddMTextCommand>) {
                x.block.pos += d;
            } else if constexpr (std::is_same_v<T, AddMLeaderCommand>) {
                for (Vec2& v : x.vertices) {
                    v += d;
                }
                x.block.pos += d; // the owned label moves with the leader
            }
        },
        c);
}

void mirror_cmd(Command& c, Vec2 A, Vec2 B) {
    const Vec2 dir = normalized(B - A);
    const double axis = std::atan2(dir.y, dir.x);
    const auto refl = [&](Vec2 p) {
        const Vec2 ap = p - A;
        const double t = dot(ap, dir);
        const Vec2 proj = A + dir * t;
        return proj * 2.0 - p;
    };
    const auto refl_ang = [&](double th) { return 2.0 * axis - th; };
    std::visit(
        [&](auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, AddLineCommand>) {
                x.a = refl(x.a);
                x.b = refl(x.b);
            } else if constexpr (std::is_same_v<T, AddCircleCommand>) {
                x.center = refl(x.center);
            } else if constexpr (std::is_same_v<T, AddArcCommand>) {
                const double s = x.start_angle;
                const double e = x.end_angle;
                x.center = refl(x.center);
                x.start_angle = refl_ang(e); // reflection reverses orientation
                x.end_angle = refl_ang(s);
            } else if constexpr (std::is_same_v<T, AddPolylineCommand>) {
                for (Vec2& p : x.points) {
                    p = refl(p);
                }
                for (double& b : x.bulges) {
                    b = -b; // reflection flips arc orientation
                }
            } else if constexpr (std::is_same_v<T, AddTextCommand>) {
                x.pos = refl(x.pos);
                x.rotation = refl_ang(x.rotation);
            } else if constexpr (std::is_same_v<T, AddDimensionCommand>) {
                x.a = refl(x.a);
                x.b = refl(x.b);
                x.line_pt = refl(x.line_pt);
            } else if constexpr (std::is_same_v<T, AddLeaderCommand>) {
                x.tip = refl(x.tip);
                x.knee = refl(x.knee);
            } else if constexpr (std::is_same_v<T, AddMTextCommand>) {
                x.block.pos = refl(x.block.pos);
                x.block.rotation = refl_ang(x.block.rotation);
            } else if constexpr (std::is_same_v<T, AddMLeaderCommand>) {
                for (Vec2& v : x.vertices) {
                    v = refl(v);
                }
                x.block.pos = refl(x.block.pos);
                x.block.rotation = refl_ang(x.block.rotation);
            }
        },
        c);
}

void rotate_cmd(Command& c, Vec2 base, double ang) {
    const double cs = std::cos(ang);
    const double sn = std::sin(ang);
    const auto rot = [&](Vec2 p) {
        const Vec2 d = p - base;
        return Vec2{base.x + d.x * cs - d.y * sn, base.y + d.x * sn + d.y * cs};
    };
    std::visit(
        [&](auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, AddLineCommand>) {
                x.a = rot(x.a);
                x.b = rot(x.b);
            } else if constexpr (std::is_same_v<T, AddCircleCommand>) {
                x.center = rot(x.center);
            } else if constexpr (std::is_same_v<T, AddArcCommand>) {
                x.center = rot(x.center);
                x.start_angle += ang;
                x.end_angle += ang;
            } else if constexpr (std::is_same_v<T, AddPolylineCommand>) {
                for (Vec2& p : x.points) {
                    p = rot(p);
                }
            } else if constexpr (std::is_same_v<T, AddTextCommand>) {
                x.pos = rot(x.pos);
                x.rotation += ang;
            } else if constexpr (std::is_same_v<T, AddDimensionCommand>) {
                x.a = rot(x.a);
                x.b = rot(x.b);
                x.line_pt = rot(x.line_pt);
            } else if constexpr (std::is_same_v<T, AddLeaderCommand>) {
                x.tip = rot(x.tip);
                x.knee = rot(x.knee);
            } else if constexpr (std::is_same_v<T, AddMTextCommand>) {
                x.block.pos = rot(x.block.pos);
                x.block.rotation += ang;
            } else if constexpr (std::is_same_v<T, AddMLeaderCommand>) {
                for (Vec2& v : x.vertices) {
                    v = rot(v);
                }
                x.block.pos = rot(x.block.pos);
                x.block.rotation += ang;
            }
        },
        c);
}

/// True if point p (assumed on the arc's circle) lies within the CCW sweep.
bool angle_on_arc(const ArcData& arc, Vec2 p) {
    double sweep = arc.end_angle - arc.start_angle;
    while (sweep < 0.0) {
        sweep += kTwoPi;
    }
    if (sweep <= 0.0) {
        sweep = kTwoPi;
    }
    double rel = std::atan2(p.y - arc.center.y, p.x - arc.center.x) - arc.start_angle;
    while (rel < 0.0) {
        rel += kTwoPi;
    }
    return rel <= sweep + 1e-9;
}

/// A representative anchor point for an add-command (used by polar array when the
/// items should orbit the centre without rotating themselves).
Vec2 command_anchor(const Command& c) {
    Vec2 out{0.0, 0.0};
    std::visit(
        [&](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, AddLineCommand>) {
                out = x.a;
            } else if constexpr (std::is_same_v<T, AddCircleCommand>) {
                out = x.center;
            } else if constexpr (std::is_same_v<T, AddArcCommand>) {
                out = x.center;
            } else if constexpr (std::is_same_v<T, AddPolylineCommand>) {
                out = x.points.empty() ? Vec2{0.0, 0.0} : x.points.front();
            } else if constexpr (std::is_same_v<T, AddTextCommand>) {
                out = x.pos;
            } else if constexpr (std::is_same_v<T, AddDimensionCommand>) {
                out = x.a;
            } else if constexpr (std::is_same_v<T, AddLeaderCommand>) {
                out = x.tip;
            } else if constexpr (std::is_same_v<T, AddMTextCommand>) {
                out = x.block.pos;
            } else if constexpr (std::is_same_v<T, AddMLeaderCommand>) {
                out = x.vertices.empty() ? x.block.pos : x.vertices.front();
            }
        },
        c);
    return out;
}

void scale_cmd(Command& c, Vec2 base, double f) {
    const auto scl = [&](Vec2 p) { return base + (p - base) * f; };
    std::visit(
        [&](auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, AddLineCommand>) {
                x.a = scl(x.a);
                x.b = scl(x.b);
            } else if constexpr (std::is_same_v<T, AddCircleCommand>) {
                x.center = scl(x.center);
                x.radius *= f;
            } else if constexpr (std::is_same_v<T, AddArcCommand>) {
                x.center = scl(x.center);
                x.radius *= f;
            } else if constexpr (std::is_same_v<T, AddPolylineCommand>) {
                for (Vec2& p : x.points) {
                    p = scl(p);
                }
            } else if constexpr (std::is_same_v<T, AddTextCommand>) {
                x.pos = scl(x.pos);
                x.height *= f;
            } else if constexpr (std::is_same_v<T, AddDimensionCommand>) {
                x.a = scl(x.a);
                x.b = scl(x.b);
                x.line_pt = scl(x.line_pt);
            } else if constexpr (std::is_same_v<T, AddLeaderCommand>) {
                x.tip = scl(x.tip);
                x.knee = scl(x.knee);
                x.text_height *= f;
            } else if constexpr (std::is_same_v<T, AddMTextCommand>) {
                x.block.pos = scl(x.block.pos);
                x.block.height *= f;
                x.block.width *= f;
            } else if constexpr (std::is_same_v<T, AddMLeaderCommand>) {
                for (Vec2& v : x.vertices) {
                    v = scl(v);
                }
                x.block.pos = scl(x.block.pos);
                x.block.height *= f;
                x.block.width *= f;
            }
        },
        c);
}

} // namespace

void GeometryEngine::apply_move(Vec2 delta, bool copy, std::uint64_t group) {
    const std::vector<EntityHandle> sel = selection_;
    std::vector<EntityHandle> moved;
    for (const EntityHandle h : sel) {
        if (!store_.is_valid(h)) {
            continue;
        }
        const Command original = capture_entity(h);
        Command result = original;
        translate_cmd(result, delta);
        if (!copy) {
            remove_indexed(h);
            push_erase_item(group, original);
        }
        const EntityHandle nh = create_indexed(result);
        push_create_item(group, nh, result);
        moved.push_back(nh);
    }
    if (!copy && !moved.empty()) {
        selection_ = moved; // the moved entities stay selected
    }
    redo_.clear();
    geom_dirty_ = true;
}

void GeometryEngine::apply_mirror(Vec2 a, Vec2 b, bool erase_source, std::uint64_t group) {
    const std::vector<EntityHandle> sel = selection_;
    std::vector<EntityHandle> result_handles;
    for (const EntityHandle h : sel) {
        if (!store_.is_valid(h)) {
            continue;
        }
        const Command original = capture_entity(h);
        Command mirrored = original;
        mirror_cmd(mirrored, a, b);
        if (erase_source) {
            remove_indexed(h);
            push_erase_item(group, original);
        }
        const EntityHandle nh = create_indexed(mirrored);
        push_create_item(group, nh, mirrored);
        result_handles.push_back(nh);
    }
    if (erase_source && !result_handles.empty()) {
        selection_ = result_handles;
    }
    redo_.clear();
    geom_dirty_ = true;
}

void GeometryEngine::apply_rotate(Vec2 base, double angle, std::uint64_t group) {
    const std::vector<EntityHandle> sel = selection_;
    std::vector<EntityHandle> out;
    for (const EntityHandle h : sel) {
        if (!store_.is_valid(h)) {
            continue;
        }
        const Command original = capture_entity(h);
        Command result = original;
        rotate_cmd(result, base, angle);
        remove_indexed(h);
        push_erase_item(group, original);
        const EntityHandle nh = create_indexed(result);
        push_create_item(group, nh, result);
        out.push_back(nh);
    }
    if (!out.empty()) {
        selection_ = out;
    }
    redo_.clear();
    geom_dirty_ = true;
}

void GeometryEngine::apply_scale(Vec2 base, double factor, std::uint64_t group) {
    if (!(factor > 0.0)) {
        return;
    }
    const std::vector<EntityHandle> sel = selection_;
    std::vector<EntityHandle> out;
    for (const EntityHandle h : sel) {
        if (!store_.is_valid(h)) {
            continue;
        }
        const Command original = capture_entity(h);
        Command result = original;
        scale_cmd(result, base, factor);
        remove_indexed(h);
        push_erase_item(group, original);
        const EntityHandle nh = create_indexed(result);
        push_create_item(group, nh, result);
        out.push_back(nh);
    }
    if (!out.empty()) {
        selection_ = out;
    }
    redo_.clear();
    geom_dirty_ = true;
}

void GeometryEngine::apply_array_rect(int rows, int cols, double dx, double dy,
                                      std::uint64_t group) {
    rows = std::max(rows, 1);
    cols = std::max(cols, 1);
    const std::vector<EntityHandle> sel = selection_;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (r == 0 && c == 0) {
                continue; // the originals stay in place
            }
            const Vec2 d{static_cast<double>(c) * dx, static_cast<double>(r) * dy};
            for (const EntityHandle h : sel) {
                if (!store_.is_valid(h)) {
                    continue;
                }
                Command copy = capture_entity(h);
                translate_cmd(copy, d);
                const EntityHandle nh = create_indexed(copy);
                push_create_item(group, nh, copy);
            }
        }
    }
    redo_.clear();
    geom_dirty_ = true;
}

void GeometryEngine::apply_array_polar(Vec2 center, int count, double total_angle,
                                       bool rotate_items, std::uint64_t group) {
    if (count < 2) {
        return;
    }
    // Full circle distributes count items evenly; a partial fill spans the angle
    // across (count-1) gaps (AutoCAD semantics).
    const bool full = std::abs(std::abs(total_angle) - kTwoPi) < 1e-6;
    const double step = full ? total_angle / static_cast<double>(count)
                             : total_angle / static_cast<double>(count - 1);
    const std::vector<EntityHandle> sel = selection_;
    for (int i = 1; i < count; ++i) {
        const double a = step * static_cast<double>(i);
        for (const EntityHandle h : sel) {
            if (!store_.is_valid(h)) {
                continue;
            }
            Command copy = capture_entity(h);
            if (rotate_items) {
                rotate_cmd(copy, center, a);
            } else {
                // Move the copy around the circle without rotating the entity.
                const Vec2 anchor = command_anchor(copy);
                const double cs = std::cos(a);
                const double sn = std::sin(a);
                const Vec2 d = anchor - center;
                const Vec2 moved{center.x + d.x * cs - d.y * sn, center.y + d.x * sn + d.y * cs};
                translate_cmd(copy, moved - anchor);
            }
            const EntityHandle nh = create_indexed(copy);
            push_create_item(group, nh, copy);
        }
    }
    redo_.clear();
    geom_dirty_ = true;
}

void GeometryEngine::apply_extend(Vec2 pick, double radius, std::uint64_t group) {
    const EntityHandle h = pick_nearest(pick, radius);
    if (h.is_null()) {
        report("Extend: nothing under the pick.");
        return;
    }
    if (h.kind != EntityKind::Line) {
        report("Extend: only line entities can be extended yet (closed shapes have no open end).");
        return;
    }
    const LineData* l = store_.line(h);
    const Vec2 a = l->a;
    const Vec2 b = l->b;
    // The end nearer the pick is the one that moves; extend away from the fixed end.
    Vec2 mov = a;
    Vec2 fix = b;
    if (length_squared(pick - b) < length_squared(pick - a)) {
        mov = b;
        fix = a;
    }
    const Vec2 dir = normalized(mov - fix);

    // Find the nearest boundary hit strictly forward of the moving end. Boundaries
    // can be anywhere, so scan live entities (EXTEND is interactive/infrequent).
    double best = std::numeric_limits<double>::infinity();
    Vec2 target{};
    bool found = false;
    const auto consider = [&](Vec2 p) {
        const double fwd = dot(p - mov, dir);
        if (fwd > 1e-6 && fwd < best) {
            best = fwd;
            target = p;
            found = true;
        }
    };
    for (const EntityHandle c : all_live()) {
        if (c == h) {
            continue;
        }
        if (c.kind == EntityKind::Line) {
            const LineData* m = store_.line(c);
            Vec2 p{};
            if (NativeKernel2D::line_line_intersection(fix, mov, m->a, m->b, p)) {
                const Vec2 md = m->b - m->a;
                const double u = dot(p - m->a, md) / std::max(length_squared(md), 1e-18);
                if (u >= -1e-9 && u <= 1.0 + 1e-9) {
                    consider(p);
                }
            }
        } else if (c.kind == EntityKind::Circle) {
            const CircleData* cc = store_.circle(c);
            Vec2 p0{};
            Vec2 p1{};
            const int n = NativeKernel2D::line_circle_intersection(fix, mov, cc->center, cc->radius,
                                                                   p0, p1);
            if (n >= 1) {
                consider(p0);
            }
            if (n == 2) {
                consider(p1);
            }
        } else if (c.kind == EntityKind::Arc) {
            const ArcData* arc = store_.arc(c);
            Vec2 p0{};
            Vec2 p1{};
            const int n =
                NativeKernel2D::line_circle_intersection(fix, mov, arc->center, arc->radius, p0, p1);
            if (n >= 1 && angle_on_arc(*arc, p0)) {
                consider(p0);
            }
            if (n == 2 && angle_on_arc(*arc, p1)) {
                consider(p1);
            }
        }
    }
    if (!found) {
        report("Extend: no boundary ahead of that end.");
        return;
    }
    const Command original = capture_entity(h);
    remove_indexed(h);
    push_erase_item(group, original);
    const Command extended = AddLineCommand{fix, target, 0};
    push_create_item(group, create_indexed(extended), extended);
    redo_.clear();
    geom_dirty_ = true;
    report("Extended.");
}

namespace {
/// The endpoint of L on the same side of the corner P as the pick (the part the
/// user wants to keep). `dir` returns the unit direction from P toward that end.
Vec2 kept_endpoint(const LineData& L, Vec2 P, Vec2 pick, Vec2& dir) {
    const Vec2 d = L.b - L.a;
    const double len2 = std::max(length_squared(d), 1e-18);
    const double tP = dot(P - L.a, d) / len2;
    const double tpick = dot(pick - L.a, d) / len2;
    const Vec2 keep = (tpick < tP) ? L.a : L.b;
    dir = normalized(keep - P);
    return keep;
}

double dist_point_seg(Vec2 a, Vec2 b, Vec2 p) {
    const Vec2 ab = b - a;
    const double len2 = length_squared(ab);
    const double t = len2 > 1e-18 ? std::clamp(dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
    return length((a + ab * t) - p);
}

/// Index of the polyline segment nearest p (seg i joins v[i], v[i+1]; the closing
/// segment n-1 joins v[n-1], v[0] when closed). -1 if too few vertices.
int nearest_pl_segment(std::span<const Vec2> v, bool closed, Vec2 p) {
    const std::size_t n = v.size();
    if (n < 2) {
        return -1;
    }
    const std::size_t segs = closed ? n : n - 1;
    int best = -1;
    double bestd = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < segs; ++i) {
        const double d = dist_point_seg(v[i], v[(i + 1) % n], p);
        if (d < bestd) {
            bestd = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

/// The vertex shared by two adjacent polyline segments, or -1 if not adjacent.
int shared_vertex(int s1, int s2, int n, bool closed) {
    if (s1 == s2) {
        return -1;
    }
    if (s2 == s1 + 1) {
        return s1 + 1;
    }
    if (s1 == s2 + 1) {
        return s2 + 1;
    }
    if (closed && ((s1 == 0 && s2 == n - 1) || (s2 == 0 && s1 == n - 1))) {
        return 0; // the wrap corner joins segment 0 and segment n-1 at vertex 0
    }
    return -1;
}

/// Replace vertex `sv` with a bevel: a point d_prev along the edge toward the
/// previous vertex and d_next toward the next. Returns false if it can't fit.
bool chamfer_pl(std::vector<Vec2>& pts, bool closed, int sv, double d_prev, double d_next) {
    const std::size_t n = pts.size();
    const std::size_t s = static_cast<std::size_t>(sv);
    if (n < 3 || (!closed && (sv <= 0 || s >= n - 1))) {
        return false;
    }
    const std::size_t prev = (s + n - 1) % n;
    const std::size_t next = (s + 1) % n;
    const Vec2 V = pts[s];
    if (d_prev > distance(V, pts[prev]) + 1e-9 || d_next > distance(V, pts[next]) + 1e-9) {
        return false;
    }
    const Vec2 A = V + normalized(pts[prev] - V) * d_prev;
    const Vec2 B = V + normalized(pts[next] - V) * d_next;
    std::vector<Vec2> out;
    out.reserve(n + 1);
    for (std::size_t i = 0; i < n; ++i) {
        if (i == s) {
            out.push_back(A);
            out.push_back(B);
        } else {
            out.push_back(pts[i]);
        }
    }
    pts = std::move(out);
    return true;
}

/// Replace vertex `sv` with a tangent arc of radius r, approximated by vertices.
// Rounds corner `sv` with a true arc by replacing the corner vertex with its two
// tangent points and recording the arc as a BULGE on the first -- the geometry
// stays a parametric polyline (no baked facets), so it can be dimensioned and
// re-tessellated at any zoom. `bulges` is grown to match `pts` (zeros = straight).
bool fillet_pl(std::vector<Vec2>& pts, std::vector<double>& bulges, bool closed, int sv, double r) {
    const std::size_t n = pts.size();
    const std::size_t s = static_cast<std::size_t>(sv);
    if (n < 3 || (!closed && (sv <= 0 || s >= n - 1)) || r <= 0.0) {
        return false;
    }
    if (bulges.size() != n) {
        bulges.assign(n, 0.0);
    }
    const std::size_t prev = (s + n - 1) % n;
    const std::size_t next = (s + 1) % n;
    const Vec2 V = pts[s];
    const Vec2 uP = normalized(pts[prev] - V);
    const Vec2 uN = normalized(pts[next] - V);
    const double alpha = std::acos(std::clamp(dot(uP, uN), -1.0, 1.0));
    if (alpha < 1e-4 || alpha > kPi - 1e-4) {
        return false;
    }
    const double td = r / std::tan(alpha / 2.0);
    if (td > distance(V, pts[prev]) + 1e-9 || td > distance(V, pts[next]) + 1e-9) {
        return false;
    }
    const Vec2 Tp = V + uP * td; // tangent point on the incoming edge
    const Vec2 Tn = V + uN * td; // tangent point on the outgoing edge
    const Vec2 C = V + normalized(uP + uN) * (r / std::sin(alpha / 2.0));
    double a0 = std::atan2(Tp.y - C.y, Tp.x - C.x);
    const double a1 = std::atan2(Tn.y - C.y, Tn.x - C.x);
    double sweep = a1 - a0;
    while (sweep <= -kPi) {
        sweep += kTwoPi;
    }
    while (sweep > kPi) {
        sweep -= kTwoPi;
    }
    const double bulge = std::tan(sweep / 4.0); // arc Tp->Tn as an AutoCAD bulge
    // Replace V (index s) with Tp, Tn; the prev->Tp edge keeps its bulge, Tp->Tn is
    // the fillet arc, Tn->next keeps what V->next had.
    pts[s] = Tp;
    pts.insert(pts.begin() + static_cast<std::ptrdiff_t>(s) + 1, Tn);
    const double out_bulge = bulges[s]; // old V->next segment bulge
    bulges[s] = bulge;
    bulges.insert(bulges.begin() + static_cast<std::ptrdiff_t>(s) + 1, out_bulge);
    return true;
}

/// Endpoints of the line, or the polyline segment nearest `pick`, under `h`.
/// False for any other entity kind (or a degenerate polyline).
bool segment_endpoints(const GeometryStore& store, EntityHandle h, Vec2 pick, Vec2& a, Vec2& b) {
    if (h.kind == EntityKind::Line) {
        const LineData* l = store.line(h);
        a = l->a;
        b = l->b;
        return true;
    }
    if (h.kind == EntityKind::Polyline) {
        const PolylineData* pl = store.polyline(h);
        const std::span<const Vec2> v = store.vertices_of(*pl);
        const int s = nearest_pl_segment(v, pl->closed, pick);
        if (s < 0) {
            return false;
        }
        const std::size_t n = v.size();
        a = v[static_cast<std::size_t>(s)];
        b = v[(static_cast<std::size_t>(s) + 1) % n];
        return true;
    }
    return false;
}
} // namespace

bool GeometryEngine::resolve_dim_defs(std::uint8_t type, Vec2 pick1, Vec2 pick2, double radius,
                                      DimData& out) const {
    const auto dt = static_cast<DimType>(type);
    const EntityHandle h1 = pick_nearest(pick1, radius);
    if (h1.is_null()) {
        return false;
    }
    out.type = dt;
    out.style = 0;
    if (dt == DimType::Radius || dt == DimType::Diameter) {
        Vec2 center{};
        double r = 0.0;
        if (h1.kind == EntityKind::Circle) {
            const CircleData* c = store_.circle(h1);
            center = c->center;
            r = c->radius;
        } else if (h1.kind == EntityKind::Arc) {
            const ArcData* arc = store_.arc(h1);
            center = arc->center;
            r = arc->radius;
        } else if (h1.kind == EntityKind::Polyline) {
            // Dimension a filleted (bulged) polyline segment: find the arc segment
            // nearest the pick and read its recovered centre + radius.
            const PolylineData* pl = store_.polyline(h1);
            const auto v = store_.vertices_of(*pl);
            const auto b = store_.bulges_of(*pl);
            if (b.empty() || v.empty()) {
                return false;
            }
            const std::size_t n = v.size();
            const std::size_t segs = (pl->closed && n >= 2) ? n : n - 1;
            double best = std::numeric_limits<double>::infinity();
            bool found = false;
            for (std::size_t i = 0; i < segs; ++i) {
                if (b[i] == 0.0) {
                    continue;
                }
                const BulgeArc a = arc_from_bulge(v[i], v[(i + 1) % n], b[i]);
                const double d = std::abs(distance(pick1, a.center) - a.radius);
                if (d < best) {
                    best = d;
                    center = a.center;
                    r = a.radius;
                    found = true;
                }
            }
            if (!found) {
                return false;
            }
        } else {
            return false;
        }
        Vec2 dir = pick2 - center;
        dir = length_squared(dir) > 1e-12 ? normalized(dir) : Vec2{1.0, 0.0};
        out.a = center;
        out.b = center + dir * r;
        out.line_pt = pick2;
        return true;
    }
    if (dt == DimType::Angular) {
        const EntityHandle h2 = pick_nearest(pick2, radius);
        if (h2.is_null()) {
            return false;
        }
        Vec2 a1{};
        Vec2 b1{};
        Vec2 a2{};
        Vec2 b2{};
        if (!segment_endpoints(store_, h1, pick1, a1, b1) ||
            !segment_endpoints(store_, h2, pick2, a2, b2)) {
            return false;
        }
        Vec2 v{};
        if (!NativeKernel2D::line_line_intersection(a1, b1, a2, b2, v)) {
            return false;
        }
        // A ray point on each line, on the picked side of the vertex; its distance
        // sizes the dimension arc (the measured angle is direction-only).
        const auto ray_pt = [&](Vec2 pa, Vec2 pb, Vec2 pick) -> Vec2 {
            Vec2 d = pb - pa;
            if (length_squared(d) < 1e-18) {
                return pb;
            }
            d = normalized(d);
            if (dot(pick - v, d) < 0.0) {
                d = d * -1.0;
            }
            double len = distance(v, pick);
            if (len < 1e-6) {
                len = 1.0;
            }
            return v + d * len;
        };
        out.a = v;
        out.b = ray_pt(a1, b1, pick1);
        out.line_pt = ray_pt(a2, b2, pick2);
        return true;
    }
    // Linear / Aligned.
    Vec2 a{};
    Vec2 b{};
    if (!segment_endpoints(store_, h1, pick1, a, b)) {
        return false;
    }
    out.a = a;
    out.b = b;
    out.line_pt = pick2;
    return true;
}

void GeometryEngine::apply_object_dimension(std::uint8_t type, Vec2 pick1, Vec2 pick2,
                                            double radius, std::uint16_t style,
                                            std::uint64_t group) {
    DimData d;
    if (!resolve_dim_defs(type, pick1, pick2, radius, d)) {
        report("Could not dimension that object -- select a line, circle, or arc.");
        return;
    }
    const Command add = AddDimensionCommand{type, d.a, d.b, d.line_pt, style, group, {}};
    const EntityHandle nh = create_indexed(add);
    push_create_item(group, nh, add);
    redo_.clear();
    geom_dirty_ = true;
    report("Dimension created from object.");
}

void GeometryEngine::apply_grip_commit(std::uint64_t group) {
    if (!grip_active_ || !store_.is_valid(grip_handle_)) {
        return;
    }
    // Erase the original and create the grip-edited entity as ONE undo group --
    // exactly the move/property-edit pattern. The edit is parametric (no baking).
    const Command original = capture_entity(grip_handle_);
    const Command edited = edit_for_grip_drag(store_, grip_handle_, grip_index_, grip_pos_);
    remove_indexed(grip_handle_);
    push_erase_item(group, original);
    const EntityHandle nh = create_indexed(edited);
    push_create_item(group, nh, edited);
    selection_ = {nh}; // keep the edited entity selected (grips follow)
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report("Edited.");
}

void GeometryEngine::apply_text_edit(Vec2 at, double pick_radius, const std::string& content,
                                     std::uint64_t group) {
    // Find the nearest editable text-bearing entity whose AABB (grown by the pick
    // aperture) contains `at`. Reuses the entity AABB; respects selectable() so
    // locked/off/frozen text can't be edited.
    EntityHandle target;
    double best = 0.0;
    const auto consider = [&](EntityHandle h) {
        if (!store_.is_valid(h) || !selectable(h)) {
            return;
        }
        Vec2 lo;
        Vec2 hi;
        if (!entity_aabb(store_, h, lo, hi)) {
            return;
        }
        const double pad = std::max(pick_radius, 1e-9);
        if (at.x < lo.x - pad || at.x > hi.x + pad || at.y < lo.y - pad || at.y > hi.y + pad) {
            return;
        }
        const Vec2 c{(lo.x + hi.x) * 0.5, (lo.y + hi.y) * 0.5};
        const double d2 = length_squared(at - c);
        if (target.is_null() || d2 < best) {
            target = h;
            best = d2;
        }
    };
    const auto scan = [&](const auto& arena, EntityKind kind) {
        for (std::uint32_t i = 0; i < arena.slot_count(); ++i) {
            if (arena.alive(i)) {
                consider(EntityHandle{i, arena.generations()[i], kind});
            }
        }
    };
    scan(store_.texts(), EntityKind::Text);
    scan(store_.mtexts(), EntityKind::MText);
    scan(store_.mleaders(), EntityKind::MLeader);
    if (target.is_null()) {
        report("No editable text there.");
        return;
    }
    // Capture the entity, change ONLY its content, recommit as one undo group --
    // layer/properties/position are preserved (not a delete+recreate).
    Command edited = capture_entity(target);
    std::visit(
        [&](auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, AddTextCommand> ||
                          std::is_same_v<T, AddMTextCommand> ||
                          std::is_same_v<T, AddMLeaderCommand>) {
                x.content = content;
            }
        },
        edited);
    const Command original = capture_entity(target);
    remove_indexed(target);
    push_erase_item(group, original);
    const EntityHandle nh = create_indexed(edited);
    push_create_item(group, nh, edited);
    selection_ = {nh};
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report("Text edited.");
}

void GeometryEngine::apply_fillet(Vec2 pick1, Vec2 pick2, double radius, double pick_radius,
                                  std::uint64_t group) {
    const EntityHandle h1 = pick_nearest(pick1, pick_radius);
    const EntityHandle h2 = pick_nearest(pick2, pick_radius);
    if (h1.is_null() || h2.is_null()) {
        report("Fillet: pick two edges.");
        return;
    }

    // Case 1: two adjacent segments of the same polyline -> round that corner.
    if (h1 == h2 && h1.kind == EntityKind::Polyline) {
        const PolylineData* pl = store_.polyline(h1);
        const std::span<const Vec2> v = store_.vertices_of(*pl);
        const int s1 = nearest_pl_segment(v, pl->closed, pick1);
        const int s2 = nearest_pl_segment(v, pl->closed, pick2);
        const int sv = shared_vertex(s1, s2, static_cast<int>(v.size()), pl->closed);
        if (sv < 0) {
            report("Fillet: pick two adjacent edges of the polyline.");
            return;
        }
        std::vector<Vec2> pts(v.begin(), v.end());
        const auto bspan = store_.bulges_of(*pl);
        std::vector<double> bulges(bspan.begin(), bspan.end());
        if (!fillet_pl(pts, bulges, pl->closed, sv, radius)) {
            report("Fillet: radius too large for that corner.");
            return;
        }
        const bool closed = pl->closed;
        const Command orig = capture_entity(h1);
        remove_indexed(h1);
        push_erase_item(group, orig);
        const Command np = AddPolylineCommand{std::move(pts), closed, 0, {}, std::move(bulges)};
        push_create_item(group, create_indexed(np), np);
        redo_.clear();
        geom_dirty_ = true;
        report("Filleted.");
        return;
    }

    // Case 2: two distinct lines.
    if (h1 == h2 || h1.kind != EntityKind::Line || h2.kind != EntityKind::Line) {
        report("Fillet: pick two lines, or two adjacent edges of one polyline.");
        return;
    }
    const LineData l1 = *store_.line(h1);
    const LineData l2 = *store_.line(h2);
    Vec2 P{};
    if (!NativeKernel2D::line_line_intersection(l1.a, l1.b, l2.a, l2.b, P)) {
        report("Fillet: the two lines are parallel.");
        return;
    }
    Vec2 u1{};
    Vec2 u2{};
    const Vec2 k1 = kept_endpoint(l1, P, pick1, u1);
    const Vec2 k2 = kept_endpoint(l2, P, pick2, u2);

    Vec2 t1 = P;
    Vec2 t2 = P;
    std::optional<Command> arc;
    if (radius > 0.0) {
        const double cosang = std::clamp(dot(u1, u2), -1.0, 1.0);
        const double alpha = std::acos(cosang);
        if (alpha < 1e-6 || alpha > kPi - 1e-6) {
            report("Fillet: the two lines are collinear.");
            return;
        }
        const double td = radius / std::tan(alpha / 2.0);
        if (td > length(k1 - P) + 1e-9 || td > length(k2 - P) + 1e-9) {
            report("Fillet: radius too large for these lines.");
            return;
        }
        const double cd = radius / std::sin(alpha / 2.0);
        t1 = P + u1 * td;
        t2 = P + u2 * td;
        const Vec2 center = P + normalized(u1 + u2) * cd;
        double a1 = std::atan2(t1.y - center.y, t1.x - center.x);
        double a2 = std::atan2(t2.y - center.y, t2.x - center.x);
        double ccw = a2 - a1;
        while (ccw < 0.0) {
            ccw += kTwoPi;
        }
        if (ccw > kPi) {
            std::swap(a1, a2); // keep the minor (rounding) arc
        }
        arc = AddArcCommand{center, radius, a1, a2, 0};
    }

    const Command o1 = capture_entity(h1);
    const Command o2 = capture_entity(h2);
    remove_indexed(h1);
    push_erase_item(group, o1);
    remove_indexed(h2);
    push_erase_item(group, o2);
    const Command e1 = AddLineCommand{k1, t1, 0};
    push_create_item(group, create_indexed(e1), e1);
    const Command e2 = AddLineCommand{k2, t2, 0};
    push_create_item(group, create_indexed(e2), e2);
    if (arc) {
        push_create_item(group, create_indexed(*arc), *arc);
    }
    redo_.clear();
    geom_dirty_ = true;
    report("Filleted.");
}

void GeometryEngine::apply_chamfer(Vec2 pick1, Vec2 pick2, double dist1, double dist2,
                                   double pick_radius, std::uint64_t group) {
    const EntityHandle h1 = pick_nearest(pick1, pick_radius);
    const EntityHandle h2 = pick_nearest(pick2, pick_radius);
    if (h1.is_null() || h2.is_null()) {
        report("Chamfer: pick two edges.");
        return;
    }

    // Case 1: two adjacent segments of the same polyline -> bevel that corner.
    if (h1 == h2 && h1.kind == EntityKind::Polyline) {
        const PolylineData* pl = store_.polyline(h1);
        const std::span<const Vec2> v = store_.vertices_of(*pl);
        const int n = static_cast<int>(v.size());
        const int s1 = nearest_pl_segment(v, pl->closed, pick1);
        const int s2 = nearest_pl_segment(v, pl->closed, pick2);
        const int sv = shared_vertex(s1, s2, n, pl->closed);
        if (sv < 0) {
            report("Chamfer: pick two adjacent edges of the polyline.");
            return;
        }
        const int prevseg = (sv - 1 + n) % n; // segment joining prev vertex to sv
        double d_prev = dist2;
        double d_next = dist1;
        if (s1 == prevseg) {
            d_prev = dist1;
            d_next = dist2;
        }
        std::vector<Vec2> pts(v.begin(), v.end());
        if (!chamfer_pl(pts, pl->closed, sv, d_prev, d_next)) {
            report("Chamfer: distances too large for that corner.");
            return;
        }
        const bool closed = pl->closed;
        const Command orig = capture_entity(h1);
        remove_indexed(h1);
        push_erase_item(group, orig);
        const Command np = AddPolylineCommand{std::move(pts), closed, 0};
        push_create_item(group, create_indexed(np), np);
        redo_.clear();
        geom_dirty_ = true;
        report("Chamfered.");
        return;
    }

    // Case 2: two distinct lines.
    if (h1 == h2 || h1.kind != EntityKind::Line || h2.kind != EntityKind::Line) {
        report("Chamfer: pick two lines, or two adjacent edges of one polyline.");
        return;
    }
    const LineData l1 = *store_.line(h1);
    const LineData l2 = *store_.line(h2);
    Vec2 P{};
    if (!NativeKernel2D::line_line_intersection(l1.a, l1.b, l2.a, l2.b, P)) {
        report("Chamfer: the two lines are parallel.");
        return;
    }
    Vec2 u1{};
    Vec2 u2{};
    const Vec2 k1 = kept_endpoint(l1, P, pick1, u1);
    const Vec2 k2 = kept_endpoint(l2, P, pick2, u2);
    const Vec2 t1 = P + u1 * dist1;
    const Vec2 t2 = P + u2 * dist2;

    const Command o1 = capture_entity(h1);
    const Command o2 = capture_entity(h2);
    remove_indexed(h1);
    push_erase_item(group, o1);
    remove_indexed(h2);
    push_erase_item(group, o2);
    const Command e1 = AddLineCommand{k1, t1, 0};
    push_create_item(group, create_indexed(e1), e1);
    const Command e2 = AddLineCommand{k2, t2, 0};
    push_create_item(group, create_indexed(e2), e2);
    if (length_squared(t1 - t2) > 1e-12) { // skip the connector for a clean corner
        const Command bevel = AddLineCommand{t1, t2, 0};
        push_create_item(group, create_indexed(bevel), bevel);
    }
    redo_.clear();
    geom_dirty_ = true;
    report("Chamfered.");
}

namespace {
/// Applies `fn` to the EntityProps inside an Add* command (engaging the optional
/// if needed). No-op for non-Add commands.
void modify_cmd_props(Command& c, const std::function<void(EntityProps&)>& fn) {
    std::visit(
        [&](auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, AddLineCommand> ||
                          std::is_same_v<T, AddPolylineCommand> ||
                          std::is_same_v<T, AddCircleCommand> || std::is_same_v<T, AddArcCommand> ||
                          std::is_same_v<T, AddTextCommand> || std::is_same_v<T, AddDimensionCommand> ||
                          std::is_same_v<T, AddLeaderCommand> || std::is_same_v<T, AddMTextCommand> ||
                          std::is_same_v<T, AddMLeaderCommand>) {
                if (!x.props) {
                    x.props = EntityProps{};
                }
                fn(*x.props);
            }
        },
        c);
}
} // namespace

void GeometryEngine::apply_props_change(const std::function<void(EntityProps&)>& modify,
                                        std::uint64_t group) {
    const std::vector<EntityHandle> sel = selection_;
    std::vector<EntityHandle> out;
    for (const EntityHandle h : sel) {
        if (!store_.is_valid(h)) {
            continue;
        }
        const Command original = capture_entity(h);
        Command modified = original; // carries the entity's exact props
        modify_cmd_props(modified, modify);
        remove_indexed(h);
        push_erase_item(group, original);
        const EntityHandle nh = create_indexed(modified);
        push_create_item(group, nh, modified);
        out.push_back(nh);
    }
    if (!out.empty()) {
        selection_ = out;
    }
    redo_.clear();
    geom_dirty_ = true;
}

void GeometryEngine::apply_set_property(PropertyId id, const PropertyValue& value,
                                       std::uint64_t group) {
    // The PR palette's single write path: change one property on every selected
    // entity it applies to, as one undo group (capture/erase/recreate -- the
    // apply_props_change pattern, so layer/other props/position are preserved).
    const std::vector<EntityHandle> sel = selection_;
    std::vector<EntityHandle> out;
    bool any = false;
    for (const EntityHandle h : sel) {
        if (!store_.is_valid(h)) {
            continue;
        }
        const Command original = capture_entity(h);
        if (!property_applies(id, h.kind)) {
            out.push_back(h); // unchanged, keep selected
            continue;
        }
        Command modified = original;
        write_property(modified, id, value);
        remove_indexed(h);
        push_erase_item(group, original);
        const EntityHandle nh = create_indexed(modified);
        push_create_item(group, nh, modified);
        out.push_back(nh);
        any = true;
    }
    if (any) {
        selection_ = out;
        redo_.clear();
        geom_dirty_ = true;
        dirty_ = true;
        report("Property changed.");
    }
}

void GeometryEngine::apply_entity_layer(std::uint16_t layer, std::uint64_t group) {
    apply_props_change([layer](EntityProps& p) { p.layer = layer; }, group);
    report("Moved selection to layer.");
}

void GeometryEngine::apply_entity_color(bool by_layer, Rgb color, std::uint64_t group) {
    apply_props_change(
        [by_layer, color](EntityProps& p) {
            p.set_color_by_layer(by_layer);
            if (!by_layer) {
                p.color = color;
            }
        },
        group);
    report(by_layer ? "Colour set to ByLayer." : "Colour override applied.");
}

void GeometryEngine::apply_offset(Vec2 pick, double radius, double distance, Vec2 side,
                                  std::uint64_t group) {
    const EntityHandle h = pick_nearest(pick, radius);
    if (h.is_null()) {
        report("Offset: nothing under the pick.");
        return;
    }
    Command add;
    if (kernel_.offset(store_, h, distance, side, add)) {
        const EntityHandle nh = create_indexed(add);
        push_create_item(group, nh, add);
        redo_.clear();
        geom_dirty_ = true;
        report("Offset created.");
    } else {
        report("Offset: can't offset that entity.");
    }
}

void GeometryEngine::apply_trim(Vec2 pick, double radius, std::uint64_t group) {
    const EntityHandle h = pick_nearest(pick, radius);
    if (h.is_null()) {
        report("Trim: nothing under the pick.");
        return;
    }
    if (h.kind != EntityKind::Line) {
        report("Trim: only line entities can be trimmed yet (explode polylines first).");
        return;
    }
    const LineData* l = store_.line(h);
    const Vec2 a = l->a;
    const Vec2 b = l->b;
    const Vec2 ab = b - a;
    const double len2 = length_squared(ab);
    if (len2 <= 0.0) {
        return;
    }

    // Intersection parameters along the line with every nearby other entity.
    Vec2 lo;
    Vec2 hi;
    entity_aabb(store_, h, lo, hi);
    std::vector<EntityHandle> cand;
    grid_.query(lo, hi, cand);
    std::vector<double> ts;
    std::vector<Vec2> hits;
    for (const EntityHandle c : cand) {
        if (c == h) {
            continue;
        }
        hits.clear();
        kernel_.intersect(store_, h, c, hits);
        for (const Vec2& p : hits) {
            const double t = dot(p - a, ab) / len2;
            if (t > 1e-6 && t < 1.0 - 1e-6) {
                ts.push_back(t);
            }
        }
    }
    if (ts.empty()) {
        report("Trim: no crossing edge found.");
        return;
    }
    std::sort(ts.begin(), ts.end());
    const double tp = std::clamp(dot(pick - a, ab) / len2, 0.0, 1.0);
    double lo_t = 0.0;
    double hi_t = 1.0;
    for (const double t : ts) {
        if (t <= tp) {
            lo_t = t;
        }
        if (t >= tp) {
            hi_t = t;
            break;
        }
    }

    const Command original = capture_entity(h);
    remove_indexed(h);
    push_erase_item(group, original);
    if (lo_t > 1e-6) {
        const Command piece = AddLineCommand{a, a + ab * lo_t, 0};
        push_create_item(group, create_indexed(piece), piece);
    }
    if (hi_t < 1.0 - 1e-6) {
        const Command piece = AddLineCommand{a + ab * hi_t, b, 0};
        push_create_item(group, create_indexed(piece), piece);
    }
    redo_.clear();
    geom_dirty_ = true;
    report("Trimmed.");
}

void GeometryEngine::apply(const Command& command) {
    std::visit(
        [this, &command](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, AddLineCommand> ||
                          std::is_same_v<T, AddPolylineCommand> ||
                          std::is_same_v<T, AddCircleCommand> ||
                          std::is_same_v<T, AddArcCommand> || std::is_same_v<T, AddTextCommand> ||
                          std::is_same_v<T, AddDimensionCommand> ||
                          std::is_same_v<T, AddLeaderCommand> || std::is_same_v<T, AddMTextCommand> ||
                          std::is_same_v<T, AddMLeaderCommand>) {
                const EntityHandle h = create_indexed(command);
                push_create_item(c.group, h, command);
                redo_.clear();
                geom_dirty_ = true;
            } else if constexpr (std::is_same_v<T, EraseCommand>) {
                std::vector<EntityHandle> targets;
                if (c.scope == EraseScope::All) {
                    targets = all_live();
                } else if (const EntityHandle last = most_recent_live(); !last.is_null()) {
                    targets.push_back(last);
                }
                for (const EntityHandle h : targets) {
                    Command restore = capture_entity(h);
                    remove_indexed(h);
                    push_erase_item(c.group, std::move(restore));
                }
                redo_.clear();
                geom_dirty_ = true;
            } else if constexpr (std::is_same_v<T, ErasePickCommand>) {
                const EntityHandle h = pick_nearest(c.world, c.pick_radius);
                if (!h.is_null()) {
                    Command restore = capture_entity(h);
                    remove_indexed(h);
                    push_erase_item(c.group, std::move(restore));
                    redo_.clear();
                    geom_dirty_ = true;
                }
            } else if constexpr (std::is_same_v<T, UndoLastGroupCommand>) {
                do_undo_group();
                geom_dirty_ = true;
            } else if constexpr (std::is_same_v<T, RedoLastGroupCommand>) {
                do_redo_group();
                geom_dirty_ = true;
            } else if constexpr (std::is_same_v<T, UndoLastOpCommand>) {
                do_undo_op();
                geom_dirty_ = true;
            } else if constexpr (std::is_same_v<T, SetCursorCommand>) {
                cursor_ = c.world;
                pick_radius_ = c.pick_radius;
                osnap_enabled_ = c.osnap;
                snap_mask_ = c.snap_mask;
                has_from_ = c.has_from;
                from_ = c.from;
            } else if constexpr (std::is_same_v<T, EraseSelectionCommand>) {
                const std::vector<EntityHandle> sel = selection_;
                for (const EntityHandle h : sel) {
                    if (!store_.is_valid(h)) {
                        continue;
                    }
                    Command restore = capture_entity(h);
                    remove_indexed(h);
                    push_erase_item(c.group, std::move(restore));
                }
                selection_.clear();
                redo_.clear();
                geom_dirty_ = true;
            } else if constexpr (std::is_same_v<T, SelectPickCommand>) {
                if (!c.additive) {
                    selection_.clear();
                }
                sel_add(pick_nearest(c.world, c.radius));
            } else if constexpr (std::is_same_v<T, SelectWindowCommand>) {
                select_window(c.min, c.max, c.crossing, c.additive);
            } else if constexpr (std::is_same_v<T, SelectAllCommand>) {
                selection_ = all_live();
            } else if constexpr (std::is_same_v<T, ClearSelectionCommand>) {
                selection_.clear();
            } else if constexpr (std::is_same_v<T, MoveSelectionCommand>) {
                apply_move(c.delta, false, c.group);
            } else if constexpr (std::is_same_v<T, CopySelectionCommand>) {
                apply_move(c.delta, true, c.group);
            } else if constexpr (std::is_same_v<T, MirrorSelectionCommand>) {
                apply_mirror(c.a, c.b, c.erase_source, c.group);
            } else if constexpr (std::is_same_v<T, OffsetPickCommand>) {
                apply_offset(c.pick, c.radius, c.distance, c.side, c.group);
            } else if constexpr (std::is_same_v<T, TrimPickCommand>) {
                apply_trim(c.pick, c.radius, c.group);
            } else if constexpr (std::is_same_v<T, RotateSelectionCommand>) {
                apply_rotate(c.base, c.angle, c.group);
            } else if constexpr (std::is_same_v<T, ScaleSelectionCommand>) {
                apply_scale(c.base, c.factor, c.group);
            } else if constexpr (std::is_same_v<T, ArrayRectCommand>) {
                apply_array_rect(c.rows, c.cols, c.dx, c.dy, c.group);
            } else if constexpr (std::is_same_v<T, ArrayPolarCommand>) {
                apply_array_polar(c.center, c.count, c.total_angle, c.rotate_items, c.group);
            } else if constexpr (std::is_same_v<T, ExtendPickCommand>) {
                apply_extend(c.pick, c.radius, c.group);
            } else if constexpr (std::is_same_v<T, FilletPickCommand>) {
                apply_fillet(c.pick1, c.pick2, c.radius, c.pick_radius, c.group);
            } else if constexpr (std::is_same_v<T, ChamferPickCommand>) {
                apply_chamfer(c.pick1, c.pick2, c.dist1, c.dist2, c.pick_radius, c.group);
            } else if constexpr (std::is_same_v<T, AddObjectDimensionCommand>) {
                apply_object_dimension(c.type, c.pick1, c.pick2, c.pick_radius, c.style, c.group);
            } else if constexpr (std::is_same_v<T, ResolveDimObjectCommand>) {
                // Non-mutating: resolve def points for the UI placement preview.
                DimData d;
                has_pending_dim_ = resolve_dim_defs(c.type, c.pick1, c.pick2, c.pick_radius, d);
                if (has_pending_dim_) {
                    pending_dim_ = d;
                }
                ++pending_dim_version_;
            } else if constexpr (std::is_same_v<T, SetViewScaleCommand>) {
                // Zoom-adaptive tessellation: re-tessellate only when the view scale
                // crosses a half-octave bucket (so panning never re-tessellates).
                view_world_per_px_ = c.world_per_px > 0.0 ? c.world_per_px : view_world_per_px_;
                const int bucket =
                    static_cast<int>(std::lround(std::log2(view_world_per_px_) * 2.0));
                if (bucket != tess_bucket_) {
                    tess_bucket_ = bucket;
                    constexpr double kChordPx = 0.3; // target screen-space chord error
                    tess_tolerance_ = std::max(1e-9, kChordPx * view_world_per_px_);
                    geom_dirty_ = true; // force re-tessellation at the new resolution
                }
            } else if constexpr (std::is_same_v<T, GripDragCommand>) {
                using P = GripDragCommand::Phase;
                if (c.phase == P::Begin) {
                    // Arm the drag only if the entity is selectable (layer on/unlocked).
                    grip_active_ = store_.is_valid(c.handle) && selectable(c.handle);
                    grip_handle_ = c.handle;
                    grip_index_ = c.grip;
                } else if (c.phase == P::Move) {
                    grip_pos_ = c.pos; // preview recomputed in rebuild_and_publish
                } else if (c.phase == P::Commit) {
                    grip_pos_ = c.pos;
                    apply_grip_commit(c.group);
                    grip_active_ = false;
                } else { // Cancel
                    grip_active_ = false;
                }
            } else if constexpr (std::is_same_v<T, EditTextContentCommand>) {
                apply_text_edit(c.at, c.pick_radius, c.content, c.group);
            } else if constexpr (std::is_same_v<T, SetPropertyCommand>) {
                apply_set_property(c.id, c.value, c.group);
            } else if constexpr (std::is_same_v<T, SaveDocumentCommand>) {
                const io::Document doc = io::document_from_store(store_);
                const io::IoResult r =
                    c.dxf ? io::save_dxf(doc, c.path) : io::save_native(doc, c.path);
                if (r.ok) {
                    dirty_ = false;
                    ++document_version_;
                }
                report(r.message);
            } else if constexpr (std::is_same_v<T, OpenDocumentCommand>) {
                load_document_replace(command);
            } else if constexpr (std::is_same_v<T, NewDocumentCommand>) {
                new_document();
            } else if constexpr (std::is_same_v<T, AddLayerCommand>) {
                store_.add_layer(c.layer);
                geom_dirty_ = true;
                report("Layer \"" + c.layer.name + "\" added.");
            } else if constexpr (std::is_same_v<T, SetLayerCommand>) {
                store_.set_layer(c.index, c.layer);
                geom_dirty_ = true;
                prune_selection();
            } else if constexpr (std::is_same_v<T, RemoveLayerCommand>) {
                if (store_.remove_layer(c.index)) {
                    geom_dirty_ = true;
                    report("Layer removed.");
                } else {
                    report("Cannot delete layer 0, the current layer, or a layer with objects.");
                }
            } else if constexpr (std::is_same_v<T, SetCurrentLayerCommand>) {
                store_.set_current_layer(c.index);
            } else if constexpr (std::is_same_v<T, SetEntityLayerCommand>) {
                apply_entity_layer(c.index, c.group);
            } else if constexpr (std::is_same_v<T, SetEntityColorCommand>) {
                apply_entity_color(c.by_layer, c.color, c.group);
            } else if constexpr (std::is_same_v<T, AddDimStyleCommand>) {
                store_.add_dimstyle(c.style);
                geom_dirty_ = true;
                report("Dimension style \"" + c.style.name + "\" added.");
            } else if constexpr (std::is_same_v<T, SetDimStyleCommand>) {
                store_.set_dimstyle(c.index, c.style);
                geom_dirty_ = true; // dims using this style recompute on rebuild
            } else if constexpr (std::is_same_v<T, SetLineweightDisplayCommand>) {
                lineweight_display_ = c.on;
            }
        },
        command);

    // Any geometry-mutating command marks the drawing dirty. Persistence and pure
    // view/selection commands manage the flag themselves (or leave it alone).
    std::visit(
        [this](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            constexpr bool view_or_io =
                std::is_same_v<T, SetCursorCommand> || std::is_same_v<T, SelectPickCommand> ||
                std::is_same_v<T, SelectWindowCommand> || std::is_same_v<T, SelectAllCommand> ||
                std::is_same_v<T, ClearSelectionCommand> ||
                std::is_same_v<T, SaveDocumentCommand> ||
                std::is_same_v<T, OpenDocumentCommand> || std::is_same_v<T, NewDocumentCommand> ||
                std::is_same_v<T, SetLineweightDisplayCommand> ||
                std::is_same_v<T, ResolveDimObjectCommand> ||
                std::is_same_v<T, SetViewScaleCommand> ||
                std::is_same_v<T, GripDragCommand>; // Commit sets dirty_ itself
            if constexpr (!view_or_io) {
                dirty_ = true;
            }
        },
        command);
}

void GeometryEngine::load_document_replace(const Command& command) {
    const auto* open = std::get_if<OpenDocumentCommand>(&command);
    if (open == nullptr) {
        return;
    }
    io::Document doc;
    const io::IoResult r =
        open->dxf ? io::load_dxf(open->path, doc) : io::load_native(open->path, doc);
    if (!r.ok) {
        report(r.message); // store left untouched
        return;
    }
    // One store operation: clear, repopulate, rebuild the index, reset history.
    store_.clear();
    grid_.clear();
    io::populate_store(store_, doc);
    for (const EntityHandle h : all_live()) {
        Vec2 lo;
        Vec2 hi;
        if (entity_aabb(store_, h, lo, hi)) {
            grid_.insert(h, lo, hi);
        }
    }
    undo_.clear();
    redo_.clear();
    selection_.clear();
    geom_dirty_ = true;
    dirty_ = false;
    ++document_version_;
    report(r.message);
}

void GeometryEngine::new_document() {
    store_.clear();
    grid_.clear();
    undo_.clear();
    redo_.clear();
    selection_.clear();
    geom_dirty_ = true;
    dirty_ = false;
    ++document_version_;
    report("New drawing.");
}

void GeometryEngine::rebuild_and_publish() {
    if (geom_dirty_) {
        // Curves tessellate to the current zoom bucket's chord tolerance (Part A);
        // stored geometry stays parametric -- only this render payload is sampled.
        build_render_snapshot(store_, kernel_, geom_cache_, tess_tolerance_);
        geom_dirty_ = false;
        ++geom_version_;
    }
    prune_selection();

    RenderSnapshot& buf = snapshots_.write_buffer();
    buf.points = geom_cache_.points;
    buf.line_vertices = geom_cache_.line_vertices;
    buf.line_batches = geom_cache_.line_batches;
    buf.point_batches = geom_cache_.point_batches;
    buf.fill_vertices = geom_cache_.fill_vertices;
    buf.fill_batches = geom_cache_.fill_batches;
    buf.text_edit_targets = geom_cache_.text_edit_targets; // double-click-to-edit
    buf.bounds_min = geom_cache_.bounds_min;
    buf.bounds_max = geom_cache_.bounds_max;
    buf.has_bounds = geom_cache_.has_bounds;
    // Layer table + current layer are cheap and may change without a geometry
    // rebuild (e.g. SetCurrentLayer), so publish them fresh from the store.
    buf.layers.assign(store_.layers().begin(), store_.layers().end());
    buf.current_layer = store_.current_layer();
    // Dimension styles for the UI placement preview (cheap; few entries).
    buf.dimstyles.assign(store_.dimstyles().begin(), store_.dimstyles().end());

    // Pending object-dimension def points for the placement preview (Part C).
    buf.has_pending_dim = has_pending_dim_;
    buf.pending_dim_a = pending_dim_.a;
    buf.pending_dim_b = pending_dim_.b;
    buf.pending_dim_line_pt = pending_dim_.line_pt;
    buf.pending_dim_type = static_cast<std::uint8_t>(pending_dim_.type);
    buf.pending_dim_version = pending_dim_version_;

    // Publish the selection set (queryable API) and its segments (highlight/ghost).
    buf.selection = selection_;
    // Aggregate the selection's editable properties for the PR palette (geometry
    // thread -> snapshot, so the UI never queries the store). Capture is cheap over
    // the small selection set.
    {
        std::vector<Command> captured;
        captured.reserve(selection_.size());
        for (const EntityHandle h : selection_) {
            if (store_.is_valid(h)) {
                captured.push_back(capture_entity(h));
            }
        }
        buf.selection_summary = summarize_selection(captured);
    }
    buf.selected_line_vertices.clear();
    {
        std::vector<Vec2> tess;
        for (const EntityHandle h : selection_) {
            kernel_.tessellate(store_, h, kDefaultTessTolerance, tess);
            for (std::size_t s = 1; s < tess.size(); ++s) {
                buf.selected_line_vertices.push_back(tess[s - 1]);
                buf.selected_line_vertices.push_back(tess[s]);
            }
        }
    }

    // Grips of the selected set (display + hit-test) + the hot grip (grabbed during
    // a drag, else the one nearest the cursor within the pick aperture).
    buf.grips.clear();
    buf.hot_grip = -1;
    {
        std::vector<Grip> gs;
        for (const EntityHandle h : selection_) {
            if (!selectable(h)) {
                continue;
            }
            gs.clear();
            grips_of(store_, h, gs);
            for (const Grip& g : gs) {
                buf.grips.push_back(
                    GripInfo{g.pos, h, g.index, static_cast<std::uint8_t>(g.kind)});
            }
        }
        if (grip_active_) {
            for (std::size_t i = 0; i < buf.grips.size(); ++i) {
                if (buf.grips[i].handle == grip_handle_ && buf.grips[i].index == grip_index_) {
                    buf.hot_grip = static_cast<int>(i);
                    break;
                }
            }
        } else if (pick_radius_ > 0.0) {
            double best = pick_radius_ * pick_radius_;
            for (std::size_t i = 0; i < buf.grips.size(); ++i) {
                const double d2 = length_squared(buf.grips[i].pos - cursor_);
                if (d2 <= best) {
                    best = d2;
                    buf.hot_grip = static_cast<int>(i);
                }
            }
        }
    }

    // Active grip drag: preview the edited entity on a TEMPORARY store (the real
    // store is untouched -- zero op-log churn) and publish its drawable geometry.
    buf.grip_preview_segments.clear();
    buf.grip_preview_fills.clear();
    if (grip_active_ && store_.is_valid(grip_handle_)) {
        const Command edited = edit_for_grip_drag(store_, grip_handle_, grip_index_, grip_pos_);
        grip_preview_store_.clear();
        grip_preview_store_.set_layer_table(store_.layers(), store_.current_layer());
        grip_preview_store_.set_dimstyle_table(store_.dimstyles());
        const EntityProps* ep = store_.props(grip_handle_);
        add_command_to_store(grip_preview_store_, edited,
                             ep != nullptr ? *ep : EntityProps{store_.current_layer()});
        RenderSnapshot tmp;
        build_render_snapshot(grip_preview_store_, kernel_, tmp, tess_tolerance_);
        buf.grip_preview_segments = std::move(tmp.line_vertices);
        buf.grip_preview_fills = std::move(tmp.fill_vertices);
    }

    // Rollover (hover) candidate: the entity under the cursor's pick-box. Same
    // pick query as a single click; render-side highlight, no extra handoff.
    buf.has_hover = false;
    buf.hover = EntityHandle{};
    buf.hover_line_vertices.clear();
    if (pick_radius_ > 0.0) {
        const EntityHandle hv = pick_nearest(cursor_, pick_radius_);
        if (!hv.is_null() && !sel_contains(hv)) { // don't hover-highlight selected entities
            buf.hover = hv;
            buf.has_hover = true;
            std::vector<Vec2> tess;
            kernel_.tessellate(store_, hv, kDefaultTessTolerance, tess);
            for (std::size_t s = 1; s < tess.size(); ++s) {
                buf.hover_line_vertices.push_back(tess[s - 1]);
                buf.hover_line_vertices.push_back(tess[s]);
            }
        }
    }

    buf.has_snap = false;
    buf.snap_type = SnapType::None;
    if (osnap_enabled_ && pick_radius_ > 0.0) {
        const std::optional<Vec2> from = has_from_ ? std::optional<Vec2>(from_) : std::nullopt;
        const SnapResult s =
            compute_snap(store_, kernel_, grid_, cursor_, pick_radius_, snap_mask_, from);
        if (s.found) {
            buf.has_snap = true;
            buf.snap_point = s.point;
            buf.snap_type = s.type;
        }
    }

    buf.status = status_;
    buf.status_version = status_version_;
    buf.dirty = dirty_;
    buf.document_version = document_version_;
    buf.lineweight_display = lineweight_display_;

    buf.geometry_version = geom_version_;
    buf.version = version_.fetch_add(1, std::memory_order_acq_rel) + 1;
    buf.checksum = buf.compute_checksum();
    snapshots_.publish();
}

void GeometryEngine::report(std::string message) {
    status_ = std::move(message);
    ++status_version_;
}

} // namespace musacad::core

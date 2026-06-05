#include "musacad/core/geometry_engine.hpp"

#include <limits>
#include <utility>
#include <variant>

#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/osnap.hpp"
#include "musacad/core/scene_snapshot.hpp"

namespace musacad::core {

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
    EntityHandle handle;
    std::visit(
        [&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, AddLineCommand>) {
                handle = store_.add_line(c.a, c.b);
            } else if constexpr (std::is_same_v<T, AddPolylineCommand>) {
                handle = store_.add_polyline(c.points, c.closed);
            } else if constexpr (std::is_same_v<T, AddCircleCommand>) {
                handle = store_.add_circle(c.center, c.radius);
            } else if constexpr (std::is_same_v<T, AddArcCommand>) {
                handle = store_.add_arc(c.center, c.radius, c.start_angle, c.end_angle);
            }
        },
        add_command);
    return handle;
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
    switch (h.kind) {
    case EntityKind::Line: {
        const LineData* l = store_.line(h);
        return AddLineCommand{l->a, l->b, 0};
    }
    case EntityKind::Circle: {
        const CircleData* c = store_.circle(h);
        return AddCircleCommand{c->center, c->radius, 0};
    }
    case EntityKind::Arc: {
        const ArcData* a = store_.arc(h);
        return AddArcCommand{a->center, a->radius, a->start_angle, a->end_angle, 0};
    }
    case EntityKind::Polyline: {
        const PolylineData* p = store_.polyline(h);
        const auto verts = store_.vertices_of(*p);
        return AddPolylineCommand{std::vector<Vec2>(verts.begin(), verts.end()), p->closed, 0};
    }
    case EntityKind::Point:
    case EntityKind::Spline:
        break; // not produced by commands in this phase
    }
    return AddLineCommand{}; // restore no-op fallback
}

EntityHandle GeometryEngine::most_recent_live() const {
    for (auto git = undo_.rbegin(); git != undo_.rend(); ++git) {
        if (!git->is_create) {
            continue;
        }
        for (auto it = git->items.rbegin(); it != git->items.rend(); ++it) {
            if (store_.is_valid(it->handle)) {
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
    return live;
}

void GeometryEngine::push_create_item(std::uint64_t group, EntityHandle handle, Command data) {
    if (undo_.empty() || undo_.back().id != group || !undo_.back().is_create) {
        undo_.push_back(Group{group, true, {}});
    }
    undo_.back().items.push_back(Item{std::move(data), handle});
}

void GeometryEngine::push_erase_item(std::uint64_t group, Command data) {
    if (undo_.empty() || undo_.back().id != group || undo_.back().is_create) {
        undo_.push_back(Group{group, false, {}});
    }
    undo_.back().items.push_back(Item{std::move(data), EntityHandle::null()});
}

void GeometryEngine::do_undo_group() {
    if (undo_.empty()) {
        return;
    }
    Group g = std::move(undo_.back());
    undo_.pop_back();
    if (g.is_create) {
        for (Item& it : g.items) {
            remove_indexed(it.handle);
            it.handle = EntityHandle::null();
        }
    } else {
        for (Item& it : g.items) {
            it.handle = create_indexed(it.data);
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
    if (g.is_create) {
        for (Item& it : g.items) {
            it.handle = create_indexed(it.data);
        }
    } else {
        for (Item& it : g.items) {
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
    if (g.is_create) {
        remove_indexed(it.handle);
    } else {
        create_indexed(it.data);
    }
    if (g.items.empty()) {
        undo_.pop_back();
    }
}

void GeometryEngine::apply(const Command& command) {
    std::visit(
        [this, &command](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, AddLineCommand> ||
                          std::is_same_v<T, AddPolylineCommand> ||
                          std::is_same_v<T, AddCircleCommand> ||
                          std::is_same_v<T, AddArcCommand>) {
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
            }
        },
        command);
}

void GeometryEngine::rebuild_and_publish() {
    if (geom_dirty_) {
        build_render_snapshot(store_, kernel_, geom_cache_);
        geom_dirty_ = false;
    }
    RenderSnapshot& buf = snapshots_.write_buffer();
    buf.points = geom_cache_.points;
    buf.line_vertices = geom_cache_.line_vertices;
    buf.bounds_min = geom_cache_.bounds_min;
    buf.bounds_max = geom_cache_.bounds_max;
    buf.has_bounds = geom_cache_.has_bounds;

    buf.has_snap = false;
    buf.snap_type = SnapType::None;
    if (osnap_enabled_ && pick_radius_ > 0.0) {
        const SnapResult s = compute_snap(store_, kernel_, grid_, cursor_, pick_radius_);
        if (s.found) {
            buf.has_snap = true;
            buf.snap_point = s.point;
            buf.snap_type = s.type;
        }
    }

    buf.version = version_.fetch_add(1, std::memory_order_acq_rel) + 1;
    buf.checksum = buf.compute_checksum();
    snapshots_.publish();
}

} // namespace musacad::core

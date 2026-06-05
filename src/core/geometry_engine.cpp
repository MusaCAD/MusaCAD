#include "musacad/core/geometry_engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <variant>

#include "musacad/core/entity_bounds.hpp"
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

void GeometryEngine::apply_offset(Vec2 pick, double radius, double distance, Vec2 side,
                                  std::uint64_t group) {
    const EntityHandle h = pick_nearest(pick, radius);
    if (h.is_null()) {
        return;
    }
    Command add;
    if (kernel_.offset(store_, h, distance, side, add)) {
        const EntityHandle nh = create_indexed(add);
        push_create_item(group, nh, add);
        redo_.clear();
        geom_dirty_ = true;
    }
}

void GeometryEngine::apply_trim(Vec2 pick, double radius, std::uint64_t group) {
    const EntityHandle h = pick_nearest(pick, radius);
    if (h.is_null() || h.kind != EntityKind::Line) {
        return; // only line trimming is implemented (see COMMANDS.md)
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
        return; // nothing crosses this line -> nothing to trim
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
            }
        },
        command);
}

void GeometryEngine::rebuild_and_publish() {
    if (geom_dirty_) {
        build_render_snapshot(store_, kernel_, geom_cache_);
        geom_dirty_ = false;
        ++geom_version_;
    }
    prune_selection();

    RenderSnapshot& buf = snapshots_.write_buffer();
    buf.points = geom_cache_.points;
    buf.line_vertices = geom_cache_.line_vertices;
    buf.bounds_min = geom_cache_.bounds_min;
    buf.bounds_max = geom_cache_.bounds_max;
    buf.has_bounds = geom_cache_.has_bounds;

    // Publish the selection set (queryable API) and its segments (highlight/ghost).
    buf.selection = selection_;
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

    buf.geometry_version = geom_version_;
    buf.version = version_.fetch_add(1, std::memory_order_acq_rel) + 1;
    buf.checksum = buf.compute_checksum();
    snapshots_.publish();
}

} // namespace musacad::core

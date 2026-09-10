// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/geometry_engine.hpp"

#include "musacad/core/ellipse.hpp"
#include "musacad/core/spline_eval.hpp"
#include "musacad/core/units.hpp"

#include "musacad/core/dimension.hpp"
#include "musacad/core/properties_registry.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <span>
#include <optional>
#include <unordered_set>
#include <utility>
#include <variant>

#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/hatch.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/properties_registry.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/osnap.hpp"
#include "musacad/core/scene_snapshot.hpp"
#include "musacad/core/polyline_ops.hpp"
#include "musacad/core/text/mtext.hpp"
#include "musacad/core/hatch_pattern.hpp"
#include "musacad/core/table.hpp"

namespace musacad::core {

namespace {
/// Filename portion of a path (after the last '/' or '\\'), for a document tab name.
std::string doc_basename(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

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
    // The first document ("Drawing1") -- its heavy state is the engine's live members.
    // Initialised here (before the worker launches, so the geometry thread sees it).
    if (doc_metas_.empty()) {
        DocMeta m;
        m.id = next_doc_id_++;
        m.name = "Drawing" + std::to_string(++doc_name_counter_);
        doc_metas_.push_back(std::move(m));
        active_idx_ = 0;
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
    const auto& xl = store_.xlines();
    for (std::uint32_t i = 0; i < xl.slot_count(); ++i) {
        if (!xl.alive(i)) {
            continue;
        }
        const EntityHandle h{i, xl.generations()[i], EntityKind::Xline};
        if (selectable(h) && kernel_.closest_point(store_, h, world, cp)) {
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
    collect(store_.attdefs(), EntityKind::AttDef);
    collect(store_.dimensions(), EntityKind::Dimension);
    collect(store_.leaders(), EntityKind::Leader);
    collect(store_.mtexts(), EntityKind::MText);
    collect(store_.mleaders(), EntityKind::MLeader);
    collect(store_.inserts(), EntityKind::Insert);
    // Hatches were MISSING here. all_live() feeds the load-time spatial-index rebuild,
    // SelectAll and ERASE All -- so a hatch loaded from a file was never indexed and
    // could not be picked, hovered, window-selected or erased (one created in-session
    // worked, because create_indexed inserts it directly, which is why it went unseen).
    // Pre-existing; fixed here because the GD&T arenas would have inherited it exactly.
    collect(store_.xlines(), EntityKind::Xline);
    collect(store_.ellipses(), EntityKind::Ellipse);
    collect(store_.hatches(), EntityKind::Hatch);
    collect(store_.fcfs(), EntityKind::Fcf);
    collect(store_.datums(), EntityKind::Datum);
    collect(store_.images(), EntityKind::Image);
    collect(store_.tables(), EntityKind::Table);
    return live;
}

void GeometryEngine::push_create_item(std::uint64_t group, EntityHandle handle, Command data) {
    if (undo_.empty() || undo_.back().id != group) {
        undo_.push_back(Group{group, {}});
    }
    undo_.back().items.push_back(Item{std::move(data), handle, true});
}

void GeometryEngine::push_erase_item(std::uint64_t group, EntityHandle erased, Command data) {
    if (undo_.empty() || undo_.back().id != group) {
        undo_.push_back(Group{group, {}});
    }
    // The erased entity's (now dead) handle is kept as its identity in the history, so
    // when undo re-creates it the neighbouring items can be remapped to the new handle.
    undo_.back().items.push_back(Item{std::move(data), erased, false});
}

void GeometryEngine::do_undo_group() {
    if (undo_.empty()) {
        return;
    }
    Group g = std::move(undo_.back());
    undo_.pop_back();
    // Reverse the items in reverse order so a mixed (erase+create) group undoes
    // cleanly. A re-created entity gets a NEW handle; every history item that named the
    // old one is rewritten to it (see remap_history_handle). Handles are never nulled:
    // a dead handle stays as the identity token the next remap keys on.
    for (auto it = g.items.rbegin(); it != g.items.rend(); ++it) {
        if (it->is_create) {
            remove_indexed(it->handle);
        } else {
            const EntityHandle old = it->handle;
            it->handle = create_indexed(it->data);
            remap_history_handle(old, it->handle, g);
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
            const EntityHandle old = it.handle;
            it.handle = create_indexed(it.data);
            remap_history_handle(old, it.handle, g);
        } else {
            remove_indexed(it.handle);
        }
    }
    undo_.push_back(std::move(g));
}

// An entity re-created by undo/redo comes back under a NEW generational handle. Its
// other history items -- the create item of the edit that first made it, the erase item
// of a later edit that replaced it -- still name the old handle; left stale, the next
// undo's remove misses silently and the drawing keeps a duplicate (move, rotate, undo,
// undo once left two objects). Handles are unique over time, so rewriting old -> new
// across both stacks and the group in flight is exact.
void GeometryEngine::remap_history_handle(EntityHandle from, EntityHandle to, Group& in_flight) {
    if (from == to || from.is_null()) {
        return;
    }
    const auto fix = [&](Group& g) {
        for (Item& i : g.items) {
            if (i.handle == from) {
                i.handle = to;
            }
        }
    };
    fix(in_flight);
    for (Group& g : undo_) {
        fix(g);
    }
    for (Group& g : redo_) {
        fix(g);
    }
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

bool GeometryEngine::entity_hits_rect(EntityHandle h, Vec2 mn, Vec2 mx, bool crossing) const {
    // Bounding box first: a box entirely outside the rect cannot touch it, and one
    // entirely inside satisfies either mode. Only the straddling cases tessellate.
    Vec2 lo;
    Vec2 hi;
    if (entity_aabb(store_, h, lo, hi)) {
        if (hi.x < mn.x || lo.x > mx.x || hi.y < mn.y || lo.y > mx.y) {
            return false;
        }
        if (lo.x >= mn.x && hi.x <= mx.x && lo.y >= mn.y && hi.y <= mx.y) {
            return true;
        }
    }
    std::vector<Vec2> tess;
    kernel_.tessellate(store_, h, kDefaultTessTolerance, tess);
    if (tess.empty()) {
        return false;
    }
    // An INSERT tessellates to disjoint segment PAIRS (no phantom connectors);
    // crossing must test pair (2i, 2i+1), not consecutive points.
    const bool pairs = h.kind == EntityKind::Insert;
    if (crossing) {
        if (pairs) {
            for (std::size_t i = 0; i + 1 < tess.size(); i += 2) {
                if (segment_hits_rect(tess[i], tess[i + 1], mn, mx)) {
                    return true;
                }
            }
        } else {
            for (std::size_t i = 1; i < tess.size(); ++i) {
                if (segment_hits_rect(tess[i - 1], tess[i], mn, mx)) {
                    return true;
                }
            }
        }
        return tess.size() == 1 && point_in_rect(tess[0], mn, mx);
    }
    for (const Vec2& p : tess) { // window: every point must be inside
        if (!point_in_rect(p, mn, mx)) {
            return false;
        }
    }
    return true;
}

namespace {
/// Does the (semi-)infinite construction line pass through the rect [mn,mx]? Clips the
/// parametric line base + t*dir to the rect (Liang-Barsky); a RAY restricts t >= 0.
bool xline_hits_rect(const XlineData& x, Vec2 mn, Vec2 mx) {
    double t0 = x.ray ? 0.0 : -std::numeric_limits<double>::infinity();
    double t1 = std::numeric_limits<double>::infinity();
    const double px[2] = {-x.dir.x, x.dir.x};
    const double pq[2] = {x.base.x - mn.x, mx.x - x.base.x};
    const double py[2] = {-x.dir.y, x.dir.y};
    const double qy[2] = {x.base.y - mn.y, mx.y - x.base.y};
    const auto clip = [&](double pp, double qq) {
        if (std::abs(pp) < 1e-12) {
            return qq >= 0.0; // parallel: inside iff on the correct side
        }
        const double r = qq / pp;
        if (pp < 0.0) {
            t0 = std::max(t0, r);
        } else {
            t1 = std::min(t1, r);
        }
        return true;
    };
    for (int i = 0; i < 2; ++i) {
        if (!clip(px[i], pq[i]) || !clip(py[i], qy[i])) {
            return false;
        }
    }
    return t0 <= t1;
}
} // namespace

void GeometryEngine::select_window(Vec2 mn, Vec2 mx, bool crossing, bool additive,
                                   bool announce) {
    if (!additive) {
        selection_.clear();
        forget_stretch_windows();
    }
    const std::size_t before = selection_.size();
    std::vector<EntityHandle> candidates;
    grid_.query(mn, mx, candidates);
    for (const EntityHandle h : candidates) {
        if (!selectable(h)) {
            continue; // window/crossing select ignores off/frozen/locked layers
        }
        if (entity_hits_rect(h, mn, mx, crossing)) {
            sel_add(h);
        }
    }
    // Construction lines are not in the grid. A CROSSING window catches one whose
    // infinite (or semi-infinite) line passes through the rect; a plain WINDOW never
    // encloses an infinite line, so it is skipped there.
    if (crossing) {
        const auto& xl = store_.xlines();
        for (std::uint32_t i = 0; i < xl.slot_count(); ++i) {
            if (!xl.alive(i)) {
                continue;
            }
            const EntityHandle h{i, xl.generations()[i], EntityKind::Xline};
            const XlineData* xd = xl.get(i, xl.generations()[i]);
            if (xd != nullptr && selectable(h) && xline_hits_rect(*xd, mn, mx)) {
                sel_add(h);
            }
        }
    }
    // A CROSSING window is remembered for STRETCH: it is the record of which vertices
    // the user caught. An ordinary window is not -- everything it selects is enclosed,
    // and enclosed objects move whole, which needs no window to decide.
    if (crossing) {
        stretch_windows_.emplace_back(mn, mx);
    }
    note_selection_for_windows();
    if (announce) {
        const std::size_t found = selection_.size() - before;
        std::string msg = std::to_string(found) + " found";
        if (additive && before > 0) {
            msg += ", " + std::to_string(selection_.size()) + " total";
        }
        report(msg + ".");
    }
}

// ---------------------------------------------------------------------------
// Modify
// ---------------------------------------------------------------------------

namespace {

/// STRETCH (issue #24): move only the stored points that lie inside `mn..mx`, leaving the
/// rest of the entity anchored. THE per-kind rule, in one place next to translate_cmd.
///
/// Returns true if anything moved, so the caller can skip entities the window did not
/// touch rather than churning the undo log with no-op recreations.
///
/// Kinds whose shape is defined by a single point (circle, arc, text, insert, image,
/// table, hatch, GD&T) MOVE ENTIRELY when that point is enclosed and are otherwise
/// untouched -- AutoCAD does the same (a circle cannot be stretched). Dimensions move
/// their enclosed DEF POINTS, so a stretched feature's dimension RE-MEASURES for free:
/// the value is computed from those points, never baked.
/// A crossing window on record: {min, max}.
using StretchWindow = std::pair<Vec2, Vec2>;

/// Apply STRETCH's per-kind rule to one captured entity: every "stretch point" (vertex,
/// endpoint, or the single anchor of a rigid object) that lies inside ANY of `windows`
/// moves by `d`; the rest stay. Returns false if nothing moved. A point inside two
/// overlapping windows moves once -- membership is a predicate, not a sum.
bool stretch_cmd(Command& c, Vec2 d, std::span<const StretchWindow> windows) {
    const auto inside = [&](Vec2 p) {
        for (const StretchWindow& w : windows) {
            if (p.x >= w.first.x && p.x <= w.second.x && p.y >= w.first.y && p.y <= w.second.y) {
                return true;
            }
        }
        return false;
    };
    bool moved = false;
    const auto pull = [&](Vec2& p) {
        if (inside(p)) {
            p += d;
            moved = true;
        }
    };
    std::visit(
        [&](auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, AddPointCommand>) {
                pull(x.p);
            } else if constexpr (std::is_same_v<T, AddXlineCommand>) {
                pull(x.base);
            } else if constexpr (std::is_same_v<T, AddEllipseCommand>) {
                pull(x.center); // rigid, like a circle: moves whole when its centre is caught
            } else if constexpr (std::is_same_v<T, AddSplineCommand>) {
                for (Vec2& p : x.control_points) {
                    pull(p); // control points inside the window move, the rest stay
                }
            } else if constexpr (std::is_same_v<T, AddLineCommand>) {
                pull(x.a);
                pull(x.b);
            } else if constexpr (std::is_same_v<T, AddPolylineCommand>) {
                for (Vec2& v : x.points) {
                    pull(v);
                }
            } else if constexpr (std::is_same_v<T, AddArcCommand>) {
                // An arc's stretch points are its two ENDPOINTS. Both inside -> it moves
                // whole. One inside -> that end moves and the arc is rebuilt through the
                // new chord keeping its SAGITTA (height above the chord), the AutoCAD
                // rule: the arc flattens or bulges as its chord changes rather than
                // swinging about a fixed centre. Neither inside -> untouched, even if the
                // centre happens to be in the window: the centre is not a stretch point.
                double sweep = x.end_angle - x.start_angle;
                while (sweep <= 0.0) {
                    sweep += kTwoPi;
                }
                const Vec2 p0{x.center.x + x.radius * std::cos(x.start_angle),
                              x.center.y + x.radius * std::sin(x.start_angle)};
                const Vec2 p1{x.center.x + x.radius * std::cos(x.end_angle),
                              x.center.y + x.radius * std::sin(x.end_angle)};
                const bool i0 = inside(p0);
                const bool i1 = inside(p1);
                if (i0 && i1) {
                    x.center += d;
                    moved = true;
                } else if (i0 || i1) {
                    // Signed sagitta: the arc's midpoint measured along the chord's left
                    // normal. Negative for a minor CCW arc (it bulges away from the centre,
                    // which sits on the left), positive for a major one.
                    const double mid = x.start_angle + sweep * 0.5;
                    const Vec2 m{x.center.x + x.radius * std::cos(mid),
                                 x.center.y + x.radius * std::sin(mid)};
                    const Vec2 chord = p1 - p0;
                    const double len = length(chord);
                    if (len > 1e-12) {
                        const Vec2 n{-chord.y / len, chord.x / len};
                        const Vec2 q{(p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5};
                        const double h = dot(m - q, n);
                        const Vec2 np0 = i0 ? p0 + d : p0;
                        const Vec2 np1 = i1 ? p1 + d : p1;
                        const Vec2 nc = np1 - np0;
                        const double nl = length(nc);
                        if (nl > 1e-12 && std::abs(h) > 1e-9) {
                            const Vec2 nn{-nc.y / nl, nc.x / nl};
                            const Vec2 nq{(np0.x + np1.x) * 0.5, (np0.y + np1.y) * 0.5};
                            const double r = (nl * nl * 0.25 + h * h) / (2.0 * std::abs(h));
                            // Centre: r back from the new arc midpoint, on the side away
                            // from it -- left of the chord for a minor arc, right for major.
                            const double sgn = h < 0.0 ? -1.0 : 1.0;
                            const Vec2 centre{nq.x + nn.x * (h - sgn * r), nq.y + nn.y * (h - sgn * r)};
                            x.center = centre;
                            x.radius = r;
                            x.start_angle = std::atan2(np0.y - centre.y, np0.x - centre.x);
                            x.end_angle = std::atan2(np1.y - centre.y, np1.x - centre.x);
                            moved = true;
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, AddDimensionCommand>) {
                // Def points re-measure; the placement point follows so the dimension
                // line keeps its offset from the feature it now describes.
                pull(x.a);
                pull(x.b);
                pull(x.line_pt);
            } else if constexpr (std::is_same_v<T, AddLeaderCommand>) {
                pull(x.tip);
                pull(x.knee);
            } else if constexpr (std::is_same_v<T, AddMLeaderCommand>) {
                for (Vec2& v : x.vertices) {
                    pull(v);
                }
            } else if constexpr (std::is_same_v<T, AddHatchCommand>) {
                for (std::vector<Vec2>& loop : x.loops) {
                    for (Vec2& v : loop) {
                        pull(v);
                    }
                }
            } else if constexpr (std::is_same_v<T, AddDatumCommand>) {
                pull(x.tip);
                pull(x.pos);
            } else if constexpr (requires { x.center; }) {
                pull(x.center); // circle: cannot be stretched; moves if its centre is caught
            } else if constexpr (requires { x.pos; }) {
                pull(x.pos); // text, mtext, insert, image, table, FCF: one anchor
            }
        },
        c);
    return moved;
}

void translate_cmd(Command& c, Vec2 d) {
    std::visit(
        [&](auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, AddPointCommand>) {
                x.p += d;
            } else if constexpr (std::is_same_v<T, AddXlineCommand>) {
                x.base += d;
            } else if constexpr (std::is_same_v<T, AddEllipseCommand>) {
                x.center += d;
            } else if constexpr (std::is_same_v<T, AddSplineCommand>) {
                for (Vec2& p : x.control_points) {
                    p += d;
                }
            } else if constexpr (std::is_same_v<T, AddLineCommand>) {
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
            } else if constexpr (std::is_same_v<T, AddAttDefCommand>) {
                x.text.pos += d;
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
            } else if constexpr (std::is_same_v<T, AddInsertCommand>) {
                x.pos += d;
            } else if constexpr (std::is_same_v<T, AddHatchCommand>) {
                for (std::vector<Vec2>& loop : x.loops) {
                    for (Vec2& p : loop) {
                        p += d;
                    }
                }
                x.pattern_origin += d;
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
            if constexpr (std::is_same_v<T, AddPointCommand>) {
                x.p = refl(x.p);
            } else if constexpr (std::is_same_v<T, AddXlineCommand>) {
                const Vec2 tip = refl(x.base + x.dir);
                x.base = refl(x.base);
                x.dir = normalized(tip - x.base);
            } else if constexpr (std::is_same_v<T, AddSplineCommand>) {
                for (Vec2& p : x.control_points) {
                    p = refl(p);
                }
            } else if constexpr (std::is_same_v<T, AddEllipseCommand>) {
                // Reflection reverses orientation: the mirrored curve is the ellipse with
                // the reflected axes traversed through parameters -end..-start.
                const Vec2 tip = refl(x.center + x.major);
                const EllipseData before{x.center, x.major, x.ratio, x.start, x.end, {}};
                x.center = refl(x.center);
                x.major = tip - x.center;
                if (!ellipse::is_full(before)) {
                    const auto norm = [](double a) {
                        a = std::fmod(a, kTwoPi);
                        return a < 0.0 ? a + kTwoPi : a;
                    };
                    const double s = norm(-x.end);
                    const double e = norm(-x.start);
                    x.start = s;
                    x.end = e;
                }
            } else if constexpr (std::is_same_v<T, AddLineCommand>) {
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
            } else if constexpr (std::is_same_v<T, AddAttDefCommand>) {
                x.text.pos = refl(x.text.pos);
                x.text.rotation = refl_ang(x.text.rotation);
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
            } else if constexpr (std::is_same_v<T, AddInsertCommand>) {
                // Mirror the insertion point + orientation; negating one scale axis
                // reflects the referenced geometry without baking it.
                x.pos = refl(x.pos);
                x.rotation = refl_ang(x.rotation);
                x.scale_y = -x.scale_y;
            } else if constexpr (std::is_same_v<T, AddHatchCommand>) {
                for (std::vector<Vec2>& loop : x.loops) {
                    for (Vec2& p : loop) {
                        p = refl(p);
                    }
                }
                x.pattern_origin = refl(x.pattern_origin);
                x.pattern_angle = refl_ang(x.pattern_angle);
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
            if constexpr (std::is_same_v<T, AddPointCommand>) {
                x.p = rot(x.p);
            } else if constexpr (std::is_same_v<T, AddXlineCommand>) {
                const Vec2 tip = rot(x.base + x.dir);
                x.base = rot(x.base);
                x.dir = normalized(tip - x.base);
            } else if constexpr (std::is_same_v<T, AddEllipseCommand>) {
                const Vec2 tip = rot(x.center + x.major);
                x.center = rot(x.center);
                x.major = tip - x.center;
            } else if constexpr (std::is_same_v<T, AddSplineCommand>) {
                for (Vec2& p : x.control_points) {
                    p = rot(p);
                }
            } else if constexpr (std::is_same_v<T, AddLineCommand>) {
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
            } else if constexpr (std::is_same_v<T, AddAttDefCommand>) {
                x.text.pos = rot(x.text.pos);
                x.text.rotation += ang;
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
            } else if constexpr (std::is_same_v<T, AddInsertCommand>) {
                x.pos = rot(x.pos);
                x.rotation += ang;
            } else if constexpr (std::is_same_v<T, AddHatchCommand>) {
                for (std::vector<Vec2>& loop : x.loops) {
                    for (Vec2& p : loop) {
                        p = rot(p);
                    }
                }
                x.pattern_origin = rot(x.pattern_origin);
                x.pattern_angle += ang;
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
            if constexpr (std::is_same_v<T, AddPointCommand>) {
                out = x.p;
            } else if constexpr (std::is_same_v<T, AddXlineCommand>) {
                out = x.base;
            } else if constexpr (std::is_same_v<T, AddEllipseCommand>) {
                out = x.center;
            } else if constexpr (std::is_same_v<T, AddSplineCommand>) {
                out = x.control_points.empty() ? Vec2{0.0, 0.0} : x.control_points.front();
            } else if constexpr (std::is_same_v<T, AddLineCommand>) {
                out = x.a;
            } else if constexpr (std::is_same_v<T, AddCircleCommand>) {
                out = x.center;
            } else if constexpr (std::is_same_v<T, AddArcCommand>) {
                out = x.center;
            } else if constexpr (std::is_same_v<T, AddPolylineCommand>) {
                out = x.points.empty() ? Vec2{0.0, 0.0} : x.points.front();
            } else if constexpr (std::is_same_v<T, AddAttDefCommand>) {
                out = x.text.pos;
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
            } else if constexpr (std::is_same_v<T, AddInsertCommand>) {
                out = x.pos;
            } else if constexpr (std::is_same_v<T, AddHatchCommand>) {
                out = (x.loops.empty() || x.loops.front().empty()) ? Vec2{0.0, 0.0}
                                                                   : x.loops.front().front();
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
            if constexpr (std::is_same_v<T, AddPointCommand>) {
                x.p = scl(x.p);
            } else if constexpr (std::is_same_v<T, AddXlineCommand>) {
                x.base = scl(x.base);
            } else if constexpr (std::is_same_v<T, AddEllipseCommand>) {
                x.center = scl(x.center);
                x.major = x.major * f;
            } else if constexpr (std::is_same_v<T, AddSplineCommand>) {
                for (Vec2& p : x.control_points) {
                    p = scl(p);
                }
            } else if constexpr (std::is_same_v<T, AddLineCommand>) {
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
            } else if constexpr (std::is_same_v<T, AddAttDefCommand>) {
                x.text.pos = scl(x.text.pos);
                x.text.height *= f;
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
            } else if constexpr (std::is_same_v<T, AddInsertCommand>) {
                x.pos = scl(x.pos);
                x.scale_x *= f;
                x.scale_y *= f;
            } else if constexpr (std::is_same_v<T, AddHatchCommand>) {
                for (std::vector<Vec2>& loop : x.loops) {
                    for (Vec2& p : loop) {
                        p = scl(p);
                    }
                }
                x.pattern_origin = scl(x.pattern_origin);
                x.pattern_scale *= f; // the pattern scales with the boundary
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
            push_erase_item(group, h, original);
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

namespace {
/// Signed area of a closed point list (the shoelace formula); the sign encodes winding,
/// which callers do not need, so they take the absolute value.
double shoelace(const std::vector<Vec2>& p) {
    double a = 0.0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const Vec2& u = p[i];
        const Vec2& v = p[(i + 1) % p.size()];
        a += u.x * v.y - v.x * u.y;
    }
    return a * 0.5;
}
double path_length(const std::vector<Vec2>& p, bool closed) {
    double L = 0.0;
    for (std::size_t i = 1; i < p.size(); ++i) {
        L += distance(p[i - 1], p[i]);
    }
    if (closed && p.size() > 2) {
        L += distance(p.back(), p.front());
    }
    return L;
}
} // namespace

EntityHandle GeometryEngine::most_recent_dimension() const {
    // Walk the undo log backwards, exactly as most_recent_live() does -- no new state to
    // keep in sync, and it naturally follows undo: undo the last dimension and the chain
    // continues from the one before it.
    for (auto git = undo_.rbegin(); git != undo_.rend(); ++git) {
        for (auto it = git->items.rbegin(); it != git->items.rend(); ++it) {
            if (it->is_create && it->handle.kind == EntityKind::Dimension &&
                store_.is_valid(it->handle)) {
                return it->handle;
            }
        }
    }
    return EntityHandle::null();
}

void GeometryEngine::apply_chain_dimension(Vec2 at, bool baseline, std::uint64_t group) {
    const EntityHandle prev = most_recent_dimension();
    if (!store_.is_valid(prev)) {
        report(baseline ? "DIMBASELINE: no previous dimension to stack from."
                        : "DIMCONTINUE: no previous dimension to continue from.");
        return;
    }
    const DimData* p = store_.dimension(prev);
    if (p->type != DimType::Linear && p->type != DimType::Aligned) {
        // Continuing a radius/diameter/angular dimension is not defined; say so rather
        // than producing something arbitrary.
        report(baseline ? "DIMBASELINE needs a linear or aligned dimension to stack from."
                        : "DIMCONTINUE needs a linear or aligned dimension to continue from.");
        return;
    }

    // The previous dimension's line direction and which way its dim line sits relative to
    // the def points -- both derived, so a chain follows a dimension that was later moved.
    const Vec2 dir = p->type == DimType::Aligned && length_squared(p->b - p->a) > 1e-18
                         ? normalized(p->b - p->a)
                         : (std::abs(p->b.x - p->a.x) >= std::abs(p->b.y - p->a.y) ? Vec2{1, 0}
                                                                                   : Vec2{0, 1});
    // Which side of the def points the dimension line sits on. This must be measured
    // along the PERPENDICULAR: `line_pt - foot_of_a` is parallel to `dir` by construction
    // (the foot IS the projection of `a` onto the line through `line_pt` along `dir`), so
    // using it gives a direction along the dimension instead of away from it, and a
    // baseline stack that never offsets.
    const Vec2 perp{-dir.y, dir.x};
    const Vec2 away = dot(p->line_pt - p->a, perp) >= 0.0 ? perp : perp * -1.0;

    const DimStyle* raw = store_.dimstyle(p->style);
    const DimStyle st = apply_dim_overrides(raw != nullptr ? *raw : DimStyle{}, p->overrides);
    // Baseline spacing follows AutoCAD's DIMDLI default proportion (3.75 mm at 2.5 mm
    // text). Derived from the text height rather than stored as its own style variable --
    // a deliberate simplification, recorded in docs/TODO.md.
    const double spacing = st.text_height * 1.5;

    DimData d;
    d.type = p->type;
    d.a = baseline ? p->a : p->b; // stack from the first origin, or continue from the second
    d.b = at;
    d.line_pt = baseline ? p->line_pt + away * spacing : p->line_pt;
    d.style = p->style;
    d.props = p->props;
    d.overrides = p->overrides; // the chain inherits the previous dimension's look

    AddDimensionCommand cmd;
    cmd.type = static_cast<std::uint8_t>(d.type);
    cmd.a = d.a;
    cmd.b = d.b;
    cmd.line_pt = d.line_pt;
    cmd.style = d.style;
    cmd.group = group;
    cmd.props = d.props;
    cmd.overrides = d.overrides;
    const Command command = cmd;
    const EntityHandle nh = create_indexed(command);
    push_create_item(group, nh, command);
    redo_.clear();
    geom_dirty_ = true;
    report(baseline ? "Baseline dimension added." : "Continued dimension added.");
}

void GeometryEngine::apply_area_query(Vec2 at, double radius) {
    const EntityHandle h = pick_nearest(at, radius);
    if (!store_.is_valid(h)) {
        report("AREA: no object found at that point.");
        return;
    }
    // Circles and arcs have exact closed forms; everything else is measured from the
    // SAME tessellation the renderer draws, so the reported area matches what is on
    // screen rather than an independent approximation.
    if (h.kind == EntityKind::Circle) {
        const CircleData* c = store_.circle(h);
        const double r = c->radius;
        report("Area = " + fmt_len(kPi * r * r) + ",  Circumference = " + fmt_len(kTwoPi * r));
        return;
    }
    std::vector<Vec2> pts;
    kernel_.tessellate(store_, h, kDefaultTessTolerance, pts);
    if (pts.size() < 2) {
        report("AREA: that object has no measurable extent.");
        return;
    }
    bool closed = distance(pts.front(), pts.back()) < 1e-9;
    if (h.kind == EntityKind::Polyline) {
        const PolylineData* pl = store_.polyline(h);
        closed = closed || pl->closed;
    }
    if (closed) {
        report("Area = " + fmt_len(std::abs(shoelace(pts))) + ",  Perimeter = " +
               fmt_len(path_length(pts, true)));
    } else {
        // An open path has no area; saying so is better than reporting the area of the
        // polygon you would get by closing it, which is what the number would mean.
        report("Length = " + fmt_len(path_length(pts, false)) + "  (open object -- no area)");
    }
}

void GeometryEngine::apply_list_query(Vec2 at, double radius) {
    const EntityHandle h = pick_nearest(at, radius);
    if (!store_.is_valid(h)) {
        report("LIST: no object found at that point.");
        return;
    }
    const EntityProps* pr = store_.props(h);
    const std::string layer =
        pr != nullptr && pr->layer < store_.layers().size() ? store_.layers()[pr->layer].name : "?";
    std::string out = std::string(kind_name(h.kind)) + "  on layer \"" + layer + "\"";
    switch (h.kind) {
    case EntityKind::Ellipse: {
        const EllipseData* e = store_.ellipse(h);
        const double a = length(e->major);
        out += ",  center (" + fmt_len(e->center.x) + "," + fmt_len(e->center.y) + "),  major radius " +
               fmt_len(a) + ",  minor radius " + fmt_len(a * e->ratio) + ",  rotation " +
               fmt_ang(std::atan2(e->major.y, e->major.x));
        if (!ellipse::is_full(*e)) {
            out += ",  start parameter " + fmt_ang(e->start) + ",  end parameter " +
                   fmt_ang(e->end);
        }
        break;
    }
    case EntityKind::Xline: {
        const XlineData* x = store_.xline(h);
        out += std::string(x->ray ? ",  ray from (" : ",  construction line through (") +
               fmt_len(x->base.x) + "," + fmt_len(x->base.y) + "),  direction (" + fmt_len(x->dir.x) + "," +
               fmt_len(x->dir.y) + ")";
        break;
    }
    case EntityKind::Line: {
        const LineData* l = store_.line(h);
        out += ",  from (" + fmt_len(l->a.x) + "," + fmt_len(l->a.y) + ") to (" + fmt_len(l->b.x) + "," +
               fmt_len(l->b.y) + "),  length " + fmt_len(distance(l->a, l->b));
        break;
    }
    case EntityKind::Circle: {
        const CircleData* c = store_.circle(h);
        out += ",  centre (" + fmt_len(c->center.x) + "," + fmt_len(c->center.y) + "),  radius " +
               fmt_len(c->radius);
        break;
    }
    case EntityKind::Arc: {
        const ArcData* a = store_.arc(h);
        out += ",  centre (" + fmt_len(a->center.x) + "," + fmt_len(a->center.y) + "),  radius " +
               fmt_len(a->radius) + ",  from " + fmt_ang(a->start_angle) + " to " +
               fmt_ang(a->end_angle);
        break;
    }
    case EntityKind::Polyline: {
        const PolylineData* p = store_.polyline(h);
        out += ",  " + std::to_string(p->count) + " vertices,  " +
               (p->closed ? "closed" : "open");
        break;
    }
    case EntityKind::Text: {
        const TextData* t = store_.text(h);
        out += ",  height " + fmt_len(t->height) + ",  \"" + std::string(store_.string_of(*t)) + "\"";
        break;
    }
    case EntityKind::AttDef: {
        const AttDefData* a = store_.attdef(h);
        out += ",  tag \"" + std::string(store_.string_of(a->text)) + "\",  prompt \"" +
               std::string(store_.attdef_prompt(*a)) + "\",  default \"" +
               std::string(store_.attdef_default(*a)) + "\"";
        if ((a->flags & kAttInvisible) != 0) {
            out += ",  invisible";
        }
        if ((a->flags & kAttConstant) != 0) {
            out += ",  constant";
        }
        if ((a->flags & kAttVerify) != 0) {
            out += ",  verify";
        }
        if ((a->flags & kAttPreset) != 0) {
            out += ",  preset";
        }
        break;
    }
    case EntityKind::Dimension: {
        const DimData* d = store_.dimension(h);
        // Report the MEASURED value -- the whole point of the entity is that this is
        // computed from the def points and cannot have been authored.
        out += ",  measures " + fmt_len(dim_measure(*d));
        if (!store_.dim_override(*d).empty()) {
            out += ",  text override \"" + std::string(store_.dim_override(*d)) + "\"";
        }
        break;
    }
    case EntityKind::Table: {
        const TableData* t = store_.table(h);
        out += ",  " + std::to_string(t->rows) + " rows x " + std::to_string(t->cols) +
               " columns";
        break;
    }
    default:
        break;
    }
    report(out);
}

std::vector<GeometryEngine::StretchEdit> GeometryEngine::stretched_commands(Vec2 delta) const {
    std::vector<StretchEdit> out;
    // The recorded crossing windows describe THIS selection only. If anything replaced
    // the selection since (an edit, a paste, a new document) they are meaningless, and
    // every selected object falls back to moving whole -- which is exactly what AutoCAD
    // does for objects that were not selected by a crossing window.
    const bool windows_valid = !stretch_windows_.empty() && stretch_windows_sel_ == selection_;
    // "Crossed by a recorded window" does not depend on the drag, so it is classified
    // once per (selection, windows, edit state) and reused for every preview frame.
    if (!(stretch_class_valid_ && stretch_class_serial_ == edit_serial_ &&
          stretch_class_handles_ == selection_ && stretch_class_windows_ == stretch_windows_)) {
        stretch_class_crossed_.assign(selection_.size(), false);
        if (windows_valid) {
            for (std::size_t i = 0; i < selection_.size(); ++i) {
                const EntityHandle h = selection_[i];
                if (!store_.is_valid(h)) {
                    continue;
                }
                for (const auto& w : stretch_windows_) {
                    if (entity_hits_rect(h, w.first, w.second, /*crossing=*/true)) {
                        stretch_class_crossed_[i] = true;
                        break;
                    }
                }
            }
        }
        stretch_class_handles_ = selection_;
        stretch_class_windows_ = stretch_windows_;
        stretch_class_serial_ = edit_serial_;
        stretch_class_valid_ = true;
    }
    for (std::size_t i = 0; i < selection_.size(); ++i) {
        const EntityHandle h = selection_[i];
        if (!store_.is_valid(h) || !selectable(h)) {
            continue;
        }
        const bool crossed = stretch_class_crossed_[i];
        Command edited = capture_entity(h);
        bool changed = false;
        if (crossed) {
            // Partly enclosed: only the caught vertices move. A line that merely passes
            // through the window with both ends outside is crossed but unchanged, and
            // AutoCAD leaves it alone too -- `changed` stays false and it is skipped.
            changed = stretch_cmd(edited, delta, stretch_windows_);
        } else {
            translate_cmd(edited, delta); // enclosed or picked: moved whole
            changed = true;
        }
        if (changed) {
            out.push_back(StretchEdit{h, std::move(edited)});
        }
    }
    return out;
}

void GeometryEngine::apply_stretch(Vec2 delta, std::uint64_t group) {
    stretch_preview_active_ = false; // the rubber band ends with the commit
    prune_selection();
    if (selection_.empty()) {
        report("Nothing selected to stretch.");
        return;
    }
    const std::vector<StretchEdit> edits = stretched_commands(delta);
    if (edits.empty()) {
        report("Nothing to stretch: no vertex of the selection lies inside the crossing window.");
        return;
    }
    std::vector<EntityHandle> result;
    for (const StretchEdit& e : edits) {
        const Command original = capture_entity(e.handle);
        remove_indexed(e.handle);
        push_erase_item(group, e.handle, original);
        const EntityHandle nh = create_indexed(e.edited);
        push_create_item(group, nh, e.edited);
        result.push_back(nh);
    }
    selection_ = result;
    forget_stretch_windows(); // the caught vertices have moved out from under the windows
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report("Stretched " + std::to_string(edits.size()) +
           (edits.size() == 1 ? " object." : " objects."));
}

void GeometryEngine::apply_copy_clipboard() {
    if (selection_.empty()) {
        report("Nothing selected to copy.");
        return;
    }
    Clipboard cb;
    // Snapshot the source document's named tables so paste can remap by name even after
    // the source document is switched away or closed.
    cb.src_layers.assign(store_.layers().begin(), store_.layers().end());
    cb.src_dimstyles.assign(store_.dimstyles().begin(), store_.dimstyles().end());
    cb.src_blocks.assign(store_.blocks().begin(), store_.blocks().end());
    cb.src_fonts.assign(store_.fonts().begin(), store_.fonts().end());
    Vec2 lo{std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    Vec2 hi{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};
    bool any_bounds = false;
    for (const EntityHandle h : selection_) {
        if (!store_.is_valid(h)) {
            continue;
        }
        cb.items.push_back(capture_entity(h));
        Vec2 a;
        Vec2 b;
        if (entity_aabb(store_, h, a, b)) {
            lo = {std::min(lo.x, a.x), std::min(lo.y, a.y)};
            hi = {std::max(hi.x, b.x), std::max(hi.y, b.y)};
            any_bounds = true;
        }
    }
    if (cb.items.empty()) {
        report("Nothing selected to copy.");
        return;
    }
    cb.base = any_bounds ? lo : Vec2{0.0, 0.0};
    cb.has = true;
    clipboard_ = std::move(cb);
    report(std::to_string(clipboard_.items.size()) + " copied to clipboard.");
}

void GeometryEngine::apply_cut_clipboard(std::uint64_t group) {
    apply_copy_clipboard();
    if (!clipboard_.has) {
        return; // nothing to cut
    }
    const std::vector<EntityHandle> sel = selection_;
    std::size_t erased = 0;
    for (const EntityHandle h : sel) {
        if (!store_.is_valid(h)) {
            continue;
        }
        const Command original = capture_entity(h);
        remove_indexed(h);
        push_erase_item(group, h, original);
        ++erased;
    }
    selection_.clear();
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report(std::to_string(erased) + " cut to clipboard.");
}

void GeometryEngine::apply_paste_clipboard(Vec2 at, std::uint64_t group, bool at_cursor) {
    if (!clipboard_.has || clipboard_.items.empty()) {
        report("Clipboard is empty.");
        return;
    }
    // Paste-at-cursor lands the clip's reference point at `at`; otherwise keep the
    // original world coordinates (tab-to-tab drag).
    const Vec2 offset = at_cursor ? (at - clipboard_.base) : Vec2{0.0, 0.0};
    // Resolve a source table index -> a target index BY NAME (creating if missing), the
    // same get-or-add pattern DXF import uses. Memoised per paste.
    std::unordered_map<std::uint16_t, std::uint16_t> layer_map;
    std::unordered_map<std::uint16_t, std::uint16_t> dim_map;
    std::unordered_map<std::uint16_t, std::uint16_t> block_map;
    const auto ensure_layer = [&](std::uint16_t i) -> std::uint16_t {
        const auto it = layer_map.find(i);
        if (it != layer_map.end()) {
            return it->second;
        }
        const std::uint16_t out =
            i < clipboard_.src_layers.size() ? store_.add_layer(clipboard_.src_layers[i]) : 0;
        layer_map[i] = out;
        return out;
    };
    const auto ensure_dim = [&](std::uint16_t i) -> std::uint16_t {
        const auto it = dim_map.find(i);
        if (it != dim_map.end()) {
            return it->second;
        }
        const std::uint16_t out = i < clipboard_.src_dimstyles.size()
                                      ? store_.add_dimstyle(clipboard_.src_dimstyles[i])
                                      : 0;
        dim_map[i] = out;
        return out;
    };
    // Font references travel as a SOURCE font-table index; resolve to the source NAME, then
    // get-or-add it in the target. (Empty name = the built-in stroke font, index 0.)
    const auto ensure_font = [&](std::uint16_t i) -> std::uint16_t {
        return store_.add_font(i < clipboard_.src_fonts.size() ? std::string_view{clipboard_.src_fonts[i]}
                                                               : std::string_view{});
    };
    // Deep-copy a referenced block definition tree by name, remapping nested-insert block
    // indices AND block-internal entity layers/fonts so the pasted block resolves + renders.
    // `in_progress` breaks self-referential / cyclic block definitions (malformed input)
    // instead of recursing forever.
    std::unordered_set<std::uint16_t> in_progress;
    std::function<std::uint16_t(std::uint16_t)> ensure_block = [&](std::uint16_t i) -> std::uint16_t {
        const auto it = block_map.find(i);
        if (it != block_map.end()) {
            return it->second;
        }
        if (i >= clipboard_.src_blocks.size() || !in_progress.insert(i).second) {
            return 0; // out of range, or a cycle -> placeholder block 0 (no infinite loop)
        }
        BlockDef def = clipboard_.src_blocks[i];
        for (auto& e : def.content.lines) {
            e.props.layer = ensure_layer(e.props.layer);
        }
        for (auto& e : def.content.circles) {
            e.props.layer = ensure_layer(e.props.layer);
        }
        for (auto& e : def.content.arcs) {
            e.props.layer = ensure_layer(e.props.layer);
        }
        for (auto& e : def.content.polylines) {
            e.props.layer = ensure_layer(e.props.layer);
        }
        for (auto& e : def.content.texts) {
            e.props.layer = ensure_layer(e.props.layer);
        }
        for (auto& e : def.content.mtexts) {
            e.props.layer = ensure_layer(e.props.layer);
            e.block.font = ensure_font(e.block.font);
        }
        for (auto& ins : def.content.inserts) {
            ins.block = ensure_block(ins.block); // recursive closure (block tree)
            ins.props.layer = ensure_layer(ins.props.layer);
        }
        const std::uint16_t out = store_.add_block(def);
        block_map[i] = out;
        in_progress.erase(i);
        return out;
    };

    std::vector<EntityHandle> pasted;
    pasted.reserve(clipboard_.items.size());
    for (Command cmd : clipboard_.items) { // a working copy per item
        std::visit(
            [&](auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (requires { c.props; }) {
                    if (c.props) {
                        c.props->layer = ensure_layer(c.props->layer);
                    }
                }
                if constexpr (std::is_same_v<T, AddDimensionCommand> ||
                              std::is_same_v<T, AddLeaderCommand> ||
                              std::is_same_v<T, AddMLeaderCommand>) {
                    c.style = ensure_dim(c.style);
                }
                // MLeader's label font now travels as a NAME (c.font, like TEXT/MTEXT/LEADER)
                // and is resolved to the target document's font table on apply -- no index
                // remap needed.
                if constexpr (std::is_same_v<T, AddInsertCommand>) {
                    c.block = ensure_block(c.block);
                }
            },
            cmd);
        translate_cmd(cmd, offset);
        const EntityHandle nh = create_indexed(cmd);
        push_create_item(group, nh, cmd);
        pasted.push_back(nh);
    }
    selection_ = pasted; // the pasted entities become the new selection
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report(std::to_string(pasted.size()) + " pasted.");
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
            push_erase_item(group, h, original);
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

void GeometryEngine::apply_rotate(Vec2 base, double angle, std::uint64_t group, bool copy) {
    const std::vector<EntityHandle> sel = selection_;
    std::vector<EntityHandle> out;
    for (const EntityHandle h : sel) {
        if (!store_.is_valid(h)) {
            continue;
        }
        const Command original = capture_entity(h);
        Command result = original;
        rotate_cmd(result, base, angle);
        if (!copy) {
            remove_indexed(h);
            push_erase_item(group, h, original);
        }
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

void GeometryEngine::apply_scale(Vec2 base, double factor, std::uint64_t group, bool copy) {
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
        if (!copy) {
            remove_indexed(h);
            push_erase_item(group, h, original);
        }
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

namespace {

/// A curve flattened to a polyline, with cumulative arc length: THE way a station and a
/// tangent are found along a path. Shared by ARRAYPATH, DIVIDE and MEASURE so the three
/// cannot disagree about where "40% along this arc" is.
struct PathSampler {
    std::vector<Vec2> pts;
    std::vector<double> cum;
    double total = 0.0;
    bool closed = false;

    [[nodiscard]] bool valid() const { return pts.size() >= 2 && total > 1e-12; }

    void build(std::vector<Vec2> polyline) {
        pts = std::move(polyline);
        cum.assign(pts.size(), 0.0);
        for (std::size_t i = 1; i < pts.size(); ++i) {
            cum[i] = cum[i - 1] + length(pts[i] - pts[i - 1]);
        }
        total = cum.empty() ? 0.0 : cum.back();
        closed = pts.size() >= 2 && length(pts.back() - pts.front()) <= 1e-9;
    }

    /// Position and tangent direction at arc length `s`.
    void at(double s, Vec2& p, double& ang) const {
        std::size_t i = 1;
        while (i + 1 < pts.size() && cum[i] < s) {
            ++i;
        }
        const double seg = cum[i] - cum[i - 1];
        const double t = seg > 1e-12 ? (s - cum[i - 1]) / seg : 0.0;
        const Vec2 d = pts[i] - pts[i - 1];
        p = pts[i - 1] + d * t;
        ang = std::atan2(d.y, d.x);
    }
};

} // namespace

void GeometryEngine::apply_array_rect(int rows, int cols, double dx, double dy, double angle,
                                      std::uint64_t group) {
    rows = std::max(rows, 1);
    cols = std::max(cols, 1);
    const std::vector<EntityHandle> sel = selection_;
    if (sel.empty()) {
        report("Nothing selected to array.");
        return;
    }
    // AutoCAD's axis angle rotates the ROW/COLUMN DIRECTIONS, not the items: a rotated
    // rectangular array is a skewed lattice of upright copies.
    const double cs = std::cos(angle);
    const double sn = std::sin(angle);
    int made = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (r == 0 && c == 0) {
                continue; // the originals stay in place
            }
            const double ax = static_cast<double>(c) * dx;
            const double ay = static_cast<double>(r) * dy;
            const Vec2 d{ax * cs - ay * sn, ax * sn + ay * cs};
            for (const EntityHandle h : sel) {
                if (!store_.is_valid(h)) {
                    continue;
                }
                Command copy = capture_entity(h);
                translate_cmd(copy, d);
                const EntityHandle nh = create_indexed(copy);
                push_create_item(group, nh, copy);
                ++made;
            }
        }
    }
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report(made == 0 ? "Array: a 1x1 array adds nothing."
                     : "Array created: " + std::to_string(made) + " copies.");
}

void GeometryEngine::apply_array_path(const ArrayPathCommand& c) {
    const std::vector<EntityHandle> sel = selection_;
    if (sel.empty()) {
        report("Nothing selected to array.");
        return;
    }
    const EntityHandle path = pick_nearest(c.pick, c.pick_radius);
    if (path.is_null()) {
        report("Path array: no curve under the pick.");
        return;
    }
    // The path must not also be part of what is being arrayed, or each copy would drag
    // a copy of the path along with it.
    if (std::find(sel.begin(), sel.end(), path) != sel.end()) {
        report("Path array: the path curve is part of the selection. Deselect it first.");
        return;
    }

    // One tessellation is THE path: stations and tangents both come from it, so the
    // items cannot land somewhere the curve does not go. Fine enough that the chord
    // error is far below what item placement can show.
    std::vector<Vec2> flat;
    kernel_.tessellate(store_, path, std::max(tess_tolerance_ * 0.25, 1e-6), flat);
    PathSampler sampler;
    sampler.build(std::move(flat));
    if (!sampler.valid()) {
        report("Path array: that entity has no length to array along.");
        return;
    }
    const double total = sampler.total;
    const bool closed = sampler.closed;

    // Where the items go. Divide (spacing 0) spreads `count` over the whole path;
    // Measure steps every `spacing`, with `count` capping how many (0 = as many as fit).
    std::vector<double> stations;
    if (c.spacing > 1e-12) {
        const int cap = c.count > 0 ? c.count : std::numeric_limits<int>::max();
        for (int i = 0; i < cap; ++i) {
            const double s = static_cast<double>(i) * c.spacing;
            if (s > total + 1e-9) {
                break;
            }
            stations.push_back(std::min(s, total));
        }
    } else {
        const int n = std::max(c.count, 1);
        if (n == 1) {
            stations.push_back(0.0);
        } else {
            // A closed path has no distinct end station -- placing one there would sit
            // an item on top of the one at the start.
            const double denom = closed ? static_cast<double>(n) : static_cast<double>(n - 1);
            for (int i = 0; i < n; ++i) {
                stations.push_back(total * static_cast<double>(i) / denom);
            }
        }
    }
    if (stations.size() < 2) {
        report("Path array: need at least two items.");
        return;
    }

    // The selection rides the path by ONE shared base point, so a multi-entity
    // selection keeps its internal arrangement instead of scattering entity by entity.
    Vec2 base = c.base;
    if (!c.has_base) {
        Vec2 lo{0, 0};
        Vec2 hi{0, 0};
        bool any = false;
        for (const EntityHandle h : sel) {
            Vec2 l;
            Vec2 g;
            if (store_.is_valid(h) && entity_aabb(store_, h, l, g)) {
                lo = any ? Vec2{std::min(lo.x, l.x), std::min(lo.y, l.y)} : l;
                hi = any ? Vec2{std::max(hi.x, g.x), std::max(hi.y, g.y)} : g;
                any = true;
            }
        }
        if (!any) {
            report("Nothing selected to array.");
            return;
        }
        base = {(lo.x + hi.x) * 0.5, (lo.y + hi.y) * 0.5};
    }

    double ang0 = 0.0;
    Vec2 p0;
    sampler.at(stations[0], p0, ang0);

    int made = 0;
    for (std::size_t i = 0; i < stations.size(); ++i) {
        Vec2 p;
        double ang = 0.0;
        sampler.at(stations[i], p, ang);
        for (const EntityHandle h : sel) {
            if (!store_.is_valid(h)) {
                continue;
            }
            Command copy = capture_entity(h);
            if (c.align) {
                // Turn each copy by how far the tangent has swung since the FIRST
                // station, about the shared base, so item 0 keeps the orientation the
                // user drew and the rest follow the curve.
                rotate_cmd(copy, base, ang - ang0);
            }
            translate_cmd(copy, p - base);
            const EntityHandle nh = create_indexed(copy);
            push_create_item(c.group, nh, copy);
            ++made;
        }
    }
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report("Path array created: " + std::to_string(made) + " copies along the path.");
}

void GeometryEngine::apply_divide_measure(const DividePathCommand& c) {
    const bool divide = c.segments > 0;
    const EntityHandle path = pick_nearest(c.pick, c.pick_radius);
    if (path.is_null()) {
        report(divide ? "Divide: no curve under the pick." : "Measure: no curve under the pick.");
        return;
    }
    std::vector<Vec2> flat;
    kernel_.tessellate(store_, path, std::max(tess_tolerance_ * 0.25, 1e-6), flat);
    PathSampler sampler;
    sampler.build(std::move(flat));
    if (!sampler.valid()) {
        report("That entity has no length to divide.");
        return;
    }

    // AutoCAD's placement rules differ between the two, and the difference is the whole
    // point of having both:
    //   DIVIDE  n segments -> n-1 points on an OPEN curve (the ends already divide it),
    //           but n points on a CLOSED one, where there is no free end.
    //   MEASURE d          -> a point every d from the start, never one AT the start.
    std::vector<double> stations;
    if (divide) {
        const int n = c.segments;
        const int first = sampler.closed ? 0 : 1;
        const int last = sampler.closed ? n - 1 : n - 1;
        for (int i = first; i <= last; ++i) {
            stations.push_back(sampler.total * static_cast<double>(i) / static_cast<double>(n));
        }
    } else {
        if (c.distance <= 1e-12) {
            report("Measure: the segment length must be positive.");
            return;
        }
        for (double d = c.distance; d <= sampler.total + 1e-9; d += c.distance) {
            stations.push_back(std::min(d, sampler.total));
        }
    }
    if (stations.empty()) {
        report(divide ? "Divide: that needs at least two segments."
                      : "Measure: the segment length is longer than the curve.");
        return;
    }

    // The marks are ordinary POINT entities, exactly as in AutoCAD: they snap with
    // Node, they select, they erase, and the curve itself is left alone. Props are left
    // unset so each mark lands on the CURRENT layer, like any other fresh draw.
    for (const double st : stations) {
        Vec2 p;
        double ang = 0.0;
        sampler.at(st, p, ang);
        Command mark = AddPointCommand{p, c.group, {}};
        const EntityHandle nh = create_indexed(mark);
        push_create_item(c.group, nh, mark);
    }
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report((divide ? "Divided: " : "Measured: ") + std::to_string(stations.size()) +
           " points placed.");
}

void GeometryEngine::apply_array_polar(Vec2 center, int count, double total_angle,
                                       bool rotate_items, std::uint64_t group) {
    const std::vector<EntityHandle> sel = selection_;
    if (sel.empty()) {
        report("Nothing selected to array.");
        return;
    }
    if (count < 2) {
        report("Polar array: need at least two items.");
        return;
    }
    // Full circle distributes count items evenly; a partial fill spans the angle
    // across (count-1) gaps (AutoCAD semantics).
    const bool full = std::abs(std::abs(total_angle) - kTwoPi) < 1e-6;
    const double step = full ? total_angle / static_cast<double>(count)
                             : total_angle / static_cast<double>(count - 1);
    int made = 0;
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
            ++made;
        }
    }
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report("Polar array created: " + std::to_string(made) + " copies.");
}

// The nearest boundary hit strictly forward of the moving end `mov` along fix->mov.
// Boundaries can be anywhere, so scan live entities (EXTEND is interactive/infrequent).
bool GeometryEngine::nearest_boundary_ahead(EntityHandle self, Vec2 fix, Vec2 mov,
                                            Vec2& target) const {
    const Vec2 dir = normalized(mov - fix);
    double best = std::numeric_limits<double>::infinity();
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
        if (c == self) {
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
        } else if (c.kind == EntityKind::Polyline) {
            // Each segment of a polyline boundary (straight ones exactly; arc segments
            // through their chord tessellation from the kernel would be approximate, so
            // only straight segments count here).
            const PolylineData* pl = store_.polyline(c);
            const std::span<const Vec2> pv = store_.vertices_of(*pl);
            const std::span<const double> pb = store_.bulges_of(*pl);
            const std::size_t pn = pv.size();
            const std::size_t pm = pl->closed ? pn : (pn > 0 ? pn - 1 : 0);
            for (std::size_t i = 0; i < pm; ++i) {
                if (!pb.empty() && std::abs(pb[i]) > 1e-12) {
                    continue;
                }
                const Vec2 q0 = pv[i];
                const Vec2 q1 = pv[(i + 1) % pn];
                Vec2 p{};
                if (NativeKernel2D::line_line_intersection(fix, mov, q0, q1, p)) {
                    const Vec2 md = q1 - q0;
                    const double u = dot(p - q0, md) / std::max(length_squared(md), 1e-18);
                    if (u >= -1e-9 && u <= 1.0 + 1e-9) {
                        consider(p);
                    }
                }
            }
        }
    }
    return found;
}

void GeometryEngine::apply_extend_arc(EntityHandle h, Vec2 pick, std::uint64_t group) {
    const ArcData* arc = store_.arc(h);
    const Vec2 centre = arc->center;
    const double r = arc->radius;
    const double start = arc->start_angle;
    const EntityProps props = arc->props;
    const double cts = store_.celtscale(h);
    double total = arc->end_angle - start;
    while (total <= 0.0) {
        total += kTwoPi;
    }
    // Which END grows: the one nearer the pick. Growing the end means sweeping FORWARD
    // past `total`; growing the start means sweeping BACKWARD past 0, which is the same
    // search run on the reversed arc.
    const Vec2 start_pt{centre.x + r * std::cos(start), centre.y + r * std::sin(start)};
    const Vec2 end_pt{centre.x + r * std::cos(arc->end_angle),
                      centre.y + r * std::sin(arc->end_angle)};
    const bool grow_end = length_squared(pick - end_pt) <= length_squared(pick - start_pt);

    // Candidates near the arc's FULL circle, since the extension leaves the arc's own box.
    std::vector<EntityHandle> cand;
    grid_.query(Vec2{centre.x - r, centre.y - r}, Vec2{centre.x + r, centre.y + r}, cand);

    // The nearest crossing strictly beyond the growing end, measured as extra sweep.
    double best = 0.0;
    bool found = false;
    std::vector<Vec2> hits;
    for (const EntityHandle c : cand) {
        if (c == h) {
            continue;
        }
        // Intersect the FULL circle the arc lies on: a boundary the arc does not reach
        // yet is exactly what we are extending to, so the arc's own sweep must not filter.
        hits.clear();
        if (c.kind == EntityKind::Line) {
            const LineData* m = store_.line(c);
            Vec2 p0{};
            Vec2 p1{};
            const int n =
                NativeKernel2D::line_circle_intersection(m->a, m->b, centre, r, p0, p1);
            if (n >= 1) {
                hits.push_back(p0);
            }
            if (n == 2) {
                hits.push_back(p1);
            }
        } else if (c.kind == EntityKind::Circle || c.kind == EntityKind::Arc) {
            // Curve-vs-curve: tessellate the other entity and cross each of its segments
            // with this arc's circle. Accurate to the tessellation, which is the same
            // guarantee the kernel's own curve-curve fallback gives.
            std::vector<Vec2> poly;
            kernel_.tessellate(store_, c, std::max(tess_tolerance_, 1e-6), poly);
            for (std::size_t i = 1; i < poly.size(); ++i) {
                Vec2 p0{};
                Vec2 p1{};
                const int n = NativeKernel2D::line_circle_intersection(poly[i - 1], poly[i], centre,
                                                                       r, p0, p1);
                if (n >= 1) {
                    hits.push_back(p0);
                }
                if (n == 2) {
                    hits.push_back(p1);
                }
            }
        } else {
            continue;
        }
        for (const Vec2& p : hits) {
            // Extra sweep beyond the growing end, always positive going the growth way.
            double extra = grow_end
                               ? std::atan2(p.y - centre.y, p.x - centre.x) - arc->end_angle
                               : start - std::atan2(p.y - centre.y, p.x - centre.x);
            while (extra <= 1e-9) {
                extra += kTwoPi;
            }
            // Never wrap all the way round onto the arc itself.
            if (extra + total >= kTwoPi - 1e-9) {
                continue;
            }
            if (!found || extra < best) {
                best = extra;
                found = true;
            }
        }
    }
    if (!found) {
        report("Extend: no boundary ahead of that end.");
        return;
    }
    const Command extended =
        grow_end ? AddArcCommand{centre, r, start, start + total + best, 0, props, cts}
                 : AddArcCommand{centre, r, start - best, start + total, 0, props, cts};
    const Command original = capture_entity(h);
    remove_indexed(h);
    push_erase_item(group, h, original);
    push_create_item(group, create_indexed(extended), extended);
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report("Extended.");
}

void GeometryEngine::apply_extend(Vec2 pick, double radius, std::uint64_t group) {
    const EntityHandle h = pick_nearest(pick, radius);
    if (h.is_null()) {
        report("Extend: nothing under the pick.");
        return;
    }
    if (h.kind == EntityKind::Arc) {
        apply_extend_arc(h, pick, group);
        return;
    }
    if (h.kind == EntityKind::Polyline) {
        const PolylineData* pl = store_.polyline(h);
        if (pl->closed) {
            report("Extend: a closed polyline has no open end.");
            return;
        }
        const std::span<const Vec2> vs = store_.vertices_of(*pl);
        const std::span<const double> bs = store_.bulges_of(*pl);
        std::vector<Vec2> v(vs.begin(), vs.end());
        std::vector<double> b(v.size(), 0.0);
        if (!bs.empty()) {
            b.assign(bs.begin(), bs.end());
        }
        if (v.size() < 2) {
            return;
        }
        // The end nearer the pick grows, straight along its last segment.
        const bool at_end = length_squared(pick - v.back()) <= length_squared(pick - v.front());
        const std::size_t mov_i = at_end ? v.size() - 1 : 0;
        const std::size_t fix_i = at_end ? v.size() - 2 : 1;
        const std::size_t seg_i = at_end ? v.size() - 2 : 0;
        if (std::abs(b[seg_i]) > 1e-12) {
            report("Extend: extending an arc segment of a polyline is not supported yet.");
            return;
        }
        Vec2 target{};
        if (!nearest_boundary_ahead(h, v[fix_i], v[mov_i], target)) {
            report("Extend: no boundary ahead of that end.");
            return;
        }
        const EntityProps props = pl->props;
        const double cts = store_.celtscale(h);
        v[mov_i] = target;
        const Command original = capture_entity(h);
        remove_indexed(h);
        push_erase_item(group, h, original);
        const Command extended = AddPolylineCommand{v, false, 0, props, b, cts};
        push_create_item(group, create_indexed(extended), extended);
        redo_.clear();
        geom_dirty_ = true;
        report("Extended.");
        return;
    }
    if (h.kind != EntityKind::Line) {
        report("Extend: only lines, arcs and open polylines can be extended.");
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
    Vec2 target{};
    if (!nearest_boundary_ahead(h, fix, mov, target)) {
        report("Extend: no boundary ahead of that end.");
        return;
    }
    // Same object, longer: keep its properties rather than stamping the current layer.
    const EntityProps props = store_.line(h)->props;
    const double cts = store_.celtscale(h);
    const Command original = capture_entity(h);
    remove_indexed(h);
    push_erase_item(group, h, original);
    const Command extended = AddLineCommand{fix, target, 0, props, cts};
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
                                      DimData& out, Vec2 pick3, Vec2 pick4) const {
    const auto dt = static_cast<DimType>(type);
    const EntityHandle h1 = pick_nearest(pick1, radius);
    if (h1.is_null()) {
        return false;
    }
    out.type = dt;
    out.style = 0;
    out.aux = 0.0;
    if (dt == DimType::ArcLength) {
        // An arc entity, or the bulged polyline segment nearest the pick: centre, start
        // point and end angle (counter-clockwise); placement at the second pick.
        Vec2 c{};
        double r = 0.0;
        double s0 = 0.0;
        double s1 = 0.0;
        if (h1.kind == EntityKind::Arc) {
            const ArcData* arc = store_.arc(h1);
            c = arc->center;
            r = arc->radius;
            s0 = arc->start_angle;
            s1 = arc->end_angle;
        } else if (h1.kind == EntityKind::Polyline) {
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
                const double dd = std::abs(distance(pick1, a.center) - a.radius);
                if (dd < best) {
                    best = dd;
                    c = a.center;
                    r = a.radius;
                    const double a0 = std::atan2(v[i].y - c.y, v[i].x - c.x);
                    const double theta = 4.0 * std::atan(b[i]);
                    s0 = theta > 0.0 ? a0 : a0 + theta;
                    s1 = theta > 0.0 ? a0 + theta : a0;
                    found = true;
                }
            }
            if (!found) {
                return false;
            }
        } else {
            return false;
        }
        out.a = c;
        out.b = {c.x + r * std::cos(s0), c.y + r * std::sin(s0)};
        out.aux = s1;
        out.line_pt = pick2;
        return true;
    }
    if (dt == DimType::Radius || dt == DimType::Diameter || dt == DimType::Jogged) {
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
        if (dt == DimType::Jogged) {
            // The dimension line runs from the CENTRE OVERRIDE (pick3) through the
            // dimension-line location (pick2) to the arc; the jog sits at pick4's
            // projection along it (half-way when no jog location was given).
            const bool has_override = pick3.x != 0.0 || pick3.y != 0.0;
            const Vec2 oc = has_override ? pick3 : center;
            Vec2 u = pick2 - oc;
            u = length_squared(u) > 1e-12 ? normalized(u) : dir;
            Vec2 p0{};
            Vec2 p1{};
            const int n = NativeKernel2D::line_circle_intersection(oc, oc + u, center, r, p0, p1);
            Vec2 edge = center + u * r;
            double best_t = std::numeric_limits<double>::infinity();
            for (int i = 0; i < n; ++i) {
                const Vec2 p = i == 0 ? p0 : p1;
                const double t = dot(p - oc, u);
                if (t > 1e-9 && t < best_t) {
                    best_t = t;
                    edge = p;
                }
            }
            out.b = edge;
            out.line_pt = oc;
            const double len = distance(oc, edge);
            double frac = 0.5;
            if (len > 1e-9 && (pick4.x != 0.0 || pick4.y != 0.0)) {
                frac = std::clamp(dot(pick4 - oc, u) / len, 0.1, 0.9);
            }
            out.aux = frac;
        }
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

void GeometryEngine::apply_revcloud_object(const RevcloudObjectCommand& c) {
    const EntityHandle h = pick_nearest(c.pick, c.pick_radius);
    if (h.is_null()) {
        report("Revision cloud: no object under the pick.");
        return;
    }
    if (h.kind != EntityKind::Line && h.kind != EntityKind::Arc && h.kind != EntityKind::Circle &&
        h.kind != EntityKind::Polyline && h.kind != EntityKind::Spline) {
        report("Revision cloud: pick a line, arc, circle, polyline or spline to convert.");
        return;
    }
    std::vector<Vec2> flat;
    kernel_.tessellate(store_, h, std::max(tess_tolerance_, 1e-6), flat);
    if (flat.size() < 2) {
        report("Revision cloud: that object has no length.");
        return;
    }
    const bool closed = length(flat.back() - flat.front()) <= 1e-9;
    if (closed) {
        flat.pop_back(); // the loop closes itself; a repeated vertex would make a zero lobe
    }
    std::vector<Vec2> verts;
    std::vector<double> bulges;
    polyline_ops::revcloud_from_path(flat, closed, c.arc_len, false, verts, bulges);
    if (verts.size() < 2) {
        report("Revision cloud: arc length is too large for that object.");
        return;
    }
    const EntityProps* ep = store_.props(h);
    AddPolylineCommand cloud;
    cloud.points = std::move(verts);
    cloud.bulges = std::move(bulges);
    cloud.closed = closed;
    cloud.props = ep != nullptr ? *ep : EntityProps{store_.current_layer()};
    // AutoCAD converts the object: the cloud replaces it, as one undo group.
    const Command original = capture_entity(h);
    remove_indexed(h);
    push_erase_item(c.group, h, original);
    const Command cmd = cloud;
    const EntityHandle nh = create_indexed(cmd);
    push_create_item(c.group, nh, cmd);
    selection_ = {nh};
    forget_stretch_windows();
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report("Converted to a revision cloud of " + std::to_string(cloud.points.size()) + " arcs.");
}

void GeometryEngine::apply_revcloud_reverse(std::uint64_t group) {
    prune_selection();
    std::vector<EntityHandle> out;
    int flipped = 0;
    for (const EntityHandle h : selection_) {
        if (h.kind != EntityKind::Polyline || !store_.is_valid(h)) {
            out.push_back(h);
            continue;
        }
        Command edited = capture_entity(h);
        auto& pc = std::get<AddPolylineCommand>(edited);
        if (pc.bulges.empty()) {
            out.push_back(h);
            continue;
        }
        for (double& b : pc.bulges) {
            b = -b;
        }
        const Command original = capture_entity(h);
        remove_indexed(h);
        push_erase_item(group, h, original);
        const EntityHandle nh = create_indexed(edited);
        push_create_item(group, nh, edited);
        out.push_back(nh);
        ++flipped;
    }
    selection_ = out;
    if (flipped == 0) {
        report("Reverse direction: no revision cloud is selected.");
        return;
    }
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report("Reversed the arc direction.");
}

void GeometryEngine::apply_explode(std::uint64_t group) {
    prune_selection();
    if (selection_.empty()) {
        report("Nothing selected to explode.");
        return;
    }
    // Each kind contributes its components to `parts`; the shared tail swaps them in.
    std::vector<Command> parts;
    const auto line = [&](Vec2 a, Vec2 b, const EntityProps& pr) {
        if (length(b - a) > 1e-12) {
            parts.push_back(AddLineCommand{a, b, 0, pr});
        }
    };
    const auto lines_from_pairs = [&](const std::vector<Vec2>& segs, const EntityProps& pr) {
        for (std::size_t i = 0; i + 1 < segs.size(); i += 2) {
            line(segs[i], segs[i + 1], pr);
        }
    };
    // AutoCAD makes 2D SOLIDs of filled arrowheads; the nearest thing here is a SOLID
    // hatch per triangle, which renders and plots as the same filled shape.
    const auto solids_from_tris = [&](const std::vector<Vec2>& tris, const EntityProps& pr) {
        for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
            AddHatchCommand hc;
            hc.loops = {{tris[i], tris[i + 1], tris[i + 2]}};
            hc.pattern_name = "SOLID";
            hc.props = pr;
            parts.push_back(std::move(hc));
        }
    };
    const auto text = [&](std::string content, Vec2 pos, double h, double rot, std::uint8_t just,
                          std::string font, const EntityProps& pr) {
        if (content.empty()) {
            return;
        }
        AddTextCommand tc;
        tc.pos = pos;
        tc.height = h;
        tc.rotation = rot;
        tc.justify = just;
        tc.content = std::move(content);
        tc.props = pr;
        tc.font = std::move(font);
        parts.push_back(std::move(tc));
    };
    // Polyline vertices + bulges -> lines and arcs (width/tangent info, which this model
    // does not hold anyway, is what AutoCAD discards here).
    const auto explode_polyline = [&](const std::vector<Vec2>& pts, const std::vector<double>& bulges,
                                      bool closed, const EntityProps& pr) {
        const std::size_t n = pts.size();
        const std::size_t segs = closed ? n : (n > 0 ? n - 1 : 0);
        for (std::size_t i = 0; i < segs; ++i) {
            const Vec2 a = pts[i];
            const Vec2 b = pts[(i + 1) % n];
            const double bulge = i < bulges.size() ? bulges[i] : 0.0;
            if (bulge == 0.0) {
                line(a, b, pr);
                continue;
            }
            const BulgeArc arc = arc_from_bulge(a, b, bulge);
            if (arc.radius <= 1e-12) {
                line(a, b, pr);
                continue;
            }
            // Arcs are stored CCW from start to end: a CW bulge runs from b back to a.
            if (arc.sweep > 0.0) {
                parts.push_back(AddArcCommand{arc.center, arc.radius, arc.a0, arc.a0 + arc.sweep, 0, pr});
            } else {
                parts.push_back(AddArcCommand{arc.center, arc.radius, arc.a0 + arc.sweep, arc.a0, 0, pr});
            }
        }
    };

    std::vector<EntityHandle> exploded;
    int skipped = 0;
    for (const EntityHandle h : selection_) {
        if (!store_.is_valid(h) || !selectable(h)) {
            continue;
        }
        const std::size_t before = parts.size();
        switch (h.kind) {
        case EntityKind::Polyline: {
            const PolylineData* pl = store_.polyline(h);
            const std::span<const Vec2> v = store_.vertices_of(*pl);
            const std::span<const double> bg = store_.bulges_of(*pl);
            explode_polyline(std::vector<Vec2>(v.begin(), v.end()),
                             std::vector<double>(bg.begin(), bg.end()), pl->closed, pl->props);
            break;
        }
        case EntityKind::Insert: {
            // One grouping level: the block's members come out as their own kinds, placed
            // by the insert's transform; a nested insert stays an insert, transformed.
            const InsertData* in = store_.insert(h);
            if (in->block >= store_.blocks().size()) {
                break;
            }
            const BlockDef& def = store_.blocks()[in->block];
            const double cs = std::cos(in->rotation);
            const double sn = std::sin(in->rotation);
            const auto xf = [&](Vec2 p) {
                const double lx = (p.x - def.base.x) * in->scale_x;
                const double ly = (p.y - def.base.y) * in->scale_y;
                return Vec2{in->pos.x + lx * cs - ly * sn, in->pos.y + lx * sn + ly * cs};
            };
            const bool uniform = std::abs(in->scale_x - in->scale_y) < 1e-12 && in->scale_x > 0.0;
            const double us = std::abs(in->scale_x);
            for (const LineData& l : def.content.lines) {
                line(xf(l.a), xf(l.b), l.props);
            }
            for (const CircleData& ci : def.content.circles) {
                if (uniform) {
                    parts.push_back(AddCircleCommand{xf(ci.center), ci.radius * us, 0, ci.props});
                } else {
                    std::vector<Vec2> ring; // a non-uniform scale makes it an ellipse: approximate
                    for (int k = 0; k < 64; ++k) {
                        const double a = kTwoPi * static_cast<double>(k) / 64.0;
                        ring.push_back(xf({ci.center.x + ci.radius * std::cos(a),
                                           ci.center.y + ci.radius * std::sin(a)}));
                    }
                    AddPolylineCommand pc;
                    pc.points = std::move(ring);
                    pc.closed = true;
                    pc.props = ci.props;
                    parts.push_back(std::move(pc));
                }
            }
            for (const ArcData& ar : def.content.arcs) {
                if (uniform) {
                    parts.push_back(AddArcCommand{xf(ar.center), ar.radius * us,
                                                  ar.start_angle + in->rotation,
                                                  ar.end_angle + in->rotation, 0, ar.props});
                } else {
                    double sweep = ar.end_angle - ar.start_angle;
                    while (sweep <= 0.0) {
                        sweep += kTwoPi;
                    }
                    std::vector<Vec2> run;
                    const int nseg = std::max(8, static_cast<int>(sweep / (kPi / 16.0)));
                    for (int k = 0; k <= nseg; ++k) {
                        const double a = ar.start_angle + sweep * static_cast<double>(k) / nseg;
                        run.push_back(xf({ar.center.x + ar.radius * std::cos(a),
                                          ar.center.y + ar.radius * std::sin(a)}));
                    }
                    AddPolylineCommand pc;
                    pc.points = std::move(run);
                    pc.props = ar.props;
                    parts.push_back(std::move(pc));
                }
            }
            for (const BlockPolyline& bp : def.content.polylines) {
                AddPolylineCommand pc;
                for (const Vec2& q : bp.verts) {
                    pc.points.push_back(xf(q));
                }
                pc.bulges = bp.bulges; // a bulge is scale-invariant under uniform scale
                if (!uniform && !pc.bulges.empty()) {
                    pc.bulges.clear(); // arcs do not survive a non-uniform scale exactly
                }
                pc.closed = bp.closed;
                pc.props = bp.props;
                parts.push_back(std::move(pc));
            }
            for (const BlockText& bt : def.content.texts) {
                text(bt.content, xf(bt.pos), bt.height * us, bt.rotation + in->rotation, bt.justify,
                     std::string{}, bt.props);
            }
            for (const BlockAttDef& ba : def.content.attdefs) {
                // As in AutoCAD, an exploded attribute is its definition again (the tag
                // shows; the value is dropped).
                AddAttDefCommand ac;
                ac.text = AddTextCommand{xf(ba.text.pos), ba.text.height * us,
                                         ba.text.rotation + in->rotation, ba.text.justify, ba.tag, 0,
                                         ba.text.props};
                ac.prompt = ba.prompt;
                ac.def = ba.def;
                ac.flags = ba.flags;
                parts.push_back(std::move(ac));
            }
            for (const BlockMText& bm : def.content.mtexts) {
                AddMTextCommand mc;
                mc.block = bm.block;
                mc.block.pos = xf(bm.block.pos);
                mc.block.height *= us;
                mc.block.width *= us;
                mc.block.rotation += in->rotation;
                mc.content = bm.content;
                mc.props = bm.props;
                mc.font = std::string(store_.font_name(bm.block.font));
                parts.push_back(std::move(mc));
            }
            for (const InsertData& sub : def.content.inserts) {
                parts.push_back(AddInsertCommand{sub.block, xf(sub.pos), sub.scale_x * in->scale_x,
                                                 sub.scale_y * in->scale_y,
                                                 sub.rotation + in->rotation, 0, sub.props,
                                                 store_.insert_attribs(sub)});
            }
            break;
        }
        case EntityKind::Dimension: {
            const DimData* d = store_.dimension(h);
            const DimStyle* st = store_.dimstyle(d->style);
            const DimGeometry g = compute_dim_geometry(*d, st != nullptr ? *st : DimStyle{}, Rgb{},
                                                       store_.dim_text_parts(*d));
            lines_from_pairs(g.ext_lines, d->props);
            lines_from_pairs(g.dim_lines, d->props);
            lines_from_pairs(g.arrow_lines, d->props);
            solids_from_tris(g.arrow_fills, d->props);
            text(g.label, g.text_pos, g.text_height, g.text_rotation,
                 static_cast<std::uint8_t>(g.text_justify), std::string{}, d->props);
            text(g.label2, g.label2_pos, g.text_height, g.text_rotation,
                 static_cast<std::uint8_t>(g.text_justify), std::string{}, d->props);
            break;
        }
        case EntityKind::Leader: {
            const LeaderData* l = store_.leader(h);
            const DimStyle* st = store_.dimstyle(l->style);
            const DimStyle s = apply_dim_overrides(st != nullptr ? *st : DimStyle{}, l->overrides);
            line(l->tip, l->knee, l->props);
            std::vector<Vec2> af;
            std::vector<Vec2> al;
            append_arrowhead(af, al, l->tip, l->knee - l->tip, s.arrow_size,
                             static_cast<ArrowType>(s.arrow_type));
            lines_from_pairs(al, l->props);
            solids_from_tris(af, l->props);
            text(std::string(store_.string_of(*l)), l->knee + Vec2{s.arrow_size * 0.4, 0.0},
                 l->text_height, 0.0, 0, std::string(store_.font_name(l->font)), l->props);
            break;
        }
        case EntityKind::MLeader: {
            const MLeaderData* m = store_.mleader(h);
            const DimStyle* st = store_.dimstyle(m->style);
            const DimStyle s = apply_dim_overrides(st != nullptr ? *st : DimStyle{}, m->overrides);
            const std::span<const Vec2> v = store_.vertices_of(*m);
            for (std::size_t i = 1; i < v.size(); ++i) {
                line(v[i - 1], v[i], m->props);
            }
            if (v.size() >= 2) {
                std::vector<Vec2> af;
                std::vector<Vec2> al;
                append_arrowhead(af, al, v[0], v[1] - v[0], s.arrow_size,
                                 static_cast<ArrowType>(s.arrow_type));
                lines_from_pairs(al, m->props);
                solids_from_tris(af, m->props);
            }
            AddMTextCommand mc;
            mc.block = m->text;
            mc.content = std::string(store_.string_of(m->text));
            mc.props = m->props;
            mc.font = std::string(store_.font_name(m->text.font));
            parts.push_back(std::move(mc));
            break;
        }
        case EntityKind::Hatch: {
            const HatchData* hd = store_.hatch(h);
            const std::vector<std::vector<Vec2>> loops = store_.hatch_loops(*hd);
            const std::string_view pname = store_.string_of(*hd);
            if (const hatch::Pattern* pat = pname == "SOLID" ? nullptr : hatch::builtin_pattern(pname)) {
                std::vector<hatch::Segment> segs;
                hatch::generate_pattern_segments(loops, *pat, hd->pattern_scale, hd->pattern_angle,
                                                 hd->pattern_origin, segs);
                for (const hatch::Segment& sg : segs) {
                    line(sg.a, sg.b, hd->props);
                }
            } else {
                // A filled region has no lines to hand back; its boundary is what remains.
                for (const std::vector<Vec2>& loop : loops) {
                    if (loop.size() >= 2) {
                        AddPolylineCommand pc;
                        pc.points = loop;
                        pc.closed = true;
                        pc.props = hd->props;
                        parts.push_back(std::move(pc));
                    }
                }
            }
            break;
        }
        case EntityKind::MText: {
            const MTextData* m = store_.mtext(h);
            const std::string_view font = store_.font_name(m->text.font);
            const text::MTextLayout lay = text::layout_mtext(m->text, store_.string_of(m->text),
                                                             store_.font_engine(), font);
            for (const text::MTextLine& ln : lay.lines) {
                text(ln.text, ln.origin, m->text.height, m->text.rotation, 0, std::string(font),
                     m->props);
            }
            break;
        }
        case EntityKind::Table: {
            const TableData* td = store_.table(h);
            const TableStyle* st = store_.table_style(td->style);
            const TableGeometry g = compute_table_geometry(
                *td, store_.table_cell_views(*td), store_.table_col_widths(*td),
                store_.table_row_heights(*td), st != nullptr ? *st : TableStyle{}, Rgb{});
            lines_from_pairs(g.lines, td->props);
            for (std::size_t i = 0; i < g.cell_text.size(); ++i) {
                text(g.cell_text[i], g.text_pos[i], g.text_height[i], g.rotation, 0, std::string{},
                     td->props);
            }
            break;
        }
        case EntityKind::Point:
        case EntityKind::Line:
        case EntityKind::Circle:
        case EntityKind::Arc:
        case EntityKind::Spline:
        case EntityKind::Text:
        case EntityKind::Fcf:
        case EntityKind::Datum:
        case EntityKind::Image:
        case EntityKind::Xline:
        case EntityKind::Ellipse:
        case EntityKind::AttDef:
            break; // already simple, or nothing meaningful to break into
        }
        if (parts.size() > before) {
            exploded.push_back(h);
        } else {
            ++skipped;
        }
    }
    if (exploded.empty()) {
        report("Nothing to explode: the selection holds only simple objects.");
        return;
    }
    std::vector<EntityHandle> result;
    for (const EntityHandle h : exploded) {
        const Command original = capture_entity(h);
        remove_indexed(h);
        push_erase_item(group, h, original);
    }
    for (const Command& part : parts) {
        const EntityHandle nh = create_indexed(part);
        push_create_item(group, nh, part);
        result.push_back(nh);
    }
    selection_ = result;
    forget_stretch_windows();
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    std::string msg = "Exploded " + std::to_string(exploded.size()) +
                      (exploded.size() == 1 ? " object into " : " objects into ") +
                      std::to_string(parts.size()) + ".";
    if (skipped > 0) {
        msg += " " + std::to_string(skipped) + " could not be exploded.";
    }
    report(msg);
}

std::string GeometryEngine::fmt_len(double v) const {
    return units::format_length(v, store_.units());
}

std::string GeometryEngine::fmt_ang(double radians) const {
    return units::format_angle(radians, store_.units());
}

void GeometryEngine::apply_purge(std::uint8_t what) {
    // Walk each table BACKWARDS: a removal reindexes every reference above the slot it
    // drops, so going down means the indices still to be examined never move under us.
    const bool all = what == 0;
    int layers = 0;
    int dimstyles = 0;
    int tstyles = 0;
    int blocks = 0;
    int images = 0;
    int groups = 0;
    if (all || what == 4) {
        for (std::size_t i = store_.layer_count(); i-- > 1;) {
            layers += store_.remove_layer(static_cast<std::uint16_t>(i)) ? 1 : 0;
        }
    }
    if (all || what == 2) {
        for (std::size_t i = store_.dimstyles().size(); i-- > 1;) {
            dimstyles += store_.remove_dimstyle(static_cast<std::uint16_t>(i)) ? 1 : 0;
        }
    }
    if (all || what == 5) {
        for (std::size_t i = store_.table_styles().size(); i-- > 1;) {
            tstyles += store_.remove_table_style(static_cast<std::uint16_t>(i)) ? 1 : 0;
        }
    }
    if (all || what == 1) {
        // Repeat until stable: a block only referenced from another unused block frees
        // up once that block is gone.
        for (bool again = true; again;) {
            again = false;
            for (std::size_t i = store_.block_count(); i-- > 0;) {
                if (store_.remove_block(static_cast<std::uint16_t>(i))) {
                    ++blocks;
                    again = true;
                }
            }
        }
    }
    if (all || what == 6) {
        for (std::size_t i = store_.image_defs().size(); i-- > 0;) {
            images += store_.remove_image_def(static_cast<std::uint16_t>(i)) ? 1 : 0;
        }
    }
    int tstyles_text = 0;
    if (all || what == 7) {
        for (std::size_t i = store_.text_styles().size(); i-- > 1;) {
            tstyles_text += store_.remove_text_style(static_cast<std::uint16_t>(i)) ? 1 : 0;
        }
    }
    if (all || what == 3) {
        std::vector<EntityGroup> kept;
        for (const EntityGroup& g : store_.groups()) {
            bool alive = false;
            for (const EntityHandle m : g.members) {
                alive = alive || store_.is_valid(m);
            }
            if (alive) {
                kept.push_back(g);
            } else {
                ++groups;
            }
        }
        if (groups > 0) {
            store_.set_groups(std::move(kept));
        }
    }
    const int total = layers + dimstyles + tstyles + blocks + images + groups + tstyles_text;
    if (total == 0) {
        report("Purge: nothing to purge.");
        return;
    }
    // Indices moved, so every cached AABB key is stale in the same way a layer change
    // is; a full re-publish is the cheapest correct answer.
    geom_dirty_ = true;
    dirty_ = true;
    std::string msg = "Purged";
    const auto part = [&](int n, const char* one, const char* many) {
        if (n > 0) {
            msg += " " + std::to_string(n) + " " + (n == 1 ? one : many) + ",";
        }
    };
    part(layers, "layer", "layers");
    part(dimstyles, "dimension style", "dimension styles");
    part(tstyles, "table style", "table styles");
    part(blocks, "block", "blocks");
    part(images, "image definition", "image definitions");
    part(groups, "empty group", "empty groups");
    part(tstyles_text, "text style", "text styles");
    msg.back() = '.';
    report(msg);
}

// AUDIT: every reference an entity carries must point inside its table; every entity
// must have the shape its kind requires. Findings are counted; with `fix`, a bad
// reference is reset to the default entry (a re-create, one undo group), a degenerate
// entity or one referencing a missing definition is erased, and dead group members
// are dropped. The report follows AutoCAD's AUDIT wording.
// BLOCK: the selection becomes a definition. Block content is kept in WORLD
// coordinates with the picked base point as the definition's base, so replacing the
// originals with one insert AT that base (scale 1, rotation 0) leaves every stroke where
// it was -- the insert transform is world = pos + R(S(local - base)). One undo group.
void GeometryEngine::collect_block_content(const std::vector<EntityHandle>& handles,
                                           BlockContent& content, std::vector<EntityHandle>& taken,
                                           int& skipped) const {
    for (const EntityHandle h : handles) {
        switch (h.kind) {
        case EntityKind::Line:
            content.lines.push_back(*store_.line(h));
            break;
        case EntityKind::Circle:
            content.circles.push_back(*store_.circle(h));
            break;
        case EntityKind::Arc:
            content.arcs.push_back(*store_.arc(h));
            break;
        case EntityKind::Polyline: {
            const PolylineData* pl = store_.polyline(h);
            const auto v = store_.vertices_of(*pl);
            const auto b = store_.bulges_of(*pl);
            content.polylines.push_back(BlockPolyline{std::vector<Vec2>(v.begin(), v.end()),
                                                          std::vector<double>(b.begin(), b.end()),
                                                          pl->closed, pl->props});
            break;
        }
        case EntityKind::Text: {
            const TextData* t = store_.text(h);
            content.texts.push_back(BlockText{t->pos, t->height, t->rotation, t->justify,
                                                  std::string(store_.string_of(*t)), t->props});
            break;
        }
        case EntityKind::AttDef: {
            // The definition joins the block as an attribute: INSERT will ask its prompt.
            const AttDefData* a = store_.attdef(h);
            content.attdefs.push_back(
                BlockAttDef{BlockText{a->text.pos, a->text.height, a->text.rotation, a->text.justify,
                                      std::string(), a->text.props},
                            std::string(store_.string_of(a->text)),
                            std::string(store_.attdef_prompt(*a)),
                            std::string(store_.attdef_default(*a)), a->flags});
            break;
        }
        case EntityKind::MText: {
            const MTextData* m = store_.mtext(h);
            content.mtexts.push_back(
                BlockMText{m->text, std::string(store_.string_of(m->text)), m->props});
            break;
        }
        case EntityKind::Insert:
            content.inserts.push_back(*store_.insert(h)); // nests
            break;
        case EntityKind::Point:
        case EntityKind::Spline:
        case EntityKind::Dimension:
        case EntityKind::Leader:
        case EntityKind::MLeader:
        case EntityKind::Hatch:
        case EntityKind::Fcf:
        case EntityKind::Datum:
        case EntityKind::Image:
        case EntityKind::Table:
        case EntityKind::Xline:
        case EntityKind::Ellipse:
            ++skipped; // the block content cannot hold this kind yet: left in place
            continue;
        }
        taken.push_back(h);
    }
}

void GeometryEngine::apply_define_block(const DefineBlockCommand& c) {
    prune_selection();
    if (c.name.empty()) {
        report("Block: a name is required.");
        return;
    }
    if (selection_.empty()) {
        report("Block: nothing selected.");
        return;
    }
    BlockDef def;
    def.name = c.name;
    def.base = c.base;
    std::vector<EntityHandle> taken;
    int skipped = 0;
    collect_block_content(selection_, def.content, taken, skipped);
    if (taken.empty()) {
        report("Block: none of the selected objects can go into a block yet (lines, circles, "
               "arcs, polylines, text, mtext and inserts can).");
        return;
    }
    std::uint16_t bi = 0xFFFF;
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(store_.block_count()); ++i) {
        if (store_.block(i)->name == c.name) {
            bi = i;
        }
    }
    const bool redefined = bi != 0xFFFF;
    if (redefined) {
        store_.redefine_block(bi, def);
    } else {
        bi = store_.add_block(def);
    }
    for (const EntityHandle h : taken) {
        const Command original = capture_entity(h);
        remove_indexed(h);
        push_erase_item(c.group, h, original);
    }
    const Command ins = AddInsertCommand{bi, c.base, 1.0, 1.0, 0.0, 0, {}, {}};
    const EntityHandle nh = create_indexed(ins);
    push_create_item(c.group, nh, ins);
    selection_ = {nh};
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    std::string msg = std::string(redefined ? "Block \"" : "Block \"") + c.name +
                      (redefined ? "\" redefined with " : "\" defined with ") +
                      std::to_string(taken.size()) + (taken.size() == 1 ? " object" : " objects");
    if (skipped > 0) {
        msg += " (" + std::to_string(skipped) + " unsupported left in place)";
    }
    report(msg + ".");
}

// WBLOCK: a block's content as a drawing of its own (base point at the origin), or the
// whole drawing when no name is given.
void GeometryEngine::apply_refedit(const RefEditCommand& c) {
    if (refedit_.active) {
        report("A reference is already being edited; REFCLOSE it first.");
        return;
    }
    const EntityHandle h = pick_nearest(c.pick, c.pick_radius);
    const InsertData* in = store_.insert(h);
    const BlockDef* bd = in != nullptr ? store_.block(in->block) : nullptr;
    if (bd == nullptr) {
        report("REFEDIT: select a block reference.");
        return;
    }
    // The working set is mapped back into the definition's frame on save, which
    // needs an invertible, shape-preserving transform: a positive uniform scale.
    if (!(in->scale_x > 0.0) || std::abs(in->scale_x - in->scale_y) > 1e-9) {
        report("REFEDIT needs a reference with a positive, uniform scale.");
        return;
    }
    RefEditSession session;
    session.block = in->block;
    session.insert = std::get<AddInsertCommand>(capture_entity(h));
    session.insert.group = 0;
    // Its members become ordinary objects at the reference's place (EXPLODE's path, so
    // nested references stay references and attributes become their definitions).
    selection_ = {h};
    const std::size_t before = undo_.size();
    apply_explode(c.group);
    if (!store_.is_valid(h) && (undo_.size() > before || (!undo_.empty() && undo_.back().id == c.group))) {
        session.working_set = selection_;
        session.active = true;
        refedit_ = std::move(session);
        report("Reference \"" + bd->name + "\" open for editing: " +
               std::to_string(refedit_.working_set.size()) +
               " object(s) in the working set. REFSET adds or removes objects; REFCLOSE saves "
               "or discards.");
    } else {
        report("REFEDIT: that reference has nothing to edit.");
    }
}

void GeometryEngine::apply_refset(const RefSetCommand& c) {
    (void)c.group;
    if (!refedit_.active) {
        report("No reference is being edited (REFEDIT first).");
        return;
    }
    prune_selection();
    if (selection_.empty()) {
        report("REFSET: nothing selected.");
        return;
    }
    const auto in_set = [&](EntityHandle h) {
        for (const EntityHandle w : refedit_.working_set) {
            if (w == h) {
                return true;
            }
        }
        return false;
    };
    std::size_t n = 0;
    if (c.add) {
        for (const EntityHandle h : selection_) {
            if (!in_set(h)) {
                refedit_.working_set.push_back(h);
                refedit_.added.push_back(h);
                ++n;
            }
        }
        report(std::to_string(n) + " object(s) added to the working set.");
    } else {
        for (const EntityHandle h : selection_) {
            const auto it = std::find(refedit_.working_set.begin(), refedit_.working_set.end(), h);
            if (it != refedit_.working_set.end()) {
                refedit_.working_set.erase(it);
                ++n;
            }
        }
        report(std::to_string(n) + " object(s) removed from the working set.");
    }
}

void GeometryEngine::apply_refclose(const RefCloseCommand& c) {
    if (!refedit_.active) {
        report("No reference is being edited (REFEDIT first).");
        return;
    }
    RefEditSession session = std::move(refedit_);
    refedit_ = RefEditSession{};
    const BlockDef* bd = store_.block(session.block);
    if (bd == nullptr) {
        report("REFCLOSE: the block definition is gone; the working set stays as it is.");
        return;
    }
    std::vector<EntityHandle> live;
    for (const EntityHandle h : session.working_set) {
        if (store_.is_valid(h)) {
            live.push_back(h);
        }
    }
    const auto was_added = [&](EntityHandle h) {
        return std::find(session.added.begin(), session.added.end(), h) != session.added.end();
    };
    std::size_t saved = 0;
    int skipped = 0;
    if (c.save) {
        // Map each working-set object into the definition's frame: the inverse of the
        // reference's transform, then the block's base point.
        const AddInsertCommand& ins = session.insert;
        const double s = ins.scale_x > 0.0 ? ins.scale_x : 1.0;
        std::vector<EntityHandle> temp;
        std::vector<EntityHandle> source;
        for (const EntityHandle h : live) {
            Command local = capture_entity(h);
            translate_cmd(local, Vec2{-ins.pos.x, -ins.pos.y});
            rotate_cmd(local, Vec2{0.0, 0.0}, -ins.rotation);
            scale_cmd(local, Vec2{0.0, 0.0}, 1.0 / s);
            translate_cmd(local, bd->base);
            temp.push_back(create_indexed(local));
            source.push_back(h);
        }
        BlockDef def;
        def.name = bd->name;
        def.base = bd->base;
        std::vector<EntityHandle> taken;
        collect_block_content(temp, def.content, taken, skipped);
        for (std::size_t i = 0; i < temp.size(); ++i) {
            const bool went_in =
                std::find(taken.begin(), taken.end(), temp[i]) != taken.end();
            remove_indexed(temp[i]); // the local copies served only the content build
            if (went_in) {
                const Command original = capture_entity(source[i]);
                remove_indexed(source[i]);
                push_erase_item(c.group, source[i], original);
                ++saved;
            }
        }
        store_.redefine_block(session.block, def);
    } else {
        for (const EntityHandle h : live) {
            if (was_added(h)) {
                continue; // REFSET-added objects go back to being ordinary objects
            }
            const Command original = capture_entity(h);
            remove_indexed(h);
            push_erase_item(c.group, h, original);
        }
    }
    const Command add = session.insert;
    const EntityHandle nh = create_indexed(add);
    push_create_item(c.group, nh, add);
    selection_ = {nh};
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    if (c.save) {
        std::string msg = "Reference \"" + bd->name + "\" saved with " + std::to_string(saved) +
                          " object(s)";
        if (skipped > 0) {
            msg += " (" + std::to_string(skipped) + " unsupported left in place)";
        }
        report(msg + ".");
    } else {
        report("Reference \"" + bd->name + "\" changes discarded.");
    }
}

void GeometryEngine::apply_polyline_vertex(const PolylineVertexCommand& c) {
    using Op = PolylineVertexCommand::Op;
    if (!store_.is_valid(c.handle) || c.handle.kind != EntityKind::Polyline) {
        report("Not a polyline.");
        return;
    }
    const Command original = capture_entity(c.handle);
    AddPolylineCommand pl = std::get<AddPolylineCommand>(original);
    const std::size_t n = pl.points.size();
    const std::size_t nseg = n < 2 ? 0 : (pl.closed ? n : n - 1);
    if (pl.bulges.size() != n) {
        pl.bulges.assign(n, 0.0);
    }
    const bool is_seg = c.index >= kSegmentGripBase;
    const std::size_t seg = is_seg ? c.index - kSegmentGripBase : c.index; // vertex i starts segment i
    if ((is_seg && seg >= nseg) || (!is_seg && seg >= n)) {
        report("No such grip.");
        return;
    }
    std::string what;
    switch (c.op) {
    case Op::AddVertex: {
        // Insert after vertex `seg`, on the segment (its midpoint, on the arc if curved).
        if (seg >= nseg) {
            report("Add vertex: pick a segment or an inner vertex.");
            return;
        }
        const std::size_t j = (seg + 1) % n;
        const Vec2 a = pl.points[seg];
        const Vec2 b = pl.points[j];
        const double bulge = pl.bulges[seg];
        Vec2 mid{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5};
        double half = 0.0;
        if (bulge != 0.0) {
            const Vec2 d = b - a;
            const double len = length(d);
            if (len > 1e-12) {
                mid = mid + Vec2{d.y / len, -d.x / len} * (bulge * len * 0.5);
            }
            half = std::tan(std::atan(bulge) / 2.0); // each half keeps the arc's curvature
        }
        pl.points.insert(pl.points.begin() + static_cast<std::ptrdiff_t>(seg) + 1, mid);
        pl.bulges[seg] = half;
        pl.bulges.insert(pl.bulges.begin() + static_cast<std::ptrdiff_t>(seg) + 1, half);
        what = "Vertex added.";
        break;
    }
    case Op::RemoveVertex: {
        if (is_seg) {
            report("Remove vertex: pick a vertex grip.");
            return;
        }
        if (n <= 2) {
            report("A polyline keeps at least two vertices.");
            return;
        }
        pl.points.erase(pl.points.begin() + static_cast<std::ptrdiff_t>(seg));
        pl.bulges.erase(pl.bulges.begin() + static_cast<std::ptrdiff_t>(seg));
        what = "Vertex removed.";
        break;
    }
    case Op::ToArc:
    case Op::ToLine: {
        if (seg >= nseg) {
            report("Pick a segment.");
            return;
        }
        // A new arc bows by a quarter circle; its midpoint grip then reshapes it.
        pl.bulges[seg] = c.op == Op::ToArc ? std::tan(kPi / 8.0) : 0.0;
        what = c.op == Op::ToArc ? "Segment converted to an arc." : "Segment converted to a line.";
        break;
    }
    }
    bool all_straight = true;
    for (const double b : pl.bulges) {
        all_straight = all_straight && b == 0.0;
    }
    if (all_straight) {
        pl.bulges.clear();
    }
    remove_indexed(c.handle);
    push_erase_item(c.group, c.handle, original);
    const Command add = pl;
    const EntityHandle nh = create_indexed(add);
    push_create_item(c.group, nh, add);
    selection_ = {nh};
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report(what);
}

void GeometryEngine::apply_write_block(const WriteBlockCommand& c) {
    io::Document doc;
    std::size_t count = 0;
    if (c.name.empty()) {
        doc = io::document_from_store(store_);
        count = doc.entity_count();
    } else {
        const BlockDef* def = nullptr;
        for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(store_.block_count()); ++i) {
            if (store_.block(i)->name == c.name) {
                def = store_.block(i);
            }
        }
        if (def == nullptr) {
            report("Block \"" + c.name + "\" not found.");
            return;
        }
        const Vec2 o = def->base;
        for (const LineData& l : def->content.lines) {
            doc.lines.push_back(io::DocLine{l.a - o, l.b - o, l.props});
        }
        for (const CircleData& ci : def->content.circles) {
            doc.circles.push_back(io::DocCircle{ci.center - o, ci.radius, ci.props});
        }
        for (const ArcData& a : def->content.arcs) {
            doc.arcs.push_back(io::DocArc{a.center - o, a.radius, a.start_angle, a.end_angle, a.props});
        }
        for (const BlockPolyline& p : def->content.polylines) {
            std::vector<Vec2> pts;
            for (const Vec2& v : p.verts) {
                pts.push_back(v - o);
            }
            doc.polylines.push_back(io::DocPolyline{pts, p.closed, p.props, p.bulges});
        }
        for (const BlockText& t : def->content.texts) {
            doc.texts.push_back(
                io::DocText{t.pos - o, t.height, t.rotation, t.justify, t.content, t.props});
        }
        for (const BlockMText& m : def->content.mtexts) {
            io::DocMText dm{m.block, m.content, m.props};
            dm.block.pos = m.block.pos - o;
            doc.mtexts.push_back(std::move(dm));
        }
        if (!def->content.inserts.empty()) {
            // Nested inserts need their definitions: carry the whole block table.
            const io::Document whole = io::document_from_store(store_);
            doc.block_defs = whole.block_defs;
            for (const InsertData& ni : def->content.inserts) {
                const BlockDef* nd = store_.block(ni.block);
                doc.inserts.push_back(io::DocInsert{nd != nullptr ? nd->name : std::string{},
                                                    ni.pos - o, ni.scale_x, ni.scale_y, ni.rotation,
                                                    ni.props, {}});
            }
        }
        doc.layers = io::document_from_store(store_).layers;
        count = doc.entity_count();
    }
    const io::IoResult r = io::save_native(doc, c.path);
    if (!r.ok) {
        report("Wblock: could not write " + c.path + ": " + r.message);
        return;
    }
    report("Wrote " + std::to_string(count) + (count == 1 ? " object to " : " objects to ") + c.path + ".");
}

// FIELD values: the date, time, file name and login the text layout substitutes for
// %<Date>%, %<Time>%, %<Filename>% and %<Login>%. Refreshed on every rebuild, so a
// field is as current as the last edit or REGEN (AutoCAD's FIELDEVAL on regen).
text::FieldContext GeometryEngine::field_context_now() const {
    return text::make_field_context(active_idx_ < doc_metas_.size() ? doc_metas_[active_idx_].path
                                                                    : std::string());
}

void GeometryEngine::apply_audit(bool fix) {
    int errors = 0;
    int fixed = 0;
    const std::uint64_t group = fix ? 0xA0D17ull : 0;
    std::vector<EntityHandle> to_erase;
    std::vector<std::pair<EntityHandle, Command>> to_replace;
    const std::size_t nlayers = store_.layer_count();
    const std::size_t nstyles = store_.dimstyles().size();
    const std::size_t nfonts = store_.fonts().size();
    const std::size_t ntstyles = store_.table_styles().size();
    const std::size_t nblocks = store_.block_count();
    const std::size_t nimages = store_.image_defs().size();

    for (const EntityHandle h : all_live()) {
        const EntityProps* pr = store_.props(h);
        bool bad_layer = pr != nullptr && pr->layer >= nlayers;
        bool bad_ref = false;   // fixable: style / font -> default
        bool erase = false;     // unfixable: missing definition or degenerate shape
        switch (h.kind) {
        case EntityKind::Dimension:
            bad_ref = store_.dimension(h)->style >= nstyles;
            break;
        case EntityKind::Leader:
            bad_ref = store_.leader(h)->style >= nstyles || store_.leader(h)->font >= nfonts;
            break;
        case EntityKind::MLeader:
            bad_ref = store_.mleader(h)->style >= nstyles;
            break;
        case EntityKind::Fcf:
            bad_ref = store_.fcf(h)->style >= nstyles;
            break;
        case EntityKind::Datum:
            bad_ref = store_.datum(h)->style >= nstyles;
            break;
        case EntityKind::Text:
            bad_ref = store_.text(h)->font >= nfonts ||
                      store_.text(h)->style >= store_.text_styles().size();
            break;
        case EntityKind::AttDef:
            bad_ref = store_.attdef(h)->text.font >= nfonts ||
                      store_.attdef(h)->text.style >= store_.text_styles().size();
            break;
        case EntityKind::Table:
            bad_ref = store_.table(h)->style >= ntstyles;
            break;
        case EntityKind::Insert:
            erase = store_.insert(h)->block >= nblocks;
            break;
        case EntityKind::Image:
            erase = store_.image(h)->def >= nimages;
            break;
        case EntityKind::Polyline:
            erase = store_.polyline(h)->count < 2;
            break;
        case EntityKind::Hatch: {
            const HatchData* hd = store_.hatch(h);
            for (const auto& loop : store_.hatch_loops(*hd)) {
                if (loop.size() < 3) {
                    erase = true;
                }
            }
            break;
        }
        case EntityKind::Point:
        case EntityKind::Line:
        case EntityKind::Circle:
        case EntityKind::Arc:
        case EntityKind::Spline:
        case EntityKind::MText:
        case EntityKind::Xline:
        case EntityKind::Ellipse:
            break;
        }
        if (bad_layer) {
            ++errors;
        }
        if (bad_ref) {
            ++errors;
        }
        if (erase) {
            ++errors;
        }
        if (!fix) {
            continue;
        }
        if (erase) {
            to_erase.push_back(h);
            fixed += (bad_layer ? 1 : 0) + (bad_ref ? 1 : 0) + 1;
            continue;
        }
        if (bad_layer && !bad_ref) {
            EntityProps fixed_props = *pr;
            fixed_props.layer = 0;
            store_.set_props(h, fixed_props);
            ++fixed;
            continue;
        }
        if (bad_ref) {
            Command c = capture_entity(h);
            std::visit(
                [&](auto& x) {
                    if constexpr (requires { x.style; }) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(x.style)>, std::uint16_t>) {
                            x.style = 0;
                        }
                    }
                    if constexpr (requires { x.font; }) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(x.font)>, std::string>) {
                            x.font.clear();
                        }
                    }
                    if constexpr (std::is_same_v<std::decay_t<decltype(x)>, AddTextCommand>) {
                        x.style.clear(); // back to Standard
                    }
                    if constexpr (std::is_same_v<std::decay_t<decltype(x)>, AddAttDefCommand>) {
                        x.text.style.clear();
                    }
                    if constexpr (requires { x.props; }) {
                        if (bad_layer && x.props.has_value()) {
                            x.props->layer = 0;
                        }
                    }
                },
                c);
            to_replace.emplace_back(h, std::move(c));
            fixed += (bad_layer ? 1 : 0) + 1;
        }
    }
    // Groups: dead members.
    {
        std::vector<EntityGroup> gs = store_.groups();
        bool changed = false;
        for (EntityGroup& g : gs) {
            const std::size_t n = g.members.size();
            std::erase_if(g.members, [&](EntityHandle m) { return !store_.is_valid(m); });
            if (g.members.size() != n) {
                errors += static_cast<int>(n - g.members.size());
                if (fix) {
                    fixed += static_cast<int>(n - g.members.size());
                    changed = true;
                }
            }
        }
        if (fix && changed) {
            std::erase_if(gs, [](const EntityGroup& g) { return g.members.empty(); });
            store_.set_groups(std::move(gs));
        }
    }
    if (fix) {
        for (const EntityHandle h : to_erase) {
            const Command original = capture_entity(h);
            remove_indexed(h);
            push_erase_item(group, h, original);
        }
        for (auto& [h, c] : to_replace) {
            const Command original = capture_entity(h);
            remove_indexed(h);
            push_erase_item(group, h, original);
            push_create_item(group, create_indexed(c), c);
        }
        if (!to_erase.empty() || !to_replace.empty()) {
            redo_.clear();
        }
        prune_selection();
        geom_dirty_ = true;
        dirty_ = dirty_ || fixed > 0;
    }
    report("Auditing Header  Auditing Tables  Auditing Entities Pass 1  Total errors found " +
           std::to_string(errors) + " fixed " + std::to_string(fixed) + ".");
}

void GeometryEngine::apply_align(const AlignSelectionCommand& c) {
    const std::vector<EntityHandle> sel = selection_;
    if (sel.empty()) {
        report("Nothing selected to align.");
        return;
    }
    const Vec2 sv = c.src2 - c.src1;
    const Vec2 dv = c.dst2 - c.dst1;
    const double slen = length(sv);
    if (slen <= 1e-12 || length(dv) <= 1e-12) {
        report("Align: the two source points and the two destination points must differ.");
        return;
    }
    const double ang = std::atan2(dv.y, dv.x) - std::atan2(sv.y, sv.x);
    const double f = c.scale ? length(dv) / slen : 1.0;

    // p -> dst1 + f * R(ang) * (p - src1), composed from the existing per-kind
    // transforms so every entity kind is aligned by the code that already knows how to
    // rotate and scale it. Order matters: both pivot on src1 before the move.
    std::vector<EntityHandle> out;
    for (const EntityHandle h : sel) {
        if (!store_.is_valid(h)) {
            continue;
        }
        const Command original = capture_entity(h);
        Command result = original;
        rotate_cmd(result, c.src1, ang);
        if (c.scale) {
            scale_cmd(result, c.src1, f);
        }
        translate_cmd(result, c.dst1 - c.src1);
        remove_indexed(h);
        push_erase_item(c.group, h, original);
        const EntityHandle nh = create_indexed(result);
        push_create_item(c.group, nh, result);
        out.push_back(nh);
    }
    if (!out.empty()) {
        selection_ = out;
    }
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report("Aligned " + std::to_string(out.size()) +
           (out.size() == 1 ? " object." : " objects."));
}

void GeometryEngine::apply_lengthen(const LengthenCommand& c) {
    const EntityHandle h = pick_nearest(c.pick, c.pick_radius);
    if (h.is_null()) {
        report("Lengthen: no curve under the pick.");
        return;
    }
    // What the new length should be, given the current one.
    const auto resolve = [&](double current) {
        switch (c.mode) {
        case LengthenCommand::Mode::Delta:
            return current + c.value;
        case LengthenCommand::Mode::Percent:
            return current * c.value / 100.0;
        case LengthenCommand::Mode::Total:
            break;
        }
        return c.value;
    };

    Command edited;
    double before = 0.0;
    double after = 0.0;
    switch (h.kind) {
    case EntityKind::Xline:
        report("Lengthen: a construction line is already infinite.");
        return;
    case EntityKind::Ellipse:
        report("Lengthen: ellipses are not supported yet (lines, arcs and polylines are).");
        return;
    case EntityKind::Line: {
        const LineData* l = store_.line(h);
        before = length(l->b - l->a);
        if (before <= 1e-12) {
            report("Lengthen: that line has no length.");
            return;
        }
        after = resolve(before);
        if (after <= 1e-9) {
            report("Lengthen: that would leave nothing of the object.");
            return;
        }
        // AutoCAD moves the end NEARER the pick and anchors the other.
        const bool move_b = length(c.pick - l->b) <= length(c.pick - l->a);
        const Vec2 anchor = move_b ? l->a : l->b;
        const Vec2 dir = (move_b ? l->b - l->a : l->a - l->b) / before;
        const Vec2 moved = anchor + dir * after;
        edited = AddLineCommand{move_b ? anchor : moved, move_b ? moved : anchor, 0, l->props};
        break;
    }
    case EntityKind::Arc: {
        const ArcData* a = store_.arc(h);
        double sweep = a->end_angle - a->start_angle;
        while (sweep <= 0.0) {
            sweep += kTwoPi;
        }
        before = sweep * a->radius; // arc LENGTH, so Delta/Total are in drawing units
        if (before <= 1e-12 || a->radius <= 1e-12) {
            report("Lengthen: that arc has no length.");
            return;
        }
        after = resolve(before);
        if (after <= 1e-9) {
            report("Lengthen: that would leave nothing of the object.");
            return;
        }
        const double new_sweep = std::min(after / a->radius, kTwoPi);
        const Vec2 start_pt{a->center.x + a->radius * std::cos(a->start_angle),
                            a->center.y + a->radius * std::sin(a->start_angle)};
        const Vec2 end_pt{a->center.x + a->radius * std::cos(a->end_angle),
                          a->center.y + a->radius * std::sin(a->end_angle)};
        // Grow or shrink from whichever end the pick is nearer, keeping the other fixed.
        if (length(c.pick - end_pt) <= length(c.pick - start_pt)) {
            edited = AddArcCommand{a->center, a->radius, a->start_angle,
                                   a->start_angle + new_sweep, 0, a->props};
        } else {
            edited = AddArcCommand{a->center, a->radius, a->end_angle - new_sweep, a->end_angle,
                                   0, a->props};
        }
        break;
    }
    default:
        report("Lengthen: only lines and arcs have an end to move.");
        return;
    }

    const Command original = capture_entity(h);
    remove_indexed(h);
    push_erase_item(c.group, h, original);
    const EntityHandle nh = create_indexed(edited);
    push_create_item(c.group, nh, edited);
    selection_ = {nh};
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Length changed from %.4g to %.4g.", before, after);
    report(buf);
}

void GeometryEngine::apply_break(const BreakCommand& c) {
    const EntityHandle h = pick_nearest(c.pick, c.pick_radius);
    if (h.is_null()) {
        report("Break: no curve under the pick.");
        return;
    }
    // BREAK AT POINT: the two points coincide, so nothing is removed and the curve is
    // simply split. A closed shape has no free end to split at, so it is refused.
    const bool at_point = length(c.p2 - c.p1) <= 1e-9;

    // Each case builds the REPLACEMENT pieces; the shared tail below swaps them in as
    // one undo group, so no case has to repeat the erase/create bookkeeping.
    std::vector<Command> pieces;
    const auto keep_line = [&](Vec2 a, Vec2 b) {
        if (length(b - a) > 1e-9) {
            pieces.push_back(AddLineCommand{a, b, 0, store_.line(h)->props});
        }
    };

    switch (h.kind) {
    case EntityKind::Xline:
        report("Break: a construction line cannot be broken.");
        return;
    case EntityKind::Ellipse:
        report("Break: ellipses are not supported yet (lines, arcs, circles and polylines are).");
        return;
    case EntityKind::Line: {
        const LineData* l = store_.line(h);
        const Vec2 d = l->b - l->a;
        const double len2 = length_squared(d);
        if (len2 <= 1e-18) {
            report("Break: that line has no length.");
            return;
        }
        // Project both points onto the line and order them along it.
        double t1 = dot(c.p1 - l->a, d) / len2;
        double t2 = at_point ? t1 : dot(c.p2 - l->a, d) / len2;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        t1 = std::clamp(t1, 0.0, 1.0);
        t2 = std::clamp(t2, 0.0, 1.0);
        keep_line(l->a, l->a + d * t1);
        keep_line(l->a + d * t2, l->b);
        break;
    }
    case EntityKind::Arc: {
        const ArcData* a = store_.arc(h);
        // Sweep from start to end, always increasing, so a break point's position is a
        // single number and the two kept pieces are just the ends of that sweep.
        const auto sweep_to = [&](Vec2 p) {
            const double ang = std::atan2(p.y - a->center.y, p.x - a->center.x);
            double s = ang - a->start_angle;
            while (s < 0.0) {
                s += kTwoPi;
            }
            return s;
        };
        double total = a->end_angle - a->start_angle;
        while (total <= 0.0) {
            total += kTwoPi;
        }
        double s1 = sweep_to(c.p1);
        double s2 = at_point ? s1 : sweep_to(c.p2);
        if (s1 > s2) {
            std::swap(s1, s2);
        }
        s1 = std::clamp(s1, 0.0, total);
        s2 = std::clamp(s2, 0.0, total);
        const auto keep_arc = [&](double from, double to) {
            if (to - from > 1e-9) {
                pieces.push_back(AddArcCommand{a->center, a->radius, a->start_angle + from,
                                               a->start_angle + to, 0, a->props});
            }
        };
        keep_arc(0.0, s1);
        keep_arc(s2, total);
        break;
    }
    case EntityKind::Circle: {
        if (at_point) {
            report("Break: a circle needs two different break points.");
            return;
        }
        const CircleData* ci = store_.circle(h);
        // AutoCAD removes the piece running COUNTER-CLOCKWISE from the first point to
        // the second, so what survives is the arc from p2 round to p1.
        const double a1 = std::atan2(c.p1.y - ci->center.y, c.p1.x - ci->center.x);
        const double a2 = std::atan2(c.p2.y - ci->center.y, c.p2.x - ci->center.x);
        pieces.push_back(AddArcCommand{ci->center, ci->radius, a2, a1, 0, ci->props});
        break;
    }
    case EntityKind::Polyline: {
        const PolylineData* pl = store_.polyline(h);
        const std::span<const Vec2> v = store_.vertices_of(*pl);
        if (v.size() < 2) {
            report("Break: that polyline has no length.");
            return;
        }
        // Position along the polyline as (segment index + fraction), which orders the
        // two break points even when they fall on different segments.
        const auto locate = [&](Vec2 p) {
            const int si = nearest_pl_segment(v, pl->closed, p);
            if (si < 0) {
                return 0.0;
            }
            const std::size_t i = static_cast<std::size_t>(si);
            const Vec2 a = v[i];
            const Vec2 b = v[(i + 1) % v.size()];
            const Vec2 d = b - a;
            const double len2 = length_squared(d);
            const double t = len2 > 1e-18 ? std::clamp(dot(p - a, d) / len2, 0.0, 1.0) : 0.0;
            return static_cast<double>(si) + t;
        };
        const auto point_at = [&](double u) {
            const std::size_t i = static_cast<std::size_t>(std::floor(u));
            const double t = u - static_cast<double>(i);
            const Vec2 a = v[std::min(i, v.size() - 1)];
            const Vec2 b = v[(i + 1) % v.size()];
            return a + (b - a) * t;
        };
        double u1 = locate(c.p1);
        double u2 = at_point ? u1 : locate(c.p2);
        if (u1 > u2) {
            std::swap(u1, u2);
        }
        // A closed polyline breaks into ONE open run: the part that survives is the
        // stretch from the second point round to the first.
        const auto emit = [&](std::vector<Vec2> pts) {
            if (pts.size() >= 2) {
                AddPolylineCommand pc;
                pc.points = std::move(pts);
                pc.closed = false;
                pc.props = pl->props;
                pieces.push_back(std::move(pc));
            }
        };
        if (pl->closed) {
            std::vector<Vec2> run{point_at(u2)};
            for (std::size_t k = static_cast<std::size_t>(std::floor(u2)) + 1;
                 k <= static_cast<std::size_t>(std::floor(u1)) + v.size(); ++k) {
                run.push_back(v[k % v.size()]);
            }
            run.push_back(point_at(u1));
            emit(std::move(run));
        } else {
            std::vector<Vec2> head(v.begin(),
                                   v.begin() + static_cast<std::ptrdiff_t>(
                                                   std::floor(u1)) + 1);
            head.push_back(point_at(u1));
            emit(std::move(head));

            std::vector<Vec2> tail{point_at(u2)};
            for (std::size_t k = static_cast<std::size_t>(std::floor(u2)) + 1; k < v.size();
                 ++k) {
                tail.push_back(v[k]);
            }
            emit(std::move(tail));
        }
        break;
    }
    default:
        report("Break: that entity kind cannot be broken.");
        return;
    }

    if (pieces.empty()) {
        report("Break: that would remove the whole object -- use ERASE.");
        return;
    }
    const Command original = capture_entity(h);
    remove_indexed(h);
    push_erase_item(c.group, h, original);
    std::vector<EntityHandle> made;
    for (const Command& piece : pieces) {
        const EntityHandle nh = create_indexed(piece);
        push_create_item(c.group, nh, piece);
        made.push_back(nh);
    }
    selection_ = made;
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report(at_point ? "Split into " + std::to_string(pieces.size()) + " objects."
                    : "Broke 1 object into " + std::to_string(pieces.size()) + ".");
}

void GeometryEngine::apply_object_dimension(std::uint8_t type, Vec2 pick1, Vec2 pick2, Vec2 pick3,
                                            Vec2 pick4, double radius, std::uint16_t style,
                                            std::uint64_t group) {
    DimData d;
    if (!resolve_dim_defs(type, pick1, pick2, radius, d, pick3, pick4)) {
        report("Could not dimension that object -- select a line, circle, or arc.");
        return;
    }
    AddDimensionCommand dim;
    dim.type = type;
    dim.a = d.a;
    dim.b = d.b;
    dim.line_pt = d.line_pt;
    dim.aux = d.aux;
    dim.style = style;
    dim.group = group;
    const Command add = dim;
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
    push_erase_item(group, grip_handle_, original);
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
    // A table is text-bearing too: the pick resolves to the CELL under the point, so
    // the same double-click / DDEDIT gesture that edits a text edits a cell. Scanned
    // last so a text sitting on top of a table still wins the pick.
    scan(store_.tables(), EntityKind::Table);
    if (target.is_null()) {
        report("No editable text there.");
        return;
    }
    // Which cell was picked? Resolved here, against the SAME derived geometry the
    // renderer and the picker use, so the cell that lights up is the cell that edits.
    int cell_index = -1;
    if (target.kind == EntityKind::Table) {
        const TableData* td = store_.table(target);
        const TableStyle* st = store_.table_style(td->style);
        const TableGeometry g = compute_table_geometry(
            *td, store_.table_cell_views(*td), store_.table_col_widths(*td),
            store_.table_row_heights(*td), st != nullptr ? *st : TableStyle{}, Rgb{});
        cell_index = table_cell_at(g, at);
        if (cell_index < 0) {
            // Inside the table's bounding box but not in any cell (the pick landed on
            // the border of a rotated table). Say so rather than editing a guess.
            report("Pick inside a cell to edit it.");
            return;
        }
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
            } else if constexpr (std::is_same_v<T, AddTableCommand>) {
                // Only the picked cell changes; every other cell, both size vectors and
                // the style come through capture_entity untouched.
                const auto i = static_cast<std::size_t>(cell_index);
                if (i < x.texts.size()) {
                    x.texts[i] = content;
                }
            }
        },
        edited);
    const Command original = capture_entity(target);
    remove_indexed(target);
    push_erase_item(group, target, original);
    const EntityHandle nh = create_indexed(edited);
    push_create_item(group, nh, edited);
    selection_ = {nh};
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report(target.kind == EntityKind::Table ? "Table cell edited." : "Text edited.");
}

namespace {

/// A bulged polyline segment as an arc: centre, radius, the angle of its first vertex,
/// and the SIGNED sweep (positive = counter-clockwise), from the AutoCAD bulge
/// convention (bulge = tan(sweep/4)).
struct SegArc {
    Vec2 center;
    double radius = 0.0;
    double a0 = 0.0;
    double theta = 0.0;
};

bool seg_arc(Vec2 p0, Vec2 p1, double bulge, SegArc& out) {
    const double theta = 4.0 * std::atan(bulge);
    const Vec2 d = p1 - p0;
    const double c = length(d);
    if (std::abs(theta) < 1e-12 || c < 1e-12) {
        return false;
    }
    const double r = c / (2.0 * std::sin(std::abs(theta) / 2.0));
    const double h = r * std::cos(theta / 2.0); // signed with theta via cos(|theta|/2)
    const Vec2 left{-d.y / c, d.x / c};
    // A positive (CCW) bulge sweeps to the RIGHT of the chord, so its centre lies to the
    // LEFT; a negative one mirrors that.
    out.center = (p0 + p1) * 0.5 + left * (bulge > 0.0 ? h : -h);
    out.radius = r;
    out.a0 = std::atan2(p0.y - out.center.y, p0.x - out.center.x);
    out.theta = theta;
    return true;
}

/// Point at polyline parameter q = segment index + fraction (bulged segments by sweep).
Vec2 poly_point_at(const std::vector<Vec2>& v, const std::vector<double>& b, double q) {
    const std::size_t n = v.size();
    const auto sidx = static_cast<std::size_t>(std::floor(q + 1e-12));
    const double f = std::clamp(q - static_cast<double>(sidx), 0.0, 1.0);
    const Vec2 p0 = v[sidx % n];
    const Vec2 p1 = v[(sidx + 1) % n];
    SegArc a;
    if (seg_arc(p0, p1, b[sidx % n], a)) {
        const double ang = a.a0 + a.theta * f;
        return {a.center.x + a.radius * std::cos(ang), a.center.y + a.radius * std::sin(ang)};
    }
    return p0 + (p1 - p0) * f;
}

/// The parameter of the point on the polyline nearest `p`, and that distance.
void polyline_param(const std::vector<Vec2>& v, const std::vector<double>& b, bool closed,
                    Vec2 p, double& q_out, double& dist_out) {
    const std::size_t n = v.size();
    const std::size_t m = closed ? n : n - 1;
    q_out = 0.0;
    dist_out = std::numeric_limits<double>::infinity();
    for (std::size_t s = 0; s < m; ++s) {
        const Vec2 p0 = v[s];
        const Vec2 p1 = v[(s + 1) % n];
        SegArc a;
        double f = 0.0;
        double dist = 0.0;
        if (seg_arc(p0, p1, b[s], a)) {
            double rel = std::atan2(p.y - a.center.y, p.x - a.center.x) - a.a0;
            if (a.theta < 0.0) {
                rel = -rel;
            }
            while (rel < 0.0) {
                rel += kTwoPi;
            }
            const double span = std::abs(a.theta);
            if (rel <= span) {
                f = rel / span;
                dist = std::abs(length(p - a.center) - a.radius);
            } else {
                const double d0 = length(p - p0);
                const double d1 = length(p - p1);
                f = d0 <= d1 ? 0.0 : 1.0;
                dist = std::min(d0, d1);
            }
        } else {
            const Vec2 d = p1 - p0;
            const double len2 = length_squared(d);
            f = len2 > 0.0 ? std::clamp(dot(p - p0, d) / len2, 0.0, 1.0) : 0.0;
            dist = length(p - (p0 + d * f));
        }
        if (dist < dist_out) {
            dist_out = dist;
            q_out = static_cast<double>(s) + f;
        }
    }
}

/// The piece of the polyline from parameter qa to qb (qb may exceed the segment count
/// on a closed polyline: it wraps), with each partial bulged segment re-bulged for its
/// sub-sweep so arcs stay exact.
void polyline_sub(const std::vector<Vec2>& v, const std::vector<double>& b, double qa, double qb,
                  std::vector<Vec2>& pts, std::vector<double>& bulges) {
    const std::size_t n = v.size();
    pts.clear();
    bulges.clear();
    pts.push_back(poly_point_at(v, b, qa));
    double q = qa;
    while (q < qb - 1e-9) {
        const auto sidx = static_cast<std::size_t>(std::floor(q + 1e-12));
        const double f0 = q - static_cast<double>(sidx);
        const double qn = std::min(qb, static_cast<double>(sidx + 1));
        const double f1 = qn - static_cast<double>(sidx);
        const double bulge = b[sidx % n];
        double sub = 0.0;
        if (std::abs(bulge) > 1e-12) {
            sub = std::tan(4.0 * std::atan(bulge) * (f1 - f0) / 4.0);
        }
        bulges.push_back(sub);
        pts.push_back(poly_point_at(v, b, qn));
        q = qn;
    }
    bulges.push_back(0.0);
}

/// The two intersections of two circles (0, 1 or 2).
int circle_circle_hits(Vec2 c0, double r0, Vec2 c1, double r1, Vec2& p0, Vec2& p1) {
    const Vec2 d = c1 - c0;
    const double dist = length(d);
    if (dist < 1e-12 || dist > r0 + r1 + 1e-9 || dist < std::abs(r0 - r1) - 1e-9) {
        return 0;
    }
    const double a = (r0 * r0 - r1 * r1 + dist * dist) / (2.0 * dist);
    const double h2 = r0 * r0 - a * a;
    const Vec2 u = d * (1.0 / dist);
    const Vec2 mid = c0 + u * a;
    if (h2 <= 1e-12) {
        p0 = mid;
        return 1;
    }
    const double h = std::sqrt(h2);
    const Vec2 perp{-u.y, u.x};
    p0 = mid + perp * h;
    p1 = mid - perp * h;
    return 2;
}

} // namespace

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
        if (!polyline_ops::fillet_corner(pts, bulges, pl->closed, sv, radius)) {
            report("Fillet: radius too large for that corner.");
            return;
        }
        const bool closed = pl->closed;
        const Command orig = capture_entity(h1);
        remove_indexed(h1);
        push_erase_item(group, h1, orig);
        const Command np = AddPolylineCommand{std::move(pts), closed, 0, {}, std::move(bulges)};
        push_create_item(group, create_indexed(np), np);
        redo_.clear();
        geom_dirty_ = true;
        report("Filleted.");
        return;
    }

    // Case 2: a line with an arc/circle, or two arcs/circles.
    const auto curvy = [](EntityKind k) { return k == EntityKind::Arc || k == EntityKind::Circle; };
    const auto lineish = [&](EntityKind k) { return k == EntityKind::Line || curvy(k); };
    if (h1 != h2 && lineish(h1.kind) && lineish(h2.kind) && (curvy(h1.kind) || curvy(h2.kind))) {
        apply_fillet_curves(h1, h2, pick1, pick2, radius, group);
        return;
    }

    // Case 3: two distinct lines.
    if (h1 == h2 || h1.kind != EntityKind::Line || h2.kind != EntityKind::Line) {
        report("Fillet: pick two lines, arcs or circles, or two adjacent edges of one polyline.");
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
    push_erase_item(group, h1, o1);
    remove_indexed(h2);
    push_erase_item(group, h2, o2);
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

// FILLET between a line and an arc/circle, or two arcs/circles. The fillet circle is
// tangent to both, so its centre lies on an OFFSET of each curve at the fillet radius:
// a parallel line (either side) or a concentric circle (radius R +/- r). Intersecting
// the offsets gives every candidate centre; AutoCAD's rule picks the one whose tangent
// points are nearest the two pick points. Each curve is then trimmed (or extended) to
// its tangent point on the side the pick chose -- a circle stays whole -- and the
// rounding arc is the minor arc between the tangent points.
void GeometryEngine::apply_fillet_curves(EntityHandle h1, EntityHandle h2, Vec2 pick1, Vec2 pick2,
                                         double radius, std::uint64_t group) {
    if (radius <= 0.0) {
        report("Fillet: a radius greater than 0 is needed to fillet a curve.");
        return;
    }
    struct Curve {
        EntityKind kind;
        Vec2 a{};   // line ends
        Vec2 b{};
        Vec2 c{};   // circle/arc centre
        double r = 0.0;
    };
    const auto curve_of = [&](EntityHandle h) {
        Curve cv{h.kind};
        if (h.kind == EntityKind::Line) {
            cv.a = store_.line(h)->a;
            cv.b = store_.line(h)->b;
        } else if (h.kind == EntityKind::Circle) {
            cv.c = store_.circle(h)->center;
            cv.r = store_.circle(h)->radius;
        } else {
            cv.c = store_.arc(h)->center;
            cv.r = store_.arc(h)->radius;
        }
        return cv;
    };
    const Curve c1 = curve_of(h1);
    const Curve c2 = curve_of(h2);

    // Offsets of a curve at distance `radius`: lines as (point, point) pairs on the
    // parallel; circles as radii.
    struct OffLine {
        Vec2 a;
        Vec2 b;
    };
    const auto line_offsets = [&](const Curve& cv, std::vector<OffLine>& out) {
        const Vec2 d = normalized(cv.b - cv.a);
        const Vec2 nrm{-d.y, d.x};
        out.push_back({cv.a + nrm * radius, cv.b + nrm * radius});
        out.push_back({cv.a - nrm * radius, cv.b - nrm * radius});
    };
    const auto circle_offsets = [&](const Curve& cv, std::vector<double>& out) {
        out.push_back(cv.r + radius);
        if (cv.r - radius > 1e-9) {
            out.push_back(cv.r - radius);
        }
    };
    std::vector<Vec2> centers;
    const auto add = [&](Vec2 p) { centers.push_back(p); };
    if (c1.kind == EntityKind::Line) {
        std::vector<OffLine> l1;
        line_offsets(c1, l1);
        std::vector<double> r2;
        circle_offsets(c2, r2);
        for (const OffLine& ol : l1) {
            for (const double rr : r2) {
                Vec2 p0{};
                Vec2 p1{};
                const int n = NativeKernel2D::line_circle_intersection(ol.a, ol.b, c2.c, rr, p0, p1);
                if (n >= 1) {
                    add(p0);
                }
                if (n == 2) {
                    add(p1);
                }
            }
        }
    } else if (c2.kind == EntityKind::Line) {
        std::vector<OffLine> l2;
        line_offsets(c2, l2);
        std::vector<double> r1;
        circle_offsets(c1, r1);
        for (const OffLine& ol : l2) {
            for (const double rr : r1) {
                Vec2 p0{};
                Vec2 p1{};
                const int n = NativeKernel2D::line_circle_intersection(ol.a, ol.b, c1.c, rr, p0, p1);
                if (n >= 1) {
                    add(p0);
                }
                if (n == 2) {
                    add(p1);
                }
            }
        }
    } else {
        std::vector<double> r1;
        std::vector<double> r2;
        circle_offsets(c1, r1);
        circle_offsets(c2, r2);
        for (const double ra : r1) {
            for (const double rb : r2) {
                Vec2 p0{};
                Vec2 p1{};
                const int n = circle_circle_hits(c1.c, ra, c2.c, rb, p0, p1);
                if (n >= 1) {
                    add(p0);
                }
                if (n == 2) {
                    add(p1);
                }
            }
        }
    }
    if (centers.empty()) {
        report("Fillet: no arc of that radius is tangent to both objects.");
        return;
    }
    // Tangent point of a candidate centre on a curve.
    const auto tangent_on = [&](const Curve& cv, Vec2 center) {
        if (cv.kind == EntityKind::Line) {
            const Vec2 d = normalized(cv.b - cv.a);
            return cv.a + d * dot(center - cv.a, d);
        }
        const Vec2 dir = center - cv.c;
        const double len = length(dir);
        return len > 1e-12 ? cv.c + dir * (cv.r / len) : cv.c + Vec2{cv.r, 0.0};
    };
    Vec2 best_c{};
    Vec2 t1{};
    Vec2 t2{};
    double best_score = std::numeric_limits<double>::infinity();
    for (const Vec2& cc : centers) {
        const Vec2 ta = tangent_on(c1, cc);
        const Vec2 tb = tangent_on(c2, cc);
        const double score = length(ta - pick1) + length(tb - pick2);
        if (score < best_score) {
            best_score = score;
            best_c = cc;
            t1 = ta;
            t2 = tb;
        }
    }

    // The rounding arc: the minor arc between the tangent points about the centre.
    double a1 = std::atan2(t1.y - best_c.y, t1.x - best_c.x);
    double a2 = std::atan2(t2.y - best_c.y, t2.x - best_c.x);
    double ccw = a2 - a1;
    while (ccw < 0.0) {
        ccw += kTwoPi;
    }
    if (ccw > kPi) {
        std::swap(a1, a2);
    }
    const Command arc = AddArcCommand{best_c, radius, a1, a2, 0};

    // Each curve trimmed/extended to its tangent point on the pick's side.
    const auto trimmed = [&](EntityHandle h, const Curve& cv, Vec2 t, Vec2 pick) -> std::optional<Command> {
        if (cv.kind == EntityKind::Line) {
            const LineData l = *store_.line(h);
            Vec2 u{};
            const Vec2 k = kept_endpoint(l, t, pick, u);
            return AddLineCommand{k, t, 0, l.props, store_.celtscale(h)};
        }
        if (cv.kind == EntityKind::Circle) {
            return std::nullopt; // a circle is left whole
        }
        const ArcData a = *store_.arc(h);
        double total = a.end_angle - a.start_angle;
        while (total <= 0.0) {
            total += kTwoPi;
        }
        const auto sweep_from_start = [&](Vec2 p) {
            double sw = std::atan2(p.y - a.center.y, p.x - a.center.x) - a.start_angle;
            while (sw < 0.0) {
                sw += kTwoPi;
            }
            return sw;
        };
        const double st = sweep_from_start(t);
        const double sp = sweep_from_start(pick);
        double ns = a.start_angle;
        double ne = a.end_angle;
        if (st <= total + 1e-9) {
            // The tangent point is on the arc: keep the side the pick is on.
            if (sp >= st) {
                ns = a.start_angle + st;
            } else {
                ne = a.start_angle + st;
            }
        } else {
            // Beyond the arc: extend whichever end is angularly nearer to it.
            const double past_end = st - total;
            const double before_start = kTwoPi - st;
            if (past_end <= before_start) {
                ne = a.start_angle + st;
            } else {
                ns = a.start_angle + st - kTwoPi;
            }
        }
        return AddArcCommand{a.center, a.radius, ns, ne, 0, a.props, store_.celtscale(h)};
    };
    const std::optional<Command> e1 = trimmed(h1, c1, t1, pick1);
    const std::optional<Command> e2 = trimmed(h2, c2, t2, pick2);

    if (e1) {
        const Command o1 = capture_entity(h1);
        remove_indexed(h1);
        push_erase_item(group, h1, o1);
        push_create_item(group, create_indexed(*e1), *e1);
    }
    if (e2) {
        const Command o2 = capture_entity(h2);
        remove_indexed(h2);
        push_erase_item(group, h2, o2);
        push_create_item(group, create_indexed(*e2), *e2);
    }
    push_create_item(group, create_indexed(arc), arc);
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
        if (!polyline_ops::chamfer_corner(pts, pl->closed, sv, d_prev, d_next)) {
            report("Chamfer: distances too large for that corner.");
            return;
        }
        const bool closed = pl->closed;
        const Command orig = capture_entity(h1);
        remove_indexed(h1);
        push_erase_item(group, h1, orig);
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
    push_erase_item(group, h1, o1);
    remove_indexed(h2);
    push_erase_item(group, h2, o2);
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
            if constexpr (std::is_same_v<T, AddPointCommand> ||
                          std::is_same_v<T, AddXlineCommand> ||
                          std::is_same_v<T, AddEllipseCommand> ||
                          std::is_same_v<T, AddSplineCommand> ||
                          std::is_same_v<T, AddLineCommand> ||
                          std::is_same_v<T, AddPolylineCommand> ||
                          std::is_same_v<T, AddCircleCommand> || std::is_same_v<T, AddArcCommand> ||
                          std::is_same_v<T, AddTextCommand> || std::is_same_v<T, AddDimensionCommand> ||
                          std::is_same_v<T, AddLeaderCommand> || std::is_same_v<T, AddMTextCommand> ||
                          std::is_same_v<T, AddMLeaderCommand> ||
                          std::is_same_v<T, AddInsertCommand>) {
                if (!x.props) {
                    x.props = EntityProps{};
                }
                fn(*x.props);
            } else if constexpr (std::is_same_v<T, AddAttDefCommand>) {
                if (!x.text.props) {
                    x.text.props = EntityProps{};
                }
                fn(*x.text.props);
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
        push_erase_item(group, h, original);
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
        push_erase_item(group, h, original);
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

void GeometryEngine::apply_match_pick_source(Vec2 point, double radius) {
    // Capture the source's property values once (snapshot as an Add*Command). The source
    // entity is never mutated; subsequent applies read from this snapshot.
    const EntityHandle h = pick_nearest(point, radius);
    if (h.is_null() || !store_.is_valid(h) || !selectable(h)) {
        match_source_.reset();
        report("MATCHPROP: no source object found.");
        return;
    }
    match_source_ = capture_entity(h);
    match_source_handle_ = h;
    report("MATCHPROP: source selected -- pick destination objects.");
}

void GeometryEngine::apply_match_source_from_selection() {
    // Noun-verb: MA was run with a selection active -> the first selected (selectable)
    // entity is the source. Reduce the selection to just it so the source is highlighted
    // while picking targets; no undo entry (this only records the source).
    EntityHandle src;
    for (const EntityHandle h : selection_) {
        if (store_.is_valid(h) && selectable(h)) {
            src = h;
            break;
        }
    }
    if (src.is_null()) {
        match_source_.reset();
        report("MATCHPROP: nothing selectable in the current selection.");
        return;
    }
    match_source_ = capture_entity(src);
    match_source_handle_ = src;
    selection_ = {src};
    report("MATCHPROP: source = selection -- pick destination objects.");
}

void GeometryEngine::apply_match_apply(Vec2 point, double radius, const MatchPropFilter& filter,
                                       std::uint64_t group) {
    if (!match_source_.has_value()) {
        report("MATCHPROP: select a source object first.");
        return;
    }
    const EntityHandle h = pick_nearest(point, radius);
    // Targets respect the same selectable() gate (off/frozen/locked layers excluded);
    // matching to the source itself is a no-op. Missing the geometry is silent (clean).
    if (h.is_null() || !store_.is_valid(h) || !selectable(h) || h == match_source_handle_) {
        return;
    }
    // ONE property-write path: registry copies the applicable properties into the captured
    // target command; then the standard capture/erase/recreate as one undo group (the same
    // pattern as apply_set_property). ByLayer/ByBlock travels as state, not resolved literal.
    const Command original = capture_entity(h);
    Command modified = original;
    const int applied = match_properties(*match_source_, modified, filter);
    if (applied == 0) {
        return; // every category filtered off -> nothing to do, no undo entry
    }
    remove_indexed(h);
    push_erase_item(group, h, original);
    const EntityHandle nh = create_indexed(modified);
    push_create_item(group, nh, modified);
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report("Properties matched.");
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
    } else if (h.kind == EntityKind::Polyline) {
        // A valid polyline only fails to offset when the distance collapses/folds it.
        report("Offset distance too large for this polyline.");
    } else if (h.kind == EntityKind::Circle || h.kind == EntityKind::Arc) {
        // Circle/arc offset only fails when shrinking inward past radius 0.
        report("Offset distance too large (would collapse the curve).");
    } else {
        report("Offset: can't offset that entity.");
    }
}

// PEDIT: every option is "capture the polyline, change the vertex list, re-create it"
// as one undo group, so undo, redo and the file see an ordinary polyline edit. A line
// or arc under the pick becomes a polyline first (AutoCAD's "turn it into one?").
void GeometryEngine::apply_pedit(const PeditCommand& c) {
    const EntityHandle h = pick_nearest(c.pick, c.pick_radius);
    if (h.is_null()) {
        report("PEDIT: nothing under the pick.");
        return;
    }
    std::vector<Vec2> v;
    std::vector<double> b;
    bool closed = false;
    EntityProps props{};
    double cts = 1.0;
    bool converted = false;
    if (h.kind == EntityKind::Polyline) {
        const PolylineData* pl = store_.polyline(h);
        const auto vs = store_.vertices_of(*pl);
        const auto bs = store_.bulges_of(*pl);
        v.assign(vs.begin(), vs.end());
        b.assign(v.size(), 0.0);
        if (!bs.empty()) {
            b.assign(bs.begin(), bs.end());
        }
        closed = pl->closed;
        props = pl->props;
        cts = store_.celtscale(h);
    } else if (h.kind == EntityKind::Line) {
        const LineData* l = store_.line(h);
        v = {l->a, l->b};
        b = {0.0, 0.0};
        props = l->props;
        cts = store_.celtscale(h);
        converted = true;
    } else if (h.kind == EntityKind::Arc) {
        const ArcData* a = store_.arc(h);
        double sweep = a->end_angle - a->start_angle;
        while (sweep <= 0.0) {
            sweep += kTwoPi;
        }
        v = {{a->center.x + a->radius * std::cos(a->start_angle),
              a->center.y + a->radius * std::sin(a->start_angle)},
             {a->center.x + a->radius * std::cos(a->end_angle),
              a->center.y + a->radius * std::sin(a->end_angle)}};
        b = {std::tan(sweep / 4.0), 0.0};
        props = a->props;
        cts = store_.celtscale(h);
        converted = true;
    } else {
        report("PEDIT: select a polyline (or a line or arc to convert).");
        return;
    }
    const std::size_t n = v.size();
    std::optional<Command> replacement;
    std::string what;
    const auto nearest_vertex = [&](Vec2 p) {
        std::size_t best = 0;
        double bd = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < n; ++i) {
            const double d = length_squared(v[i] - p);
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
        return best;
    };
    switch (c.op) {
    case 0: // Close
        if (n < 3) {
            report("PEDIT: a polyline needs three vertices to close.");
            return;
        }
        closed = true;
        what = "Closed.";
        break;
    case 1: // Open
        closed = false;
        what = "Opened.";
        break;
    case 2: { // Reverse: vertices backwards; each segment's bulge flips sign
        std::vector<Vec2> rv(v.rbegin(), v.rend());
        std::vector<double> rb(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t src = (n + n - 2 - i) % n; // old segment now traversed backwards
            if (closed || i + 1 < n) {
                rb[i] = -b[src];
            }
        }
        v = std::move(rv);
        b = std::move(rb);
        what = "Reversed.";
        break;
    }
    case 3: // Decurve
        std::fill(b.begin(), b.end(), 0.0);
        what = "Decurved.";
        break;
    case 4: { // Spline: a fit spline through the vertices (back to the first when closed)
        std::vector<Vec2> fit = v;
        if (closed && n >= 2) {
            fit.push_back(v.front());
        }
        if (fit.size() < 2) {
            report("PEDIT: not enough vertices for a spline.");
            return;
        }
        AddSplineCommand sp;
        sp.control_points = spline::fit_or_fallback(fit, 3, spline::FitParam::Chord);
        sp.degree = 3;
        sp.props = props;
        replacement = sp;
        what = "Converted to a spline.";
        break;
    }
    case 5: { // Insert a vertex at p1, into the nearest segment
        double q = 0.0;
        double dist = 0.0;
        polyline_param(v, b, closed, c.p1, q, dist);
        const auto seg = static_cast<std::size_t>(std::floor(q + 1e-12));
        const std::size_t at = std::min(seg + 1, n);
        v.insert(v.begin() + static_cast<std::ptrdiff_t>(at), c.p1);
        b.insert(b.begin() + static_cast<std::ptrdiff_t>(at), 0.0);
        if (seg < b.size()) {
            b[seg] = 0.0; // the split segment becomes straight on both sides
        }
        what = "Vertex inserted.";
        break;
    }
    case 6: { // Delete the vertex nearest p1
        if ((closed && n <= 3) || (!closed && n <= 2)) {
            report("PEDIT: cannot delete -- too few vertices would remain.");
            return;
        }
        const std::size_t i = nearest_vertex(c.p1);
        v.erase(v.begin() + static_cast<std::ptrdiff_t>(i));
        b.erase(b.begin() + static_cast<std::ptrdiff_t>(i));
        if (i > 0 && i - 1 < b.size()) {
            b[i - 1] = 0.0; // the segment bridging the gap is straight
        }
        what = "Vertex deleted.";
        break;
    }
    case 7: { // Move the vertex nearest p1 to p2
        const std::size_t i = nearest_vertex(c.p1);
        v[i] = c.p2;
        what = "Vertex moved.";
        break;
    }
    default:
        report("PEDIT: unknown option.");
        return;
    }
    if (!replacement) {
        bool any_bulge = false;
        for (const double bb : b) {
            any_bulge = any_bulge || std::abs(bb) > 1e-12;
        }
        replacement = AddPolylineCommand{v, closed, 0, props, any_bulge ? b : std::vector<double>{}, cts};
    }
    const Command original = capture_entity(h);
    remove_indexed(h);
    push_erase_item(c.group, h, original);
    const EntityHandle nh = create_indexed(*replacement);
    push_create_item(c.group, nh, *replacement);
    selection_ = {nh};
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report((converted ? "Converted to a polyline. " : "") + what);
}

void GeometryEngine::apply_trim(Vec2 pick, double radius, std::uint64_t group) {
    const EntityHandle h = pick_nearest(pick, radius);
    if (h.is_null()) {
        report("Trim: nothing under the pick.");
        return;
    }
    if (h.kind != EntityKind::Line && h.kind != EntityKind::Arc && h.kind != EntityKind::Circle &&
        h.kind != EntityKind::Polyline) {
        report("Trim: only lines, arcs, circles and polylines can be trimmed.");
        return;
    }

    // Every crossing with a nearby entity, whatever both kinds are. The kernel already
    // resolves line-vs-curve exactly and falls back to tessellation for curve-vs-curve,
    // so the cutting side has never been the limitation -- only the trimmed side was.
    Vec2 lo_box;
    Vec2 hi_box;
    entity_aabb(store_, h, lo_box, hi_box);
    std::vector<EntityHandle> cand;
    grid_.query(lo_box, hi_box, cand);
    std::vector<Vec2> crossings;
    std::vector<Vec2> hits;
    for (const EntityHandle c : cand) {
        if (c == h) {
            continue;
        }
        hits.clear();
        kernel_.intersect(store_, h, c, hits);
        crossings.insert(crossings.end(), hits.begin(), hits.end());
    }
    if (crossings.empty()) {
        report("Trim: no crossing edge found.");
        return;
    }

    // The surviving pieces, built per kind; the shared tail commits them as one group.
    std::vector<Command> pieces;

    if (h.kind == EntityKind::Line) {
        const LineData* l = store_.line(h);
        const Vec2 a = l->a;
        const Vec2 b = l->b;
        const Vec2 ab = b - a;
        const double len2 = length_squared(ab);
        if (len2 <= 0.0) {
            return;
        }
        const EntityProps props = l->props;
        const double cts = store_.celtscale(h);
        std::vector<double> ts;
        for (const Vec2& p : crossings) {
            const double t = dot(p - a, ab) / len2;
            if (t > 1e-6 && t < 1.0 - 1e-6) {
                ts.push_back(t);
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
        // The pieces are the SAME object, shortened, so they keep its properties. Read
        // them BEFORE the store pointer is invalidated, and pass them explicitly --
        // leaving props empty stamps the CURRENT layer, silently moving the line off its own.
        if (lo_t > 1e-6) {
            pieces.push_back(AddLineCommand{a, a + ab * lo_t, 0, props, cts});
        }
        if (hi_t < 1.0 - 1e-6) {
            pieces.push_back(AddLineCommand{a + ab * hi_t, b, 0, props, cts});
        }
    } else if (h.kind == EntityKind::Arc) {
        const ArcData* arc = store_.arc(h);
        const Vec2 centre = arc->center;
        const double r = arc->radius;
        const double start = arc->start_angle;
        const EntityProps props = arc->props;
        const double cts = store_.celtscale(h);
        double total = arc->end_angle - start;
        while (total <= 0.0) {
            total += kTwoPi;
        }
        // Sweep from the arc's own start, so a crossing is one number and the surviving
        // pieces are just the two ends of that sweep -- the same shape as the line case.
        const auto sweep_of = [&](Vec2 p) {
            double sw = std::atan2(p.y - centre.y, p.x - centre.x) - start;
            while (sw < 0.0) {
                sw += kTwoPi;
            }
            return sw;
        };
        std::vector<double> ss;
        for (const Vec2& p : crossings) {
            const double sw = sweep_of(p);
            if (sw > 1e-6 && sw < total - 1e-6) {
                ss.push_back(sw);
            }
        }
        if (ss.empty()) {
            report("Trim: no crossing edge found.");
            return;
        }
        std::sort(ss.begin(), ss.end());
        const double sp = std::clamp(sweep_of(pick), 0.0, total);
        double lo_s = 0.0;
        double hi_s = total;
        for (const double sw : ss) {
            if (sw <= sp) {
                lo_s = sw;
            }
            if (sw >= sp) {
                hi_s = sw;
                break;
            }
        }
        if (lo_s > 1e-6) {
            pieces.push_back(AddArcCommand{centre, r, start, start + lo_s, 0, props, cts});
        }
        if (hi_s < total - 1e-6) {
            pieces.push_back(AddArcCommand{centre, r, start + hi_s, start + total, 0, props, cts});
        }
    } else if (h.kind == EntityKind::Polyline) {
        // The polyline in its own parameter space: segment index + fraction along it
        // (by sweep on a bulged segment). Crossings and the pick become numbers on one
        // axis, and the surviving pieces are sub-ranges -- the same shape as the line
        // case -- re-bulged exactly where a cut lands inside an arc segment.
        const PolylineData* pl = store_.polyline(h);
        const std::span<const Vec2> vs = store_.vertices_of(*pl);
        const std::span<const double> bs = store_.bulges_of(*pl);
        const std::vector<Vec2> v(vs.begin(), vs.end());
        std::vector<double> b(v.size(), 0.0);
        if (!bs.empty()) {
            b.assign(bs.begin(), bs.end());
        }
        const bool closed = pl->closed;
        const EntityProps props = pl->props;
        const double cts = store_.celtscale(h);
        const std::size_t n = v.size();
        if (n < 2) {
            return;
        }
        const double m = static_cast<double>(closed ? n : n - 1);
        std::vector<double> qs;
        for (const Vec2& p : crossings) {
            double q = 0.0;
            double dist = 0.0;
            polyline_param(v, b, closed, p, q, dist);
            if (dist > 0.05) {
                continue; // a crossing reported off the curve (tessellation slack)
            }
            if (!closed && (q < 1e-6 || q > m - 1e-6)) {
                continue; // an open polyline's own ends are not cuts
            }
            qs.push_back(q);
        }
        std::sort(qs.begin(), qs.end());
        qs.erase(std::unique(qs.begin(), qs.end(),
                             [](double x, double y) { return std::abs(x - y) < 1e-9; }),
                 qs.end());
        if (qs.empty()) {
            report("Trim: no crossing edge found.");
            return;
        }
        double qp = 0.0;
        double dp = 0.0;
        polyline_param(v, b, closed, pick, qp, dp);
        std::vector<Vec2> pts;
        std::vector<double> bulges;
        if (!closed) {
            double lo = 0.0;
            double hi = m;
            for (const double q : qs) {
                if (q <= qp) {
                    lo = q;
                }
                if (q >= qp) {
                    hi = q;
                    break;
                }
            }
            if (lo > 1e-6) {
                polyline_sub(v, b, 0.0, lo, pts, bulges);
                pieces.push_back(AddPolylineCommand{pts, false, 0, props, bulges, cts});
            }
            if (hi < m - 1e-6) {
                polyline_sub(v, b, hi, m, pts, bulges);
                pieces.push_back(AddPolylineCommand{pts, false, 0, props, bulges, cts});
            }
        } else {
            // Closed: the crossings bracket the removed span going round, and what is
            // kept is the rest -- one OPEN polyline (the circle rule, on a polyline).
            if (qs.size() < 2) {
                report("Trim: a closed polyline needs two crossing edges to trim between.");
                return;
            }
            double from = qs.back();
            double to = qs.front() + m;
            for (std::size_t i = 0; i < qs.size(); ++i) {
                const double lo_q = qs[i];
                const double hi_q = (i + 1 < qs.size()) ? qs[i + 1] : qs.front() + m;
                const double p_adj = (qp >= lo_q) ? qp : qp + m;
                if (p_adj >= lo_q && p_adj <= hi_q) {
                    from = lo_q;
                    to = hi_q;
                    break;
                }
            }
            polyline_sub(v, b, to, from + m, pts, bulges);
            if (pts.size() >= 2) {
                pieces.push_back(AddPolylineCommand{pts, false, 0, props, bulges, cts});
            }
        }
    } else { // Circle
        const CircleData* ci = store_.circle(h);
        const Vec2 centre = ci->center;
        const double r = ci->radius;
        const EntityProps props = ci->props;
        const double cts = store_.celtscale(h);
        // A circle has no ends, so the crossings themselves bound the piece to remove:
        // find the two that bracket the pick, going round, and keep the rest. That
        // leaves ONE arc -- a circle with a piece missing is an arc.
        std::vector<double> as;
        for (const Vec2& p : crossings) {
            double ang = std::atan2(p.y - centre.y, p.x - centre.x);
            while (ang < 0.0) {
                ang += kTwoPi;
            }
            as.push_back(ang);
        }
        std::sort(as.begin(), as.end());
        as.erase(std::unique(as.begin(), as.end(),
                             [](double x, double y) { return std::abs(x - y) < 1e-9; }),
                 as.end());
        if (as.size() < 2) {
            report("Trim: a circle needs two crossing edges to trim between.");
            return;
        }
        double pa = std::atan2(pick.y - centre.y, pick.x - centre.x);
        while (pa < 0.0) {
            pa += kTwoPi;
        }
        // The bracketing pair, wrapping past 2*pi for a pick in the last gap.
        double from = as.back();
        double to = as.front();
        for (std::size_t i = 0; i < as.size(); ++i) {
            const double lo_a = as[i];
            const double hi_a = (i + 1 < as.size()) ? as[i + 1] : as.front() + kTwoPi;
            const double p_adj = (pa >= lo_a) ? pa : pa + kTwoPi;
            if (p_adj >= lo_a && p_adj <= hi_a) {
                from = lo_a;
                to = hi_a;
                break;
            }
        }
        // Keep the complement: from the end of the removed span round to its start.
        pieces.push_back(AddArcCommand{centre, r, to, from + kTwoPi, 0, props, cts});
    }

    if (pieces.empty()) {
        report("Trim: that would remove the whole object -- use ERASE.");
        return;
    }
    const Command original = capture_entity(h);
    remove_indexed(h);
    push_erase_item(group, h, original);
    for (const Command& piece : pieces) {
        push_create_item(group, create_indexed(piece), piece);
    }
    redo_.clear();
    geom_dirty_ = true;
    dirty_ = true;
    report("Trimmed.");
}

namespace {
// One entity's contribution to a joined polyline: an ordered vertex list with a bulge
// per segment (size == verts.size()-1). Lines -> 2 verts / 1 zero bulge; arcs -> 2 verts
// / 1 bulge (tan(sweep/4)); open polylines -> their own verts + bulges (closed ones have
// no free endpoints and can't be joined).
struct JoinSeg {
    std::vector<Vec2> verts;
    std::vector<double> bulges;
    EntityHandle handle{};
    EntityProps props{};
};

// Reverse a vertex+bulge chain: vertices flip order and each arc's sweep sign flips, so
// the new per-segment bulges are the reverse of the negated old ones.
void reverse_join(std::vector<Vec2>& verts, std::vector<double>& bulges) {
    std::reverse(verts.begin(), verts.end());
    const std::size_t m = bulges.size();
    std::vector<double> nb(m);
    for (std::size_t i = 0; i < m; ++i) {
        nb[i] = -bulges[m - 1 - i];
    }
    bulges = std::move(nb);
}

std::optional<JoinSeg> to_join_seg(const GeometryStore& store, EntityHandle h) {
    JoinSeg js;
    js.handle = h;
    switch (h.kind) {
    case EntityKind::Xline:
        return std::nullopt; // a construction line has no endpoints to join
    case EntityKind::Ellipse:
        return std::nullopt; // not joinable into a polyline
    case EntityKind::Line: {
        const LineData* l = store.line(h);
        if (l == nullptr) {
            return std::nullopt;
        }
        js.verts = {l->a, l->b};
        js.bulges = {0.0};
        js.props = l->props;
        return js;
    }
    case EntityKind::Arc: {
        const ArcData* a = store.arc(h);
        if (a == nullptr) {
            return std::nullopt;
        }
        double sweep = a->end_angle - a->start_angle;
        while (sweep <= 0.0) {
            sweep += kTwoPi; // arcs run CCW from start to end
        }
        js.verts = {{a->center.x + a->radius * std::cos(a->start_angle),
                     a->center.y + a->radius * std::sin(a->start_angle)},
                    {a->center.x + a->radius * std::cos(a->end_angle),
                     a->center.y + a->radius * std::sin(a->end_angle)}};
        js.bulges = {std::tan(sweep / 4.0)};
        js.props = a->props;
        return js;
    }
    case EntityKind::Polyline: {
        const PolylineData* p = store.polyline(h);
        if (p == nullptr || p->closed) {
            return std::nullopt; // a closed polyline has no free endpoints to join
        }
        const std::span<const Vec2> v = store.vertices_of(*p);
        if (v.size() < 2) {
            return std::nullopt;
        }
        const std::span<const double> bl = store.bulges_of(*p);
        js.verts.assign(v.begin(), v.end());
        js.bulges.assign(v.size() - 1, 0.0);
        for (std::size_t i = 0; i + 1 < v.size(); ++i) {
            js.bulges[i] = bl.empty() ? 0.0 : bl[i];
        }
        js.props = p->props;
        return js;
    }
    default:
        return std::nullopt;
    }
}
} // namespace

void GeometryEngine::apply_join(const std::vector<Vec2>& picks, double radius,
                                std::uint64_t group) {
    // Resolve picks -> unique entity handles, then join all that connect.
    std::vector<EntityHandle> ents;
    for (const Vec2 pk : picks) {
        const EntityHandle h = pick_nearest(pk, radius);
        if (h.is_null()) {
            continue;
        }
        if (std::find(ents.begin(), ents.end(), h) == ents.end()) {
            ents.push_back(h);
        }
    }
    join_entities(ents, radius, group);
}

void GeometryEngine::apply_join_selection(double radius, std::uint64_t group) {
    const std::vector<EntityHandle> sel = selection_; // copy: join_entities reassigns selection_
    join_entities(sel, radius, group);
}

void GeometryEngine::apply_hatch_from_selection(const std::string& pattern, double scale,
                                                double angle, std::uint64_t group, Rgb color2) {
    // Each selected CLOSED polyline contributes a boundary loop. The even-odd fill makes
    // separate loops fill independently and nested loops (a polyline inside another) read
    // as holes -- no nesting classification needed here.
    std::vector<std::vector<Vec2>> loops;
    for (const EntityHandle h : selection_) {
        if (h.kind != EntityKind::Polyline) {
            continue;
        }
        const PolylineData* p = store_.polyline(h);
        if (p == nullptr || !p->closed) {
            continue;
        }
        const std::span<const Vec2> v = store_.vertices_of(*p);
        if (v.size() >= 3) {
            loops.emplace_back(v.begin(), v.end());
        }
    }
    if (loops.empty()) {
        report("Valid hatch boundary not found."); // AutoCAD's message
        return;
    }
    AddHatchCommand cmd;
    cmd.loops = std::move(loops);
    cmd.pattern_name = pattern;
    cmd.pattern_scale = scale;
    cmd.pattern_angle = angle;
    cmd.color2 = color2;
    cmd.group = group; // props unset -> created on the current layer, ByLayer (AutoCAD default)
    const EntityHandle nh = create_indexed(cmd);
    push_create_item(group, nh, cmd);
    selection_.clear();
    selection_.push_back(nh); // select the new hatch
    redo_.clear();
    geom_dirty_ = true;
    report(pattern == "SOLID" ? "Solid hatch created." : "Hatch created.");
}

void GeometryEngine::apply_hatch_pick_point(Vec2 p, const std::string& pattern, double scale,
                                            double angle, std::uint64_t group, Rgb color2) {
    // Gather candidate boundary edges from every curve-like entity (tessellated, so arcs +
    // bulged polylines follow their true shape), closing the loop for closed shapes.
    std::vector<hatch::Segment> segs;
    std::vector<Vec2> tess;
    const auto gather = [&](EntityHandle h, bool closed) {
        kernel_.tessellate(store_, h, kDefaultTessTolerance, tess);
        for (std::size_t i = 1; i < tess.size(); ++i) {
            segs.push_back({tess[i - 1], tess[i]});
        }
        if (closed && tess.size() >= 3) {
            segs.push_back({tess.back(), tess.front()});
        }
    };
    for (const EntityHandle h : all_live()) {
        switch (h.kind) {
        case EntityKind::Xline:
            break; // an infinite line is not a hatch boundary
        case EntityKind::Ellipse:
            gather(h, ellipse::is_full(*store_.ellipse(h)));
            break;
        case EntityKind::Line:
        case EntityKind::Arc:
        case EntityKind::Spline:
            gather(h, false);
            break;
        case EntityKind::Circle:
            gather(h, true);
            break;
        case EntityKind::Polyline: {
            const PolylineData* pl = store_.polyline(h);
            gather(h, pl != nullptr && pl->closed);
            break;
        }
        default:
            break; // text/dim/leader/insert/hatch are not boundaries
        }
    }
    const double tol = 1e-6; // basic endpoint gap bridging; full HPGAPTOL parity staged
    const std::optional<std::vector<Vec2>> outer = hatch::trace_boundary(segs, p, tol);
    if (!outer) {
        report("Valid hatch boundary not found.");
        return;
    }
    std::vector<std::vector<Vec2>> loops;
    loops.push_back(*outer);
    // Islands: closed polylines / circles fully inside the outer loop, NOT containing the
    // pick (a loop containing the pick is the boundary, not a hole), become holes.
    std::vector<Vec2> verts;
    for (const EntityHandle h : all_live()) {
        if (h.kind == EntityKind::Polyline) {
            const PolylineData* pl = store_.polyline(h);
            if (pl == nullptr || !pl->closed) {
                continue;
            }
        } else if (h.kind != EntityKind::Circle) {
            continue;
        }
        kernel_.tessellate(store_, h, kDefaultTessTolerance, verts);
        if (verts.size() < 3 || hatch::point_in_loops({verts}, p)) {
            continue; // too small, or the pick is inside it (it's a boundary, not a hole)
        }
        bool all_in = true;
        for (const Vec2& v : verts) {
            if (!hatch::point_in_loops({*outer}, v)) {
                all_in = false;
                break;
            }
        }
        if (all_in) {
            loops.push_back(verts);
        }
    }
    AddHatchCommand cmd;
    cmd.loops = std::move(loops);
    cmd.pattern_name = pattern;
    cmd.pattern_scale = scale;
    cmd.pattern_angle = angle;
    cmd.color2 = color2;
    cmd.group = group;
    const EntityHandle nh = create_indexed(cmd);
    push_create_item(group, nh, cmd);
    selection_.clear();
    selection_.push_back(nh);
    redo_.clear();
    geom_dirty_ = true;
    report(pattern == "SOLID" ? "Solid hatch created." : "Hatch created.");
}

void GeometryEngine::join_entities(const std::vector<EntityHandle>& ents, double radius,
                                   std::uint64_t group) {
    // Convert every joinable input to a uniform vertex+bulge segment. Closed polylines
    // (no free endpoints) and non-curves are not joinable -- left untouched.
    std::vector<JoinSeg> segs;
    for (const EntityHandle h : ents) {
        if (auto js = to_join_seg(store_, h)) {
            segs.push_back(std::move(*js));
        }
    }
    if (segs.size() < 2) {
        report("JOIN: select at least two joinable objects (lines, arcs, open polylines).");
        return;
    }

    const double tol = radius > 0.0 ? radius : 1e-6;
    const auto approx = [tol](Vec2 a, Vec2 b) { return length(a - b) <= tol; };
    const auto append_tail = [](std::vector<Vec2>& cv, std::vector<double>& cb,
                                const JoinSeg& seg) {
        for (std::size_t k = 1; k < seg.verts.size(); ++k) {
            cb.push_back(seg.bulges[k - 1]);
            cv.push_back(seg.verts[k]);
        }
    };
    // Splice candidate `e` onto either end of the chain (cv,cb), reversing as needed so
    // every splice is a tail-append. Returns false if `e` shares no endpoint with the chain.
    const auto try_connect = [&](std::vector<Vec2>& cv, std::vector<double>& cb, JoinSeg e) {
        if (approx(e.verts.front(), cv.back())) {
            append_tail(cv, cb, e);
        } else if (approx(e.verts.back(), cv.back())) {
            reverse_join(e.verts, e.bulges);
            append_tail(cv, cb, e);
        } else if (approx(e.verts.back(), cv.front())) {
            reverse_join(cv, cb);
            append_tail(cv, cb, e);
        } else if (approx(e.verts.front(), cv.front())) {
            reverse_join(cv, cb);
            reverse_join(e.verts, e.bulges);
            append_tail(cv, cb, e);
        } else {
            return false;
        }
        return true;
    };

    std::vector<bool> used(segs.size(), false);
    std::vector<EntityHandle> new_sel;
    int polylines_made = 0;
    std::size_t entities_joined = 0;
    bool single_closed = false;

    // Each connected sub-chain among the inputs becomes its own polyline; an entity that
    // connects to nothing else selected stays a separate entity (chain of one).
    for (std::size_t seed = 0; seed < segs.size(); ++seed) {
        if (used[seed]) {
            continue;
        }
        std::vector<Vec2> cv = segs[seed].verts;
        std::vector<double> cb = segs[seed].bulges; // invariant: cb.size() == cv.size() - 1
        std::vector<EntityHandle> chain{segs[seed].handle};
        used[seed] = true;
        bool progress = true;
        while (progress) {
            progress = false;
            for (std::size_t i = 0; i < segs.size(); ++i) {
                if (!used[i] && try_connect(cv, cb, segs[i])) {
                    used[i] = true;
                    chain.push_back(segs[i].handle);
                    progress = true;
                }
            }
        }
        if (chain.size() < 2) {
            continue; // nothing connected -> leave this entity alone
        }

        // A chain whose ends meet closes into a closed polyline (which OFFSETs uniformly).
        bool closed = false;
        if (cv.size() >= 3 && approx(cv.front(), cv.back())) {
            closed = true;
            cv.pop_back(); // drop the duplicate closing vertex; cb already wraps it
        }
        // Bulges: empty when all straight; else sized to points (open pads the last vertex).
        std::vector<double> out_bulges;
        if (std::any_of(cb.begin(), cb.end(), [](double b) { return b != 0.0; })) {
            out_bulges = cb;
            if (!closed) {
                out_bulges.push_back(0.0);
            }
        }
        // Erase the chain's sources and add the merged polyline -- all in one undo group.
        for (const EntityHandle h : chain) {
            if (!store_.is_valid(h)) {
                continue;
            }
            const Command original = capture_entity(h);
            remove_indexed(h);
            push_erase_item(group, h, original);
        }
        AddPolylineCommand cmd;
        cmd.points = cv;
        cmd.closed = closed;
        cmd.group = group;
        cmd.props = segs[seed].props; // result inherits the seed (source) entity's props
        cmd.bulges = out_bulges;
        const EntityHandle nh = create_indexed(cmd);
        push_create_item(group, nh, cmd);
        new_sel.push_back(nh);
        ++polylines_made;
        entities_joined += chain.size();
        single_closed = closed;
    }

    if (polylines_made == 0) {
        report("JOIN: the selected objects don't share endpoints.");
        return;
    }
    selection_ = new_sel;
    redo_.clear();
    geom_dirty_ = true;

    std::string msg;
    if (polylines_made == 1) {
        msg = std::to_string(entities_joined) + " objects joined into a " +
              (single_closed ? "closed polyline." : "polyline.");
    } else {
        msg = "Joined " + std::to_string(entities_joined) + " objects into " +
              std::to_string(polylines_made) + " polylines.";
    }
    report(msg);
}

void GeometryEngine::apply(const Command& command) {
    std::visit(
        [this, &command](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, AddPointCommand> ||
                          std::is_same_v<T, AddXlineCommand> ||
                          std::is_same_v<T, AddEllipseCommand> ||
                          std::is_same_v<T, AddSplineCommand> ||
                          std::is_same_v<T, AddLineCommand> ||
                          std::is_same_v<T, AddPolylineCommand> ||
                          std::is_same_v<T, AddCircleCommand> ||
                          std::is_same_v<T, AddArcCommand> || std::is_same_v<T, AddTextCommand> ||
                          std::is_same_v<T, AddAttDefCommand> ||
                          std::is_same_v<T, AddDimensionCommand> ||
                          std::is_same_v<T, AddLeaderCommand> || std::is_same_v<T, AddMTextCommand> ||
                          std::is_same_v<T, AddMLeaderCommand> ||
                          std::is_same_v<T, AddInsertCommand> ||
                          std::is_same_v<T, AddHatchCommand> || std::is_same_v<T, AddFcfCommand> ||
                          std::is_same_v<T, AddDatumCommand> ||
                          std::is_same_v<T, AddImageCommand> ||
                          std::is_same_v<T, AddTableCommand>) {
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
                    push_erase_item(c.group, h, std::move(restore));
                }
                redo_.clear();
                geom_dirty_ = true;
            } else if constexpr (std::is_same_v<T, ErasePickCommand>) {
                const EntityHandle h = pick_nearest(c.world, c.pick_radius);
                if (!h.is_null()) {
                    Command restore = capture_entity(h);
                    remove_indexed(h);
                    push_erase_item(c.group, h, std::move(restore));
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
                    push_erase_item(c.group, h, std::move(restore));
                }
                selection_.clear();
                redo_.clear();
                geom_dirty_ = true;
            } else if constexpr (std::is_same_v<T, SelectPickCommand>) {
                if (!c.additive) {
                    selection_.clear();
                    forget_stretch_windows();
                }
                const std::size_t before = selection_.size();
                const EntityHandle picked = pick_nearest(c.world, c.radius);
                sel_add(picked);
                // GROUP: picking a member selects the whole group (PICKSTYLE on).
                if (pickstyle_group_ && !picked.is_null()) {
                    const std::size_t gi = store_.group_of(picked);
                    if (gi != static_cast<std::size_t>(-1) && store_.groups()[gi].selectable) {
                        for (const EntityHandle m : store_.groups()[gi].members) {
                            if (selectable(m)) {
                                sel_add(m);
                            }
                        }
                    }
                }
                note_selection_for_windows(); // a picked object moves whole; windows stay
                if (c.announce) {
                    const std::size_t found = selection_.size() - before;
                    std::string msg = std::to_string(found) + " found";
                    if (c.additive && before > 0) {
                        msg += ", " + std::to_string(selection_.size()) + " total";
                    }
                    report(msg + ".");
                }
            } else if constexpr (std::is_same_v<T, SelectWindowCommand>) {
                select_window(c.min, c.max, c.crossing, c.additive, c.announce);
            } else if constexpr (std::is_same_v<T, SelectAllCommand>) {
                selection_ = all_live();
                forget_stretch_windows(); // nothing was "caught": everything moves whole
            } else if constexpr (std::is_same_v<T, ClearSelectionCommand>) {
                selection_.clear();
                forget_stretch_windows();
            } else if constexpr (std::is_same_v<T, ChainDimensionCommand>) {
                apply_chain_dimension(c.at, c.baseline, c.group);
            } else if constexpr (std::is_same_v<T, AreaQueryCommand>) {
                apply_area_query(c.at, c.pick_radius);
            } else if constexpr (std::is_same_v<T, ListQueryCommand>) {
                apply_list_query(c.at, c.pick_radius);
            } else if constexpr (std::is_same_v<T, StretchSelectionCommand>) {
                apply_stretch(c.delta, c.group);
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
            } else if constexpr (std::is_same_v<T, JoinPickCommand>) {
                apply_join(c.picks, c.radius, c.group);
            } else if constexpr (std::is_same_v<T, JoinSelectionCommand>) {
                apply_join_selection(c.radius, c.group);
            } else if constexpr (std::is_same_v<T, HatchFromSelectionCommand>) {
                apply_hatch_from_selection(c.pattern_name, c.pattern_scale, c.pattern_angle,
                                           c.group, c.color2);
            } else if constexpr (std::is_same_v<T, HatchPickPointCommand>) {
                apply_hatch_pick_point(c.point, c.pattern_name, c.pattern_scale, c.pattern_angle,
                                       c.group, c.color2);
            } else if constexpr (std::is_same_v<T, RotateSelectionCommand>) {
                apply_rotate(c.base, c.angle, c.group, c.copy);
            } else if constexpr (std::is_same_v<T, ScaleSelectionCommand>) {
                apply_scale(c.base, c.factor, c.group, c.copy);
            } else if constexpr (std::is_same_v<T, ArrayRectCommand>) {
                apply_array_rect(c.rows, c.cols, c.dx, c.dy, c.angle, c.group);
            } else if constexpr (std::is_same_v<T, ArrayPathCommand>) {
                apply_array_path(c);
            } else if constexpr (std::is_same_v<T, DividePathCommand>) {
                apply_divide_measure(c);
            } else if constexpr (std::is_same_v<T, BreakCommand>) {
                apply_break(c);
            } else if constexpr (std::is_same_v<T, PurgeCommand>) {
                apply_purge(c.what);
            } else if constexpr (std::is_same_v<T, RevcloudObjectCommand>) {
                apply_revcloud_object(c);
            } else if constexpr (std::is_same_v<T, RevcloudReverseCommand>) {
                apply_revcloud_reverse(c.group);
            } else if constexpr (std::is_same_v<T, ExplodeSelectionCommand>) {
                apply_explode(c.group);
            } else if constexpr (std::is_same_v<T, StretchPreviewCommand>) {
                // Preview only: recomputed at the next publish on the scratch store.
                stretch_preview_active_ = c.active && !selection_.empty();
                stretch_preview_delta_ = c.delta;
            } else if constexpr (std::is_same_v<T, AlignSelectionCommand>) {
                apply_align(c);
            } else if constexpr (std::is_same_v<T, LengthenCommand>) {
                apply_lengthen(c);
            } else if constexpr (std::is_same_v<T, ArrayPolarCommand>) {
                apply_array_polar(c.center, c.count, c.total_angle, c.rotate_items, c.group);
            } else if constexpr (std::is_same_v<T, ExtendPickCommand>) {
                apply_extend(c.pick, c.radius, c.group);
            } else if constexpr (std::is_same_v<T, FilletPickCommand>) {
                apply_fillet(c.pick1, c.pick2, c.radius, c.pick_radius, c.group);
            } else if constexpr (std::is_same_v<T, ChamferPickCommand>) {
                apply_chamfer(c.pick1, c.pick2, c.dist1, c.dist2, c.pick_radius, c.group);
            } else if constexpr (std::is_same_v<T, AddObjectDimensionCommand>) {
                apply_object_dimension(c.type, c.pick1, c.pick2, c.pick3, c.pick4, c.pick_radius,
                                       c.style, c.group);
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
            } else if constexpr (std::is_same_v<T, BuildPlotSnapshotCommand>) {
                // Read-only: build a fine-tolerance snapshot into the plot buffer (smooth
                // arcs at any paper scale), then bump the version the UI waits on. The
                // store is never mutated; the live snapshot/triple-buffer is untouched.
                const double tol = c.tolerance > 0.0 ? c.tolerance : kDefaultTessTolerance;
                text::set_field_context(field_context_now());
                build_render_snapshot(store_, kernel_, plot_snapshot_, tol, store_.ltscale());
                plot_version_.fetch_add(1, std::memory_order_release);
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
            } else if constexpr (std::is_same_v<T, MatchPropPickSourceCommand>) {
                apply_match_pick_source(c.point, c.radius);
            } else if constexpr (std::is_same_v<T, MatchPropSourceFromSelectionCommand>) {
                apply_match_source_from_selection();
            } else if constexpr (std::is_same_v<T, MatchPropApplyCommand>) {
                apply_match_apply(c.point, c.radius, c.filter, c.group);
            } else if constexpr (std::is_same_v<T, SaveDocumentCommand>) {
                const io::Document doc = io::document_from_store(store_);
                const io::IoResult r =
                    c.dxf ? io::save_dxf(doc, c.path) : io::save_native(doc, c.path);
                if (r.ok) {
                    dirty_ = false;
                    ++document_version_;
                    // A native save (re)binds the active document's path + tab name; a DXF
                    // export does not change the document's identity.
                    if (!c.dxf && active_idx_ < doc_metas_.size()) {
                        doc_metas_[active_idx_].path = c.path;
                        doc_metas_[active_idx_].name = doc_basename(c.path);
                        doc_metas_[active_idx_].is_dxf_path = false;
                    }
                }
                report(r.message);
            } else if constexpr (std::is_same_v<T, OpenDocumentCommand>) {
                if (c.new_tab) {
                    open_into_new_tab(command);
                } else {
                    load_document_replace(command);
                }
            } else if constexpr (std::is_same_v<T, NewDocumentCommand>) {
                new_document();
            } else if constexpr (std::is_same_v<T, CreateDocumentCommand>) {
                create_document(c.name);
            } else if constexpr (std::is_same_v<T, SwitchDocumentCommand>) {
                switch_document(c.id);
            } else if constexpr (std::is_same_v<T, CloseDocumentCommand>) {
                close_document(c.id);
            } else if constexpr (std::is_same_v<T, CopyClipboardCommand>) {
                apply_copy_clipboard();
            } else if constexpr (std::is_same_v<T, CutClipboardCommand>) {
                apply_cut_clipboard(c.group);
            } else if constexpr (std::is_same_v<T, PasteClipboardCommand>) {
                apply_paste_clipboard(c.at, c.group, c.at_cursor);
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
            } else if constexpr (std::is_same_v<T, SetLtscaleCommand>) {
                store_.set_ltscale(c.scale);
                geom_dirty_ = true; // re-dash all non-continuous entities at rebuild
            } else if constexpr (std::is_same_v<T, AddPageSetupCommand>) {
                store_.add_page_setup(c.setup);
                dirty_ = true;      // an unsaved document change
                geom_dirty_ = true; // republish so the snapshot carries the new setup
                report("Page setup \"" + c.setup.name + "\" saved.");
            } else if constexpr (std::is_same_v<T, SaveNamedViewCommand>) {
                store_.add_named_view(c.view);
                dirty_ = true;
                geom_dirty_ = true; // republish the VIEW table
                report("View \"" + c.view.name + "\" saved.");
            } else if constexpr (std::is_same_v<T, DeleteNamedViewCommand>) {
                if (store_.remove_named_view(c.name)) {
                    dirty_ = true;
                    geom_dirty_ = true;
                    report("View \"" + c.name + "\" deleted.");
                } else {
                    report("View \"" + c.name + "\" not found.");
                }
            } else if constexpr (std::is_same_v<T, CreateGroupCommand>) {
                prune_selection();
                if (selection_.empty()) {
                    report("Group: nothing selected.");
                } else {
                    EntityGroup g;
                    g.name = c.name.empty() ? store_.next_group_name() : c.name;
                    g.description = c.description;
                    g.members = selection_;
                    if (!c.name.empty() && store_.group_index(c.name) != static_cast<std::size_t>(-1)) {
                        report("Group \"" + c.name + "\" already exists.");
                    } else {
                        store_.add_group(std::move(g));
                        dirty_ = true;
                        geom_dirty_ = true;
                        report("Group \"" + (c.name.empty() ? store_.groups().back().name : c.name) +
                               "\" has been created.");
                    }
                }
            } else if constexpr (std::is_same_v<T, UngroupCommand>) {
                std::size_t gi = static_cast<std::size_t>(-1);
                if (c.by_name) {
                    gi = store_.group_index(c.name);
                } else {
                    const EntityHandle h = pick_nearest(c.pick, c.pick_radius);
                    if (!h.is_null()) {
                        gi = store_.group_of(h);
                    }
                }
                if (gi == static_cast<std::size_t>(-1)) {
                    report(c.by_name ? "Ungroup: no group named \"" + c.name + "\"."
                                     : "Ungroup: that object is not in a group.");
                } else {
                    const std::string name = store_.groups()[gi].name;
                    store_.remove_group(gi);
                    dirty_ = true;
                    geom_dirty_ = true;
                    report("Group \"" + name + "\" exploded.");
                }
            } else if constexpr (std::is_same_v<T, SetPickStyleCommand>) {
                pickstyle_group_ = c.group_select;
                report(std::string("PICKSTYLE = ") + (c.group_select ? "1" : "0") + ".");
            } else if constexpr (std::is_same_v<T, SetUnitsCommand>) {
                store_.set_units(c.units);
                dirty_ = true;
                geom_dirty_ = true; // republish: the readout and inquiry formats follow
                report(std::string("Units: ") + units::linear_name(c.units.linear) + ", precision " +
                       std::to_string(c.units.linear_precision) + "; angles " +
                       units::angular_name(c.units.angular) + ", precision " +
                       std::to_string(c.units.angular_precision) + ".");
            } else if constexpr (std::is_same_v<T, AuditCommand>) {
                apply_audit(c.fix);
            } else if constexpr (std::is_same_v<T, SetTextStyleCommand>) {
                const std::uint16_t i = store_.add_text_style(c.style);
                if (c.make_current) {
                    store_.set_current_text_style(i);
                }
                dirty_ = true;
                geom_dirty_ = true; // texts using the style re-lay-out; the table republishes
                report("\"" + c.style.name + "\" is now the current text style.");
            } else if constexpr (std::is_same_v<T, DefineBlockCommand>) {
                apply_define_block(c);
            } else if constexpr (std::is_same_v<T, InsertBlockCommand>) {
                std::uint16_t bi = 0xFFFF;
                for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(store_.block_count()); ++i) {
                    if (store_.block(i)->name == c.name) {
                        bi = i;
                    }
                }
                if (bi == 0xFFFF) {
                    report("Block \"" + c.name + "\" not found.");
                } else {
                    AddInsertCommand ins{bi, c.pos, c.scale_x, c.scale_y, c.rotation, 0, {}, {}};
                    ins.attribs = c.attribs;
                    const Command add = ins;
                    const EntityHandle nh = create_indexed(add);
                    push_create_item(c.group, nh, add);
                    redo_.clear();
                    geom_dirty_ = true;
                    dirty_ = true;
                    report("Inserted \"" + c.name + "\".");
                }
            } else if constexpr (std::is_same_v<T, WriteBlockCommand>) {
                apply_write_block(c);
            } else if constexpr (std::is_same_v<T, RegenCommand>) {
                geom_dirty_ = true;
                report("Regenerating model.");
            } else if constexpr (std::is_same_v<T, PeditCommand>) {
                apply_pedit(c);
            } else if constexpr (std::is_same_v<T, RefEditCommand>) {
                apply_refedit(c);
            } else if constexpr (std::is_same_v<T, RefSetCommand>) {
                apply_refset(c);
            } else if constexpr (std::is_same_v<T, RefCloseCommand>) {
                apply_refclose(c);
            } else if constexpr (std::is_same_v<T, PolylineVertexCommand>) {
                apply_polyline_vertex(c);
            } else if constexpr (std::is_same_v<T, SetAttDispCommand>) {
                store_.set_attdisp(c.mode);
                geom_dirty_ = true;
                dirty_ = true;
                report(c.mode == 1   ? "Attributes: all on."
                       : c.mode == 2 ? "Attributes: all off."
                                     : "Attributes: normal (each attribute's own visibility).");
            } else if constexpr (std::is_same_v<T, SetInsertAttribCommand>) {
                const EntityHandle h = pick_nearest(c.pick, c.pick_radius);
                const InsertData* in = store_.insert(h);
                const BlockDef* bd = in != nullptr ? store_.block(in->block) : nullptr;
                if (bd == nullptr) {
                    report("Select a block reference.");
                } else if (bd->content.attdefs.empty()) {
                    report("That block has no attributes.");
                } else {
                    std::vector<std::string> values = store_.insert_attribs(*in);
                    for (std::size_t i = values.size(); i < bd->content.attdefs.size(); ++i) {
                        values.push_back(bd->content.attdefs[i].def); // still at the default
                    }
                    const auto same_tag = [](std::string_view a, std::string_view b) {
                        if (a.size() != b.size()) {
                            return false;
                        }
                        for (std::size_t i = 0; i < a.size(); ++i) {
                            if (std::toupper(static_cast<unsigned char>(a[i])) !=
                                std::toupper(static_cast<unsigned char>(b[i]))) {
                                return false;
                            }
                        }
                        return true;
                    };
                    bool any = false;
                    for (std::size_t i = 0; i < bd->content.attdefs.size(); ++i) {
                        if (c.tag.empty() || same_tag(bd->content.attdefs[i].tag, c.tag)) {
                            values[i] = c.value;
                            any = true;
                        }
                    }
                    if (!any) {
                        report("No attribute tagged \"" + c.tag + "\" on that block.");
                    } else {
                        // An edit is an erase + create of the reference, so it undoes as one.
                        const Command original = capture_entity(h);
                        remove_indexed(h);
                        push_erase_item(c.group, h, original);
                        AddInsertCommand nc = std::get<AddInsertCommand>(original);
                        nc.attribs = values;
                        const Command add = nc;
                        push_create_item(c.group, create_indexed(add), add);
                        redo_.clear();
                        geom_dirty_ = true;
                        dirty_ = true;
                        report("Attribute updated.");
                    }
                }
            } else if constexpr (std::is_same_v<T, SetWipeoutFramesCommand>) {
                store_.set_wipeout_frames(c.on);
                dirty_ = true;
                geom_dirty_ = true;
                report(c.on ? "Wipeout frames on." : "Wipeout frames off.");
            } else if constexpr (std::is_same_v<T, WipeoutFromPolylineCommand>) {
                const EntityHandle h = pick_nearest(c.pick, c.pick_radius);
                if (h.is_null() || h.kind != EntityKind::Polyline || !store_.polyline(h)->closed) {
                    report("Wipeout: select a closed polyline.");
                } else {
                    std::vector<Vec2> ring;
                    kernel_.tessellate(store_, h, tess_tolerance_, ring); // arcs become chords
                    if (ring.size() >= 2 && length_squared(ring.front() - ring.back()) < 1e-18) {
                        ring.pop_back();
                    }
                    if (ring.size() < 3) {
                        report("Wipeout: that polyline has too few vertices.");
                    } else {
                        AddHatchCommand w;
                        w.loops = {ring};
                        w.pattern_name = "WIPEOUT";
                        w.props = store_.polyline(h)->props;
                        if (c.erase) {
                            const Command original = capture_entity(h);
                            remove_indexed(h);
                            push_erase_item(c.group, h, original);
                        }
                        const Command add = w;
                        push_create_item(c.group, create_indexed(add), add);
                        redo_.clear();
                        geom_dirty_ = true;
                        dirty_ = true;
                        report("Wipeout created.");
                    }
                }
            } else if constexpr (std::is_same_v<T, SetCurrentTextStyleCommand>) {
                const std::uint16_t i = store_.text_style_index(c.name);
                if (i == 0xFFFF) {
                    report("Text style \"" + c.name + "\" not found.");
                } else {
                    store_.set_current_text_style(i);
                    dirty_ = true;
                    geom_dirty_ = true;
                    report("\"" + c.name + "\" is now the current text style.");
                }
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
                std::is_same_v<T, CreateDocumentCommand> ||
                std::is_same_v<T, SwitchDocumentCommand> ||
                std::is_same_v<T, CloseDocumentCommand> ||
                std::is_same_v<T, CopyClipboardCommand> || // read-only (snapshots selection)
                std::is_same_v<T, SetLineweightDisplayCommand> ||
                std::is_same_v<T, ResolveDimObjectCommand> ||
                std::is_same_v<T, SetViewScaleCommand> ||
                std::is_same_v<T, BuildPlotSnapshotCommand> || // read-only plot build
                std::is_same_v<T, StretchPreviewCommand> || // rubber band only
                std::is_same_v<T, GripDragCommand>; // Commit sets dirty_ itself
            if constexpr (!view_or_io) {
                dirty_ = true;
                ++edit_serial_;
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

void GeometryEngine::reset_active_state() {
    // Reset the live (active-document) heavy state to a fresh empty drawing.
    store_.clear();
    store_.set_font_engine(font_engine_); // re-bind metrics (clear may drop the engine)
    grid_.clear();
    undo_.clear();
    redo_.clear();
    selection_.clear();
    refedit_ = RefEditSession{};
    geom_cache_.clear();
    geom_dirty_ = true;
    geom_version_ = 0;
    dirty_ = false;
    document_version_ = 0;
    has_pending_dim_ = false;
    pending_dim_ = DimData{};
    grip_active_ = false;
    grip_handle_ = EntityHandle{};
    grip_index_ = 0;
    grip_preview_store_.clear();
    forget_stretch_windows();
    stretch_preview_active_ = false;
}

void GeometryEngine::new_document() {
    reset_active_state();
    ++document_version_;
    report("New drawing.");
}

void GeometryEngine::park_active(DocState& d) {
    d.store = std::move(store_);
    d.grid = std::move(grid_);
    d.undo = std::move(undo_);
    d.redo = std::move(redo_);
    d.selection = std::move(selection_);
    d.refedit = std::move(refedit_);
    d.geom_cache = std::move(geom_cache_);
    d.geom_dirty = geom_dirty_;
    d.geom_version = geom_version_;
    d.dirty = dirty_;
    d.document_version = document_version_;
    d.has_pending_dim = has_pending_dim_;
    d.pending_dim = pending_dim_;
    d.pending_dim_version = pending_dim_version_;
    d.grip_active = grip_active_;
    d.grip_handle = grip_handle_;
    d.grip_index = grip_index_;
    d.grip_pos = grip_pos_;
    d.grip_preview_store = std::move(grip_preview_store_);
    d.stretch_windows = std::move(stretch_windows_);
    d.stretch_windows_sel = std::move(stretch_windows_sel_);
    d.stretch_preview_active = stretch_preview_active_;
    d.stretch_preview_delta = stretch_preview_delta_;
}

void GeometryEngine::load_active(DocState& d) {
    store_ = std::move(d.store);
    store_.set_font_engine(font_engine_); // font engine is engine-global
    grid_ = std::move(d.grid);
    undo_ = std::move(d.undo);
    redo_ = std::move(d.redo);
    selection_ = std::move(d.selection);
    refedit_ = std::move(d.refedit);
    geom_cache_ = std::move(d.geom_cache);
    geom_dirty_ = true; // force a rebuild at the current zoom for the new active document
    geom_version_ = d.geom_version;
    dirty_ = d.dirty;
    document_version_ = d.document_version;
    has_pending_dim_ = d.has_pending_dim;
    pending_dim_ = d.pending_dim;
    pending_dim_version_ = d.pending_dim_version;
    grip_active_ = d.grip_active;
    grip_handle_ = d.grip_handle;
    grip_index_ = d.grip_index;
    grip_pos_ = d.grip_pos;
    grip_preview_store_ = std::move(d.grip_preview_store);
    stretch_windows_ = std::move(d.stretch_windows);
    stretch_windows_sel_ = std::move(d.stretch_windows_sel);
    stretch_preview_active_ = d.stretch_preview_active;
    stretch_preview_delta_ = d.stretch_preview_delta;
}

std::size_t GeometryEngine::doc_index(std::uint64_t id) const {
    for (std::size_t i = 0; i < doc_metas_.size(); ++i) {
        if (doc_metas_[i].id == id) {
            return i;
        }
    }
    return doc_metas_.size(); // not found
}

void GeometryEngine::create_document(const std::string& name) {
    DocState parked;
    park_active(parked);
    parked_[doc_metas_[active_idx_].id] = std::move(parked);
    reset_active_state();
    ++document_version_;
    DocMeta m;
    m.id = next_doc_id_++;
    m.name = name.empty() ? ("Drawing" + std::to_string(++doc_name_counter_)) : name;
    doc_metas_.push_back(std::move(m));
    active_idx_ = doc_metas_.size() - 1;
    report("New drawing.");
}

void GeometryEngine::switch_document(std::uint64_t id) {
    const std::size_t idx = doc_index(id);
    if (idx >= doc_metas_.size() || idx == active_idx_) {
        return;
    }
    DocState parked;
    park_active(parked);
    parked_[doc_metas_[active_idx_].id] = std::move(parked);
    auto it = parked_.find(id);
    if (it == parked_.end()) {
        return; // should not happen: every inactive doc is parked
    }
    load_active(it->second);
    parked_.erase(it);
    active_idx_ = idx;
}

void GeometryEngine::open_into_new_tab(const Command& command) {
    const auto* open = std::get_if<OpenDocumentCommand>(&command);
    if (open == nullptr) {
        return;
    }
    io::Document doc;
    const io::IoResult r =
        open->dxf ? io::load_dxf(open->path, doc) : io::load_native(open->path, doc);
    if (!r.ok) {
        report(r.message); // load failed -> no tab created, nothing disturbed
        return;
    }
    // Park the current active, make a fresh active document, populate it from the file.
    DocState parked;
    park_active(parked);
    parked_[doc_metas_[active_idx_].id] = std::move(parked);
    reset_active_state();
    io::populate_store(store_, doc);
    for (const EntityHandle h : all_live()) {
        Vec2 lo;
        Vec2 hi;
        if (entity_aabb(store_, h, lo, hi)) {
            grid_.insert(h, lo, hi);
        }
    }
    geom_dirty_ = true;
    dirty_ = false;
    ++document_version_;
    DocMeta m;
    m.id = next_doc_id_++;
    m.name = open->name.empty() ? doc_basename(open->path) : open->name;
    m.path = open->dxf ? std::string{} : open->path; // DXF import has no native path
    m.is_dxf_path = open->dxf;
    doc_metas_.push_back(std::move(m));
    active_idx_ = doc_metas_.size() - 1;
    report(r.message);
}

void GeometryEngine::close_document(std::uint64_t id) {
    const std::size_t idx = doc_index(id);
    if (idx >= doc_metas_.size()) {
        return;
    }
    if (doc_metas_.size() == 1) {
        // Never zero tabs: reset the sole document to a fresh empty drawing.
        reset_active_state();
        ++document_version_;
        doc_metas_[0].name = "Drawing" + std::to_string(++doc_name_counter_);
        doc_metas_[0].path.clear();
        doc_metas_[0].is_dxf_path = false;
        active_idx_ = 0;
        report("New drawing.");
        return;
    }
    if (idx == active_idx_) {
        // Activate a neighbour (next, else previous), then drop the closing doc.
        const std::size_t nb = (idx + 1 < doc_metas_.size()) ? idx + 1 : idx - 1;
        const std::uint64_t nb_id = doc_metas_[nb].id;
        auto it = parked_.find(nb_id);
        if (it != parked_.end()) {
            load_active(it->second); // the closing doc's live state is discarded
            parked_.erase(it);
        }
        doc_metas_.erase(doc_metas_.begin() + static_cast<std::ptrdiff_t>(idx));
        active_idx_ = doc_index(nb_id);
    } else {
        const std::uint64_t active_id = doc_metas_[active_idx_].id;
        parked_.erase(id);
        doc_metas_.erase(doc_metas_.begin() + static_cast<std::ptrdiff_t>(idx));
        active_idx_ = doc_index(active_id);
    }
    report("Drawing closed.");
}

void GeometryEngine::rebuild_and_publish() {
    if (geom_dirty_) {
        // Curves tessellate to the current zoom bucket's chord tolerance (Part A);
        // stored geometry stays parametric -- only this render payload is sampled.
        text::set_field_context(field_context_now());
        build_render_snapshot(store_, kernel_, geom_cache_, tess_tolerance_, store_.ltscale());
        geom_dirty_ = false;
        ++geom_version_;
        ++geom_cache_id_; // never reset, so a slot stamp cannot collide across documents
        ++edit_serial_;
    }
    prune_selection();

    RenderSnapshot& buf = snapshots_.write_buffer();
    // The scene arrays are copied into a slot only when that slot does not already hold
    // this build. After three publishes every slot has it, and from then on a cursor move
    // costs nothing proportional to the drawing.
    if (buf.copied_geometry_id != geom_cache_id_) {
        buf.points = geom_cache_.points;
        buf.line_vertices = geom_cache_.line_vertices;
        buf.construction_lines = geom_cache_.construction_lines;
        buf.wipeout_vertices = geom_cache_.wipeout_vertices;
        buf.wipeout_frames = geom_cache_.wipeout_frames;
        buf.line_batches = geom_cache_.line_batches;
        buf.point_batches = geom_cache_.point_batches;
        buf.fill_vertices = geom_cache_.fill_vertices;
        buf.fill_batches = geom_cache_.fill_batches;
        buf.text_edit_targets = geom_cache_.text_edit_targets; // double-click-to-edit
        buf.bounds_min = geom_cache_.bounds_min;
        buf.bounds_max = geom_cache_.bounds_max;
        buf.has_bounds = geom_cache_.has_bounds;
        buf.copied_geometry_id = geom_cache_id_;
    }
    // Layer table + current layer are cheap and may change without a geometry
    // rebuild (e.g. SetCurrentLayer), so publish them fresh from the store.
    buf.layers.assign(store_.layers().begin(), store_.layers().end());
    buf.current_layer = store_.current_layer();
    // Dimension styles for the UI placement preview (cheap; few entries).
    buf.dimstyles.assign(store_.dimstyles().begin(), store_.dimstyles().end());
    buf.named_views = store_.named_views(); // VIEW table (Restore / ?)
    buf.units = store_.units();
    buf.text_styles = store_.text_styles();
    buf.current_text_style = store_.current_text_style();
    buf.group_names.clear();
    for (const EntityGroup& g : store_.groups()) {
        buf.group_names.push_back(g.name); // GROUP names (feedback / ?)
    }
    buf.block_names.clear();
    for (std::uint16_t bi = 0; bi < static_cast<std::uint16_t>(store_.block_count()); ++bi) {
        buf.block_names.push_back(store_.block(bi)->name); // INSERT ? / prompt default
    }
    buf.block_attdefs.clear();
    for (std::uint16_t bi = 0; bi < static_cast<std::uint16_t>(store_.block_count()); ++bi) {
        std::vector<BlockAttDefInfo> infos;
        for (const BlockAttDef& a : store_.block(bi)->content.attdefs) {
            infos.push_back(BlockAttDefInfo{a.tag, a.prompt, a.def, a.flags});
        }
        buf.block_attdefs.push_back(std::move(infos)); // INSERT's attribute prompts
    }

    // Pending object-dimension def points for the placement preview (Part C).
    buf.has_pending_dim = has_pending_dim_;
    buf.pending_dim_a = pending_dim_.a;
    buf.pending_dim_b = pending_dim_.b;
    buf.pending_dim_line_pt = pending_dim_.line_pt;
    buf.pending_dim_aux = pending_dim_.aux;
    buf.pending_dim_type = static_cast<std::uint8_t>(pending_dim_.type);
    buf.pending_dim_version = pending_dim_version_;

    // Publish the selection set (queryable API) and its segments (highlight/ghost).
    //
    // The highlight tessellation and the PR property summary depend only on WHICH
    // entities are selected and on the drawing's edit state -- not on the cursor. They
    // are rebuilt only when either changes, and a slot that already holds the current
    // build keeps it: selecting everything in a large drawing and then moving the mouse
    // must not re-tessellate the whole selection per move.
    if (!(sel_cache_valid_ && sel_cache_serial_ == edit_serial_ &&
          sel_cache_handles_ == selection_)) {
        std::vector<Command> captured;
        captured.reserve(selection_.size());
        for (const EntityHandle h : selection_) {
            if (store_.is_valid(h)) {
                captured.push_back(capture_entity(h));
            }
        }
        sel_cache_summary_ = summarize_selection(captured);
        sel_cache_lines_.clear();
        sel_cache_fills_.clear();
        std::vector<Vec2> tess;
        std::vector<Vec2> htris;
        for (const EntityHandle h : selection_) {
            kernel_.tessellate(store_, h, kDefaultTessTolerance, tess);
            if (h.kind == EntityKind::Insert) { // disjoint pairs: no phantom connectors
                for (std::size_t t = 0; t + 1 < tess.size(); t += 2) {
                    sel_cache_lines_.push_back(tess[t]);
                    sel_cache_lines_.push_back(tess[t + 1]);
                }
            } else {
                for (std::size_t t = 1; t < tess.size(); ++t) {
                    sel_cache_lines_.push_back(tess[t - 1]);
                    sel_cache_lines_.push_back(tess[t]);
                }
            }
            // A selected SOLID hatch also surfaces its fill triangles so the highlight
            // tints the whole filled region (the fill stays in the scene fill_* channels;
            // this is a derived overlay drawn render-side in the selection colour).
            if (h.kind == EntityKind::Hatch) {
                const HatchData* hd = store_.hatch(h);
                if (hd != nullptr && store_.string_of(*hd) == "SOLID") {
                    htris.clear();
                    hatch::triangulate_filled(store_.hatch_loops(*hd), htris);
                    sel_cache_fills_.insert(sel_cache_fills_.end(), htris.begin(), htris.end());
                }
            }
        }
        sel_cache_handles_ = selection_;
        sel_cache_serial_ = edit_serial_;
        sel_cache_valid_ = true;
        ++sel_cache_build_;
    }
    if (buf.copied_selection_build != sel_cache_build_) {
        buf.selection = selection_;
        buf.selection_summary = sel_cache_summary_;
        buf.selected_line_vertices = sel_cache_lines_;
        buf.selected_fill_vertices = sel_cache_fills_;
        buf.copied_selection_build = sel_cache_build_;
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
        build_render_snapshot(grip_preview_store_, kernel_, tmp, tess_tolerance_, store_.ltscale());
        buf.grip_preview_segments = std::move(tmp.line_vertices);
        buf.grip_preview_fills = std::move(tmp.fill_vertices);
    } else if (stretch_preview_active_) {
        // Live STRETCH: the whole selection, stretched by the current cursor delta, on
        // the same scratch store and through the same publish channel as a grip drag --
        // so the renderer needs nothing new. Built by stretched_commands(), i.e. by the
        // code the commit will run, so the band shows exactly what the click will do.
        const std::vector<StretchEdit> edits = stretched_commands(stretch_preview_delta_);
        if (!edits.empty()) {
            grip_preview_store_.clear();
            grip_preview_store_.set_layer_table(store_.layers(), store_.current_layer());
            grip_preview_store_.set_dimstyle_table(store_.dimstyles());
            for (const StretchEdit& e : edits) {
                const EntityProps* ep = store_.props(e.handle);
                add_command_to_store(grip_preview_store_, e.edited,
                                     ep != nullptr ? *ep : EntityProps{store_.current_layer()});
            }
            RenderSnapshot tmp;
            build_render_snapshot(grip_preview_store_, kernel_, tmp, tess_tolerance_,
                                  store_.ltscale());
            buf.grip_preview_segments = std::move(tmp.line_vertices);
            buf.grip_preview_fills = std::move(tmp.fill_vertices);
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
            if (hv.kind == EntityKind::Insert) { // disjoint pairs: no phantom connectors
                for (std::size_t s = 0; s + 1 < tess.size(); s += 2) {
                    buf.hover_line_vertices.push_back(tess[s]);
                    buf.hover_line_vertices.push_back(tess[s + 1]);
                }
            } else {
                for (std::size_t s = 1; s < tess.size(); ++s) {
                    buf.hover_line_vertices.push_back(tess[s - 1]);
                    buf.hover_line_vertices.push_back(tess[s]);
                }
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

    // Multi-document tab strip: every open document (active doc's dirty is the live flag;
    // inactive docs report their parked dirty). The UI renders the tabs purely from this.
    buf.documents.clear();
    buf.documents.reserve(doc_metas_.size());
    for (std::size_t i = 0; i < doc_metas_.size(); ++i) {
        DocumentInfo di;
        di.id = doc_metas_[i].id;
        di.name = doc_metas_[i].name;
        di.path = doc_metas_[i].path;
        if (i == active_idx_) {
            di.dirty = dirty_;
        } else {
            const auto it = parked_.find(doc_metas_[i].id);
            di.dirty = it != parked_.end() && it->second.dirty;
        }
        buf.documents.push_back(std::move(di));
    }
    buf.active_document_id =
        active_idx_ < doc_metas_.size() ? doc_metas_[active_idx_].id : 0;

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

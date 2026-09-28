// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Object snaps (issue #66): Extension and Geometric Center, the snaps typed for one pick
// at a point prompt (END, MID, ... NON), the point filters FROM / M2P / TT / TK, -OSNAP's
// default and OSMODE.

#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/snap_keywords.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/osnap.hpp"
#include "musacad/core/spatial_grid.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {
constexpr double kPiT = 3.14159265358979323846;

struct Scene {
    GeometryStore store;
    NativeKernel2D kernel;
    SpatialGrid grid{16.0};
    std::vector<ExtensionPath> paths;

    void index(EntityHandle h) {
        Vec2 lo;
        Vec2 hi;
        entity_aabb(store, h, lo, hi);
        grid.insert(h, lo, hi);
    }
    void line(Vec2 a, Vec2 b) { index(store.add_line(a, b)); }
    void arc(Vec2 c, double r, double a0, double a1) { index(store.add_arc(c, r, a0, a1)); }
    void poly(const std::vector<Vec2>& v, const std::vector<double>& b, bool closed) {
        index(store.add_polyline(v, b, closed));
    }
    /// The cursor passes over an end: it is acquired.
    bool acquire(Vec2 cursor, double r = 0.5) {
        const auto p = extension_path_at(store, grid, cursor, r);
        if (p) {
            paths.push_back(*p);
        }
        return p.has_value();
    }
    SnapResult snap(Vec2 cursor, std::uint32_t mask = kAllSnaps, double r = 0.5) {
        return compute_snap(store, kernel, grid, cursor, r, mask, std::nullopt, paths);
    }
};

template <class Pred>
bool wait_until(GeometryEngine& e, Pred pred) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        e.consume_snapshot();
        if (pred(e.snapshot())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

struct Out : musacad::command::CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
    [[nodiscard]] bool any_contains(const std::string& sub) const {
        for (const auto& l : lines) {
            if (l.find(sub) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};
struct View : musacad::command::ViewControl {
    std::uint32_t mask = kDefaultRunningSnaps;
    int changed = 0;
    void zoom_extents() override {}
    void zoom_scale(double) override {}
    [[nodiscard]] std::uint32_t snap_mask() const override { return mask; }
    void set_snap_mask(std::uint32_t m) override { mask = m; }
    void snap_override_changed() override { ++changed; }
};
struct H {
    std::vector<Command> cmds;
    Out out;
    View view;
    musacad::command::CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, &view, out};
    template <class T>
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
    template <class T>
    [[nodiscard]] int count() const {
        int n = 0;
        for (const Command& c : cmds) {
            n += std::holds_alternative<T>(c) ? 1 : 0;
        }
        return n;
    }
    void run(std::initializer_list<const char*> lines_in) {
        for (const char* l : lines_in) {
            proc.submit_line(l);
        }
    }
};
constexpr const char* kNext = "Specify next point or [Undo]: ";
} // namespace

TEST_CASE("#66 Extension: the line carried on past an acquired end, never back over the object") {
    Scene s;
    s.line({0, 0}, {10, 0});
    REQUIRE(!s.acquire({5, 0.1}));    // the middle of the line is no end
    REQUIRE(s.acquire({10.1, 0.1}));
    REQUIRE(s.paths.size() == 1);
    REQUIRE(s.paths[0].from == Vec2{10, 0});
    REQUIRE(s.paths[0].dir.x == Approx(1.0));

    const SnapResult r = s.snap({15.0, 0.2});
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Extension);
    REQUIRE(r.point.x == Approx(15.0));
    REQUIRE(r.point.y == Approx(0.0).margin(1e-12));
    REQUIRE(r.has_path);
    REQUIRE(r.path_from == Vec2{10, 0});

    // Too far from the path, or the mode off, or nothing acquired: no such snap.
    REQUIRE(!s.snap({15.0, 2.0}).found);
    REQUIRE(!s.snap({15.0, 0.2}, kAllSnaps & ~snap_bit(SnapType::Extension)).found);
    // Over the line itself the foot is behind the end: Nearest is what is there.
    const SnapResult on = s.snap({5.0, 0.2});
    REQUIRE(on.found);
    REQUIRE(on.type != SnapType::Extension);
    Scene none;
    none.line({0, 0}, {10, 0});
    REQUIRE(!none.snap({15.0, 0.2}).found);
}

TEST_CASE("#66 Extension: round an arc's circle, where two paths cross, and its rank") {
    Scene s;
    s.arc({0, 0}, 5.0, 0.0, kPiT / 2.0);
    REQUIRE(s.acquire({0.1, 5.1}));
    REQUIRE(s.paths[0].arc);
    const double a = 3.0 * kPiT / 4.0;
    const SnapResult r = s.snap({5.2 * std::cos(a), 5.2 * std::sin(a)});
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Extension);
    REQUIRE(std::hypot(r.point.x, r.point.y) == Approx(5.0));
    REQUIRE(r.point.x == Approx(5.0 * std::cos(a)));

    // Two acquired lines: their crossing, exactly.
    Scene x;
    x.line({0, 0}, {10, 0});
    x.line({20, -10}, {20, -2});
    REQUIRE(x.acquire({10, 0.1}));
    REQUIRE(x.acquire({20.1, -2}));
    const SnapResult c = x.snap({19.8, 0.15});
    REQUIRE(c.found);
    REQUIRE(c.type == SnapType::Extension);
    REQUIRE(c.point.x == Approx(20.0));
    REQUIRE(c.point.y == Approx(0.0).margin(1e-12));

    // A snap to an object out-ranks a point on no object; Nearest stays last.
    Scene k;
    k.line({0, 0}, {10, 0});
    k.line({15, 0.1}, {15, 8});
    REQUIRE(k.acquire({10, 0}));
    const SnapResult e = k.snap({15.0, 0.2});
    REQUIRE(e.type == SnapType::Endpoint);
    const SnapResult n = k.snap({15.2, 0.0}, snap_bit(SnapType::Nearest) | snap_bit(SnapType::Extension));
    REQUIRE(n.type == SnapType::Extension);
    REQUIRE(snap_rank(SnapType::Extension) < snap_rank(SnapType::Nearest));
    REQUIRE(snap_rank(SnapType::ApparentIntersection) < snap_rank(SnapType::Extension));
}

TEST_CASE("#66 an open polyline's ends extend; its arc segments have a true midpoint and a centre") {
    Scene s;
    s.poly({{0, 0}, {10, 0}, {10, 10}}, {}, false);
    REQUIRE(s.acquire({10, 10.1}));
    REQUIRE(s.paths[0].dir.y == Approx(1.0));
    const SnapResult r = s.snap({10.2, 14.0});
    REQUIRE(r.type == SnapType::Extension);
    REQUIRE(r.point == Vec2{10, 14});

    Scene b;
    b.poly({{0, 0}, {10, 0}}, {1.0, 0.0}, false); // a half circle below the chord
    const SnapResult mid = b.snap({5.1, -4.9}, snap_bit(SnapType::Midpoint));
    REQUIRE(mid.found);
    REQUIRE(mid.point.x == Approx(5.0));
    REQUIRE(mid.point.y == Approx(-5.0));
    Scene c;
    c.poly({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {}, true);
    const SnapResult g = c.snap({5.1, 5.1}, snap_bit(SnapType::GeometricCenter));
    REQUIRE(g.found);
    REQUIRE(g.type == SnapType::GeometricCenter);
    REQUIRE(g.point == Vec2{5, 5});
}

TEST_CASE("#66 the engine acquires ends as the cursor passes and lets them go at a pick") {
    GeometryEngine e;
    e.start();
    e.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(e, [](const auto& s) { return !s.line_vertices.empty(); }));
    const std::uint32_t mask = snap_bit(SnapType::Endpoint) | snap_bit(SnapType::Extension);
    e.submit(SetCursorCommand{{10.1, 0.1}, 0.5, true, mask, {}, false});
    REQUIRE(wait_until(e, [](const auto& s) {
        return s.has_snap && s.snap_type == SnapType::Endpoint && s.snap_acquired.size() == 1;
    }));
    e.submit(SetCursorCommand{{15.0, 0.2}, 0.5, true, mask, {}, false});
    REQUIRE(wait_until(e, [](const auto& s) {
        return s.has_snap && s.snap_type == SnapType::Extension && s.snap_has_path;
    }));
    REQUIRE(e.snapshot().snap_point.x == Approx(15.0));
    REQUIRE(e.snapshot().snap_point.y == Approx(0.0).margin(1e-12));
    REQUIRE(e.snapshot().snap_path_from == Vec2{10, 0});
    // A point was picked (the command has a previous point now): a fresh start.
    e.submit(SetCursorCommand{{15.0, 0.2}, 0.5, true, mask, {15, 0}, true});
    REQUIRE(wait_until(e, [](const auto& s) { return !s.has_snap && s.snap_acquired.empty(); }));
    e.stop();
}

TEST_CASE("#66 the snap keywords: AutoCAD's spellings, lists, NON, OSMODE") {
    using namespace musacad::command;
    std::uint32_t m = 0;
    std::string label;
    std::string joiner;
    REQUIRE(parse_snap_list("END,MID", m, &label, &joiner));
    REQUIRE(m == (snap_bit(SnapType::Endpoint) | snap_bit(SnapType::Midpoint)));
    REQUIRE(label == "Endpoint, Midpoint");
    REQUIRE(joiner == "of");
    REQUIRE(parse_snap_list("endp", m));
    REQUIRE(m == snap_bit(SnapType::Endpoint));
    REQUIRE(parse_snap_list("PERP", m, &label, &joiner));
    REQUIRE(m == snap_bit(SnapType::Perpendicular));
    REQUIRE(joiner == "to");
    REQUIRE(parse_snap_list("gcen", m));
    REQUIRE(m == snap_bit(SnapType::GeometricCenter));
    REQUIRE(parse_snap_list("cen", m));
    REQUIRE(m == snap_bit(SnapType::Center));
    REQUIRE(parse_snap_list("EXT int", m));
    REQUIRE(m == (snap_bit(SnapType::Extension) | snap_bit(SnapType::Intersection)));
    REQUIRE(parse_snap_list("app", m));
    REQUIRE(m == snap_bit(SnapType::ApparentIntersection));
    m = 7;
    REQUIRE(parse_snap_list("NON", m));
    REQUIRE(m == 0);
    std::string unknown;
    REQUIRE(!parse_snap_list("END,FOO", m, nullptr, nullptr, &unknown));
    REQUIRE(unknown == "FOO");
    REQUIRE(!parse_snap_list("EN", m));
    REQUIRE(!parse_snap_list("", m));
    REQUIRE(!parse_snap_list("10,20", m));
    REQUIRE(!parse_snap_list("C", m));

    REQUIRE(osmode_of(kDefaultRunningSnaps) == 4133);
    REQUIRE(mask_of_osmode(4133) == kDefaultRunningSnaps);
    REQUIRE(mask_of_osmode(3) == (snap_bit(SnapType::Endpoint) | snap_bit(SnapType::Midpoint)));
    REQUIRE((kDefaultRunningSnaps & snap_bit(SnapType::Parallel)) == 0);
    REQUIRE(snap_codes(kDefaultRunningSnaps) == "End,Cen,Int,Ext");
    REQUIRE(snap_codes(0) == "None");
    REQUIRE(snap_list(snap_bit(SnapType::GeometricCenter)) == "Geometric Center");
}

TEST_CASE("#66 a snap typed at a point prompt holds for one pick") {
    H h;
    h.run({"L", "0,0"});
    REQUIRE(h.out.prompt == kNext);
    h.proc.submit_line("END");
    REQUIRE(h.out.prompt == std::string(kNext) + "_end of ");
    REQUIRE(h.proc.snap_override().has_value());
    REQUIRE(*h.proc.snap_override() == snap_bit(SnapType::Endpoint));
    REQUIRE(h.view.changed == 1);

    // Nothing of the kind under the cursor: an invalid pick, and the prompt asks again.
    h.proc.pick_point({5, 5}, std::nullopt);
    REQUIRE(h.out.any_contains("No Endpoint found for specified point."));
    REQUIRE(h.out.any_contains("Invalid point."));
    REQUIRE(h.out.prompt == kNext);
    REQUIRE(!h.proc.snap_override().has_value());
    REQUIRE(h.count<AddLineCommand>() == 0);

    // Found: the snap point is the pick.
    h.proc.submit_line("per");
    REQUIRE(h.out.prompt == std::string(kNext) + "_per to ");
    h.proc.pick_point({5, 5}, Vec2{7, 7});
    REQUIRE(h.count<AddLineCommand>() == 1);
    REQUIRE(h.last<AddLineCommand>()->b == Vec2{7, 7});
    REQUIRE(!h.proc.snap_override().has_value());

    // NON: the running snap under the cursor is passed over for this pick.
    h.proc.submit_line("non");
    REQUIRE(*h.proc.snap_override() == 0u);
    h.proc.pick_point({3, 3}, Vec2{9, 9});
    REQUIRE(h.last<AddLineCommand>()->b == Vec2{3, 3});
    // ... and only this one.
    h.proc.pick_point({4, 4}, Vec2{9, 9});
    REQUIRE(h.last<AddLineCommand>()->b == Vec2{9, 9});
}

TEST_CASE("#66 a typed snap is given up by a typed point, by Enter, by another snap, by Esc") {
    H h;
    h.run({"L", "0,0", "MID", "4,4"});
    REQUIRE(!h.proc.snap_override().has_value());
    REQUIRE(h.last<AddLineCommand>()->b == Vec2{4, 4});

    h.proc.submit_line("CEN");
    REQUIRE(h.proc.snap_override().has_value());
    h.proc.submit_line(""); // Enter: the prompt as it was, the command still running
    REQUIRE(!h.proc.snap_override().has_value());
    REQUIRE(h.proc.has_active_command());
    REQUIRE(h.out.prompt == kNext);

    h.run({"QUA", "TAN"});
    REQUIRE(*h.proc.snap_override() == snap_bit(SnapType::Tangent));
    REQUIRE(h.out.prompt == std::string(kNext) + "_tan to ");
    h.proc.cancel();
    REQUIRE(!h.proc.snap_override().has_value());
    REQUIRE(!h.proc.has_active_command());

    // A command's own option wins over a snap of the same spelling: PLINE's arc mode
    // reads CE as CEnter, and a keyword that is no snap is left to the command.
    H p;
    p.run({"PL", "0,0", "10,0", "A", "CE"});
    REQUIRE(p.out.prompt == "Specify center point of arc: ");
    REQUIRE(!p.proc.snap_override().has_value());
    p.proc.cancel();
}

TEST_CASE("#66 FROM: a base point and an offset from it") {
    H h;
    h.run({"L", "FROM"});
    REQUIRE(h.out.prompt == "Specify first point: _from Base point: ");
    REQUIRE(h.proc.point_filter_active());
    h.proc.submit_line("10,10");
    REQUIRE(h.out.prompt == "Specify first point: _from Base point: <Offset>: ");
    h.proc.submit_line("@5,0");
    REQUIRE(!h.proc.point_filter_active());
    REQUIRE(h.out.prompt == kNext);
    h.run({"@0,5", ""});
    REQUIRE(h.last<AddLineCommand>()->a == Vec2{15, 10});
    REQUIRE(h.last<AddLineCommand>()->b == Vec2{15, 15});

    // The base point by a snap typed for it, the offset as a polar entry.
    H s;
    s.run({"C", "from", "end"});
    REQUIRE(s.out.prompt.find("_from Base point: _end of ") != std::string::npos);
    s.proc.pick_point({0.2, 0.1}, Vec2{0, 0});
    REQUIRE(s.out.prompt.find("<Offset>: ") != std::string::npos);
    s.run({"@10<90", "2"});
    const auto* c = s.last<AddCircleCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->center.x == Approx(0.0).margin(1e-9));
    REQUIRE(c->center.y == Approx(10.0));
    REQUIRE(c->radius == Approx(2.0));

    // Enter gives the filter up; the command goes on asking.
    H e;
    e.run({"L", "0,0", "FROM", ""});
    REQUIRE(!e.proc.point_filter_active());
    REQUIRE(e.proc.has_active_command());
    REQUIRE(e.out.prompt == kNext);
    // ... and relative input is from the command's own last point again.
    e.run({"@3,0", ""});
    REQUIRE(e.last<AddLineCommand>()->b == Vec2{3, 0});
}

TEST_CASE("#66 M2P: the middle of two points") {
    H h;
    h.run({"L", "0,0", "M2P"});
    REQUIRE(h.out.prompt == std::string(kNext) + "_m2p First point of mid: ");
    h.proc.submit_line("10,0");
    REQUIRE(h.out.prompt == std::string(kNext) + "_m2p First point of mid: Second point of mid: ");
    h.proc.pick_point({10.1, 9.9}, Vec2{10, 10});
    REQUIRE(h.last<AddLineCommand>()->b == Vec2{10, 5});
    h.run({"MTP", "0,0", "0,20"});
    REQUIRE(h.last<AddLineCommand>()->b == Vec2{0, 10});
}

TEST_CASE("#66 TK: a chain of orthogonal moves, Enter takes the point reached") {
    H h;
    h.run({"L", "TK"});
    REQUIRE(h.out.prompt == "Specify first point: _tk First tracking point: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompt == "Specify first point: _tk Next point (Press ENTER to end tracking): ");
    // The cursor is held to one axis from the tracking point while it lasts.
    const Vec2 held = h.proc.resolve_pick({5, 1}, std::nullopt);
    REQUIRE(held == Vec2{5, 0});
    h.run({"5,1", "6,7", ""});
    REQUIRE(!h.proc.point_filter_active());
    REQUIRE(h.out.prompt == kNext);
    h.run({"@1,0", ""});
    REQUIRE(h.last<AddLineCommand>()->a == Vec2{5, 7});
    REQUIRE(h.last<AddLineCommand>()->b == Vec2{6, 7});
}

TEST_CASE("#66 TT: the cursor locks onto the paths through a temporary tracking point") {
    H h;
    h.proc.set_pick_radius(0.5);
    h.run({"L", "0,0", "TT"});
    REQUIRE(h.out.prompt == std::string(kNext) + "_tt Specify temporary OTRACK point: ");
    h.proc.submit_line("5,5");
    REQUIRE(h.out.prompt == kNext);
    REQUIRE(h.proc.track_points().size() == 1);
    REQUIRE(h.count<AddLineCommand>() == 0); // the tracking point is no point of the line

    REQUIRE(h.proc.resolve_pick({9.0, 5.2}, std::nullopt) == Vec2{9, 5});
    REQUIRE(h.proc.resolve_pick({5.3, 9.0}, std::nullopt) == Vec2{5, 9});
    REQUIRE(h.proc.resolve_pick({9.0, 7.0}, std::nullopt) == Vec2{9, 7}); // out of reach of both
    // An object snap still wins.
    REQUIRE(h.proc.resolve_pick({9.0, 5.2}, Vec2{1, 1}) == Vec2{1, 1});
    // With ORTHO the cursor runs along the axis from the last point and stops where
    // that crosses the tracking path.
    h.proc.set_ortho(true);
    REQUIRE(h.proc.resolve_pick({5.2, 1.0}, std::nullopt) == Vec2{5, 0});
    REQUIRE(h.proc.resolve_pick({9.0, 5.2}, std::nullopt) == Vec2{9, 0});
    h.proc.set_ortho(false);

    // The point given, the tracking point has served.
    h.proc.pick_point({9.0, 5.2}, std::nullopt);
    REQUIRE(h.last<AddLineCommand>()->b == Vec2{9, 5});
    REQUIRE(h.proc.track_points().empty());
}

TEST_CASE("#66 -OSNAP offers the modes in force as its default; OSMODE is their bit sum") {
    H h;
    h.proc.submit_line("-OSNAP");
    REQUIRE(h.out.prompt == "Enter list of object snap modes <End,Cen,Int,Ext>: ");
    h.proc.submit_line("");
    REQUIRE(h.view.mask == kDefaultRunningSnaps);
    REQUIRE(!h.proc.has_active_command());

    h.run({"-OSNAP", "END,FOO"});
    REQUIRE(h.out.any_contains("Invalid object snap mode \"FOO\""));
    REQUIRE(h.proc.has_active_command());
    h.proc.submit_line("end,mid,gcen,ext");
    REQUIRE(h.view.mask == (snap_bit(SnapType::Endpoint) | snap_bit(SnapType::Midpoint) |
                            snap_bit(SnapType::GeometricCenter) | snap_bit(SnapType::Extension)));
    h.run({"-OSNAP", "NONE"});
    REQUIRE(h.view.mask == 0u);

    h.view.mask = kDefaultRunningSnaps;
    h.proc.submit_line("OSMODE");
    REQUIRE(h.out.prompt == "Enter new value for OSMODE <4133>: ");
    h.proc.submit_line("1.5");
    REQUIRE(h.proc.has_active_command());
    h.proc.submit_line("3");
    REQUIRE(h.view.mask == (snap_bit(SnapType::Endpoint) | snap_bit(SnapType::Midpoint)));
    REQUIRE(!h.proc.has_active_command());
}

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Polar tracking and object snap tracking (issue #64): the polar angles, a cursor that
// locks onto a path within the aperture, PolarSnap, acquired points, the crossing of two
// paths, the temporary override keys and the system variables.

#include <cmath>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/tracking.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/snap.hpp"

using namespace musacad::core;
using namespace musacad::command;
using Catch::Approx;

namespace {
constexpr double kDeg = 3.14159265358979323846 / 180.0;

struct Out : CommandOutput {
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
struct View : ViewControl {
    int polar_calls = 0;
    int otrack_calls = 0;
    bool polar = false;
    bool otrack = false;
    void zoom_extents() override {}
    void zoom_scale(double) override {}
    void set_polar_mode(bool on) override {
        polar = on;
        ++polar_calls;
    }
    void set_otrack_mode(bool on) override {
        otrack = on;
        ++otrack_calls;
    }
};
struct H {
    std::vector<Command> cmds;
    Out out;
    View view;
    CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, &view, out};
    template <class T>
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
    void run(std::initializer_list<const char*> lines_in) {
        for (const char* l : lines_in) {
            proc.submit_line(l);
        }
    }
};
bool near(Vec2 p, Vec2 q, double eps = 1e-9) {
    return std::abs(p.x - q.x) < eps && std::abs(p.y - q.y) < eps;
}
} // namespace

TEST_CASE("#64 the polar angles: the increment, the additional ones, relative to the last segment") {
    TrackingSettings s;
    std::vector<double> a = polar_angles(s, 0.0);
    REQUIRE(a.size() == 4); // 90 degrees to begin with, as AutoCAD's POLARANG
    REQUIRE(a[1] == Approx(90.0 * kDeg));
    REQUIRE(a[3] == Approx(270.0 * kDeg));

    s.polar_increment = 45.0 * kDeg;
    REQUIRE(polar_angles(s, 0.0).size() == 8);
    s.polar_increment = 22.5 * kDeg;
    REQUIRE(polar_angles(s, 0.0).size() == 16);

    // Additional angles count only while POLARMODE's bit 4 is set; they are single
    // directions, not increments.
    s.polar_increment = 90.0 * kDeg;
    s.additional = {20.0 * kDeg};
    REQUIRE(polar_angles(s, 0.0).size() == 4);
    s.set_polarmode(4);
    a = polar_angles(s, 0.0);
    REQUIRE(a.size() == 5);
    REQUIRE(a[1] == Approx(20.0 * kDeg));
    REQUIRE(s.polarmode() == 4);

    // Relative: measured from the direction of the segment just drawn.
    s.set_polarmode(1);
    a = polar_angles(s, 30.0 * kDeg);
    REQUIRE(a.size() == 4);
    REQUIRE(a[0] == Approx(30.0 * kDeg));
    REQUIRE(a[1] == Approx(120.0 * kDeg));
    // A base that carries an angle past the full turn wraps.
    a = polar_angles(s, 300.0 * kDeg);
    REQUIRE(a[0] == Approx(30.0 * kDeg));
}

TEST_CASE("#64 polar tracking takes the cursor only within the aperture of a path") {
    TrackingSettings s;
    s.polar_increment = 45.0 * kDeg;
    const Vec2 last{0, 0};
    TrackResult r = track(s, true, false, {10.0, 9.6}, last, 0.0, {}, 0.5);
    REQUIRE(r.hits.size() == 1);
    REQUIRE(r.hits[0].polar);
    REQUIRE(r.hits[0].angle == Approx(45.0 * kDeg));
    REQUIRE(r.point.x == Approx(9.8));
    REQUIRE(r.point.y == Approx(9.8));
    REQUIRE(r.hits[0].length == Approx(9.8 * std::sqrt(2.0)));

    // Between two paths the cursor is its own.
    r = track(s, true, false, {10.0, 5.0}, last, 0.0, {}, 0.5);
    REQUIRE(r.hits.empty());
    REQUIRE(r.point == Vec2{10, 5});
    // Every direction round the point is a path, the ones behind it too.
    r = track(s, true, false, {-7.0, 0.2}, last, 0.0, {}, 0.5);
    REQUIRE(r.hits.size() == 1);
    REQUIRE(r.hits[0].angle == Approx(180.0 * kDeg));
    REQUIRE(near(r.point, {-7, 0}));
    // Off, without a last point, or with no aperture: nothing.
    REQUIRE(track(s, false, false, {10.0, 9.6}, last, 0.0, {}, 0.5).hits.empty());
    REQUIRE(track(s, true, false, {10.0, 9.6}, std::nullopt, 0.0, {}, 0.5).hits.empty());
    REQUIRE(track(s, true, false, {10.0, 9.6}, last, 0.0, {}, 0.0).hits.empty());
    // On the last point itself there is no direction to take.
    REQUIRE(track(s, true, false, {0.1, 0.1}, last, 0.0, {}, 0.5).hits.empty());
}

TEST_CASE("#64 PolarSnap steps along the polar path by POLARDIST") {
    TrackingSettings s;
    s.polar_distance = 2.5;
    s.polar_snap = true;
    TrackResult r = track(s, true, false, {10.1, 0.2}, Vec2{0, 0}, 0.0, {}, 0.5);
    REQUIRE(r.hits.size() == 1);
    REQUIRE(near(r.point, {10, 0}));
    REQUIRE(r.hits[0].length == Approx(10.0));
    r = track(s, true, false, {11.4, 0.2}, Vec2{0, 0}, 0.0, {}, 0.5);
    REQUIRE(near(r.point, {12.5, 0}));
    // Never back onto the point: the first step at the least.
    r = track(s, true, false, {0.9, 0.0}, Vec2{0, 0}, 0.0, {}, 0.5);
    REQUIRE(near(r.point, {2.5, 0}));
    s.polar_snap = false;
    r = track(s, true, false, {10.1, 0.2}, Vec2{0, 0}, 0.0, {}, 0.5);
    REQUIRE(near(r.point, {10.1, 0}));
}

TEST_CASE("#64 object snap tracking: paths through the acquired points, and where two cross") {
    TrackingSettings s;
    const std::vector<TrackPoint> pts{{{5, 5}, "Endpoint"}};
    TrackResult r = track(s, false, true, {9.0, 5.2}, std::nullopt, 0.0, pts, 0.5);
    REQUIRE(r.hits.size() == 1);
    REQUIRE(!r.hits[0].polar);
    REQUIRE(r.hits[0].label == "Endpoint");
    REQUIRE(r.hits[0].angle == Approx(0.0));
    REQUIRE(r.hits[0].length == Approx(4.0));
    REQUIRE(near(r.point, {9, 5}));
    r = track(s, false, true, {1.0, 5.2}, std::nullopt, 0.0, pts, 0.5);
    REQUIRE(r.hits[0].angle == Approx(180.0 * kDeg));
    REQUIRE(near(r.point, {1, 5}));
    r = track(s, false, true, {4.8, -3.0}, std::nullopt, 0.0, pts, 0.5);
    REQUIRE(r.hits[0].angle == Approx(270.0 * kDeg));
    REQUIRE(near(r.point, {5, -3}));

    // Orthogonal paths only, unless POLARMODE's bit 2 asks for every polar angle.
    s.polar_increment = 45.0 * kDeg;
    const std::vector<TrackPoint> origin{{{0, 0}, "Center"}};
    REQUIRE(track(s, false, true, {5.1, 4.9}, std::nullopt, 0.0, origin, 0.5).hits.empty());
    s.set_polarmode(2);
    r = track(s, false, true, {5.1, 4.9}, std::nullopt, 0.0, origin, 0.5);
    REQUIRE(r.hits.size() == 1);
    REQUIRE(near(r.point, {5, 5}));

    // A polar path from the last point and a path through an acquired point: the
    // crossing, exactly.
    TrackingSettings t;
    const std::vector<TrackPoint> corner{{{8, 5}, "Midpoint"}};
    r = track(t, true, true, {8.2, 0.3}, Vec2{0, 0}, 0.0, corner, 0.5);
    REQUIRE(r.hits.size() == 2);
    REQUIRE(near(r.point, {8, 0}));
    // Two acquired points: the corner they make.
    const std::vector<TrackPoint> two{{{0, 10}, "Endpoint"}, {{10, 0}, "Endpoint"}};
    r = track(t, false, true, {9.8, 10.2}, std::nullopt, 0.0, two, 0.5);
    REQUIRE(r.hits.size() == 2);
    REQUIRE(near(r.point, {10, 10}));
}

TEST_CASE("#64 POLAR in a command: 90 degrees to begin with, POLARANG changes it, the tooltip") {
    H h;
    h.proc.set_polar(true);
    h.proc.set_pick_radius(0.5);
    h.run({"L", "0,0"});
    h.proc.pick_point({10.0, 0.3}, std::nullopt);
    REQUIRE(h.last<AddLineCommand>()->b == Vec2{10, 0});
    // Away from every path the pick is where the cursor is.
    h.proc.pick_point({14.0, 3.0}, std::nullopt);
    REQUIRE(h.last<AddLineCommand>()->b == Vec2{14, 3});
    h.proc.cancel();

    h.proc.submit_line("POLARANG");
    REQUIRE(h.out.prompt == "Enter new value for POLARANG <90>: ");
    h.proc.submit_line("0");
    REQUIRE(h.proc.has_active_command());
    h.proc.submit_line("45");
    REQUIRE(h.proc.tracking_settings().polar_increment == Approx(45.0 * kDeg));
    h.proc.submit_line("POLARANG");
    REQUIRE(h.out.prompt == "Enter new value for POLARANG <45>: ");
    h.proc.submit_line("");

    h.run({"L", "0,0"});
    const Vec2 at = h.proc.resolve_pick({10.0, 9.6}, std::nullopt);
    REQUIRE(at.x == Approx(9.8));
    REQUIRE(at.y == Approx(9.8));
    REQUIRE(h.proc.tracked().hits.size() == 1);
    REQUIRE(h.proc.tracking_tooltip().rfind("Polar: 13.8593 < 45", 0) == 0);
    // An object snap wins over tracking.
    REQUIRE(h.proc.resolve_pick({10.0, 9.6}, Vec2{1, 2}) == Vec2{1, 2});
    h.proc.cancel();
    REQUIRE(h.proc.tracked().hits.empty());
}

TEST_CASE("#64 relative polar angles are measured from the segment just drawn") {
    H h;
    h.proc.set_polar(true);
    h.proc.set_pick_radius(0.5);
    h.run({"POLARMODE", "1", "L", "0,0", "10,10"});
    // 90 degrees from a 45 degree segment: the path at 135.
    const Vec2 at = h.proc.resolve_pick({5.1, 15.0}, std::nullopt);
    REQUIRE(at.x == Approx(5.05));
    REQUIRE(at.y == Approx(14.95));
    REQUIRE(h.proc.tracking_tooltip().rfind("Relative Polar: ", 0) == 0);
    // Due east is no path now.
    REQUIRE(h.proc.resolve_pick({20.0, 10.2}, std::nullopt) == Vec2{20, 10.2});
    h.proc.cancel();
    h.run({"POLARMODE", "0"});
}

TEST_CASE("#64 a point the cursor rests on is acquired, and let go when it rests there again") {
    H h;
    h.proc.set_pick_radius(0.5);
    h.proc.set_otrack(true);
    h.run({"L"});
    h.proc.note_snap(Vec2{5, 5}, "Endpoint", 10.00);
    h.proc.note_snap(Vec2{5, 5}, "Endpoint", 10.10);
    REQUIRE(h.proc.acquired_points().empty()); // passing over is not resting
    h.proc.note_snap(Vec2{5, 5}, "Endpoint", 10.25);
    REQUIRE(h.proc.acquired_points().size() == 1);
    h.proc.note_snap(Vec2{5, 5}, "Endpoint", 10.80);
    REQUIRE(h.proc.acquired_points().size() == 1); // one rest, one acquisition

    // Before the first point there is nothing polar to track, but the acquired point has
    // its paths.
    REQUIRE(h.proc.resolve_pick({9.0, 5.2}, std::nullopt) == Vec2{9, 5});
    REQUIRE(h.proc.tracking_tooltip().rfind("Endpoint: 4.0000 < 0", 0) == 0);

    h.proc.note_snap(std::nullopt, "", 11.0);
    h.proc.note_snap(Vec2{5, 5}, "Endpoint", 11.1);
    h.proc.note_snap(Vec2{5, 5}, "Endpoint", 11.4);
    REQUIRE(h.proc.acquired_points().empty());

    // The point given, what was acquired for it has served.
    h.proc.note_snap(std::nullopt, "", 11.8); // the cursor leaves, and comes back to rest
    h.proc.note_snap(Vec2{5, 5}, "Endpoint", 12.0);
    h.proc.note_snap(Vec2{5, 5}, "Endpoint", 12.3);
    REQUIRE(h.proc.acquired_points().size() == 1);
    h.proc.pick_point({9.0, 5.2}, std::nullopt);
    REQUIRE(h.proc.acquired_points().empty());
    h.proc.cancel();

    // OTRACK off, or no command asking: nothing is acquired.
    h.proc.note_snap(Vec2{5, 5}, "Endpoint", 20.0);
    h.proc.note_snap(Vec2{5, 5}, "Endpoint", 20.5);
    REQUIRE(h.proc.acquired_points().empty());
    h.proc.set_otrack(false);
    h.run({"L"});
    h.proc.note_snap(Vec2{5, 5}, "Endpoint", 30.0);
    h.proc.note_snap(Vec2{5, 5}, "Endpoint", 30.5);
    REQUIRE(h.proc.acquired_points().empty());
    h.proc.cancel();
}

TEST_CASE("#64 the temporary override keys, while they are held") {
    using O = CommandProcessor::Override;
    H h;
    h.proc.set_pick_radius(0.5);
    h.proc.set_ortho(true);
    h.run({"L", "0,0"});
    REQUIRE(h.proc.resolve_pick({10, 3}, std::nullopt) == Vec2{10, 0});
    h.proc.set_override(O::Ortho, true); // Shift: ORTHO the other way
    REQUIRE(h.proc.resolve_pick({10, 3}, std::nullopt) == Vec2{10, 3});
    h.proc.set_override(O::Ortho, false);
    REQUIRE(h.proc.resolve_pick({10, 3}, std::nullopt) == Vec2{10, 0});
    // ... and with ORTHO off, Shift switches it on.
    h.proc.set_ortho(false);
    h.proc.set_override(O::Ortho, true);
    REQUIRE(h.proc.resolve_pick({10, 3}, std::nullopt) == Vec2{10, 0});
    h.proc.clear_overrides();

    // Shift + X: POLAR the other way.
    REQUIRE(h.proc.resolve_pick({10.0, 0.3}, std::nullopt) == Vec2{10, 0.3});
    h.proc.set_override(O::Polar, true);
    REQUIRE(h.proc.resolve_pick({10.0, 0.3}, std::nullopt) == Vec2{10, 0});
    h.proc.clear_overrides();

    // Shift + A: OSNAP the other way.
    REQUIRE(h.proc.effective_osnap(true));
    h.proc.set_override(O::Osnap, true);
    REQUIRE(!h.proc.effective_osnap(true));
    REQUIRE(h.proc.effective_osnap(false));
    h.proc.clear_overrides();

    // Shift + E / V / C: that snap alone, OSNAP on or off.
    REQUIRE(!h.proc.snap_override().has_value());
    h.proc.set_override(O::Endpoint, true);
    REQUIRE(*h.proc.snap_override() == snap_bit(SnapType::Endpoint));
    REQUIRE(h.proc.effective_osnap(false));
    h.proc.set_override(O::Endpoint, false);
    h.proc.set_override(O::Midpoint, true);
    REQUIRE(*h.proc.snap_override() == snap_bit(SnapType::Midpoint));
    h.proc.clear_overrides();
    REQUIRE(!h.proc.snap_override().has_value());
    // A snap typed for the pick is not displaced by a held key.
    h.proc.submit_line("CEN");
    h.proc.set_override(O::Endpoint, true);
    REQUIRE(*h.proc.snap_override() == snap_bit(SnapType::Center));
    h.proc.clear_overrides();
    h.proc.submit_line("");

    // Shift + D: no snapping, no tracking -- the bare cursor.
    h.proc.set_ortho(true);
    h.proc.set_override(O::DisableAll, true);
    REQUIRE(h.proc.resolve_pick({10, 3}, std::nullopt) == Vec2{10, 3});
    REQUIRE(*h.proc.snap_override() == 0u);
    REQUIRE(!h.proc.effective_osnap(true));
    h.proc.clear_overrides();
    REQUIRE(h.proc.resolve_pick({10, 3}, std::nullopt) == Vec2{10, 0});
    h.proc.cancel();
}

TEST_CASE("#64 POLARADDANG, POLARDIST, SNAPTYPE, AUTOSNAP and TEMPOVERRIDES") {
    H h;
    h.proc.submit_line("POLARADDANG");
    REQUIRE(h.out.prompt == "Enter new value for POLARADDANG, or . for none <\"\">: ");
    h.proc.submit_line("15;x");
    REQUIRE(h.proc.has_active_command());
    h.proc.submit_line("15;75");
    REQUIRE(h.proc.tracking_settings().additional.size() == 2);
    REQUIRE(h.proc.tracking_settings().additional[1] == Approx(75.0 * kDeg));
    h.proc.submit_line("POLARADDANG");
    REQUIRE(h.out.prompt == "Enter new value for POLARADDANG, or . for none <\"15;75\">: ");
    h.proc.submit_line(".");
    REQUIRE(h.proc.tracking_settings().additional.empty());

    h.run({"POLARMODE", "16"});
    REQUIRE(h.proc.has_active_command());
    h.proc.submit_line("6");
    REQUIRE(h.proc.tracking_settings().otrack_polar);
    REQUIRE(h.proc.tracking_settings().use_additional);
    REQUIRE(!h.proc.tracking_settings().relative);
    h.run({"POLARMODE", "0"});

    h.proc.submit_line("POLARDIST");
    REQUIRE(h.out.prompt == "Enter new value for POLARDIST <0.0000>: ");
    h.proc.submit_line("2.5");
    REQUIRE(h.proc.tracking_settings().polar_distance == 2.5);
    h.run({"SNAPTYPE", "1"});
    REQUIRE(h.proc.snap_type() == 1);
    // PolarSnap is the snap while SNAP is on: along the path in steps, off the grid.
    h.proc.set_polar(true);
    h.proc.set_grid_snap(true);
    h.proc.set_grid_spacing(10.0);
    h.proc.set_pick_radius(0.5);
    h.run({"L", "0,0"});
    REQUIRE(h.proc.resolve_pick({11.4, 0.2}, std::nullopt) == Vec2{12.5, 0});
    REQUIRE(h.proc.resolve_pick({11.4, 3.0}, std::nullopt) == Vec2{11.4, 3.0});
    h.proc.cancel();
    h.run({"SNAPTYPE", "0"});
    h.run({"L", "0,0"});
    REQUIRE(h.proc.resolve_pick({11.4, 3.0}, std::nullopt) == Vec2{10, 0});
    h.proc.cancel();
    h.proc.set_grid_snap(false);
    h.proc.set_polar(false);

    h.proc.submit_line("AUTOSNAP");
    REQUIRE(h.out.prompt == "Enter new value for AUTOSNAP <39>: ");
    h.proc.submit_line("63");
    REQUIRE(h.proc.polar_tracking());
    REQUIRE(h.proc.object_snap_tracking());
    REQUIRE(h.view.polar);
    REQUIRE(h.view.otrack);
    h.run({"AUTOSNAP", "39"});
    REQUIRE(!h.proc.polar_tracking());
    REQUIRE(!h.proc.object_snap_tracking());
    REQUIRE(!h.view.polar);

    h.proc.submit_line("TEMPOVERRIDES");
    REQUIRE(h.out.prompt == "Enter new value for TEMPOVERRIDES <1>: ");
    h.proc.submit_line("0");
    REQUIRE(!h.proc.temp_overrides());
    h.run({"TEMPOVERRIDES", "1"});
    REQUIRE(h.proc.temp_overrides());
}

TEST_CASE("#64 the override keys are read only while a point is asked for") {
    H h;
    REQUIRE(!h.proc.asking_for_point()); // no command running
    h.run({"L"});
    REQUIRE(h.proc.asking_for_point()); // Specify first point:
    h.run({"0,0"});
    REQUIRE(h.proc.asking_for_point()); // Specify next point or [Undo]:
    h.proc.cancel();
    h.run({"REC", "0,0"});
    REQUIRE(h.proc.asking_for_point()); // Specify other corner point or [...]
    h.run({"D"});
    REQUIRE(!h.proc.asking_for_point()); // Specify length for rectangles <...>:
    h.proc.cancel();
    h.run({"TEXT", "0,0", "", ""});
    REQUIRE(h.out.prompt.rfind("Enter text", 0) == 0);
    REQUIRE(!h.proc.asking_for_point()); // capitals are typed, not read as overrides
    h.proc.cancel();
    h.run({"POLARANG"});
    REQUIRE(!h.proc.asking_for_point());
    h.proc.cancel();
    h.run({"TEMPOVERRIDES", "0", "L"});
    REQUIRE(!h.proc.asking_for_point()); // switched off
    h.proc.cancel();
    h.run({"TEMPOVERRIDES", "1"});
}

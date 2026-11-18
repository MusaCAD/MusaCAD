// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "musacad/command/command_processor.hpp"
using namespace musacad;
using Catch::Approx;
namespace {
struct ZoomView : command::ViewControl {
    core::Vec2 center{100, 200}, min{10, 20}, max{110, 70};
    double scale = 2;
    bool bounds = true;
    int sets = 0;
    void zoom_extents() override { ++sets; }
    void zoom_scale(double f) override { scale *= f; }
    bool current_view(core::Vec2& c, double& s) override { c = center; s = scale; return true; }
    void set_view(core::Vec2 c, double s) override { center = c; scale = s; ++sets; }
    bool viewport_size(int& w, int& h) const override { w = 800; h = 600; return true; }
    bool content_bounds(core::Vec2& a, core::Vec2& b) const override { a = min; b = max; return bounds; }
    bool selection_bounds(core::Vec2& a, core::Vec2& b) override { a = min; b = max; return bounds; }
};
struct Output : command::CommandOutput {
    std::string prompt;
    void append_line(const std::string&) override {}
    void set_prompt(const std::string& s) override { prompt = s; }
};
struct Harness {
    ZoomView view;
    Output out;
    int mutations = 0;
    command::CommandProcessor proc{[this](core::Command) { ++mutations; }, &view, out};
    void zoom(const char* option) { proc.submit_line("Z"); proc.submit_line(option); }
};
}
TEST_CASE("Zoom Object frames the preselection and Previous restores the view", "[zoom]") {
    Harness h;
    h.proc.set_selection_count(2);
    h.zoom("Object");
    REQUIRE_FALSE(h.proc.has_active_command());
    REQUIRE(h.view.center == core::Vec2{60, 45});
    REQUIRE(h.view.scale == Approx(800.0 / 110));
    REQUIRE(h.mutations == 0);
    h.zoom("P");
    REQUIRE(h.view.center == core::Vec2{100, 200});
    REQUIRE(h.view.scale == 2);
}
TEST_CASE("Zoom Object collects a selection and handles empty or unbounded selections", "[zoom]") {
    Harness h;
    h.zoom("O");
    REQUIRE(h.proc.has_active_command());
    REQUIRE(h.out.prompt.find("Select objects") != std::string::npos);
    h.proc.set_selection_count(1);
    h.proc.submit_line("");
    REQUIRE_FALSE(h.proc.has_active_command());
    REQUIRE(h.view.center == core::Vec2{60, 45});
    h.proc.set_selection_count(0);
    h.zoom("O");
    h.proc.submit_line("");
    REQUIRE(h.view.sets == 1);
    h.proc.set_selection_count(1);
    h.view.bounds = false;
    h.zoom("O");
    REQUIRE(h.view.sets == 1);
}
TEST_CASE("Zoom Object handles horizontal lines, vertical lines and points", "[zoom]") {
    Harness h;
    h.proc.set_selection_count(1);
    h.view.min = {0, 0};
    h.view.max = {100, 0};
    h.zoom("O");
    REQUIRE(h.view.scale == Approx(800.0 / 110));
    h.view.max = {0, 100};
    h.zoom("O");
    REQUIRE(h.view.scale == Approx(600.0 / 110));
    h.view.min = h.view.max = {50, 70};
    h.zoom("O");
    REQUIRE(h.view.center == core::Vec2{50, 70});
    REQUIRE(h.view.scale == Approx(600.0 / 110));
}
TEST_CASE("Zoom All includes limits, outside geometry, and empty drawings", "[zoom]") {
    Harness h;
    h.proc.set_limits({0, 0}, {400, 300}, false);
    h.zoom("A");
    REQUIRE(h.view.center == core::Vec2{200, 150});
    REQUIRE(h.view.scale == Approx(800.0 / 440));
    h.view.min = {-400, -300};
    h.zoom("ALL");
    REQUIRE(h.view.center == core::Vec2{0, 0});
    REQUIRE(h.view.scale == Approx(800.0 / 880));
    h.view.bounds = false;
    h.zoom("A");
    REQUIRE(h.view.center == core::Vec2{200, 150});
    REQUIRE(h.view.scale == Approx(800.0 / 440));
    REQUIRE(h.mutations == 0);
}

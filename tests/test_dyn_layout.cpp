// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
#include <catch2/catch_test_macros.hpp>
#include "musacad/render/dyn_layout.hpp"

using namespace musacad;

TEST_CASE("DYN fields remain readable at every viewport corner and display scale", "[dyn][layout]") {
    for (const double dpr : {1.0, 1.5, 2.0}) {
        render::Camera2D camera;
        camera.set_viewport(static_cast<int>(800*dpr),static_cast<int>(600*dpr));
        for (const core::Vec2 screen : {core::Vec2{0,0}, {800*dpr,0}, {0,600*dpr}, {800*dpr,600*dpr}}) {
            render::DynLabel a;
            a.anchor=camera.screen_to_world(screen);
            a.out={1,1}; a.focused=true; a.value_width=60*dpr; a.caption_width=32*dpr;
            render::DynLabel b=a; b.focused=false; b.locked=true;
            auto ui=render::layout_dyn_labels({a,b},camera,dpr);
            REQUIRE(ui.bounds.size()==4);
            for (const auto& p:ui.bounds) {
                CHECK(p.x>=0); CHECK(p.y>=0);
                CHECK(p.x<=800*dpr); CHECK(p.y<=600*dpr);
            }
            const auto& r=ui.bounds;
            CHECK((r[1].x<=r[2].x || r[3].x<=r[0].x || r[1].y<=r[2].y || r[3].y<=r[0].y));
            CHECK_FALSE(ui.hi_fills.empty());
            CHECK_FALSE(ui.accent_lines.empty());
        }
    }
}

TEST_CASE("DYN fields avoid the cursor prompt and track the current render camera", "[dyn][layout]") {
    render::Camera2D camera;
    camera.set_viewport(800,600);
    render::DynLabel a;
    a.anchor={0,0}; a.value_width=60; a.caption_width=32;
    const auto initial=render::layout_dyn_labels({a},camera,1);
    REQUIRE(initial.bounds.size()==2);
    const auto moved=render::layout_dyn_labels({a},camera,1,initial.bounds);
    REQUIRE(moved.bounds.size()==2);
    const auto& r=moved.bounds; const auto& o=initial.bounds;
    CHECK((r[1].x<=o[0].x || o[1].x<=r[0].x || r[1].y<=o[0].y || o[1].y<=r[0].y));
    camera.pan_pixels({40,20});
    const auto panned=render::layout_dyn_labels({a},camera,1);
    CHECK(panned.bounds[0].x==initial.bounds[0].x+40);
    CHECK(panned.bounds[0].y==initial.bounds[0].y+20);
}

TEST_CASE("DYN live values highlight replacement while typed values show a caret and lock", "[dyn][layout]") {
    render::Camera2D camera;
    camera.set_viewport(800,600);
    render::DynLabel a;
    a.focused=true; a.value_width=60;
    CHECK_FALSE(render::layout_dyn_labels({a},camera,1).hi_fills.empty());
    a.editing=true; a.locked=true;
    const auto typed=render::layout_dyn_labels({a},camera,1);
    CHECK(typed.hi_fills.empty());
    CHECK(typed.accent_lines.size()>8); // focus border plus caret and padlock
    a.focused=false;
    CHECK_FALSE(render::layout_dyn_labels({a},camera,1).accent_lines.empty());
    a.locked=false;
    CHECK(render::layout_dyn_labels({a},camera,1).accent_lines.empty());
}

TEST_CASE("DYN glyph coverage survives placement without leaving its box", "[dyn][layout]") {
    render::Camera2D camera;
    camera.set_viewport(800,600);
    render::DynLabel label;
    label.value_width=30;
    label.value_glyphs={{{0,-10},0.25f},{{4,-10},0.25f},{{4,-9},0.25f}};
    const auto ui=render::layout_dyn_labels({label},camera,1.5);
    REQUIRE(ui.glyph_fills.size()==3);
    for (const auto& v:ui.glyph_fills) {
        CHECK(v.coverage==0.25f);
        CHECK(v.position.x>=ui.bounds[0].x);
        CHECK(v.position.x<=ui.bounds[1].x);
        CHECK(v.position.y>=ui.bounds[0].y);
        CHECK(v.position.y<=ui.bounds[1].y);
    }
}

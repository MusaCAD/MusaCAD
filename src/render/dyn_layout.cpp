// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
#include "musacad/render/dyn_layout.hpp"
#include <algorithm>
#include <cmath>

namespace musacad::render {
namespace {
using core::Vec2;
struct Box { double x, y, w, h; };
void quad(std::vector<Vec2>& v, const Box& b) {
    v.insert(v.end(), {{b.x,b.y}, {b.x+b.w,b.y}, {b.x+b.w,b.y+b.h},
                       {b.x,b.y}, {b.x+b.w,b.y+b.h}, {b.x,b.y+b.h}});
}
void edge(std::vector<Vec2>& v, Vec2 a, Vec2 b) { v.push_back(a); v.push_back(b); }
void outline(std::vector<Vec2>& v, const Box& b) {
    edge(v,{b.x,b.y},{b.x+b.w,b.y}); edge(v,{b.x+b.w,b.y},{b.x+b.w,b.y+b.h});
    edge(v,{b.x+b.w,b.y+b.h},{b.x,b.y+b.h}); edge(v,{b.x,b.y+b.h},{b.x,b.y});
}
bool overlaps(const Box& a, const Box& b, double gap) {
    return a.x < b.x+b.w+gap && a.x+a.w+gap > b.x &&
           a.y < b.y+b.h+gap && a.y+a.h+gap > b.y;
}
void glyphs(std::vector<CanvasGlyphVertex>& to, const std::vector<CanvasGlyphVertex>& from, double x, double y,
            const Box& box) {
    x = std::round(x);
    y = std::round(y);
    // Runs are shaped at their actual size; the UI caps long buffers before shaping.
    // Skip complete triangles outside a tiny viewport instead of drawing offscreen.
    for (std::size_t i=0; i+2<from.size(); i+=3) {
        bool inside=true;
        for (std::size_t j=0; j<3; ++j) {
            const Vec2 p{from[i+j].position.x+x, from[i+j].position.y+y};
            inside = inside && p.x>=box.x && p.x<=box.x+box.w && p.y>=box.y && p.y<=box.y+box.h;
        }
        if (inside) for (std::size_t j=0; j<3; ++j) to.push_back({{from[i+j].position.x+x,from[i+j].position.y+y},from[i+j].coverage});
    }
}
} // namespace

CanvasCommandUI layout_dyn_labels(const std::vector<DynLabel>& labels, const Camera2D& camera,
                                  double dpr, const std::vector<Vec2>& obstacles) {
    CanvasCommandUI ui;
    ui.active = !labels.empty();
    const double margin=4*dpr, gap=6*dpr;
    const double sw=camera.viewport_width(), sh=camera.viewport_height();
    std::vector<Box> occupied;
    for (std::size_t i=0; i+1<obstacles.size(); i+=2) {
        occupied.push_back({obstacles[i].x,obstacles[i].y,
                            obstacles[i+1].x-obstacles[i].x,obstacles[i+1].y-obstacles[i].y});
    }
    // Dimensions take precedence over informational tracking tooltips.
    std::vector<const DynLabel*> ordered;
    for (const auto& l:labels) if (!l.prose) ordered.push_back(&l);
    for (const auto& l:labels) if (l.prose) ordered.push_back(&l);
    for (const DynLabel* lp:ordered) {
        const DynLabel& l=*lp;
        const double caption=l.caption_width>0 ? l.caption_width+17*dpr : 0;
        const double value=std::max(l.value_width,l.prose ? 0.0 : 48*dpr);
        const double w=std::min(caption+value+(l.prose ? 16 : 34)*dpr,std::max(1.0,sw-2*margin));
        const double h=std::min((l.prose ? 26 : 30)*dpr,std::max(1.0,sh-2*margin));
        const Vec2 anchor=camera.world_to_screen(l.anchor);
        Vec2 dir=camera.world_to_screen(l.anchor+l.out)-anchor;
        const double len=core::length(dir);
        dir=len>1e-6 ? dir*(1/len) : Vec2{0,-1};
        const double push=0.5*(std::abs(dir.x)*w+std::abs(dir.y)*h)+12*dpr;
        Box box{anchor.x+dir.x*push-w/2,anchor.y+dir.y*push-h/2,w,h};
        const auto clamp=[&](Box b) {
            b.x=std::clamp(b.x,margin,std::max(margin,sw-margin-w));
            b.y=std::clamp(b.y,margin,std::max(margin,sh-margin-h));
            return b;
        };
        box=clamp(box);
        const auto clear=[&](const Box& b) {
            return std::none_of(occupied.begin(),occupied.end(),[&](const Box& other){return overlaps(b,other,gap);});
        };
        if (!clear(box)) {
            const Box start=box;
            bool found=false;
            // Search nearby rows and columns after clamping, including all four screen edges.
            for (int ring=1; ring<=static_cast<int>(occupied.size())+2 && !found; ++ring) {
                for (const Vec2 offset : {Vec2{0,-h-gap},Vec2{0,h+gap},Vec2{-w-gap,0},Vec2{w+gap,0}}) {
                    const Box candidate=clamp({start.x+offset.x*ring,start.y+offset.y*ring,w,h});
                    if (clear(candidate)) { box=candidate; found=true; break; }
                }
            }
            // There may be no room in a tiny viewport. Suppress a tracking tip before covering input.
            if (!found && l.prose) continue;
        }
        occupied.push_back(box);
        ui.bounds.insert(ui.bounds.end(),{{box.x,box.y},{box.x+w,box.y+h}});
        quad(ui.shadow_fills,{box.x+2*dpr,box.y+2*dpr,w,h});
        quad(ui.box_fills,box);
        outline(l.focused ? ui.accent_lines : ui.lines,box);
        const double vx=box.x+8*dpr+caption;
        if (l.focused && !l.editing) {
            quad(ui.hi_fills,{vx-3*dpr,box.y+5*dpr,std::min(value+6*dpr,w-caption-16*dpr),h-10*dpr});
        }
        glyphs(ui.muted_glyphs,l.caption_glyphs,box.x+8*dpr,box.y+19*dpr,box);
        glyphs(ui.glyph_fills,l.value_glyphs,vx,box.y+(l.prose ? 18 : 20)*dpr,box);
        if (caption>0) edge(ui.lines,{vx-9*dpr,box.y+7*dpr},{vx-9*dpr,box.y+h-7*dpr});
        if (l.focused && l.editing) {
            const double cx=std::min(vx+l.value_width+2*dpr,box.x+w-17*dpr);
            edge(ui.accent_lines,{cx,box.y+8*dpr},{cx,box.y+h-8*dpr});
        }
        if (l.locked) {
            // A small closed padlock: a constraint is visible even after Tab leaves the field.
            const double x=box.x+w-13*dpr,y=box.y+13*dpr;
            outline(ui.accent_lines,{x,y,7*dpr,6*dpr});
            edge(ui.accent_lines,{x+1*dpr,y},{x+1*dpr,y-4*dpr});
            edge(ui.accent_lines,{x+1*dpr,y-4*dpr},{x+6*dpr,y-4*dpr});
            edge(ui.accent_lines,{x+6*dpr,y-4*dpr},{x+6*dpr,y});
        }
    }
    return ui;
}
} // namespace musacad::render

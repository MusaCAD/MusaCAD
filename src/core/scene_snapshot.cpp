// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/scene_snapshot.hpp"
#include "musacad/core/text/text_frame.hpp"
#include "musacad/core/polyline_width.hpp"

#include <optional>
#include <limits>
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <vector>

#include "musacad/core/block_resolve.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/font_engine.hpp"
#include "musacad/core/gdt.hpp"
#include "musacad/core/image.hpp"
#include "musacad/core/table.hpp"
#include "musacad/core/hatch.hpp"
#include "musacad/core/hatch_pattern.hpp"
#include "musacad/core/linetype.hpp"
#include "musacad/core/spline_eval.hpp"
#include "musacad/core/text/mtext.hpp"
#include "musacad/core/text/stroke_font.hpp"
#include "musacad/core/text/text_codes.hpp"
#include "musacad/core/text/text_run.hpp"

#include <cmath>

namespace musacad::core {

namespace {

template <class Arena, class Fn>
void for_each_live(const Arena& arena, EntityKind kind, Fn&& fn) {
    for (std::uint32_t i = 0; i < arena.slot_count(); ++i) {
        if (arena.alive(i)) {
            fn(EntityHandle{i, arena.generations()[i], kind});
        }
    }
}

std::uint32_t pack_rgb(Rgb c) {
    return (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) | c.b;
}
Rgb unpack_rgb(std::uint32_t v) {
    return {static_cast<std::uint8_t>((v >> 16) & 0xff), static_cast<std::uint8_t>((v >> 8) & 0xff),
            static_cast<std::uint8_t>(v & 0xff)};
}
// Line batch key: height bucket (bits 33-48) | colour 24 bits (bits 9-32) | text flag (bit 8)
// | display lineweight (bits 0-7). The text flag keeps stroke-text in its own batches (so the
// renderer can give it a heavier on-screen weight, never merged with same-colour geometry);
// the height bucket (1/8-unit quanta, 0 for non-text) further splits text by cap height so the
// renderer can taper that weight to the glyph's on-screen size.
/// The opacity of the entity being emitted -- its own transparency, else its layer's -- set
/// by visible() as each entity is checked (255 = opaque).
thread_local std::uint8_t t_alpha = 255;
/// The draw order of the entity being emitted as a depth, 0 on top to 1 underneath, set by
/// visible() from its order's rank among `t_orders` (every live object's, sorted); a layout
/// build puts its own objects in the nearer half, the model seen through its viewports in
/// the farther (`t_depth_scale` 0.5).
thread_local float t_depth = 0.5f;
thread_local const std::vector<std::uint32_t>* t_orders = nullptr;
thread_local float t_depth_scale = 1.0f;
/// Bits 49-56 of a line key, bits 24-31 of a colour key: the transparency (255 - alpha),
/// so an opaque batch keeps the key it always had.
std::uint64_t line_key(Rgb c, std::uint8_t lw, bool is_text, std::uint64_t height_bucket) {
    return (static_cast<std::uint64_t>(255 - t_alpha) << 49) | (height_bucket << 33) |
           (static_cast<std::uint64_t>(pack_rgb(c)) << 9) | (static_cast<std::uint64_t>(is_text) << 8) | lw;
}
std::uint32_t color_key(Rgb c) {
    return (static_cast<std::uint32_t>(255 - t_alpha) << 24) | pack_rgb(c);
}
// Quantise a world cap height to a batch-key bucket (1/8-unit resolution; 0 == non-text).
std::uint64_t height_bucket(double h) {
    if (h <= 0.0) {
        return 0;
    }
    const auto b = static_cast<std::int64_t>(h * 8.0 + 0.5);
    return static_cast<std::uint64_t>(std::clamp<std::int64_t>(b, 1, 0xFFFF));
}

/// Off/frozen layers contribute no geometry, so the renderer never sees them.
/// The space the builder is emitting: the active one, or model space while a layout's
/// viewports are being filled (see build_render_snapshot_in).
thread_local std::uint8_t t_build_space = 0;
/// VPLAYER: the layers frozen in the viewport whose model view is being built (null when
/// the build is not through a viewport). Consulted by visible()/editable() only.
thread_local const std::vector<std::uint16_t>* t_vp_frozen = nullptr;

bool vp_frozen(std::uint16_t layer) {
    return t_vp_frozen != nullptr &&
           std::find(t_vp_frozen->begin(), t_vp_frozen->end(), layer) != t_vp_frozen->end();
}
bool visible(const GeometryStore& store, const EntityProps& p) {
    const Layer* l = store.layer(p.layer);
    if (t_orders != nullptr && !t_orders->empty()) {
        const auto at = std::lower_bound(t_orders->begin(), t_orders->end(), p.order);
        const double rank = static_cast<double>(at - t_orders->begin());
        const double n = static_cast<double>(t_orders->size());
        t_depth = static_cast<float>(static_cast<double>(t_depth_scale) * (1.0 - (rank + 1.0) / (n + 1.0)));
    }
    if (l != nullptr) {
        // Its own (0 to 90 percent), or ByLayer the layer's; ByBlock outside a reference: none.
        const int percent = p.transparency <= 90                                     ? p.transparency
                            : p.transparency == EntityProps::kTransparencyByBlock ? 0
                                                                                   : l->transparency;
        t_alpha = static_cast<std::uint8_t>(std::lround(255.0 * (100.0 - std::min<int>(percent, 90)) / 100.0));
    }
    return l != nullptr && l->on && !l->frozen && !p.hidden() && p.space() == t_build_space &&
           !vp_frozen(p.layer);
}
/// Editable = visible and not on a locked layer (locked text can't be edited).
bool editable(const GeometryStore& store, const EntityProps& p) {
    const Layer* l = store.layer(p.layer);
    return l != nullptr && l->on && !l->frozen && !l->locked && !p.hidden() &&
           p.space() == t_build_space && !vp_frozen(p.layer);
}

// Liang-Barsky: clip the segment a-b to the rectangle; false when nothing remains.
bool clip_segment(Vec2& a, Vec2& b, double x0, double y0, double x1, double y1) {
    double t0 = 0.0;
    double t1 = 1.0;
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {a.x - x0, x1 - a.x, a.y - y0, y1 - a.y};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) {
                return false;
            }
        } else {
            const double t = q[i] / p[i];
            if (p[i] < 0.0) {
                if (t > t1) {
                    return false;
                }
                t0 = std::max(t0, t);
            } else {
                if (t < t0) {
                    return false;
                }
                t1 = std::min(t1, t);
            }
        }
    }
    const Vec2 na{a.x + dx * t0, a.y + dy * t0};
    const Vec2 nb{a.x + dx * t1, a.y + dy * t1};
    a = na;
    b = nb;
    return true;
}

// Sutherland-Hodgman: the polygon clipped to the rectangle (empty when outside).
std::vector<Vec2> clip_polygon(std::vector<Vec2> poly, double x0, double y0, double x1, double y1) {
    const auto pass = [&](const std::vector<Vec2>& in, auto inside, auto intersect) {
        std::vector<Vec2> out;
        const std::size_t n = in.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Vec2 cur = in[i];
            const Vec2 prev = in[(i + n - 1) % n];
            const bool ci = inside(cur);
            const bool pi = inside(prev);
            if (ci) {
                if (!pi) {
                    out.push_back(intersect(prev, cur));
                }
                out.push_back(cur);
            } else if (pi) {
                out.push_back(intersect(prev, cur));
            }
        }
        return out;
    };
    const auto at_x = [](Vec2 p, Vec2 q, double x) {
        const double t = (x - p.x) / (q.x - p.x);
        return Vec2{x, p.y + (q.y - p.y) * t};
    };
    const auto at_y = [](Vec2 p, Vec2 q, double y) {
        const double t = (y - p.y) / (q.y - p.y);
        return Vec2{p.x + (q.x - p.x) * t, y};
    };
    poly = pass(poly, [&](Vec2 p) { return p.x >= x0; }, [&](Vec2 p, Vec2 q) { return at_x(p, q, x0); });
    if (poly.empty()) {
        return poly;
    }
    poly = pass(poly, [&](Vec2 p) { return p.x <= x1; }, [&](Vec2 p, Vec2 q) { return at_x(p, q, x1); });
    if (poly.empty()) {
        return poly;
    }
    poly = pass(poly, [&](Vec2 p) { return p.y >= y0; }, [&](Vec2 p, Vec2 q) { return at_y(p, q, y0); });
    if (poly.empty()) {
        return poly;
    }
    poly = pass(poly, [&](Vec2 p) { return p.y <= y1; }, [&](Vec2 p, Vec2 q) { return at_y(p, q, y1); });
    return poly;
}

// Every live object's draw order, sorted (the ranks visible() turns into depths).
std::vector<std::uint32_t> live_orders(const GeometryStore& store) {
    std::vector<std::uint32_t> out;
    const auto add = [&](const auto& arena) {
        const auto data = arena.data();
        for (std::uint32_t i = 0; i < arena.slot_count(); ++i) {
            if (!arena.alive(i)) {
                continue;
            }
            if constexpr (requires { data[i].props.order; }) {
                out.push_back(data[i].props.order);
            } else if constexpr (requires { data[i].text.props.order; }) {
                out.push_back(data[i].text.props.order);
            }
        }
    };
    add(store.points());
    add(store.xlines());
    add(store.ellipses());
    add(store.lines());
    add(store.circles());
    add(store.arcs());
    add(store.polylines());
    add(store.splines());
    add(store.texts());
    add(store.attdefs());
    add(store.dimensions());
    add(store.leaders());
    add(store.mtexts());
    add(store.mleaders());
    add(store.hatches());
    add(store.fcfs());
    add(store.datums());
    add(store.images());
    add(store.viewports());
    add(store.tables());
    add(store.mlines());
    add(store.inserts());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

ResolvedProps entity_resolved(const GeometryStore& store, const EntityProps& p) {
    const Layer* l = store.layer(p.layer);
    return l != nullptr ? resolve(p, *l) : ResolvedProps{};
}

} // namespace

/// The builder proper: `space` is what it emits; `with_viewports` lets a layout build
/// fill its viewports with a nested model-space build.
void build_render_snapshot_in(const GeometryStore& store, const IGeometryKernel& kernel,
                              RenderSnapshot& out, double tolerance, double ltscale,
                              std::uint8_t space, bool with_viewports);

void build_render_snapshot(const GeometryStore& store, const IGeometryKernel& kernel,
                           RenderSnapshot& out, double tolerance, double ltscale) {
    // MSPACE: model space edited through a viewport hides the layers frozen in it.
    const std::vector<std::uint16_t>* frozen = nullptr;
    if (store.active_space() == 0) {
        if (const ViewportData* mv = store.viewport(store.mspace_viewport());
            mv != nullptr && !mv->frozen_layers.empty()) {
            frozen = &mv->frozen_layers;
        }
    }
    t_vp_frozen = frozen;
    build_render_snapshot_in(store, kernel, out, tolerance, ltscale, store.active_space(), true);
    t_vp_frozen = nullptr;
}

void build_render_snapshot_in(const GeometryStore& store, const IGeometryKernel& kernel,
                              RenderSnapshot& out, double tolerance, double ltscale,
                              std::uint8_t space, bool with_viewports) {
    t_build_space = space;
    t_alpha = 255;
    // Draw order: a layout's own objects over the model seen through its viewports.
    const std::vector<std::uint32_t> orders = live_orders(store);
    const float depth_scale = with_viewports && space != 0 ? 0.5f : 1.0f;
    t_orders = &orders;
    t_depth_scale = depth_scale;
    t_depth = 0.5f;
    const IFontEngine* fonts = store.font_engine();
    out.points.clear();
    out.line_vertices.clear();
    out.construction_lines.clear();
    out.line_batches.clear();
    out.point_batches.clear();
    out.fill_vertices.clear();
    out.fill_batches.clear();
    out.wipeout_vertices.clear();
    out.line_depth.clear();
    out.point_depth.clear();
    out.fill_depth.clear();
    out.wipeout_depth.clear();
    out.images.clear();
    out.text_edit_targets.clear();
    out.attrib_edit_targets.clear();
    out.layers.assign(store.layers().begin(), store.layers().end());
    out.current_layer = store.current_layer();
    out.page_setups = store.page_setups();
    out.wipeout_frames = store.wipeout_frames();
    out.fillmode = store.fillmode();
    out.pdmode = store.pdmode();
    out.pdsize = store.pdsize();
    out.limits_min = store.limits_min();
    out.limits_max = store.limits_max();
    out.limits_on = store.limits_on();
    out.insbase = store.insbase();
    out.data_links = store.data_links();
    out.active_space = store.active_space();
    out.layouts.clear();
    for (const Layout& l : store.layouts()) {
        out.layouts.push_back(LayoutInfo{l.id, l.name, l.page.paper_w_mm, l.page.paper_h_mm,
                                         l.page.landscape});
    }

    // Lines group by (colour, lineweight); points and fills by colour. Each keeps the depth
    // of what it holds: per segment, per point, per fill vertex.
    struct Group {
        std::vector<Vec2> v;
        std::vector<float> d;
    };
    std::map<std::uint64_t, Group> line_groups;
    std::map<std::uint32_t, Group> point_groups;
    std::map<std::uint32_t, Group> fill_groups;
    std::map<std::uint32_t, Group> gradient_groups; // computed colours (GRADIENT bands)

    const auto add_line = [&](Rgb c, std::uint8_t lw, Vec2 a, Vec2 b, bool is_text = false,
                              double text_height = 0.0) {
        Group& g = line_groups[line_key(c, lw, is_text, height_bucket(text_height))];
        g.v.push_back(a);
        g.v.push_back(b);
        g.d.push_back(t_depth);
    };
    const auto add_lines = [&](Rgb c, std::uint8_t lw, const std::vector<Vec2>& segs,
                               bool is_text = false, double text_height = 0.0) {
        Group& g = line_groups[line_key(c, lw, is_text, height_bucket(text_height))];
        g.v.insert(g.v.end(), segs.begin(), segs.end());
        g.d.insert(g.d.end(), segs.size() / 2, t_depth);
    };
    const auto add_fills = [&](Rgb c, const std::vector<Vec2>& tris) {
        Group& g = fill_groups[color_key(c)];
        g.v.insert(g.v.end(), tris.begin(), tris.end());
        g.d.insert(g.d.end(), tris.size(), t_depth);
    };
    const auto add_point = [&](Rgb c, Vec2 p) {
        Group& g = point_groups[color_key(c)];
        g.v.push_back(p);
        g.d.push_back(t_depth);
    };
    // A paragraph's layout in its colour, the runs a format code colours (\C, \c) in theirs.
    std::vector<Vec2> mtext_part;
    const auto add_mtext = [&](const text::MTextLayout& lay, Rgb c, std::uint8_t lw, double text_height) {
        if (lay.colors.empty()) {
            add_lines(c, lw, lay.segments, /*is_text=*/true, text_height);
            add_fills(c, lay.fills);
            return;
        }
        const auto emit = [&](Rgb rc, std::size_t s0, std::size_t s1, std::size_t f0, std::size_t f1) {
            if (s1 > s0) {
                mtext_part.assign(lay.segments.begin() + static_cast<std::ptrdiff_t>(s0),
                                  lay.segments.begin() + static_cast<std::ptrdiff_t>(s1));
                add_lines(rc, lw, mtext_part, /*is_text=*/true, text_height);
            }
            if (f1 > f0) {
                mtext_part.assign(lay.fills.begin() + static_cast<std::ptrdiff_t>(f0),
                                  lay.fills.begin() + static_cast<std::ptrdiff_t>(f1));
                add_fills(rc, mtext_part);
            }
        };
        std::size_t seg = 0;
        std::size_t fill = 0;
        for (const text::MTextColorSpan& span : lay.colors) {
            emit(c, seg, span.seg_begin, fill, span.fill_begin);
            emit(span.color, span.seg_begin, span.seg_end, span.fill_begin, span.fill_end);
            seg = span.seg_end;
            fill = span.fill_end;
        }
        emit(c, seg, lay.segments.size(), fill, lay.fills.size());
    };
    const auto add_gradient_fills = [&](Rgb c, const std::vector<Vec2>& tris) {
        Group& g = gradient_groups[color_key(c)];
        g.v.insert(g.v.end(), tris.begin(), tris.end());
        g.d.insert(g.d.end(), tris.size(), t_depth);
    };

    // The two coexisting text paths share one layout; only the per-glyph geometry source
    // differs. An outline (TTF/OTF) font emits FILLED triangles; otherwise the built-in
    // stroke font emits LINE segments. Glyphs are generated here, never baked.
    text::TextRunGeometry trun;
    // `lineweight` is the entity's RESOLVED weight (ByLayer already applied). Stroke
    // glyphs carry it into the line batches exactly like any other stroked geometry, so
    // PLOT prints text at the weight the author set. The viewport is unaffected: it
    // ignores `lineweight` for `is_text` batches and applies its own screen weight
    // (Ph31), so the on-screen polish and the paper result stay independent.
    const auto emit_text_run = [&](std::string_view raw, Vec2 origin, double height,
                                   double rotation, text::Justify justify, std::uint16_t font_id,
                                   Rgb color, std::uint8_t lineweight,
                                   const TextStyle* ts = nullptr) {
        text::text_run_geometry(raw, origin, height, rotation, justify, store.font_name(font_id), fonts, ts,
                                /*outlines=*/false, trun);
        if (trun.outline) {
            add_fills(color, trun.tris);
        } else {
            add_lines(color, lineweight, trun.segments, /*is_text=*/true, height);
        }
        // Overline (%%o) / underline (%%u) bars, in the text colour.
        for (std::size_t i = 0; i + 1 < trun.bars.size(); i += 2) {
            add_line(color, lineweight, trun.bars[i], trun.bars[i + 1], /*is_text=*/true, height);
        }
    };

    // Points as PDMODE draws them: 0 a dot (the renderer's point), 1 nothing, 2 a plus, 3 a
    // cross, 4 a tick up from the point; +32 inside a circle, +64 inside a square. PDSIZE
    // above zero is the marker's size in drawing units; zero or less, a size on screen
    // whatever the zoom (about forty pixels: `tolerance` is a chord error of 0.3 pixels).
    const std::uint8_t pdmode = store.pdmode();
    const double point_half = store.pdsize() > 0.0 ? store.pdsize() * 0.5 : (tolerance / 0.3) * 20.0;
    std::vector<Vec2> marker;
    for_each_live(store.points(), EntityKind::Point, [&](EntityHandle h) {
        const PointData* d = store.point(h);
        if (!visible(store, d->props)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, d->props);
        const std::uint8_t shape = pdmode & 7;
        if (shape == 1) {
            return;
        }
        if (shape == 0 && (pdmode & 96) == 0) {
            add_point(r.color, d->p);
            return;
        }
        marker.clear();
        const Vec2 c = d->p;
        const double hs = point_half;
        const auto seg = [&](Vec2 a, Vec2 b) {
            marker.push_back(a);
            marker.push_back(b);
        };
        if (shape == 0) {
            seg(c - Vec2{hs * 0.05, 0.0}, c + Vec2{hs * 0.05, 0.0}); // the dot, inside its frame
        } else if (shape == 2) {
            seg(c - Vec2{hs, 0.0}, c + Vec2{hs, 0.0});
            seg(c - Vec2{0.0, hs}, c + Vec2{0.0, hs});
        } else if (shape == 3) {
            seg(c + Vec2{-hs, -hs}, c + Vec2{hs, hs});
            seg(c + Vec2{-hs, hs}, c + Vec2{hs, -hs});
        } else if (shape == 4) {
            seg(c, c + Vec2{0.0, hs});
        }
        if ((pdmode & 32) != 0) {
            constexpr int kSteps = 24;
            for (int i = 0; i < kSteps; ++i) {
                const double a0 = kTwoPi * i / kSteps;
                const double a1 = kTwoPi * (i + 1) / kSteps;
                seg(c + Vec2{std::cos(a0), std::sin(a0)} * hs, c + Vec2{std::cos(a1), std::sin(a1)} * hs);
            }
        }
        if ((pdmode & 64) != 0) {
            seg(c + Vec2{-hs, -hs}, c + Vec2{hs, -hs});
            seg(c + Vec2{hs, -hs}, c + Vec2{hs, hs});
            seg(c + Vec2{hs, hs}, c + Vec2{-hs, hs});
            seg(c + Vec2{-hs, hs}, c + Vec2{-hs, -hs});
        }
        add_lines(r.color, r.lineweight, marker);
    });

    // Dash buffer reused across entities. dash_polyline appends "on" sub-segments
    // (endpoint pairs) per the resolved linetype, scaled by LTSCALE; Continuous
    // copies every segment. Dashing is derived here, never stored.
    std::vector<Vec2> dashed;
    for_each_live(store.lines(), EntityKind::Line, [&](EntityHandle h) {
        const LineData* l = store.line(h);
        if (!visible(store, l->props)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, l->props);
        const std::array<Vec2, 2> seg{l->a, l->b};
        dashed.clear();
        // Effective linetype scale = document LTSCALE x this entity's CELTSCALE. This is
        // the ONE multiplication feeding the Ph23 dash renderer; the plot path uses the
        // same build_render_snapshot, so plot honours it with no fork.
        dash_polyline(seg, r.linetype, store.builtin_linetypes(), store.linetypes(), ltscale * store.celtscale(h), dashed);
        add_lines(r.color, r.lineweight, dashed);
    });

    std::vector<Vec2> tess;
    const auto emit_curve = [&](EntityHandle h, const EntityProps& p) {
        if (!visible(store, p)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, p);
        kernel.tessellate(store, h, tolerance, tess);
        // Dash by arc-length along the tessellated curve (phase carries across the
        // tessellation vertices), so the pattern stays even and zoom-consistent.
        dashed.clear();
        dash_polyline(tess, r.linetype, store.builtin_linetypes(), store.linetypes(), ltscale * store.celtscale(h), dashed);
        add_lines(r.color, r.lineweight, dashed);
    };
    for_each_live(store.circles(), EntityKind::Circle,
                  [&](EntityHandle h) { emit_curve(h, store.circle(h)->props); });
    for_each_live(store.arcs(), EntityKind::Arc,
                  [&](EntityHandle h) { emit_curve(h, store.arc(h)->props); });
    // A polyline with widths draws each wide segment as a filled band centred on it
    // (its outline with FILLMODE off); a segment of no width stays ordinary line work.
    // The band is derived here from the vertices and widths, like every other display.
    std::vector<Vec2> band;
    std::vector<Vec2> seg_left;
    std::vector<Vec2> seg_right;
    for_each_live(store.polylines(), EntityKind::Polyline, [&](EntityHandle h) {
        const PolylineData* pl = store.polyline(h);
        const std::span<const double> widths = store.widths_of(*pl);
        if (!pline::has_width(widths)) {
            const ResolvedProps rp = entity_resolved(store, pl->props);
            if (pl->ltype_gen || rp.linetype == Linetype::Continuous) {
                emit_curve(h, pl->props);
                return;
            }
            if (!visible(store, pl->props)) {
                return;
            }
            // Linetype generation off: the pattern starts again at every vertex.
            const std::span<const Vec2> v = store.vertices_of(*pl);
            const std::span<const double> b = store.bulges_of(*pl);
            const std::size_t n = v.size();
            const std::size_t nseg = n < 2 ? 0 : (pl->closed ? n : n - 1);
            dashed.clear();
            for (std::size_t i = 0; i < nseg; ++i) {
                seg_left.clear();
                seg_right.clear();
                pline::detail::segment_rows(v[i], v[(i + 1) % n], i < b.size() ? b[i] : 0.0, 0.0, 0.0, tolerance,
                                            seg_left, seg_right);
                dash_polyline(seg_left, rp.linetype, store.builtin_linetypes(), store.linetypes(), ltscale * store.celtscale(h), dashed);
            }
            add_lines(rp.color, rp.lineweight, dashed);
            return;
        }
        if (!visible(store, pl->props)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, pl->props);
        const std::span<const Vec2> verts = store.vertices_of(*pl);
        const std::span<const double> bulges = store.bulges_of(*pl);
        const std::optional<double> uniform = pline::uniform_width(widths);
        band.clear();
        if (r.linetype != Linetype::Continuous && uniform) {
            // A dashed wide polyline: every dash is a piece of the band.
            kernel.tessellate(store, h, tolerance, tess);
            dashed.clear();
            dash_polyline(tess, r.linetype, store.builtin_linetypes(), store.linetypes(), ltscale * store.celtscale(h), dashed);
            const std::array<double, 4> ww{*uniform, *uniform, *uniform, *uniform};
            for (std::size_t i = 0; i + 1 < dashed.size(); i += 2) {
                const std::array<Vec2, 2> piece{dashed[i], dashed[i + 1]};
                if (store.fillmode()) {
                    pline::band_triangles(piece, {}, ww, false, tolerance, band);
                } else {
                    pline::band_outline(piece, {}, ww, false, tolerance, band);
                }
            }
        } else if (store.fillmode()) {
            pline::band_triangles(verts, bulges, widths, pl->closed, tolerance, band);
        } else {
            pline::band_outline(verts, bulges, widths, pl->closed, tolerance, band);
        }
        if (store.fillmode()) {
            add_fills(r.color, band);
        } else {
            add_lines(r.color, r.lineweight, band);
        }
        if (!uniform) {
            band.clear();
            pline::plain_segments(verts, bulges, widths, pl->closed, tolerance, band);
            add_lines(r.color, r.lineweight, band);
        }
    });
    for_each_live(store.splines(), EntityKind::Spline,
                  [&](EntityHandle h) { emit_curve(h, store.spline(h)->props); });
    for_each_live(store.ellipses(), EntityKind::Ellipse,
                  [&](EntityHandle h) { emit_curve(h, store.ellipse(h)->props); });

    // Single-line text -> stroke-font segments (thin: text isn't lineweight-driven).
    std::vector<Vec2> tseg;
    for_each_live(store.texts(), EntityKind::Text, [&](EntityHandle h) {
        const TextData* t = store.text(h);
        if (!visible(store, t->props)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, t->props);
        // Laid out in the text's own frame (its justification resolved, Aligned's height
        // and Fit's width factor worked out): the run starts at the frame's origin.
        const text::TextFrame frame = text::frame_of(store, *t);
        TextStyle tstyle = store.text_style_of(*t);
        tstyle.width_factor = frame.width_factor;
        emit_text_run(store.string_of(*t), frame.origin, frame.height, frame.rotation,
                      text::Justify::Left, t->font, r.color, r.lineweight, &tstyle);
        if (editable(store, t->props)) {
            // The selection box uses the VISIBLE width (codes expanded); the edit target
            // keeps the RAW string so double-click editing shows %%c50, not the symbol.
            out.text_edit_targets.push_back(TextEditTarget{
                h, frame.origin, {frame.origin.x, frame.origin.y},
                {frame.origin.x + frame.width(), frame.origin.y + frame.height}, frame.height,
                frame.rotation, false, std::string(store.string_of(*t))});
        }
    });

    // ATTDEF: in model space an attribute definition shows its TAG (AutoCAD's look
    // before BLOCK folds it into a definition); values only appear on INSERTs.
    for_each_live(store.attdefs(), EntityKind::AttDef, [&](EntityHandle h) {
        const AttDefData* a = store.attdef(h);
        if (!visible(store, a->text.props)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, a->text.props);
        const text::TextFrame frame = text::frame_of(store, a->text);
        TextStyle tstyle = store.text_style_of(a->text);
        tstyle.width_factor = frame.width_factor;
        emit_text_run(store.string_of(a->text), frame.origin, frame.height, frame.rotation,
                      text::Justify::Left, a->text.font, r.color, r.lineweight, &tstyle);
    });

    // Dimensions: per-element colours (ext/dim/arrow/text), filled arrowheads, and
    // the measured label -- all computed from def points + style.
    for_each_live(store.dimensions(), EntityKind::Dimension, [&](EntityHandle h) {
        const DimData* d = store.dimension(h);
        if (!visible(store, d->props)) {
            return;
        }
        const Rgb base = entity_resolved(store, d->props).color;
        const DimStyle* style = store.dimstyle(d->style);
        const DimGeometry g = compute_dim_geometry(*d, style != nullptr ? *style : DimStyle{}, base,
                                                   store.dim_text_parts(*d));
        add_lines(g.ext_color, g.lineweight, g.ext_lines);
        add_lines(g.dim_color, g.lineweight, g.dim_lines);
        add_lines(g.arrow_color, g.lineweight, g.arrow_lines);
        add_fills(g.arrow_color, g.arrow_fills);
        tseg.clear();
        append_dim_label_segments(g, tseg); // the label (a Limits second line; a DIMTFAC tolerance)
        add_lines(g.text_color, g.lineweight, tseg, /*is_text=*/true, g.text_height);
    });

    // TABLE: borders/grid into the line batches, cell text through the same stroke-font
    // emission every other entity uses. All derived here, never baked.
    for_each_live(store.tables(), EntityKind::Table, [&](EntityHandle h) {
        const TableData* td = store.table(h);
        if (!visible(store, td->props)) {
            return;
        }
        const Rgb base = entity_resolved(store, td->props).color;
        const TableStyle* st = store.table_style(td->style);
        const TableGeometry g = compute_table_geometry(
            *td, store.table_cell_views(*td), store.table_col_widths(*td),
            store.table_row_heights(*td), st != nullptr ? *st : TableStyle{}, base);
        add_lines(g.line_color, g.lineweight, g.lines);
        const std::span<const TableCell> raw_cells = store.table_cells(*td);
        for (std::size_t i = 0; i < g.cell_text.size(); ++i) {
            // Every VISIBLE cell is a double-click target, empty ones included -- an
            // empty cell is precisely the one a user wants to type into. The target box
            // is the cell's own quad, so the click resolves to the cell under the cursor
            // rather than to the table as a whole.
            const std::array<Vec2, 4>& q = g.cell_quads[i];
            Vec2 lo = q[0];
            Vec2 hi = q[0];
            for (const Vec2& c : q) {
                lo = {std::min(lo.x, c.x), std::min(lo.y, c.y)};
                hi = {std::max(hi.x, c.x), std::max(hi.y, c.y)};
            }
            // The RAW stored string, not the code-substituted one: editing must reopen
            // what the user typed (`%%c`), not the glyph it renders as.
            const std::uint32_t ci = g.cell_index[i];
            std::string raw;
            if (ci < raw_cells.size()) {
                raw = std::string(store.string_of(raw_cells[ci]));
            }
            out.text_edit_targets.push_back(TextEditTarget{h, g.text_pos[i], lo, hi,
                                                          g.text_height[i], g.rotation,
                                                          /*multiline=*/false, std::move(raw)});
            if (g.cell_text[i].empty()) {
                continue;
            }
            tseg.clear();
            text::append_text_segments(g.cell_text[i], g.text_pos[i], g.text_height[i],
                                       g.rotation, text::Justify::Left, tseg);
            add_lines(g.text_color, g.lineweight, tseg, /*is_text=*/true, g.text_height[i]);
        }
    });

    // Raster images: publish the TRANSFORM only. Decoded pixels never enter the
    // snapshot -- it is copied through the triple buffer on every publish, so the
    // renderer/plotter caches textures by definition index instead (see ImageInstance).
    for_each_live(store.images(), EntityKind::Image, [&](EntityHandle h) {
        const ImageData* im = store.image(h);
        if (!visible(store, im->props)) {
            return;
        }
        const ImageDef* def = store.image_def(im->def);
        const ImageQuad q = resolve_image_quad(*im);
        const std::array<double, 4> uv = resolve_image_uv(*im);
        ImageInstance inst;
        inst.quad = q;
        inst.uv = {static_cast<float>(uv[0]), static_cast<float>(uv[1]),
                   static_cast<float>(uv[2]), static_cast<float>(uv[3])};
        inst.def = im->def;
        inst.def_version = def != nullptr ? def->version : 0;
        inst.handle = h;
        const std::span<const Vec2> poly = store.image_clip_polygon(*im);
        if (!poly.empty()) {
            // A polygonal clip: the region as textured triangles (triangulated in image
            // fractions, then placed), plus its outline for the frame and the plot's clip.
            inst.clip_world = image_uv_to_world(*im, poly);
            std::vector<Vec2> uv_tris;
            hatch::triangulate_filled({std::vector<Vec2>(poly.begin(), poly.end())}, uv_tris);
            inst.tri_uv = uv_tris;
            inst.tri_world = image_uv_to_world(*im, uv_tris);
        }
        inst.depth = t_depth;
        out.images.push_back(inst);
        if (store.image_frame() != 0) {
            // IMAGEFRAME: the clip boundary's outline in the entity's colour.
            const ResolvedProps r = entity_resolved(store, im->props);
            if (!poly.empty()) {
                const std::vector<Vec2>& cw = out.images.back().clip_world;
                for (std::size_t i = 0; i < cw.size(); ++i) {
                    add_line(r.color, r.lineweight, cw[i], cw[(i + 1) % cw.size()]);
                }
            } else {
                for (std::size_t i = 0; i < 4; ++i) {
                    add_line(r.color, r.lineweight, q[i], q[(i + 1) % 4]);
                }
            }
        }
    });

    // GD&T: borders/dividers into the line batches, cell text through the SAME
    // emit_text_run every other entity uses, and the datum triangle into the existing
    // fill channel exactly as an arrowhead is. All geometry derived here, never baked.
    for_each_live(store.fcfs(), EntityKind::Fcf, [&](EntityHandle h) {
        const FcfData* f = store.fcf(h);
        if (!visible(store, f->props)) {
            return;
        }
        const Rgb base = entity_resolved(store, f->props).color;
        const DimStyle* st = store.dimstyle(f->style);
        const FcfGeometry g =
            compute_fcf_geometry(*f, store.fcf_cell_text(*f), st != nullptr ? *st : DimStyle{}, base);
        add_lines(g.line_color, g.lineweight, g.lines);
        for (std::size_t i = 0; i < g.cell_text.size(); ++i) {
            tseg.clear();
            text::append_text_segments(g.cell_text[i], g.text_pos[i], g.text_height, g.rotation,
                                       text::Justify::Left, tseg);
            add_lines(g.text_color, g.lineweight, tseg, /*is_text=*/true, g.text_height);
        }
    });

    for_each_live(store.xlines(), EntityKind::Xline, [&](EntityHandle h) {
        const XlineData* x = store.xline(h);
        if (!visible(store, x->props)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, x->props);
        out.construction_lines.push_back(
            ConstructionLineView{x->base, x->dir, x->ray, r.color, r.lineweight});
    });

    // Multilines: each element in its own colour and linetype (ByLayer: the layer's; ByBlock:
    // the multiline's own), the caps and joints in the multiline's, the fill under them.
    for_each_live(store.mlines(), EntityKind::MLine, [&](EntityHandle h) {
        const MLineData* m = store.mline(h);
        if (!visible(store, m->props)) {
            return;
        }
        static const MLineStyle kStandard{};
        const MLineStyle* st = store.mline_style(m->style);
        const MLineStyle& style = st != nullptr ? *st : kStandard;
        const ResolvedProps r = entity_resolved(store, m->props);
        EntityProps layer_props = m->props;
        layer_props.flags |= EntityProps::kColorByLayer | EntityProps::kLinetypeByLayer;
        const ResolvedProps by_layer = entity_resolved(store, layer_props);
        const auto color_of = [&](std::uint8_t mode, Rgb own) {
            return mode == 0 ? own : mode == 1 ? by_layer.color : r.color;
        };
        const mline::Geometry g = mline::geometry(*m, style);
        if (!g.fill.empty() && store.fillmode()) {
            add_fills(color_of(style.fill_mode, style.fill_color), g.fill);
        }
        for (const mline::Geometry::Piece& p : g.pieces) {
            const MLineElement& el = style.elements[p.element];
            const Linetype lt = el.linetype_mode == 0 ? el.linetype
                                : el.linetype_mode == 1 ? by_layer.linetype
                                                        : r.linetype;
            const std::array<Vec2, 2> seg{p.a, p.b};
            dashed.clear();
            dash_polyline(seg, lt, store.builtin_linetypes(), store.linetypes(), ltscale * store.celtscale(h), dashed);
            add_lines(color_of(el.color_mode, el.color), r.lineweight, dashed);
        }
        add_lines(r.color, r.lineweight, g.caps);
    });

    for_each_live(store.datums(), EntityKind::Datum, [&](EntityHandle h) {
        const DatumData* d = store.datum(h);
        if (!visible(store, d->props)) {
            return;
        }
        const Rgb base = entity_resolved(store, d->props).color;
        const DimStyle* st = store.dimstyle(d->style);
        const DatumGeometry g =
            compute_datum_geometry(*d, store.string_of(*d), st != nullptr ? *st : DimStyle{}, base);
        add_lines(g.line_color, g.lineweight, g.lines);
        add_fills(g.line_color, g.fills); // the filled triangle, like an arrowhead
        tseg.clear();
        text::append_text_segments(g.text, g.text_pos, g.text_height, g.rotation,
                                   text::Justify::Left, tseg);
        add_lines(g.text_color, g.lineweight, tseg, /*is_text=*/true, g.text_height);
    });

    // Leaders: arrowhead + leader line + text label (shares the dimstyle arrow).
    std::vector<Vec2> afill;
    std::vector<Vec2> aline;
    for_each_live(store.leaders(), EntityKind::Leader, [&](EntityHandle h) {
        const LeaderData* l = store.leader(h);
        if (!visible(store, l->props)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, l->props);
        const DimStyle* style = store.dimstyle(l->style);
        // Per-leader arrow override wins over the dimstyle (override-first, like dimensions).
        const DimStyle s = apply_dim_overrides(style != nullptr ? *style : DimStyle{}, l->overrides);
        const Rgb arrow_c = s.arrow_color.resolve(r.color);
        const Rgb text_c = s.text_color.resolve(r.color);
        add_line(r.color, r.lineweight, l->tip, l->knee);
        afill.clear();
        aline.clear();
        append_arrowhead(afill, aline, l->tip, l->knee - l->tip, s.arrow_size,
                         static_cast<ArrowType>(s.arrow_type));
        add_fills(arrow_c, afill);
        add_lines(arrow_c, r.lineweight, aline);
        emit_text_run(store.string_of(*l), l->knee + Vec2{s.arrow_size * 0.4, 0.0}, l->text_height,
                      0.0, text::Justify::Left, l->font, text_c, r.lineweight);
    });

    // MTEXT: multi-line paragraph text. Layout is COMPUTED here from the stored
    // fields (text/mtext.cpp) -- never baked on the entity.
    for_each_live(store.mtexts(), EntityKind::MText, [&](EntityHandle h) {
        const MTextData* m = store.mtext(h);
        if (!visible(store, m->props)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, m->props);
        const text::MTextLayout lay = text::layout_mtext(m->text, store.string_of(m->text), fonts,
                                                         store.font_name(m->text.font));
        add_mtext(lay, r.color, r.lineweight, m->text.height);
        if (editable(store, m->props)) {
            out.text_edit_targets.push_back(TextEditTarget{h, m->text.pos, lay.min, lay.max,
                                                           m->text.height, m->text.rotation, true,
                                                           std::string(store.string_of(m->text))});
        }
    });

    // QLEADER: leader polyline + arrowhead + owned paragraph label (same layout).
    for_each_live(store.mleaders(), EntityKind::MLeader, [&](EntityHandle h) {
        const MLeaderData* m = store.mleader(h);
        if (!visible(store, m->props)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, m->props);
        const DimStyle* style = store.dimstyle(m->style);
        const DimStyle s = apply_dim_overrides(style != nullptr ? *style : DimStyle{}, m->overrides);
        const Rgb arrow_c = s.arrow_color.resolve(r.color);
        const Rgb text_c = s.text_color.resolve(r.color);
        const std::span<const Vec2> v = store.vertices_of(*m);
        // The leader's path: its points, or a spline through them (the landing, the last
        // segment, left straight); none at all for a leader type of None.
        std::vector<Vec2> path(v.begin(), v.end());
        const bool landing = (m->leader_flags & LeaderForm::kLanding) != 0;
        if ((m->leader_flags & LeaderForm::kSpline) != 0 && v.size() >= (landing ? 4u : 3u)) {
            const std::size_t through = landing ? v.size() - 1 : v.size();
            const std::vector<Vec2> ctrl =
                spline::fit_or_fallback(std::span<const Vec2>(v.data(), through), 3, spline::FitParam::Chord);
            path.clear();
            spline::tessellate(ctrl, 3, path);
            if (landing) {
                path.push_back(v.back());
            }
        }
        const bool line = (m->leader_flags & LeaderForm::kNoLine) == 0;
        for (std::size_t i = 1; line && i < path.size(); ++i) {
            add_line(r.color, r.lineweight, path[i - 1], path[i]);
        }
        if (line && path.size() >= 2 && (m->leader_flags & LeaderForm::kNoArrow) == 0) {
            afill.clear();
            aline.clear();
            append_arrowhead(afill, aline, path[0], path[1] - path[0], s.arrow_size,
                             static_cast<ArrowType>(s.arrow_type));
            add_fills(arrow_c, afill);
            add_lines(arrow_c, r.lineweight, aline);
        }
        // MLEADEREDIT's added leaders: straight from each arrowhead to the landing.
        const Vec2 attach = mleader_attach(v, m->leader_flags);
        for (const Vec2& tip : store.extra_tips_of(*m)) {
            if (!line || distance(tip, attach) < 1e-12) {
                continue;
            }
            add_line(r.color, r.lineweight, tip, attach);
            if ((m->leader_flags & LeaderForm::kNoArrow) == 0) {
                afill.clear();
                aline.clear();
                append_arrowhead(afill, aline, tip, attach - tip, s.arrow_size, static_cast<ArrowType>(s.arrow_type));
                add_fills(arrow_c, afill);
                add_lines(arrow_c, r.lineweight, aline);
            }
        }
        const text::MTextLayout lay = text::layout_mtext(m->text, store.string_of(m->text), fonts,
                                                         store.font_name(m->text.font));
        add_mtext(lay, text_c, r.lineweight, m->text.height);
        if (editable(store, m->props)) {
            out.text_edit_targets.push_back(TextEditTarget{h, m->text.pos, lay.min, lay.max,
                                                           m->text.height, m->text.rotation, true,
                                                           std::string(store.string_of(m->text))});
        }
    });

    // HATCH: a filled (SOLID) or patterned region. Geometry is COMPUTED here from the
    // boundary loops + pattern parameters (derived-not-baked) -- the store keeps only the
    // boundary + pattern name/scale/angle. Part A renders SOLID via the fill pipeline (so it
    // plots as PDF vectors with no special case); line-family patterns are added in Part B.
    std::vector<Vec2> htris;
    std::vector<hatch::Segment> hpsegs;
    for_each_live(store.hatches(), EntityKind::Hatch, [&](EntityHandle h) {
        const HatchData* hd = store.hatch(h);
        if (!visible(store, hd->props)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, hd->props);
        const std::string_view pname = store.string_of(*hd);
        if (!store.fillmode() && pname != "WIPEOUT") {
            return; // FILLMODE 0 hides hatches and fills
        }
        if (pname == "SOLID") {
            htris.clear();
            hatch::triangulate_filled(store.hatch_loops(*hd), htris);
            add_fills(r.color, htris);
        } else if (pname == "WIPEOUT") {
            // A wipeout masks what lies beneath: its fill goes to the renderer's mask pass
            // (background colour, drawn after geometry and before text). Its frame is
            // ordinary line work in the entity's colour when WIPEOUTFRAME is on.
            htris.clear();
            hatch::triangulate_filled(store.hatch_loops(*hd), htris);
            out.wipeout_vertices.insert(out.wipeout_vertices.end(), htris.begin(), htris.end());
            out.wipeout_depth.insert(out.wipeout_depth.end(), htris.size(), t_depth);
            if (store.wipeout_frames()) {
                for (const auto& loop : store.hatch_loops(*hd)) {
                    for (std::size_t i = 0; i < loop.size(); ++i) {
                        add_line(r.color, r.lineweight, loop[i], loop[(i + 1) % loop.size()]);
                    }
                }
            }
        } else if (pname == "GRADIENT") {
            // A two-colour linear gradient along pattern_angle, drawn as bands: each
            // triangle of the filled region is clipped to the band strips and coloured by
            // the band's mix -- no renderer change, and it plots as flat vector fills.
            htris.clear();
            hatch::triangulate_filled(store.hatch_loops(*hd), htris);
            if (!htris.empty()) {
                const Vec2 axis{std::cos(hd->pattern_angle), std::sin(hd->pattern_angle)};
                double lo = std::numeric_limits<double>::infinity();
                double hi = -lo;
                for (const Vec2& p : htris) {
                    const double t = dot(p, axis);
                    lo = std::min(lo, t);
                    hi = std::max(hi, t);
                }
                constexpr int kBands = 24;
                const double span = std::max(hi - lo, 1e-9);
                std::vector<Vec2> band_tris;
                std::vector<Vec2> poly;
                std::vector<Vec2> clipped;
                const auto clip_half = [&](const std::vector<Vec2>& in, double offset, bool keep_ge,
                                           std::vector<Vec2>& outp) {
                    outp.clear();
                    const std::size_t n = in.size();
                    for (std::size_t i = 0; i < n; ++i) {
                        const Vec2 a = in[i];
                        const Vec2 b = in[(i + 1) % n];
                        const double da = dot(a, axis) - offset;
                        const double db = dot(b, axis) - offset;
                        const bool ina = keep_ge ? da >= 0.0 : da <= 0.0;
                        const bool inb = keep_ge ? db >= 0.0 : db <= 0.0;
                        if (ina) {
                            outp.push_back(a);
                        }
                        if (ina != inb) {
                            const double t = da / (da - db);
                            outp.push_back(a + (b - a) * t);
                        }
                    }
                };
                // Neighbouring bands overlap by a sliver so the seams between them do not
                // show as hairlines when the fills are antialiased (screen and PDF alike).
                const double overlap = span / kBands * 0.03;
                for (int k = 0; k < kBands; ++k) {
                    const double t0 = lo + span * static_cast<double>(k) / kBands;
                    const double t1 = lo + span * static_cast<double>(k + 1) / kBands + overlap;
                    const double mix = (static_cast<double>(k) + 0.5) / kBands;
                    const auto lerp8 = [mix](std::uint8_t a, std::uint8_t b) {
                        return static_cast<std::uint8_t>(std::lround(a + (static_cast<double>(b) - a) * mix));
                    };
                    const Rgb c{lerp8(r.color.r, hd->color2.r), lerp8(r.color.g, hd->color2.g),
                                lerp8(r.color.b, hd->color2.b)};
                    band_tris.clear();
                    for (std::size_t i = 0; i + 2 < htris.size(); i += 3) {
                        poly = {htris[i], htris[i + 1], htris[i + 2]};
                        clip_half(poly, t0, true, clipped);
                        if (clipped.size() < 3) {
                            continue;
                        }
                        clip_half(clipped, t1, false, poly);
                        if (poly.size() < 3) {
                            continue;
                        }
                        for (std::size_t j = 1; j + 1 < poly.size(); ++j) {
                            band_tris.push_back(poly[0]);
                            band_tris.push_back(poly[j]);
                            band_tris.push_back(poly[j + 1]);
                        }
                    }
                    if (!band_tris.empty()) {
                        add_gradient_fills(c, band_tris);
                    }
                }
            }
        } else if (const hatch::Pattern* pat = store.pattern(pname)) {
            // Line patterns (ANSI31 etc.): generate the clipped line families here
            // (derived-not-baked) and route them into the colour/lineweight line groups, so
            // they batch like every other line and plot as PDF vectors with no special case.
            hpsegs.clear();
            hatch::generate_pattern_segments(store.hatch_loops(*hd), *pat, store.hatch_pattern_scale(*hd),
                                             hd->pattern_angle, hd->pattern_origin, hpsegs);
            for (const hatch::Segment& sg : hpsegs) {
                add_line(r.color, r.lineweight, sg.a, sg.b);
            }
        }
        // An unknown pattern name renders nothing (the entity stays selectable by region).
    });

    // INSERT (block reference): resolve the definition x transform to world segments
    // (the same one path pick/bounds use) and route them into the colour/lineweight
    // line groups. N identical inserts add vertices but NOT draw calls -- they batch by
    // (colour, lineweight) like every other line. Geometry lives once in the definition;
    // nothing is baked into the store per instance (Ph16/23 derived-not-baked).
    std::vector<InsertSeg> isegs;
    std::vector<InsertFill> ifills;
    for_each_live(store.inserts(), EntityKind::Insert, [&](EntityHandle h) {
        const InsertData* in = store.insert(h);
        if (!visible(store, in->props)) {
            return;
        }
        isegs.clear();
        ifills.clear();
        resolve_insert(store, *in, tolerance, isegs, &ifills);
        for (const InsertSeg& s : isegs) {
            add_line(s.color, s.lineweight, s.a, s.b);
        }
        for (const InsertFill& f : ifills) {
            Group& g = fill_groups[color_key(f.color)];
            g.v.insert(g.v.end(), {f.a, f.b, f.c});
            g.d.insert(g.d.end(), 3, t_depth);
        }
        // A reference with attributes is an editor target (EATTEDIT / double-click): its
        // extent for the hit-test and its values, defaults filled in.
        const BlockDef* bd = store.block(in->block);
        if (bd != nullptr && !bd->content.attdefs.empty() && editable(store, in->props)) {
            AttribEditTarget t;
            t.handle = h;
            t.block = in->block;
            t.min = in->pos;
            t.max = in->pos;
            for (const InsertSeg& s : isegs) {
                t.min = {std::min({t.min.x, s.a.x, s.b.x}), std::min({t.min.y, s.a.y, s.b.y})};
                t.max = {std::max({t.max.x, s.a.x, s.b.x}), std::max({t.max.y, s.a.y, s.b.y})};
            }
            t.values = store.insert_attribs(*in);
            for (std::size_t i = t.values.size(); i < bd->content.attdefs.size(); ++i) {
                t.values.push_back(bd->content.attdefs[i].def);
            }
            t.values.resize(bd->content.attdefs.size());
            out.attrib_edit_targets.push_back(std::move(t));
        }
    });

    // Paper-space viewports: the frame in the entity's colour, then model space seen
    // through it -- built once as its own snapshot, then mapped into the sheet and
    // clipped to the frame. Nothing is copied into the layout: it is derived here, so
    // a model edit shows in every viewport on the next rebuild.
    if (with_viewports && space != 0) {
        std::vector<EntityHandle> vps;
        for_each_live(store.viewports(), EntityKind::Viewport, [&](EntityHandle h) {
            if (visible(store, store.viewport(h)->props)) {
                vps.push_back(h);
            }
        });
        if (!vps.empty()) {
            RenderSnapshot model;
            build_render_snapshot_in(store, kernel, model, tolerance, ltscale, 0, false);
            t_build_space = space; // the model build switched it
            t_orders = &orders;
            t_depth_scale = depth_scale;
            std::vector<Vec2> tris;
            std::vector<Vec2> poly;
            for (const EntityHandle h : vps) {
                const ViewportData* v = store.viewport(h);
                // VPLAYER: a viewport with its own frozen layers gets its own model build;
                // so does one whose scale is not 1:1 when PSLTSCALE is on -- the dashes are
                // then LTSCALE long on the sheet, which is LTSCALE / scale in the model.
                RenderSnapshot own;
                const RenderSnapshot* model_src = &model;
                const bool ps_scaled = store.psltscale() && v->scale > 0.0 &&
                                       std::abs(v->scale - 1.0) > 1e-9;
                if (!v->frozen_layers.empty() || ps_scaled) {
                    t_vp_frozen = v->frozen_layers.empty() ? nullptr : &v->frozen_layers;
                    build_render_snapshot_in(store, kernel, own, tolerance,
                                             ps_scaled ? ltscale / v->scale : ltscale, 0, false);
                    t_vp_frozen = nullptr;
                    t_build_space = space;
                    t_orders = &orders;
                    t_depth_scale = depth_scale;
                    model_src = &own;
                }
                const RenderSnapshot& mdl = *model_src;
                (void)visible(store, v->props); // the frame's own layer transparency (a model build ran since)
                const ResolvedProps rp = entity_resolved(store, v->props);
                const double x0 = v->center.x - v->width * 0.5;
                const double x1 = v->center.x + v->width * 0.5;
                const double y0 = v->center.y - v->height * 0.5;
                const double y1 = v->center.y + v->height * 0.5;
                add_line(rp.color, rp.lineweight, {x0, y0}, {x1, y0});
                add_line(rp.color, rp.lineweight, {x1, y0}, {x1, y1});
                add_line(rp.color, rp.lineweight, {x1, y1}, {x0, y1});
                add_line(rp.color, rp.lineweight, {x0, y1}, {x0, y0});
                if (!v->on || !(v->scale > 0.0)) {
                    continue;
                }
                const double sc = v->scale;
                const auto to_paper = [&](Vec2 m) {
                    return Vec2{v->center.x + (m.x - v->view_center.x) * sc,
                                v->center.y + (m.y - v->view_center.y) * sc};
                };
                // The model's draw order behind the sheet's own objects (the farther half).
                const auto model_depth = [](const std::vector<float>& d, std::size_t i) {
                    return 0.5f + 0.5f * (i < d.size() ? d[i] : 0.5f);
                };
                for (const ColorBatch& b : mdl.line_batches) {
                    t_alpha = b.alpha; // the model's own transparency, through the viewport
                    for (std::uint32_t s = b.first; s < b.first + b.count; ++s) {
                        Vec2 a = to_paper(mdl.line_vertices[2 * static_cast<std::size_t>(s)]);
                        Vec2 c = to_paper(mdl.line_vertices[2 * static_cast<std::size_t>(s) + 1]);
                        if (clip_segment(a, c, x0, y0, x1, y1)) {
                            t_depth = model_depth(mdl.line_depth, s);
                            add_line(b.color, b.lineweight, a, c, b.is_text, static_cast<double>(b.text_height) * sc);
                        }
                    }
                }
                for (const ColorBatch& b : mdl.point_batches) {
                    t_alpha = b.alpha;
                    for (std::uint32_t i = b.first; i < b.first + b.count; ++i) {
                        const Vec2 p = to_paper(mdl.points[i]);
                        if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1) {
                            t_depth = model_depth(mdl.point_depth, i);
                            add_point(b.color, p);
                        }
                    }
                }
                // Each triangle mapped and clipped, at its own depth.
                const auto clipped_tri = [&](const std::vector<Vec2>& src, std::uint32_t i, std::vector<Vec2>& dst) {
                    dst.clear();
                    poly = {to_paper(src[i]), to_paper(src[i + 1]), to_paper(src[i + 2])};
                    poly = clip_polygon(poly, x0, y0, x1, y1);
                    for (std::size_t j = 1; j + 1 < poly.size(); ++j) {
                        dst.push_back(poly[0]);
                        dst.push_back(poly[j]);
                        dst.push_back(poly[j + 1]);
                    }
                };
                for (const ColorBatch& b : mdl.fill_batches) {
                    t_alpha = b.alpha;
                    for (std::uint32_t i = b.first; i + 2 < b.first + b.count; i += 3) {
                        clipped_tri(mdl.fill_vertices, i, tris);
                        if (tris.empty()) {
                            continue;
                        }
                        t_depth = model_depth(mdl.fill_depth, i);
                        if (b.computed_color) {
                            add_gradient_fills(b.color, tris);
                        } else {
                            add_fills(b.color, tris);
                        }
                    }
                }
                for (std::uint32_t i = 0; i + 2 < mdl.wipeout_vertices.size(); i += 3) {
                    clipped_tri(mdl.wipeout_vertices, i, tris);
                    out.wipeout_vertices.insert(out.wipeout_vertices.end(), tris.begin(), tris.end());
                    out.wipeout_depth.insert(out.wipeout_depth.end(), tris.size(), model_depth(mdl.wipeout_depth, i));
                }
                for (const ImageInstance& im : mdl.images) {
                    ImageInstance inst = im;
                    inst.depth = 0.5f + 0.5f * im.depth;
                    bool inside = true;
                    for (Vec2& q : inst.quad) {
                        q = to_paper(q);
                        inside = inside && q.x >= x0 && q.x <= x1 && q.y >= y0 && q.y <= y1;
                    }
                    if (inside) { // a raster is shown whole or not at all (no quad clipping yet)
                        out.images.push_back(inst);
                    }
                }
            }
        }
    }

    // Flatten groups into contiguous batches over the payload arrays.
    const auto alpha_of = [](std::uint64_t transparency) { return static_cast<std::uint8_t>(255 - (transparency & 0xFFu)); };
    for (auto& [key, g] : line_groups) {
        ColorBatch b{unpack_rgb(static_cast<std::uint32_t>((key >> 9) & 0xFFFFFFu)),
                     static_cast<std::uint32_t>(out.line_vertices.size() / 2),
                     static_cast<std::uint32_t>(g.v.size() / 2),
                     static_cast<std::uint8_t>(key & 0xff), ((key >> 8) & 1u) != 0,
                     static_cast<float>((key >> 33) & 0xFFFFu) / 8.0f};
        b.alpha = alpha_of(key >> 49);
        out.line_batches.push_back(b);
        out.line_vertices.insert(out.line_vertices.end(), g.v.begin(), g.v.end());
        out.line_depth.insert(out.line_depth.end(), g.d.begin(), g.d.end());
    }
    for (auto& [key, g] : point_groups) {
        ColorBatch b{unpack_rgb(key & 0xFFFFFFu), static_cast<std::uint32_t>(out.points.size()),
                     static_cast<std::uint32_t>(g.v.size()), 0};
        b.alpha = alpha_of(key >> 24);
        out.point_batches.push_back(b);
        out.points.insert(out.points.end(), g.v.begin(), g.v.end());
        out.point_depth.insert(out.point_depth.end(), g.d.begin(), g.d.end());
    }
    for (auto& [key, g] : fill_groups) {
        ColorBatch b{unpack_rgb(key & 0xFFFFFFu), static_cast<std::uint32_t>(out.fill_vertices.size()),
                     static_cast<std::uint32_t>(g.v.size()), 0};
        b.alpha = alpha_of(key >> 24);
        out.fill_batches.push_back(b);
        out.fill_vertices.insert(out.fill_vertices.end(), g.v.begin(), g.v.end());
        out.fill_depth.insert(out.fill_depth.end(), g.d.begin(), g.d.end());
    }
    for (auto& [key, g] : gradient_groups) {
        ColorBatch b{unpack_rgb(key & 0xFFFFFFu), static_cast<std::uint32_t>(out.fill_vertices.size()),
                     static_cast<std::uint32_t>(g.v.size()), 0};
        b.alpha = alpha_of(key >> 24);
        b.computed_color = true;
        out.fill_batches.push_back(b);
        out.fill_vertices.insert(out.fill_vertices.end(), g.v.begin(), g.v.end());
        out.fill_depth.insert(out.fill_depth.end(), g.d.begin(), g.d.end());
    }
    t_orders = nullptr; // `orders` ends here (a nested build's caller points it back)

    // World-space bounds of live geometry (for ZOOM extents).
    out.has_bounds = false;
    const auto extend = [&](const Vec2& p) {
        if (!out.has_bounds) {
            out.bounds_min = p;
            out.bounds_max = p;
            out.has_bounds = true;
        } else {
            out.bounds_min = {std::min(out.bounds_min.x, p.x), std::min(out.bounds_min.y, p.y)};
            out.bounds_max = {std::max(out.bounds_max.x, p.x), std::max(out.bounds_max.y, p.y)};
        }
    };
    for (const Vec2& p : out.points) {
        extend(p);
    }
    for (const Vec2& p : out.line_vertices) {
        extend(p);
    }
    // Images contribute no line/fill vertices, so their corners must be folded in
    // explicitly -- otherwise an image-only drawing has NO bounds and both ZOOM extents
    // and `--plot --extents` would report nothing to draw.
    for (const ImageInstance& im : out.images) {
        for (const Vec2& c : im.quad) {
            extend(c);
        }
    }
    for (const Vec2& p : out.fill_vertices) {
        extend(p);
    }
}

} // namespace musacad::core

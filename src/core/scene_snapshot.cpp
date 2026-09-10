// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/scene_snapshot.hpp"

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
#include "musacad/core/text/mtext.hpp"
#include "musacad/core/text/stroke_font.hpp"
#include "musacad/core/text/text_codes.hpp"

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
std::uint64_t line_key(Rgb c, std::uint8_t lw, bool is_text, std::uint64_t height_bucket) {
    return (height_bucket << 33) | (static_cast<std::uint64_t>(pack_rgb(c)) << 9) |
           (static_cast<std::uint64_t>(is_text) << 8) | lw;
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
bool visible(const GeometryStore& store, const EntityProps& p) {
    const Layer* l = store.layer(p.layer);
    return l != nullptr && l->on && !l->frozen && p.space() == store.active_space();
}
/// Editable = visible and not on a locked layer (locked text can't be edited).
bool editable(const GeometryStore& store, const EntityProps& p) {
    const Layer* l = store.layer(p.layer);
    return l != nullptr && l->on && !l->frozen && !l->locked && p.space() == store.active_space();
}
ResolvedProps entity_resolved(const GeometryStore& store, const EntityProps& p) {
    const Layer* l = store.layer(p.layer);
    return l != nullptr ? resolve(p, *l) : ResolvedProps{};
}

} // namespace

void build_render_snapshot(const GeometryStore& store, const IGeometryKernel& kernel,
                           RenderSnapshot& out, double tolerance, double ltscale) {
    const IFontEngine* fonts = store.font_engine();
    out.points.clear();
    out.line_vertices.clear();
    out.construction_lines.clear();
    out.line_batches.clear();
    out.point_batches.clear();
    out.fill_vertices.clear();
    out.fill_batches.clear();
    out.wipeout_vertices.clear();
    out.images.clear();
    out.text_edit_targets.clear();
    out.layers.assign(store.layers().begin(), store.layers().end());
    out.current_layer = store.current_layer();
    out.page_setups = store.page_setups();
    out.wipeout_frames = store.wipeout_frames();
    out.active_space = store.active_space();
    out.layouts.clear();
    for (const Layout& l : store.layouts()) {
        out.layouts.push_back(LayoutInfo{l.id, l.name, l.page.paper_w_mm, l.page.paper_h_mm,
                                         l.page.landscape});
    }

    // Lines group by (colour, lineweight); points and fills by colour.
    std::map<std::uint64_t, std::vector<Vec2>> line_groups;
    std::map<std::uint32_t, std::vector<Vec2>> point_groups;
    std::map<std::uint32_t, std::vector<Vec2>> fill_groups;
    std::map<std::uint32_t, std::vector<Vec2>> gradient_groups; // computed colours (GRADIENT bands)

    const auto add_line = [&](Rgb c, std::uint8_t lw, Vec2 a, Vec2 b, bool is_text = false,
                              double text_height = 0.0) {
        auto& v = line_groups[line_key(c, lw, is_text, height_bucket(text_height))];
        v.push_back(a);
        v.push_back(b);
    };
    const auto add_lines = [&](Rgb c, std::uint8_t lw, const std::vector<Vec2>& segs,
                               bool is_text = false, double text_height = 0.0) {
        auto& v = line_groups[line_key(c, lw, is_text, height_bucket(text_height))];
        v.insert(v.end(), segs.begin(), segs.end());
    };
    const auto add_fills = [&](Rgb c, const std::vector<Vec2>& tris) {
        auto& v = fill_groups[pack_rgb(c)];
        v.insert(v.end(), tris.begin(), tris.end());
    };
    const auto add_gradient_fills = [&](Rgb c, const std::vector<Vec2>& tris) {
        auto& v = gradient_groups[pack_rgb(c)];
        v.insert(v.end(), tris.begin(), tris.end());
    };

    // The two coexisting text paths share one layout; only the per-glyph geometry source
    // differs. An outline (TTF/OTF) font emits FILLED triangles; otherwise the built-in
    // stroke font emits LINE segments. Glyphs are generated here, never baked.
    std::vector<Vec2> trun;
    const auto font_is_outline = [&](std::uint16_t font_id) {
        return fonts != nullptr && fonts->is_outline_font(store.font_name(font_id));
    };
    // `lineweight` is the entity's RESOLVED weight (ByLayer already applied). Stroke
    // glyphs carry it into the line batches exactly like any other stroked geometry, so
    // PLOT prints text at the weight the author set. The viewport is unaffected: it
    // ignores `lineweight` for `is_text` batches and applies its own screen weight
    // (Ph31), so the on-screen polish and the paper result stay independent.
    const auto emit_text_run = [&](std::string_view raw, Vec2 origin, double height,
                                   double rotation, text::Justify justify, std::uint16_t font_id,
                                   Rgb color, std::uint8_t lineweight,
                                   const TextStyle* ts = nullptr) {
        const double wf = ts != nullptr ? ts->width_factor : 1.0;
        const double ob = ts != nullptr ? ts->oblique : 0.0;
        // Expand control codes at render time (storage stays raw). `vis` drives every metric
        // and glyph; the over/under-line toggles become decoration bars below.
        const text::SubstitutedText sub = text::substitute_text_codes(raw);
        const std::string_view str = sub.text;
        const std::string_view fname = store.font_name(font_id);
        const bool outline = fonts != nullptr && fonts->is_outline_font(fname);
        const auto measure = [&](std::string_view s) {
            return outline ? fonts->advance(fname, s, height) : text::text_width(s, height);
        };
        double off = 0.0; // justification along the baseline
        if (justify != text::Justify::Left) {
            const double w = measure(str);
            off = justify == text::Justify::Center ? -w * 0.5 : -w;
        }
        const double cs = std::cos(rotation);
        const double sn = std::sin(rotation);
        const Vec2 o{origin.x + off * cs, origin.y + off * sn};
        if (outline) {
            trun.clear();
            fonts->glyph_fills(fname, str, o, height, rotation, trun);
            text::apply_text_style(trun, origin, rotation, wf, ob); // STYLE width/oblique
            add_fills(color, trun);
        } else {
            trun.clear();
            text::append_text_segments(str, o, height, rotation, text::Justify::Left, trun);
            text::apply_text_style(trun, origin, rotation, wf, ob);
            add_lines(color, lineweight, trun, /*is_text=*/true, height);
        }
        // Overline (%%o) / underline (%%u): a horizontal stroke spanning each toggled run,
        // placed just above the cap height / just below the baseline, in the text colour.
        if (sub.has_decor()) {
            const auto bar = [&](const std::vector<text::DecorSpan>& spans, double ly) {
                for (const text::DecorSpan& s : spans) {
                    const double x0 = measure(str.substr(0, s.begin));
                    const double x1 = measure(str.substr(0, s.end));
                    std::vector<Vec2> bar_pts{{o.x + x0 * cs - ly * sn, o.y + x0 * sn + ly * cs},
                                              {o.x + x1 * cs - ly * sn, o.y + x1 * sn + ly * cs}};
                    text::apply_text_style(bar_pts, origin, rotation, wf, ob);
                    add_line(color, lineweight, bar_pts[0], bar_pts[1], /*is_text=*/true, height);
                }
            };
            bar(sub.overline, height * 1.15);
            bar(sub.underline, -height * 0.22);
        }
    };

    for_each_live(store.points(), EntityKind::Point, [&](EntityHandle h) {
        const PointData* d = store.point(h);
        if (visible(store, d->props)) {
            point_groups[pack_rgb(entity_resolved(store, d->props).color)].push_back(d->p);
        }
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
        dash_polyline(seg, r.linetype, ltscale * store.celtscale(h), dashed);
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
        dash_polyline(tess, r.linetype, ltscale * store.celtscale(h), dashed);
        add_lines(r.color, r.lineweight, dashed);
    };
    for_each_live(store.circles(), EntityKind::Circle,
                  [&](EntityHandle h) { emit_curve(h, store.circle(h)->props); });
    for_each_live(store.arcs(), EntityKind::Arc,
                  [&](EntityHandle h) { emit_curve(h, store.arc(h)->props); });
    for_each_live(store.polylines(), EntityKind::Polyline,
                  [&](EntityHandle h) { emit_curve(h, store.polyline(h)->props); });
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
        const TextStyle& tstyle = store.text_style_of(*t);
        emit_text_run(store.string_of(*t), t->pos, t->height, t->rotation,
                      static_cast<text::Justify>(t->justify), t->font, r.color, r.lineweight,
                      &tstyle);
        if (editable(store, t->props)) {
            // The selection box uses the VISIBLE width (codes expanded); the edit target
            // keeps the RAW string so double-click editing shows %%c50, not the symbol.
            const std::string vis = text::substitute_text(store.string_of(*t));
            const double w = (font_is_outline(t->font)
                                 ? fonts->advance(store.font_name(t->font), vis, t->height)
                                 : text::text_width(vis, t->height)) *
                             tstyle.width_factor; // the style's width factor
            out.text_edit_targets.push_back(TextEditTarget{
                h, t->pos, {t->pos.x, t->pos.y}, {t->pos.x + w, t->pos.y + t->height}, t->height,
                t->rotation, false, std::string(store.string_of(*t))});
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
        const TextStyle& tstyle = store.text_style_of(a->text);
        emit_text_run(store.string_of(a->text), a->text.pos, a->text.height, a->text.rotation,
                      static_cast<text::Justify>(a->text.justify), a->text.font, r.color,
                      r.lineweight, &tstyle);
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
        text::append_text_segments(g.label, g.text_pos, g.text_height, g.text_rotation,
                                   g.text_justify, tseg);
        // The Limits tolerance mode stacks the lower limit under the upper; every other
        // mode leaves label2 empty, so this is a no-op for them.
        if (!g.label2.empty()) {
            text::append_text_segments(g.label2, g.label2_pos, g.text_height, g.text_rotation,
                                       g.text_justify, tseg);
        }
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
        out.images.push_back(inst);
        if (store.image_frame() != 0) {
            // IMAGEFRAME: the clipped quad's outline in the entity's colour.
            const ResolvedProps r = entity_resolved(store, im->props);
            for (std::size_t i = 0; i < 4; ++i) {
                add_line(r.color, r.lineweight, q[i], q[(i + 1) % 4]);
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
        add_lines(r.color, r.lineweight, lay.segments, /*is_text=*/true, m->text.height);
        add_fills(r.color, lay.fills);
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
        for (std::size_t i = 1; i < v.size(); ++i) {
            add_line(r.color, r.lineweight, v[i - 1], v[i]);
        }
        if (v.size() >= 2) {
            afill.clear();
            aline.clear();
            append_arrowhead(afill, aline, v[0], v[1] - v[0], s.arrow_size,
                             static_cast<ArrowType>(s.arrow_type));
            add_fills(arrow_c, afill);
            add_lines(arrow_c, r.lineweight, aline);
        }
        const text::MTextLayout lay = text::layout_mtext(m->text, store.string_of(m->text), fonts,
                                                         store.font_name(m->text.font));
        add_lines(text_c, r.lineweight, lay.segments, /*is_text=*/true, m->text.height);
        add_fills(text_c, lay.fills);
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
        } else if (const hatch::Pattern* pat = hatch::builtin_pattern(pname)) {
            // Line patterns (ANSI31 etc.): generate the clipped line families here
            // (derived-not-baked) and route them into the colour/lineweight line groups, so
            // they batch like every other line and plot as PDF vectors with no special case.
            hpsegs.clear();
            hatch::generate_pattern_segments(store.hatch_loops(*hd), *pat, hd->pattern_scale,
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
    for_each_live(store.inserts(), EntityKind::Insert, [&](EntityHandle h) {
        const InsertData* in = store.insert(h);
        if (!visible(store, in->props)) {
            return;
        }
        isegs.clear();
        resolve_insert(store, *in, tolerance, isegs);
        for (const InsertSeg& s : isegs) {
            add_line(s.color, s.lineweight, s.a, s.b);
        }
    });

    // Flatten groups into contiguous batches over the payload arrays.
    for (auto& [key, verts] : line_groups) {
        out.line_batches.push_back(
            ColorBatch{unpack_rgb(static_cast<std::uint32_t>(key >> 9)),
                       static_cast<std::uint32_t>(out.line_vertices.size() / 2),
                       static_cast<std::uint32_t>(verts.size() / 2),
                       static_cast<std::uint8_t>(key & 0xff), ((key >> 8) & 1u) != 0,
                       static_cast<float>((key >> 33) & 0xFFFFu) / 8.0f});
        out.line_vertices.insert(out.line_vertices.end(), verts.begin(), verts.end());
    }
    for (auto& [key, pts] : point_groups) {
        out.point_batches.push_back(ColorBatch{unpack_rgb(key),
                                               static_cast<std::uint32_t>(out.points.size()),
                                               static_cast<std::uint32_t>(pts.size()), 0});
        out.points.insert(out.points.end(), pts.begin(), pts.end());
    }
    for (auto& [key, verts] : fill_groups) {
        out.fill_batches.push_back(ColorBatch{unpack_rgb(key),
                                              static_cast<std::uint32_t>(out.fill_vertices.size()),
                                              static_cast<std::uint32_t>(verts.size()), 0});
        out.fill_vertices.insert(out.fill_vertices.end(), verts.begin(), verts.end());
    }
    for (auto& [key, verts] : gradient_groups) {
        ColorBatch b{unpack_rgb(key), static_cast<std::uint32_t>(out.fill_vertices.size()),
                     static_cast<std::uint32_t>(verts.size()), 0};
        b.computed_color = true;
        out.fill_batches.push_back(b);
        out.fill_vertices.insert(out.fill_vertices.end(), verts.begin(), verts.end());
    }

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

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/grips.hpp"

#include "musacad/core/ellipse.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <variant>

#include "musacad/core/dimension.hpp"
#include "musacad/core/image.hpp"
#include "musacad/core/table.hpp"
#include "musacad/core/text/mtext.hpp"

namespace musacad::core {

Command capture_entity(const GeometryStore& store, EntityHandle h) {
    switch (h.kind) {
    case EntityKind::Line: {
        const LineData* l = store.line(h);
        return AddLineCommand{l->a, l->b, 0, l->props, store.celtscale(h)};
    }
    case EntityKind::Circle: {
        const CircleData* c = store.circle(h);
        return AddCircleCommand{c->center, c->radius, 0, c->props, store.celtscale(h)};
    }
    case EntityKind::Arc: {
        const ArcData* a = store.arc(h);
        return AddArcCommand{a->center,   a->radius, a->start_angle, a->end_angle, 0,
                             a->props,    store.celtscale(h)};
    }
    case EntityKind::Polyline: {
        const PolylineData* p = store.polyline(h);
        const auto verts = store.vertices_of(*p);
        const auto bulges = store.bulges_of(*p);
        return AddPolylineCommand{std::vector<Vec2>(verts.begin(), verts.end()),
                                  p->closed,
                                  0,
                                  p->props,
                                  std::vector<double>(bulges.begin(), bulges.end()),
                                  store.celtscale(h)};
    }
    case EntityKind::Text: {
        const TextData* t = store.text(h);
        const TextStyle& ts = store.text_style_of(*t);
        return AddTextCommand{t->pos,
                              t->height,
                              t->rotation,
                              t->justify,
                              std::string(store.string_of(*t)),
                              0,
                              t->props,
                              std::string(store.font_name(t->font)),
                              t->style == 0 ? std::string{} : ts.name};
    }
    case EntityKind::AttDef: {
        const AttDefData* a = store.attdef(h);
        const TextStyle& ts = store.text_style_of(a->text);
        AddAttDefCommand c;
        c.text = AddTextCommand{a->text.pos,
                                a->text.height,
                                a->text.rotation,
                                a->text.justify,
                                std::string(store.string_of(a->text)),
                                0,
                                a->text.props,
                                std::string(store.font_name(a->text.font)),
                                a->text.style == 0 ? std::string{} : ts.name};
        c.prompt = std::string(store.attdef_prompt(*a));
        c.def = std::string(store.attdef_default(*a));
        c.flags = a->flags;
        return c;
    }
    case EntityKind::Dimension: {
        const DimData* d = store.dimension(h);
        const DimStyle* st = store.dimstyle(d->style);
        return AddDimensionCommand{static_cast<std::uint8_t>(d->type),
                                   d->a,
                                   d->b,
                                   d->line_pt,
                                   d->style,
                                   0,
                                   d->props,
                                   d->overrides,
                                   st != nullptr ? *st : DimStyle{},
                                   std::string(store.dim_prefix(*d)),
                                   std::string(store.dim_suffix(*d)),
                                   d->tol,
                                   std::string(store.dim_override(*d)),
                                   d->text_offset,
                                   d->aux};
    }
    case EntityKind::Leader: {
        const LeaderData* l = store.leader(h);
        const DimStyle* lst = store.dimstyle(l->style);
        return AddLeaderCommand{l->tip,
                                l->knee,
                                l->text_height,
                                l->style,
                                std::string(store.string_of(*l)),
                                0,
                                l->props,
                                std::string(store.font_name(l->font)),
                                l->overrides,
                                lst != nullptr ? *lst : DimStyle{}};
    }
    case EntityKind::MText: {
        const MTextData* m = store.mtext(h);
        return AddMTextCommand{m->text, std::string(store.string_of(m->text)), 0, m->props,
                               std::string(store.font_name(m->text.font))};
    }
    case EntityKind::MLeader: {
        const MLeaderData* m = store.mleader(h);
        const auto v = store.vertices_of(*m);
        const DimStyle* mst = store.dimstyle(m->style);
        return AddMLeaderCommand{std::vector<Vec2>(v.begin(), v.end()),
                                 m->style,
                                 m->text,
                                 std::string(store.string_of(m->text)),
                                 0,
                                 m->props,
                                 std::string(store.font_name(m->text.font)),
                                 m->overrides,
                                 mst != nullptr ? *mst : DimStyle{}};
    }
    case EntityKind::Insert: {
        const InsertData* in = store.insert(h);
        AddInsertCommand c{in->block, in->pos, in->scale_x, in->scale_y, in->rotation, 0, in->props, {}};
        c.attribs = store.insert_attribs(*in);
        return c;
    }
    case EntityKind::Hatch: {
        const HatchData* hd = store.hatch(h);
        return AddHatchCommand{store.hatch_loops(*hd),
                               std::string(store.string_of(*hd)),
                               hd->pattern_scale,
                               hd->pattern_angle,
                               hd->pattern_origin,
                               0,
                               hd->props,
                               hd->color2};
    }
    // GD&T: capture is authoritative for the overrides and carries a style SNAPSHOT for
    // PR's effective-value display (ignored on recreate) -- exactly AddDimensionCommand.
    case EntityKind::Fcf: {
        const FcfData* f = store.fcf(h);
        std::vector<std::string> cells;
        cells.reserve(f->cell_count);
        for (const std::string_view c : store.fcf_cell_text(*f)) {
            cells.emplace_back(c);
        }
        const DimStyle* st = store.dimstyle(f->style);
        return AddFcfCommand{std::move(cells), f->pos,      f->rotation,   f->style, 0,
                             f->props,         f->overrides, st != nullptr ? *st : DimStyle{}};
    }
    case EntityKind::Datum: {
        const DatumData* d = store.datum(h);
        const DimStyle* st = store.dimstyle(d->style);
        return AddDatumCommand{std::string(store.string_of(*d)),
                               d->tip,
                               d->pos,
                               d->rotation,
                               d->style,
                               0,
                               d->props,
                               d->overrides,
                               st != nullptr ? *st : DimStyle{}};
    }
    case EntityKind::Table: {
        const TableData* td = store.table(h);
        AddTableCommand c;
        c.rows = td->rows;
        c.cols = td->cols;
        for (const TableCell& cell : store.table_cells(*td)) {
            c.cells.push_back(cell);
            c.texts.emplace_back(store.string_of(cell));
        }
        const std::span<const double> cw = store.table_col_widths(*td);
        const std::span<const double> rh = store.table_row_heights(*td);
        c.col_widths.assign(cw.begin(), cw.end());
        c.row_heights.assign(rh.begin(), rh.end());
        c.pos = td->pos;
        c.rotation = td->rotation;
        c.style = td->style;
        c.has_title = td->has_title;
        c.has_header = td->has_header;
        c.props = td->props;
        return c;
    }
    case EntityKind::Viewport: {
        const ViewportData* v = store.viewport(h);
        return AddViewportCommand{v->center, v->width, v->height, v->view_center, v->scale, v->on, 0, v->props};
    }
    case EntityKind::Image: {
        const ImageData* im = store.image(h);
        return AddImageCommand{im->def,     im->pos,     im->width,   im->height,
                               im->rotation, im->clipped, im->clip_u0, im->clip_v0,
                               im->clip_u1,  im->clip_v1, 0,           im->props};
    }
    case EntityKind::Point:
        return AddPointCommand{store.point(h)->p, 0, store.point(h)->props};
    case EntityKind::Xline: {
        const XlineData* x = store.xline(h);
        return AddXlineCommand{x->base, x->dir, x->ray, 0, x->props};
    }
    case EntityKind::Ellipse: {
        const EllipseData* e = store.ellipse(h);
        return AddEllipseCommand{e->center, e->major, e->ratio, e->start, e->end, 0, e->props};
    }
    case EntityKind::Spline: {
        const SplineData* sp = store.spline(h);
        const std::span<const Vec2> cp = store.control_points_of(*sp);
        return AddSplineCommand{std::vector<Vec2>(cp.begin(), cp.end()), sp->degree, 0, sp->props};
    }
    }
    return AddLineCommand{};
}

EntityHandle add_command_to_store(GeometryStore& store, const Command& cmd, EntityProps fallback) {
    EntityHandle handle;
    const auto props_of = [&](const std::optional<EntityProps>& p) { return p ? *p : fallback; };
    std::visit(
        [&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, AddPointCommand>) {
                handle = store.add_point(c.p, props_of(c.props));
            } else if constexpr (std::is_same_v<T, AddXlineCommand>) {
                handle = store.add_xline(c.base, c.dir, c.ray, props_of(c.props));
            } else if constexpr (std::is_same_v<T, AddEllipseCommand>) {
                handle = store.add_ellipse(c.center, c.major, c.ratio, c.start, c.end,
                                           props_of(c.props));
            } else if constexpr (std::is_same_v<T, AddSplineCommand>) {
                handle = store.add_spline(c.control_points, c.degree, props_of(c.props));
            } else if constexpr (std::is_same_v<T, AddLineCommand>) {
                handle = store.add_line(c.a, c.b, props_of(c.props));
                store.set_celtscale(handle, c.celtscale);
            } else if constexpr (std::is_same_v<T, AddPolylineCommand>) {
                handle = store.add_polyline(c.points, c.bulges, c.closed, props_of(c.props));
                store.set_celtscale(handle, c.celtscale);
            } else if constexpr (std::is_same_v<T, AddCircleCommand>) {
                handle = store.add_circle(c.center, c.radius, props_of(c.props));
                store.set_celtscale(handle, c.celtscale);
            } else if constexpr (std::is_same_v<T, AddArcCommand>) {
                handle =
                    store.add_arc(c.center, c.radius, c.start_angle, c.end_angle, props_of(c.props));
                store.set_celtscale(handle, c.celtscale);
            } else if constexpr (std::is_same_v<T, AddTextCommand>) {
                {

                    // STYLE: resolve the style name; its font applies when the command has none.

                    std::uint16_t style = 0;

                    std::string font = c.font;

                    if (!c.style.empty()) {

                        const std::uint16_t si = store.text_style_index(c.style);

                        if (si != 0xFFFF) {

                            style = si;

                            if (font.empty()) {

                                font = store.text_styles()[si].font;

                            }

                        }

                    }

                    handle = store.add_text(c.pos, c.height, c.rotation, c.justify, c.content,

                                            props_of(c.props), store.add_font(font), style);

                }
            } else if constexpr (std::is_same_v<T, AddAttDefCommand>) {
                std::uint16_t style = 0;
                std::string font = c.text.font;
                if (!c.text.style.empty()) {
                    const std::uint16_t si = store.text_style_index(c.text.style);
                    if (si != 0xFFFF) {
                        style = si;
                        if (font.empty()) {
                            font = store.text_styles()[si].font;
                        }
                    }
                }
                handle = store.add_attdef(c.text.pos, c.text.height, c.text.rotation, c.text.justify,
                                          c.text.content, c.prompt, c.def, c.flags,
                                          props_of(c.text.props), store.add_font(font), style);
            } else if constexpr (std::is_same_v<T, AddDimensionCommand>) {
                handle = store.add_dimension(static_cast<DimType>(c.type), c.a, c.b, c.line_pt,
                                             c.style, props_of(c.props), c.overrides, c.prefix,
                                             c.suffix, c.tol, c.text_override, c.text_offset);
                store.set_dim_aux(handle, c.aux);
            } else if constexpr (std::is_same_v<T, AddLeaderCommand>) {
                handle = store.add_leader(c.tip, c.knee, c.text_height, c.style, c.content,
                                          props_of(c.props), store.add_font(c.font), c.overrides);
            } else if constexpr (std::is_same_v<T, AddMTextCommand>) {
                MTextBlock b = c.block;
                b.font = store.add_font(c.font);
                handle = store.add_mtext(b, c.content, props_of(c.props));
            } else if constexpr (std::is_same_v<T, AddMLeaderCommand>) {
                MTextBlock b = c.block;
                b.font = store.add_font(c.font); // label font travels as a name (like MTEXT)
                handle = store.add_mleader(c.vertices, c.style, b, c.content, props_of(c.props),
                                           c.overrides);
            } else if constexpr (std::is_same_v<T, AddInsertCommand>) {
                handle = store.add_insert(c.block, c.pos, c.scale_x, c.scale_y, c.rotation,
                                          props_of(c.props), c.attribs);
            } else if constexpr (std::is_same_v<T, AddHatchCommand>) {
                handle = store.add_hatch(c.loops, c.pattern_name, c.pattern_scale, c.pattern_angle,
                                         c.pattern_origin, props_of(c.props), c.color2);
            } else if constexpr (std::is_same_v<T, AddFcfCommand>) {
                handle = store.add_fcf(c.cells, c.pos, c.rotation, c.style, props_of(c.props),
                                       c.overrides);
            } else if constexpr (std::is_same_v<T, AddDatumCommand>) {
                handle = store.add_datum(c.letter, c.tip, c.pos, c.rotation, c.style,
                                         props_of(c.props), c.overrides);
            } else if constexpr (std::is_same_v<T, AddTableCommand>) {
                // The command carries cell TEXT; add_table takes cells whose text is
                // already pooled, so intern each string first -- the same shape every
                // other pooled-string command uses.
                std::vector<TableCell> cells = c.cells;
                for (std::size_t i = 0; i < cells.size() && i < c.texts.size(); ++i) {
                    cells[i].str_offset = store.intern_string(c.texts[i]);
                    cells[i].str_len = static_cast<std::uint32_t>(c.texts[i].size());
                }
                handle = store.add_table(c.rows, c.cols, cells, c.col_widths, c.row_heights,
                                         c.pos, c.rotation, c.style, c.has_title, c.has_header,
                                         props_of(c.props));
            } else if constexpr (std::is_same_v<T, AddViewportCommand>) {
                handle = store.add_viewport(c.center, c.width, c.height, c.view_center, c.scale, c.on,
                                            props_of(c.props));
            } else if constexpr (std::is_same_v<T, AddImageCommand>) {
                handle = store.add_image(c.def, c.pos, c.width, c.height, c.rotation,
                                         props_of(c.props));
                if (ImageData* d = store.mutable_image(handle)) {
                    d->clipped = c.clipped;
                    d->clip_u0 = c.clip_u0;
                    d->clip_v0 = c.clip_v0;
                    d->clip_u1 = c.clip_u1;
                    d->clip_v1 = c.clip_v1;
                }
            }
        },
        cmd);
    return handle;
}

namespace {
void push(std::vector<Grip>& out, Vec2 p, GripKind k, std::uint32_t i) {
    out.push_back(Grip{p, k, i});
}
} // namespace

void grips_of(const GeometryStore& store, EntityHandle h, std::vector<Grip>& out) {
    switch (h.kind) {
    case EntityKind::Ellipse: {
        // Full ellipse: centre (move) + the four axis endpoints (major ends set the major
        // radius and rotation, minor ends set the ratio). Elliptical arc: centre, the two
        // ends (re-parameterise) and the midpoint (scales the ellipse through it) -- the
        // AutoCAD grip set.
        const EllipseData* e = store.ellipse(h);
        push(out, e->center, GripKind::Move, 0);
        if (ellipse::is_full(*e)) {
            push(out, ellipse::point_at(*e, 0.0), GripKind::Radius, 1);
            push(out, ellipse::point_at(*e, kPi), GripKind::Radius, 2);
            push(out, ellipse::point_at(*e, kHalfPi), GripKind::Radius, 3);
            push(out, ellipse::point_at(*e, kPi + kHalfPi), GripKind::Radius, 4);
        } else {
            const double sw = ellipse::sweep_of(*e);
            push(out, ellipse::point_at(*e, e->start), GripKind::Endpoint, 1);
            push(out, ellipse::point_at(*e, e->start + sw), GripKind::Endpoint, 2);
            push(out, ellipse::point_at(*e, e->start + sw * 0.5), GripKind::Radius, 3);
        }
        break;
    }
    case EntityKind::Xline: {
        // One grip at the root (base point): it moves the whole construction line. The
        // line is infinite, so there is no endpoint to grip; re-aiming is ROTATE's job.
        push(out, store.xline(h)->base, GripKind::Move, 0);
        break;
    }
    case EntityKind::Line: {
        const LineData* l = store.line(h);
        push(out, l->a, GripKind::Endpoint, 0);
        push(out, l->b, GripKind::Endpoint, 1);
        push(out, (l->a + l->b) * 0.5, GripKind::Move, 2); // midpoint moves the line
        break;
    }
    case EntityKind::Circle: {
        const CircleData* c = store.circle(h);
        push(out, c->center, GripKind::Move, 0);
        const double r = c->radius;
        push(out, {c->center.x + r, c->center.y}, GripKind::Radius, 1); // E
        push(out, {c->center.x, c->center.y + r}, GripKind::Radius, 2); // N
        push(out, {c->center.x - r, c->center.y}, GripKind::Radius, 3); // W
        push(out, {c->center.x, c->center.y - r}, GripKind::Radius, 4); // S
        break;
    }
    case EntityKind::Arc: {
        const ArcData* a = store.arc(h);
        push(out, a->center, GripKind::Move, 0);
        push(out, {a->center.x + a->radius * std::cos(a->start_angle),
                   a->center.y + a->radius * std::sin(a->start_angle)},
             GripKind::Endpoint, 1);
        push(out, {a->center.x + a->radius * std::cos(a->end_angle),
                   a->center.y + a->radius * std::sin(a->end_angle)},
             GripKind::Endpoint, 2);
        double sweep = a->end_angle - a->start_angle;
        while (sweep < 0.0) {
            sweep += kTwoPi;
        }
        if (sweep <= 0.0) {
            sweep = kTwoPi;
        }
        const double mid = a->start_angle + sweep * 0.5;
        push(out, {a->center.x + a->radius * std::cos(mid), a->center.y + a->radius * std::sin(mid)},
             GripKind::Radius, 3);
        break;
    }
    case EntityKind::Polyline: {
        const PolylineData* p = store.polyline(h);
        const auto verts = store.vertices_of(*p);
        const auto bulges = store.bulges_of(*p);
        for (std::uint32_t i = 0; i < verts.size(); ++i) {
            push(out, verts[i], GripKind::Vertex, i);
        }
        // AutoCAD's segment midpoint grips: on the arc itself for an arc segment.
        const std::uint32_t n = static_cast<std::uint32_t>(verts.size());
        const std::uint32_t nseg = n < 2 ? 0 : (p->closed ? n : n - 1);
        for (std::uint32_t s = 0; s < nseg; ++s) {
            const Vec2 a = verts[s];
            const Vec2 b = verts[(s + 1) % n];
            const double bulge = s < bulges.size() ? bulges[s] : 0.0;
            Vec2 mid{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5};
            if (bulge != 0.0) {
                // Sagitta = bulge * half-chord, to the right of the chord for a positive
                // (counter-clockwise) bulge.
                const Vec2 d = b - a;
                const double len = length(d);
                if (len > 1e-12) {
                    const Vec2 right{d.y / len, -d.x / len};
                    mid = mid + right * (bulge * len * 0.5);
                }
            }
            push(out, mid, GripKind::Segment, kSegmentGripBase + s);
        }
        break;
    }
    case EntityKind::Text: {
        const TextData* t = store.text(h);
        push(out, t->pos, GripKind::Move, 0);
        break;
    }
    case EntityKind::AttDef: {
        push(out, store.attdef(h)->text.pos, GripKind::Move, 0);
        break;
    }
    case EntityKind::Dimension: {
        const DimData* d = store.dimension(h);
        const DimStyle* s = store.dimstyle(d->style);
        const DimGeometry g =
            compute_dim_geometry(*d, s != nullptr ? *s : DimStyle{}, Rgb{}, store.dim_text_parts(*d));
        const auto t = d->type;
        if (t == DimType::Radius || t == DimType::Diameter || t == DimType::Jogged ||
            t == DimType::ArcLength) {
            push(out, d->a, GripKind::Move, 0);    // centre -> moves the dim
            push(out, d->b, GripKind::DimDef, 1);  // edge / arc start -> re-measures
            push(out, d->line_pt, GripKind::DimLine, 2); // placement / centre override
        } else if (t == DimType::Ordinate) {
            push(out, d->a, GripKind::DimDef, 0);  // the feature (re-measures)
            push(out, d->b, GripKind::DimLine, 1); // the leader endpoint
        } else if (t == DimType::Angular) {
            push(out, d->a, GripKind::DimDef, 0);  // vertex
            push(out, d->b, GripKind::DimDef, 1);  // ray 1
            push(out, d->line_pt, GripKind::DimDef, 2); // ray 2
        } else { // Linear / Aligned
            // Full grip set: both extension-line origins (def points, re-measure),
            // both dim-line ends (feet), and the offset midpoint. Indices >= 2 all
            // slide the dim line to the cursor (edit_for_grip_drag maps them to
            // line_pt), so the dimension is grabbable anywhere and freely placeable.
            push(out, d->a, GripKind::DimDef, 0); // ext-line origin a
            push(out, d->b, GripKind::DimDef, 1); // ext-line origin b
            if (g.dim_lines.size() >= 2) {
                push(out, g.dim_lines[0], GripKind::DimLine, 2); // dim-line foot a
                push(out, g.dim_lines[1], GripKind::DimLine, 3); // dim-line foot b
                push(out, (g.dim_lines[0] + g.dim_lines[1]) * 0.5, GripKind::DimLine,
                     4); // offset / placement
            } else {
                push(out, d->line_pt, GripKind::DimLine, 2);
            }
        }
        // The TEXT grip (issue #21), on every dimension type. Its index is a sentinel
        // outside the contiguous range above, so the per-type grip sets can grow without
        // ever colliding with it. Sits at the label's baseline-left corner, i.e. on the
        // text the user is reaching for.
        {
            Vec2 q[4];
            if (dim_label_quad(g, /*second=*/false, q)) {
                push(out, (q[0] + q[2]) * 0.5, GripKind::Move, DimData::kTextGripIndex);
            }
        }
        break;
    }
    case EntityKind::Leader: {
        const LeaderData* l = store.leader(h);
        push(out, l->tip, GripKind::Endpoint, 0);
        push(out, l->knee, GripKind::Move, 1);
        break;
    }
    case EntityKind::MText: {
        const MTextData* m = store.mtext(h);
        push(out, m->text.pos, GripKind::Move, 0); // insertion / attachment
        const Vec2 xdir{std::cos(m->text.rotation), std::sin(m->text.rotation)};
        if (m->text.width > 0.0) {
            push(out, m->text.pos + xdir * m->text.width, GripKind::DimLine, 1); // width grip
        } else {
            const text::MTextLayout lay = text::layout_mtext(
                m->text, store.string_of(m->text), store.font_engine(),
                store.font_name(m->text.font));
            push(out, {lay.max.x, (lay.min.y + lay.max.y) * 0.5}, GripKind::DimLine, 1);
        }
        break;
    }
    case EntityKind::MLeader: {
        const MLeaderData* m = store.mleader(h);
        const auto v = store.vertices_of(*m);
        for (std::uint32_t i = 0; i < v.size(); ++i) {
            push(out, v[i], i == 0 ? GripKind::Endpoint : GripKind::Vertex, i);
        }
        push(out, m->text.pos, GripKind::Move, static_cast<std::uint32_t>(v.size())); // text
        break;
    }
    case EntityKind::Insert: {
        const InsertData* in = store.insert(h);
        push(out, in->pos, GripKind::Move, 0); // insertion point moves the instance
        break;
    }
    case EntityKind::Viewport: {
        // Centre moves; a corner resizes against the opposite corner.
        const ViewportData* v = store.viewport(h);
        const double hw = v->width * 0.5;
        const double hh = v->height * 0.5;
        push(out, v->center, GripKind::Move, 0);
        push(out, {v->center.x - hw, v->center.y - hh}, GripKind::Vertex, 1);
        push(out, {v->center.x + hw, v->center.y - hh}, GripKind::Vertex, 2);
        push(out, {v->center.x + hw, v->center.y + hh}, GripKind::Vertex, 3);
        push(out, {v->center.x - hw, v->center.y + hh}, GripKind::Vertex, 4);
        break;
    }
    case EntityKind::Image: {
        // Insertion point moves; the opposite corner scales. Both are parameters of the
        // placement, so the quad stays derived rather than becoming a stored polygon.
        const ImageData* im = store.image(h);
        const ImageQuad q = resolve_image_quad(*im);
        push(out, q[0], GripKind::Move, 0);
        push(out, q[2], GripKind::Vertex, 1);
        break;
    }
    case EntityKind::Table: {
        // Insertion point (top-left) moves the table; a grip on each interior column
        // boundary resizes that column, and one on each interior row boundary resizes
        // that row. Both axes are stored per-table (col_widths / row_heights) and both
        // are what compute_table_geometry lays the grid out from, so both are draggable.
        const TableData* td = store.table(h);
        push(out, td->pos, GripKind::Move, 0);
        const std::span<const double> cw = store.table_col_widths(*td);
        const std::span<const double> rh = store.table_row_heights(*td);
        const double cs = std::cos(td->rotation);
        const double sn = std::sin(td->rotation);
        double x = 0.0;
        for (std::size_t i = 0; i + 1 < cw.size(); ++i) {
            x += cw[i];
            push(out, Vec2{td->pos.x + x * cs, td->pos.y + x * sn}, GripKind::Vertex,
                 static_cast<std::uint32_t>(i + 1));
        }
        // Row boundaries sit on the table's LEFT edge. Local y runs DOWN from the
        // insertion point, which is the -cs/+sn diagonal in world space.
        double y = 0.0;
        for (std::size_t j = 0; j + 1 < rh.size(); ++j) {
            y += rh[j];
            push(out, Vec2{td->pos.x + y * sn, td->pos.y - y * cs}, GripKind::Vertex,
                 kTableRowGripBase + static_cast<std::uint32_t>(j));
        }
        break;
    }
    case EntityKind::Fcf: {
        // One grip: the insertion point moves the whole frame. The frame's SIZE is
        // derived from the text height, so there is nothing else to drag -- resizing it
        // would mean overriding the text height, which is the PR's job.
        push(out, store.fcf(h)->pos, GripKind::Move, 0);
        break;
    }
    case EntityKind::Datum: {
        const DatumData* d = store.datum(h);
        push(out, d->pos, GripKind::Move, 0); // the box
        push(out, d->tip, GripKind::Endpoint, 1); // the triangle on the feature
        break;
    }
    case EntityKind::Hatch: {
        // A grip at every boundary-loop vertex (flat index across all loops, in order),
        // so the user can drag the boundary to reshape the hatch.
        const HatchData* hd = store.hatch(h);
        std::uint32_t idx = 0;
        for (const std::vector<Vec2>& loop : store.hatch_loops(*hd)) {
            for (const Vec2& v : loop) {
                push(out, v, GripKind::Vertex, idx++);
            }
        }
        break;
    }
    case EntityKind::Point:
        break; // a point has no grips (as before)
    case EntityKind::Spline: {
        // A grip on every control point (the AutoCAD CV grips); dragging reshapes.
        const SplineData* sp = store.spline(h);
        std::uint32_t idx = 0;
        for (const Vec2& p : store.control_points_of(*sp)) {
            push(out, p, GripKind::Vertex, idx++);
        }
        break;
    }
    }
}

Command edit_for_grip_drag(const GeometryStore& store, EntityHandle h, std::uint32_t grip_index,
                           Vec2 newpos) {
    Command c = capture_entity(store, h);
    std::visit(
        [&](auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, AddXlineCommand>) {
                x.base = newpos; // the root grip moves the whole construction line
            } else if constexpr (std::is_same_v<T, AddSplineCommand>) {
                if (grip_index < x.control_points.size()) {
                    x.control_points[grip_index] = newpos;
                }
            } else if constexpr (std::is_same_v<T, AddEllipseCommand>) {
                const EllipseData e{x.center, x.major, x.ratio, x.start, x.end, {}};
                if (grip_index == 0) {
                    x.center = newpos;
                } else if (ellipse::is_full(e)) {
                    const Vec2 v = newpos - x.center;
                    if (grip_index == 1 || grip_index == 2) {
                        // Major end: new major radius AND rotation; the minor radius is
                        // kept, so the ratio is re-derived (clamped: minor <= major).
                        const double minor_r = length(x.major) * x.ratio;
                        const double ml = length(v);
                        if (ml > 1e-9) {
                            x.major = grip_index == 1 ? v : Vec2{-v.x, -v.y};
                            x.ratio = std::clamp(minor_r / ml, 1e-6, 1.0);
                        }
                    } else {
                        const double ml = length(x.major);
                        if (ml > 1e-9) {
                            x.ratio = std::clamp(length(v) / ml, 1e-6, 1.0);
                        }
                    }
                } else if (grip_index == 1) {
                    x.start = ellipse::param_of(e, newpos);
                } else if (grip_index == 2) {
                    x.end = ellipse::param_of(e, newpos);
                } else {
                    // Midpoint: scale the ellipse about its centre so the arc passes
                    // through the new point (ratio and rotation unchanged).
                    const Vec2 mid = ellipse::point_at(e, e.start + ellipse::sweep_of(e) * 0.5);
                    const double d0 = length(mid - x.center);
                    if (d0 > 1e-9) {
                        x.major = x.major * (length(newpos - x.center) / d0);
                    }
                }
            } else if constexpr (std::is_same_v<T, AddLineCommand>) {
                if (grip_index == 0) {
                    x.a = newpos;
                } else if (grip_index == 1) {
                    x.b = newpos;
                } else { // midpoint -> translate the whole line
                    const Vec2 d = newpos - (x.a + x.b) * 0.5;
                    x.a = x.a + d;
                    x.b = x.b + d;
                }
            } else if constexpr (std::is_same_v<T, AddCircleCommand>) {
                if (grip_index == 0) {
                    x.center = newpos; // move
                } else {
                    x.radius = distance(x.center, newpos); // quadrant -> radius
                }
            } else if constexpr (std::is_same_v<T, AddArcCommand>) {
                if (grip_index == 0) {
                    x.center = newpos;
                } else if (grip_index == 1) {
                    x.start_angle = std::atan2(newpos.y - x.center.y, newpos.x - x.center.x);
                } else if (grip_index == 2) {
                    x.end_angle = std::atan2(newpos.y - x.center.y, newpos.x - x.center.x);
                } else {
                    x.radius = distance(x.center, newpos); // mid -> radius
                }
            } else if constexpr (std::is_same_v<T, AddPolylineCommand>) {
                if (grip_index >= kSegmentGripBase) {
                    // A segment grip: a straight segment moves parallel to itself (both
                    // ends by the drag); an arc segment is reshaped to pass through the
                    // dragged point (its bulge follows), as in AutoCAD.
                    const std::size_t n = x.points.size();
                    const std::size_t seg = grip_index - kSegmentGripBase;
                    const std::size_t nseg = n < 2 ? 0 : (x.closed ? n : n - 1);
                    if (seg < nseg) {
                        const std::size_t j = (seg + 1) % n;
                        const double bulge = seg < x.bulges.size() ? x.bulges[seg] : 0.0;
                        const Vec2 a = x.points[seg];
                        const Vec2 b = x.points[j];
                        if (bulge == 0.0) {
                            const Vec2 mid{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5};
                            const Vec2 d = newpos - mid;
                            x.points[seg] = a + d;
                            x.points[j] = b + d;
                        } else {
                            x.bulges[seg] = bulge_through(a, newpos, b);
                        }
                    }
                } else if (grip_index < x.points.size()) {
                    x.points[grip_index] = newpos;
                }
            } else if constexpr (std::is_same_v<T, AddAttDefCommand>) {
                x.text.pos = newpos;
            } else if constexpr (std::is_same_v<T, AddTextCommand>) {
                x.pos = newpos;
            } else if constexpr (std::is_same_v<T, AddDimensionCommand>) {
                const auto t = static_cast<DimType>(x.type);
                if (grip_index == DimData::kTextGripIndex) {
                    // Displacement is stored in the TEXT's own frame, so a rotated
                    // (e.g. vertical) dimension's label moves along its own baseline
                    // rather than in world x -- and so the offset survives the whole
                    // dimension being rotated later.
                    DimData probe;
                    probe.type = t;
                    probe.a = x.a;
                    probe.b = x.b;
                    probe.line_pt = x.line_pt;
                    probe.overrides = x.overrides;
                    probe.tol = x.tol;
                    const DimGeometry base = compute_dim_geometry(
                        probe, x.dim_style, Rgb{}, {x.prefix, x.suffix, x.text_override});
                    Vec2 q[4];
                    const Vec2 anchor = dim_label_quad(base, false, q) ? (q[0] + q[2]) * 0.5
                                                                      : base.text_pos;
                    const Vec2 delta = newpos - anchor;
                    const double cs = std::cos(base.text_rotation);
                    const double sn = std::sin(base.text_rotation);
                    x.text_offset = {delta.x * cs + delta.y * sn, -delta.x * sn + delta.y * cs};
                    return;
                }
                if ((t == DimType::Radius || t == DimType::Diameter || t == DimType::Jogged ||
                     t == DimType::ArcLength) &&
                    grip_index == 0) {
                    const Vec2 d = newpos - x.a; // centre grip -> move the whole dim
                    x.a = x.a + d;
                    x.b = x.b + d;
                    x.line_pt = x.line_pt + d;
                } else if (grip_index == 0) {
                    x.a = newpos;
                } else if (grip_index == 1) {
                    x.b = newpos;
                } else {
                    x.line_pt = newpos; // dim-line offset / placement
                }
            } else if constexpr (std::is_same_v<T, AddTableCommand>) {
                // Both axes measure the drag in the TABLE's own frame, so a rotated
                // table resizes along its own axes rather than the world ones.
                const double cs = std::cos(x.rotation);
                const double sn = std::sin(x.rotation);
                const Vec2 d = newpos - x.pos;
                if (grip_index == 0) {
                    x.pos = newpos;
                } else if (grip_index >= kTableRowGripBase) {
                    // Row-boundary grip: local y runs DOWN from the insertion point.
                    const std::size_t j = grip_index - kTableRowGripBase;
                    if (j < x.row_heights.size()) {
                        const double down = d.x * sn - d.y * cs;
                        double before = 0.0;
                        for (std::size_t i = 0; i < j; ++i) {
                            before += x.row_heights[i];
                        }
                        const double hgt = down - before;
                        if (hgt > 1e-6) {
                            x.row_heights[j] = hgt;
                        }
                    }
                } else if (grip_index <= x.col_widths.size()) {
                    // Column-boundary grip: set that column's width from the drag.
                    const double along = d.x * cs + d.y * sn;
                    double before = 0.0;
                    for (std::size_t i = 0; i + 1 < grip_index; ++i) {
                        before += x.col_widths[i];
                    }
                    const double w = along - before;
                    if (w > 1e-6) {
                        x.col_widths[grip_index - 1] = w;
                    }
                }
            } else if constexpr (std::is_same_v<T, AddViewportCommand>) {
                if (grip_index == 0) {
                    x.center = newpos;
                } else if (grip_index <= 4) {
                    // The dragged corner moves; the opposite one stays.
                    const double hw = x.width * 0.5;
                    const double hh = x.height * 0.5;
                    const double sx = (grip_index == 2 || grip_index == 3) ? 1.0 : -1.0;
                    const double sy = (grip_index >= 3) ? 1.0 : -1.0;
                    const Vec2 fixed{x.center.x - sx * hw, x.center.y - sy * hh};
                    const double w = std::abs(newpos.x - fixed.x);
                    const double hgt = std::abs(newpos.y - fixed.y);
                    if (w > 1e-6 && hgt > 1e-6) {
                        x.width = w;
                        x.height = hgt;
                        x.center = {(newpos.x + fixed.x) * 0.5, (newpos.y + fixed.y) * 0.5};
                    }
                }
            } else if constexpr (std::is_same_v<T, AddImageCommand>) {
                if (grip_index == 0) {
                    x.pos = newpos;
                } else {
                    // Opposite corner -> resize, measured in the image's own frame so a
                    // rotated image scales along its own axes.
                    const double cs = std::cos(x.rotation);
                    const double sn = std::sin(x.rotation);
                    const Vec2 d = newpos - x.pos;
                    const double lx = d.x * cs + d.y * sn;
                    const double ly = -d.x * sn + d.y * cs;
                    if (lx > 1e-9) {
                        x.width = lx;
                    }
                    if (ly > 1e-9) {
                        x.height = ly;
                    }
                }
            } else if constexpr (std::is_same_v<T, AddFcfCommand>) {
                x.pos = newpos; // one grip: move the frame
            } else if constexpr (std::is_same_v<T, AddDatumCommand>) {
                if (grip_index == 0) {
                    x.pos = newpos; // the box
                } else {
                    x.tip = newpos; // the triangle on the feature
                }
            } else if constexpr (std::is_same_v<T, AddLeaderCommand>) {
                if (grip_index == 0) {
                    x.tip = newpos;
                } else {
                    x.knee = newpos;
                }
            } else if constexpr (std::is_same_v<T, AddMTextCommand>) {
                if (grip_index == 0) {
                    x.block.pos = newpos; // move insertion
                } else {
                    // Width grip: re-wrap to the cursor along the text x-axis.
                    const Vec2 xdir{std::cos(x.block.rotation), std::sin(x.block.rotation)};
                    x.block.width = std::max(x.block.height, dot(newpos - x.block.pos, xdir));
                }
            } else if constexpr (std::is_same_v<T, AddMLeaderCommand>) {
                const std::uint32_t n = static_cast<std::uint32_t>(x.vertices.size());
                if (grip_index < n) {
                    const Vec2 d = newpos - x.vertices[grip_index];
                    x.vertices[grip_index] = newpos;
                    if (grip_index == n - 1) {
                        x.block.pos = x.block.pos + d; // landing drags the label with it
                    }
                } else {
                    x.block.pos = newpos; // text grip
                }
            } else if constexpr (std::is_same_v<T, AddInsertCommand>) {
                x.pos = newpos; // insertion-point grip moves the whole instance
            } else if constexpr (std::is_same_v<T, AddHatchCommand>) {
                // The grip index is the flat vertex index across all boundary loops.
                std::uint32_t idx = 0;
                for (std::vector<Vec2>& loop : x.loops) {
                    for (Vec2& v : loop) {
                        if (idx == grip_index) {
                            v = newpos;
                        }
                        ++idx;
                    }
                }
            }
        },
        c);
    return c;
}

double bulge_through(Vec2 a, Vec2 p, Vec2 b) {
    const Vec2 chord = b - a;
    const double len = length(chord);
    if (len < 1e-12) {
        return 0.0;
    }
    // Signed distance of p from the chord: positive to the right of a->b (the side a
    // positive, counter-clockwise bulge bows towards).
    const Vec2 right{chord.y / len, -chord.x / len};
    const double d = dot(p - a, right);
    if (std::abs(d) < 1e-12) {
        return 0.0;
    }
    // Circle through a, p, b: its centre lies on the chord's perpendicular bisector at a
    // distance found from |c - p| = |c - a|.
    const Vec2 mid{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5};
    const double h = len * 0.5;
    const double t = dot(p - mid, chord) / len; // p's offset along the chord
    // With the centre at mid - right * k:  (t)^2 + (d + k)^2 = h^2 + k^2  ->  k.
    const double k = (h * h - t * t - d * d) / (2.0 * d);
    const double r = std::sqrt(h * h + k * k);
    // Included angle of the arc through p: the arc's midpoint sagitta is r - |k| when p's
    // side is the "short" side (|k| measured towards p), else r + |k|.
    const double sagitta = (d * k < 0.0) ? r + std::abs(k) : r - std::abs(k);
    const double bulge = sagitta / h;
    return d > 0.0 ? bulge : -bulge;
}

} // namespace musacad::core

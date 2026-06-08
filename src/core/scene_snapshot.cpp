#include "musacad/core/scene_snapshot.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

#include "musacad/core/dimension.hpp"
#include "musacad/core/text/mtext.hpp"
#include "musacad/core/text/stroke_font.hpp"

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
// Line batch key = colour (24 bits) << 8 | display lineweight (8 bits).
std::uint64_t line_key(Rgb c, std::uint8_t lw) {
    return (static_cast<std::uint64_t>(pack_rgb(c)) << 8) | lw;
}

/// Off/frozen layers contribute no geometry, so the renderer never sees them.
bool visible(const GeometryStore& store, const EntityProps& p) {
    const Layer* l = store.layer(p.layer);
    return l != nullptr && l->on && !l->frozen;
}
ResolvedProps entity_resolved(const GeometryStore& store, const EntityProps& p) {
    const Layer* l = store.layer(p.layer);
    return l != nullptr ? resolve(p, *l) : ResolvedProps{};
}

} // namespace

void build_render_snapshot(const GeometryStore& store, const IGeometryKernel& kernel,
                           RenderSnapshot& out, double tolerance) {
    out.points.clear();
    out.line_vertices.clear();
    out.line_batches.clear();
    out.point_batches.clear();
    out.fill_vertices.clear();
    out.fill_batches.clear();
    out.layers.assign(store.layers().begin(), store.layers().end());
    out.current_layer = store.current_layer();

    // Lines group by (colour, lineweight); points and fills by colour.
    std::map<std::uint64_t, std::vector<Vec2>> line_groups;
    std::map<std::uint32_t, std::vector<Vec2>> point_groups;
    std::map<std::uint32_t, std::vector<Vec2>> fill_groups;

    const auto add_line = [&](Rgb c, std::uint8_t lw, Vec2 a, Vec2 b) {
        auto& v = line_groups[line_key(c, lw)];
        v.push_back(a);
        v.push_back(b);
    };
    const auto add_lines = [&](Rgb c, std::uint8_t lw, const std::vector<Vec2>& segs) {
        auto& v = line_groups[line_key(c, lw)];
        v.insert(v.end(), segs.begin(), segs.end());
    };
    const auto add_fills = [&](Rgb c, const std::vector<Vec2>& tris) {
        auto& v = fill_groups[pack_rgb(c)];
        v.insert(v.end(), tris.begin(), tris.end());
    };

    for_each_live(store.points(), EntityKind::Point, [&](EntityHandle h) {
        const PointData* d = store.point(h);
        if (visible(store, d->props)) {
            point_groups[pack_rgb(entity_resolved(store, d->props).color)].push_back(d->p);
        }
    });

    for_each_live(store.lines(), EntityKind::Line, [&](EntityHandle h) {
        const LineData* l = store.line(h);
        if (!visible(store, l->props)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, l->props);
        add_line(r.color, r.lineweight, l->a, l->b);
    });

    std::vector<Vec2> tess;
    const auto emit_curve = [&](EntityHandle h, const EntityProps& p) {
        if (!visible(store, p)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, p);
        kernel.tessellate(store, h, tolerance, tess);
        for (std::size_t s = 1; s < tess.size(); ++s) {
            add_line(r.color, r.lineweight, tess[s - 1], tess[s]);
        }
    };
    for_each_live(store.circles(), EntityKind::Circle,
                  [&](EntityHandle h) { emit_curve(h, store.circle(h)->props); });
    for_each_live(store.arcs(), EntityKind::Arc,
                  [&](EntityHandle h) { emit_curve(h, store.arc(h)->props); });
    for_each_live(store.polylines(), EntityKind::Polyline,
                  [&](EntityHandle h) { emit_curve(h, store.polyline(h)->props); });
    for_each_live(store.splines(), EntityKind::Spline,
                  [&](EntityHandle h) { emit_curve(h, store.spline(h)->props); });

    // Single-line text -> stroke-font segments (thin: text isn't lineweight-driven).
    std::vector<Vec2> tseg;
    for_each_live(store.texts(), EntityKind::Text, [&](EntityHandle h) {
        const TextData* t = store.text(h);
        if (!visible(store, t->props)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, t->props);
        tseg.clear();
        text::append_text_segments(store.string_of(*t), t->pos, t->height, t->rotation,
                                   static_cast<text::Justify>(t->justify), tseg);
        add_lines(r.color, 0, tseg);
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
        const DimGeometry g = compute_dim_geometry(*d, style != nullptr ? *style : DimStyle{}, base);
        add_lines(g.ext_color, g.lineweight, g.ext_lines);
        add_lines(g.dim_color, g.lineweight, g.dim_lines);
        add_lines(g.arrow_color, g.lineweight, g.arrow_lines);
        add_fills(g.arrow_color, g.arrow_fills);
        tseg.clear();
        text::append_text_segments(g.label, g.text_pos, g.text_height, g.text_rotation,
                                   g.text_justify, tseg);
        add_lines(g.text_color, 0, tseg);
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
        const DimStyle s = style != nullptr ? *style : DimStyle{};
        const Rgb arrow_c = s.arrow_color.resolve(r.color);
        const Rgb text_c = s.text_color.resolve(r.color);
        add_line(r.color, r.lineweight, l->tip, l->knee);
        afill.clear();
        aline.clear();
        append_arrowhead(afill, aline, l->tip, l->knee - l->tip, s.arrow_size,
                         static_cast<ArrowType>(s.arrow_type));
        add_fills(arrow_c, afill);
        add_lines(arrow_c, r.lineweight, aline);
        tseg.clear();
        text::append_text_segments(store.string_of(*l), l->knee + Vec2{s.arrow_size * 0.4, 0.0},
                                   l->text_height, 0.0, text::Justify::Left, tseg);
        add_lines(text_c, 0, tseg);
    });

    // MTEXT: multi-line paragraph text. Layout is COMPUTED here from the stored
    // fields (text/mtext.cpp) -- never baked on the entity.
    for_each_live(store.mtexts(), EntityKind::MText, [&](EntityHandle h) {
        const MTextData* m = store.mtext(h);
        if (!visible(store, m->props)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, m->props);
        const text::MTextLayout lay = text::layout_mtext(m->text, store.string_of(m->text));
        add_lines(r.color, 0, lay.segments);
    });

    // QLEADER: leader polyline + arrowhead + owned paragraph label (same layout).
    for_each_live(store.mleaders(), EntityKind::MLeader, [&](EntityHandle h) {
        const MLeaderData* m = store.mleader(h);
        if (!visible(store, m->props)) {
            return;
        }
        const ResolvedProps r = entity_resolved(store, m->props);
        const DimStyle* style = store.dimstyle(m->style);
        const DimStyle s = style != nullptr ? *style : DimStyle{};
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
        const text::MTextLayout lay = text::layout_mtext(m->text, store.string_of(m->text));
        add_lines(text_c, 0, lay.segments);
    });

    // Flatten groups into contiguous batches over the payload arrays.
    for (auto& [key, verts] : line_groups) {
        out.line_batches.push_back(
            ColorBatch{unpack_rgb(static_cast<std::uint32_t>(key >> 8)),
                       static_cast<std::uint32_t>(out.line_vertices.size() / 2),
                       static_cast<std::uint32_t>(verts.size() / 2),
                       static_cast<std::uint8_t>(key & 0xff)});
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
    for (const Vec2& p : out.fill_vertices) {
        extend(p);
    }
}

} // namespace musacad::core

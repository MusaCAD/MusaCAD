// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/io/document.hpp"

#include <unordered_map>

#include "musacad/core/geometry_store.hpp"

namespace musacad::core::io {

namespace {
std::string block_name_of(const GeometryStore& store, std::uint16_t idx) {
    const BlockDef* b = store.block(idx);
    return b != nullptr ? b->name : std::string();
}
DocInsert insert_to_doc(const GeometryStore& store, const InsertData& d) {
    return DocInsert{block_name_of(store, d.block), d.pos, d.scale_x, d.scale_y, d.rotation, d.props};
}
} // namespace

Document document_from_store(const GeometryStore& store) {
    Document doc;
    doc.layers.assign(store.layers().begin(), store.layers().end());
    doc.current_layer = store.current_layer();
    doc.ltscale = store.ltscale();
    doc.page_setups = store.page_setups();

    const auto& pts = store.points();
    for (std::uint32_t i = 0; i < pts.slot_count(); ++i) {
        if (pts.alive(i)) {
            doc.points.push_back(DocPoint{pts.data()[i].p, pts.data()[i].props});
        }
    }
    const auto& lines = store.lines();
    for (std::uint32_t i = 0; i < lines.slot_count(); ++i) {
        if (lines.alive(i)) {
            const LineData& l = lines.data()[i];
            doc.lines.push_back(DocLine{l.a, l.b, l.props});
        }
    }
    const auto& circles = store.circles();
    for (std::uint32_t i = 0; i < circles.slot_count(); ++i) {
        if (circles.alive(i)) {
            const CircleData& c = circles.data()[i];
            doc.circles.push_back(DocCircle{c.center, c.radius, c.props});
        }
    }
    const auto& arcs = store.arcs();
    for (std::uint32_t i = 0; i < arcs.slot_count(); ++i) {
        if (arcs.alive(i)) {
            const ArcData& a = arcs.data()[i];
            doc.arcs.push_back(DocArc{a.center, a.radius, a.start_angle, a.end_angle, a.props});
        }
    }
    const auto& plines = store.polylines();
    for (std::uint32_t i = 0; i < plines.slot_count(); ++i) {
        if (plines.alive(i)) {
            const PolylineData& p = plines.data()[i];
            const std::span<const Vec2> v = store.vertices_of(p);
            const std::span<const double> b = store.bulges_of(p);
            doc.polylines.push_back(DocPolyline{std::vector<Vec2>(v.begin(), v.end()), p.closed,
                                                p.props, std::vector<double>(b.begin(), b.end())});
        }
    }
    const auto& splines = store.splines();
    for (std::uint32_t i = 0; i < splines.slot_count(); ++i) {
        if (splines.alive(i)) {
            const SplineData& s = splines.data()[i];
            const std::span<const Vec2> cp = store.control_points_of(s);
            doc.splines.push_back(DocSpline{std::vector<Vec2>(cp.begin(), cp.end()), s.degree,
                                            s.props});
        }
    }
    doc.dimstyles.assign(store.dimstyles().begin(), store.dimstyles().end());
    const auto& texts = store.texts();
    for (std::uint32_t i = 0; i < texts.slot_count(); ++i) {
        if (texts.alive(i)) {
            const TextData& t = texts.data()[i];
            doc.texts.push_back(DocText{t.pos, t.height, t.rotation, t.justify,
                                        std::string(store.string_of(t)), t.props,
                                        std::string(store.font_name(t.font))});
        }
    }
    const auto& dims = store.dimensions();
    for (std::uint32_t i = 0; i < dims.slot_count(); ++i) {
        if (dims.alive(i)) {
            const DimData& dd = dims.data()[i];
            doc.dims.push_back(DocDim{static_cast<std::uint8_t>(dd.type), dd.a, dd.b, dd.line_pt,
                                      dd.style, dd.props, dd.overrides});
        }
    }
    const auto& leaders = store.leaders();
    for (std::uint32_t i = 0; i < leaders.slot_count(); ++i) {
        if (leaders.alive(i)) {
            const LeaderData& ld = leaders.data()[i];
            doc.leaders.push_back(DocLeader{ld.tip, ld.knee, ld.text_height, ld.style,
                                            std::string(store.string_of(ld)), ld.props,
                                            std::string(store.font_name(ld.font))});
        }
    }
    const auto& mtx = store.mtexts();
    for (std::uint32_t i = 0; i < mtx.slot_count(); ++i) {
        if (mtx.alive(i)) {
            const MTextData& m = mtx.data()[i];
            doc.mtexts.push_back(DocMText{m.text, std::string(store.string_of(m.text)), m.props,
                                          std::string(store.font_name(m.text.font))});
        }
    }
    const auto& mld = store.mleaders();
    for (std::uint32_t i = 0; i < mld.slot_count(); ++i) {
        if (mld.alive(i)) {
            const MLeaderData& m = mld.data()[i];
            const auto v = store.vertices_of(m);
            doc.mleaders.push_back(DocMLeader{std::vector<Vec2>(v.begin(), v.end()), m.style,
                                              m.text, std::string(store.string_of(m.text)),
                                              m.props, std::string(store.font_name(m.text.font))});
        }
    }
    // Block definitions (by name) + their self-contained content.
    for (std::uint16_t bi = 0; bi < static_cast<std::uint16_t>(store.block_count()); ++bi) {
        const BlockDef* b = store.block(bi);
        DocBlockDef bd;
        bd.name = b->name;
        bd.base = b->base;
        for (const LineData& l : b->content.lines) {
            bd.lines.push_back(DocLine{l.a, l.b, l.props});
        }
        for (const CircleData& c : b->content.circles) {
            bd.circles.push_back(DocCircle{c.center, c.radius, c.props});
        }
        for (const ArcData& a : b->content.arcs) {
            bd.arcs.push_back(DocArc{a.center, a.radius, a.start_angle, a.end_angle, a.props});
        }
        for (const BlockPolyline& p : b->content.polylines) {
            bd.polylines.push_back(DocPolyline{p.verts, p.closed, p.props, p.bulges});
        }
        for (const BlockText& t : b->content.texts) {
            bd.texts.push_back(DocText{t.pos, t.height, t.rotation, t.justify, t.content, t.props});
        }
        for (const BlockMText& m : b->content.mtexts) {
            bd.mtexts.push_back(DocMText{m.block, m.content, m.props});
        }
        for (const InsertData& ni : b->content.inserts) {
            bd.inserts.push_back(insert_to_doc(store, ni));
        }
        doc.block_defs.push_back(std::move(bd));
    }
    const auto& ins = store.inserts();
    for (std::uint32_t i = 0; i < ins.slot_count(); ++i) {
        if (ins.alive(i)) {
            doc.inserts.push_back(insert_to_doc(store, ins.data()[i]));
        }
    }
    return doc;
}

void populate_store(GeometryStore& store, const Document& doc) {
    store.set_layer_table(doc.layers, doc.current_layer);
    store.set_dimstyle_table(doc.dimstyles);
    store.set_ltscale(doc.ltscale);
    store.set_page_setups(doc.page_setups);

    // Block definitions: name -> index (defs may reference each other; resolve all by
    // name against the full list). Then build the core block table and model inserts.
    std::unordered_map<std::string, std::uint16_t> block_index;
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(doc.block_defs.size()); ++i) {
        block_index.emplace(doc.block_defs[i].name, i);
    }
    const auto resolve_block = [&](const std::string& name) -> std::uint16_t {
        const auto it = block_index.find(name);
        return it != block_index.end() ? it->second : 0;
    };
    const auto to_insert = [&](const DocInsert& di) {
        return InsertData{resolve_block(di.block_name), di.pos,     di.scale_x,
                          di.scale_y,                   di.rotation, di.props};
    };
    if (!doc.block_defs.empty()) {
        std::vector<BlockDef> blocks;
        blocks.reserve(doc.block_defs.size());
        for (const DocBlockDef& bd : doc.block_defs) {
            BlockDef cb;
            cb.name = bd.name;
            cb.base = bd.base;
            for (const DocLine& l : bd.lines) {
                cb.content.lines.push_back(LineData{l.a, l.b, l.props});
            }
            for (const DocCircle& c : bd.circles) {
                cb.content.circles.push_back(CircleData{c.center, c.radius, c.props});
            }
            for (const DocArc& a : bd.arcs) {
                cb.content.arcs.push_back(
                    ArcData{a.center, a.radius, a.start_angle, a.end_angle, a.props});
            }
            for (const DocPolyline& p : bd.polylines) {
                cb.content.polylines.push_back(BlockPolyline{p.points, p.bulges, p.closed, p.props});
            }
            for (const DocText& t : bd.texts) {
                cb.content.texts.push_back(
                    BlockText{t.pos, t.height, t.rotation, t.justify, t.content, t.props});
            }
            for (const DocMText& m : bd.mtexts) {
                cb.content.mtexts.push_back(BlockMText{m.block, m.content, m.props});
            }
            for (const DocInsert& ni : bd.inserts) {
                cb.content.inserts.push_back(to_insert(ni));
            }
            blocks.push_back(std::move(cb));
        }
        store.set_block_table(std::move(blocks));
    }
    for (const DocInsert& di : doc.inserts) {
        store.add_insert(resolve_block(di.block_name), di.pos, di.scale_x, di.scale_y, di.rotation,
                         di.props);
    }
    for (const DocText& t : doc.texts) {
        store.add_text(t.pos, t.height, t.rotation, t.justify, t.content, t.props,
                       store.add_font(t.font));
    }
    for (const DocDim& d : doc.dims) {
        store.add_dimension(static_cast<DimType>(d.type), d.a, d.b, d.line_pt, d.style, d.props,
                            d.overrides);
    }
    for (const DocLeader& l : doc.leaders) {
        store.add_leader(l.tip, l.knee, l.text_height, l.style, l.content, l.props,
                         store.add_font(l.font));
    }
    for (const DocMText& m : doc.mtexts) {
        MTextBlock b = m.block;
        b.font = store.add_font(m.font);
        store.add_mtext(b, m.content, m.props);
    }
    for (const DocMLeader& m : doc.mleaders) {
        MTextBlock b = m.block;
        b.font = store.add_font(m.font);
        store.add_mleader(m.vertices, m.style, b, m.content, m.props);
    }
    for (const DocPoint& p : doc.points) {
        store.add_point(p.p, p.props);
    }
    for (const DocLine& l : doc.lines) {
        store.add_line(l.a, l.b, l.props);
    }
    for (const DocCircle& c : doc.circles) {
        store.add_circle(c.center, c.radius, c.props);
    }
    for (const DocArc& a : doc.arcs) {
        store.add_arc(a.center, a.radius, a.start_angle, a.end_angle, a.props);
    }
    for (const DocPolyline& p : doc.polylines) {
        store.add_polyline(p.points, p.bulges, p.closed, p.props);
    }
    for (const DocSpline& s : doc.splines) {
        store.add_spline(s.control_points, s.degree, s.props);
    }
}

} // namespace musacad::core::io

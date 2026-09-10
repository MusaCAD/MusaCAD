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
    DocInsert di{block_name_of(store, d.block), d.pos, d.scale_x, d.scale_y, d.rotation, d.props, {}};
    // Values pair with the definition's attdefs by position; stored by tag so a
    // re-ordered definition still finds them.
    const std::vector<std::string> values = store.insert_attribs(d);
    if (const BlockDef* bd = store.block(d.block); bd != nullptr && !values.empty()) {
        for (std::size_t i = 0; i < values.size() && i < bd->content.attdefs.size(); ++i) {
            di.attribs.push_back(DocAttrib{bd->content.attdefs[i].tag, values[i]});
        }
    }
    return di;
}
} // namespace


namespace {
/// How many live slots of the same arena precede slot `index` -- the entity's position
/// among its kind in document order (the order the arenas are walked here and re-added
/// by populate_store).
template <class Arena>
std::uint32_t alive_before(const Arena& arena, std::uint32_t index) {
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < index && i < arena.slot_count(); ++i) {
        if (arena.alive(i)) {
            ++n;
        }
    }
    return n;
}
std::uint32_t doc_index_of(const GeometryStore& store, EntityHandle h) {
    switch (h.kind) {
    case EntityKind::Point:
        return alive_before(store.points(), h.index);
    case EntityKind::Xline:
        return alive_before(store.xlines(), h.index);
    case EntityKind::Ellipse:
        return alive_before(store.ellipses(), h.index);
    case EntityKind::Line:
        return alive_before(store.lines(), h.index);
    case EntityKind::Circle:
        return alive_before(store.circles(), h.index);
    case EntityKind::Arc:
        return alive_before(store.arcs(), h.index);
    case EntityKind::Polyline:
        return alive_before(store.polylines(), h.index);
    case EntityKind::Spline:
        return alive_before(store.splines(), h.index);
    case EntityKind::Text:
        return alive_before(store.texts(), h.index);
    case EntityKind::AttDef:
        return alive_before(store.attdefs(), h.index);
    case EntityKind::Dimension:
        return alive_before(store.dimensions(), h.index);
    case EntityKind::Leader:
        return alive_before(store.leaders(), h.index);
    case EntityKind::MText:
        return alive_before(store.mtexts(), h.index);
    case EntityKind::MLeader:
        return alive_before(store.mleaders(), h.index);
    case EntityKind::Insert:
        return alive_before(store.inserts(), h.index);
    case EntityKind::Hatch:
        return alive_before(store.hatches(), h.index);
    case EntityKind::Fcf:
        return alive_before(store.fcfs(), h.index);
    case EntityKind::Datum:
        return alive_before(store.datums(), h.index);
    case EntityKind::Image:
        return alive_before(store.images(), h.index);
    case EntityKind::Table:
        return alive_before(store.tables(), h.index);
    }
    return 0;
}
/// The live handles of one kind in document order.
std::vector<EntityHandle> handles_of_kind(const GeometryStore& store, EntityKind kind) {
    std::vector<EntityHandle> out;
    const auto collect = [&](const auto& arena, EntityKind k) {
        for (std::uint32_t i = 0; i < arena.slot_count(); ++i) {
            if (arena.alive(i)) {
                out.push_back(EntityHandle{i, arena.generations()[i], k});
            }
        }
    };
    switch (kind) {
    case EntityKind::Point:
        collect(store.points(), EntityKind::Point);
        break;
    case EntityKind::Xline:
        collect(store.xlines(), EntityKind::Xline);
        break;
    case EntityKind::Ellipse:
        collect(store.ellipses(), EntityKind::Ellipse);
        break;
    case EntityKind::Line:
        collect(store.lines(), EntityKind::Line);
        break;
    case EntityKind::Circle:
        collect(store.circles(), EntityKind::Circle);
        break;
    case EntityKind::Arc:
        collect(store.arcs(), EntityKind::Arc);
        break;
    case EntityKind::Polyline:
        collect(store.polylines(), EntityKind::Polyline);
        break;
    case EntityKind::Spline:
        collect(store.splines(), EntityKind::Spline);
        break;
    case EntityKind::Text:
        collect(store.texts(), EntityKind::Text);
        break;
    case EntityKind::AttDef:
        collect(store.attdefs(), EntityKind::AttDef);
        break;
    case EntityKind::Dimension:
        collect(store.dimensions(), EntityKind::Dimension);
        break;
    case EntityKind::Leader:
        collect(store.leaders(), EntityKind::Leader);
        break;
    case EntityKind::MText:
        collect(store.mtexts(), EntityKind::MText);
        break;
    case EntityKind::MLeader:
        collect(store.mleaders(), EntityKind::MLeader);
        break;
    case EntityKind::Insert:
        collect(store.inserts(), EntityKind::Insert);
        break;
    case EntityKind::Hatch:
        collect(store.hatches(), EntityKind::Hatch);
        break;
    case EntityKind::Fcf:
        collect(store.fcfs(), EntityKind::Fcf);
        break;
    case EntityKind::Datum:
        collect(store.datums(), EntityKind::Datum);
        break;
    case EntityKind::Image:
        collect(store.images(), EntityKind::Image);
        break;
    case EntityKind::Table:
        collect(store.tables(), EntityKind::Table);
        break;
    }
    return out;
}
} // namespace

Document document_from_store(const GeometryStore& store) {
    Document doc;
    doc.layers.assign(store.layers().begin(), store.layers().end());
    doc.current_layer = store.current_layer();
    doc.ltscale = store.ltscale();
    doc.page_setups = store.page_setups();
    doc.views = store.named_views();
    doc.display_units = store.units();
    doc.text_styles = store.text_styles();
    doc.current_text_style = store.current_text_style();
    doc.wipeout_frames = store.wipeout_frames();
    doc.attdisp = store.attdisp();
    doc.image_frame = store.image_frame();
    for (const EntityGroup& g : store.groups()) {
        DocGroup dg;
        dg.name = g.name;
        dg.description = g.description;
        dg.selectable = g.selectable;
        for (const EntityHandle m : g.members) {
            if (store.is_valid(m)) {
                dg.members.emplace_back(static_cast<std::uint8_t>(m.kind), doc_index_of(store, m));
            }
        }
        if (!dg.members.empty()) {
            doc.groups.push_back(std::move(dg)); // an emptied group is not worth keeping
        }
    }

    const auto& pts = store.points();
    for (std::uint32_t i = 0; i < pts.slot_count(); ++i) {
        if (pts.alive(i)) {
            doc.points.push_back(DocPoint{pts.data()[i].p, pts.data()[i].props});
        }
    }
    const auto& xlines = store.xlines();
    for (std::uint32_t i = 0; i < xlines.slot_count(); ++i) {
        if (xlines.alive(i)) {
            const XlineData& x = xlines.data()[i];
            doc.xlines.push_back(DocXline{x.base, x.dir, x.ray, x.props});
        }
    }
    const auto& ellipses = store.ellipses();
    for (std::uint32_t i = 0; i < ellipses.slot_count(); ++i) {
        if (ellipses.alive(i)) {
            const EllipseData& e = ellipses.data()[i];
            doc.ellipses.push_back(DocEllipse{e.center, e.major, e.ratio, e.start, e.end, e.props});
        }
    }
    const auto& lines = store.lines();
    for (std::uint32_t i = 0; i < lines.slot_count(); ++i) {
        if (lines.alive(i)) {
            const LineData& l = lines.data()[i];
            doc.lines.push_back(DocLine{
                l.a, l.b, l.props,
                store.celtscale(EntityHandle{i, lines.generations()[i], EntityKind::Line})});
        }
    }
    const auto& circles = store.circles();
    for (std::uint32_t i = 0; i < circles.slot_count(); ++i) {
        if (circles.alive(i)) {
            const CircleData& c = circles.data()[i];
            doc.circles.push_back(DocCircle{
                c.center, c.radius, c.props,
                store.celtscale(EntityHandle{i, circles.generations()[i], EntityKind::Circle})});
        }
    }
    const auto& arcs = store.arcs();
    for (std::uint32_t i = 0; i < arcs.slot_count(); ++i) {
        if (arcs.alive(i)) {
            const ArcData& a = arcs.data()[i];
            doc.arcs.push_back(
                DocArc{a.center, a.radius, a.start_angle, a.end_angle, a.props,
                       store.celtscale(EntityHandle{i, arcs.generations()[i], EntityKind::Arc})});
        }
    }
    const auto& plines = store.polylines();
    for (std::uint32_t i = 0; i < plines.slot_count(); ++i) {
        if (plines.alive(i)) {
            const PolylineData& p = plines.data()[i];
            const std::span<const Vec2> v = store.vertices_of(p);
            const std::span<const double> b = store.bulges_of(p);
            doc.polylines.push_back(DocPolyline{
                std::vector<Vec2>(v.begin(), v.end()), p.closed, p.props,
                std::vector<double>(b.begin(), b.end()),
                store.celtscale(EntityHandle{i, plines.generations()[i], EntityKind::Polyline})});
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
                                        std::string(store.font_name(t.font)), t.style});
        }
    }
    const auto& adefs = store.attdefs();
    for (std::uint32_t i = 0; i < adefs.slot_count(); ++i) {
        if (adefs.alive(i)) {
            const AttDefData& a = adefs.data()[i];
            doc.attdefs.push_back(DocAttDef{
                DocText{a.text.pos, a.text.height, a.text.rotation, a.text.justify,
                        std::string(store.string_of(a.text)), a.text.props,
                        std::string(store.font_name(a.text.font)), a.text.style},
                std::string(store.attdef_prompt(a)), std::string(store.attdef_default(a)), a.flags});
        }
    }
    const auto& dims = store.dimensions();
    for (std::uint32_t i = 0; i < dims.slot_count(); ++i) {
        if (dims.alive(i)) {
            const DimData& dd = dims.data()[i];
            doc.dims.push_back(DocDim{static_cast<std::uint8_t>(dd.type), dd.a, dd.b, dd.line_pt,
                                      dd.style, dd.props, dd.overrides,
                                      std::string(store.dim_prefix(dd)),
                                      std::string(store.dim_suffix(dd)), dd.tol,
                                      std::string(store.dim_override(dd)), dd.text_offset,
                                      dd.aux});
        }
    }
    const auto& leaders = store.leaders();
    for (std::uint32_t i = 0; i < leaders.slot_count(); ++i) {
        if (leaders.alive(i)) {
            const LeaderData& ld = leaders.data()[i];
            doc.leaders.push_back(DocLeader{ld.tip, ld.knee, ld.text_height, ld.style,
                                            std::string(store.string_of(ld)), ld.props,
                                            std::string(store.font_name(ld.font)), ld.overrides});
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
                                              m.text, std::string(store.string_of(m.text)), m.props,
                                              std::string(store.font_name(m.text.font)),
                                              m.overrides});
        }
    }
    const auto& hatch_arena = store.hatches();
    for (std::uint32_t i = 0; i < hatch_arena.slot_count(); ++i) {
        if (hatch_arena.alive(i)) {
            const HatchData& h = hatch_arena.data()[i];
            doc.hatches.push_back(DocHatch{store.hatch_loops(h), std::string(store.string_of(h)),
                                           h.pattern_scale, h.pattern_angle, h.pattern_origin,
                                           h.props,
                                           h.color2});
        }
    }
    const auto& fcf_arena = store.fcfs();
    for (std::uint32_t i = 0; i < fcf_arena.slot_count(); ++i) {
        if (fcf_arena.alive(i)) {
            const FcfData& f = fcf_arena.data()[i];
            std::vector<std::string> cells;
            cells.reserve(f.cell_count);
            for (const std::string_view c : store.fcf_cell_text(f)) {
                cells.emplace_back(c);
            }
            doc.fcfs.push_back(
                DocFcf{std::move(cells), f.pos, f.rotation, f.style, f.props, f.overrides});
        }
    }
    const auto& datum_arena = store.datums();
    for (std::uint32_t i = 0; i < datum_arena.slot_count(); ++i) {
        if (datum_arena.alive(i)) {
            const DatumData& d = datum_arena.data()[i];
            doc.datums.push_back(DocDatum{std::string(store.string_of(d)), d.tip, d.pos, d.rotation,
                                          d.style, d.props, d.overrides});
        }
    }
    for (const TableStyle& ts : store.table_styles()) {
        doc.table_styles.push_back(ts);
    }
    const auto& table_arena = store.tables();
    for (std::uint32_t i = 0; i < table_arena.slot_count(); ++i) {
        if (table_arena.alive(i)) {
            const TableData& t = table_arena.data()[i];
            DocTable dt;
            dt.rows = t.rows;
            dt.cols = t.cols;
            dt.has_title = t.has_title;
            dt.has_header = t.has_header;
            dt.pos = t.pos;
            dt.rotation = t.rotation;
            dt.style = t.style;
            dt.props = t.props;
            const std::span<const double> cw = store.table_col_widths(t);
            const std::span<const double> rh = store.table_row_heights(t);
            dt.col_widths.assign(cw.begin(), cw.end());
            dt.row_heights.assign(rh.begin(), rh.end());
            for (const TableCell& c : store.table_cells(t)) {
                dt.cells.push_back(DocTableCell{std::string(store.string_of(c)), c.span_cols,
                                                c.span_rows, c.align});
            }
            doc.tables.push_back(std::move(dt));
        }
    }
    for (const ImageDef& d : store.image_defs()) {
        doc.image_defs.push_back(DocImageDef{d.source, d.bytes, d.pixel_w, d.pixel_h});
    }
    const auto& image_arena = store.images();
    for (std::uint32_t i = 0; i < image_arena.slot_count(); ++i) {
        if (image_arena.alive(i)) {
            const ImageData& im = image_arena.data()[i];
            doc.images.push_back(DocImage{im.def, im.pos, im.width, im.height, im.rotation,
                                          im.clipped, im.clip_u0, im.clip_v0, im.clip_u1,
                                          im.clip_v1, im.props});
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
        for (const BlockAttDef& a : b->content.attdefs) {
            bd.attdefs.push_back(DocAttDef{DocText{a.text.pos, a.text.height, a.text.rotation,
                                                   a.text.justify, a.tag, a.text.props},
                                           a.prompt, a.def, a.flags});
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
    if (!doc.text_styles.empty()) {
        store.set_text_styles(doc.text_styles);
    }
    store.set_current_text_style(doc.current_text_style);
    store.set_wipeout_frames(doc.wipeout_frames);
    store.set_attdisp(doc.attdisp);
    store.set_image_frame(doc.image_frame);
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
            for (const DocAttDef& a : bd.attdefs) {
                cb.content.attdefs.push_back(
                    BlockAttDef{BlockText{a.text.pos, a.text.height, a.text.rotation, a.text.justify,
                                          a.text.content, a.text.props},
                                a.text.content, a.prompt, a.def, a.flags});
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
        const std::uint16_t bi = resolve_block(di.block_name);
        // Values by tag -> the definition's attdef order (unknown tags are dropped).
        std::vector<std::string> values;
        if (const BlockDef* bd = store.block(bi); bd != nullptr && !di.attribs.empty()) {
            values.reserve(bd->content.attdefs.size());
            for (const BlockAttDef& a : bd->content.attdefs) {
                std::string v = a.def;
                for (const DocAttrib& at : di.attribs) {
                    if (at.tag == a.tag) {
                        v = at.value;
                        break;
                    }
                }
                values.push_back(std::move(v));
            }
        }
        store.add_insert(bi, di.pos, di.scale_x, di.scale_y, di.rotation, di.props, values);
    }
    for (const DocText& t : doc.texts) {
        store.add_text(t.pos, t.height, t.rotation, t.justify, t.content, t.props,

                       store.add_font(t.font), t.style);
    }
    for (const DocAttDef& a : doc.attdefs) {
        store.add_attdef(a.text.pos, a.text.height, a.text.rotation, a.text.justify, a.text.content,
                         a.prompt, a.def, a.flags, a.text.props, store.add_font(a.text.font),
                         a.text.style);
    }
    for (const DocDim& d : doc.dims) {
        const EntityHandle dh = store.add_dimension(static_cast<DimType>(d.type), d.a, d.b, d.line_pt, d.style, d.props,
                            d.overrides, d.prefix, d.suffix, d.tol, d.text_override,
                            d.text_offset);

        store.set_dim_aux(dh, d.aux);
    }
    for (const DocLeader& l : doc.leaders) {
        store.add_leader(l.tip, l.knee, l.text_height, l.style, l.content, l.props,
                         store.add_font(l.font), l.overrides);
    }
    for (const DocMText& m : doc.mtexts) {
        MTextBlock b = m.block;
        b.font = store.add_font(m.font);
        store.add_mtext(b, m.content, m.props);
    }
    for (const DocMLeader& m : doc.mleaders) {
        MTextBlock b = m.block;
        b.font = store.add_font(m.font);
        store.add_mleader(m.vertices, m.style, b, m.content, m.props, m.overrides);
    }
    for (const DocHatch& h : doc.hatches) {
        store.add_hatch(h.loops, h.pattern_name, h.pattern_scale, h.pattern_angle, h.pattern_origin,
                        h.props, h.color2);
    }
    {
        std::vector<ImageDef> defs;
        defs.reserve(doc.image_defs.size());
        for (const DocImageDef& d : doc.image_defs) {
            defs.push_back(ImageDef{d.source, d.bytes, d.pixel_w, d.pixel_h, 1});
        }
        store.set_image_def_table(std::move(defs));
    }
    for (const DocImage& im : doc.images) {
        const EntityHandle h =
            store.add_image(im.def, im.pos, im.width, im.height, im.rotation, im.props);
        if (ImageData* d = store.mutable_image(h)) {
            d->clipped = im.clipped;
            d->clip_u0 = im.clip_u0;
            d->clip_v0 = im.clip_v0;
            d->clip_u1 = im.clip_u1;
            d->clip_v1 = im.clip_v1;
        }
    }
    if (!doc.table_styles.empty()) {
        store.set_table_style_table(doc.table_styles);
    }
    for (const DocTable& t : doc.tables) {
        std::vector<TableCell> cells;
        cells.reserve(t.cells.size());
        for (const DocTableCell& c : t.cells) {
            TableCell tc;
            tc.str_offset = store.intern_string(c.text);
            tc.str_len = static_cast<std::uint32_t>(c.text.size());
            tc.span_cols = c.span_cols;
            tc.span_rows = c.span_rows;
            tc.align = c.align;
            cells.push_back(tc);
        }
        store.add_table(t.rows, t.cols, cells, t.col_widths, t.row_heights, t.pos, t.rotation,
                        t.style, t.has_title, t.has_header, t.props);
    }
    for (const DocFcf& f : doc.fcfs) {
        store.add_fcf(f.cells, f.pos, f.rotation, f.style, f.props, f.overrides);
    }
    for (const DocDatum& d : doc.datums) {
        store.add_datum(d.letter, d.tip, d.pos, d.rotation, d.style, d.props, d.overrides);
    }
    for (const DocPoint& p : doc.points) {
        store.add_point(p.p, p.props);
    }
    for (const DocXline& x : doc.xlines) {
        store.add_xline(x.base, x.dir, x.ray, x.props);
    }
    for (const DocEllipse& e : doc.ellipses) {
        store.add_ellipse(e.center, e.major, e.ratio, e.start, e.end, e.props);
    }
    for (const DocLine& l : doc.lines) {
        store.set_celtscale(store.add_line(l.a, l.b, l.props), l.celtscale);
    }
    for (const DocCircle& c : doc.circles) {
        store.set_celtscale(store.add_circle(c.center, c.radius, c.props), c.celtscale);
    }
    for (const DocArc& a : doc.arcs) {
        store.set_celtscale(store.add_arc(a.center, a.radius, a.start_angle, a.end_angle, a.props),
                            a.celtscale);
    }
    for (const DocPolyline& p : doc.polylines) {
        store.set_celtscale(store.add_polyline(p.points, p.bulges, p.closed, p.props), p.celtscale);
    }
    for (const DocSpline& s : doc.splines) {
        store.add_spline(s.control_points, s.degree, s.props);
    }
    store.set_named_views(doc.views);
    store.set_units(doc.display_units);
    std::vector<EntityGroup> groups;
    for (const DocGroup& dg : doc.groups) {
        EntityGroup g;
        g.name = dg.name;
        g.description = dg.description;
        g.selectable = dg.selectable;
        for (const auto& [kind_raw, idx] : dg.members) {
            const auto kind = static_cast<EntityKind>(kind_raw);
            const std::vector<EntityHandle> hs = handles_of_kind(store, kind);
            if (idx < hs.size()) {
                g.members.push_back(hs[idx]);
            }
        }
        if (!g.members.empty()) {
            groups.push_back(std::move(g));
        }
    }
    store.set_groups(std::move(groups));
}

} // namespace musacad::core::io

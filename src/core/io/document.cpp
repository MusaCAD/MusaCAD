#include "musacad/core/io/document.hpp"

#include "musacad/core/geometry_store.hpp"

namespace musacad::core::io {

Document document_from_store(const GeometryStore& store) {
    Document doc;
    doc.layers.assign(store.layers().begin(), store.layers().end());
    doc.current_layer = store.current_layer();

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
            doc.polylines.push_back(
                DocPolyline{std::vector<Vec2>(v.begin(), v.end()), p.closed, p.props});
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
                                        std::string(store.string_of(t)), t.props});
        }
    }
    const auto& dims = store.dimensions();
    for (std::uint32_t i = 0; i < dims.slot_count(); ++i) {
        if (dims.alive(i)) {
            const DimData& dd = dims.data()[i];
            doc.dims.push_back(DocDim{static_cast<std::uint8_t>(dd.type), dd.a, dd.b, dd.line_pt,
                                      dd.style, dd.props});
        }
    }
    const auto& leaders = store.leaders();
    for (std::uint32_t i = 0; i < leaders.slot_count(); ++i) {
        if (leaders.alive(i)) {
            const LeaderData& ld = leaders.data()[i];
            doc.leaders.push_back(DocLeader{ld.tip, ld.knee, ld.text_height, ld.style,
                                            std::string(store.string_of(ld)), ld.props});
        }
    }
    return doc;
}

void populate_store(GeometryStore& store, const Document& doc) {
    store.set_layer_table(doc.layers, doc.current_layer);
    store.set_dimstyle_table(doc.dimstyles);
    for (const DocText& t : doc.texts) {
        store.add_text(t.pos, t.height, t.rotation, t.justify, t.content, t.props);
    }
    for (const DocDim& d : doc.dims) {
        store.add_dimension(static_cast<DimType>(d.type), d.a, d.b, d.line_pt, d.style, d.props);
    }
    for (const DocLeader& l : doc.leaders) {
        store.add_leader(l.tip, l.knee, l.text_height, l.style, l.content, l.props);
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
        store.add_polyline(p.points, p.closed, p.props);
    }
    for (const DocSpline& s : doc.splines) {
        store.add_spline(s.control_points, s.degree, s.props);
    }
}

} // namespace musacad::core::io

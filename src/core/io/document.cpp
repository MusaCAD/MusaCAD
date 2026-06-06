#include "musacad/core/io/document.hpp"

#include "musacad/core/geometry_store.hpp"

namespace musacad::core::io {

Document document_from_store(const GeometryStore& store) {
    Document doc;

    const auto& pts = store.points();
    for (std::uint32_t i = 0; i < pts.slot_count(); ++i) {
        if (pts.alive(i)) {
            doc.points.push_back(pts.data()[i].p);
        }
    }
    const auto& lines = store.lines();
    for (std::uint32_t i = 0; i < lines.slot_count(); ++i) {
        if (lines.alive(i)) {
            doc.lines.push_back(DocLine{lines.data()[i].a, lines.data()[i].b});
        }
    }
    const auto& circles = store.circles();
    for (std::uint32_t i = 0; i < circles.slot_count(); ++i) {
        if (circles.alive(i)) {
            doc.circles.push_back(DocCircle{circles.data()[i].center, circles.data()[i].radius});
        }
    }
    const auto& arcs = store.arcs();
    for (std::uint32_t i = 0; i < arcs.slot_count(); ++i) {
        if (arcs.alive(i)) {
            const ArcData& a = arcs.data()[i];
            doc.arcs.push_back(DocArc{a.center, a.radius, a.start_angle, a.end_angle});
        }
    }
    const auto& plines = store.polylines();
    for (std::uint32_t i = 0; i < plines.slot_count(); ++i) {
        if (plines.alive(i)) {
            const PolylineData& p = plines.data()[i];
            const std::span<const Vec2> v = store.vertices_of(p);
            doc.polylines.push_back(DocPolyline{std::vector<Vec2>(v.begin(), v.end()), p.closed});
        }
    }
    const auto& splines = store.splines();
    for (std::uint32_t i = 0; i < splines.slot_count(); ++i) {
        if (splines.alive(i)) {
            const SplineData& s = splines.data()[i];
            const std::span<const Vec2> cp = store.control_points_of(s);
            doc.splines.push_back(
                DocSpline{std::vector<Vec2>(cp.begin(), cp.end()), s.degree});
        }
    }
    return doc;
}

void populate_store(GeometryStore& store, const Document& doc) {
    for (const Vec2& p : doc.points) {
        store.add_point(p);
    }
    for (const DocLine& l : doc.lines) {
        store.add_line(l.a, l.b);
    }
    for (const DocCircle& c : doc.circles) {
        store.add_circle(c.center, c.radius);
    }
    for (const DocArc& a : doc.arcs) {
        store.add_arc(a.center, a.radius, a.start_angle, a.end_angle);
    }
    for (const DocPolyline& p : doc.polylines) {
        store.add_polyline(p.points, p.closed);
    }
    for (const DocSpline& s : doc.splines) {
        store.add_spline(s.control_points, s.degree);
    }
}

} // namespace musacad::core::io

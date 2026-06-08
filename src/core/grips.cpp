#include "musacad/core/grips.hpp"

#include <string>
#include <variant>

#include "musacad/core/dimension.hpp"

namespace musacad::core {

Command capture_entity(const GeometryStore& store, EntityHandle h) {
    switch (h.kind) {
    case EntityKind::Line: {
        const LineData* l = store.line(h);
        return AddLineCommand{l->a, l->b, 0, l->props};
    }
    case EntityKind::Circle: {
        const CircleData* c = store.circle(h);
        return AddCircleCommand{c->center, c->radius, 0, c->props};
    }
    case EntityKind::Arc: {
        const ArcData* a = store.arc(h);
        return AddArcCommand{a->center, a->radius, a->start_angle, a->end_angle, 0, a->props};
    }
    case EntityKind::Polyline: {
        const PolylineData* p = store.polyline(h);
        const auto verts = store.vertices_of(*p);
        const auto bulges = store.bulges_of(*p);
        return AddPolylineCommand{std::vector<Vec2>(verts.begin(), verts.end()), p->closed, 0,
                                  p->props, std::vector<double>(bulges.begin(), bulges.end())};
    }
    case EntityKind::Text: {
        const TextData* t = store.text(h);
        return AddTextCommand{t->pos,   t->height, t->rotation, t->justify,
                              std::string(store.string_of(*t)), 0, t->props};
    }
    case EntityKind::Dimension: {
        const DimData* d = store.dimension(h);
        return AddDimensionCommand{static_cast<std::uint8_t>(d->type),
                                   d->a, d->b, d->line_pt, d->style, 0, d->props};
    }
    case EntityKind::Leader: {
        const LeaderData* l = store.leader(h);
        return AddLeaderCommand{l->tip, l->knee, l->text_height, l->style,
                                std::string(store.string_of(*l)), 0, l->props};
    }
    case EntityKind::Point:
    case EntityKind::Spline:
        break;
    }
    return AddLineCommand{};
}

EntityHandle add_command_to_store(GeometryStore& store, const Command& cmd, EntityProps fallback) {
    EntityHandle handle;
    const auto props_of = [&](const std::optional<EntityProps>& p) { return p ? *p : fallback; };
    std::visit(
        [&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, AddLineCommand>) {
                handle = store.add_line(c.a, c.b, props_of(c.props));
            } else if constexpr (std::is_same_v<T, AddPolylineCommand>) {
                handle = store.add_polyline(c.points, c.bulges, c.closed, props_of(c.props));
            } else if constexpr (std::is_same_v<T, AddCircleCommand>) {
                handle = store.add_circle(c.center, c.radius, props_of(c.props));
            } else if constexpr (std::is_same_v<T, AddArcCommand>) {
                handle =
                    store.add_arc(c.center, c.radius, c.start_angle, c.end_angle, props_of(c.props));
            } else if constexpr (std::is_same_v<T, AddTextCommand>) {
                handle = store.add_text(c.pos, c.height, c.rotation, c.justify, c.content,
                                        props_of(c.props));
            } else if constexpr (std::is_same_v<T, AddDimensionCommand>) {
                handle = store.add_dimension(static_cast<DimType>(c.type), c.a, c.b, c.line_pt,
                                             c.style, props_of(c.props));
            } else if constexpr (std::is_same_v<T, AddLeaderCommand>) {
                handle = store.add_leader(c.tip, c.knee, c.text_height, c.style, c.content,
                                          props_of(c.props));
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
        for (std::uint32_t i = 0; i < verts.size(); ++i) {
            push(out, verts[i], GripKind::Vertex, i);
        }
        break;
    }
    case EntityKind::Text: {
        const TextData* t = store.text(h);
        push(out, t->pos, GripKind::Move, 0);
        break;
    }
    case EntityKind::Dimension: {
        const DimData* d = store.dimension(h);
        const DimStyle* s = store.dimstyle(d->style);
        const DimGeometry g = compute_dim_geometry(*d, s != nullptr ? *s : DimStyle{}, Rgb{});
        const auto t = d->type;
        if (t == DimType::Radius || t == DimType::Diameter) {
            push(out, d->a, GripKind::Move, 0);    // centre -> moves the dim
            push(out, d->b, GripKind::DimDef, 1);  // edge -> re-measures R/diameter
            push(out, d->line_pt, GripKind::DimLine, 2); // leader/text placement
        } else if (t == DimType::Angular) {
            push(out, d->a, GripKind::DimDef, 0);  // vertex
            push(out, d->b, GripKind::DimDef, 1);  // ray 1
            push(out, d->line_pt, GripKind::DimDef, 2); // ray 2
        } else { // Linear / Aligned
            push(out, d->a, GripKind::DimDef, 0);
            push(out, d->b, GripKind::DimDef, 1);
            const Vec2 mid = g.dim_lines.size() >= 2 ? (g.dim_lines[0] + g.dim_lines[1]) * 0.5
                                                     : d->line_pt;
            push(out, mid, GripKind::DimLine, 2); // dim-line offset (value unchanged)
        }
        break;
    }
    case EntityKind::Leader: {
        const LeaderData* l = store.leader(h);
        push(out, l->tip, GripKind::Endpoint, 0);
        push(out, l->knee, GripKind::Move, 1);
        break;
    }
    case EntityKind::Point:
    case EntityKind::Spline:
        break;
    }
}

Command edit_for_grip_drag(const GeometryStore& store, EntityHandle h, std::uint32_t grip_index,
                           Vec2 newpos) {
    Command c = capture_entity(store, h);
    std::visit(
        [&](auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, AddLineCommand>) {
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
                if (grip_index < x.points.size()) {
                    x.points[grip_index] = newpos;
                }
            } else if constexpr (std::is_same_v<T, AddTextCommand>) {
                x.pos = newpos;
            } else if constexpr (std::is_same_v<T, AddDimensionCommand>) {
                const auto t = static_cast<DimType>(x.type);
                if ((t == DimType::Radius || t == DimType::Diameter) && grip_index == 0) {
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
            } else if constexpr (std::is_same_v<T, AddLeaderCommand>) {
                if (grip_index == 0) {
                    x.tip = newpos;
                } else {
                    x.knee = newpos;
                }
            }
        },
        c);
    return c;
}

} // namespace musacad::core
